/* liblhde's resolver, settings store and migration on fake files (no Microsoft files): a fresh $LHDE_DATA with empty style
   files classified by a test probe, theme files written here, $XDG_CONFIG_HOME of its own. make check-liblhde.
   Covers: inheritance and its sources, absent vs none, the defaults of each style kind (v3, v4 CompositedWindow 4074, v4
   Sidebar 4051, v4 DWM 5048/5112, classic), bare styles as themes, aliases, the fallbacks (frames, pack -> classic taskbar,
   wallpaper, style luna -> luna4051, icons, base not found), cycles and the depth limit, lhde_theme_set_key
   (NAME-modified.theme), serialization, the settings store (comments kept), the legacy [override:NAME] layer and the
   migration from ~/.config/longhorn + ~/.config/lhshell. */
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
    setenv("HOME", T, 1); setenv("XDG_DATA_DIRS", "/nonexistent", 1); setenv("LHDE_TREE", "0", 1);
    unsetenv("LHDE_SETTINGS"); unsetenv("LH_THEMES_INI"); unsetenv("DISPLAY");
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
    lhde_theme_free(t);
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
    /* the legacy layer: flxp's [override:NAME] (also under an alias) */
    lhde_settings_set("override:jade-old", "taskbar", "none");
    lhde_settings_set("override:jade", "icons", "longhorn");
    t = R("jade");
    EQ(G(t, "Shell.taskbar"), "none"); CHECK(lhde_theme_source(t, "Shell.taskbar") == LHDE_FROM_SETTINGS);
    EQ(G(t, "Desktop.icons"), "longhorn4074"); HAS(W(t, "Desktop.icons"), "[override:jade]");
    lhde_theme_free(t);
    lhde_settings_set("desktop", "wallpaper", "none");
    t = R("aero"); EQ(G(t, "Desktop.wallpaper"), "none"); EQ(G(t, "path.wallpaper"), ""); lhde_theme_free(t);

    /* ---- the migration ---- */
    snprintf(p, sizeof p, "%s/config/lhde", T);
    { char c[PATH_MAX + 32]; snprintf(c, sizeof c, "rm -rf '%s'", p); if (system(c)) return 2; }
    lhde_settings_reload();
    put("config/longhorn/settings.ini", "; Shared desktop settings (x11/SETTINGS.md).\n; Every writer keeps keys; _LH_SETTINGS_CHANGED.\n\n"
        "[theme]\ncomposition = follow\nname = jadeclassic\nanimations = 4074plus\n\n[desktop]\nmode = fill\nwallpaper = \nicons = xp\n\n"
        "[taskbar]\nautohide = 0\nkind = avalon\n\n[override:jade-old]\nicons = xp\ntaskbar = none\n\n[override:aero]\ncomposition = off\n\n"
        "[sidebar]\nwidth = 181\n");
    put("config/longhorn/themes/My Aero.theme", "[Theme]\nname = My Aero\nbase = aero\n[Desktop]\nwallpaper = /pics/a.jpg\nicons = longhorn\n"
        "[Composition]\ncomposition = off\n[Shell]\nshell = slate\n");
    put("config/lhshell/state.ini", "; lhshell state\n[shell]\ntheme = aero\n[sidebar]\ntiles = power;clock\nlayer = below\n[clock]\npinned = 1\n");
    static char rep[16384];
    int m = lhde_settings_migrate(0, rep, sizeof rep);
    CHECK(m > 0);
    char *ns = slurp("config/lhde/settings.ini");
    HAS(ns, "; lhde's settings"); HAS(ns, "name = jade-modified"); HAS(ns, "width = 181"); HAS(ns, "autohide = 0");
    CHECK(!strstr(ns, "override")); CHECK(!strstr(ns, "kind = avalon")); CHECK(!strstr(ns, "animations")); CHECK(!strstr(ns, "x11/SETTINGS.md"));
    HAS(ns, "stacking = bottom"); HAS(ns, "pinned = 1");   /* from state.ini */
    char *jm = slurp("config/lhde/themes/jade-modified.theme");
    HAS(jm, "base = jade"); HAS(jm, "icons = xp"); HAS(jm, "taskbar = none");
    char *am = slurp("config/lhde/themes/aero-modified.theme");
    HAS(am, "composition = off");
    char *my = slurp("config/lhde/themes/my-aero.theme");
    HAS(my, "base = aero"); HAS(my, "wallpaper = /pics/a.jpg"); HAS(my, "icons = longhorn4074"); HAS(my, "[Glass]\ncomposition = off");
    HAS(my, "pack = 4074-slate");
    char *st = slurp("config/lhshell/state.ini");
    HAS(st, "tiles = power;clock"); CHECK(!strstr(st, "theme = aero")); CHECK(!strstr(st, "layer"));
    { struct stat ls; snprintf(p, sizeof p, "%s/config/longhorn/settings.ini", T);
      CHECK(lstat(p, &ls) == 0 && S_ISLNK(ls.st_mode));
      snprintf(p, sizeof p, "%s/config/longhorn/settings.ini.pre-lhde", T); CHECK(lstat(p, &ls) == 0 && S_ISREG(ls.st_mode)); }
    CHECK(lhde_settings_migrate(0, rep, sizeof rep) == 0);   /* once */
    lhde_settings_set("sidebar", "width", "200");            /* written through the old path's symlink too */
    HAS(slurp("config/longhorn/settings.ini"), "width = 200");

    /* ---- the compatibility registry ---- */
    char reg[PATH_MAX];
    CHECK(lhde_compat_registry(reg, sizeof reg) != NULL);
    HAS(slurp("run/lhde/nodisplay/themes.ini"), "[jade]\ntitle = Jade (4074)\naliases = jade-old jadeclassic\n");
    HAS(slurp("run/lhde/nodisplay/themes.ini"), "flxp_frame = longhorn");
    HAS(slurp("run/lhde/nodisplay/wallpapers.ini"), "[jade]\nfile = ");

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
