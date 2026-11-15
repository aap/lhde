/* slowfs: an LD_PRELOAD shim that makes one directory tree as slow as a network mount, for the tests that measure how flxp
 * behaves on one (tests/headless/slowpath.sh). Every call about a path under $LHDE_SLOW_PREFIX waits $LHDE_SLOW_MS
 * milliseconds (default 50) before it is made, as a round trip to a file server would:
 *
 *   stat, lstat, fstatat, statx, access, faccessat, realpath          one round trip per call
 *   open, openat, fopen (and their 64 forms), opendir                 one per call
 *   read, pread, fread on a descriptor or FILE opened under it        one per new 1 MiB window of the file (the client
 *                                                                     reads ahead in rsize blocks), the first included
 *   readdir                                                           one per 128 entries (a READDIR reply's batch)
 *   getxattr, lgetxattr, listxattr, llistxattr (Linux);
 *   extattr_get_file, extattr_get_link, extattr_list_file, extattr_list_link (FreeBSD)   one per call
 *
 * With LHDE_SLOW_REMOTE=1, statfs under the prefix answers as an NFS mount does (Linux's f_type 0x6969, FreeBSD's
 * f_fstypename "nfs"), so lhde_fs_is_remote() takes the tree for a network one. With $LHDE_SLOW_LOG every call is logged
 * there, one line each: milliseconds since the first call, the call, the path, and for close the bytes read through the
 * descriptor ("123.4 close /p/x.jpg 65536"). Calls libc makes inside itself are not seen (an LD_PRELOAD sees only the
 * program's own calls), which is why fopen, opendir and realpath are wrapped as a whole. Portable to FreeBSD:
 * dlsym(RTLD_NEXT) only, no /proc. */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/statfs.h>
#include <sys/xattr.h>
#else
#include <sys/extattr.h>
#endif

#define MAXFD 65536
#define WINDOW (1 << 20)

static const char *g_prefix;
static size_t g_plen;
static long g_ms = 50;
static int g_remote;
static int g_log = -1;
static struct timespec g_t0;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* the descriptors opened under the prefix: their path, the bytes read and the last 1 MiB window read */
struct fdinfo { char *path; uint64_t bytes, pos; int64_t window; };
static struct fdinfo g_fds[MAXFD];
/* directory streams: their path and the entries read */
struct dirinfo { DIR *d; char *path; unsigned long entries; };
#define MAXDIRS 256
static struct dirinfo g_dirs[MAXDIRS];

#define REAL(ret, name, args) static ret(*real)args; if (!real) real = (ret(*)args)dlsym(RTLD_NEXT, name)

