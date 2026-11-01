/* liblhde: theme files and the resolver (lhde/theme.h, docs/THEME-FORMAT.md sections 2-6). */
#define _GNU_SOURCE
#include "lhde/theme.h"
#include "lhde/data.h"
#include "lhde/settings.h"
#include "internal.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- the record */
struct item { char *key, *value, *why; int src; };
struct lhde_theme { struct item *it; int n, cap; };

static struct item *find_item(const lhde_theme *t, const char *key)
{
    for (int i = 0; t && i < t->n; i++) if (!strcasecmp(t->it[i].key, key)) return &t->it[i];
    return NULL;
}
/* set (replace) key; why may be NULL */
static void put(lhde_theme *t, const char *key, const char *value, int src, const char *why)
{
    struct item *x = find_item(t, key);
    if (!x) {
        if (t->n == t->cap) { int c = t->cap ? t->cap * 2 : 64; struct item *ni = realloc(t->it, (size_t)c * sizeof *ni); if (!ni) return; t->it = ni; t->cap = c; }
        x = &t->it[t->n++]; memset(x, 0, sizeof *x);
        x->key = strdup(key);
    }
    free(x->value); free(x->why);
    x->value = strdup(value ? value : ""); x->why = strdup(why ? why : ""); x->src = src;
}
static const char *get(const lhde_theme *t, const char *key) { struct item *x = find_item(t, key); return x ? x->value : NULL; }
static int has(const lhde_theme *t, const char *key) { return find_item(t, key) != NULL; }

const char *lhde_theme_get(const lhde_theme *t, const char *key) { return get(t, key); }
const char *lhde_theme_why(const lhde_theme *t, const char *key) { struct item *x = find_item(t, key); return x ? x->why : NULL; }
enum lhde_source lhde_theme_source(const lhde_theme *t, const char *key) { struct item *x = find_item(t, key); return x ? (enum lhde_source)x->src : LHDE_FROM_DERIVED; }
int lhde_theme_item(const lhde_theme *t, int i, const char **key, const char **value, const char **why)
{
    if (!t || i < 0 || i >= t->n) return 0;
    if (key) *key = t->it[i].key;
    if (value) *value = t->it[i].value;
    if (why) *why = t->it[i].why;
    return 1;
}
void lhde_theme_free(lhde_theme *t)
{
    if (!t) return;
    for (int i = 0; i < t->n; i++) { free(t->it[i].key); free(t->it[i].value); free(t->it[i].why); }
    free(t->it); free(t);
}

/* ---------------------------------------------------------------- the format's keys (section 2) */
static const char *const SECTIONS[] = { "Theme", "VisualStyle", "Glass", "Shell", "Explorer", "Desktop", "Fonts", "Cursors", "Sounds",
                                        "Colors", "Metrics", NULL };
/* the classic look (section 2a): COLOR_ names by index, NONCLIENTMETRICS + the icon title font */
static const char *const COLORS[] = {
    "scrollbar", "background", "activecaption", "inactivecaption", "menu", "window", "windowframe", "menutext", "windowtext",
    "captiontext", "activeborder", "inactiveborder", "appworkspace", "highlight", "highlighttext", "btnface", "btnshadow",
    "graytext", "btntext", "inactivecaptiontext", "btnhighlight", "3ddkshadow", "3dlight", "infotext", "infobk",
    "alternatebtnface", "hotlight", "gradientactivecaption", "gradientinactivecaption", "menuhilight", "menubar", NULL };
static const char *const METRICS[] = {
    "borderwidth", "scrollwidth", "scrollheight", "captionwidth", "captionheight", "smcaptionwidth", "smcaptionheight",
    "menuwidth", "menuheight", "captionfont", "smcaptionfont", "menufont", "statusfont", "messagefont", "icontitlefont", NULL };
const char *lhde_color_name(int i) { return i >= 0 && i < (int)(sizeof COLORS / sizeof *COLORS) ? COLORS[i] : NULL; }
static const char *const KEYS[] = {
    "Theme.name", "Theme.base", "Theme.build",
    "VisualStyle.style", "VisualStyle.color", "VisualStyle.size", "VisualStyle.scheme", "VisualStyle.shellstyle",
    "Glass.frames", "Glass.frames_classic", "Glass.composition", "Glass.animations",
    "Shell.taskbar", "Shell.pack", "Shell.sidebar",
    "Explorer.chrome", "Explorer.navbar", "Explorer.navbar_preset", "Explorer.taskpane",
    "Desktop.wallpaper", "Desktop.mode", "Desktop.color", "Desktop.icons",
    "Fonts.fonts", "Fonts.smoothing", NULL };
int lhde_theme_is_key(const char *key)
{
    for (int i = 0; KEYS[i]; i++) if (!strcasecmp(KEYS[i], key)) return 1;
    if (!strncasecmp(key, "Colors.", 7)) { for (int i = 0; COLORS[i]; i++) if (!strcasecmp(COLORS[i], key + 7)) return 1; }
    if (!strncasecmp(key, "Metrics.", 8)) { for (int i = 0; METRICS[i]; i++) if (!strcasecmp(METRICS[i], key + 8)) return 1; }
    return 0;
}
/* "visualstyle" -> "VisualStyle"; an unknown section as written */
static const char *canon_section(const char *s)
{
    for (int i = 0; SECTIONS[i]; i++) if (!strcasecmp(SECTIONS[i], s)) return SECTIONS[i];
    return s;
}

/* ---------------------------------------------------------------- where themes are */
const char *lhde__config_home(char *out, size_t n)
{
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(out, n, "%s", x); else snprintf(out, n, "%s/.config", h ? h : ".");
    return out;
}
const char *lhde_theme_user_dir(char *out, size_t n)
{
    char c[PATH_MAX]; lhde__config_home(c, sizeof c);
    snprintf(out, n, "%s/lhde/themes", c);
    return out;
}
static int name_ok(const char *n) { return n && n[0] && n[0] != '.' && !strchr(n, '/') && strlen(n) < 200; }

