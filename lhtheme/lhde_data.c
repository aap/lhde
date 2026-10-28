/* lhde's data directories and the resolved registry (lhde_data.h). */
#define _GNU_SOURCE
#include "lhde_data.h"
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
        char t[PATH_MAX + 32], w[PATH_MAX + 32];
        snprintf(t, sizeof t, "%s/data/themes.ini", p); snprintf(w, sizeof w, "%s/luxwm/wm.c", p);
        if (exists(t) && exists(w)) { snprintf(tree_dir, sizeof tree_dir, "%s", p); return; }
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
    find_exe_and_tree();
    add_dir(user_dir);
    if (exe_dir[0] && !tree_dir[0]) { char p[PATH_MAX + 32]; snprintf(p, sizeof p, "%s/../share/lhde", exe_dir); add_dir(p); }
    const char *xd = getenv("XDG_DATA_DIRS");
    char list[4096]; snprintf(list, sizeof list, "%s", xd && *xd ? xd : "/usr/local/share:/usr/share");
    for (char *save = NULL, *t = strtok_r(list, ":", &save); t; t = strtok_r(NULL, ":", &save)) {
        char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/lhde", t); add_dir(p);
    }
    if (tree_dir[0]) { char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/data", tree_dir); add_dir(p); }
}

int lhde_data_dirs(const char **dirs, int max)
{
    init();
    int n = 0;
    for (int i = 0; i < ndirs && n < max; i++) dirs[n++] = dir_buf[i];
    return n;
}

const char *lhde_data_user(void) { init(); return user_dir; }
const char *lhde_tree(void) { init(); return tree_dir[0] ? tree_dir : NULL; }

const char *lhde_data_find(const char *rel, char *out, size_t n)
{
    init();
    while (rel[0] == '.' && rel[1] == '/') rel += 2;
    for (int i = 0; i < ndirs; i++) {
        snprintf(out, n, "%s/%s", dir_buf[i], rel);
        if (exists(out)) return out;
    }
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
    snprintf(out, n, "%s/%s", dir, name);
    if (exists(out)) return out;
    char rd[PATH_MAX], keep[PATH_MAX];
    snprintf(keep, sizeof keep, "%s", out);
    if (!realpath(dir, rd)) snprintf(rd, sizeof rd, "%s", dir);
    for (int i = 0; i < ndirs; i++) {
        size_t l = strlen(dir_buf[i]);
        if (strncmp(rd, dir_buf[i], l) || (rd[l] != '/' && rd[l])) continue;
        const char *rel = rd + l; while (*rel == '/') rel++;
        for (int j = 0; j < ndirs; j++) {
            snprintf(out, n, "%s/%s%s%s", dir_buf[j], rel, *rel ? "/" : "", name);
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
    if (lhde_data_find(rel, out, n)) { char *sl = strrchr(out, '/'); if (sl) *sl = 0; return out; }
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

const char *lhde_registry_source(char *out, size_t n)
{
    const char *e = getenv("LH_THEMES_INI");
    if (e && *e && exists(e)) { snprintf(out, n, "%s", e); return out; }
    return lhde_data_find("themes.ini", out, n);
}

/* ---- the resolved registry ---- */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

static long long mtime_of(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long long)st.st_mtime * 1000000000LL + st.st_mtim.tv_nsec : -1; }

static int mkdirs(const char *d)
{
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s", d);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; if (mkdir(p, 0700) && errno != EEXIST) return -1; *s = '/'; }
    return mkdir(p, 0700) && errno != EEXIST ? -1 : 0;
}

/* the keys whose values are paths (THEMES.md); fonts is a ';' list, frame_classic may be the word "style" */
static int path_key(const char *k)
{
    static const char *const keys[] = { "msstyles", "flxp_msstyles", "frame", "frame_classic", "shell", "fonts", "wallpaper",
                                        "flxp_longhorn_system32", "flxp_explorer_resources", "flxp_longhorn_wim", NULL };
    for (int i = 0; keys[i]; i++) if (!strcmp(k, keys[i])) return 1;
    return 0;
}

/* one "key = value [; comment]" line resolved; returns 1 when it wrote it */
static int resolve_line(FILE *o, char *line, int wallpapers)
{
    char buf[4096]; snprintf(buf, sizeof buf, "%s", line);
    char *s = trim(buf);
    if (!s[0] || s[0] == ';' || s[0] == '#' || s[0] == '[') return 0;
    char *eq = strchr(s, '='); if (!eq) return 0;
    *eq = 0; char *k = trim(s), *v = trim(eq + 1), *comment = "";
    if (!(wallpapers ? !strcmp(k, "root") : path_key(k))) return 0;
    for (char *c = v; *c; c++) if ((*c == ';' || *c == '#') && c > v && (c[-1] == ' ' || c[-1] == '\t')) {
        comment = c; c[-1] = 0; v = trim(v); break; }
    if (!v[0] || !strcmp(v, "style") || !strcmp(v, "none")) return 0;
    char res[8192] = "";
    if (!strcmp(k, "fonts")) {
        char list[4096]; snprintf(list, sizeof list, "%s", v);
        for (char *save = NULL, *t = strtok_r(list, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
            t = trim(t); if (!*t) continue;
            char one[PATH_MAX]; lhde_data_resolve(t, one, sizeof one);
            size_t l = strlen(res); snprintf(res + l, sizeof res - l, "%s%s", l ? ";" : "", one);
        }
    } else if (!wallpapers && (!strcmp(k, "frame") || !strcmp(k, "frame_classic"))) lhde_data_resolve_dir(v, "theme.ini", res, sizeof res);
    else if (!wallpapers && !strcmp(k, "shell")) lhde_data_resolve_dir(v, "shell.lua", res, sizeof res);
    else lhde_data_resolve(v, res, sizeof res);
    fprintf(o, "%s = %s%s%s\n", k, res, comment[0] ? "   " : "", comment);
    return 1;
}

static int resolve_file(const char *src, const char *dst, int wallpapers)
{
    FILE *f = fopen(src, "r");
    if (!f) return -1;
    char tmp[PATH_MAX + 16]; snprintf(tmp, sizeof tmp, "%s.%d", dst, (int)getpid());
    FILE *o = fopen(tmp, "w");
    if (!o) { fclose(f); return -1; }
    fprintf(o, "; lhde: resolved from %s (lhtheme/lhde_data.c: every path made absolute through the data directories; edit the source)\n", src);
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "; lhde: resolved from ", 22)) continue;
        if (!resolve_line(o, line, wallpapers)) fputs(line, o);
    }
    fclose(f);
    if (fclose(o) || rename(tmp, dst)) { unlink(tmp); return -1; }
    return 0;
}

