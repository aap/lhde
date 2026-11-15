// Shell.start_icon / [taskbar] start_icon (shell/starticon.h) in a scratch home with nothing imported: the default and an
// explicit "windows" give no picture (the flag is drawn as before), the settings.ini key wins over the theme's, each of
// lhde, linux and freebsd loads at 16, 20, 24 and 32 px from share/icons/flags (and at 22 resampled from the 32), auto
// follows uname's system name ($LHDE_UNAME), a file path, a file:PACK/PATH of the data directories and an icon key work,
// and what cannot be loaded gives no picture. windows:BUILD over stand-in explorer.exe, shell32.dll and browseui.dll files
// (32-bit .res files): the builds with a Start bitmap and the Shell tab's list, XP's 143 / 176 with their alpha, 2000's one
// 16 px 8-bit flag keyed and centred in the styled cell, a styled flag only cut to 16 px, the brands' frame 0, and the builds
// without one (Longhorn's explorer.exe, ReactOS's logo, not imported) falling back to the theme's flag; a stand-in 4051's
// shellbrd.dll 1040 / 1041 with straight alpha. windows:4074 with jade4074 from the caller's LHDE_WINDOWS (skipped when it
// has none): the white flag of its CompositedWindow.DEFAULTICON in the styled and the classic cell, alpha kept; without the
// style, the theme's flag; a shellbrd.dll (a stand-in, then the caller's resources/4074/shellbrd.dll when it has one) wins
// over the style: its pure white flag.
// Usage: build/tests/starticontest
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <string>
#include <vector>
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

// ---- windows:BUILD over stand-in explorer.exe files: 32-bit .res files (res::load reads them as it reads a PE) ----
static void le(std::string& s, uint32_t v, int n) { for (int i = 0; i < n; i++) s += (char)(v >> (8 * i)); }
// a bottom-up DIB of w x h: 32-bit (pixel(x, y) as 0xAARRGGBB) or, paletted, 8-bit with a palette of
// { 192 192 192, the colour } and the colour where pixel(x, y) is opaque (2000's kind of bitmap, no alpha)
template <class F> static std::string dib(int w, int h, F pixel, bool paletted = false)
{
    std::string d;
    le(d, 40, 4); le(d, (uint32_t)w, 4); le(d, (uint32_t)h, 4); le(d, 1, 2); le(d, paletted ? 8 : 32, 2);
    le(d, 0, 4); le(d, 0, 4); le(d, 0, 4); le(d, 0, 4); le(d, paletted ? 2 : 0, 4); le(d, 0, 4);
    if (paletted) { le(d, 0xc0c0c0, 4); le(d, pixel(-1, -1) & 0xffffff, 4); }
    for (int y = h - 1; y >= 0; y--) {
        size_t row = d.size();
        for (int x = 0; x < w; x++) {
            uint32_t c = pixel(x, y);
            if (paletted) d += (char)((c >> 24) ? 1 : 0); else le(d, c, 4);
        }
        while ((d.size() - row) % 4) d += '\0';
    }
    return d;
}
// a .res of RT_BITMAP entries { id, DIB } at REL in the user's data directory
static void put_res(const std::string& rel, const std::vector<std::pair<int, std::string>>& bitmaps)
{
    std::string r;
    le(r, 0, 4); le(r, 32, 4); le(r, 0xffff, 2); le(r, 0, 2); le(r, 0xffff, 2); le(r, 0, 2); r.append(16, '\0');   // the empty first entry
    for (const auto& b : bitmaps) {
        le(r, (uint32_t)b.second.size(), 4); le(r, 32, 4);
        le(r, 0xffff, 2); le(r, 2, 2); le(r, 0xffff, 2); le(r, (uint32_t)b.first, 2);
        le(r, 0, 4); le(r, 0x30, 2); le(r, 1033, 2); le(r, 0, 4); le(r, 0, 4);
        r += b.second;
        while (r.size() % 4) r += '\0';
    }
    std::string p = T + "/lhde/" + rel;
    make_dirs(dir_of(p));
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) { perror(p.c_str()); exit(2); }
    fwrite(r.data(), 1, r.size(), f);
    fclose(f);
}