/* the theme files: the user's, then each data directory's themes/ (a name once, the first wins) */
struct tfile { char name[200]; char path[PATH_MAX]; char aliases[512]; char title[256]; int group; };
struct tindex { struct tfile *f; int n, cap; };
static void tindex_free(struct tindex *x) { free(x->f); x->f = NULL; x->n = x->cap = 0; }
static void scan_dir(struct tindex *x, const char *dir, int group)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 7 || strcmp(e->d_name + l - 6, ".theme") || e->d_name[0] == '.') continue;
        char nm[200]; snprintf(nm, sizeof nm, "%.*s", (int)(l - 6), e->d_name);
        int dup = 0;
        for (int i = 0; i < x->n; i++) if (!strcmp(x->f[i].name, nm)) dup = 1;
        if (dup) continue;
        if (x->n == x->cap) { int c = x->cap ? x->cap * 2 : 32; struct tfile *nf = realloc(x->f, (size_t)c * sizeof *nf); if (!nf) break; x->f = nf; x->cap = c; }
        struct tfile *t = &x->f[x->n++]; memset(t, 0, sizeof *t);
        snprintf(t->name, sizeof t->name, "%s", nm);
        snprintf(t->path, sizeof t->path, "%s/%s", dir, e->d_name);
        t->group = group;
        lhde_ini *ini = lhde_ini_load(t->path);
        if (!lhde_ini_get(ini, "Theme", "aliases", t->aliases, sizeof t->aliases)) t->aliases[0] = 0;
        if (!lhde_ini_get(ini, "Theme", "name", t->title, sizeof t->title) || !t->title[0]) snprintf(t->title, sizeof t->title, "%s", nm);
        lhde_ini_free(ini);
    }
    closedir(d);
}
static void tindex_build(struct tindex *x)
{
    memset(x, 0, sizeof *x);
    char u[PATH_MAX]; scan_dir(x, lhde_theme_user_dir(u, sizeof u), 0);
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) { char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/themes", dirs[i]); scan_dir(x, p, 1); }
}
static int has_word(const char *list, const char *w)
{
    char buf[512]; snprintf(buf, sizeof buf, "%s", list);
    for (char *save = NULL, *t = strtok_r(buf, " \t,", &save); t; t = strtok_r(NULL, " \t,", &save)) if (!strcmp(t, w)) return 1;
    return 0;
}
/* 1 theme file (group in *group), 2 bare style, 0 unknown */
static int find_theme(const struct tindex *x, const char *name, char *canon, size_t nc, char *file, size_t nf, int *group)
{
    if (!name_ok(name)) return 0;
    for (int i = 0; i < x->n; i++) if (!strcmp(x->f[i].name, name)) {
        snprintf(canon, nc, "%s", name); snprintf(file, nf, "%s", x->f[i].path); if (group) *group = x->f[i].group; return 1; }
    for (int i = 0; i < x->n; i++) if (x->f[i].aliases[0] && has_word(x->f[i].aliases, name)) {
        snprintf(canon, nc, "%s", x->f[i].name); snprintf(file, nf, "%s", x->f[i].path); if (group) *group = x->f[i].group; return 1; }
    char sp[PATH_MAX]; lhde_style_path(name, sp, sizeof sp);
    if (sp[0]) { snprintf(canon, nc, "%s", name); snprintf(file, nf, "%s", sp); if (group) *group = 2; return 2; }
    return 0;
}
int lhde_theme_lookup(const char *name, char *canon, size_t ncanon, char *file, size_t nfile)
{
    struct tindex x; tindex_build(&x);
    char c[256], f[PATH_MAX]; int g = 0;
    int k = find_theme(&x, name, c, sizeof c, f, sizeof f, &g);
    tindex_free(&x);
    if (!k) return 0;
    if (canon) snprintf(canon, ncanon, "%s", c);
    if (file) snprintf(file, nfile, "%s", k == 1 ? f : "");
    return k;
}

static int cmp_title(const void *a, const void *b)
{
    const struct tfile *x = a, *y = b;
    if (x->group != y->group) return x->group - y->group;
    return strcasecmp(x->title, y->title);
}
int lhde_theme_list(void (*fn)(const char *, const char *, const char *, int, void *), void *ud)
{
    struct tindex x; tindex_build(&x);
    /* bare styles: every styles/NAME.msstyles and styles/NAME/ with a .msstyles that no file claims by name or alias */
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) {
        char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/styles", dirs[i]);
        DIR *d = opendir(p); if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char nm[200]; snprintf(nm, sizeof nm, "%s", e->d_name);
            size_t l = strlen(nm);
            if (l > 9 && !strcasecmp(nm + l - 9, ".msstyles")) nm[l - 9] = 0;
            else { char q[PATH_MAX * 2]; snprintf(q, sizeof q, "%s/%s", p, e->d_name); if (!lhde__is_dir(q)) continue; }
            char sp[PATH_MAX]; lhde_style_path(nm, sp, sizeof sp);
            if (!sp[0]) continue;
            size_t nl = strlen(nm);
            if (nl > 3 && !strcmp(nm + nl - 3, "-xp")) continue;   /* the XP backports of Jade/Slate: retired (decision 1) */
            int claimed = 0;
            for (int k = 0; k < x.n; k++) if (x.f[k].group == 2 && !strcmp(x.f[k].name, nm)) claimed = 1;   /* NAME.msstyles + NAME/ */
            for (int k = 0; k < x.n; k++) if (!strcmp(x.f[k].name, nm) || (x.f[k].group < 2 && has_word(x.f[k].aliases, nm))) claimed = 1;
            if (claimed) continue;
            if (x.n == x.cap) { int c = x.cap ? x.cap * 2 : 32; struct tfile *nf = realloc(x.f, (size_t)c * sizeof *nf); if (!nf) break; x.f = nf; x.cap = c; }
            struct tfile *t = &x.f[x.n++]; memset(t, 0, sizeof *t);
            snprintf(t->name, sizeof t->name, "%s", nm); snprintf(t->title, sizeof t->title, "%s", nm); t->group = 2;
        }
        closedir(d);
    }
    qsort(x.f, (size_t)x.n, sizeof *x.f, cmp_title);
    for (int i = 0; i < x.n && fn; i++) fn(x.f[i].name, x.f[i].title, x.f[i].group == 2 ? "" : x.f[i].path, x.f[i].group, ud);
    int n = x.n;
    tindex_free(&x);
    return n;
}

/* ---------------------------------------------------------------- components: present or not */
/* frames/NAME: theme.ini somewhere; a glass (mode = dwm) set also needs its pixels (a PNG in any copy, or in cache/), a classic
   (msstyles) set its style, which luxwm reads live, or else its pixels */
