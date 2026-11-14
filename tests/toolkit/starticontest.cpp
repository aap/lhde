// Shell.start_icon / [taskbar] start_icon (shell/starticon.h) in a scratch home with nothing imported: the default and an
// explicit "windows" give no picture (the flag is drawn as before), the settings.ini key wins over the theme's, each of
// lhde, linux and freebsd loads at 16, 20, 24 and 32 px from share/icons/flags (and at 22 resampled from the 32), auto
// follows uname's system name ($LHDE_UNAME), a file path, a file:PACK/PATH of the data directories and an icon key work,
// and what cannot be loaded gives no picture.
// Usage: build/tests/starticontest
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <string>
#include "gdi/bitmap.h"
#include "gdi/image.h"
#include "gdi/png.h"
#include "shell/config.h"
#include "shell/lhdata.h"
#include "shell/lhtheme.h"
#include "shell/settings.h"
#include "shell/starticon.h"
extern "C" {
#include "lhde/data.h"
}

using namespace shell;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { std::string x_ = (a), y_ = (b); if (x_ == y_) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: %s\n  got      [%s]\n  expected [%s]\n", __FILE__, __LINE__, #a, x_.c_str(), y_.c_str()); } } while (0)

static std::string T;

static void settings(const std::string& text)
{
    std::string p = T + "/config/lhde/settings.ini";
    make_dirs(dir_of(p));
    FILE* f = fopen(p.c_str(), "w");
    if (!f) { perror(p.c_str()); exit(2); }
    fputs(text.c_str(), f);
    fclose(f);
}

// the picture's size, and whether it has opaque pixels and transparent ones (a drawing in a square, not a filled box)
static std::string shape(gdi::Bitmap* b)
{
    if (!b) return "none";
    int opaque = 0, clear = 0;
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++) {
            uint32_t a = b->row(y)[x] >> 24;
            if (a == 255) opaque++;
            if (a == 0) clear++;
        }
    std::string s = std::to_string(b->width) + "x" + std::to_string(b->height) + (opaque ? " opaque" : "") + (clear ? " clear" : "");
    gdi::destroy_bitmap(b);
    return s;
}

static void theme_says(const std::string& v)
{
    SessionTheme t;
    t.name = "zztest";
    if (!v.empty()) t.keys["Shell.start_icon"] = v;
    set_session_theme(t);
}