static void builds()
{
    const uint32_t XP = 0xff1060e0, XPC = 0xff20c020, HALF = 0x80ff8000, K2 = 0xffe02010, Z = 0xff8040c0;
    const uint32_t SB = 0xffffffff, SB_EDGE = 0x40ffffff, SBC = 0xfff0f0f0;
    // xp: start.bmp 25 x 20 (the flag in the left 20 columns, a half-transparent pixel at (3, 3)), startclassic.bmp 16 x 16,
    // shell32's micro brand of three 22 px frames (frame 0 white with a blue pixel at (5, 5))
    put_res("resources/xp/explorer.exe", {
        { 143, dib(25, 20, [&](int x, int y) { return x == 3 && y == 3 ? HALF : x < 20 && y >= 1 ? XP : 0u; }) },
        { 176, dib(16, 16, [&](int, int y) { return y < 15 ? XPC : 0u; }) },
        { 145, dib(4, 4, [](int, int) { return 0xff000000u; }) } });
    put_res("resources/xp/shell32.dll", { { 240, dib(22, 66, [](int x, int y) { return y < 22 ? (x == 5 && y == 5 ? 0xff0000ffu : 0xffffffffu) : 0xff00ff00u; }) } });
    // 2k: one 16 px 8-bit flag on the 192 grey of the button face, no IDB_STARTCLASSIC; browseui's brand 306 of two frames
    put_res("resources/2k/explorer.exe", { { 143, dib(16, 16, [&](int x, int y) { return x < 0 || (x >= 2 && x < 14 && y >= 2 && y < 14) ? K2 : 0u; }, true) } });
    put_res("resources/2k/browseui.dll", { { 306, dib(22, 44, [](int, int y) { return y < 22 ? 0xff102030u : 0xff405060u; }) } });
    // a build whose explorer.exe has no Start bitmap (Longhorn's: the Avalon Start button is not explorer's), ReactOS (its 143 is
    // ReactOS's logo), a build without explorer.exe, and one with a styled flag only (no 176)
    put_res("resources/4074/explorer.exe", { { 145, dib(4, 4, [](int, int) { return 0xff000000u; }) } });
    put_res("resources/reactos/explorer.exe", { { 143, dib(25, 20, [](int, int) { return 0xff000000u; }) } });
    put_res("resources/5112/shell32.dll", { { 240, dib(22, 22, [](int, int) { return 0xff000000u; }) } });
    put_res("resources/zz25/explorer.exe", { { 143, dib(25, 20, [&](int x, int) { return x < 20 ? Z : 0u; }) } });
    // 4051: no Start bitmap in explorer.exe, the flag in shellbrd.dll (1040 25 x 20 and 1041 16 x 16, straight alpha: a
    // white pixel of alpha 0x40 at (3, 3), colour above alpha, as shellbrd's edges have)
    put_res("resources/4051/explorer.exe", { { 145, dib(4, 4, [](int, int) { return 0xff000000u; }) } });
    put_res("resources/4051/shellbrd.dll", {
        { 1040, dib(25, 20, [&](int x, int y) { return x == 3 && y == 3 ? SB_EDGE : x >= 1 && x < 20 && y >= 1 && y < 19 ? SB : 0u; }) },
        { 1041, dib(16, 16, [&](int, int y) { return y < 14 ? SBC : 0u; }) } });
    lhde_data_reset();

    std::vector<std::string> b = start_flag_builds();
    std::string all;
    for (const std::string& s : b) all += s + " ";
    CHECK_EQ(all, "2k xp 4051 zz25 ");
    std::string items;
    for (const auto& it : start_icon_items()) items += it.second + "=" + it.first + "; ";
    CHECK_EQ(items, "windows=Windows flag (the theme's); windows:2k=Windows 2000 flag; windows:xp=Windows XP flag; "
                    "windows:4051=Longhorn 4051 flag; windows:zz25=Windows zz25 flag; lhde=lhde; linux=Linux penguin; freebsd=FreeBSD daemon; "
                    "auto=Automatic (by the system); ");
    CHECK_EQ(start_flag_label("4074"), "Longhorn 4074 flag");
    CHECK_EQ(start_flag_label("4042main"), "Longhorn 4042 flag");
    CHECK_EQ(start_flag_label("5112"), "Longhorn 5112 flag");

    // the choice: a build with a flag stays, the rest is the theme's flag (windows)
    CHECK_EQ(start_icon_choice("windows:2k"), "windows:2k");
    CHECK_EQ(start_icon_choice(" Windows:XP "), "windows:xp");
    for (const char* v : { "windows:4074", "windows:reactos", "windows:5112", "windows:9999", "windows:", "windows:../xp" })
        CHECK_EQ(start_icon_choice(v), "windows");
    CHECK_EQ(start_flag_build("windows:xp"), "xp");
    CHECK_EQ(start_flag_build("linux"), "");
    settings("[theme]\nname = zztest\n[taskbar]\nstart_icon = windows:4074\n");
    CHECK_EQ(start_icon_choice(start_icon_setting()), "windows");
    settings("[theme]\nname = zztest\n[taskbar]\nstart_icon = windows:2k\n");
    CHECK_EQ(start_icon_choice(start_icon_setting()), "windows:2k");
    CHECK(start_icon_source("windows:xp", 16).find("/resources/xp/explorer.exe#176") != std::string::npos);
    CHECK(start_icon_source("windows:xp", 20).find("/resources/xp/explorer.exe#143") != std::string::npos);
    CHECK(start_icon_source("windows:2k", 16).find("/resources/2k/explorer.exe#143") != std::string::npos);
    CHECK_EQ(start_icon_source("windows:4074", 16), "");
    CHECK_EQ(start_icon_choice("windows:4051"), "windows:4051");
    CHECK(start_icon_source("windows:4051", 16).find("/resources/4051/shellbrd.dll#1041") != std::string::npos);
    CHECK(start_icon_source("windows:4051", 20).find("/resources/4051/shellbrd.dll#1040") != std::string::npos);

    // xp: 143 as it is under a style (alpha kept), 176 classic
    gdi::Bitmap* f = start_flag_image("xp", true);
    CHECK(f && f->width == 25 && f->height == 20);
    if (f) { CHECK(f->row(0)[0] == 0 && f->row(1)[0] == XP && f->row(3)[3] == HALF && f->row(10)[22] == 0); gdi::destroy_bitmap(f); }
    f = start_flag_image("xp", false);
    CHECK(f && f->width == 16 && f->height == 16);
    if (f) { CHECK(f->row(0)[0] == XPC && f->row(15)[0] == 0); gdi::destroy_bitmap(f); }
    // 2k: keyed (the grey is clear, the flag opaque); under a style centred in the left 20 x 20 of the 25 x 20 cell
    f = start_flag_image("2k", false);
    CHECK(f && f->width == 16 && f->height == 16);
    if (f) { CHECK(f->row(0)[0] == 0 && f->row(2)[2] == K2 && f->row(13)[13] == K2 && f->row(14)[14] == 0); gdi::destroy_bitmap(f); }
    f = start_flag_image("2k", true);
    CHECK(f && f->width == 25 && f->height == 20);
    if (f) { CHECK(f->row(3)[3] == 0 && f->row(4)[4] == K2 && f->row(15)[15] == K2 && f->row(16)[16] == 0 && f->row(10)[22] == 0); gdi::destroy_bitmap(f); }
    // a styled flag only: classic gets its left 20 columns at 16
    f = start_flag_image("zz25", false);
    CHECK(f && f->width == 16 && f->height == 16);
    if (f) { CHECK(f->row(8)[8] == Z && f->row(8)[15] == Z); gdi::destroy_bitmap(f); }
    // 4051: shellbrd's 1040 as it is under a style (straight alpha: the edge pixel unchanged), 1041 classic
    f = start_flag_image("4051", true);
    CHECK(f && f->width == 25 && f->height == 20);
    if (f) { CHECK(f->row(0)[0] == 0 && f->row(3)[3] == SB_EDGE && f->row(10)[10] == SB && f->row(10)[22] == 0); gdi::destroy_bitmap(f); }
    f = start_flag_image("4051", false);
    CHECK(f && f->width == 16 && f->height == 16);
    if (f) { CHECK(f->row(0)[0] == SBC && f->row(15)[0] == 0); gdi::destroy_bitmap(f); }
    CHECK_EQ(shape(start_flag_brand("4051", 22)), "none");
    CHECK_EQ(shape(start_icon_image("windows:4051", 20)), "20x20 opaque clear");
    CHECK_EQ(shape(start_flag_image("4074", true)), "none");
    CHECK_EQ(shape(start_flag_image("reactos", true)), "none");
    // the flag alone at a size (the Explorer's brand without a micro brand of the build's)
    CHECK_EQ(shape(start_icon_image("windows:xp", 20)), "20x20 opaque clear");
    CHECK_EQ(shape(start_icon_image("windows:2k", 16)), "16x16 opaque clear");
    CHECK_EQ(shape(start_icon_image("windows:4074", 16)), "none");

    // the brand: frame 0 of xp's shell32 240, 2k's browseui 306, resampled to another size; none for a build without one
    f = start_flag_brand("xp", 22);
    CHECK(f && f->width == 22 && f->height == 22);
    if (f) { CHECK(f->row(5)[5] == 0xff0000ffu && f->row(21)[0] == 0xffffffffu); gdi::destroy_bitmap(f); }
    f = start_flag_brand("2k", 22);
    CHECK(f && f->row(21)[21] == 0xff102030u);
    if (f) gdi::destroy_bitmap(f);
    CHECK_EQ(shape(start_flag_brand("2k", 26)), "26x26 opaque");
    CHECK_EQ(shape(start_flag_brand("zz25", 22)), "none");
    CHECK_EQ(shape(start_flag_brand("5112", 22)), "none");
}

