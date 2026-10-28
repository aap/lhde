// flxp's lookups in lhde's data directories (shell/lhdata.h): a fake import in a
// fresh directory ($LHDE_DATA), the registry elsewhere ($LH_THEMES_INI) so its
// relative values must come from the data directory. No Microsoft files.
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
#include "shell/wallpapers.h"

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

int main()
{
    char tmpl[] = "/tmp/lhdatatest.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    T = tmpl;
    std::string D = T + "/data";
    setenv("LHDE_DATA", D.c_str(), 1);
    setenv("HOME", T.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (T + "/config").c_str(), 1);
    setenv("XDG_DATA_DIRS", (T + "/none").c_str(), 1);
    setenv("LH_THEMES_INI", (T + "/reg/themes.ini").c_str(), 1);
    unsetenv("DISPLAY"); unsetenv("LH_WALLPAPERS_INI");
    for (const char* k : { "FLXP_LONGHORN_SYSTEM32", "FLXP_FONT_PATH", "FLXP_WALLPAPER_DIRS", "FLXP_THEME", "FLXP_THEME_PATH" }) unsetenv(k);

    put("reg/themes.ini",
        "[slate4051]\nmsstyles = styles/slate4051.msstyles\nfonts = fonts/4074\nflxp_msstyles = styles/slate4074-xp/slate.msstyles\n"
        "flxp_longhorn_system32 = resources/4051\n"
        "[luna]\nmsstyles = styles/luna4051.msstyles   ; a comment\nfonts = fonts/4074\n"
        "[aero-patch]\nmsstyles = styles/aero-patch.msstyles\nflxp_longhorn_system32 = resources/4074\n"
        "[royale]\nmsstyles = styles/royale.msstyles\nfonts = fonts/4074\n"
        "[aero5112]\nmsstyles = styles/aero5112.msstyles\nfonts = fonts/5112\n");
    put("data/wallpapers.ini", "[default]\nroot = wallpapers/\nfile = 4074/Bliss.bmp\n[luna]\nfile = xp/Bliss.jpg\n");
    for (const char* f : { "styles/slate4051.msstyles", "styles/luna4051.msstyles", "styles/luna-xp.msstyles",
                           "styles/slate4074-xp/slate.msstyles", "styles/aero5112.msstyles", "resources/4051/shell32.dll",
                           "resources/4074/shell32.dll", "fonts/4074/tahoma.ttf", "fonts/5112/segoeui.ttf",
                           "wallpapers/4074/Bliss.bmp", "wallpapers/xp/Bliss.jpg" })
        put(std::string("data/") + f);

    CHECK_EQ(lhde_registry(), T + "/reg/themes.ini");
    CHECK_EQ(lhde_find("styles/luna-xp.msstyles"), D + "/styles/luna-xp.msstyles");
    CHECK_EQ(lhde_find("styles/none.msstyles"), "");
    CHECK_EQ(lhde_where("resources/4074"), D);
    CHECK_EQ(lhde_resolve("~/x"), T + "/x");
    CHECK_EQ(lhde_resolve("/abs"), "/abs");

    // the build of each theme, and its paths through the data directory (not beside the registry)
    const char* want[][2] = { { "slate4051", "4051" }, { "luna", "4051" }, { "aero-patch", "4074" }, { "royale", "4074" },
                              { "aero5112", "5112" } };
    for (auto& w : want) {
        SessionTheme t;
        if (!lookup_session_theme(w[0], lhde_registry(), t)) { fprintf(stderr, "FAIL lookup %s\n", w[0]); g_fail++; continue; }
        CHECK_EQ(theme_build(t), w[1]);
        CHECK_EQ(t.path("msstyles"), D + "/" + t.value("msstyles"));
    }
    SessionTheme t;
    lookup_session_theme("slate4051", lhde_registry(), t);
    CHECK_EQ(t.msstyles, D + "/styles/slate4074-xp/slate.msstyles");   // flxp_msstyles, found in the data directory

    // longhorn_system32: the theme's key, else resources/<build>, else nothing; flxp.ini wins
    set_session_theme(t);
    CHECK_EQ(theme_setting("longhorn_system32"), D + "/resources/4051");
    lookup_session_theme("luna", lhde_registry(), t);
    set_session_theme(t);
    CHECK_EQ(theme_setting("longhorn_system32"), D + "/resources/4051");
    lookup_session_theme("aero5112", lhde_registry(), t);
    set_session_theme(t);
    CHECK_EQ(theme_setting("longhorn_system32"), "");                 // resources/5112 not imported
    setenv("FLXP_LONGHORN_SYSTEM32", "/elsewhere", 1);
    CHECK_EQ(theme_setting("longhorn_system32"), "/elsewhere");
    unsetenv("FLXP_LONGHORN_SYSTEM32");

    // the theme's wallpaper: wallpapers.ini from the data directory, its root there too
    lookup_session_theme("luna", lhde_registry(), t);
    Wallpaper wp;
    CHECK_EQ(wallpapers_ini(t, wp) ? wp.file : "", D + "/wallpapers/xp/Bliss.jpg");

    // fonts, wallpapers, styles by name
    std::vector<std::string> fd = default_font_dirs();
    CHECK_EQ(fd.size() == 1 ? fd[0] : "", D + "/fonts/4074");
    CHECK_EQ(default_wallpaper_dirs(), "~/Pictures/Wallpapers:" + D + "/wallpapers/4074:" + D + "/wallpapers/xp");
    CHECK_EQ(find_msstyles("luna"), D + "/styles/luna-xp.msstyles");
    CHECK_EQ(find_msstyles("slate4074-xp"), D + "/styles/slate4074-xp/slate.msstyles");
    CHECK_EQ(find_msstyles("slate4074"), D + "/styles/slate4074-xp/slate.msstyles");
    CHECK_EQ(find_msstyles("aero5112"), D + "/styles/aero5112.msstyles");
    CHECK_EQ(find_msstyles("nosuch"), "");

    std::string rep = lhde_report();
    if (rep.find("resources/4051: " + D) == std::string::npos) { g_fail++; fprintf(stderr, "FAIL report:\n%s", rep.c_str()); }
    else g_pass++;

    if (system(("rm -rf '" + T + "'").c_str())) {}
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
