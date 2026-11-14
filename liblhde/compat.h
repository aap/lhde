/* What differs between the systems lhde builds on (Linux with glibc or musl, FreeBSD 15), behind one set of names, so
 * the rest of the tree has no #ifdef of its own for them. Header only (static inline), C and C++: liblhde, luxshell,
 * wux and flxp include it as "liblhde/compat.h"; it is not installed.
 *
 *   lhde_progname()                  argv[0]'s basename (program_invocation_short_name; getprogname)
 *   lhde_self_exe(buf, n)            the running binary's path (/proc/self/exe; sysctl kern.proc.pathname)
 *   lhde_pid_exe(pid, buf, n)        another process's binary
 *   lhde_xattr_get/set/list/remove   extended attributes by their Linux names (user.NAME): on FreeBSD NAME in the user
 *   lhde_fxattr_* lhde_lxattr_*      namespace (extattr(2)), so `getextattr user lux.desc FILE` shows user.lux.desc;
 *                                    the f* forms take a descriptor, the l* forms do not follow a symlink. A list is in
 *                                    Linux's form ("user.a\0user.b\0"); FreeBSD lists the user namespace only. A value
 *                                    that is not there fails with LHDE_ENOATTR (Linux's ENODATA, FreeBSD's ENOATTR; libc++
 *                                    defines its own ENODATA there), a buffer too small with ERANGE.
 *   lhde_fs_is_remote(path)          the file system holding path is a network one (NFS, SMB, 9p, sshfs, ...)
 *   lhde_mount_each(fn, ctx)         every mount: device, mount point, file system type
 *   lhde_opath_chown, _chmod,        owner, mode and times of the file an O_PATH descriptor holds (which take neither
 *   _utimens(fd, ...)                fchown nor fchmod: /proc/self/fd/N; FreeBSD's AT_EMPTY_PATH)
 *   lhde_parent_death_signal(sig)    sig to this process when its parent exits (PR_SET_PDEATHSIG; PROC_PDEATHSIG_CTL)
 *   lhde_proc_pids, _comm, _read     processes: their ids, command names (LHDE_COMM_MAX bytes), environment and
 *                                    arguments (NUL-separated, malloc'd) (/proc; sysctl kern.proc)
 *   lhde_gsbase_set / _get           the GS segment base of this thread, x86-64 (arch_prctl; amd64_set_gsbase)
 */
#ifndef LHDE_COMPAT_H
#define LHDE_COMPAT_H

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#if defined(__x86_64__)
#include <asm/prctl.h>
#endif
#define LHDE_COMM_MAX 15
#define LHDE_ENOATTR ENODATA
#ifndef __cplusplus
extern char *program_invocation_short_name;   /* <errno.h> declares it only under _GNU_SOURCE */
#endif
#elif defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/extattr.h>
#include <sys/mount.h>
#include <sys/procctl.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#if defined(__x86_64__)
#include <machine/sysarch.h>
#endif
#define LHDE_ENOATTR ENOATTR
#define LHDE_COMM_MAX MAXCOMLEN
#else
#error "liblhde/compat.h: Linux or FreeBSD"
#endif

/* ---- the program ---------------------------------------------------------------------------------------------------- */

static inline const char *lhde_progname(void)
{
#if defined(__linux__)
    return program_invocation_short_name;
#else
    return getprogname();
#endif
}

#if defined(__FreeBSD__)
static inline ssize_t lhde__sysctl_string(int *mib, unsigned miblen, char *buf, size_t n)
{
    size_t len = n;
    if (n == 0 || sysctl(mib, miblen, buf, &len, NULL, 0) != 0) return -1;
    if (len == 0) { errno = ENOENT; return -1; }
    if (len >= n) len = n - 1;
    buf[len] = 0;
    return (ssize_t)strlen(buf);
}
#endif