// the box of a picture's pixels with alpha, "X,Y WxH"; how many are opaque near-white, how many partly transparent
static std::string alpha_box(const gdi::Bitmap* b, int* white, int* partial)
{
    int x0 = b->width, y0 = b->height, x1 = -1, y1 = -1;
    *white = *partial = 0;
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++) {
            uint32_t c = b->row(y)[x], a = c >> 24;
            if (!a) continue;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
            if (a == 255 && ((c >> 16) & 255) > 220 && ((c >> 8) & 255) > 220 && (c & 255) > 220) (*white)++;
            if (a < 255) (*partial)++;
        }
    return x1 < 0 ? "empty" : std::to_string(x0) + "," + std::to_string(y0) + " " + std::to_string(x1 - x0 + 1) + "x" + std::to_string(y1 - y0 + 1);
}

// windows:4074: 4074's explorer.exe has no IDB_START; its flag is Jade's CompositedWindow.DEFAULTICON (20 x 17, alpha), from
// the caller's Windows tree (read only: a link to its styles/jade4074.msstyles in the scratch data directory)
static void style_flag(const std::string& windows)
{
    std::string jade = windows + "/index/styles/jade4074.msstyles";
    if (!is_file(jade)) { printf("skip windows:4074's flag: no %s\n", jade.c_str()); return; }
    std::string link = T + "/lhde/styles/jade4074.msstyles";
    make_dirs(dir_of(link));
    CHECK(symlink(jade.c_str(), link.c_str()) == 0);
    lhde_data_reset();
    std::string all;
    for (const std::string& s : start_flag_builds()) all += s + " ";
    CHECK_EQ(all, "2k xp 4051 4074 zz25 ");
    CHECK_EQ(start_icon_choice("windows:4074"), "windows:4074");
    std::string src = start_icon_source("windows:4074", 16), want = "/styles/jade4074.msstyles#CompositedWindow.DEFAULTICON";
    CHECK(src.compare(0, 6, "style:") == 0 && src.size() > want.size() && src.compare(src.size() - want.size(), want.size(), want) == 0);
    int white = 0, partial = 0;
    // a visual style's cell: the 20 x 17 flag as it is, centred in the left 20 x 20 (XP's flag box), its alpha kept
    gdi::Bitmap* f = start_flag_image("4074", true);
    CHECK(f && f->width == 25 && f->height == 20);
    if (f) {
        CHECK_EQ(alpha_box(f, &white, &partial), "0,1 20x17");
        CHECK(white >= 60 && partial >= 20);
        gdi::destroy_bitmap(f);
    }
    // classic: shrunk to 16 x 14, centred in the 16 x 16 cell
    f = start_flag_image("4074", false);
    CHECK(f && f->width == 16 && f->height == 16);
    if (f) {
        CHECK_EQ(alpha_box(f, &white, &partial), "0,1 16x14");
        CHECK(white >= 20 && partial >= 10);
        gdi::destroy_bitmap(f);
    }
    // the Explorer's brand: no micro brand of 4074's, the flag alone at 20 px
    CHECK_EQ(shape(start_flag_brand("4074", 22)), "none");
    CHECK_EQ(shape(start_icon_image("windows:4074", 20)), "20x20 opaque clear");
    // the style gone: the theme's flag again
    unlink(link.c_str());
    lhde_data_reset();
    CHECK_EQ(start_icon_choice("windows:4074"), "windows");
    CHECK_EQ(shape(start_flag_image("4074", true)), "none");
}

