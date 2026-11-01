/* liblhde's resolver and settings store on fake files (no Microsoft files): a fresh $LHDE_DATA with empty style
   files classified by a test probe, theme files written here, $XDG_CONFIG_HOME of its own. make check-liblhde.
   Covers: inheritance and its sources, absent vs none, the defaults of each style kind (v3, v4 CompositedWindow 4074, v4
   Sidebar 4051, v4 DWM 5048/5112, classic), bare styles as themes, aliases, the fallbacks (frames, pack -> classic taskbar,
   wallpaper, style luna -> luna4051, icons, base not found), cycles and the depth limit, lhde_theme_set_key
   (NAME-modified.theme), serialization, the settings store (comments kept), the per-user [desktop] wallpaper = none, the
   three trees. */
#define _GNU_SOURCE
#include "lhde/data.h"
#include "lhde/theme.h"
#include "lhde/settings.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails, passes;
static char T[PATH_MAX];

#define CHECK(c) do { if (c) passes++; else { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static void eq(int line, const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) { passes++; return; }
    if (!got && !want) { passes++; return; }
    fails++; fprintf(stderr, "FAIL line %d: %s\n  got      [%s]\n  expected [%s]\n", line, what, got ? got : "(null)", want ? want : "(null)");
}
#define EQ(a, b) eq(__LINE__, #a, (a), (b))
static void has(int line, const char *what, const char *got, const char *needle)
{
    if (got && strstr(got, needle)) { passes++; return; }
    fails++; fprintf(stderr, "FAIL line %d: %s\n  got      [%s]\n  should contain [%s]\n", line, what, got ? got : "(null)", needle);
}
#define HAS(a, b) has(__LINE__, #a, (a), (b))