/* the binary process pid runs (0: this one), NUL-terminated in buf; its length, -1 when unknown */
static inline ssize_t lhde_pid_exe(pid_t pid, char *buf, size_t n)
{
    if (n == 0) { errno = EINVAL; return -1; }
#if defined(__linux__)
    char link[64];
    if (pid) snprintf(link, sizeof link, "/proc/%lu/exe", (unsigned long)pid);
    else snprintf(link, sizeof link, "/proc/self/exe");
    ssize_t k = readlink(link, buf, n - 1);
    if (k < 0) return -1;
    buf[k] = 0;
    return k;
#else
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, pid ? (int)pid : -1 };
    return lhde__sysctl_string(mib, 4, buf, n);
#endif
}

static inline ssize_t lhde_self_exe(char *buf, size_t n) { return lhde_pid_exe(0, buf, n); }

/* ---- extended attributes -------------------------------------------------------------------------------------------- */

#if defined(__linux__)
static inline ssize_t lhde_xattr_get(const char *p, const char *nm, void *v, size_t n)       { return getxattr(p, nm, v, n); }
static inline ssize_t lhde_lxattr_get(const char *p, const char *nm, void *v, size_t n)      { return lgetxattr(p, nm, v, n); }
static inline ssize_t lhde_fxattr_get(int fd, const char *nm, void *v, size_t n)             { return fgetxattr(fd, nm, v, n); }
static inline int lhde_xattr_set(const char *p, const char *nm, const void *v, size_t n)     { return setxattr(p, nm, v, n, 0); }
static inline int lhde_lxattr_set(const char *p, const char *nm, const void *v, size_t n)    { return lsetxattr(p, nm, v, n, 0); }
static inline int lhde_fxattr_set(int fd, const char *nm, const void *v, size_t n)           { return fsetxattr(fd, nm, v, n, 0); }
static inline ssize_t lhde_xattr_list(const char *p, char *buf, size_t n)                    { return listxattr(p, buf, n); }
static inline ssize_t lhde_lxattr_list(const char *p, char *buf, size_t n)                   { return llistxattr(p, buf, n); }
static inline ssize_t lhde_fxattr_list(int fd, char *buf, size_t n)                          { return flistxattr(fd, buf, n); }
static inline int lhde_xattr_remove(const char *p, const char *nm)                           { return removexattr(p, nm); }
#else
/* the file an attribute call is about: a path (followed, or not: link) or a descriptor */
struct lhde__xt { const char *path; int fd; int link; };

static inline int lhde__xattr_ns(const char *name, const char **attr)
{
    if (strncmp(name, "user.", 5) == 0 && name[5]) { *attr = name + 5; return EXTATTR_NAMESPACE_USER; }
    if (strncmp(name, "system.", 7) == 0 && name[7]) { *attr = name + 7; return EXTATTR_NAMESPACE_SYSTEM; }
    errno = ENOTSUP;   /* security.*, trusted.*: no FreeBSD namespace */
    return -1;
}

static inline ssize_t lhde__xget(struct lhde__xt t, const char *name, void *v, size_t n)
{
    const char *a;
    int ns = lhde__xattr_ns(name, &a);
    if (ns < 0) return -1;
#define LHDE__XGET(V, N) (t.fd >= 0 ? extattr_get_fd(t.fd, ns, a, V, N) : t.link ? extattr_get_link(t.path, ns, a, V, N) \
                                                                                 : extattr_get_file(t.path, ns, a, V, N))
    ssize_t size = LHDE__XGET(NULL, 0);
    if (size < 0 || !v || n == 0) return size;
    if ((size_t)size > n) { errno = ERANGE; return -1; }   /* extattr would cut the value short */
    return LHDE__XGET(v, n);
#undef LHDE__XGET
}

static inline int lhde__xset(struct lhde__xt t, const char *name, const void *v, size_t n)
{
    const char *a;
    int ns = lhde__xattr_ns(name, &a);
    if (ns < 0) return -1;
    ssize_t r = t.fd >= 0 ? extattr_set_fd(t.fd, ns, a, v, n) : t.link ? extattr_set_link(t.path, ns, a, v, n)
                                                                        : extattr_set_file(t.path, ns, a, v, n);
    return r < 0 ? -1 : 0;
}

