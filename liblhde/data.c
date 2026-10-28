/* liblhde: lhde's data directories (lhde/data.h). */
#define _GNU_SOURCE
#include "lhde/data.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char user_dir[PATH_MAX], tree_dir[PATH_MAX], exe_dir[PATH_MAX];
static char dir_buf[LHDE_DATA_MAX][PATH_MAX];
static int ndirs = -1;

static int is_dir(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int exists(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0; }

/* a source tree: data/themes/ (step 4) or data/themes.ini (before), and luxwm/wm.c */
static int is_tree(const char *p)
{
    char t[PATH_MAX + 32], u[PATH_MAX + 32], w[PATH_MAX + 32];
    snprintf(t, sizeof t, "%s/data/themes", p); snprintf(u, sizeof u, "%s/data/themes.ini", p); snprintf(w, sizeof w, "%s/luxwm/wm.c", p);
    return (is_dir(t) || exists(u)) && exists(w);
}

static void find_exe_and_tree(void)
{
    char p[PATH_MAX];
    ssize_t k = readlink("/proc/self/exe", p, sizeof p - 1);
    if (k <= 0) return;
    p[k] = 0;
    char *sl = strrchr(p, '/');
    if (!sl) return;
    *sl = 0;
    snprintf(exe_dir, sizeof exe_dir, "%s", p);
    /* a build tree: <top>/build/bin, <top>/build/examples, <top>/build/luxwm/tests, ... (up to 4 levels) */
    for (int up = 0; up < 4; up++) {
        if (is_tree(p)) { snprintf(tree_dir, sizeof tree_dir, "%s", p); return; }
        sl = strrchr(p, '/');
        if (!sl || sl == p) return;
        *sl = 0;
    }
}

static void add_dir(const char *d)
{
    char r[PATH_MAX];
    if (ndirs >= LHDE_DATA_MAX || !is_dir(d) || !realpath(d, r)) return;
    for (int i = 0; i < ndirs; i++) if (!strcmp(dir_buf[i], r)) return;
    snprintf(dir_buf[ndirs++], PATH_MAX, "%s", r);
}

static void init(void)
{
    if (ndirs >= 0) return;
    ndirs = 0;
    const char *e = getenv("LHDE_DATA"), *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (e && *e) snprintf(user_dir, sizeof user_dir, "%s", e);
    else if (x && *x) snprintf(user_dir, sizeof user_dir, "%s/lhde", x);
    else snprintf(user_dir, sizeof user_dir, "%s/.local/share/lhde", h ? h : "");
    tree_dir[0] = exe_dir[0] = 0;
    find_exe_and_tree();
    add_dir(user_dir);
    if (exe_dir[0] && !tree_dir[0]) { char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/../share/lhde", exe_dir); add_dir(p); }
    const char *xd = getenv("XDG_DATA_DIRS");
    char list[4096]; snprintf(list, sizeof list, "%s", xd && *xd ? xd : "/usr/local/share:/usr/share");
    for (char *save = NULL, *t = strtok_r(list, ":", &save); t; t = strtok_r(NULL, ":", &save)) {
        char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/lhde", t); add_dir(p);
    }
    const char *nt = getenv("LHDE_TREE");   /* LHDE_TREE=0: not the source tree's data/ (tests on fake files) */
    if (tree_dir[0] && !(nt && !strcmp(nt, "0"))) { char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/data", tree_dir); add_dir(p); }
}

void lhde_data_reset(void) { ndirs = -1; }

int lhde_data_dirs(const char **dirs, int max)
{
    init();
    int n = 0;
    for (int i = 0; i < ndirs && n < max; i++) dirs[n++] = dir_buf[i];
    return n;
}

const char *lhde_data_user(void) { init(); return user_dir; }
const char *lhde_tree(void) { init(); return tree_dir[0] ? tree_dir : NULL; }

/* DIR/REL, else DIR/<REL's directory>/cache/<REL's last component> (manifest-first frame sets and packs) */
static int exists_or_cache(const char *dir, const char *rel, char *out, size_t n)
{
    snprintf(out, n, "%s/%s", dir, rel);
    if (exists(out)) return 1;
    const char *sl = strrchr(rel, '/');
    if (!sl) return 0;   /* a top-level name has no cache form */
    snprintf(out, n, "%s/%.*s/cache/%s", dir, (int)(sl - rel), rel, sl + 1);
    return exists(out);
}

const char *lhde_data_find(const char *rel, char *out, size_t n)
{
    init();
    while (rel[0] == '.' && rel[1] == '/') rel += 2;
    for (int i = 0; i < ndirs; i++)
        if (exists_or_cache(dir_buf[i], rel, out, n)) return out;
    snprintf(out, n, "%s/%s", user_dir, rel);
    return NULL;
}

const char *lhde_data_resolve(const char *v, char *out, size_t n)
{
    if (!v || !v[0]) { if (n) out[0] = 0; return out; }
    if (v[0] == '/') { snprintf(out, n, "%s", v); return out; }
    if (v[0] == '~' && (v[1] == '/' || !v[1])) { const char *h = getenv("HOME"); snprintf(out, n, "%s%s", h ? h : "", v + 1); return out; }
    lhde_data_find(v, out, n);
    return out;
}

const char *lhde_data_find_in(const char *dir, const char *name, char *out, size_t n)
{
    init();
    if (exists_or_cache(dir, name, out, n)) return out;
    snprintf(out, n, "%s/cache/%s", dir, name);
    if (exists(out)) return out;
    char rd[PATH_MAX], keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%s/%s", dir, name);
    if (!realpath(dir, rd)) snprintf(rd, sizeof rd, "%s", dir);
    for (int i = 0; i < ndirs; i++) {
        size_t l = strlen(dir_buf[i]);
        if (strncmp(rd, dir_buf[i], l) || (rd[l] != '/' && rd[l])) continue;
        const char *rel = rd + l; while (*rel == '/') rel++;
        for (int j = 0; j < ndirs; j++) {
            snprintf(out, n, "%s/%s%s%s", dir_buf[j], rel, *rel ? "/" : "", name);
            if (exists(out)) return out;
            snprintf(out, n, "%s/%s%scache/%s", dir_buf[j], rel, *rel ? "/" : "", name);
            if (exists(out)) return out;
        }
        break;
    }
    snprintf(out, n, "%s", keep);
    return NULL;
}

const char *lhde_data_resolve_dir(const char *v, const char *marker, char *out, size_t n)
{
    if (!v || !v[0] || v[0] == '/' || v[0] == '~') return lhde_data_resolve(v, out, n);
    char rel[PATH_MAX];
    snprintf(rel, sizeof rel, "%s/%s", v, marker);
    init();
    for (int i = 0; i < ndirs; i++) {   /* the marker itself, not its cache form */
        snprintf(out, n, "%s/%s", dir_buf[i], rel);
        if (exists(out)) { char *sl = strrchr(out, '/'); if (sl) *sl = 0; return out; }
    }
    return lhde_data_resolve(v, out, n);
}

const char *lhde_component_dir(const char *name, char *out, size_t n)
{
    init();
    if (tree_dir[0]) { snprintf(out, n, "%s/%s", tree_dir, name); if (is_dir(out)) return out; }
    if (exe_dir[0]) {
        char p[PATH_MAX * 2], r[PATH_MAX];
        snprintf(p, sizeof p, "%s/../share/lhde/%s", exe_dir, name);
        if (is_dir(p) && realpath(p, r)) { snprintf(out, n, "%s", r); return out; }
    }
    return NULL;
}