/* the source a resolved copy names on its first line, else the file itself */
static void source_of(const char *p, char *out, size_t n)
{
    snprintf(out, n, "%s", p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[PATH_MAX + 64];
    if (fgets(line, sizeof line, f) && !strncmp(line, "; lhde: resolved from ", 22)) {
        char *s = line + 22, *e = strstr(s, " (lhtheme/");
        if (e) { *e = 0; snprintf(out, n, "%s", trim(s)); }
    }
    fclose(f);
}

const char *lhde_registry_resolved(const char *src_in, char *out, size_t n)
{
    char src[PATH_MAX], wsrc[PATH_MAX + 32], dir[PATH_MAX], dst[PATH_MAX + 32], wdst[PATH_MAX + 32];
    source_of(src_in, src, sizeof src);
    if (!exists(src)) return NULL;
    const char *rt = getenv("XDG_RUNTIME_DIR"), *d = getenv("DISPLAY");
    char disp[64] = ""; int j = 0;
    for (const char *c = d ? d : ""; *c && j < 60; c++) disp[j++] = (*c == '/' ? '_' : *c);
    disp[j] = 0;
    if (rt && *rt) snprintf(dir, sizeof dir, "%s/lhde/%s", rt, disp[0] ? disp : "nodisplay");
    else snprintf(dir, sizeof dir, "/tmp/lhde-%d/%s", (int)getuid(), disp[0] ? disp : "nodisplay");
    if (mkdirs(dir)) return NULL;
    snprintf(dst, sizeof dst, "%s/themes.ini", dir); snprintf(wdst, sizeof wdst, "%s/wallpapers.ini", dir);
    /* wallpapers.ini: $LH_WALLPAPERS_INI, else beside the source registry, else in the data directories */
    const char *we = getenv("LH_WALLPAPERS_INI");
    if (we && *we) snprintf(wsrc, sizeof wsrc, "%s", we);
    else {
        char sd[PATH_MAX]; snprintf(sd, sizeof sd, "%s", src);
        char *sl = strrchr(sd, '/'); if (sl) *sl = 0; else snprintf(sd, sizeof sd, ".");
        snprintf(wsrc, sizeof wsrc, "%s/wallpapers.ini", sd);
        if (!exists(wsrc)) lhde_data_find("wallpapers.ini", wsrc, sizeof wsrc);
    }
    if (strcmp(src, src_in)) {   /* refreshing an existing copy: write it where it is */
        snprintf(dst, sizeof dst, "%s", src_in);
        snprintf(dir, sizeof dir, "%s", src_in); char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
        snprintf(wdst, sizeof wdst, "%s/wallpapers.ini", dir);
    }
    char have[PATH_MAX]; source_of(dst, have, sizeof have);
    int fresh = exists(dst) && !strcmp(have, src) && mtime_of(dst) >= mtime_of(src);
    if (!fresh && resolve_file(src, dst, 0)) return NULL;
    if (exists(wsrc) && (!fresh || !exists(wdst) || mtime_of(wdst) < mtime_of(wsrc))) resolve_file(wsrc, wdst, 1);
    snprintf(out, n, "%s", dst);
    return out;
}