static int frames_present(const char *name, char *dir, size_t n)
{
    char rel[300]; snprintf(rel, sizeof rel, "frames/%s", name);
    lhde_data_resolve_dir(rel, "theme.ini", dir, n);
    char ti[PATH_MAX + 16]; snprintf(ti, sizeof ti, "%s/theme.ini", dir);
    if (!lhde__exists(ti)) return 0;
    lhde_ini *ini = lhde_ini_load(ti); char mode[32] = "", style[256] = "";
    lhde_ini_get(ini, "", "mode", mode, sizeof mode);
    lhde_ini_get(ini, "", "msstyles", style, sizeof style);
    lhde_ini_free(ini);
    if (strcmp(mode, "dwm")) {
        char sp[PATH_MAX];
        if (!style[0] || style[0] == '/' || lhde_data_find(style, sp, sizeof sp)) return 1;
    }
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) {   /* imported: lhde-import's manifest (the CompositedWindow sets are read live from the style) */
        char mf[PATH_MAX + 320]; snprintf(mf, sizeof mf, "%s/frames/%s/manifest.ini", dirs[i], name);
        if (lhde__exists(mf)) return 1;
    }
    for (int i = 0; i < nd; i++)
        for (int c = 0; c < 2; c++) {
            char p[PATH_MAX + 320]; snprintf(p, sizeof p, "%s/frames/%s%s", dirs[i], name, c ? "/cache" : "");
            DIR *d = opendir(p); if (!d) continue;
            struct dirent *e; int png = 0;
            while ((e = readdir(d))) { size_t l = strlen(e->d_name); if (l > 4 && !strcasecmp(e->d_name + l - 4, ".png")) { png = 1; break; } }
            closedir(d);
            if (png) return 1;
        }
    return 0;
}
/* packs/NAME: shell.lua, and the pixels of its build (packs/<build>/, named by the pack's leading digits) */
static int pack_present(const char *name, char *dir, size_t n, char *why, size_t nw)
{
    char rel[300]; snprintf(rel, sizeof rel, "packs/%s", name);
    lhde_data_resolve_dir(rel, "shell.lua", dir, n);
    char sl[PATH_MAX + 16]; snprintf(sl, sizeof sl, "%s/shell.lua", dir);
    if (!lhde__exists(sl)) { snprintf(why, nw, "pack %s not shipped (packs/%s/shell.lua)", name, name); return 0; }
    char b[16]; lhde__build_of(name, b, sizeof b);
    if (b[0]) { char pr[64], o[PATH_MAX]; snprintf(pr, sizeof pr, "packs/%s", b);
        if (!lhde_data_find(pr, o, sizeof o)) { snprintf(why, nw, "pack %s: its pixels (packs/%s) not imported", name, b); return 0; } }
    return 1;
}
static const char *const IMG_EXT[] = { "", ".jpg", ".bmp", ".png", ".jpeg", ".gif", ".webp", NULL };
static int wallpaper_find(const char *v, char *out, size_t n)
{
    if (v[0] == '/' || (v[0] == '~' && v[1] == '/')) { lhde_data_resolve(v, out, n); return lhde__exists(out) && !lhde__is_dir(out); }
    for (int i = 0; IMG_EXT[i]; i++) {
        char rel[600]; snprintf(rel, sizeof rel, "wallpapers/%s%s", v, IMG_EXT[i]);
        if (lhde_data_find(rel, out, n) && !lhde__is_dir(out)) return 1;
    }
    /* case-insensitively (setup layouts give bliss.jpg, the theme files say Bliss): GROUP/NAME or GROUP/NAME.ext */
    char grp[512] = "", nm[512];
    const char *sl = strrchr(v, '/');
    if (sl) { snprintf(grp, sizeof grp, "/%.*s", (int)(sl - v), v); snprintf(nm, sizeof nm, "%s", sl + 1); } else snprintf(nm, sizeof nm, "%s", v);
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    for (int i = 0; i < nd; i++) {
        char dp[PATH_MAX + 600]; snprintf(dp, sizeof dp, "%s/wallpapers%s", dirs[i], grp);
        DIR *d = opendir(dp); if (!d) continue;
        struct dirent *e; int hit = 0;
        while ((e = readdir(d))) {
            const char *dot = strrchr(e->d_name, '.');
            size_t stem = dot ? (size_t)(dot - e->d_name) : strlen(e->d_name);
            if (!strcasecmp(e->d_name, nm) || (strlen(nm) == stem && !strncasecmp(e->d_name, nm, stem))) {
                snprintf(out, n, "%s/%s", dp, e->d_name); hit = !lhde__is_dir(out); if (hit) break; }
        }
        closedir(d);
        if (hit) return 1;
    }
    out[0] = 0;
    return 0;
}