int main()
{
    char tmpl[] = "/tmp/starticontest.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    T = tmpl;
    setenv("HOME", T.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (T + "/config").c_str(), 1);
    setenv("XDG_DATA_HOME", (T + "/data").c_str(), 1);
    setenv("XDG_RUNTIME_DIR", (T + "/run").c_str(), 1);
    setenv("XDG_DATA_DIRS", (T + "/none").c_str(), 1);
    setenv("LHDE_WINDOWS", (T + "/nonexistent").c_str(), 1);
    setenv("LHDE_DATA", (T + "/lhde").c_str(), 1);   // the user's data directory: icons/mine/ below
    unsetenv("DISPLAY"); unsetenv("LHDE_SETTINGS"); unsetenv("LHDE_TREE"); unsetenv("LHDE_UNAME");
    lhde_data_reset();

    // the default: no key anywhere is windows, as an explicit windows is: no picture, the flag
    settings("[theme]\nname = zztest\n");
    theme_says("");
    CHECK_EQ(start_icon_setting(), "windows");
    CHECK_EQ(start_icon_choice(start_icon_setting()), "windows");
    CHECK_EQ(shape(start_icon_image(start_icon_choice(start_icon_setting()), 20)), "none");
    CHECK_EQ(start_icon_source("windows", 16), "");
    settings("[theme]\nname = zztest\n[taskbar]\nstart_icon = windows\n");
    CHECK_EQ(start_icon_choice(start_icon_setting()), "windows");
    CHECK_EQ(shape(start_icon_image(start_icon_choice(start_icon_setting()), 20)), "none");
    CHECK_EQ(start_icon_choice(""), "windows");
    CHECK_EQ(start_icon_choice(" Windows "), "windows");

    // the theme's key, and settings.ini's over it
    settings("[theme]\nname = zztest\n");
    theme_says("freebsd");
    CHECK_EQ(start_icon_setting(), "freebsd");
    settings("[theme]\nname = zztest\n[taskbar]\nstart_icon = linux\n");
    CHECK_EQ(start_icon_setting(), "linux");
    theme_says("");

    // the named icons: each drawing at its own size, from the source tree's share/icons/flags
    for (const char* n : { "lhde", "linux", "freebsd" })
        for (int s : { 16, 20, 24, 32 }) {
            std::string src = start_icon_source(n, s);
            std::string want = std::string("/icons/flags/") + n + "-" + std::to_string(s) + ".png";
            CHECK(src.size() > want.size() && src.compare(src.size() - want.size(), want.size(), want) == 0);
            std::string sz = std::to_string(s) + "x" + std::to_string(s);
            CHECK_EQ(shape(start_icon_image(n, s)), sz + " opaque clear");
        }
    CHECK_EQ(start_icon_choice("LINUX"), "linux");
    // a size without a drawing: the 32 px one resampled (the Explorer's brand would ask 22)
    CHECK(start_icon_source("lhde", 22).find("/icons/flags/lhde-32.png") != std::string::npos);
    CHECK_EQ(shape(start_icon_image("lhde", 22)), "22x22 opaque clear");

    // auto: by uname's system name
    setenv("LHDE_UNAME", "Linux", 1);   CHECK_EQ(start_icon_choice("auto"), "linux");
    setenv("LHDE_UNAME", "FreeBSD", 1); CHECK_EQ(start_icon_choice("auto"), "freebsd");
    setenv("LHDE_UNAME", "Darwin", 1);  CHECK_EQ(start_icon_choice("Auto"), "lhde");
    unsetenv("LHDE_UNAME");
    struct utsname u;
    uname(&u);
    std::string sys = lower(u.sysname);
    CHECK_EQ(start_icon_choice("auto"), sys == "linux" ? "linux" : sys == "freebsd" ? "freebsd" : "lhde");

    // a file of the user's: a 40 x 20 PNG fits a 16 px square at 16 x 8, centred (rows 4 to 11 drawn, the rest clear)
    gdi::Bitmap* wide = gdi::create_bitmap(40, 20);
    for (int y = 0; y < 20; y++) for (int x = 0; x < 40; x++) wide->row(y)[x] = 0xff20a040;
    std::string mine = T + "/mine.png";
    CHECK(gdi::write_png(wide, mine, gdi::PngText{}));
    gdi::destroy_bitmap(wide);
    CHECK_EQ(start_icon_choice(mine), mine);
    CHECK_EQ(start_icon_source(mine, 16), mine);
    gdi::Bitmap* b = start_icon_image(mine, 16);
    CHECK(b && b->width == 16 && b->height == 16);
    if (b) {
        CHECK((b->row(3)[8] >> 24) == 0 && (b->row(4)[8] >> 24) == 255 && (b->row(11)[8] >> 24) == 255 && (b->row(12)[8] >> 24) == 0);
        CHECK((b->row(8)[0] & 0xffffff) == 0x20a040);
        gdi::destroy_bitmap(b);
    }
    CHECK_EQ(start_icon_source("~/mine.png", 16), mine);
    // file:PACK/PATH: icons/PACK/PATH of the data directories, as the icon set recipes take it
    std::string pack = T + "/lhde/icons/mine/start.png";
    make_dirs(dir_of(pack));
    CHECK(system(("cp '" + mine + "' '" + pack + "'").c_str()) == 0);
    lhde_data_reset();
    CHECK_EQ(start_icon_source("file:mine/start.png", 24), pack);
    CHECK_EQ(shape(start_icon_image("file:mine/start.png", 24)), "24x24 opaque clear");
    CHECK_EQ(start_icon_source("computer", 20), "key:computer");
    CHECK_EQ(shape(start_icon_image("computer", 20)).substr(0, 5), "20x20");
    // what is not there: no picture (the flag)
    CHECK_EQ(start_icon_source(T + "/missing.png", 16), "");
    CHECK_EQ(shape(start_icon_image(T + "/missing.png", 16)), "none");
    CHECK_EQ(shape(start_icon_image("no-such-key-anywhere", 16)), "none");
    CHECK_EQ(shape(start_icon_image("file:mine/none.png", 16)), "none");

    if (system(("rm -rf '" + T + "'").c_str()) != 0) fprintf(stderr, "could not remove %s\n", T.c_str());
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
