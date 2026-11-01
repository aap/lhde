// flxp's lookups in lhde's data directories (shell/lhdata.h) and its view of the
// session theme through liblhde (shell/lhtheme.h, shell/wallpapers.h,
// shell/settings.h): a fake import in a fresh directory ($LHDE_DATA) with
// theme files, the style probe replaced (no Microsoft files).
// Usage: build/tests/lhdatatest
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "shell/config.h"
#include "shell/lhdata.h"
#include "shell/lhtheme.h"
#include "shell/runtime.h"
#include "shell/settings.h"
#include "shell/wallpapers.h"
extern "C" {
#include "lhde/data.h"
#include "lhde/theme.h"
}

using namespace shell;

static int g_fail = 0, g_pass = 0;
#define CHECK_EQ(a, b) do { std::string x_ = (a), y_ = (b); if (x_ == y_) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: %s\n  got      [%s]\n  expected [%s]\n", __FILE__, __LINE__, #a, x_.c_str(), y_.c_str()); } } while (0)

static std::string T;
static void put(const std::string& rel, const std::string& data = "x")
{
    std::string p = T + "/" + rel;
    make_dirs(dir_of(p));
    FILE* f = fopen(p.c_str(), "w");
    if (!f) { perror(p.c_str()); exit(2); }
    fputs(data.c_str(), f);
    fclose(f);
}

// the test's styles: classified by their base name (liblhde/tests/resolve.c's probe, shortened)
static int probe(const char* path, lhde_style_info* o)
{
    const char* b = strrchr(path, '/');
    b = b ? b + 1 : path;
    o->format = 4;
    snprintf(o->color, sizeof o->color, "Default");
    snprintf(o->size, sizeof o->size, "Normal");
    if (!strncmp(b, "luna", 4)) { o->format = 3; snprintf(o->family, sizeof o->family, "luna"); snprintf(o->color, sizeof o->color, "NormalColor"); return 1; }
    if (!strncmp(b, "slate", 5)) { o->sidebar = 1; snprintf(o->family, sizeof o->family, "slate"); return 1; }
    if (!strncmp(b, "aero5112", 8)) { o->dwm = 1; snprintf(o->family, sizeof o->family, "aero"); snprintf(o->color, sizeof o->color, "NormalColor"); return 1; }
    return 0;
}

