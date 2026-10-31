/* lhde-theme explain and lhde-theme dirs: where every visible part of a theme comes from, and the three trees
   (docs/USAGE.md "When something looks wrong", docs/LAYOUT.md). Part of the lhde-theme program, not of the library. */
#define _GNU_SOURCE
#include "lhde/data.h"
#include "lhde/theme.h"
#include "lhde/settings.h"
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int lhde_explain(const char *name);
int lhde_explain_files(const char *name);
int lhde_dirs_report(void);

static int isdir(const char *p) { struct stat st; return p && p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static const char *nz(const char *s) { return s ? s : ""; }
static int none(const char *v) { return !v || !v[0] || !strcasecmp(v, "none"); }

/* ~ for $HOME */
static const char *tilde(const char *p, char *out, size_t n)
{
    const char *h = getenv("HOME");
    size_t l = h ? strlen(h) : 0;
    if (l > 1 && !strncmp(p, h, l) && (p[l] == '/' || !p[l])) snprintf(out, n, "~%s", p + l);
    else snprintf(out, n, "%s", p);
    return out;
}

/* a file as the user should look for it: its real place, and the tree it lies in */
static const char *where(const char *path, char *out, size_t n)
{
    if (!path || !path[0]) { snprintf(out, n, "-"); return out; }
    char rp[PATH_MAX], rel[PATH_MAX], t[PATH_MAX];
    if (!realpath(path, rp)) { snprintf(out, n, "%s (does not exist)", tilde(path, t, sizeof t)); return out; }
    int k = lhde_data_origin(path, rel, sizeof rel);
    snprintf(out, n, "%s%s [%s]", tilde(rp, t, sizeof t), isdir(rp) ? "/" : "", lhde_tree_name(k));
    return out;
}

static int count_ext(const char *dir, const char *ext)
{
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *e; int n = 0; size_t el = strlen(ext);
    while ((e = readdir(d))) { size_t l = strlen(e->d_name); if (e->d_name[0] != '.' && l > el && !strcasecmp(e->d_name + l - el, ext)) n++; }
    closedir(d);
    return n;
}

/* ---- the table ---- */
#define MORE 8
struct row { char comp[32], set[200], status[200], file[PATH_MAX + 64], more[MORE][PATH_MAX + 200]; int nmore; };
static struct row rows[32];
static int nrows;
static struct row *row(const char *comp, const char *set)
{
    struct row *r = &rows[nrows++]; memset(r, 0, sizeof *r);
    snprintf(r->comp, sizeof r->comp, "%s", comp); snprintf(r->set, sizeof r->set, "%s", set && set[0] ? set : "-");
    snprintf(r->status, sizeof r->status, "ok");
    return r;
}
static void more(struct row *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void more(struct row *r, const char *fmt, ...)
{
    if (r->nmore >= MORE) return;
    va_list ap; va_start(ap, fmt); vsnprintf(r->more[r->nmore++], sizeof r->more[0], fmt, ap); va_end(ap);
}
static void file(struct row *r, const char *path) { where(path, r->file, sizeof r->file); }
/* the status from the record: a key that fell back says so, with its reason */
static int status(struct row *r, const lhde_theme *t, const char *key)
{
    if (lhde_theme_source(t, key) == LHDE_FROM_FALLBACK) {
        snprintf(r->status, sizeof r->status, "missing -> fallback %s", nz(lhde_theme_get(t, key)));
        const char *w = lhde_theme_why(t, key);
        if (w) more(r, "why: %s", !strncmp(w, "fallback: ", 10) ? w + 10 : w);
        return 1;
    }
    return 0;
}

/* a manifest's recipe and inputs ("composited jade4074 <- styles/jade4074.msstyles = 4074/themes/Jade/jade.msstyles") */
struct kv { char buf[2048]; };
static void input_one(const char *k, const char *v, void *ud)
{
    (void)v; struct kv *x = ud;
    char p[PATH_MAX], w[PATH_MAX + 64], rel[PATH_MAX];
    size_t l = strlen(x->buf);
    if (lhde_data_find(k, p, sizeof p)) { int t = lhde_data_origin(p, rel, sizeof rel); snprintf(w, sizeof w, "%s = %s [%s]", k, rel, lhde_tree_name(t)); }
    else snprintf(w, sizeof w, "%s (not imported)", k);
    snprintf(x->buf + l, sizeof x->buf - l, "%s%s", l ? ", " : "", w);
}
static void manifest(struct row *r, const char *path, const char *label)
{
    lhde_ini *m = lhde_ini_load(path);
    if (!m) return;
    char kind[64] = "", args[200] = "", when[32] = "";
    lhde_ini_get(m, "recipe", "kind", kind, sizeof kind); lhde_ini_get(m, "recipe", "args", args, sizeof args);
    lhde_ini_get(m, "recipe", "when", when, sizeof when);
    struct kv x; x.buf[0] = 0;
    lhde_ini_keys(m, "inputs", input_one, &x);
    more(r, "%s: recipe %s %s (%s) <- %s", label, kind, args, when, x.buf[0] ? x.buf : "?");
    lhde_ini_free(m);
}

/* frames/NAME: its theme.ini, and where its pixels come from */
static void frames_detail(struct row *r, const char *name, const char *dir, const lhde_style_info *si)
{
    char ti[PATH_MAX + 16], w[PATH_MAX + 64]; snprintf(ti, sizeof ti, "%s/theme.ini", dir);
    file(r, ti);
    lhde_ini *ini = lhde_ini_load(ti);
    char mode[32] = "", ms[256] = "";
    lhde_ini_get(ini, "", "mode", mode, sizeof mode); lhde_ini_get(ini, "", "msstyles", ms, sizeof ms);
    lhde_ini_free(ini);
    char rel[300], mf[PATH_MAX], pix[PATH_MAX] = "";
    snprintf(rel, sizeof rel, "frames/%s/manifest.ini", name);
    int hm = lhde_data_find(rel, mf, sizeof mf) != NULL;
    /* the pixels: the first copy of frames/NAME with PNGs (cache/ too) */
    const char *dirs[LHDE_DATA_MAX]; int nd = lhde_data_dirs(dirs, LHDE_DATA_MAX), npng = 0;
    for (int i = 0; i < nd && !pix[0]; i++)
        for (int c = 0; c < 2 && !pix[0]; c++) {
            char p[PATH_MAX + 320]; snprintf(p, sizeof p, "%s/frames/%s%s", dirs[i], name, c ? "/cache" : "");
            int k = count_ext(p, ".png"); if (k) { snprintf(pix, sizeof pix, "%s", p); npng = k; }
        }
    if (!strcmp(mode, "dwm")) {
        /* luxwm (frame.c theme_png): a texture comes from the open style's CompositedWindow streams when it has them (4042-4093
           styles, aero-patch), else from the PNGs (5048's uDWM bitmaps, 5112's DWMWindow composition) */
        if (si->composited)
            more(r, "glass: luxwm reads the CompositedWindow streams live from the visual style (%s); the PNGs below only serve "
                 "LHWM_THEME_SOURCE=dir", si->path[0] ? where(si->path, w, sizeof w) : "the style");
        else more(r, "glass: luxwm draws the PNGs below (the visual style has no CompositedWindow)");
    } else more(r, "classic frame set: luxwm draws the PNGs below%s%s", ms[0] ? ", with the metrics of " : "", ms);
    if (pix[0]) more(r, "pixels: %s (%d PNGs)", where(pix, w, sizeof w), npng);
    else more(r, "pixels: none found (lhde-import --regenerate)");
    if (hm) manifest(r, mf, "made by");
    else if (pix[0]) more(r, "made by: no manifest (copied in, e.g. from aap's workspace; lhde-import --regenerate remakes it when a recipe "
                              "makes this set)");
}


/* the icon recipe's DLLs: every pe:NAME# of [icons] */
struct dlls { char names[512]; };
static void dll_one(const char *k, const char *v, void *ud)
{
    (void)k; struct dlls *d = ud;
    for (const char *s = strstr(v, "pe:"); s; s = strstr(s + 3, "pe:")) {
        char nm[64]; size_t i = 0; s += 3;
        while (s[i] && s[i] != '#' && s[i] != ' ' && i < sizeof nm - 1) { nm[i] = s[i]; i++; }
        nm[i] = 0; s -= 3;
        if (!nm[0] || strstr(d->names, nm)) continue;
        size_t l = strlen(d->names); snprintf(d->names + l, sizeof d->names - l, "%s%s", l ? " " : "", nm);
    }
}

static void print_rows(void)
{
    int wc = 9, ws = 7, wt = 6;
    for (int i = 0; i < nrows; i++) {
        int a = (int)strlen(rows[i].comp), b = (int)strlen(rows[i].set), c = (int)strlen(rows[i].status);
        if (a > wc) wc = a;
        if (b > ws && b <= 28) ws = b;
        if (c > wt && c <= 30) wt = c;
    }
    printf("%-*s  %-*s  %-*s  %s\n", wc, "COMPONENT", ws, "SETTING", wt, "STATUS", "FILE [tree]");
    for (int i = 0; i < nrows; i++) {
        struct row *r = &rows[i];
        printf("%-*s  %-*s  %-*s  %s\n", wc, r->comp, ws, r->set, wt, r->status, r->file[0] ? r->file : "-");
        for (int k = 0; k < r->nmore; k++) printf("%*s  %s\n", wc, "", r->more[k]);
    }
}

int lhde_explain(const char *name)
{
    char err[512], buf[PATH_MAX], w[PATH_MAX + 64];
    lhde_theme *t;
    if (name) t = lhde_theme_resolve(name, err, sizeof err);
    else { t = lhde_theme_resolve_current(err, sizeof err); if (!t && !err[0]) snprintf(err, sizeof err, "no theme selected (lhde-theme set NAME)"); }
    if (!t) { fprintf(stderr, "lhde-theme: %s\n", err); return 1; }
#define G(k) nz(lhde_theme_get(t, k))
    lhde_style_info si; lhde_style_probe(G("VisualStyle.style"), &si);
    const char *tf = G("file");
    printf("theme %s \"%s\": %s\n", G("theme"), G("Theme.name"), tf[0] ? where(tf, w, sizeof w) : "a bare visual style (no theme file)");
    if (strcmp(G("chain"), G("theme")) && G("chain")[0]) printf("  base chain %s\n", G("chain"));
    if (strcmp(G("current"), "0") && G("current")[0])
        printf("  plus %s unsaved change%s from %s [current] (lhde-theme layer; Display Properties' \"(Modified)\")\n", G("current"),
               strcmp(G("current"), "1") ? "s" : "", tilde(lhde_settings_path(buf, sizeof buf), w, sizeof w));
    printf("  composition %s: %s\n\n", G("Glass.composition"),
           !strcasecmp(G("Glass.composition"), "off") ? "never glass" : !strcasecmp(G("Glass.composition"), "on") ? "a compositor runs: the glass frame set" :
           "the glass frame set when a compositor runs, else the classic one");
    nrows = 0;
    struct row *r;
    /* the visual style */
    int classic = si.kind == LHDE_STYLE_CLASSIC;
    char set[200];
    snprintf(set, sizeof set, "%s%s%s%s%s", G("VisualStyle.style"), classic ? "" : " ", classic ? "" : G("VisualStyle.color"),
             classic ? "" : "/", classic ? "" : G("VisualStyle.size"));
    r = row("visual style", set);
    if (!status(r, t, "VisualStyle.style")) {
        if (classic) { snprintf(r->status, sizeof r->status, "ok"); more(r, "Windows Classic: no visual style; wuxtheme and luxwm draw user32's look from [Colors]/[Metrics]"); }
        else if (si.kind == LHDE_STYLE_MISSING) snprintf(r->status, sizeof r->status, "missing");
        else { file(r, G("path.style")); more(r, "%s style%s%s, family %s%s", si.kind == LHDE_STYLE_XP ? "XP (v3)" : si.kind == LHDE_STYLE_DWM ? "DWM (v4, DWMWindow)" : "Longhorn (v4)",
                    si.build[0] ? ", build " : "", si.build, si.family, si.composited ? ", with CompositedWindow (glass parts)" : ""); }
    }
    if (G("path.shellstyle")[0]) more(r, "shellstyle: %s", where(G("path.shellstyle"), w, sizeof w));
    if (classic) {
        r = row("colour scheme", G("VisualStyle.scheme"));
        if (!status(r, t, "VisualStyle.scheme")) {
            if (none(G("VisualStyle.scheme"))) { snprintf(r->status, sizeof r->status, "off"); more(r, "Windows Standard, the built-in classic colours"); }
            else file(r, G("path.scheme"));
        }
    }
    /* frames */
    r = row("frames (glass)", G("Glass.frames"));
    if (!status(r, t, "Glass.frames")) {
        if (none(G("Glass.frames"))) { snprintf(r->status, sizeof r->status, "off"); more(r, "no glass: the classic frames below, also with a compositor"); }
        else frames_detail(r, G("Glass.frames"), G("path.frames"), &si);
    } else if (G("path.frames")[0]) frames_detail(r, G("Glass.frames"), G("path.frames"), &si);
    r = row("frames (classic)", G("Glass.frames_classic"));
    if (!status(r, t, "Glass.frames_classic")) {
        if (none(G("Glass.frames_classic"))) {
            snprintf(r->status, sizeof r->status, "ok");
            if (classic || si.kind == LHDE_STYLE_MISSING) more(r, "drawn by luxwm (classic): user32's caption, edges and Marlett buttons in the scheme's colours");
            else { more(r, "drawn by luxwm from the visual style's own window parts (Window.Caption, FrameLeft, ...)"); file(r, G("path.style")); }
        } else frames_detail(r, G("Glass.frames_classic"), G("path.frames_classic"), &si);
    }
    /* the taskbar and the shell pack */
    const char *tb = G("Shell.taskbar");
    r = row("taskbar", tb);
    if (!status(r, t, "Shell.taskbar")) {
        if (!strcasecmp(tb, "avalon")) more(r, "luxshell's Avalon bar, drawn from the shell pack below");
        else if (!strcasecmp(tb, "classic")) more(r, "flxp-taskbar, drawn from the visual style's Taskbar/TrayNotify parts%s", classic ? " (classic: user32's look)" : "");
        else snprintf(r->status, sizeof r->status, "off");
    }
    r = row("shell pack", G("Shell.pack"));
    if (!status(r, t, "Shell.pack")) {
        if (none(G("Shell.pack"))) snprintf(r->status, sizeof r->status, "off");
        else {
            char sl[PATH_MAX + 16]; snprintf(sl, sizeof sl, "%s/shell.lua", G("path.pack")); file(r, sl);
            char b[16] = ""; { const char *p = G("Shell.pack"); size_t i = 0; while (p[i] >= '0' && p[i] <= '9' && i < sizeof b - 1) { b[i] = p[i]; i++; } b[i] = 0; }
            if (!strcasecmp(tb, "avalon")) more(r, "used: the taskbar is avalon"); else more(r, "not used while taskbar = %s (the sidebar still uses it)", tb);
            if (b[0]) {
                char rel[64], pd[PATH_MAX]; snprintf(rel, sizeof rel, "packs/%s", b);
                if (lhde_data_find(rel, pd, sizeof pd)) {
                    more(r, "images: %s", where(pd, w, sizeof w));
                    DIR *d = opendir(pd); struct dirent *e;
                    while (d && (e = readdir(d))) if (!strncmp(e->d_name, "manifest", 8)) {
                        char mp[PATH_MAX + 300]; snprintf(mp, sizeof mp, "%s/%s", pd, e->d_name); manifest(r, mp, e->d_name);
                    }
                    if (d) closedir(d);
                } else more(r, "images: packs/%s not imported (lhde-import)", b);
            }
        }
    }
    /* the start menu: flxp's start panel (flxp-startmenu next to the Avalon taskbar, flxp-taskbar's own with the classic bar) */
    r = row("start menu", "flxp");
    snprintf(r->status, sizeof r->status, "ok");
    more(r, "flxp's start panel, drawn from the visual style's StartPanel parts%s", classic ? " (classic: the 2000-style menu)" : "");
    /* the navigation bar */
    char ns[200]; snprintf(ns, sizeof ns, "%s%s%s", G("Explorer.navbar"), strcmp(G("Explorer.navbar_preset"), "auto") ? " " : "", strcmp(G("Explorer.navbar_preset"), "auto") ? G("Explorer.navbar_preset") : "");
    r = row("navbar", ns);
    if (!status(r, t, "Explorer.navbar")) {
        if (none(G("Explorer.navbar"))) { snprintf(r->status, sizeof r->status, "off"); more(r, "Explorer chrome %s without a Longhorn navigation bar", G("Explorer.chrome")); }
        else if (!G("path.navbar")[0]) snprintf(r->status, sizeof r->status, "missing");
        else {
            file(r, G("path.navbar"));
            lhde_ini *ini = lhde_ini_load(G("path.navbar")); char eng[32] = "";
            lhde_ini_get(ini, "navbar", "engine", eng, sizeof eng); lhde_ini_free(ini);
            if (!strcasecmp(G("Explorer.navbar"), "vision") || !strcasecmp(eng, "luxnav")) {
                more(r, "drawn by luxnav; art: the visual style's navigation parts (%s)", si.path[0] ? where(si.path, w, sizeof w) : "no style: drawn plain");
                const char *fd = G("path.frames")[0] ? G("path.frames") : G("path.frames_classic");
                char nv[PATH_MAX];
                if (fd[0] && lhde_data_find_in(fd, "nav.ini", nv, sizeof nv)) more(r, "tuning: %s (the frame set's nav.ini)", where(nv, w, sizeof w));
            } else {
                more(r, "drawn by %s's own header; art: the Avalon framelets of resources/%s (Microsoft.Windows.Client.dll beside its system32)",
                     eng[0] ? eng : "flxp", G("Theme.build")[0] ? G("Theme.build") : "4074");
            }
        }
    }
    if (!strcasecmp(G("Explorer.chrome"), "longhorn"))
        more(r, "task band (View > Task Band): %s", !strcasecmp(G("Explorer.taskpane"), "bottom") ? "under the list (Explorer.taskpane = bottom)"
                                                                                                   : "under the command bar");
    /* icons */
    r = row("icons", G("Desktop.icons"));
    if (!status(r, t, "Desktop.icons") || G("path.icons")[0]) {
        if (none(G("Desktop.icons"))) snprintf(r->status, sizeof r->status, "off");
        else if (!G("path.icons")[0]) more(r, "Wine's built-in icons");
        else {
            file(r, G("path.icons"));
            lhde_ini *ini = lhde_ini_load(G("path.icons")); char res[256] = ""; struct dlls d; d.names[0] = 0;
            lhde_ini_get(ini, "theme", "resources", res, sizeof res); lhde_ini_keys(ini, "icons", dll_one, &d); lhde_ini_free(ini);
            char rp[PATH_MAX]; int ok = res[0] && lhde_data_find(res, rp, sizeof rp) != NULL;
            if (res[0]) more(r, "resources = %s: %s", res, ok ? where(rp, w, sizeof w) : "not imported");
            for (char *save = NULL, *nm = strtok_r(d.names, " ", &save); nm; nm = strtok_r(NULL, " ", &save)) {
                char fp[PATH_MAX];
                if (strchr(nm, '/')) more(r, "reads %s: %s", nm, lhde_data_find(nm, fp, sizeof fp) ? where(fp, w, sizeof w) : "not imported");
                else if (ok) { char q[PATH_MAX * 2]; snprintf(q, sizeof q, "%s/%s", rp, nm);
                    more(r, "reads %s: %s", nm, isdir(rp) ? where(q, w, sizeof w) : "?"); }
            }
        }
    }
    /* wallpaper, fonts, resources */
    char ws[300]; snprintf(ws, sizeof ws, "%s %s", G("Desktop.wallpaper"), G("Desktop.mode"));
    r = row("wallpaper", ws);
    if (!status(r, t, "Desktop.wallpaper") || G("path.wallpaper")[0]) {
        if (none(G("Desktop.wallpaper"))) { snprintf(r->status, sizeof r->status, "off"); more(r, "colour only: %s", G("Desktop.color")); }
        else file(r, G("path.wallpaper"));
    }
    r = row("fonts", G("Fonts.fonts"));
    if (none(G("Fonts.fonts"))) { snprintf(r->status, sizeof r->status, "off"); more(r, "fontconfig's fonts"); }
    else if (!G("path.fonts")[0]) { snprintf(r->status, sizeof r->status, "missing -> fontconfig"); more(r, "why: fonts/%s not imported", G("Fonts.fonts")); }
    else { file(r, G("path.fonts")); more(r, "%d TrueType files, registered by luxwm, luxshell and flxp", count_ext(G("path.fonts"), ".ttf")); }
    r = row("resources", G("Theme.build"));
    if (!G("Theme.build")[0]) { snprintf(r->status, sizeof r->status, "off"); more(r, "no build: no Longhorn DLLs"); }
    else if (!G("path.resources")[0]) { snprintf(r->status, sizeof r->status, "missing -> drawn art"); more(r, "why: resources/%s not imported", G("Theme.build")); }
    else { file(r, G("path.resources")); more(r, "the build's shell32/imageres/explorer/browseui (icons, flxp's Longhorn art)"); }
#undef G
    print_rows();
    printf("\nTrees: user = %s, windows = %s, shipped = the install's share/lhde (lhde-theme dirs). A key's source: lhde-theme resolve %s --why.\n",
           tilde(lhde_config_dir(buf, sizeof buf), w, sizeof w), tilde(lhde_windows_dir(), err, sizeof err), lhde_theme_get(t, "theme"));
    lhde_theme_free(t);
    return 0;
}


/* ---- lhde-theme explain --files THEME: the Windows-tree files THEME reads (lhde-pack --profile essentials) ----
   One path per line, relative to the Windows tree ($LHDE_WINDOWS) and real (<source>/system32/shell32.dll, never an index/
   name), sorted, once each. What counts as read: the visual style, its shellstyle and Shell/ directory; the HIVEDEF.INF the
   classic scheme is made from; every input of the manifest of the frame sets and the pack images the theme uses (those
   recipes, run by lhde-import, name their inputs with sha256); the DLLs the icon recipe's pe: references name; the wallpaper;
   the fonts directory; and of the theme's build the DLLs flxp, luxnav and luxshell open (FLXP_DLLS). Run it on a tree that
   holds everything (a museum import); files from local/ or generated/ are not packed and are left out. */
static const char *FLXP_DLLS[] = { "shell32.dll", "browseui.dll", "explorer.exe", "shellstyle.dll", "comctl32.dll", "themeui.dll",
                                   "imageres.dll", "logonui.exe", NULL };
static char *seen[4096]; static int nseen;

static void emit_file(const char *path)
{
    char rp[PATH_MAX], wr[PATH_MAX];
    if (!path || !path[0] || !realpath(path, rp) || !realpath(lhde_windows_dir(), wr)) return;
    size_t l = strlen(wr);
    if (strncmp(rp, wr, l) || rp[l] != '/') return;
    const char *rel = rp + l + 1;
    if (!strncmp(rel, "generated/", 10) || !strncmp(rel, "local/", 6) || !strncmp(rel, "index/", 6)) return;
    struct stat st; if (stat(rp, &st) || !S_ISREG(st.st_mode)) return;
    for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], rel)) return;
    if (nseen < 4096) seen[nseen++] = strdup(rel);
}
static void emit(const char *path)
{
    char rp[PATH_MAX]; struct stat st;
    if (!path || !path[0] || !realpath(path, rp) || stat(rp, &st)) return;
    if (!S_ISDIR(st.st_mode)) { emit_file(rp); return; }
    DIR *d = opendir(rp); struct dirent *e; if (!d) return;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char q[PATH_MAX + 300]; snprintf(q, sizeof q, "%s/%s", rp, e->d_name);
        emit(q);
    }
    closedir(d);
}
static void emit_input(const char *k, const char *v, void *ud)
{
    (void)v; (void)ud; char p[PATH_MAX];
    if (lhde_data_find(k, p, sizeof p)) emit(p);
}
static void emit_manifest(const char *path)
{
    lhde_ini *m = lhde_ini_load(path);
    if (!m) return;
    lhde_ini_keys(m, "inputs", emit_input, NULL);
    lhde_ini_free(m);
}
static int cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int lhde_explain_files(const char *name)
{
    char err[512], p[PATH_MAX], rel[PATH_MAX];
    lhde_theme *t = lhde_theme_resolve(name, err, sizeof err);
    if (!t) { fprintf(stderr, "lhde-theme: %s\n", err); return 1; }
#define G(k) nz(lhde_theme_get(t, k))
    /* the visual style, its shellstyle, and the Shell/ beside it (Luna's three colour schemes) */
    if (G("path.style")[0]) {
        emit(G("path.style"));
        char rp[PATH_MAX], sh[PATH_MAX + 8];
        if (realpath(G("path.style"), rp)) { char *sl = strrchr(rp, '/'); if (sl) { *sl = 0; snprintf(sh, sizeof sh, "%s/Shell", rp); emit(sh); } }
    }
    emit(G("path.shellstyle"));
    /* the classic scheme is generated from a hive INF: xp's, else any source's */
    if (G("path.scheme")[0]) {
        const char *w = lhde_windows_dir(); DIR *d = opendir(w); struct dirent *e; char best[PATH_MAX] = "";
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "%s/%s/setup/hivedef.inf", w, e->d_name);
            if (access(p, R_OK) == 0 && (!best[0] || !strcmp(e->d_name, "xp"))) snprintf(best, sizeof best, "%s", p);
        }
        if (d) closedir(d);
        emit(best);
    }
    /* frame sets (glass, classic) and the shell pack's images: the inputs their manifests name */
    const char *fk[2] = { G("Glass.frames"), G("Glass.frames_classic") };
    for (int i = 0; i < 2; i++) if (!none(fk[i])) { snprintf(rel, sizeof rel, "frames/%s/manifest.ini", fk[i]); if (lhde_data_find(rel, p, sizeof p)) emit_manifest(p); }
    const char *pk = G("Shell.pack");
    if (!none(pk)) {
        char b[16] = ""; size_t i = 0; while (pk[i] >= '0' && pk[i] <= '9' && i < sizeof b - 1) { b[i] = pk[i]; i++; } b[i] = 0;
        snprintf(rel, sizeof rel, "packs/%s", b);
        if (b[0] && lhde_data_find(rel, p, sizeof p)) {
            char pd[PATH_MAX]; snprintf(pd, sizeof pd, "%s", p);
            DIR *d = opendir(pd); struct dirent *e;
            while (d && (e = readdir(d))) if (!strncmp(e->d_name, "manifest", 8)) {
                char mp[PATH_MAX + 300]; snprintf(mp, sizeof mp, "%s/%s", pd, e->d_name);
                lhde_ini *m = lhde_ini_load(mp); char kind[64] = "", args[200] = "";
                if (m) { lhde_ini_get(m, "recipe", "kind", kind, sizeof kind); lhde_ini_get(m, "recipe", "args", args, sizeof args); lhde_ini_free(m); }
                if (!strcmp(kind, "startpanel")) continue;   /* an old import's start panels: nothing draws them any more */
                emit_manifest(mp);
            }
            if (d) closedir(d);
        }
    }
    /* icons: the DLLs of every pe: reference */
    if (G("path.icons")[0]) {
        FILE *f = fopen(G("path.icons"), "r"); char line[1024], res[256] = "", rp[PATH_MAX] = "";
        lhde_ini *ini = lhde_ini_load(G("path.icons")); lhde_ini_get(ini, "theme", "resources", res, sizeof res); lhde_ini_free(ini);
        if (res[0]) lhde_data_find(res, rp, sizeof rp);
        while (f && fgets(line, sizeof line, f)) {
            char *semi = strchr(line, ';'); if (semi) *semi = 0;
            for (char *s = strstr(line, "pe:"); s; s = strstr(s + 3, "pe:")) {
                char nm[256]; size_t i = 0; s += 3;
                while (s[i] && s[i] != '#' && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && i < sizeof nm - 1) { nm[i] = s[i]; i++; }
                nm[i] = 0; s -= 3;
                if (!nm[0]) continue;
                if (strchr(nm, '/')) { if (lhde_data_find(nm, p, sizeof p)) emit(p); }
                else if (rp[0]) { snprintf(p, sizeof p, "%s/%s", rp, nm); emit(p); }
            }
        }
        if (f) fclose(f);
    }
    emit(G("path.wallpaper"));
    if (!none(G("Fonts.fonts"))) emit(G("path.fonts"));
    if (G("Theme.build")[0] && G("path.resources")[0])
        for (int i = 0; FLXP_DLLS[i]; i++) {
            snprintf(p, sizeof p, "%s/%s", G("path.resources"), FLXP_DLLS[i]); emit(p);
        }
