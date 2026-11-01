/* liblhde: lhde's three trees and the lookup over them (lhde/data.h, docs/LAYOUT.md "The three trees"). */
#define _GNU_SOURCE
#include "lhde/data.h"
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char user_dir[PATH_MAX + 64], win_dir[PATH_MAX + 32], tree_dir[PATH_MAX], exe_dir[PATH_MAX];
static struct root { char path[PATH_MAX]; int kind; } roots[LHDE_DATA_MAX];
static int ndirs = -1;

static int is_dir(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int exists(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0; }

/* a source tree: share/themes/ and luxwm/wm.c */
static int is_tree(const char *p)
{
    char t[PATH_MAX + 32], w[PATH_MAX + 32];
    snprintf(t, sizeof t, "%s/share/themes", p); snprintf(w, sizeof w, "%s/luxwm/wm.c", p);
    return is_dir(t) && exists(w);
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

static void add_dir(const char *d, int kind)
{
    char r[PATH_MAX];
    if (ndirs >= LHDE_DATA_MAX || !is_dir(d) || !realpath(d, r)) return;
    for (int i = 0; i < ndirs; i++) if (!strcmp(roots[i].path, r)) return;   /* the first kind wins */
    snprintf(roots[ndirs].path, PATH_MAX, "%s", r);
    roots[ndirs++].kind = kind;
}

static void init(void)
{
    if (ndirs >= 0) return;
    ndirs = 0;
    const char *e = getenv("LHDE_DATA"), *w = getenv("LHDE_WINDOWS"), *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    char share[PATH_MAX - 64];
    if (x && *x) snprintf(share, sizeof share, "%s", x); else snprintf(share, sizeof share, "%s/.local/share", h ? h : "");
    tree_dir[0] = exe_dir[0] = 0;
    find_exe_and_tree();
    /* 1. the user's files: ~/.config/lhde (themes/, and any data file the user puts there under its data-directory name) */
    /* (spelled out here, not lhde_config_dir: libluxnav links data.o alone) */
    const char *xc = getenv("XDG_CONFIG_HOME");
    char cfg[PATH_MAX + 16];
    if (xc && *xc) snprintf(cfg, sizeof cfg, "%s/lhde", xc); else snprintf(cfg, sizeof cfg, "%s/.config/lhde", h ? h : "");
    add_dir(cfg, LHDE_TREE_USER);
    /* 2. the Windows files: $LHDE_DATA (one flat directory: tests, scratch imports), else $LHDE_WINDOWS/index */
    if (e && *e) {
        snprintf(win_dir, sizeof win_dir, "%s", e);
        snprintf(user_dir, sizeof user_dir, "%s", e);
        add_dir(user_dir, LHDE_TREE_FLAT);
    } else {
        if (w && *w) snprintf(win_dir, sizeof win_dir, "%s", w); else snprintf(win_dir, sizeof win_dir, "%s/lhde-windows", share);
        snprintf(user_dir, sizeof user_dir, "%s/index", win_dir);
        add_dir(user_dir, LHDE_TREE_WINDOWS);
    }
    /* 3. the shipped data: <exe>/../share/lhde (the program's own prefix), then $XDG_DATA_DIRS/lhde */
    if (exe_dir[0] && !tree_dir[0]) { char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/../share/lhde", exe_dir); add_dir(p, LHDE_TREE_SHIPPED); }
    const char *xd = getenv("XDG_DATA_DIRS");
    char list[4096]; snprintf(list, sizeof list, "%s", xd && *xd ? xd : "/usr/local/share:/usr/share");
    for (char *save = NULL, *t = strtok_r(list, ":", &save); t; t = strtok_r(NULL, ":", &save)) {
        char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/lhde", t); add_dir(p, LHDE_TREE_SHIPPED);
    }
    /* 4. the source tree's share/ (an uninstalled build); LHDE_TREE=0 leaves it out (tests on fake files) */
    const char *nt = getenv("LHDE_TREE");
    if (tree_dir[0] && !(nt && !strcmp(nt, "0"))) { char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/share", tree_dir); add_dir(p, LHDE_TREE_SOURCE); }
}

void lhde_data_reset(void) { ndirs = -1; }

int lhde_data_dirs(const char **dirs, int max)
{
    init();
    int n = 0;
    for (int i = 0; i < ndirs && n < max; i++) dirs[n++] = roots[i].path;
    return n;
}

int lhde_data_dir_kind(int i) { init(); return i >= 0 && i < ndirs ? roots[i].kind : -1; }

const char *lhde_tree_name(int kind)
{
    switch (kind) {
    case LHDE_TREE_USER: return "user";
    case LHDE_TREE_WINDOWS: return "windows";
    case LHDE_TREE_FLAT: return "data";
    case LHDE_TREE_SHIPPED: return "shipped";
    case LHDE_TREE_SOURCE: return "source tree";
    case LHDE_TREE_CODE: return "program";
    }
    return "elsewhere";
}

const char *lhde_data_user(void) { init(); return user_dir; }
const char *lhde_windows_dir(void) { init(); return win_dir; }
const char *lhde_tree(void) { init(); return tree_dir[0] ? tree_dir : NULL; }

/* DIR/REL, else DIR/<REL's directory>/cache/<REL's last component> (the caches of a $LHDE_DATA) */
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
        if (exists_or_cache(roots[i].path, rel, out, n)) return out;
    snprintf(out, n, "%s/%s", user_dir, rel);
    return NULL;
}

static const struct { const char *kind, *fmt; } KINDS[] = {
    { "theme", "themes/%s.theme" }, { "style", "styles/%s.msstyles" }, { "scheme", "styles/%s.scheme" }, { "frames", "frames/%s" },
    { "pack", "packs/%s" }, { "navbar", "navbar/%s.ini" }, { "icons", "icons/%s.theme" }, { "wallpaper", "wallpapers/%s" },
    { "fonts", "fonts/%s" }, { "resources", "resources/%s" }, { "source", "sources/%s.ini" }, { NULL, NULL } };

const char *lhde_data_kind_rel(const char *kind, const char *name, char *out, size_t n)
{
    for (int i = 0; KINDS[i].kind; i++)
        if (!strcmp(KINDS[i].kind, kind)) { snprintf(out, n, KINDS[i].fmt, name); return out; }
    return NULL;
}

const char *lhde_data_find_kind(const char *kind, const char *name, char *out, size_t n)
{
    char rel[PATH_MAX];
    if (!lhde_data_kind_rel(kind, name, rel, sizeof rel)) { if (n) out[0] = 0; return NULL; }
    return lhde_data_find(rel, out, n);
}

const char *lhde_data_kind_name(int i) { return i >= 0 && i < (int)(sizeof KINDS / sizeof *KINDS) - 1 ? KINDS[i].kind : NULL; }

const char *lhde_data_resolve(const char *v, char *out, size_t n)
{
    if (!v || !v[0]) { if (n) out[0] = 0; return out; }
    if (v[0] == '/') { snprintf(out, n, "%s", v); return out; }
    if (v[0] == '~' && (v[1] == '/' || !v[1])) { const char *h = getenv("HOME"); snprintf(out, n, "%s%s", h ? h : "", v + 1); return out; }
    lhde_data_find(v, out, n);
    return out;
}

/* the root DIR lies in (as given, then with symlinks resolved): its index; rel = DIR below it. -1 when none */
static int root_of(const char *dir, char *rel, size_t n)
{
    char rd[PATH_MAX];
    for (int pass = 0; pass < 2; pass++) {
        const char *d = dir;
        if (pass) { if (!realpath(dir, rd)) return -1; d = rd; }
        for (int i = 0; i < ndirs; i++) {
            size_t l = strlen(roots[i].path);
            if (strncmp(d, roots[i].path, l) || (d[l] != '/' && d[l])) continue;
            const char *r = d + l; while (*r == '/') r++;
            snprintf(rel, n, "%s", r);
            return i;
        }
    }
    return -1;
}

const char *lhde_data_find_in(const char *dir, const char *name, char *out, size_t n)
{
    init();
    if (exists_or_cache(dir, name, out, n)) return out;
    char keep[PATH_MAX], rel[PATH_MAX];
    snprintf(keep, sizeof keep, "%s/%s", dir, name);
    if (root_of(dir, rel, sizeof rel) >= 0)
        for (int j = 0; j < ndirs; j++) {
            char q[PATH_MAX * 2]; snprintf(q, sizeof q, "%s%s%s", rel, rel[0] ? "/" : "", name);
            if (exists_or_cache(roots[j].path, q, out, n)) return out;
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
        snprintf(out, n, "%s/%s", roots[i].path, rel);
        if (exists(out)) { char *sl = strrchr(out, '/'); if (sl) *sl = 0; return out; }
    }
    return lhde_data_resolve(v, out, n);
}

/* <exe>/../lib/lhde[/NAME] (make install's PKGLIBDIR) */
static const char *installed_code(const char *name, char *out, size_t n)
{
    if (!exe_dir[0]) return NULL;
    char p[PATH_MAX * 2], r[PATH_MAX];
    snprintf(p, sizeof p, "%s/../lib/lhde%s%s", exe_dir, name[0] ? "/" : "", name);
    if (!is_dir(p) || !realpath(p, r)) return NULL;
    snprintf(out, n, "%s", r);
    return out;
}

const char *lhde_component_dir(const char *name, char *out, size_t n)
{
    init();
    if (tree_dir[0]) { snprintf(out, n, "%s/%s", tree_dir, name); if (is_dir(out)) return out; }
    return installed_code(name, out, n);
}

const char *lhde_code_dir(char *out, size_t n)
{
    init();
    if (tree_dir[0]) { snprintf(out, n, "%s", tree_dir); return out; }
    return installed_code("", out, n);
}

int lhde_data_origin(const char *path, char *rel, size_t n)
{
    init();
    if (n) rel[0] = 0;
    char rp[PATH_MAX];
    if (!path || !path[0] || !realpath(path, rp)) return -1;
    /* the trees by their real tops: a file found through the Windows index lies in the Windows tree, not in index/ */
    static char tops[LHDE_DATA_MAX + 3][PATH_MAX];
    int kinds[LHDE_DATA_MAX + 3], nt = 0;
    char t[PATH_MAX];
    const char *e = getenv("LHDE_DATA");
    if (realpath(win_dir, t)) { snprintf(tops[nt], PATH_MAX, "%s", t); kinds[nt++] = e && *e ? LHDE_TREE_FLAT : LHDE_TREE_WINDOWS; }
    for (int i = 0; i < ndirs; i++) if (roots[i].kind != LHDE_TREE_WINDOWS && roots[i].kind != LHDE_TREE_FLAT) { snprintf(tops[nt], PATH_MAX, "%.*s", PATH_MAX - 1, roots[i].path); kinds[nt++] = roots[i].kind; }
    if (tree_dir[0]) { snprintf(tops[nt], PATH_MAX, "%s", tree_dir); kinds[nt++] = LHDE_TREE_SOURCE; }
    else if (installed_code("", t, sizeof t)) { snprintf(tops[nt], PATH_MAX, "%s", t); kinds[nt++] = LHDE_TREE_CODE; }
    int best = -1; size_t bl = 0;   /* the longest top that holds it */
    for (int i = 0; i < nt; i++) {
        size_t l = strlen(tops[i]);
        if (strncmp(rp, tops[i], l) || (rp[l] != '/' && rp[l]) || l < bl) continue;
        best = i; bl = l;
    }
    if (best < 0) { snprintf(rel, n, "%s", rp); return -1; }
    const char *r = rp + bl; while (*r == '/') r++;
    snprintf(rel, n, "%s", r);
    return kinds[best];
}

/* ---- sizes (lhde-theme dirs) ---- */
static long long du_sum, du_files;
static int du_one(const char *p, const struct stat *st, int flag, struct FTW *f)
{
    (void)p; (void)f;
    if (flag == FTW_F) { du_sum += (long long)st->st_size; du_files++; }
    return 0;
}
long long lhde_du(const char *dir, long long *files)
{
    du_sum = du_files = 0;
    if (is_dir(dir)) nftw(dir, du_one, 32, FTW_PHYS);
    if (files) *files = du_files;
    return du_sum;
}