int main()
{
    char tmpl[] = "/tmp/lhdatatest.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    T = tmpl;
    std::string D = T + "/data";
    setenv("LHDE_DATA", D.c_str(), 1);
    setenv("HOME", T.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (T + "/config").c_str(), 1);
    setenv("XDG_RUNTIME_DIR", (T + "/run").c_str(), 1);
    setenv("XDG_DATA_DIRS", (T + "/none").c_str(), 1);
    setenv("LHDE_TREE", "0", 1);
    unsetenv("DISPLAY"); unsetenv("LHDE_SETTINGS");
    for (const char* k : { "FLXP_WALLPAPER_DIRS", "FLXP_THEME",
                           "FLXP_HEADER_ENGINE", "FLXP_HEADER_PRESET" })
        unsetenv(k);
    lhde_data_reset();
    lhde_style_set_probe(probe);

    put("data/themes/slate4051.theme",
        "[Theme]\nname = Slate (4051)\nbuild = 4051\naliases = slate4051-classic\n[VisualStyle]\nstyle = slate4051\n"
        "[Shell]\ntaskbar = avalon\n[Explorer]\nchrome = longhorn\nnavbar = 4051\n"
        "[Desktop]\nwallpaper = 4051/Bliss\nmode = fill\nicons = longhorn4074\n");
    put("data/themes/luna.theme",
        "[Theme]\nname = Windows XP (Luna)\n[VisualStyle]\nstyle = luna\ncolor = NormalColor\n[Shell]\ntaskbar = classic\n"
        "[Explorer]\nchrome = xp\nnavbar = none\n[Desktop]\nwallpaper = xp/Bliss\nmode = stretch\ncolor = 0 78 152\nicons = xp\n");
    put("data/themes/aero5112.theme",
        "[Theme]\nname = Basic (5112)\nbuild = 5112\n[VisualStyle]\nstyle = aero5112\n[Explorer]\nnavbar = vision\nnavbar_preset = big\n"
        "[Desktop]\nwallpaper = none\n");
    for (const char* f : { "styles/slate4051.msstyles", "styles/luna.msstyles", "styles/luna-xp.msstyles", "styles/slate4074-xp/slate.msstyles",
                           "styles/aero5112.msstyles", "resources/4051/shell32.dll", "resources/4074/shell32.dll", "fonts/4074/tahoma.ttf",
                           "fonts/5112/segoeui.ttf", "wallpapers/4051/Bliss.jpg", "wallpapers/xp/Bliss.jpg", "wallpapers/xp/Bliss.bmp",
                           "navbar/vision.ini", "icons/longhorn4074.theme", "icons/xp.theme", "packs/4051-slate/shell.lua", "packs/4051/x.png" })
        put(std::string("data/") + f);

    CHECK_EQ(lhde_find("styles/luna-xp.msstyles"), D + "/styles/luna-xp.msstyles");
    CHECK_EQ(lhde_find("styles/none.msstyles"), "");
    CHECK_EQ(lhde_where("resources/4074"), D);
    CHECK_EQ(lhde_resolve("~/x"), T + "/x");
    CHECK_EQ(lhde_resolve("/abs"), "/abs");

    // the record through liblhde: the style, the build, the keys flxp maps
    SessionTheme t;
    CHECK_EQ(resolve_theme("slate4051-classic", t) ? t.name : "", "slate4051");   // an alias
    CHECK_EQ(t.msstyles, D + "/styles/slate4051.msstyles");
    CHECK_EQ(theme_build(t), "4051");
    set_session_theme(t);
    CHECK_EQ(longhorn_resources(), D + "/resources/4051");
    CHECK_EQ(theme_chrome(), "longhorn");
    CHECK_EQ(theme_setting("header_engine"), "flxp");
    CHECK_EQ(theme_setting("menubar_style"), "document");
    CHECK_EQ(theme_icons("xp"), "longhorn");                           // flxp's res/icons/longhorn.theme
    CHECK_EQ(theme_taskbar(), "avalon");

    Wallpaper wp;
    CHECK_EQ(theme_wallpaper(t, wp) ? wp.file : "", D + "/wallpapers/4051/Bliss.jpg");

    CHECK_EQ(resolve_theme("luna", t) ? t.msstyles : "", D + "/styles/luna.msstyles");
    set_session_theme(t);
    CHECK_EQ(theme_chrome(), "xp");
    CHECK_EQ(longhorn_resources(), "");                               // XP's resources are not a Longhorn build's
    CHECK_EQ(theme_icons("xp"), "xp");
    CHECK_EQ(theme_setting("menubar_style"), "");
    CHECK_EQ(theme_wallpaper(t, wp) ? wp.file.substr(0, D.size() + 20) : "", D + "/wallpapers/xp/Bliss");
    CHECK_EQ(std::to_string(wp.color), std::to_string(0x004e98));
    CHECK_EQ(wp.mode, "stretch");

    CHECK_EQ(resolve_theme("aero5112", t) ? t.name : "", "aero5112");
    set_session_theme(t);
    CHECK_EQ(theme_setting("header_engine"), "lhnav");
    CHECK_EQ(theme_setting("header_preset"), "big");

    CHECK_EQ(longhorn_resources(), "");                               // resources/5112 not imported
    CHECK_EQ(theme_wallpaper(t, wp) ? "picture" : "none", "none");
    CHECK_EQ(t.value("path.navbar"), D + "/navbar/vision.ini");

    // the published record: used when it is current (theme= and serial=), else resolved
    lhde_theme* lt = lhde_theme_resolve("luna", nullptr, 0);
    char* rec = lhde_theme_serialize(lt, 7);
    lhde_theme_free(lt);
    std::string r = rec;
    free(rec);
    CHECK_EQ(session_theme_from("luna", r, 7, t) ? t.why["Explorer.chrome"] : "", "published");
    CHECK_EQ(session_theme_from("luna", r, 8, t) ? t.why["Explorer.chrome"] : "", "file " + D + "/themes/luna.theme");
    CHECK_EQ(session_theme_from("slate4051", r, 7, t) ? t.name : "", "slate4051");

    // Set as Desktop Background / Display Properties: theme keys into NAME-modified.theme, selected
    CHECK_EQ(wallpaper_value(D + "/wallpapers/4051/Bliss.jpg"), "4051/Bliss");
    CHECK_EQ(wallpaper_value(D + "/wallpapers/xp/Bliss.jpg"), D + "/wallpapers/xp/Bliss.jpg");   // Bliss.bmp too
    CHECK_EQ(wallpaper_value("/pics/a.png"), "/pics/a.png");
    std::string sel, err;
    put("pics/a.png");
    CHECK_EQ(store::set_theme_keys("slate4051", { { "Desktop.wallpaper", T + "/pics/a.png" }, { "Desktop.mode", "center" } }, &sel, &err, false) ? sel : err,
             "slate4051-modified");
    CHECK_EQ(is_user_theme("slate4051-modified") ? "user" : "", "user");
    CHECK_EQ(is_user_theme("slate4051") ? "user" : "", "");
    resolve_theme("slate4051-modified", t);
    CHECK_EQ(t.value("path.wallpaper") + " " + t.value("Desktop.mode") + " " + t.value("Explorer.navbar"), T + "/pics/a.png center 4051");
    CHECK_EQ(store::set_theme_keys("slate4051-modified", { { "Desktop.mode", "fill" } }, &sel, &err, false) ? sel : err, "slate4051-modified");
    resolve_theme("slate4051-modified", t);
    CHECK_EQ(t.value("Desktop.mode"), "fill");
    int n = 0;
    for (const store::ThemeEntry& e : store::theme_list()) n += e.name == "slate4051-modified" && e.group == 0;
    CHECK_EQ(std::to_string(n), "1");

    // the current layer (THEME-FORMAT section 3): settings.ini [current] on top of the selected theme, no theme file
    store::apply_theme("slate4051");   // settings.ini first (no display here)
    CHECK_EQ(store::set_current_keys({ { "Desktop.mode", "center" }, { "Explorer.navbar", "vision" } }, false) ? "ok" : "fail", "ok");
    resolve_theme("slate4051", t);
    CHECK_EQ(t.value("Desktop.mode") + " " + t.value("Explorer.navbar") + " " + t.value("current"), "center vision 2");
    resolve_theme_files("slate4051", t);
    CHECK_EQ(t.value("Desktop.mode") + " " + t.value("current"), "fill 0");
    resolve_theme_with("slate4051", { { "Desktop.mode", "tile" } }, t);
    CHECK_EQ(t.value("Desktop.mode") + " " + t.value("Explorer.navbar"), "tile 4051");
    CHECK_EQ(std::to_string(store::current_layer().size()), "2");
    CHECK_EQ(store::set_current_keys({ { "Explorer.navbar", "" } }, false) ? std::to_string(store::current_layer().size()) : "fail", "1");
    n = 0;
    for (const store::ThemeEntry& e : store::theme_files()) n += e.group == 2;
    CHECK_EQ(std::to_string(n), "0");
    store::apply_theme("slate4051-modified");   // another theme: the layer is dropped
    CHECK_EQ(std::to_string(store::current_layer().size()), "0");

    // flxp.ini's own keys; settings.ini's shared ones through the store only, flxp.ini's spelling of them is no key
    write_setting("taskpane", "0");
    CHECK_EQ(setting("taskpane"), "0");
    store::set("fonts", "smoothing", "cleartype");
    CHECK_EQ(font_smoothing(), "cleartype");
    write_setting("font_smoothing", "off");
    CHECK_EQ(font_smoothing(), "cleartype");
    CHECK_EQ(store::path(), T + "/config/lhde/settings.ini");

    // fonts, wallpapers, styles by name
    std::vector<std::string> fd = default_font_dirs();
    CHECK_EQ(fd.size() == 1 ? fd[0] : "", D + "/fonts/4074");
    CHECK_EQ(default_wallpaper_dirs(), "~/Pictures/Wallpapers:" + D + "/wallpapers/4051:" + D + "/wallpapers/xp");
    CHECK_EQ(find_msstyles("slate4074-xp"), D + "/styles/slate4074-xp/slate.msstyles");
    CHECK_EQ(find_msstyles("aero5112"), D + "/styles/aero5112.msstyles");
    CHECK_EQ(find_msstyles("nosuch"), "");

    resolve_theme("slate4051", t);
    set_session_theme(t);
    std::string rep = lhde_report();
    if (rep.find("resources/4051: " + D) == std::string::npos || rep.find("path.resources") == std::string::npos) {
        g_fail++; fprintf(stderr, "FAIL report:\n%s", rep.c_str());
    } else g_pass++;

    if (system(("rm -rf '" + T + "'").c_str())) {}
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