#undef G
    lhde_theme_free(t);
    qsort(seen, (size_t)nseen, sizeof *seen, cmpstr);
    for (int i = 0; i < nseen; i++) printf("%s\n", seen[i]);
    return 0;
}

/* ---- lhde-theme dirs ---- */
static const char *human(long long b, char *out, size_t n)
{
    if (b >= 1024LL * 1024 * 1024) snprintf(out, n, "%.1f GB", b / 1073741824.0);
    else if (b >= 1024 * 1024) snprintf(out, n, "%.0f MB", b / 1048576.0);
    else if (b >= 1024) snprintf(out, n, "%.0f KB", b / 1024.0);
    else snprintf(out, n, "%lld B", b);
    return out;
}
static void tree_line(const char *what, const char *dir, const char *note)
{
    char t[PATH_MAX], h[32];
    if (!dir || !isdir(dir)) { printf("  %-12s %s  (none)%s%s\n", what, dir ? tilde(dir, t, sizeof t) : "-", note ? "  " : "", note ? note : ""); return; }
    long long f; long long b = lhde_du(dir, &f);
    printf("  %-12s %-44s %8s %6lld files%s%s\n", what, tilde(dir, t, sizeof t), human(b, h, sizeof h), f, note ? "  " : "", note ? note : "");
}
static const char *entry_note(const char *name)
{
    if (!strcmp(name, "generated")) return "derived pixels and schemes: delete freely, lhde-import --regenerate";
    if (!strcmp(name, "index")) return "symlinks under the names programs ask for (lhde-import --reindex)";
    if (!strcmp(name, "local")) return "dropped in by hand (lhde-import --style)";
    if (!strcmp(name, "settings.ini")) return "per-user settings and the current layer";
    if (!strcmp(name, "themes")) return "your theme files (Save As)";
    return NULL;
}
static int count_links(const char *dir)
{
    char cmd[PATH_MAX + 64]; snprintf(cmd, sizeof cmd, "find '%s' -type l 2>/dev/null | wc -l", dir);
    FILE *f = popen(cmd, "r"); int n = 0;
    if (f) { if (fscanf(f, "%d", &n) != 1) n = 0; pclose(f); }
    return n;
}
static void entries(const char *dir, const char *indent)
{
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e; char names[64][256]; int n = 0;
    while ((e = readdir(d)) && n < 64) if (e->d_name[0] != '.') snprintf(names[n++], 256, "%s", e->d_name);
    closedir(d);
    qsort(names, (size_t)n, 256, (int (*)(const void *, const void *))strcmp);
    for (int i = 0; i < n; i++) {
        char p[PATH_MAX + 300], h[32]; snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        struct stat ls; lstat(p, &ls);
        if (S_ISLNK(ls.st_mode)) { char l[PATH_MAX]; ssize_t k = readlink(p, l, sizeof l - 1); l[k > 0 ? k : 0] = 0; printf("%s%-14s -> %s\n", indent, names[i], l); continue; }
        const char *note = entry_note(names[i]);
        if (!isdir(p)) { printf("%s%-14s %8s%s%s\n", indent, names[i], human(ls.st_size, h, sizeof h), note ? "               " : "", note ? note : ""); continue; }
        if (!strcmp(names[i], "index")) { printf("%s%-14s %8s %6d links  %s\n", indent, names[i], "", count_links(p), note); continue; }
        long long f; long long b = lhde_du(p, &f);
        printf("%s%-14s %8s %6lld files%s%s\n", indent, names[i], human(b, h, sizeof h), f, note ? "  " : "", note ? note : "");
    }
}