static void init(void)
{
    g_prefix = getenv("LHDE_SLOW_PREFIX");
    if (g_prefix && !*g_prefix) g_prefix = NULL;
    g_plen = g_prefix ? strlen(g_prefix) : 0;
    while (g_plen > 1 && g_prefix[g_plen - 1] == '/') g_plen--;
    const char *ms = getenv("LHDE_SLOW_MS");
    if (ms && *ms) g_ms = atol(ms);
    const char *r = getenv("LHDE_SLOW_REMOTE");
    g_remote = r && atoi(r) > 0;
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    const char *log = getenv("LHDE_SLOW_LOG");
    if (log && *log) {
        REAL(int, "open", (const char *, int, ...));
        g_log = real(log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }
}

static int under(const char *path)
{
    pthread_once(&g_once, init);
    if (!g_prefix || !path || path[0] != '/') return 0;
    return strncmp(path, g_prefix, g_plen) == 0 && (path[g_plen] == '/' || path[g_plen] == 0);
}

static void say(const char *call, const char *path, long long n)
{
    if (g_log < 0) return;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    double ms = (double)(t.tv_sec - g_t0.tv_sec) * 1000 + (double)(t.tv_nsec - g_t0.tv_nsec) / 1e6;
    char line[PATH_MAX + 96];
    int k = n >= 0 ? snprintf(line, sizeof line, "%.1f %s %s %lld\n", ms, call, path, n)
                   : snprintf(line, sizeof line, "%.1f %s %s\n", ms, call, path);
    if (k > 0) { ssize_t w = write(g_log, line, (size_t)k < sizeof line ? (size_t)k : sizeof line - 1); (void)w; }
}

static void trip(void)
{
    struct timespec d = { g_ms / 1000, (g_ms % 1000) * 1000000L };
    while (nanosleep(&d, &d) != 0 && errno == EINTR) {}
}

/* a call about path: logged and delayed when it is under the prefix */
static int slow(const char *call, const char *path)
{
    if (!under(path)) return 0;
    int e = errno;
    say(call, path, -1);
    trip();
    errno = e;
    return 1;
}

/* the path of dirfd + path for the *at calls (an absolute path, or relative to a descriptor opened under the prefix) */
static int slow_at(const char *call, int dirfd, const char *path)
{
    if (path && path[0] == '/') return slow(call, path);
    pthread_once(&g_once, init);
    if (dirfd < 0 || dirfd >= MAXFD || !g_fds[dirfd].path) return 0;
    int e = errno;
    char full[PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", g_fds[dirfd].path, path ? path : "");
    say(call, full, -1);
    trip();
    errno = e;
    return 1;
}

static void track(int fd, const char *path)
{
    if (fd < 0 || fd >= MAXFD || !under(path)) return;
    pthread_mutex_lock(&g_lock);
    free(g_fds[fd].path);
    g_fds[fd].path = strdup(path);
    g_fds[fd].bytes = g_fds[fd].pos = 0;
    g_fds[fd].window = -1;
    pthread_mutex_unlock(&g_lock);
}

static void untrack(int fd)
{
    if (fd < 0 || fd >= MAXFD) return;
    pthread_mutex_lock(&g_lock);
    char *p = g_fds[fd].path;
    uint64_t n = g_fds[fd].bytes;
    g_fds[fd].path = NULL;
    pthread_mutex_unlock(&g_lock);
    if (p) { say("close", p, (long long)n); free(p); }
}

/* before a read of len bytes at off (-1: the descriptor's position): a round trip when it reaches a new window */
static void before_read(int fd, int64_t off)
{
    if (fd < 0 || fd >= MAXFD) return;
    pthread_mutex_lock(&g_lock);
    int due = 0;
    if (g_fds[fd].path) {
        int64_t w = (int64_t)((off < 0 ? g_fds[fd].pos : (uint64_t)off) / WINDOW);
        if (w != g_fds[fd].window) { g_fds[fd].window = w; due = 1; }
    }
    pthread_mutex_unlock(&g_lock);
    if (due) { int e = errno; trip(); errno = e; }
}

static void after_read(int fd, ssize_t n, int positioned)
{
    if (fd < 0 || fd >= MAXFD || n <= 0) return;
    pthread_mutex_lock(&g_lock);
    if (g_fds[fd].path) { g_fds[fd].bytes += (uint64_t)n; if (!positioned) g_fds[fd].pos += (uint64_t)n; }
    pthread_mutex_unlock(&g_lock);
}

/* ---- the calls ---------------------------------------------------------------------------------------------------- */

int stat(const char *p, struct stat *st) { REAL(int, "stat", (const char *, struct stat *)); slow("stat", p); return real(p, st); }
int lstat(const char *p, struct stat *st) { REAL(int, "lstat", (const char *, struct stat *)); slow("lstat", p); return real(p, st); }
int fstatat(int fd, const char *p, struct stat *st, int fl)
{
    REAL(int, "fstatat", (int, const char *, struct stat *, int));
    slow_at("fstatat", fd, p);
    return real(fd, p, st, fl);
}
int access(const char *p, int m) { REAL(int, "access", (const char *, int)); slow("access", p); return real(p, m); }
int faccessat(int fd, const char *p, int m, int fl)
{
    REAL(int, "faccessat", (int, const char *, int, int));
    slow_at("faccessat", fd, p);
    return real(fd, p, m, fl);
}
char *realpath(const char *p, char *out)
{
    REAL(char *, "realpath", (const char *, char *));
    slow("realpath", p);
    return real(p, out);
}

#if defined(__linux__)
int stat64(const char *p, struct stat64 *st) { REAL(int, "stat64", (const char *, struct stat64 *)); slow("stat", p); return real(p, st); }
int lstat64(const char *p, struct stat64 *st) { REAL(int, "lstat64", (const char *, struct stat64 *)); slow("lstat", p); return real(p, st); }
int fstatat64(int fd, const char *p, struct stat64 *st, int fl)
{
    REAL(int, "fstatat64", (int, const char *, struct stat64 *, int));
    slow_at("fstatat", fd, p);
    return real(fd, p, st, fl);
}
int statx(int fd, const char *p, int fl, unsigned mask, struct statx *st)
{
    REAL(int, "statx", (int, const char *, int, unsigned, struct statx *));
    slow_at("statx", fd, p);
    return real(fd, p, fl, mask, st);
}
/* glibc before 2.33 has stat() call these */
int __xstat(int v, const char *p, struct stat *st) { REAL(int, "__xstat", (int, const char *, struct stat *)); slow("stat", p); return real(v, p, st); }
int __lxstat(int v, const char *p, struct stat *st) { REAL(int, "__lxstat", (int, const char *, struct stat *)); slow("lstat", p); return real(v, p, st); }

ssize_t getxattr(const char *p, const char *n, void *v, size_t s)
{
    REAL(ssize_t, "getxattr", (const char *, const char *, void *, size_t));
    slow("getxattr", p);
    return real(p, n, v, s);
}
ssize_t lgetxattr(const char *p, const char *n, void *v, size_t s)
{
    REAL(ssize_t, "lgetxattr", (const char *, const char *, void *, size_t));
    slow("lgetxattr", p);
    return real(p, n, v, s);
}
ssize_t listxattr(const char *p, char *b, size_t s) { REAL(ssize_t, "listxattr", (const char *, char *, size_t)); slow("listxattr", p); return real(p, b, s); }
ssize_t llistxattr(const char *p, char *b, size_t s) { REAL(ssize_t, "llistxattr", (const char *, char *, size_t)); slow("llistxattr", p); return real(p, b, s); }

static int nfs_statfs(const char *p, struct statfs *fs, int r)
{
    if (r == 0 && g_remote && under(p)) fs->f_type = 0x6969;   /* NFS_SUPER_MAGIC */
    return r;
}
int statfs(const char *p, struct statfs *fs) { REAL(int, "statfs", (const char *, struct statfs *)); slow("statfs", p); return nfs_statfs(p, fs, real(p, fs)); }
int statfs64(const char *p, struct statfs64 *fs)
{
    REAL(int, "statfs64", (const char *, struct statfs64 *));
    slow("statfs", p);
    int r = real(p, fs);
    if (r == 0 && g_remote && under(p)) fs->f_type = 0x6969;
    return r;
}
#else
ssize_t extattr_get_file(const char *p, int ns, const char *n, void *v, size_t s)
{
    REAL(ssize_t, "extattr_get_file", (const char *, int, const char *, void *, size_t));
    slow("getxattr", p);
    return real(p, ns, n, v, s);
}
ssize_t extattr_get_link(const char *p, int ns, const char *n, void *v, size_t s)
{
    REAL(ssize_t, "extattr_get_link", (const char *, int, const char *, void *, size_t));
    slow("lgetxattr", p);
    return real(p, ns, n, v, s);
}
ssize_t extattr_list_file(const char *p, int ns, void *v, size_t s)
{
    REAL(ssize_t, "extattr_list_file", (const char *, int, void *, size_t));
    slow("listxattr", p);
    return real(p, ns, v, s);
}
ssize_t extattr_list_link(const char *p, int ns, void *v, size_t s)
{
    REAL(ssize_t, "extattr_list_link", (const char *, int, void *, size_t));
    slow("llistxattr", p);
    return real(p, ns, v, s);
}
int statfs(const char *p, struct statfs *fs)
{
    REAL(int, "statfs", (const char *, struct statfs *));
    slow("statfs", p);
    int r = real(p, fs);
    if (r == 0 && g_remote && under(p)) snprintf(fs->f_fstypename, sizeof fs->f_fstypename, "nfs");
    return r;
}
#endif

static mode_t mode_arg(int flags, va_list ap)
{
#ifdef O_TMPFILE
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) return (mode_t)va_arg(ap, int);
#else
    if (flags & O_CREAT) return (mode_t)va_arg(ap, int);
#endif
    return 0;
}

int open(const char *p, int flags, ...)
{
    REAL(int, "open", (const char *, int, ...));
    va_list ap; va_start(ap, flags); mode_t m = mode_arg(flags, ap); va_end(ap);
    slow("open", p);
    int fd = real(p, flags, m);
    track(fd, p);
    return fd;
}
int openat(int dirfd, const char *p, int flags, ...)
{
    REAL(int, "openat", (int, const char *, int, ...));
    va_list ap; va_start(ap, flags); mode_t m = mode_arg(flags, ap); va_end(ap);
    slow_at("openat", dirfd, p);
    int fd = real(dirfd, p, flags, m);
    if (p && p[0] == '/') track(fd, p);
    else if (fd >= 0 && dirfd >= 0 && dirfd < MAXFD && g_fds[dirfd].path) {
        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", g_fds[dirfd].path, p);
        track(fd, full);
    }
    return fd;
}
#if defined(__linux__)
int open64(const char *p, int flags, ...)
{
    REAL(int, "open64", (const char *, int, ...));
    va_list ap; va_start(ap, flags); mode_t m = mode_arg(flags, ap); va_end(ap);
    slow("open", p);
    int fd = real(p, flags, m);
    track(fd, p);
    return fd;
}
int openat64(int dirfd, const char *p, int flags, ...)
{
    REAL(int, "openat64", (int, const char *, int, ...));
    va_list ap; va_start(ap, flags); mode_t m = mode_arg(flags, ap); va_end(ap);
    slow_at("openat", dirfd, p);
    int fd = real(dirfd, p, flags, m);
    if (p && p[0] == '/') track(fd, p);
    return fd;
}
FILE *fopen64(const char *p, const char *mode)
{
    REAL(FILE *, "fopen64", (const char *, const char *));
    slow("open", p);
    FILE *f = real(p, mode);
    if (f) track(fileno(f), p);
    return f;
}
#endif
FILE *fopen(const char *p, const char *mode)
{
    REAL(FILE *, "fopen", (const char *, const char *));
    slow("open", p);
    FILE *f = real(p, mode);
    if (f) track(fileno(f), p);
    return f;
}

int close(int fd) { REAL(int, "close", (int)); untrack(fd); return real(fd); }
int fclose(FILE *f) { REAL(int, "fclose", (FILE *)); if (f) untrack(fileno(f)); return real(f); }

ssize_t read(int fd, void *b, size_t n)
{
    REAL(ssize_t, "read", (int, void *, size_t));
    before_read(fd, -1);
    ssize_t k = real(fd, b, n);
    after_read(fd, k, 0);
    return k;
}
ssize_t pread(int fd, void *b, size_t n, off_t off)
{
    REAL(ssize_t, "pread", (int, void *, size_t, off_t));
    before_read(fd, (int64_t)off);
    ssize_t k = real(fd, b, n, off);
    after_read(fd, k, 1);
    return k;
}
#if defined(__linux__)
ssize_t pread64(int fd, void *b, size_t n, off64_t off)
{
    REAL(ssize_t, "pread64", (int, void *, size_t, off64_t));
    before_read(fd, (int64_t)off);
    ssize_t k = real(fd, b, n, off);
    after_read(fd, k, 1);
    return k;
}
#endif
size_t fread(void *b, size_t size, size_t n, FILE *f)
{
    REAL(size_t, "fread", (void *, size_t, size_t, FILE *));
    int fd = f ? fileno(f) : -1;
    long at = fd >= 0 && fd < MAXFD && g_fds[fd].path ? ftell(f) : -1;
    if (at >= 0) before_read(fd, (int64_t)at);
    size_t k = real(b, size, n, f);
    if (at >= 0) after_read(fd, (ssize_t)(k * size), 1);
    return k;
}

static struct dirinfo *dir_of(DIR *d)
{
    for (int i = 0; i < MAXDIRS; i++) if (g_dirs[i].d == d) return &g_dirs[i];
    return NULL;
}

DIR *opendir(const char *p)
{
    REAL(DIR *, "opendir", (const char *));
    int s = slow("opendir", p);
    DIR *d = real(p);
    if (d && s) {
        pthread_mutex_lock(&g_lock);
        struct dirinfo *x = dir_of(NULL);
        if (x) { x->d = d; x->path = strdup(p); x->entries = 0; }
        pthread_mutex_unlock(&g_lock);
    }
    return d;
}

static void dir_entry(DIR *d)
{
    pthread_mutex_lock(&g_lock);
    struct dirinfo *x = dir_of(d);
    int due = x && x->entries++ % 128 == 127;
    char *p = x && due ? strdup(x->path) : NULL;
    pthread_mutex_unlock(&g_lock);
    if (p) { int e = errno; say("readdir", p, -1); trip(); errno = e; free(p); }
}

struct dirent *readdir(DIR *d) { REAL(struct dirent *, "readdir", (DIR *)); dir_entry(d); return real(d); }
#if defined(__linux__)
struct dirent64 *readdir64(DIR *d) { REAL(struct dirent64 *, "readdir64", (DIR *)); dir_entry(d); return real(d); }
#endif

int closedir(DIR *d)
{
    REAL(int, "closedir", (DIR *));
    pthread_mutex_lock(&g_lock);
    struct dirinfo *x = dir_of(d);
    if (x) { free(x->path); x->path = NULL; x->d = NULL; }
    pthread_mutex_unlock(&g_lock);
    return real(d);
}