/* ---------------------------------------------------------------- resolution */
/* the theme files' keys into t (absent ones only): depth 0 = the theme's own file */
struct take_ctx { lhde_theme *t; int depth; const char *who, *path, *sec; const lhde_ini *ini; };
static void take_key(const char *k, const char *v, void *ud)
{
    struct take_ctx *cc = ud;
    char full[256]; snprintf(full, sizeof full, "%s.%s", canon_section(cc->sec), k);
    for (char *p = strchr(full, '.') + 1; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (cc->depth > 0 && (!strcasecmp(full, "Theme.aliases") || !strcasecmp(full, "Theme.base"))) return;   /* the theme's own */
    if (has(cc->t, full)) return;
    char why[PATH_MAX + 300];
    if (cc->depth == 0) snprintf(why, sizeof why, "file %s", cc->path);
    else snprintf(why, sizeof why, "base %s (%s)", cc->who, cc->path);
    put(cc->t, full, v, cc->depth == 0 ? LHDE_FROM_FILE : LHDE_FROM_BASE, why);
}
static void take_sec(const char *s, void *ud)
{
    struct take_ctx *cc = ud; cc->sec = s;
    lhde_ini_keys(cc->ini, s, take_key, ud);
}
static void take_file(lhde_theme *t, const char *path, int depth, const char *who)
{
    lhde_ini *ini = lhde_ini_load(path);
    struct take_ctx c = { t, depth, who, path, NULL, ini };
    lhde_ini_sections(ini, take_sec, &c);
    lhde_ini_free(ini);
}

static void dflt(lhde_theme *t, const char *key, const char *value, const char *why)
{
    struct item *x = find_item(t, key);
    if (x && strcasecmp(x->value, "auto")) return;   /* set (an explicit `auto` is the default too) */
    if (x && !strcasecmp(x->value, "auto") && (!strcasecmp(key, "Glass.composition") || !strcasecmp(key, "VisualStyle.shellstyle") ||
                                                 !strcasecmp(key, "Explorer.navbar_preset"))) return;   /* auto is a value there */
    put(t, key, value, LHDE_FROM_DEFAULT, why);
}
static void fallback(lhde_theme *t, const char *key, const char *value, const char *why)
{
    char w[1200]; snprintf(w, sizeof w, "fallback: %s", why);
    put(t, key, value, LHDE_FROM_FALLBACK, w);
}
static int is_none(const char *v) { return !v || !v[0] || !strcasecmp(v, "none"); }

static const char *kind_name(int k)
{
    switch (k) { case LHDE_STYLE_CLASSIC: return "classic"; case LHDE_STYLE_XP: return "xp"; case LHDE_STYLE_LONGHORN: return "longhorn";
                 case LHDE_STYLE_DWM: return "dwm"; default: return "missing"; }
}

/* the default wallpaper of a build: its Bliss (4015-4093, XP), its Longhorn.jpg (4093-5112) */
static int build_wallpaper(const char *build, char *name, size_t n)
{
    static const char *const cand[] = { "Bliss", "Longhorn", "default", NULL };
    for (int i = 0; build[0] && cand[i]; i++) {
        char v[64], p[PATH_MAX]; snprintf(v, sizeof v, "%s/%s", build, cand[i]);
        if (wallpaper_find(v, p, sizeof p)) { snprintf(name, n, "%s", v); return 1; }
    }
    return 0;
}

static void apply_defaults(lhde_theme *t, const lhde_style_info *si, const char *style)
{
    char why[600], v[256];
    const char *b = get(t, "Theme.build");
    char build[16]; snprintf(build, sizeof build, "%s", b ? b : "");
    int bn = atoi(build);
    int k = si->kind;
    if (k == LHDE_STYLE_LONGHORN) snprintf(why, sizeof why, "default: v%d style%s%s, build %s, family %s", si->format ? si->format : 4,
                                           si->composited ? " with CompositedWindow" : "", si->sidebar && !si->composited ? " with Sidebar" : "",
                                           build[0] ? build : "?", si->family);
    else if (k == LHDE_STYLE_DWM) snprintf(why, sizeof why, "default: v4 style%s, build %s (5048/5112 row)", si->dwm ? " with DWMWindow" : " without CompositedWindow", build[0] ? build : "?");
    else if (k == LHDE_STYLE_XP) snprintf(why, sizeof why, "default: v3 (XP format) style");
    else snprintf(why, sizeof why, "default: no visual style (classic)");
    int lh = k == LHDE_STYLE_LONGHORN || k == LHDE_STYLE_DWM;

    dflt(t, "VisualStyle.color", si->color[0] ? si->color : "", "default: the style's first scheme");
    dflt(t, "VisualStyle.size", si->size[0] ? si->size : "", "default: the style's first size");
    dflt(t, "VisualStyle.scheme", "none", "default: only with style = classic");
    dflt(t, "VisualStyle.shellstyle", "auto", "default");
    dflt(t, "Explorer.chrome", lh ? "longhorn" : "xp", why);
    dflt(t, "Explorer.navbar", k == LHDE_STYLE_DWM ? "vision" : k == LHDE_STYLE_LONGHORN ? (bn && bn < 4060 ? "4051" : "4074") : "none", why);
    dflt(t, "Explorer.navbar_preset", "auto", "default: the navbar family's own");

    dflt(t, "Explorer.taskpane", "top", "default: under the command bar (4051)");
    dflt(t, "Desktop.icons", lh ? "longhorn4074" : "xp", why);
    /* frames: the glass frame set named like the style (frames/jade4074, frames/aero5048), for 4074-era and DWM styles */
    char fd[PATH_MAX];
    if ((k == LHDE_STYLE_DWM || (k == LHDE_STYLE_LONGHORN && (!bn || bn >= 4060))) && style && frames_present(style, fd, sizeof fd)) {
        char w2[700]; snprintf(w2, sizeof w2, "%s: the frame set named like the style", why); dflt(t, "Glass.frames", style, w2); }
    else dflt(t, "Glass.frames", "none", why);
    dflt(t, "Glass.frames_classic", "none", "default: the style's own decorations");
    dflt(t, "Glass.composition", "auto", "default");
    const char *anim = k == LHDE_STYLE_DWM ? "5048" : k == LHDE_STYLE_LONGHORN ? (bn && bn < 4060 ? "4074" : "4074plus") : "none";
    dflt(t, "Glass.animations", anim, "default: the build's own");
    /* the pack: <build>-<family> (slate -> slate, jade/aero -> aero) for Longhorn styles; the taskbar avalon when it exists */
    char pack[64] = "none";
    if (k == LHDE_STYLE_LONGHORN) {
        const char *fam = !strcmp(si->family, "jade") || !strcmp(si->family, "aero") ? "aero" : "slate";
        if (bn && bn < 4060) snprintf(pack, sizeof pack, "4051-slate");
        else snprintf(pack, sizeof pack, "%s-%s", build[0] ? build : "4074", fam);
    }
    dflt(t, "Shell.pack", pack, why);
    const char *pk = get(t, "Shell.pack");
    char pd[PATH_MAX], pw[600];
    int avalon = k == LHDE_STYLE_LONGHORN && !is_none(pk) && pack_present(pk, pd, sizeof pd, pw, sizeof pw);
    { char w2[800]; snprintf(w2, sizeof w2, "%s%s", why, k == LHDE_STYLE_LONGHORN ? (avalon ? ": its pack exists" : ": no pack of its build and family") : "");
      dflt(t, "Shell.taskbar", avalon ? "avalon" : "classic", w2); }
    dflt(t, "Shell.sidebar", "full", "default");
    if (build_wallpaper(build, v, sizeof v)) dflt(t, "Desktop.wallpaper", v, "default: the build's own wallpaper");
    else dflt(t, "Desktop.wallpaper", "none", "default: no wallpaper of the build imported");
    dflt(t, "Desktop.mode", "fill", "default");
    dflt(t, "Desktop.color", "58 110 165", "default");
    dflt(t, "Fonts.fonts", build[0] ? build : "none", "default: the build's fonts");
    dflt(t, "Fonts.smoothing", "standard", "default");
}

/* the free fallback chain of resources and icons (section 6): XP's art, then Windows 2000's, then ReactOS's (which keeps
   Windows' resource ids). The first imported one other than skip; fmt has one %s for it; its path into out */
static const char *const FREE_CHAIN[] = { "xp", "2k", "reactos", NULL };
static const char *free_chain(const char *fmt, const char *skip, char *out, size_t n)
{
    for (int i = 0; FREE_CHAIN[i]; i++) {
        char rel[128]; snprintf(rel, sizeof rel, fmt, FREE_CHAIN[i]);
        if ((!skip || strcmp(skip, FREE_CHAIN[i])) && lhde_data_find(rel, out, n)) return FREE_CHAIN[i];
    }
    out[0] = 0;
    return NULL;
}

/* section 6: what is asked for but not there becomes something plainer, with the reason; path.* derived */
static void apply_fallbacks(lhde_theme *t, const lhde_style_info *si)
{
    char p[PATH_MAX], w[1200];
    const char *v;
    /* frames, frames_classic */
    for (int c = 0; c < 2; c++) {
        const char *key = c ? "Glass.frames_classic" : "Glass.frames", *pkey = c ? "path.frames_classic" : "path.frames";
        v = get(t, key);
        p[0] = 0;
        if (!is_none(v)) {
            char nm[256]; snprintf(nm, sizeof nm, "%s", v);
            if (!frames_present(nm, p, sizeof p)) {
                snprintf(w, sizeof w, "frame set %s not imported (frames/%s: its pixels or its style)%s", nm, nm, c ? ": the style's own decorations" : ": frames_classic, else the style's decorations");
                fallback(t, key, "none", w); p[0] = 0;
            }
        }
        put(t, pkey, p, LHDE_FROM_DERIVED, "derived");
    }
    /* the pack; taskbar avalon without one: the classic taskbar */
    v = get(t, "Shell.pack"); p[0] = 0;
    if (!is_none(v)) {
        char nm[256], pw[600]; snprintf(nm, sizeof nm, "%s", v);
        if (!pack_present(nm, p, sizeof p, pw, sizeof pw)) { fallback(t, "Shell.pack", "none", pw); p[0] = 0; }
    }
    put(t, "path.pack", p, LHDE_FROM_DERIVED, "derived");
    v = get(t, "Shell.taskbar");
    if (v && !strcasecmp(v, "avalon") && is_none(get(t, "Shell.pack"))) fallback(t, "Shell.taskbar", "classic", "taskbar avalon needs a pack (none): the classic taskbar");
    /* navbar: the layout file (vision: share/navbar/vision.ini; 4051/4074 name flxp's header) */
    v = get(t, "Explorer.navbar"); p[0] = 0;
    if (!is_none(v)) { char rel[300]; snprintf(rel, sizeof rel, "navbar/%s.ini", v);
        if (!lhde_data_find(rel, p, sizeof p)) { if (!strcasecmp(v, "vision")) fallback(t, "Explorer.navbar", "4074", "navbar/vision.ini missing: luxnav cannot lay it out"); p[0] = 0; } }
    put(t, "path.navbar", p, LHDE_FROM_DERIVED, "derived");
    /* resources of the build */
    const char *b = get(t, "Theme.build"); p[0] = 0;
    if (b && b[0]) { char rel[64]; snprintf(rel, sizeof rel, "resources/%s", b);
        if (!lhde_data_find(rel, p, sizeof p)) p[0] = 0; }
    if (p[0] || !b || !b[0]) put(t, "path.resources", p, LHDE_FROM_DERIVED, "derived");
    else {
        const char *fb = free_chain("resources/%s", b, p, sizeof p);
        if (fb) snprintf(w, sizeof w, "fallback: resources/%s not imported: resources/%s", b, fb);
        else snprintf(w, sizeof w, "fallback: resources/%s not imported (nor xp, 2k, reactos): drawn art", b);
        put(t, "path.resources", p, LHDE_FROM_DERIVED, w);
    }
    /* icons: the recipe and the resources it reads; else the first of xp, 2k, reactos whose recipe and resources are imported,
       else Wine's built-in set */
    v = get(t, "Desktop.icons"); p[0] = 0;
    if (!is_none(v)) {
        char nm[128], rel[300]; snprintf(nm, sizeof nm, "%s", v); snprintf(rel, sizeof rel, "icons/%s.theme", nm);
        int ok = lhde_data_find(rel, p, sizeof p) != NULL;
        char res[256] = "";
        if (ok) { lhde_ini *ini = lhde_ini_load(p); lhde_ini_get(ini, "theme", "resources", res, sizeof res);
                  char req[256] = ""; lhde_ini_get(ini, "theme", "requires", req, sizeof req); lhde_ini_free(ini);
                  char o[PATH_MAX]; if (res[0] && !lhde_data_find(res, o, sizeof o)) ok = 0;
                  if (req[0] && !lhde_data_find(req, o, sizeof o)) { ok = 0; snprintf(res, sizeof res, "%s", req); } }   /* an icon pack */
        if (!ok) {
            const char *fb = "wine";
            for (int i = 0; FREE_CHAIN[i]; i++) {
                char o[PATH_MAX], ir[64], rr[64];
                snprintf(ir, sizeof ir, "icons/%s.theme", FREE_CHAIN[i]); snprintf(rr, sizeof rr, "resources/%s", FREE_CHAIN[i]);
                if (strcmp(nm, FREE_CHAIN[i]) && lhde_data_find(rr, o, sizeof o) && lhde_data_find(ir, o, sizeof o)) { fb = FREE_CHAIN[i]; break; }
            }
            snprintf(w, sizeof w, "icon set %s has no recipe: %s", nm, fb);
            if (res[0]) snprintf(w, sizeof w, "icon set %s: %s not imported: %s", nm, res, fb);
            fallback(t, "Desktop.icons", fb, w);
            if (strcmp(fb, "wine")) { char ir[64]; snprintf(ir, sizeof ir, "icons/%s.theme", fb); lhde_data_find(ir, p, sizeof p); } else p[0] = 0;
        }
    }
    put(t, "path.icons", p, LHDE_FROM_DERIVED, "derived");
    /* wallpaper: the file; missing: the build's own, else none */
    v = get(t, "Desktop.wallpaper"); p[0] = 0;
    if (!is_none(v)) {
        char nm[PATH_MAX]; snprintf(nm, sizeof nm, "%s", v);
        if (!wallpaper_find(nm, p, sizeof p)) {
            char bw[128];
            if (build_wallpaper(b ? b : "", bw, sizeof bw)) { snprintf(w, sizeof w, "wallpaper %s not imported: the build's %s", nm, bw); fallback(t, "Desktop.wallpaper", bw, w); wallpaper_find(bw, p, sizeof p); }
            else { snprintf(w, sizeof w, "wallpaper %s not imported: colour only", nm); fallback(t, "Desktop.wallpaper", "none", w); p[0] = 0; }
        }
    }
    put(t, "path.wallpaper", p, LHDE_FROM_DERIVED, "derived");
    /* fonts */
    v = get(t, "Fonts.fonts"); p[0] = 0;
    const char *fwhy = "derived";
    if (!is_none(v)) {
        if (v[0] == '/' || v[0] == '~') lhde_data_resolve(v, p, sizeof p);
        else { char rel[300]; snprintf(rel, sizeof rel, "fonts/%s", v); if (!lhde_data_find(rel, p, sizeof p)) p[0] = 0; }
        if (!p[0] || !lhde__exists(p)) {   /* the build's fonts, else ReactOS's (Tahoma, Marlett, ...), else fontconfig's */
            if (strcmp(v, "reactos") && lhde_data_find("fonts/reactos", p, sizeof p)) {
                snprintf(w, sizeof w, "fallback: fonts/%s not imported: fonts/reactos", v); fwhy = w;
            } else { p[0] = 0; fwhy = "fallback: fonts not imported: fontconfig's"; }
        }
    }
    put(t, "path.fonts", p, LHDE_FROM_DERIVED, fwhy);
    /* the style file and its shellstyle.dll; a classic scheme */
    put(t, "path.style", si->path, LHDE_FROM_DERIVED, "derived");
    p[0] = 0;
    v = get(t, "VisualStyle.shellstyle");
    if (si->path[0] && v && !is_none(v)) {
        if (strcasecmp(v, "auto")) lhde_data_resolve(v, p, sizeof p);
        else {
            char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s", si->path); char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
            const char *col = get(t, "VisualStyle.color");
            char cands[3][PATH_MAX + 300];
            snprintf(cands[0], sizeof cands[0], "%s/Shell/%s/shellstyle.dll", dir, col ? col : "");
            snprintf(cands[1], sizeof cands[1], "%s/shellstyle.dll", dir);
            snprintf(cands[2], sizeof cands[2], "%s/Shell/NormalColor/shellstyle.dll", dir);
            for (int i = 0; i < 3; i++) if (lhde__exists(cands[i])) { snprintf(p, sizeof p, "%s", cands[i]); break; }
        }
        if (p[0] && !lhde__exists(p)) p[0] = 0;
    }
    put(t, "path.shellstyle", p, LHDE_FROM_DERIVED, "derived");
    p[0] = 0;
    v = get(t, "VisualStyle.scheme");
    if (si->kind == LHDE_STYLE_CLASSIC && !is_none(v)) {
        char rel[300], nm[256]; snprintf(nm, sizeof nm, "%s", v); snprintf(rel, sizeof rel, "styles/%s.scheme", nm);
        if (!lhde_data_find(rel, p, sizeof p) || !lhde__exists(p)) {
            p[0] = 0;
            snprintf(w, sizeof w, "scheme %s not imported (styles/%s.scheme): Windows Standard, the built-in classic colours", nm, nm);
            fallback(t, "VisualStyle.scheme", "none", w);
        }
    }
    put(t, "path.scheme", p, LHDE_FROM_DERIVED, "derived");
}

/* section 2a: style = classic with a scheme: its [Colors] and [Metrics] (the size's [Metrics:SIZE] first) for every key the
   theme files and the current layer leave out */
struct scheme_ctx { lhde_theme *t; const char *sec, *why; };
static void scheme_key(const char *k, const char *v, void *ud)
{
    struct scheme_ctx *c = ud;
    char full[256]; snprintf(full, sizeof full, "%s.%s", c->sec, k);
    for (char *q = strchr(full, '.') + 1; *q; q++) *q = (char)tolower((unsigned char)*q);
    if (!lhde_theme_is_key(full) || has(c->t, full)) return;
    put(c->t, full, v, LHDE_FROM_SCHEME, c->why);
}
static void apply_scheme(lhde_theme *t)
{
    const char *p = get(t, "path.scheme");
    if (!p || !p[0]) return;
    char path[PATH_MAX]; snprintf(path, sizeof path, "%s", p);
    const char *sz = get(t, "VisualStyle.size");
    char size[128]; snprintf(size, sizeof size, "%s", sz ? sz : "");
    lhde_ini *ini = lhde_ini_load(path);
    char why[PATH_MAX + 300];
    const char *sn = get(t, "VisualStyle.scheme");
    snprintf(why, sizeof why, "scheme %s (%s)", sn ? sn : "?", path);
    for (int c = 0; c < 2; c++) {
        const char *base = c ? "Metrics" : "Colors";
        struct scheme_ctx x = { t, base, why };
        if (size[0] && strcasecmp(size, "Normal") && strcasecmp(size, "NormalSize")) {
            char sec[300]; snprintf(sec, sizeof sec, "%s:%s", base, size);
            lhde_ini_keys(ini, sec, scheme_key, &x);
        }
        lhde_ini_keys(ini, base, scheme_key, &x);
    }
    lhde_ini_free(ini);
}

/* settings.ini on top of the files: the per-user [desktop] wallpaper = none (luxshell's "Show the wallpaper" off; every theme) */
static void apply_settings(lhde_theme *t)
{
    char w[64];
    if (lhde_settings_get("desktop", "wallpaper", w, sizeof w) && !strcmp(w, "none"))
        put(t, "Desktop.wallpaper", "none", LHDE_FROM_SETTINGS, "settings: [desktop] wallpaper = none (per user: the wallpaper switched off)");
}

/* the style a theme ends up with: its own, else (section 6 / decision 2) luna -> luna4051, luna4074; else none (classic) */
static void choose_style(lhde_theme *t, lhde_style_info *si)
{
    const char *s = get(t, "VisualStyle.style");
    if (!s) { put(t, "VisualStyle.style", "classic", LHDE_FROM_DEFAULT, "default: no style named: classic"); s = "classic"; }
    char want[256]; snprintf(want, sizeof want, "%s", s);
    char broken[400] = "";
    if (lhde_style_probe(want, si)) {
        if (!si->broken) return;
        snprintf(broken, sizeof broken, "%s", si->broken_why);   /* XP refuses such a style: so do we */
    }
    static const char *const alt[][3] = { { "luna", "luna4051", "luna4074" }, { "luna-xp", "luna", "luna4051" }, { NULL, NULL, NULL } };
    for (int i = 0; alt[i][0]; i++) {
        if (strcmp(alt[i][0], want)) continue;
        for (int j = 1; j < 3; j++) if (lhde_style_probe(alt[i][j], si) && !si->broken) {
            char w[600]; snprintf(w, sizeof w, "style %s not imported: %s", want, alt[i][j]);
            fallback(t, "VisualStyle.style", alt[i][j], w); return; }
    }
    char w[800];
    if (broken[0]) snprintf(w, sizeof w, "style %s is broken (%s): no visual style", want, broken);
    else snprintf(w, sizeof w, "style %s not imported (styles/%s.msstyles): no visual style", want, want);
    fallback(t, "VisualStyle.style", "classic", w);
    lhde_style_probe("classic", si);
}

/* the current layer of settings.ini for theme canon (its [current] theme = canon), as key/value pairs (internal.h) */
void lhde__layer_free(struct lhde__layer *l) { for (int i = 0; i < 2 * l->n; i++) free(l->kv[i]); free(l->kv); l->kv = NULL; l->n = l->cap = 0; }
void lhde__layer_add(struct lhde__layer *l, const char *k, const char *v)
{
    if (l->n == l->cap) { int c = l->cap ? l->cap * 2 : 16; char **nk = realloc(l->kv, (size_t)c * 2 * sizeof *nk); if (!nk) return; l->kv = nk; l->cap = c; }
    l->kv[2 * l->n] = strdup(k); l->kv[2 * l->n + 1] = strdup(v ? v : ""); l->n++;
}
static void layer_cb(const char *k, const char *v, void *ud)
{
    if (strchr(k, '.')) lhde__layer_add(ud, k, v);   /* `theme` is the owner, not a key */
}
void lhde__current_layer(const char *canon, struct lhde__layer *l)
{
    memset(l, 0, sizeof *l);
    char p[PATH_MAX]; lhde_settings_path(p, sizeof p);
    lhde_ini *ini = lhde_ini_load(p);
    char owner[256] = "";
    if (lhde_ini_get(ini, "current", "theme", owner, sizeof owner) && owner[0] && canon && !strcmp(owner, canon))
        lhde_ini_keys(ini, "current", layer_cb, l);
    lhde_ini_free(ini);
}
/* a layer key: Section.key with the section's spelling and the key lower case; "" when it is none (Theme.name/base/aliases) */
void lhde__layer_key(const char *k, char *out, size_t n)
{
    out[0] = 0;
    const char *dot = strchr(k, '.');
    if (!dot || dot == k || !dot[1]) return;
    char sec[64]; snprintf(sec, sizeof sec, "%.*s", (int)(dot - k), k);
    snprintf(out, n, "%s.%s", canon_section(sec), dot + 1);
    for (char *q = strchr(out, '.') + 1; *q; q++) *q = (char)tolower((unsigned char)*q);
    if (!strcasecmp(out, "Theme.name") || !strcasecmp(out, "Theme.base") || !strcasecmp(out, "Theme.aliases")) out[0] = 0;
}

static lhde_theme *resolve_ex(const char *name, int flags, const char *const *kv, int nkv, char *err, size_t nerr);
lhde_theme *lhde_theme_resolve(const char *name, char *err, size_t nerr) { return resolve_ex(name, 0, NULL, -1, err, nerr); }
lhde_theme *lhde_theme_resolve_flags(const char *name, int flags, char *err, size_t nerr) { return resolve_ex(name, flags, NULL, -1, err, nerr); }
lhde_theme *lhde_theme_resolve_with(const char *name, const char *const *kv, int n, char *err, size_t nerr)
{
    return resolve_ex(name, 0, kv, n < 0 ? 0 : n, err, nerr);
}
lhde_theme *lhde_theme_resolve_current(char *err, size_t nerr)
{
    char b[256];
    if (!lhde_settings_get("theme", "name", b, sizeof b)) { if (err && nerr) snprintf(err, nerr, "no theme selected (settings.ini [theme] name)"); return NULL; }
    char name[256]; snprintf(name, sizeof name, "%s", b);
    return resolve_ex(name, 0, NULL, -1, err, nerr);
}

static lhde_theme *resolve_ex(const char *name, int flags, const char *const *kv, int nkv, char *err, size_t nerr)
{
    if (err && nerr) err[0] = 0;
    struct tindex x; tindex_build(&x);
    char canon[256], file[PATH_MAX]; int group = 0;
    int k = find_theme(&x, name, canon, sizeof canon, file, sizeof file, &group);
    if (!k) { if (err) snprintf(err, nerr, "no theme %s (no theme file, alias or style of that name)", name ? name : "(null)"); tindex_free(&x); return NULL; }
    lhde_theme *t = calloc(1, sizeof *t);
    if (!t) { tindex_free(&x); return NULL; }
    put(t, "theme", canon, LHDE_FROM_DERIVED, strcmp(canon, name) ? "an alias resolved" : "derived");
    put(t, "file", k == 1 ? file : "", LHDE_FROM_DERIVED, k == 1 ? (group ? "shipped" : "the user's") : "a bare style");
    char seen[9][256]; int nseen = 0;
    char cur[256], curfile[PATH_MAX]; int curk = k;
    snprintf(cur, sizeof cur, "%s", canon); snprintf(curfile, sizeof curfile, "%s", file);
    char chain[2048] = "";
    for (int depth = 0; ; depth++) {
        for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], cur)) {
            if (err) snprintf(err, nerr, "base cycle: %s -> %s", chain, cur);
            lhde_theme_free(t); tindex_free(&x); return NULL;
        }
        if (depth >= 8) { if (err) snprintf(err, nerr, "base chain deeper than 8: %s -> %s", chain, cur); lhde_theme_free(t); tindex_free(&x); return NULL; }
        snprintf(seen[nseen++], sizeof seen[0], "%s", cur);
        size_t cl = strlen(chain); snprintf(chain + cl, sizeof chain - cl, "%s%s", cl ? " -> " : "", cur);
        if (curk == 2) {   /* a bare style: VisualStyle.style = its name, everything else from its defaults */
            if (!has(t, "VisualStyle.style")) {
                char w[PATH_MAX + 64];
                if (depth == 0) snprintf(w, sizeof w, "file %s (a bare style)", curfile);
                else snprintf(w, sizeof w, "base %s (a bare style, %s)", cur, curfile);
                put(t, "VisualStyle.style", cur, depth ? LHDE_FROM_BASE : LHDE_FROM_FILE, w);
            }
            if (depth == 0) put(t, "Theme.name", cur, LHDE_FROM_FILE, "a bare style: its name");
            break;
        }
        take_file(t, curfile, depth, cur);
        lhde_ini *ini = lhde_ini_load(curfile); char base[256] = "";
        lhde_ini_get(ini, "Theme", "base", base, sizeof base);
        lhde_ini_free(ini);
        if (!base[0]) break;
        char bc[256], bf[PATH_MAX];
        int bk = find_theme(&x, base, bc, sizeof bc, bf, sizeof bf, NULL);
        if (!bk) {
            char w[600]; snprintf(w, sizeof w, "fallback: base %s not found (no theme file, alias or style): the chain ends at %s", base, cur);
            put(t, "Theme.base", base, LHDE_FROM_FALLBACK, w);
            break;
        }
        snprintf(cur, sizeof cur, "%s", bc); snprintf(curfile, sizeof curfile, "%s", bf); curk = bk;
    }
    put(t, "chain", chain, LHDE_FROM_DERIVED, "the base chain");
    tindex_free(&x);
    apply_settings(t);
    /* the current layer: the stored one of this theme, or the caller's */
    {
        struct lhde__layer stored = { NULL, 0, 0 };
        if (nkv < 0 && !(flags & LHDE_RESOLVE_FILES)) { lhde__current_layer(canon, &stored); kv = (const char *const *)stored.kv; nkv = stored.n; }
        int applied = 0;
        for (int i = 0; kv && i < nkv; i++) {
            char k[256]; lhde__layer_key(kv[2 * i], k, sizeof k);
            if (!k[0]) continue;
            put(t, k, kv[2 * i + 1], LHDE_FROM_CURRENT, "current: settings.ini [current] (unsaved changes)");
            applied++;
        }
        char nb[16]; snprintf(nb, sizeof nb, "%d", applied);
        put(t, "current", nb, LHDE_FROM_DERIVED, "the current layer's keys applied");
        lhde__layer_free(&stored);
    }
    if (!has(t, "Theme.name")) put(t, "Theme.name", canon, LHDE_FROM_DEFAULT, "default: the theme's name");
    lhde_style_info si;
    choose_style(t, &si);
    const char *style = get(t, "VisualStyle.style");
    char style_c[256]; snprintf(style_c, sizeof style_c, "%s", style ? style : "classic");
    if (!has(t, "Theme.build") || !get(t, "Theme.build")[0]) {
        char b[16]; snprintf(b, sizeof b, "%s", si.build);
        if (!b[0] && si.kind == LHDE_STYLE_CLASSIC) snprintf(b, sizeof b, "xp");
        put(t, "Theme.build", b, LHDE_FROM_DEFAULT, "default: the style's build (its build-qualified name; v3: xp)");
    }
    apply_defaults(t, &si, si.kind == LHDE_STYLE_CLASSIC ? NULL : style_c);
    apply_fallbacks(t, &si);
    if (si.kind == LHDE_STYLE_CLASSIC) apply_scheme(t);
    put(t, "style.kind", kind_name(si.kind), LHDE_FROM_DERIVED, "derived");
    char fmt[8]; snprintf(fmt, sizeof fmt, "%d", si.format);
    put(t, "style.format", fmt, LHDE_FROM_DERIVED, "derived");
    put(t, "style.family", si.family, LHDE_FROM_DERIVED, "derived");
    /* the style the theme names is not imported and it fell to classic (luna -> luna4051 is a style still): theme lists hide it */
    { const char *vs = get(t, "VisualStyle.style");
      int miss = lhde_theme_source(t, "VisualStyle.style") == LHDE_FROM_FALLBACK && vs && !strcasecmp(vs, "classic");
      put(t, "style.missing", miss ? "1" : "0", LHDE_FROM_DERIVED, miss ? "derived: the theme's style is not imported" : "derived"); }
    return t;
}