static void mkdirs(const char *d)
{
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s", d);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}
static void put(const char *rel, const char *text)
{
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", T, rel);
    char d[PATH_MAX]; snprintf(d, sizeof d, "%s", p); *strrchr(d, '/') = 0; mkdirs(d);
    FILE *f = fopen(p, "w"); if (!f) { perror(p); exit(2); }
    fputs(text, f); fclose(f);
}
static char *slurp(const char *rel)
{
    static char buf[65536];
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", T, rel);
    FILE *f = fopen(p, "r"); if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    return buf;
}

/* the test's styles: classified by their base name, no file contents */
static int probe(const char *path, lhde_style_info *o)
{
    const char *b = strrchr(path, '/'); b = b ? b + 1 : path;
    o->format = 4;
    if (!strncmp(b, "luna.", 5) || !strncmp(b, "royale", 6)) { o->format = 3; snprintf(o->family, sizeof o->family, "luna"); snprintf(o->color, sizeof o->color, "NormalColor"); snprintf(o->size, sizeof o->size, "NormalSize"); return 1; }
    if (!strncmp(b, "jade", 4) || !strncmp(b, "aero-patch", 10)) { o->composited = o->sidebar = 1; snprintf(o->family, sizeof o->family, !strncmp(b, "jade", 4) ? "jade" : "aero"); }
    else if (!strncmp(b, "slate", 5) || !strncmp(b, "luna4051", 8)) { o->sidebar = 1; snprintf(o->family, sizeof o->family, !strncmp(b, "slate", 5) ? "slate" : "luna"); }
    else if (!strncmp(b, "aero5112", 8)) { o->dwm = 1; snprintf(o->family, sizeof o->family, "aero"); }
    else if (!strncmp(b, "aero5048", 8)) snprintf(o->family, sizeof o->family, "aero");
    else return 0;
    snprintf(o->color, sizeof o->color, !strncmp(b, "aero5", 5) ? "NormalColor" : "Default");
    snprintf(o->size, sizeof o->size, "Normal");
    return 1;
}

static lhde_theme *R(const char *name)
{
    char err[512];
    lhde_theme *t = lhde_theme_resolve(name, err, sizeof err);
    if (!t) fprintf(stderr, "  (resolve %s: %s)\n", name, err);
    return t;
}
#define G(t, k) lhde_theme_get(t, k)
#define W(t, k) lhde_theme_why(t, k)

int main(void)
{
    char tmpl[] = "/tmp/lhde-resolve.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    snprintf(T, sizeof T, "%s", tmpl);
    char p[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/data", T); setenv("LHDE_DATA", p, 1);
    snprintf(p, sizeof p, "%s/config", T); setenv("XDG_CONFIG_HOME", p, 1);
    snprintf(p, sizeof p, "%s/run", T); setenv("XDG_RUNTIME_DIR", p, 1);
    snprintf(p, sizeof p, "%s/state", T); setenv("XDG_STATE_HOME", p, 1);
    setenv("HOME", T, 1); setenv("XDG_DATA_DIRS", "/nonexistent", 1); setenv("LHDE_TREE", "0", 1);
    unsetenv("LHDE_SETTINGS"); unsetenv("DISPLAY");
    lhde_data_reset();
    lhde_style_set_probe(probe);

    /* the fake import */
    const char *styles[] = { "jade4074", "slate4074", "slate4051", "aero5048", "aero5112", "luna4051", "aero-patch", "royale", NULL };
    for (int i = 0; styles[i]; i++) { char r[200]; snprintf(r, sizeof r, "data/styles/%s.msstyles", styles[i]); put(r, "x"); }
    put("data/frames/jade4074/theme.ini", "mode = dwm\n"); put("data/frames/jade4074/plate_active.png", "x");
    put("data/frames/aero5112/theme.ini", "mode = dwm\n"); put("data/frames/aero5112/cache/plate_active.png", "x");   /* manifest-first: cache/ */
    put("data/frames/aero5048/theme.ini", "mode = dwm\n");                                                            /* text only: not imported */
    put("data/frames/jade4074-classic/theme.ini", "mode = msstyles\n");
    put("data/packs/4074-aero/shell.lua", "return {}\n"); put("data/packs/4074-slate/shell.lua", "return {}\n");
    put("data/packs/4051-slate/shell.lua", "return {}\n"); put("data/packs/4074/imageres/1167.png", "x");             /* no packs/4051 */
    put("data/wallpapers/4074/Bliss.bmp", "x"); put("data/wallpapers/5112/Longhorn.jpg", "x");
    put("data/fonts/4074/seguibk.ttf", "x"); put("data/resources/4074/shell32.dll", "x");
    put("data/navbar/vision.ini", "; vision\n"); put("data/icons/longhorn4074.theme", "[theme]\nresources = resources/4074\n");
    put("data/icons/xp.theme", "[theme]\nresources = resources/xp\n");

    /* ---- defaults per style kind (bare styles) ---- */
    lhde_theme *t = R("jade4074");
    CHECK(t);
    EQ(G(t, "style.kind"), "longhorn"); EQ(G(t, "Theme.build"), "4074"); EQ(G(t, "Explorer.chrome"), "longhorn");
    EQ(G(t, "Explorer.navbar"), "4074"); EQ(G(t, "Shell.pack"), "4074-aero"); EQ(G(t, "Shell.taskbar"), "avalon");
    EQ(G(t, "Glass.frames"), "jade4074"); HAS(W(t, "Glass.frames"), "named like the style");
    EQ(G(t, "Glass.frames_classic"), "none"); EQ(G(t, "Glass.animations"), "4074plus"); EQ(G(t, "Desktop.icons"), "longhorn4074");
    EQ(G(t, "Desktop.wallpaper"), "4074/Bliss"); EQ(G(t, "VisualStyle.color"), "Default"); EQ(G(t, "file"), "");
    CHECK(lhde_theme_source(t, "Explorer.navbar") == LHDE_FROM_DEFAULT);
    HAS(G(t, "path.wallpaper"), "/data/wallpapers/4074/Bliss.bmp"); HAS(G(t, "path.fonts"), "/data/fonts/4074");
    HAS(G(t, "path.resources"), "/data/resources/4074"); HAS(G(t, "path.icons"), "icons/longhorn4074.theme");
    lhde_theme_free(t);

    t = R("slate4074");
    EQ(G(t, "Shell.pack"), "4074-slate"); EQ(G(t, "Glass.frames"), "none"); EQ(G(t, "Shell.taskbar"), "avalon");
    lhde_theme_free(t);

    t = R("slate4051");   /* 4051: navbar 4051, pack 4051-slate whose pixels (packs/4051) are missing: classic taskbar */
    EQ(G(t, "Explorer.navbar"), "4051"); EQ(G(t, "Shell.pack"), "none"); EQ(G(t, "Shell.taskbar"), "classic");
    CHECK(lhde_theme_source(t, "Shell.pack") == LHDE_FROM_FALLBACK); HAS(W(t, "Shell.pack"), "packs/4051");
    EQ(G(t, "Glass.animations"), "4074"); EQ(G(t, "Desktop.wallpaper"), "none");   /* no 4051 wallpaper imported */
    lhde_theme_free(t);

    t = R("aero5112");   /* DWMWindow: vision, classic taskbar, the frame set named like the style (pixels in cache/) */
    EQ(G(t, "style.kind"), "dwm"); EQ(G(t, "Explorer.navbar"), "vision"); EQ(G(t, "Shell.taskbar"), "classic");
    EQ(G(t, "Glass.frames"), "aero5112"); EQ(G(t, "Glass.animations"), "5048"); EQ(G(t, "Desktop.wallpaper"), "5112/Longhorn");
    HAS(G(t, "path.navbar"), "navbar/vision.ini"); HAS(G(t, "path.frames"), "frames/aero5112");
    lhde_theme_free(t);

    t = R("aero5048");   /* no DWMWindow but 5048: the DWM row; its frame set has no pixels: no glass */
    EQ(G(t, "style.kind"), "dwm"); EQ(G(t, "Glass.frames"), "none"); EQ(G(t, "Fonts.fonts"), "5048"); EQ(G(t, "path.fonts"), "");
    HAS(W(t, "path.fonts"), "fontconfig");
    lhde_theme_free(t);

    t = R("royale");     /* v3 */
    EQ(G(t, "style.kind"), "xp"); EQ(G(t, "Explorer.chrome"), "xp"); EQ(G(t, "Explorer.navbar"), "none");
    EQ(G(t, "Shell.taskbar"), "classic"); EQ(G(t, "Desktop.icons"), "wine"); EQ(G(t, "Theme.build"), "xp");
    CHECK(lhde_theme_source(t, "Desktop.icons") == LHDE_FROM_FALLBACK);   /* icons xp: no resources/xp */
    lhde_theme_free(t);

    /* the free baseline: only ReactOS imported; icons, resources and fonts fall back to it (section 6), then it goes again */
    put("data/resources/reactos/shell32.dll", "x"); put("data/fonts/reactos/marlett.ttf", "x");
    put("data/icons/reactos.theme", "[theme]\nresources = resources/reactos\n");
    t = R("royale");
    EQ(G(t, "Desktop.icons"), "reactos"); HAS(G(t, "path.icons"), "icons/reactos.theme"); HAS(W(t, "Desktop.icons"), "resources/xp not imported: reactos");
    HAS(G(t, "path.resources"), "/data/resources/reactos"); HAS(W(t, "path.resources"), "fallback: resources/xp not imported: resources/reactos");
    HAS(G(t, "path.fonts"), "/data/fonts/reactos"); HAS(W(t, "path.fonts"), "fallback: fonts/xp not imported: fonts/reactos");
    lhde_theme_free(t);
    for (const char *const *f = (const char *const[]){ "data/resources/reactos/shell32.dll", "data/fonts/reactos/marlett.ttf", "data/icons/reactos.theme",
                                                        "data/resources/reactos", "data/fonts/reactos", NULL }; *f; f++) {
        char c[PATH_MAX + 64]; snprintf(c, sizeof c, "%s/%s", T, *f); remove(c); }

    put("data/resources/xp/shell32.dll", "x");   /* from here on the XP icon set has its resources */
    /* ---- theme files: inheritance, none, aliases, fallbacks ---- */
    put("data/themes/jade.theme", "[Theme]\nname = Jade (4074)\naliases = jade-old jadeclassic\n[VisualStyle]\nstyle = jade4074\n"
                                  "[Glass]\nframes = none\nframes_classic = jade4074-classic\ncomposition = off\n[Desktop]\nwallpaper = 4074/Bliss\n");
    put("data/themes/aero.theme", "[Theme]\nname = Aero\nbase = jade\n[Glass]\nframes = jade4074\ncomposition = auto\n[Shell]\npack = 9999-none\n"
                                  "[Desktop]\nwallpaper = demos/missing\n");
    t = R("jade");
    EQ(G(t, "Glass.frames"), "none"); CHECK(lhde_theme_source(t, "Glass.frames") == LHDE_FROM_FILE);   /* none stays none */
    EQ(G(t, "Glass.frames_classic"), "jade4074-classic"); HAS(G(t, "path.frames_classic"), "frames/jade4074-classic");
    EQ(G(t, "path.frames"), ""); EQ(G(t, "Theme.name"), "Jade (4074)"); HAS(G(t, "file"), "themes/jade.theme");
    lhde_theme_free(t);
    t = R("jadeclassic");   /* an alias */
    EQ(G(t, "theme"), "jade");
    lhde_theme_free(t);
    t = R("aero");
    EQ(G(t, "Glass.frames"), "jade4074"); CHECK(lhde_theme_source(t, "Glass.frames") == LHDE_FROM_FILE);
    EQ(G(t, "Glass.frames_classic"), "jade4074-classic"); CHECK(lhde_theme_source(t, "Glass.frames_classic") == LHDE_FROM_BASE);
    HAS(W(t, "Glass.frames_classic"), "base jade"); EQ(G(t, "VisualStyle.style"), "jade4074"); EQ(G(t, "chain"), "aero -> jade");
    EQ(G(t, "Shell.pack"), "none"); EQ(G(t, "Shell.taskbar"), "classic");   /* pack missing -> taskbar classic */
    CHECK(lhde_theme_source(t, "Shell.pack") == LHDE_FROM_FALLBACK);
    EQ(G(t, "Desktop.wallpaper"), "4074/Bliss"); HAS(W(t, "Desktop.wallpaper"), "demos/missing not imported");
    EQ(G(t, "Theme.aliases"), NULL);   /* aliases are the theme's own, not inherited */
    lhde_theme_free(t);

    /* the luna fallback (decision 2): style luna missing, luna4051 there */
    put("data/themes/luna.theme", "[Theme]\nname = Luna\n[VisualStyle]\nstyle = luna\ncolor = HomeStead\n");
    t = R("luna");
    EQ(G(t, "VisualStyle.style"), "luna4051"); HAS(W(t, "VisualStyle.style"), "style luna not imported");
    EQ(G(t, "VisualStyle.color"), "HomeStead"); EQ(G(t, "style.kind"), "longhorn");
    EQ(G(t, "style.missing"), "0");   /* a fallback style is a style: listed */
    lhde_theme_free(t);
    /* a style not imported at all: classic, style.missing, left out of the lists */
    put("data/themes/gone.theme", "[Theme]\nname = Gone\n[VisualStyle]\nstyle = nosuchstyle\n");
    t = R("gone");
    EQ(G(t, "VisualStyle.style"), "classic"); EQ(G(t, "style.missing"), "1"); CHECK(lhde_theme_style_missing(t));
    lhde_theme_free(t);
    CHECK(lhde_theme_has_style("gone") == 0); CHECK(lhde_theme_has_style("jade") == 1); CHECK(lhde_theme_has_style("nosuch") == -1);
    put("data/themes/plain.theme", "[Theme]\nname = Plain\n[VisualStyle]\nstyle = classic\n");
    CHECK(lhde_theme_has_style("plain") == 1);   /* classic is always complete */
    put("data/styles/luna.msstyles", "x");   /* the XP one imported: no fallback, v3 defaults */
    t = R("luna");
    EQ(G(t, "VisualStyle.style"), "luna"); EQ(G(t, "Explorer.chrome"), "xp"); EQ(G(t, "Theme.build"), "xp");
    lhde_theme_free(t);
    /* a style that is nowhere: classic */
    put("data/themes/gone.theme", "[VisualStyle]\nstyle = plex4015\n");
    t = R("gone");
    EQ(G(t, "VisualStyle.style"), "classic"); EQ(G(t, "style.kind"), "classic"); EQ(G(t, "Explorer.chrome"), "xp");
    lhde_theme_free(t);
    /* a base that is not there ends the chain */
    put("data/themes/orphan.theme", "[Theme]\nbase = nosuchtheme\n[VisualStyle]\nstyle = slate4074\n");
    t = R("orphan");
    CHECK(t && lhde_theme_source(t, "Theme.base") == LHDE_FROM_FALLBACK); EQ(G(t, "Shell.pack"), "4074-slate");
    lhde_theme_free(t);

    /* ---- cycles and depth ---- */
    char err[512];
    put("data/themes/ca.theme", "[Theme]\nbase = cb\n"); put("data/themes/cb.theme", "[Theme]\nbase = ca\n");
    CHECK(lhde_theme_resolve("ca", err, sizeof err) == NULL); HAS(err, "cycle");
    put("data/themes/self.theme", "[Theme]\nbase = self\n");
    CHECK(lhde_theme_resolve("self", err, sizeof err) == NULL); HAS(err, "cycle");
    for (int i = 0; i < 9; i++) { char r[64], b[64]; snprintf(r, sizeof r, "data/themes/d%d.theme", i); snprintf(b, sizeof b, "[Theme]\nbase = d%d\n", i + 1); put(r, b); }
    put("data/themes/d9.theme", "[VisualStyle]\nstyle = jade4074\n");
    CHECK(lhde_theme_resolve("d0", err, sizeof err) == NULL); HAS(err, "deeper than 8");
    t = R("d2");   /* d2 .. d9: 8 files, allowed */
    CHECK(t); if (t) EQ(G(t, "VisualStyle.style"), "jade4074");
    lhde_theme_free(t);
    CHECK(lhde_theme_resolve("nosuch", err, sizeof err) == NULL); HAS(err, "no theme nosuch");

    /* ---- the user's files win over shipped ones; set_key writes NAME-modified ---- */
    char sel[300];
    CHECK(lhde_theme_set_key("aero", "Desktop.icons", "xp", sel, sizeof sel) == 1);
    EQ(sel, "aero-modified");
    HAS(slurp("config/lhde/themes/aero-modified.theme"), "base = aero");
    HAS(slurp("config/lhde/themes/aero-modified.theme"), "name = Aero (modified)");
    t = R("aero-modified");
    EQ(G(t, "Desktop.icons"), "xp"); CHECK(lhde_theme_source(t, "Glass.frames") == LHDE_FROM_BASE); EQ(G(t, "Theme.name"), "Aero (modified)");
    lhde_theme_free(t);
    CHECK(lhde_theme_set_key("aero-modified", "Glass.composition", "off", sel, sizeof sel) == 1);   /* the modified file itself */
    EQ(sel, "aero-modified");
    CHECK(lhde_theme_set_key("aero-modified", "Glass.composition", "auto", sel, sizeof sel) == 1);  /* = the base's: removed */
    CHECK(!strstr(slurp("config/lhde/themes/aero-modified.theme"), "composition"));
    CHECK(lhde_theme_set_key("aero", "nodot", "x", sel, sizeof sel) == -1);
    CHECK(strstr(slurp("data/themes/aero.theme"), "icons") == NULL);   /* shipped files are never written */

    /* ---- listing ---- */
    struct { int user, shipped, style; } cnt = { 0, 0, 0 };
    void cnt_cb(const char *n, const char *ti, const char *f, int g, void *ud);
    lhde_theme_list(cnt_cb, &cnt);
    CHECK(cnt.user == 1); CHECK(cnt.shipped >= 9);
    CHECK(cnt.style == 8);   /* the 9 styles but luna, which luna.theme claims */

    /* ---- serialize / parse ---- */
    t = R("aero");
    char *s = lhde_theme_serialize(t, 42);
    HAS(s, "theme=aero\nserial=42\n"); HAS(s, "\nGlass.frames=jade4074\n");
    unsigned long ser = 0;
    lhde_theme *u = lhde_theme_parse(s, &ser);
    CHECK(ser == 42); if (u) { EQ(G(u, "Glass.frames"), "jade4074"); EQ(G(u, "theme"), "aero"); }
    lhde_theme_free(u); free(s); lhde_theme_free(t);

    /* ---- the current layer (settings.ini [current]), classic schemes, Save As, Delete ---- */
    {
        char b2[256], e2[256];
        CHECK(lhde_theme_select("jadeclassic") == 1);   /* an alias: its theme */
        EQ(lhde_settings_get("theme", "name", b2, sizeof b2), "jade");
        put("data/icons/2k.theme", "[theme]\nresources = resources/xp\n");
        CHECK(lhde_current_set_key("Desktop.icons", "2k") == 1);
        EQ(lhde_current_theme(b2, sizeof b2), "jade");
        CHECK(lhde_current_set_key("Glass.composition", "off") == 0);   /* jade's own value: not kept, nothing changed */
        CHECK(lhde_current_keys(NULL, NULL) == 1);
        CHECK(lhde_current_set_key("Theme.name", "x") == -1); CHECK(lhde_current_set_key("Bogus.key", "x") == -1);
        t = R("jade");
        EQ(G(t, "Desktop.icons"), "2k"); CHECK(lhde_theme_source(t, "Desktop.icons") == LHDE_FROM_CURRENT); EQ(G(t, "current"), "1");
        HAS(W(t, "Desktop.icons"), "current");
        lhde_theme_free(t);
        t = lhde_theme_resolve_current(NULL, 0); CHECK(t); if (t) EQ(G(t, "Desktop.icons"), "2k"); lhde_theme_free(t);
        t = lhde_theme_resolve_flags("jade", LHDE_RESOLVE_FILES, NULL, 0);
        EQ(G(t, "Desktop.icons"), "longhorn4074"); EQ(G(t, "current"), "0");
        lhde_theme_free(t);
        t = R("aero"); EQ(G(t, "Desktop.icons"), "longhorn4074"); lhde_theme_free(t);   /* the layer is jade's */
        s = lhde_theme_serialize(lhde_theme_resolve("jade", NULL, 0) ? (t = R("jade")) : NULL, 7);
        HAS(s, "\nDesktop.icons=2k\n"); free(s); lhde_theme_free(t);
        /* a preview of changes not applied */
        const char *kv[] = { "visualstyle.STYLE", "classic", "Desktop.icons", "wine" };
        t = lhde_theme_resolve_with("jade", kv, 2, NULL, 0);
        EQ(G(t, "style.kind"), "classic"); EQ(G(t, "VisualStyle.style"), "classic"); EQ(G(t, "Desktop.icons"), "xp"); EQ(G(t, "current"), "2");   /* wine: no recipe here, the fallback */
        lhde_theme_free(t);
        /* classic schemes: [Colors] and [Metrics] (the size's first) under the files and the layer */
        put("data/styles/brick.scheme", "[Scheme]\nname = Brick\n[Colors]\nactivecaption = 128 0 0\nwindow = 255 255 255\n"
                                        "[Metrics]\ncaptionheight = 18\ncaptionfont = Tahoma, 8, bold\n[Metrics:Large Fonts]\ncaptionheight = 26\n");
        CHECK(lhde_current_set_key("VisualStyle.style", "classic") == 1);
        CHECK(lhde_current_set_key("VisualStyle.scheme", "brick") == 1);
        t = R("jade");
        EQ(G(t, "Colors.activecaption"), "128 0 0"); CHECK(lhde_theme_source(t, "Colors.activecaption") == LHDE_FROM_SCHEME);
        EQ(G(t, "Metrics.captionheight"), "18"); EQ(G(t, "Metrics.captionfont"), "Tahoma, 8, bold"); HAS(G(t, "path.scheme"), "styles/brick.scheme");
        lhde_theme_free(t);
        CHECK(lhde_current_set_key("VisualStyle.size", "Large Fonts") == 1);
        CHECK(lhde_current_set_key("Colors.ActiveCaption", "1 2 3") == 1);
        t = R("jade");
        EQ(G(t, "Metrics.captionheight"), "26"); EQ(G(t, "Colors.activecaption"), "1 2 3"); CHECK(lhde_theme_source(t, "Colors.activecaption") == LHDE_FROM_CURRENT);
        EQ(G(t, "Colors.window"), "255 255 255");
        lhde_theme_free(t);
        CHECK(lhde_current_set_key("Colors.activecaption", "128 0 0") == 1);   /* the scheme's own again: dropped */
        CHECK(!strstr(slurp("config/lhde/settings.ini"), "activecaption"));
        const char *kv2[] = { "VisualStyle.style", "classic", "VisualStyle.scheme", "nosuch" };
        t = lhde_theme_resolve_with("jade", kv2, 2, NULL, 0);
        EQ(G(t, "VisualStyle.scheme"), "none"); HAS(W(t, "VisualStyle.scheme"), "Windows Standard"); EQ(G(t, "Colors.activecaption"), NULL);
        lhde_theme_free(t);
        int ns = 0, nst = 0;
        void count_scheme(const char *n, const char *ti, const char *pa, void *ud);
        void count_style(const char *n, const char *pa, void *ud);
        CHECK(lhde_scheme_list(count_scheme, &ns) == 1 && ns == 1);
        CHECK(lhde_style_list(count_style, &nst) == 9 && nst == 9);
        /* Save As: base = jade + the layer, selected, the layer gone */
        CHECK(lhde_theme_save_as("jade", "Jade", e2, sizeof e2) == -1); HAS(e2, "built-in");
        CHECK(lhde_theme_save_as("My Jade", "x", e2, sizeof e2) == -1);
        CHECK(lhde_theme_save_as("my-jade", "My Jade", e2, sizeof e2) == 0);
        char *mj = slurp("config/lhde/themes/my-jade.theme");
        HAS(mj, "base = jade"); HAS(mj, "name = My Jade"); HAS(mj, "icons = 2k"); HAS(mj, "scheme = brick"); HAS(mj, "[VisualStyle]");
        EQ(lhde_settings_get("theme", "name", b2, sizeof b2), "my-jade"); EQ(lhde_current_theme(b2, sizeof b2), "");
        t = R("my-jade"); EQ(G(t, "Desktop.icons"), "2k"); CHECK(lhde_theme_source(t, "Desktop.icons") == LHDE_FROM_FILE);
        EQ(G(t, "Colors.activecaption"), "128 0 0"); lhde_theme_free(t);
        /* a user's own theme saved again under another name: copied, keys kept */
        CHECK(lhde_current_set_key("Desktop.mode", "tile") == 1);
        CHECK(lhde_theme_save_as("my-jade2", "My Jade 2", e2, sizeof e2) == 0);
        mj = slurp("config/lhde/themes/my-jade2.theme"); HAS(mj, "base = jade"); HAS(mj, "mode = tile"); HAS(mj, "icons = 2k");
        /* selecting another theme drops the layer */
        CHECK(lhde_current_set_key("Desktop.mode", "fit") == 1);
        CHECK(lhde_theme_select("aero") == 1); EQ(lhde_current_theme(b2, sizeof b2), "");
        /* Delete: the user's only; the selected one falls back to its base */
        CHECK(lhde_theme_delete("jade") == 0);
        CHECK(lhde_theme_delete("my-jade2") == 1);
        lhde_theme_select("my-jade");
        CHECK(lhde_theme_delete("my-jade") == 1);
        EQ(lhde_settings_get("theme", "name", b2, sizeof b2), "jade");
        { char c[PATH_MAX + 64]; snprintf(c, sizeof c, "%s/data/styles/brick.scheme", T); unlink(c); }
    }

    /* ---- the settings store: comments, unknown keys and order kept ---- */
    put("config/lhde/settings.ini", "; my comment\n[theme]\nname = jade   ; the theme\n\n[mine]\nx = 1\n");
    char b[256];
    EQ(lhde_settings_get("theme", "name", b, sizeof b), "jade");
    CHECK(lhde_settings_set("theme", "name", "aero") == 1);
    CHECK(lhde_settings_set("theme", "name", "aero") == 0);
    CHECK(lhde_settings_set("sidebar", "width", "181") == 1);
    EQ(slurp("config/lhde/settings.ini"), "; my comment\n[theme]\nname = aero   ; the theme\n\n[mine]\nx = 1\n\n[sidebar]\nwidth = 181\n");
    CHECK(lhde_settings_set("mine", "x", NULL) == 1);
    EQ(slurp("config/lhde/settings.ini"), "; my comment\n[theme]\nname = aero   ; the theme\n\n[sidebar]\nwidth = 181\n");
    /* the per-user [desktop] wallpaper = none is applied (luxshell's "Show the wallpaper" off) */
    lhde_settings_set("desktop", "wallpaper", "none");
    t = R("aero"); EQ(G(t, "Desktop.wallpaper"), "none"); EQ(G(t, "path.wallpaper"), ""); CHECK(lhde_theme_source(t, "Desktop.wallpaper") == LHDE_FROM_SETTINGS);
    lhde_theme_free(t);

    /* a fresh config directory for what follows */
    snprintf(p, sizeof p, "%s/config/lhde", T);
    { char c[PATH_MAX + 32]; snprintf(c, sizeof c, "rm -rf '%s'", p); if (system(c)) return 2; }
    lhde_settings_reload();
    /* the path helpers */
    { char b[PATH_MAX]; snprintf(p, sizeof p, "%s/state/lhde/x.ini", T); EQ(lhde_state_file("x.ini", b, sizeof b), p);
      snprintf(p, sizeof p, "%s/config/lhde/x.ini", T); EQ(lhde_config_file("x.ini", b, sizeof b), p); }

    /* ---- the three trees (docs/LAYOUT.md): the Windows tree's index, the user's files first ---- */
    {
        char W[PATH_MAX + 32], q[PATH_MAX * 2], o[PATH_MAX], rel[PATH_MAX], flat[PATH_MAX];
        snprintf(W, sizeof W, "%s/xdg/lhde-windows", T);
        put("xdg/lhde-windows/4074/themes/Jade/jade.msstyles", "x");
        put("xdg/lhde-windows/4074/system32/shell32.dll", "x");
        put("xdg/lhde-windows/generated/frames/jade4074w/close.png", "x");
        put("xdg/lhde-windows/generated/packs/4074/imageres/1179.png", "x");
        snprintf(q, sizeof q, "%s/index/styles", W); mkdirs(q);
        snprintf(q, sizeof q, "%s/index/resources", W); mkdirs(q);
        snprintf(q, sizeof q, "%s/index/styles/jade4074w.msstyles", W); CHECK(symlink("../../4074/themes/Jade/jade.msstyles", q) == 0);
        snprintf(q, sizeof q, "%s/index/resources/4074w", W); CHECK(symlink("../../4074/system32", q) == 0);
        snprintf(q, sizeof q, "%s/index/frames", W); CHECK(symlink("../generated/frames", q) == 0);
        snprintf(q, sizeof q, "%s/index/packs", W); CHECK(symlink("../generated/packs", q) == 0);
        put("config/lhde/navbar/mine.ini", "[navbar]\nfamily = 4074\n");
        snprintf(q, sizeof q, "%s/xdg", T); setenv("XDG_DATA_HOME", q, 1);
        snprintf(flat, sizeof flat, "%s", getenv("LHDE_DATA"));
        unsetenv("LHDE_DATA"); unsetenv("LHDE_WINDOWS");
        lhde_data_reset();
        EQ(lhde_windows_dir(), W);
        snprintf(q, sizeof q, "%s/index", W); EQ(lhde_data_user(), q);
        const char *d[LHDE_DATA_MAX]; int nd = lhde_data_dirs(d, LHDE_DATA_MAX);
        CHECK(nd == 2 && lhde_data_dir_kind(0) == LHDE_TREE_USER && lhde_data_dir_kind(1) == LHDE_TREE_WINDOWS);
        EQ(lhde_tree_name(lhde_data_dir_kind(1)), "windows");
        /* by kind, through the index; the origin is the real place */
        CHECK(lhde_data_find_kind("style", "jade4074w", o, sizeof o) != NULL);
        snprintf(q, sizeof q, "%s/index/styles/jade4074w.msstyles", W); EQ(o, q);
        CHECK(lhde_data_origin(o, rel, sizeof rel) == LHDE_TREE_WINDOWS); EQ(rel, "4074/themes/Jade/jade.msstyles");
        CHECK(lhde_data_find("resources/4074w/shell32.dll", o, sizeof o) != NULL);
        CHECK(lhde_data_origin(o, rel, sizeof rel) == LHDE_TREE_WINDOWS); EQ(rel, "4074/system32/shell32.dll");
        CHECK(lhde_data_find("packs/4074/imageres/1179.png", o, sizeof o) != NULL);
        CHECK(lhde_data_find_kind("navbar", "mine", o, sizeof o) != NULL);   /* the user's own file */
        CHECK(lhde_data_origin(o, rel, sizeof rel) == LHDE_TREE_USER); EQ(rel, "navbar/mine.ini");
        CHECK(lhde_data_find_kind("nonsense", "x", o, sizeof o) == NULL);
        /* a frame set split over the trees: theme.ini shipped, its pixels in generated/ */
        put("share/lhde/frames/jade4074w/theme.ini", "mode = dwm\n");
        snprintf(q, sizeof q, "%s/share", T); setenv("XDG_DATA_DIRS", q, 1);
        lhde_data_reset();
        nd = lhde_data_dirs(d, LHDE_DATA_MAX);
        CHECK(nd == 3 && lhde_data_dir_kind(2) == LHDE_TREE_SHIPPED);
        char fd[PATH_MAX]; lhde_data_resolve_dir("frames/jade4074w", "theme.ini", fd, sizeof fd);
        snprintf(q, sizeof q, "%s/share/lhde/frames/jade4074w", T); EQ(fd, q);
        CHECK(lhde_data_find_in(fd, "close.png", o, sizeof o) != NULL);
        CHECK(lhde_data_origin(o, rel, sizeof rel) == LHDE_TREE_WINDOWS); EQ(rel, "generated/frames/jade4074w/close.png");
        CHECK(lhde_data_origin(fd, rel, sizeof rel) == LHDE_TREE_SHIPPED); EQ(rel, "frames/jade4074w");
        unsetenv("LHDE_WINDOWS"); setenv("LHDE_DATA", flat, 1); setenv("XDG_DATA_DIRS", "/nonexistent", 1); lhde_data_reset();
    }

    if (!getenv("LHDE_TEST_KEEP")) { char c[PATH_MAX + 32]; snprintf(c, sizeof c, "rm -rf '%s'", T); if (system(c)) {} }
    else fprintf(stderr, "kept %s\n", T);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

void cnt_cb(const char *n, const char *ti, const char *f, int g, void *ud)
{
    (void)n; (void)ti; (void)f;
    int *c = ud;
    c[g]++;
}
void count_scheme(const char *n, const char *ti, const char *pa, void *ud) { (void)n; (void)pa; if (!strcmp(ti, "Brick")) (*(int *)ud)++; }
void count_style(const char *n, const char *pa, void *ud) { (void)n; (void)pa; (*(int *)ud)++; }
