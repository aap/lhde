/* liblhcat: what a walk enters: the exclude globs, the mount table, and the plan of an index (a dry run). */
#define _GNU_SOURCE
#include "internal.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif

/* ---- globs --------------------------------------------------------------------------------------------------------- */

/* a "[...]" set at p (after the '['): *matched, and the pattern after the ']'; NULL when there is no ']' */
static const char *set_match(const char *p, unsigned char ch, int *matched)
{
    int neg = *p == '!' || *p == '^';
    if (neg) p++;
    int in = 0;
    const char *start = p;
    while (*p && (*p != ']' || p == start)) {
        unsigned char lo = (unsigned char)*p, hi = lo;
        if (p[1] == '-' && p[2] && p[2] != ']') { hi = (unsigned char)p[2]; p += 3; }
        else p++;
        if (ch >= lo && ch <= hi) in = 1;
    }
    if (*p != ']') return NULL;
    *matched = in != neg;
    return p + 1;
}

/* pattern p against the whole of s: '*' and '?' within a name, "**" followed by '/' (or ending the pattern) any number
   of whole names */
static int gmatch(const char *p, const char *s)
{
    for (;;) {
        if (p[0] == '*' && p[1] == '*' && (p[2] == '/' || !p[2])) {
            if (!p[2]) return 1;
            p += 3;
            for (const char *t = s;;) {
                if (gmatch(p, t)) return 1;
                if (!(t = strchr(t, '/'))) return 0;
                t++;
            }
        }
        switch (*p) {
        case 0:
            return !*s;
        case '*':
            p++;
            for (;; s++) {
                if (gmatch(p, s)) return 1;
                if (!*s || *s == '/') return 0;
            }
        case '?':
            if (!*s || *s == '/') return 0;
            p++, s++;
            break;
        case '[': {
            int m = 0;
            const char *after = *s && *s != '/' ? set_match(p + 1, (unsigned char)*s, &m) : NULL;
            if (after) {
                if (!m) return 0;
                p = after, s++;
                break;
            }
            if (*s != '[') return 0;   /* no ']': a plain '[' */
            p++, s++;
            break;
        }
        case '\\':
            if (p[1]) p++;
            /* fall through */
        default:
            if (*p != *s) return 0;
            p++, s++;
        }
    }
}

const char *lhcat_exclude_match(const char *list, const char *rel, int is_dir, char *out, size_t n)
{
    if (!list || !rel) return NULL;
    for (const char *p = list; *p;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char g[PATH_MAX];
        const char *orig = p;
        snprintf(g, sizeof g, "%.*s", (int)len, p);
        p += len + (e ? 1 : 0);
        char *a = g;
        size_t k = strlen(a);
        while (k && (a[k - 1] == ' ' || a[k - 1] == '\t' || a[k - 1] == '\r')) a[--k] = 0;
        while (*a == ' ' || *a == '\t') a++;
        if (!*a || *a == '#') continue;
        while (a[0] == '.' && a[1] == '/') a += 2;
        while (*a == '/') a++;
        k = strlen(a);
        int dir_only = k && a[k - 1] == '/';
        while (k && a[k - 1] == '/') a[--k] = 0;
        if (!k || (dir_only && !is_dir)) continue;
        if (gmatch(a, rel)) {
            if (out && n) snprintf(out, n, "%.*s", (int)len, orig);
            return out ? out : "";
        }
    }
    return NULL;
}

/* ---- the mount table ----------------------------------------------------------------------------------------------- */

static int below(const char *dir, const char *path)
{
    size_t n = strlen(dir);
    if (n == 1 && dir[0] == '/') return path[0] == '/' && path[1];
    return !strncmp(dir, path, n) && path[n] == '/' && path[n + 1];
}

/* mountinfo's octal escapes (\040 a space) undone in place */
static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] >= '0' && s[1] <= '7' && s[2] >= '0' && s[2] <= '7' && s[3] >= '0' && s[3] <= '7') {
            *o++ = (char)((s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0'));
            s += 3;
        } else *o++ = *s;
    }
    *o = 0;
}

int lhcat_mounts_below(const char *dir, lhcat_mount_fn fn, void *ctx)
{
    int count = 0;
#if defined(__linux__)
    FILE *f = fopen("/proc/self/mountinfo", "re");
    if (!f) return -1;
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        /* ID PARENT MAJ:MIN ROOT MOUNTPOINT OPTIONS [OPTIONAL...] - FSTYPE SOURCE SUPER */
        char *fields[5] = { 0 }, *save = NULL;
        int i = 0;
        for (char *t = strtok_r(line, " \n", &save); t; t = i < 5 ? strtok_r(NULL, " \n", &save) : NULL) fields[i++] = t;
        if (i < 5) continue;
        const char *fstype = "";
        for (char *x; (x = strtok_r(NULL, " \n", &save));)
            if (!strcmp(x, "-")) { char *ft = strtok_r(NULL, " \n", &save); if (ft) fstype = ft; break; }
        unescape(fields[4]);
        if (!below(dir, fields[4])) continue;
        count++;
        if (fn) fn(ctx, fields[4], fstype);
    }
    free(line);
    fclose(f);