int lhde_dirs_report(void)
{
    char buf[PATH_MAX], t[PATH_MAX], c[PATH_MAX];
    const char *dirs[LHDE_DATA_MAX]; int n = lhde_data_dirs(dirs, LHDE_DATA_MAX);
    printf("Program files (make install; never edited by hand):\n");
    const char *tree = lhde_tree();
    for (int i = 0; i < n; i++) if (lhde_data_dir_kind(i) == LHDE_TREE_SHIPPED) tree_line("shipped data", dirs[i], NULL);
    if (tree) { snprintf(buf, sizeof buf, "%s/data", tree); tree_line("source tree", buf, "(this program runs from a build tree: data/ and the code)"); }
    else if (lhde_code_dir(c, sizeof c)) tree_line("code", c, "(luxshell/ session/ tools/)");
    printf("Windows files (lhde-import; $LHDE_WINDOWS):\n");
    const char *wd = lhde_windows_dir();
    tree_line(getenv("LHDE_DATA") && *getenv("LHDE_DATA") ? "LHDE_DATA" : "windows", wd, NULL);
    if (isdir(wd)) entries(wd, "    ");
    if (lhde_old_dir())
        printf("  unmigrated %s: Windows files of the old layout, not searched; `lhde-import --migrate` moves them (-n shows the move)\n",
               tilde(lhde_old_dir(), t, sizeof t));
    printf("User files (docs/SETTINGS.md \"Files\"):\n");
    tree_line("config", lhde_config_dir(buf, sizeof buf), "what you chose: settings.ini, themes/, flxp.ini, keys, overridden shipped files");
    if (isdir(buf)) entries(buf, "    ");
    tree_line("state", lhde_state_dir(buf, sizeof buf), "what the programs remember: tile order, launch counts, remembered views, icon positions");
    if (isdir(buf)) entries(buf, "    ");
    printf("\nSearch order (first match wins; lhde-theme find REL looks a name up):\n");
    for (int i = 0; i < n; i++) printf("  %d. %-12s %s\n", i + 1, lhde_tree_name(lhde_data_dir_kind(i)), tilde(dirs[i], t, sizeof t));
    printf("settings   %s\n", tilde(lhde_settings_path(buf, sizeof buf), t, sizeof t));
    return 0;
}