/* extattr's list (a length byte, then the name, per attribute) as Linux's: "user.NAME\0" per attribute */
static inline ssize_t lhde__xlist(struct lhde__xt t, char *buf, size_t n)
{
    int ns = EXTATTR_NAMESPACE_USER;
#define LHDE__XLIST(V, N) (t.fd >= 0 ? extattr_list_fd(t.fd, ns, V, N) : t.link ? extattr_list_link(t.path, ns, V, N) \
                                                                               : extattr_list_file(t.path, ns, V, N))
    for (int tries = 0; tries < 8; tries++) {
        ssize_t raw = LHDE__XLIST(NULL, 0);
        if (raw <= 0) return raw;
        unsigned char *r = (unsigned char *)malloc((size_t)raw);
        if (!r) { errno = ENOMEM; return -1; }
        ssize_t got = LHDE__XLIST(r, (size_t)raw);
        if (got < 0) { free(r); return -1; }
        if (got > raw) { free(r); continue; }   /* grew in between */
        size_t need = 0;
        for (ssize_t i = 0; i < got; i += 1 + r[i]) need += 5 + r[i] + 1;
        if (!buf || n == 0) { free(r); return (ssize_t)need; }
        if (need > n) { free(r); errno = ERANGE; return -1; }
        char *o = buf;
        for (ssize_t i = 0; i < got && i + r[i] < got; i += 1 + r[i]) {
            memcpy(o, "user.", 5);
            memcpy(o + 5, r + i + 1, r[i]);
            o[5 + r[i]] = 0;
            o += 5 + r[i] + 1;
        }
        free(r);
        return (ssize_t)(o - buf);
    }
#undef LHDE__XLIST
    errno = ERANGE;
    return -1;
}

static inline struct lhde__xt lhde__xpath(const char *p, int link) { struct lhde__xt t = { p, -1, link }; return t; }
static inline struct lhde__xt lhde__xfd(int fd) { struct lhde__xt t = { NULL, fd, 0 }; return t; }

static inline ssize_t lhde_xattr_get(const char *p, const char *nm, void *v, size_t n)       { return lhde__xget(lhde__xpath(p, 0), nm, v, n); }
static inline ssize_t lhde_lxattr_get(const char *p, const char *nm, void *v, size_t n)      { return lhde__xget(lhde__xpath(p, 1), nm, v, n); }
static inline ssize_t lhde_fxattr_get(int fd, const char *nm, void *v, size_t n)             { return lhde__xget(lhde__xfd(fd), nm, v, n); }
static inline int lhde_xattr_set(const char *p, const char *nm, const void *v, size_t n)     { return lhde__xset(lhde__xpath(p, 0), nm, v, n); }
static inline int lhde_lxattr_set(const char *p, const char *nm, const void *v, size_t n)    { return lhde__xset(lhde__xpath(p, 1), nm, v, n); }
static inline int lhde_fxattr_set(int fd, const char *nm, const void *v, size_t n)           { return lhde__xset(lhde__xfd(fd), nm, v, n); }
static inline ssize_t lhde_xattr_list(const char *p, char *buf, size_t n)                    { return lhde__xlist(lhde__xpath(p, 0), buf, n); }
static inline ssize_t lhde_lxattr_list(const char *p, char *buf, size_t n)                   { return lhde__xlist(lhde__xpath(p, 1), buf, n); }
static inline ssize_t lhde_fxattr_list(int fd, char *buf, size_t n)                          { return lhde__xlist(lhde__xfd(fd), buf, n); }
static inline int lhde_xattr_remove(const char *p, const char *nm)
{
    const char *a;
    int ns = lhde__xattr_ns(nm, &a);
    return ns < 0 ? -1 : extattr_delete_file(p, ns, a);
}
#endif

/* ---- file systems --------------------------------------------------------------------------------------------------- */

/* fn(device, mount point, type, ctx) for every mount, in the system's order; fn returning nonzero stops the walk */
static inline void lhde_mount_each(int (*fn)(const char *dev, const char *dir, const char *type, void *ctx), void *ctx)
{
#if defined(__linux__)
    FILE *m = fopen("/proc/mounts", "r");
    if (!m) return;
    char line[1024], dev[256], mnt[512], fs[64];
    while (fgets(line, sizeof line, m))
        if (sscanf(line, "%255s %511s %63s", dev, mnt, fs) == 3 && fn(dev, mnt, fs, ctx)) break;
    fclose(m);
#else
    struct statfs *s;
    int k = getmntinfo(&s, MNT_NOWAIT);
    for (int i = 0; i < k; i++)
        if (fn(s[i].f_mntfromname, s[i].f_mntonname, s[i].f_fstypename, ctx)) break;
#endif
}