#elif defined(__FreeBSD__) || defined(__APPLE__)
    struct statfs *m = NULL;
    int n = getmntinfo(&m, MNT_NOWAIT);
    if (n <= 0) return -1;
    for (int i = 0; i < n; i++) {
        if (!below(dir, m[i].f_mntonname)) continue;
        count++;
        if (fn) fn(ctx, m[i].f_mntonname, m[i].f_fstypename);
    }
#else
    (void)dir; (void)fn; (void)ctx;
    return -1;
#endif
    return count;
}

/* ---- the scope of a walk ---------------------------------------------------------------------------------------- */

static int str_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void add_mount(void *ctx, const char *path, const char *fstype)
{
    (void)fstype;
    struct lhcat__scope *s = ctx;
    if (s->nmounts % 64 == 0) {
        char **m = realloc(s->mounts, (s->nmounts + 64) * sizeof *m);
        if (!m) return;
        s->mounts = m;
    }
    if ((s->mounts[s->nmounts] = strdup(path))) s->nmounts++;
}

int lhcat__scope_init(lhcat *c, struct lhcat__scope *s, const struct lhcat_index_opts *o)
{
    memset(s, 0, sizeof *s);
    struct stat rs;
    if (stat(c->root, &rs) != 0) return lhcat__fail(c, "%s: %s", c->root, strerror(errno));
    s->dev = rs.st_dev;
    s->one_fs = !(o && o->cross_fs);
    s->root_list = c->excludes;
    s->run_list = o && o->exclude && *o->exclude ? o->exclude : NULL;
    /* $LHCAT_TEST_NO_MOUNTTAB: the device comparison alone (tests) */
    if (s->one_fs && !getenv("LHCAT_TEST_NO_MOUNTTAB")) lhcat_mounts_below(c->root, add_mount, s);
    if (s->nmounts > 1) qsort(s->mounts, s->nmounts, sizeof *s->mounts, str_cmp);
    return 0;
}

void lhcat__scope_free(struct lhcat__scope *s)
{
    for (size_t i = 0; i < s->nmounts; i++) free(s->mounts[i]);
    free(s->mounts);
    s->mounts = NULL;
    s->nmounts = 0;
}

static char kind_of_dtype(unsigned char t)
{
    return t == DT_DIR ? 'd' : t == DT_REG ? 'f' : t == DT_LNK ? 'l' : 'o';
}

int lhcat__classify(const struct lhcat__scope *s, const char *rel, const char *abs, unsigned char d_type, int stat_all,
                    struct stat *st, int *have_st, char *kind, char *glob, size_t globn)
{
    *have_st = 0;
    *kind = kind_of_dtype(d_type);
    if (s->one_fs && s->nmounts) {
        const char *key = abs;
        if (bsearch(&key, s->mounts, s->nmounts, sizeof *s->mounts, str_cmp)) { *kind = 'd'; return LHCAT__MOUNT; }
    }
    if (d_type == DT_UNKNOWN) {
        if (lstat(abs, st) != 0) return -1;
        *have_st = 1;
        *kind = S_ISREG(st->st_mode) ? 'f' : S_ISDIR(st->st_mode) ? 'd' : S_ISLNK(st->st_mode) ? 'l' : 'o';
    }
    if ((s->root_list || s->run_list) &&
        (lhcat_exclude_match(s->root_list, rel, *kind == 'd', glob, globn) ||
         lhcat_exclude_match(s->run_list, rel, *kind == 'd', glob, globn)))
        return LHCAT__EXCLUDED;
    if (!*have_st && (stat_all || *kind == 'd')) {
        if (lstat(abs, st) != 0) return -1;
        *have_st = 1;
        *kind = S_ISREG(st->st_mode) ? 'f' : S_ISDIR(st->st_mode) ? 'd' : S_ISLNK(st->st_mode) ? 'l' : 'o';
    }
    if (s->one_fs && *have_st && S_ISDIR(st->st_mode) && st->st_dev != s->dev) return LHCAT__MOUNT;
    return LHCAT__WALK;
}