int lhde_theme_style_missing(const lhde_theme *t)
{
    const char *v = t ? get(t, "style.missing") : NULL;
    return v && !strcmp(v, "1");
}
int lhde_theme_has_style(const char *name)
{
    lhde_theme *t = lhde_theme_resolve_flags(name, LHDE_RESOLVE_FILES, NULL, 0);
    if (!t) return -1;
    int r = !lhde_theme_style_missing(t);
    lhde_theme_free(t);
    return r;
}

/* ---------------------------------------------------------------- serialization (_LH_THEME_RESOLVED) */
char *lhde_theme_serialize(const lhde_theme *t, unsigned long serial)
{
    size_t len = 64;
    for (int i = 0; t && i < t->n; i++) len += strlen(t->it[i].key) + strlen(t->it[i].value) + 3;
    char *out = malloc(len), *p = out;
    if (!out) return NULL;
    const char *nm = get(t, "theme");
    p += sprintf(p, "theme=%s\nserial=%lu\n", nm ? nm : "", serial);
    for (int i = 0; t && i < t->n; i++) {
        if (!strcmp(t->it[i].key, "theme")) continue;
        p += sprintf(p, "%s=", t->it[i].key);
        for (const char *c = t->it[i].value; *c; c++) *p++ = (*c == '\n' || *c == '\r') ? ' ' : *c;
        *p++ = '\n';
    }
    *p = 0;
    return out;
}
lhde_theme *lhde_theme_parse(const char *text, unsigned long *serial)
{
    if (serial) *serial = 0;
    if (!text || !text[0]) return NULL;
    lhde_theme *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p);
        char *line = strndup(p, l);
        char *eq = line ? strchr(line, '=') : NULL;
        if (eq) {
            *eq = 0;
            if (!strcmp(line, "serial")) { if (serial) *serial = strtoul(eq + 1, NULL, 10); }
            else put(t, line, eq + 1, LHDE_FROM_DERIVED, "published");
        }
        free(line);
        if (!e) break;
        p = e + 1;
    }
    if (!has(t, "theme")) { lhde_theme_free(t); return NULL; }
    return t;
}