/* FUSE file systems that are a network share (Linux's fuse.NAME, FreeBSD's fusefs.NAME) */
static inline int lhde__fuse_remote(const char *sub)
{
    static const char *remote[] = { "sshfs", "rclone", "curlftpfs", "gvfsd-fuse", "davfs", "s3fs", "gcsfuse", "smbnetfs",
                                    "goofys", "httpdirfs" };
    for (size_t i = 0; i < sizeof remote / sizeof *remote; i++) if (strcmp(sub, remote[i]) == 0) return 1;
    return 0;
}

#if defined(__linux__)
struct lhde__mount_best { const char *path; size_t best; char type[64]; };
static inline int lhde__mount_under(const char *dev, const char *mnt, const char *type, void *ctx)
{
    struct lhde__mount_best *b = (struct lhde__mount_best *)ctx;
    size_t ml = strlen(mnt);
    (void)dev;
    int under = strcmp(b->path, mnt) == 0 ||
                (strncmp(b->path, mnt, ml) == 0 && (strcmp(mnt, "/") == 0 || b->path[ml] == '/'));
    if (under && ml >= b->best) { b->best = ml; snprintf(b->type, sizeof b->type, "%s", type); }
    return 0;
}
#endif

/* 1 when path is on a network file system, 0 when local or unknown */
static inline int lhde_fs_is_remote(const char *path)
{
    if (!path || path[0] != '/') return 0;
    struct statfs fs;
    if (statfs(path, &fs) != 0) return 0;
#if defined(__linux__)
    switch ((unsigned long)fs.f_type) {
    case 0x6969:        /* NFS */
    case 0x517b:        /* SMB */
    case 0xff534d42:    /* CIFS */
    case 0xfe534d42:    /* SMB2 */
    case 0x01021997:    /* 9p */
    case 0x5346414f:    /* AFS */
    case 0x00c36400:    /* Ceph */
    case 0x73757245:    /* Coda */
    case 0x564c:        /* NCP */
        return 1;
    case 0x65735546:    /* FUSE: what is behind it (fuseblk, ntfs-3g, is local) */
        break;
    default:
        return 0;
    }
    /* the type of the longest mount point over path, in /proc/self/mounts */
    char real[PATH_MAX];
    struct lhde__mount_best b;
    b.path = realpath(path, real) ? real : path;
    b.best = 0;
    b.type[0] = 0;
    lhde_mount_each(lhde__mount_under, &b);
    return strncmp(b.type, "fuse.", 5) == 0 && lhde__fuse_remote(b.type + 5);
#else
    const char *t = fs.f_fstypename;
    if (strcmp(t, "nfs") == 0 || strcmp(t, "smbfs") == 0 || strcmp(t, "p9fs") == 0) return 1;
    return strncmp(t, "fusefs.", 7) == 0 && lhde__fuse_remote(t + 7);
#endif
}

/* ---- O_PATH descriptors --------------------------------------------------------------------------------------------- */

#if defined(__linux__)
#define LHDE__FDPATH(fd) char pp[32]; snprintf(pp, sizeof pp, "/proc/self/fd/%d", fd)
static inline int lhde_opath_chown(int fd, uid_t u, gid_t g)  { LHDE__FDPATH(fd); return chown(pp, u, g); }
static inline int lhde_opath_chmod(int fd, mode_t m)          { LHDE__FDPATH(fd); return chmod(pp, m); }
static inline int lhde_opath_utimens(int fd, const struct timespec ts[2]) { LHDE__FDPATH(fd); return utimensat(AT_FDCWD, pp, ts, 0); }
#undef LHDE__FDPATH
#else
static inline int lhde_opath_chown(int fd, uid_t u, gid_t g)  { return fchownat(fd, "", u, g, AT_EMPTY_PATH); }
static inline int lhde_opath_chmod(int fd, mode_t m)          { return fchmodat(fd, "", m, AT_EMPTY_PATH); }
static inline int lhde_opath_utimens(int fd, const struct timespec ts[2]) { return utimensat(fd, "", ts, AT_EMPTY_PATH); }
#endif