/* ---- the plan --------------------------------------------------------------------------------------------------------- */

static int dent_cmp(const void *a, const void *b) { return strcmp(((const struct dent *)a)->name, ((const struct dent *)b)->name); }

int lhcat__list_dir(const char *abs, struct dent_list *out)
{
    out->v = NULL;
    out->n = 0;
    out->whole = 1;
    DIR *d = opendir(abs);
    if (!d) return -1;
    size_t cap = 0;
    struct dirent *e;
    errno = 0;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (out->n == cap) {
            cap = cap ? cap * 2 : 64;
            struct dent *m = realloc(out->v, cap * sizeof *m);
            if (!m) { out->whole = 0; break; }
            out->v = m;
        }
        if ((out->v[out->n].name = strdup(e->d_name))) { out->v[out->n].type = e->d_type; out->n++; }
        else out->whole = 0;
    }
    if (errno) out->whole = 0;
    closedir(d);
    if (out->n > 1) qsort(out->v, out->n, sizeof *out->v, dent_cmp);
    return 0;
}

void lhcat__free_dents(struct dent_list *l)
{
    for (size_t i = 0; i < l->n; i++) free(l->v[i].name);
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

/* the counts under the directory rel (itself not counted) */
static void plan_count(const struct lhcat__scope *s, const char *rel, const char *abs, struct lhcat_plan *p)
{
    struct dent_list l;
    if (lhcat__list_dir(abs, &l) != 0) { p->unreadable++; return; }
    for (size_t i = 0; i < l.n && !lhcat__stop; i++) {
        char crel[PATH_MAX], cabs[PATH_MAX];
        if (*rel) snprintf(crel, sizeof crel, "%s/%s", rel, l.v[i].name); else snprintf(crel, sizeof crel, "%s", l.v[i].name);
        snprintf(cabs, sizeof cabs, "%s/%s", abs, l.v[i].name);
        struct stat st;
        int have = 0;
        char kind, g[8];
        int what = lhcat__classify(s, crel, cabs, l.v[i].type, 0, &st, &have, &kind, g, sizeof g);
        if (what == LHCAT__EXCLUDED) { p->excluded++; continue; }
        if (what == LHCAT__MOUNT) { p->mounts++; continue; }
        p->entries++;
        if (what == LHCAT__WALK && kind == 'd') { p->dirs++; plan_count(s, crel, cabs, p); }
    }
    lhcat__free_dents(&l);
}

int lhcat_index_plan(lhcat *c, const char *subpath, const struct lhcat_index_opts *o, lhcat_plan_fn fn, void *ctx)
{
    struct lhcat__scope s;
    if (lhcat__scope_init(c, &s, o)) return -1;
    char sub[PATH_MAX], abs[PATH_MAX];
    snprintf(sub, sizeof sub, "%s", subpath ? subpath : "");
    while (strlen(sub) && sub[strlen(sub) - 1] == '/') sub[strlen(sub) - 1] = 0;
    lhcat__abs(c, sub, abs, sizeof abs);
    struct dent_list l;
    lhcat__stop = 0;
    if (lhcat__list_dir(abs, &l) != 0) { lhcat__scope_free(&s); return lhcat__fail(c, "%s: %s", abs, strerror(errno)); }
    for (size_t i = 0; i < l.n && !lhcat__stop; i++) {
        char crel[PATH_MAX], cabs[PATH_MAX], g[PATH_MAX] = "";
        if (*sub) snprintf(crel, sizeof crel, "%s/%s", sub, l.v[i].name); else snprintf(crel, sizeof crel, "%s", l.v[i].name);
        snprintf(cabs, sizeof cabs, "%s/%s", abs, l.v[i].name);
        struct lhcat_plan p;
        memset(&p, 0, sizeof p);
        p.name = l.v[i].name;
        struct stat st;
        int have = 0;
        char kind;
        int what = lhcat__classify(&s, crel, cabs, l.v[i].type, 0, &st, &have, &kind, g, sizeof g);
        if (what < 0) { p.unreadable = 1; what = 0; }
        p.kind = kind;
        p.what = what == LHCAT__EXCLUDED ? 1 : what == LHCAT__MOUNT ? 2 : 0;
        p.glob = what == LHCAT__EXCLUDED ? g : NULL;
        if (what == LHCAT__WALK) {
            p.entries = 1;
            if (kind == 'd') { p.dirs = 1; plan_count(&s, crel, cabs, &p); }
        }
        if (fn) fn(ctx, &p);
    }
    lhcat__free_dents(&l);
    lhcat__scope_free(&s);
    return lhcat__stop ? lhcat__fail(c, "interrupted") : 0;
}