/* ---------------------------------------------------------------- user edits (section 3) */
int lhde_theme_set_key(const char *name, const char *key, const char *value, char *selected, size_t nsel)
{
    if (selected && nsel) selected[0] = 0;
    const char *dot = key ? strchr(key, '.') : NULL;
    if (!dot || dot == key || !dot[1]) return -1;
    char canon[256], file[PATH_MAX];
    int k = lhde_theme_lookup(name, canon, sizeof canon, file, sizeof file);
    if (!k) return -1;
    char udir[PATH_MAX], target[PATH_MAX + 300], tname[300];
    lhde_theme_user_dir(udir, sizeof udir);
    size_t ul = strlen(udir);
    int own = k == 1 && !strncmp(file, udir, ul) && file[ul] == '/';
    if (own) { snprintf(target, sizeof target, "%s", file); snprintf(tname, sizeof tname, "%s", canon); }
    else {
        size_t cl = strlen(canon);
        if (cl > 9 && !strcmp(canon + cl - 9, "-modified")) snprintf(tname, sizeof tname, "%s", canon);
        else snprintf(tname, sizeof tname, "%s-modified", canon);
        snprintf(target, sizeof target, "%s/%s.theme", udir, tname);
    }
    char sec[64]; snprintf(sec, sizeof sec, "%.*s", (int)(dot - key), key);
    snprintf(sec, sizeof sec, "%s", canon_section(sec));
    lhde_ini *ini = lhde_ini_load(target);
    int fresh = !lhde__exists(target);
    if (fresh) {
        lhde_theme *bt = lhde_theme_resolve(canon, NULL, 0);
        const char *title = bt ? get(bt, "Theme.name") : canon;
        char hdr[1024]; snprintf(hdr, sizeof hdr, "; %s with the user's changes (docs/THEME-FORMAT.md section 3): only the keys that differ from base", canon);
        lhde_ini_header(ini, hdr);
        char tt[600]; snprintf(tt, sizeof tt, "%s (modified)", title && title[0] ? title : canon);
        lhde_ini_set(ini, "Theme", "name", tt);
        lhde_ini_set(ini, "Theme", "base", canon);
        lhde_theme_free(bt);
    }
    /* a value equal to what the base gives is not stored */
    const char *store = value;
    if (value) {
        char base[256] = "";
        lhde_ini_get(ini, "Theme", "base", base, sizeof base);
        lhde_theme *bt = base[0] ? lhde_theme_resolve(base, NULL, 0) : NULL;
        const char *bv = bt ? get(bt, key) : NULL;
        if (bv && !strcmp(bv, value) && lhde_theme_source(bt, key) != LHDE_FROM_FALLBACK) store = NULL;
        lhde_theme_free(bt);
    }
    int changed = lhde_ini_set(ini, sec, dot + 1, store);
    if (selected) snprintf(selected, nsel, "%s", tname);
    if (!changed && !fresh) { lhde_ini_free(ini); return 0; }
    int r = lhde_ini_save(ini, target);
    lhde_ini_free(ini);
    return r ? -1 : 1;
}