// windows:4074 with shellbrd.dll: its 1040 / 1041 over Jade's stand-in, first a stand-in shellbrd.dll, then the caller's
// (read only: a link to its resources/4074/shellbrd.dll; skipped when it has none): the white flag, 21 x 19 of alpha in the
// styled cell, the classic 16 x 16 whole
static void shellbrd_flag(const std::string& windows)
{
    std::string jade = windows + "/index/styles/jade4074.msstyles", jlink = T + "/lhde/styles/jade4074.msstyles";
    bool has_jade = is_file(jade);
    if (has_jade) { make_dirs(dir_of(jlink)); CHECK(symlink(jade.c_str(), jlink.c_str()) == 0); }
    const uint32_t W = 0xffffffff;
    put_res("resources/4074/shellbrd.dll", { { 1040, dib(25, 20, [&](int x, int y) { return x == 2 && y == 2 ? W : 0u; }) } });
    lhde_data_reset();
    CHECK_EQ(start_icon_choice("windows:4074"), "windows:4074");
    CHECK(start_icon_source("windows:4074", 16).find("/resources/4074/shellbrd.dll#1040") != std::string::npos);   // no 1041
    gdi::Bitmap* f = start_flag_image("4074", true);
    CHECK(f && f->width == 25 && f->height == 20);
    if (f) { CHECK(f->row(2)[2] == W && f->row(10)[10] == 0); gdi::destroy_bitmap(f); }
    std::string sb = T + "/lhde/resources/4074/shellbrd.dll";
    unlink(sb.c_str());

    std::string real = windows + "/index/resources/4074/shellbrd.dll";
    if (!is_file(real)) printf("skip 4074's shellbrd.dll flag: no %s\n", real.c_str());
    else {
        CHECK(symlink(real.c_str(), sb.c_str()) == 0);
        lhde_data_reset();
        CHECK(start_icon_source("windows:4074", 16).find("/resources/4074/shellbrd.dll#1041") != std::string::npos);
        CHECK(start_icon_source("windows:4074", 20).find("/resources/4074/shellbrd.dll#1040") != std::string::npos);
        int white = 0, partial = 0;
        f = start_flag_image("4074", true);
        CHECK(f && f->width == 25 && f->height == 20);
        if (f) {
            CHECK_EQ(alpha_box(f, &white, &partial), "0,1 21x19");
            int pure = 0;
            for (int y = 0; y < f->height; y++) for (int x = 0; x < f->width; x++) pure += f->row(y)[x] == W;
            CHECK(white >= 100 && pure >= 100 && partial >= 200);   // white panes, Jade's are shaded
            gdi::destroy_bitmap(f);
        }
        f = start_flag_image("4074", false);
        CHECK(f && f->width == 16 && f->height == 16);
        if (f) {
            CHECK_EQ(alpha_box(f, &white, &partial), "0,0 16x16");
            CHECK(white >= 80 && partial >= 100);
            gdi::destroy_bitmap(f);
        }
        CHECK_EQ(shape(start_flag_brand("4074", 22)), "none");
        CHECK_EQ(shape(start_icon_image("windows:4074", 20)), "20x20 opaque clear");
        unlink(sb.c_str());
    }
    if (has_jade) unlink(jlink.c_str());
    lhde_data_reset();
}

int main()
{
    char tmpl[] = "/tmp/starticontest.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    T = tmpl;
    // the caller's Windows tree, for windows:4074's flag (resources/4074/shellbrd.dll, jade4074)
    const char* lw = getenv("LHDE_WINDOWS");
    const char* home = getenv("HOME");
    std::string windows = lw && *lw ? lw : std::string(home ? home : "") + "/.local/share/lhde-windows";
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

    builds();
    style_flag(windows);
    shellbrd_flag(windows);

    if (system(("rm -rf '" + T + "'").c_str()) != 0) fprintf(stderr, "could not remove %s\n", T.c_str());
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