/* ---- processes ------------------------------------------------------------------------------------------------------ */

static inline void lhde_parent_death_signal(int sig)
{
#if defined(__linux__)
    prctl(PR_SET_PDEATHSIG, sig);
#else
    procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &sig);
#endif
}

/* the ids of the processes there are, up to max into out; how many */
static inline int lhde_proc_pids(pid_t *out, int max)
{
    int k = 0;
#if defined(__linux__)
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    while (k < max && (e = readdir(d))) {
        char *end;
        long p = strtol(e->d_name, &end, 10);
        if (*e->d_name && !*end && p > 0) out[k++] = (pid_t)p;
    }
    closedir(d);
#else
    int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC };
    size_t len = 0;
    if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0) return 0;
    len += len / 8 + 16 * sizeof(struct kinfo_proc);   /* room for processes started in between */
    struct kinfo_proc *kp = (struct kinfo_proc *)malloc(len);
    if (!kp) return 0;
    if (sysctl(mib, 3, kp, &len, NULL, 0) == 0)
        for (size_t i = 0; i < len / sizeof *kp && k < max; i++) out[k++] = kp[i].ki_pid;
    free(kp);
#endif
    return k;
}

/* the command name of pid (at most LHDE_COMM_MAX bytes) into buf; 0, or -1 when it is gone */
static inline int lhde_proc_comm(pid_t pid, char *buf, size_t n)
{
    if (n == 0) return -1;
#if defined(__linux__)
    char p[64];
    snprintf(p, sizeof p, "/proc/%lu/comm", (unsigned long)pid);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)n, f)) { fclose(f); return -1; }
    fclose(f);
    buf[strcspn(buf, "\n")] = 0;
    return 0;
#else
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    struct kinfo_proc kp;
    size_t len = sizeof kp;
    if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 || len < sizeof kp) return -1;
    snprintf(buf, n, "%s", kp.ki_comm);
    return 0;
#endif
}

enum { LHDE_PROC_ENVIRON, LHDE_PROC_ARGS };

/* pid's environment or arguments, NUL-separated, malloc'd (free it), its length in *len; NULL when not readable */
static inline char *lhde_proc_read(pid_t pid, int what, size_t *len)
{
    *len = 0;
#if defined(__linux__)
    char p[64];
    snprintf(p, sizeof p, "/proc/%lu/%s", (unsigned long)pid, what == LHDE_PROC_ENVIRON ? "environ" : "cmdline");
    FILE *f = fopen(p, "r");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *b = (char *)malloc(cap);
    size_t r;
    while (b && (r = fread(b + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) { char *nb = (char *)realloc(b, cap * 2); if (!nb) { free(b); b = NULL; break; } b = nb; cap *= 2; }
    }
    fclose(f);
    if (b) *len = n;
    return b;
#else
    int mib[4] = { CTL_KERN, KERN_PROC, what == LHDE_PROC_ENVIRON ? KERN_PROC_ENV : KERN_PROC_ARGS, (int)pid };
    size_t n = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) != 0) return NULL;
    char *b = (char *)malloc(n + 1);
    if (!b) return NULL;
    if (sysctl(mib, 4, b, &n, NULL, 0) != 0) { free(b); return NULL; }
    *len = n;
    return b;
#endif
}

/* ---- x86-64: the GS base (the win32 shim's TEB) --------------------------------------------------------------------- */

#if defined(__x86_64__)
static inline int lhde_gsbase_set(void *base)
{
#if defined(__linux__)
    return (int)syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)base);
#else
    return amd64_set_gsbase(base);
#endif
}

static inline void *lhde_gsbase_get(void)
{
#if defined(__linux__)
    unsigned long gs = 0;
    return syscall(SYS_arch_prctl, ARCH_GET_GS, &gs) == 0 ? (void *)gs : NULL;
#else
    void *gs = NULL;
    return amd64_get_gsbase(&gs) == 0 ? gs : NULL;
#endif
}
#endif

#endif
