#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>
#include "places.h"
#include "preview.h"
#include "uiicons.h"
#include "verbs.h"
#include "shell/apps.h"
#include "shell/bookmarks.h"
#include "shell/config.h"
#include "shell/icons.h"
#include "shell/lhdata.h"
#include "shell/namespace.h"
#include "gdi/image.h"
#include "uxtheme.h"
#include "vssym32.h"
extern "C" {
#include "commctrl.h"
}

namespace places {

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// ---- the model -------------------------------------------------------------------------

struct Entry {
    std::string label, path, icon, uri;
    enum Kind { Place, Bookmark } kind = Place;
    RECT rc = {};           // the whole row (content coordinates)
    RECT text = {};         // where the name is
};

struct Group {
    std::string title;
    GroupSpec::Kind kind = GroupSpec::Custom;
    std::vector<Place> custom;   // a Custom group's places
    std::vector<Entry> items;
    bool collapsed = false;
    bool hidden = false;         // Details with nothing to say (XP hides the section)
    RECT head = {}, body = {};
};

static HWND g_wnd, g_filter, g_rename;
static Callbacks g_cb;
static bool g_grp;                      // the groups look (the Longhorn hosts)
static bool g_icons = true;             // the groups look's 16 px icons (places_icons)
static Options g_opts;
static std::vector<Group> g_groups;     // the explorer: 0 Places, 1 Bookmarks; Options::spec otherwise
static int g_scroll, g_content_h;
static int g_inset;                     // the groups look: rows kept free at the top (set_top_inset)
static int g_hot_group = -1, g_hot_item = -1;   // item -1 with a group: its header
static int g_down_group = -1, g_down_item = -2;
static bool g_tracking;
// dragging an entry out (a folder drag) and the bookmarks' reorder: the
// bookmark being dragged (its place in the file), the gap shown (before
// bookmark k, k = their count: after the last), where the press was
static int g_drag_bm = -1, g_insert = -1;
static POINT g_down_pt;
static int g_rename_group = -1, g_rename_item = -1;
static struct stat g_bm_stat;
static HFONT g_font, g_bold, g_under, g_bold_under;
enum { TIMER_BOOKMARKS = 1, WM_RENAME_DONE = WM_APP + 0x41, WM_DETAILS_PICTURE = WM_APP + 0x42 };

// the Details section (XP look): the model's content and where its pieces went
static shell::details::Info g_details;
static bool g_details_set, g_details_preview, g_details_open_default;
static preview::Pictures* g_pics;
struct DetailText { RECT r; std::string text; bool bold; };
static std::vector<DetailText> g_dtext;      // content coordinates
static RECT g_dpic;                          // the mini preview (content coordinates; empty: none)
enum { PREVIEW_W = 150, PREVIEW_H = 100, INFO_GAP = 5 };   // CDUIView's 150 x 100; NameSpaceItemInfo's top margin

// XDG user dirs ($XDG_CONFIG_HOME/user-dirs.dirs: XDG_PICTURES_DIR="$HOME/Pictures")
std::string user_dir(const char* key, const char* fallback)
{
    std::string text, home = shell::home_dir();
    std::string out;
    if (shell::read_file(shell::config_home() + "/user-dirs.dirs", text)) {
        size_t i = 0;
        while (i < text.size()) {
            size_t j = text.find('\n', i);
            if (j == std::string::npos) j = text.size();
            std::string line = text.substr(i, j - i);
            i = j + 1;
            size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos || line.substr(0, eq) != key) continue;
            std::string v = line.substr(eq + 1);
            if (v.size() >= 2 && v[0] == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            if (v.compare(0, 5, "$HOME") == 0) v = home + v.substr(5);
            out = v;
        }
    }
    if (out.empty()) out = home + "/" + fallback;
    if (out == home || out == home + "/") return "";   // unset dirs point at $HOME
    struct stat st;
    return stat(out.c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? out : "";
}

static std::string trash_dir() { return shell::data_home() + "/Trash/files"; }

static void fill_places(Group& g)
{
    g.items.clear();
    auto add = [&](const std::string& label, const std::string& path, const std::string& icon) {
        Entry e; e.label = label; e.path = path; e.icon = icon; e.kind = Entry::Place;
        g.items.push_back(e);
    };
    add("Desktop", "::desktop", "desktop");
    add("Home", shell::home_dir(), "home");
    std::string docs = user_dir("XDG_DOCUMENTS_DIR", "Documents"), pics = user_dir("XDG_PICTURES_DIR", "Pictures");
    if (!docs.empty()) add("Documents", docs, "folder");
    if (!pics.empty()) add("Pictures", pics, "folder");
    add("My Computer", "::computer", "computer");
    add("Trash", trash_dir(), "trash");
}

static void fill_bookmarks(Group& g)
{
    g.items.clear();
    std::vector<shell::Bookmark> list = shell::load_bookmarks();
    for (const shell::Bookmark& b : list) {
        Entry e;
        e.kind = Entry::Bookmark;
        e.label = b.display_name();
        e.uri = b.uri;
        e.path = b.path();
        e.icon = e.path.empty() ? "network" : "folder";
        g.items.push_back(e);
    }
    // (4051's "more" under its WinFS views folded other views, lh4051-194216.png,
    // not a long list of links: every bookmark shows; no "Bookmark this folder":
    // folders are dropped on the group, the menus elsewhere add the current one)
}

static void fill_custom(Group& g)
{
    g.items.clear();
    for (const Place& p : g.custom) {
        Entry e; e.label = p.label; e.path = p.path; e.icon = p.icon; e.kind = Entry::Place;
        g.items.push_back(e);
    }
}

static void fill_group(Group& g)
{
    if (g.kind == GroupSpec::Places) fill_places(g);
    else if (g.kind == GroupSpec::Bookmarks) fill_bookmarks(g);
    else fill_custom(g);
}

static int bookmarks_group()
{
    for (size_t i = 0; i < g_groups.size(); i++) if (g_groups[i].kind == GroupSpec::Bookmarks) return (int)i;
    return -1;
}

static void read_bookmarks_stat(struct stat& st)
{
    memset(&st, 0, sizeof st);
    if (stat(shell::bookmarks_path().c_str(), &st) != 0) memset(&st, 0, sizeof st);
}

// ---- the look ----------------------------------------------------------------------------

struct Bmp {
    HBITMAP h = nullptr;    // 32bpp premultiplied DIB section
    int w = 0, h_ = 0;
    explicit operator bool() const { return h != nullptr; }
};

enum Kind { Classic, Shellstyle, ExplorerBar, Groups };

struct Look {
    Kind kind = Classic;
    COLORREF pane1 = 0, pane2 = 0;          // vertical gradient (equal: solid)
    bool pane_border = false;               // XP: a white line left and at the bottom
    COLORREF body = 0, body_border = 0;
    COLORREF title = 0, title_hot = 0, link = 0, link_hot = 0;
    COLORREF head1 = 0, head2 = 0;          // the drawn header's gradient (no bitmap)
    int head_from = 46;                     // where it starts, % of the width (IDB_HEADERSECTION: blue 86 of 186 px white)
    Bmp head, up, up_hot, down, down_hot;   // shellstyle bitmaps
    HTHEME theme = nullptr;                 // ExplorerBar parts
};
static Look g_look;

static void free_bmp(Bmp& b) { if (b.h) DeleteObject(b.h); b = Bmp(); }

// A shellstyle.dll bitmap ("pe:DLL#ID") as a premultiplied DIB: 32bpp ones keep
// their alpha (rcbmp(...,2,255,...)), others are keyed on magenta.
static Bmp load_bmp(const std::string& src)
{
    Bmp out;
    gdi::Bitmap* b = src.empty() ? nullptr : shell::load_art(src);
    if (!b) return out;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = b->width;
    bi.bmiHeader.biHeight = -b->height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    out.h = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!out.h || !bits) { gdi::destroy_bitmap(b); return Bmp(); }
    out.w = b->width; out.h_ = b->height;
    bool alpha = false;
    for (int y = 0; y < b->height && !alpha; y++)
        for (int x = 0; x < b->width; x++) if ((b->row(y)[x] >> 24) != 255) { alpha = true; break; }
    uint32_t* d = (uint32_t*)bits;
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++) {
            uint32_t p = b->row(y)[x], a = alpha ? p >> 24 : ((p & 0xffffff) == 0xff00ff ? 0 : 255);
            uint32_t r = ((p >> 16) & 255) * a / 255, g = ((p >> 8) & 255) * a / 255, bl = (p & 255) * a / 255;
            d[(size_t)y * b->width + x] = a << 24 | r << 16 | g << 8 | bl;
        }
    gdi::destroy_bitmap(b);
    return out;
}

static COLORREF mix(COLORREF a, COLORREF b, int num, int den);

// The section chevrons without shellstyle.dll (the free baseline: ReactOS has no web view and no shellstyle; docs/USAGE.md),
// drawn here after XP's IDB_UP/IDB_UPHOT/IDB_DOWN/IDB_DOWNHOT (themes/themedir/shellstyle in the XP SP1 source): the glyph is
// two 2-px chevrons four rows apart, 7 px wide, apex at column 9, rows 5-12 (up) or 6-13 (down). Luna (21x23): on a pale disc
// (radius about 8.3 around (9.5, 9), a ring in the pane's colour, white inside shading to the ring at the bottom, a faint
// shadow down and to the right). Classic (21x21): the glyph alone, the hot one inside a raised 17x16 frame.
// As premultiplied DIBs, so they draw like the bitmaps.
static Bmp make_chevron(bool down, bool hot, bool classic, COLORREF glyph, COLORREF ring)
{
    int W = 21, H = classic ? 21 : 23;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    Bmp out;
    out.h = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!out.h || !bits) return Bmp();
    out.w = W; out.h_ = H;
    uint32_t* d = (uint32_t*)bits;
    std::vector<double> A((size_t)W * H, 0), R((size_t)W * H, 0), G((size_t)W * H, 0), B((size_t)W * H, 0);
    auto over = [&](int x, int y, COLORREF c, double a) {   // straight-alpha "over" into the work buffers
        if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
        size_t i = (size_t)y * W + x;
        double na = a + A[i] * (1 - a);
        if (na <= 0) return;
        R[i] = (GetRValue(c) * a + R[i] * A[i] * (1 - a)) / na;
        G[i] = (GetGValue(c) * a + G[i] * A[i] * (1 - a)) / na;
        B[i] = (GetBValue(c) * a + B[i] * A[i] * (1 - a)) / na;
        A[i] = na;
    };
    if (classic) {
        if (hot) {
            COLORREF hi = GetSysColor(COLOR_BTNHIGHLIGHT), sh = GetSysColor(COLOR_BTNSHADOW);
            for (int x = 1; x <= 17; x++) over(x, 2, hi, 1);
            for (int y = 3; y <= 17; y++) over(1, y, hi, 1);
            for (int y = 3; y <= 17; y++) over(17, y, sh, 1);
            for (int x = 2; x <= 16; x++) over(x, 17, sh, 1);
        }
    } else {
        const double cx = 9.5, cy = 9.0, rad = 8.3, ringw = 1.4;
        const int SS = 4;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                double sh = 0, disc = 0, ringc = 0, shade = 0;
                for (int sy = 0; sy < SS; sy++)
                    for (int sx = 0; sx < SS; sx++) {
                        double px = x + (sx + 0.5) / SS, py = y + (sy + 0.5) / SS;
                        double r0 = hypot(px - cx, py - cy), r1 = hypot(px - cx - 1.0, py - cy - 1.4);
                        if (r1 < rad + 0.8) sh += 1;
                        if (r0 < rad) {
                            disc += 1;
                            if (r0 > rad - ringw) ringc += 1;
                            double t = (py - cy) / rad;                       // the lower half shades to the ring
                            shade += t > 0 ? 0.30 * t * t : 0;
                        }
                    }
                double n = SS * SS;
                over(x, y, RGB(0, 40, 100), 0.16 * sh / n);
                if (disc > 0) {
                    COLORREF in = mix(RGB(255, 255, 255), ring, (int)lround(100 * shade / disc), 100);
                    double rf = ringc / disc;
                    COLORREF c = mix(in, ring, (int)lround(100 * rf), 100);
                    over(x, y, c, disc / n);
                }
            }
    }
    for (int k = 0; k < 2; k++)
        for (int r = 0; r < 4; r++) {
            int y = down ? 6 + 4 * k + (3 - r) : 5 + 4 * k + r;
            int xs[4] = { 9 - r, 9 - r + 1, 9 + r - 1, 9 + r };
            for (int j = 0; j < 4; j++) if (r > 0 || j == 0) over(xs[j], y, glyph, 1);
        }
    for (size_t i = 0; i < A.size(); i++) {
        uint32_t a = (uint32_t)lround(A[i] * 255);
        d[i] = a << 24 | (uint32_t)lround(R[i] * A[i]) << 16 | (uint32_t)lround(G[i] * A[i]) << 8 | (uint32_t)lround(B[i] * A[i]);
    }
    return out;
}

static void make_chevrons()
{
    bool classic = g_look.kind == Classic;
    COLORREF glyph = classic ? GetSysColor(COLOR_BTNTEXT) : mix(g_look.title, RGB(0, 0, 0), 1, 5);
    COLORREF hot = classic ? glyph : g_look.title_hot;
    // the ring: the pane's darker end towards white, then towards its own grey (Luna blue: about 172,180,213)
    COLORREF ring = mix(RGB(255, 255, 255), g_look.pane2, 11, 20);
    int lum = (GetRValue(ring) * 30 + GetGValue(ring) * 59 + GetBValue(ring) * 11) / 100;
    ring = mix(ring, RGB(lum, lum, lum), 2, 5);
    if (!g_look.up) g_look.up = make_chevron(false, false, classic, glyph, ring);
    if (!g_look.up_hot) g_look.up_hot = make_chevron(false, true, classic, hot, ring);
    if (!g_look.down) g_look.down = make_chevron(true, false, classic, glyph, ring);
    if (!g_look.down_hot) g_look.down_hot = make_chevron(true, true, classic, hot, ring);
}

// An ExplorerBar part that is a placeholder: at most `most` colours where it is opaque (drawn over black and over white, the
// pixels both agree on), none at all included. ReactOS's styles carry such parts (a flat green NormalGroupCollapse, Mizu's
// flat NormalGroupHead) that ReactOS's own explorer never draws, having no web view.
static bool placeholder_part(HTHEME th, int part, int state, int w, int h, size_t most)
{
    SIZE sz = { w, h };
    if (part != EBP_NORMALGROUPHEAD) GetThemePartSize(th, nullptr, part, state, nullptr, TS_TRUE, &sz);
    if (sz.cx <= 0 || sz.cy <= 0 || sz.cx > 400 || sz.cy > 100) sz = SIZE{ w, h };
    std::vector<uint32_t> px[2];
    for (int pass = 0; pass < 2; pass++) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = sz.cx;
        bi.bmiHeader.biHeight = -sz.cy;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC mem = CreateCompatibleDC(nullptr);
        HBITMAP b = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!b || !bits) { if (b) DeleteObject(b); DeleteDC(mem); return false; }
        HGDIOBJ old = SelectObject(mem, b);
        RECT r = { 0, 0, sz.cx, sz.cy };
        HBRUSH br = CreateSolidBrush(pass ? RGB(255, 255, 255) : RGB(0, 0, 0));
        FillRect(mem, &r, br);
        DeleteObject(br);
        DrawThemeBackground(th, mem, part, state, &r, nullptr);
        px[pass].assign((uint32_t*)bits, (uint32_t*)bits + (size_t)sz.cx * sz.cy);
        SelectObject(mem, old);
        DeleteObject(b);
        DeleteDC(mem);
    }
    std::vector<uint32_t> seen;
    for (size_t i = 0; i < px[0].size(); i++) {
        uint32_t c = px[0][i] & 0xffffff;
        if (c != (px[1][i] & 0xffffff)) continue;
        if (std::find(seen.begin(), seen.end(), c) == seen.end()) { seen.push_back(c); if (seen.size() > most) return false; }
    }
    return true;
}

static std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; }

// style.uix of themes/themedir/shellstyle/{blue,homestead,metallic,classic}
static void load_look(HWND h)
{
    for (Bmp* b : { &g_look.head, &g_look.up, &g_look.up_hot, &g_look.down, &g_look.down_hot }) free_bmp(*b);
    if (g_look.theme) CloseThemeData(g_look.theme);
    g_look = Look();
    if (g_grp) {
        // the groups look: 4051's white pane (lh4051-194203/195027.png) with
        // the pre-reset concepts' groups - link-blue names, the +/- expanders
        // of Media Player's "Music Links" tree, the Save File concept's group
        // names with 16 px icons under them
        g_look.kind = Groups;
        g_look.pane1 = g_look.pane2 = RGB(255, 255, 255);
        g_look.body = g_look.pane2;
        g_look.title = RGB(30, 57, 91);
        g_look.title_hot = RGB(0, 102, 204);
        g_look.link = RGB(35, 74, 139);
        g_look.link_hot = RGB(0, 102, 204);
        return;
    }
    std::string variant, style_file, style_color;
    HTHEME th = OpenThemeData(h, L"ExplorerBar");
    if (th) {
        WCHAR file[512] = {}, color[64] = {};
        GetCurrentThemeName(file, 512, color, 64, nullptr, 0);
        style_file = w32::utf8_of(file); style_color = w32::utf8_of(color);
        std::string f = lower(style_file), c = lower(style_color);
        if (f.size() >= 13 && f.compare(f.size() - 13, 13, "luna.msstyles") == 0)
            variant = c == "homestead" ? "homestead" : c == "metallic" ? "metallic" : "blue";
        else if (IsThemePartDefined(th, EBP_NORMALGROUPHEAD, 0)) {
            g_look.kind = ExplorerBar;
            g_look.theme = th;
            th = nullptr;
        } else {
            variant = "blue";
            style_file.clear();   // another style without ExplorerBar parts: Luna's NormalColor bitmaps
        }
        if (th) CloseThemeData(th);
    }
    // XP's shellstyle.dll: the style's Shell/<color>/ (Luna's NormalColor, HomeStead, Metallic),
    // classic: system32's (resources/xp)
    if (!variant.empty() && style_file.empty()) {
        style_file = shell::lhde_find("styles/luna.msstyles");
        style_color = "NormalColor";
    }
    std::string dll = variant.empty() || !style_file.empty() ? shell::shellstyle_dll(variant.empty() ? "" : style_file, style_color) : "";
    auto art = [&](int id) { return dll.empty() ? std::string() : "pe:" + dll + "#" + std::to_string(id); };
    if (g_look.kind == ExplorerBar) {
        HTHEME t = g_look.theme;
        COLORREF c1 = RGB(140, 170, 230), c2 = RGB(100, 135, 220);
        GetThemeColor(t, 0, 0, TMT_GRADIENTCOLOR1, &c1);
        GetThemeColor(t, 0, 0, TMT_GRADIENTCOLOR2, &c2);
        g_look.pane1 = c1; g_look.pane2 = c2;
        g_look.pane_border = true;
        g_look.body = RGB(214, 223, 247); g_look.body_border = RGB(255, 255, 255);
        GetThemeColor(t, EBP_NORMALGROUPBACKGROUND, 0, TMT_FILLCOLOR, &g_look.body);
        GetThemeColor(t, EBP_NORMALGROUPBACKGROUND, 0, TMT_BORDERCOLOR, &g_look.body_border);
        g_look.title = RGB(33, 93, 198);
        GetThemeColor(t, EBP_NORMALGROUPHEAD, 0, TMT_TEXTCOLOR, &g_look.title);
        g_look.link = RGB(33, 93, 198);
        GetThemeColor(t, EBP_NORMALGROUPBACKGROUND, 0, TMT_TEXTCOLOR, &g_look.link);
        g_look.title_hot = g_look.link_hot = RGB(66, 142, 255);
        if (!placeholder_part(t, EBP_NORMALGROUPCOLLAPSE, 1, 19, 19, 2) && !placeholder_part(t, EBP_NORMALGROUPHEAD, 0, 180, 25, 3))
            return;
        // the style's ExplorerBar images are placeholders (ReactOS's styles) and it has no shellstyle.dll of its own: as XP
        // does (SHGetShellStyleHInstance falls back to system32's, the classic one), the classic pane in the style's system
        // colours (measured: XP SP3 with ReactOS's Lunar and Lautus, ~/fun/longhorn/screenshots/WinXP-193456.png, -193504.png)
        CloseThemeData(t);
        g_look = Look();
    }
    if (variant.empty()) {   // classic (class_ss / classic style.uix)
        g_look.kind = Classic;
        g_look.pane1 = g_look.pane2 = GetSysColor(COLOR_WINDOW);
        g_look.body = GetSysColor(COLOR_WINDOW);
        g_look.body_border = GetSysColor(COLOR_BTNFACE);
        g_look.head1 = g_look.head2 = GetSysColor(COLOR_BTNFACE);
        g_look.title = g_look.title_hot = GetSysColor(COLOR_BTNTEXT);
        g_look.link = g_look.link_hot = GetSysColor(COLOR_WINDOWTEXT);
    } else {
        g_look.kind = Shellstyle;
        g_look.pane_border = true;
        g_look.body_border = RGB(255, 255, 255);
        if (variant == "homestead") {
            g_look.pane1 = RGB(204, 217, 173); g_look.pane2 = RGB(165, 189, 132);
            g_look.body = RGB(246, 246, 236);
            g_look.title = g_look.link = RGB(86, 102, 45);
            g_look.title_hot = g_look.link_hot = RGB(114, 146, 29);
            g_look.head1 = RGB(255, 252, 236); g_look.head2 = RGB(224, 231, 184); g_look.head_from = 0;
        } else if (variant == "metallic") {
            g_look.pane1 = RGB(196, 200, 212); g_look.pane2 = RGB(177, 179, 200);
            g_look.body = RGB(240, 241, 245);
            g_look.title = g_look.link = RGB(63, 61, 61);
            g_look.title_hot = g_look.link_hot = RGB(126, 124, 124);
            g_look.head1 = RGB(255, 255, 255); g_look.head2 = RGB(214, 215, 224); g_look.head_from = 0;
        } else {
            g_look.pane1 = RGB(123, 162, 231); g_look.pane2 = RGB(99, 117, 214);
            g_look.body = RGB(214, 223, 247);
            g_look.title = g_look.link = RGB(33, 93, 198);
            g_look.title_hot = g_look.link_hot = RGB(66, 142, 255);
            g_look.head1 = RGB(255, 255, 255); g_look.head2 = RGB(198, 211, 247);
        }
        g_look.head = load_bmp(art(112));   // IDB_HEADERSECTION
    }
    g_look.up = load_bmp(art(100)); g_look.up_hot = load_bmp(art(101));        // IDB_UP, IDB_UPHOT
    g_look.down = load_bmp(art(102)); g_look.down_hot = load_bmp(art(103));    // IDB_DOWN, IDB_DOWNHOT
    make_chevrons();   // the ones shellstyle.dll did not give (no XP files: all four)
}

static void make_fonts()
{
    if (g_font) { DeleteObject(g_font); DeleteObject(g_bold); DeleteObject(g_under); DeleteObject(g_bold_under); }
    // the icon title font, as the tree it stands for: Tahoma 8 under Slate and Luna (style.uix's Tahoma 8pt), Segoe UI 9 under
    // Jade (4074-dwm.png: "Storage Favorites" in Segoe UI), the scheme's under classic
    LOGFONTW lf = {};
    SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0);
    g_font = CreateFontIndirectW(&lf);
    LOGFONTW b = lf; b.lfWeight = FW_BOLD;
    g_bold = CreateFontIndirectW(&b);
    LOGFONTW u = lf; u.lfUnderline = TRUE;
    g_under = CreateFontIndirectW(&u);
    b.lfUnderline = TRUE;   // the hovered current folder: bold and underlined
    g_bold_under = CreateFontIndirectW(&b);
}

// ---- layout --------------------------------------------------------------------------------
// shellstyle (measured on screenshots/explorer_xp_1.png, which agrees with
// style.uix): sections 12 px in from the pane (after its 1 px border), the
// first 12 px down, 15 px apart; header 25 high (2 px border + the 23 px
// chevron), title at +12; the box: 1 px border, padding 12 left/right and 10
// top/bottom, rows 16 px (or the wrapped name) 4 px apart, icon then 6 px then
// the name.

enum { HEAD_H = 25, PAD_X = 12, PAD_Y = 10, ROW_GAP = 4, SECTION_GAP = 15 };
// the groups look: the Filter box at (8, 8) as 4074's (lh4074-180309.png),
// group rows 22 px with the expander box at 6..14 and the title 5 px after it
// (XP's Folders tree: the 9 px glyph centred in the 19 px indent, the icon at 19,
// so 5 clear columns between them), entries 20 px with the icon at 22 and the
// name at 42 (Media Player concept), 8 px between groups
enum { GR_X = 8, GR_BOX_Y = 8, GR_BOX_H = 21, GR_FIRST = GR_BOX_Y + GR_BOX_H + 10, GR_HEAD = 22, GR_ROW = 20, GR_INDENT = 22, GR_GAP = 8,
       GR_BOX_GAP = 5 };


static int text_height(HDC dc, const std::string& s, int width, UINT extra)
{
    std::u16string t = W(s);
    RECT r = { 0, 0, width, 0 };
    DrawTextW(dc, (LPCWSTR)t.c_str(), -1, &r, DT_CALCRECT | DT_NOPREFIX | extra);
    return r.bottom - r.top;
}

// The Details section's pieces from y down, between x0 and x1: the mini
// preview, the bold name, then the lines (the type right under the name, the
// others INFO_GAP apart; several items: one block); returns the bottom
static int layout_details(HDC dc, int x0, int x1, int y)
{
    using namespace shell::details;
    g_dtext.clear();
    g_dpic = RECT{};
    int w = imax(20, x1 - x0);
    if (g_details_preview && g_details.xp_preview && g_pics && !g_details.previews.empty()) {
        int pw = 0, ph = 0;
        if (g_pics->get(g_details.previews[0], imin(PREVIEW_W, w), PREVIEW_H, &pw, &ph)) {
            g_dpic = RECT{ x0, y, x0 + pw, y + ph };
            y += ph;
        }
    }
    auto block = [&](const std::string& text, bool bold, int gap) {
        if (text.empty()) return;
        HGDIOBJ of = SelectObject(dc, bold ? g_bold : g_font);
        int h = text_height(dc, text, w, DT_WORDBREAK);
        SelectObject(dc, of);
        y += gap;
        g_dtext.push_back(DetailText{ RECT{ x0, y, x0 + w, y + h }, text, bold });
        y += h;
    };
    block(xp_title(g_details), true, INFO_GAP);
    std::vector<Line> lines = xp_lines(g_details);
    if (g_details.kind == shell::details::Kind::Many) {
        std::string all;
        for (size_t i = 0; i < lines.size(); i++) all += (i ? "\n" : "") + lines[i].text();
        block(all, false, INFO_GAP);
    } else {
        for (size_t i = 0; i < lines.size(); i++)
            block(lines[i].text(), false, i == 0 && lines[i].label.empty() ? 0 : INFO_GAP);
    }
    return y;
}

static void layout()
{
    RECT rc; GetClientRect(g_wnd, &rc);
    int w = rc.right;
    HDC dc = GetDC(g_wnd);
    HGDIOBJ of = SelectObject(dc, g_font);
    if (g_grp) {
        int y = (g_filter ? GR_FIRST : GR_X) + g_inset;
        int right = imax(GR_INDENT + 60, w - 6);
        for (Group& g : g_groups) {
            g.head = RECT{ 2, y, right, y + GR_HEAD };
            y += GR_HEAD;
            int top = y;
            if (!g.collapsed) {
                for (Entry& e : g.items) {
                    SIZE sz = {};
                    std::u16string t = W(e.label);
                    GetTextExtentPoint32W(dc, (LPCWSTR)t.c_str(), (int)t.size(), &sz);
                    int x0 = GR_INDENT, tx = g_icons ? x0 + 20 : x0;
                    e.rc = RECT{ x0, y, right, y + GR_ROW };
                    int ty = y + (GR_ROW - (int)sz.cy) / 2;
                    e.text = RECT{ tx, ty, imin(right, tx + (int)sz.cx), ty + sz.cy };
                    y += GR_ROW;
                }
            }
            g.body = RECT{ 2, top, right, y };
            y += GR_GAP;
        }
        g_content_h = y + 4;
    } else {
        int left = g_look.pane_border ? 1 : 0;
        int L = left + 12, R = w - 12;
        if (R - L < 80) R = L + 80;
        int y = 12;
        for (Group& g : g_groups) {
            if (g.hidden) { g.head = g.body = RECT{}; continue; }
            g.head = RECT{ L, y, R, y + HEAD_H };
            y += HEAD_H;
            int top = y;
            if (!g.collapsed && g.kind == GroupSpec::Details) {
                y = layout_details(dc, L + 1 + PAD_X, R - 1 - PAD_X, y + PAD_Y) + PAD_Y + 1;
            } else if (!g.collapsed) {
                y += PAD_Y;
                int x0 = L + 1 + PAD_X, x1 = R - 1 - PAD_X;
                for (size_t i = 0; i < g.items.size(); i++) {
                    Entry& e = g.items[i];
                    int tw = imax(20, x1 - (x0 + 16 + 6));
                    int th = text_height(dc, e.label, tw, DT_WORDBREAK);
                    int line = text_height(dc, "Ag", 1000, DT_SINGLELINE);
                    int ty = y + imax(0, (16 - line) / 2);   // the first line beside the icon, the rest under it
                    int h = imax(16, ty - y + th);
                    e.rc = RECT{ x0, y, x1, y + h };
                    SIZE sz = {};
                    std::u16string t = W(e.label);
                    GetTextExtentPoint32W(dc, (LPCWSTR)t.c_str(), (int)t.size(), &sz);
                    e.text = RECT{ x0 + 22, ty, imin(x1, x0 + 22 + (int)sz.cx), ty + th };
                    if (th > sz.cy) e.text.right = x0 + 22 + tw;   // wrapped: the whole column
                    y += h + (i + 1 < g.items.size() ? ROW_GAP : 0);
                }
                y += PAD_Y + 1;
            }
            g.body = RECT{ L, top, R, y };
            y += SECTION_GAP;
        }
        g_content_h = y - SECTION_GAP + 12;
    }
    SelectObject(dc, of);
    ReleaseDC(g_wnd, dc);
    // the scroll bar
    int page = rc.bottom;
    if (g_scroll > imax(0, g_content_h - page)) g_scroll = imax(0, g_content_h - page);
    SCROLLINFO si = {};
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = g_content_h - 1; si.nPage = (UINT)page; si.nPos = g_scroll;
    SetScrollInfo(g_wnd, SB_VERT, &si, TRUE);
    if (g_filter) {
        int bw = imax(60, w - 2 * GR_X - 1);
        SetWindowPos(g_filter, nullptr, GR_X, GR_BOX_Y + g_inset - g_scroll, bw, GR_BOX_H, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// ---- painting ---------------------------------------------------------------------------------

static void fill(HDC dc, const RECT& r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static COLORREF mix(COLORREF a, COLORREF b, int num, int den)
{
    if (den <= 0) return a;
    return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * num / den,
               GetGValue(a) + (GetGValue(b) - GetGValue(a)) * num / den,
               GetBValue(a) + (GetBValue(b) - GetBValue(a)) * num / den);
}

static void blend(HDC dc, const Bmp& b, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
    if (!b || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, b.h);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, dx, dy, dw, dh, mem, sx, sy, sw, sh, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// nine-grid with 3 px corners: the rounded top corners stay sharp
static void blend_grid(HDC dc, const Bmp& b, const RECT& r)
{
    const int m = 3;
    int w = r.right - r.left, h = r.bottom - r.top;
    int xs[4] = { 0, m, b.w - m, b.w }, ys[4] = { 0, m, b.h_ - m, b.h_ };
    int xd[4] = { r.left, r.left + m, r.left + w - m, r.right }, yd[4] = { r.top, r.top + m, r.top + h - m, r.bottom };
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            blend(dc, b, xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], xd[i], yd[j], xd[i + 1] - xd[i], yd[j + 1] - yd[j]);
}

static void draw_header(HDC dc, const Group& g, int gi, int dy)
{
    RECT r = g.head; OffsetRect(&r, 0, -dy);
    bool hot = g_hot_group == gi && g_hot_item == -1;
    if (g_look.kind == ExplorerBar) {
        DrawThemeBackground(g_look.theme, dc, EBP_NORMALGROUPHEAD, 0, &r, nullptr);
    } else if (g_look.head) {
        blend_grid(dc, g_look.head, r);
    } else {
        // IDB_HEADERSECTION's gradient: the left colour up to head_from, then to the right one; cut corners
        int w = r.right - r.left, x0 = w * g_look.head_from / 100;
        for (int x = 0; x < w; x++) {
            COLORREF c = x < x0 ? g_look.head1 : mix(g_look.head1, g_look.head2, x - x0, imax(1, w - 1 - x0));
            int top = r.top;
            if (g_look.kind != Classic) {
                if (x == 0 || x == w - 1) top += 2;
                else if (x == 1 || x == w - 2) top += 1;
            }
            RECT c1 = { r.left + x, top, r.left + x + 1, r.bottom };
            fill(dc, c1, c);
        }
    }
    // title: 10 px padding after the 2 px border, centred in the 23 rows under it
    HGDIOBJ of = SelectObject(dc, g_bold);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, hot ? g_look.title_hot : g_look.title);
    std::u16string t = W(g.title);
    RECT tr = { r.left + 12, r.top + 2, r.right - 26, r.bottom };
    DrawTextW(dc, (LPCWSTR)t.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
    // the chevron: 21x23, 3 px from the right, under the 2 px border
    int cx = r.right - 24, cy = r.top + 2;
    if (g_look.kind == ExplorerBar) {
        int part = g.collapsed ? EBP_NORMALGROUPEXPAND : EBP_NORMALGROUPCOLLAPSE;
        SIZE sz = { 19, 19 };
        GetThemePartSize(g_look.theme, dc, part, 1, nullptr, TS_TRUE, &sz);
        RECT b = { r.right - 3 - sz.cx, r.top + (r.bottom - r.top - sz.cy) / 2, r.right - 3, 0 };
        b.bottom = b.top + sz.cy;
        DrawThemeBackground(g_look.theme, dc, part, hot ? 2 : 1, &b, nullptr);
        return;
    }
    const Bmp& b = g.collapsed ? (hot && g_look.down_hot ? g_look.down_hot : g_look.down) : (hot && g_look.up_hot ? g_look.up_hot : g_look.up);
    if (b) {
        if (g_look.kind == Classic) cy = r.top + (r.bottom - r.top - b.h_) / 2;
        blend(dc, b, 0, 0, b.w, b.h_, cx, cy, b.w, b.h_);
    }
}

static void draw_entry(HDC dc, const Entry& e, bool hot, int dy)
{
    RECT tr = e.text; OffsetRect(&tr, 0, -dy);
    if (g_rename && &e == &g_groups[(size_t)g_rename_group].items[(size_t)g_rename_item]) return;
    if ((!g_grp || g_icons) && !e.icon.empty()) {
        int idx = uiicons::index(e.icon);
        int iy = g_grp ? e.rc.top + (e.rc.bottom - e.rc.top - 16) / 2 : e.rc.top;
        if (idx >= 0) ImageList_Draw(uiicons::small(), idx, dc, e.rc.left, iy - dy, ILD_TRANSPARENT);
    }
    bool current = false;
    if (g_grp && !e.path.empty() && g_cb.current_dir) {
        std::string cur = g_cb.current_dir();
        current = cur == e.path || (cur.empty() && g_cb.current_path && g_cb.current_path() == e.path);
    }
    HGDIOBJ of = SelectObject(dc, hot ? (current ? g_bold_under : g_under) : current ? g_bold : g_font);
    SetBkMode(dc, TRANSPARENT);
    COLORREF c = hot ? g_look.link_hot : g_look.link;
    if (e.kind == Entry::Bookmark && e.path.empty()) c = mix(c, g_look.body, 1, 2);   // remote: not browsable yet
    SetTextColor(dc, c);
    std::u16string t = W(e.label);
    RECT r = tr;
    if (g_grp) { r.right = e.rc.right; DrawTextW(dc, (LPCWSTR)t.c_str(), -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS); }
    else { r.right = e.rc.right; DrawTextW(dc, (LPCWSTR)t.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS); }
    SelectObject(dc, of);
}

// the groups look's header: Media Player's expander (a 9x9 box, +/-) and the
// title; the hot header's title in the hot colour
static void draw_group_header(HDC dc, const Group& g, int gi, int dy)
{
    RECT r = g.head; OffsetRect(&r, 0, -dy);
    bool hot = g_hot_group == gi && g_hot_item == -1;
    int bx = GR_X - 2, by = r.top + (r.bottom - r.top - 9) / 2;
    RECT box = { bx, by, bx + 9, by + 9 };
    HBRUSH frame = CreateSolidBrush(RGB(192, 195, 200));
    FrameRect(dc, &box, frame);
    DeleteObject(frame);
    RECT in = { bx + 1, by + 1, bx + 8, by + 8 };
    fill(dc, in, RGB(246, 249, 253));
    COLORREF glyph = RGB(140, 155, 184);
    RECT hl = { bx + 2, by + 4, bx + 7, by + 5 };
    fill(dc, hl, glyph);
    if (g.collapsed) { RECT vl = { bx + 4, by + 2, bx + 5, by + 7 }; fill(dc, vl, glyph); }
    HGDIOBJ of = SelectObject(dc, g_font);   // regular, as Media Player's "Music Links" and the concept's "Common Places"
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, hot ? g_look.title_hot : g_look.title);
    std::u16string t = W(g.title);
    RECT tr = { bx + 9 + GR_BOX_GAP, r.top, r.right, r.bottom };
    DrawTextW(dc, (LPCWSTR)t.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}

// the Details section's content (NameSpaceItemInfo: the default foreground,
// black; classic: the window text)
static void draw_details(HDC dc, int dy)
{
    if (g_dpic.right > g_dpic.left && g_pics) {
        int pw = 0, ph = 0;
        HBITMAP b = g_pics->get(g_details.previews[0], g_dpic.right - g_dpic.left, g_dpic.bottom - g_dpic.top, &pw, &ph);
        if (b) preview::blend(dc, b, g_dpic.left, g_dpic.top - dy, pw, ph);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_look.kind == Classic ? GetSysColor(COLOR_WINDOWTEXT) : RGB(0, 0, 0));
    for (const DetailText& t : g_dtext) {
        HGDIOBJ of = SelectObject(dc, t.bold ? g_bold : g_font);
        std::u16string u = W(t.text);
        RECT r = t.r; OffsetRect(&r, 0, -dy);
        DrawTextW(dc, (LPCWSTR)u.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, of);
    }
}

static void draw_insert(HDC dc);
static void paint(HDC dc)
{
    RECT rc; GetClientRect(g_wnd, &rc);
    if (g_grp) {
        // the groups look: white (4051), a 1 px line at the right
        RECT all = { 0, 0, rc.right - 1, rc.bottom };
        fill(dc, all, g_look.pane1);
        RECT line = { rc.right - 1, 0, rc.right, rc.bottom };
        fill(dc, line, RGB(205, 212, 224));
        int dy = g_scroll;
        for (size_t gi = 0; gi < g_groups.size(); gi++) {
            const Group& g = g_groups[gi];
            draw_group_header(dc, g, (int)gi, dy);
            if (g.collapsed) continue;
            for (size_t i = 0; i < g.items.size(); i++)
                draw_entry(dc, g.items[i], g_hot_group == (int)gi && g_hot_item == (int)i, dy);
        }
        draw_insert(dc);
        return;
    }
    // the pane: XP's gradient runs over the visible height
    if (g_look.pane1 == g_look.pane2) fill(dc, rc, g_look.pane1);
    else {
        int h = imax(1, (int)rc.bottom);
        for (int y = 0; y < rc.bottom; y++) {
            RECT r = { 0, y, rc.right, y + 1 };
            fill(dc, r, mix(g_look.pane1, g_look.pane2, y, h - 1));
        }
    }
    if (g_look.pane_border) {
        RECT l = { 0, 0, 1, rc.bottom }, b = { 0, rc.bottom - 1, rc.right, rc.bottom };
        fill(dc, l, RGB(255, 255, 255));
        fill(dc, b, RGB(255, 255, 255));
    }
    int dy = g_scroll;
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        const Group& g = g_groups[gi];
        if (g.hidden) continue;
        draw_header(dc, g, (int)gi, dy);
        if (g.collapsed) continue;
        RECT b = g.body; OffsetRect(&b, 0, -dy);
        fill(dc, b, g_look.body_border);
        RECT in = { b.left + 1, b.top, b.right - 1, b.bottom - 1 };
        fill(dc, in, g_look.body);
        if (g.kind == GroupSpec::Details) draw_details(dc, dy);
        for (size_t i = 0; i < g.items.size(); i++)
            draw_entry(dc, g.items[i], g_hot_group == (int)gi && g_hot_item == (int)i, dy);
    }
    draw_insert(dc);
}

// ---- input -------------------------------------------------------------------------------------

static bool hit(POINT pt, int* group, int* item)
{
    pt.y += g_scroll;
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        const Group& g = g_groups[gi];
        if (g.hidden) continue;
        if (PtInRect(&g.head, pt)) { *group = (int)gi; *item = -1; return true; }
        if (g.collapsed) continue;
        for (size_t i = 0; i < g.items.size(); i++) {
            const Entry& e = g.items[i];
            // XP: the icon and the name are the link, not the empty rest of the row
            RECT r = e.rc;
            r.right = imax(e.text.right, e.rc.left + 16);
            if (PtInRect(&r, pt)) { *group = (int)gi; *item = (int)i; return true; }
        }
    }
    return false;
}

static void set_hot(int group, int item)
{
    if (group == g_hot_group && item == g_hot_item) return;
    g_hot_group = group; g_hot_item = item;
    InvalidateRect(g_wnd, nullptr, FALSE);
}

static void reload_bookmarks()
{
    int bg = bookmarks_group();
    if (bg < 0) return;
    if (g_rename) DestroyWindow(g_rename), g_rename = nullptr;
    read_bookmarks_stat(g_bm_stat);
    fill_bookmarks(g_groups[(size_t)bg]);
    g_hot_group = g_hot_item = -1;
    layout();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void bookmarks_changed() { if (g_wnd) reload_bookmarks(); }

static void open_path(const std::string& p)
{
    if (p.empty()) return;
    if (p == trash_dir()) shell::make_dirs(p);   // the Trash may not exist yet
    g_cb.navigate(p);
}

static void open_new_window(const std::string& p)
{
    if (p.empty()) return;
    if (p == trash_dir()) shell::make_dirs(p);
    std::string err;
    shell::spawn({ "/proc/self/exe", p }, shell::home_dir(), &err);
}

static bool same_uri(const shell::Bookmark& b, const Entry& e) { return b.uri == e.uri; }

static void remove_bookmark(const Entry& e)
{
    std::vector<shell::Bookmark> list = shell::load_bookmarks();
    for (size_t i = 0; i < list.size(); i++)
        if (same_uri(list[i], e)) { list.erase(list.begin() + (long)i); break; }
    shell::save_bookmarks(list);
    reload_bookmarks();
}

static void remember_fold(const std::string& title, bool collapsed);
static void activate(int gi, int i)
{
    Group& g = g_groups[(size_t)gi];
    if (i < 0) {   // the header: fold (XP animates; we do not)
        g.collapsed = !g.collapsed;
        layout();
        InvalidateRect(g_wnd, nullptr, FALSE);
        remember_fold(g.title, g.collapsed);
        return;
    }
    Entry e = g.items[(size_t)i];
    switch (e.kind) {
    case Entry::Bookmark:
        if (e.path.empty()) { MessageBeep(0); break; }
        open_path(e.path);
        break;
    default: open_path(e.path); break;
    }
}

// in-place rename of a bookmark (its label in the bookmarks file)
static WNDPROC g_rename_old;

static LRESULT CALLBACK rename_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wp == VK_RETURN) { PostMessageW(g_wnd, WM_RENAME_DONE, 1, 0); return 0; }
        if (wp == VK_ESCAPE) { PostMessageW(g_wnd, WM_RENAME_DONE, 0, 0); return 0; }
        break;
    case WM_CHAR:
        if (wp == '\r' || wp == 27) return 0;
        break;
    case WM_KILLFOCUS:
        PostMessageW(g_wnd, WM_RENAME_DONE, 1, 0);
        break;
    }
    return CallWindowProcW(g_rename_old, h, msg, wp, lp);
}

static void begin_rename(int gi, int i)
{
    const Entry& e = g_groups[(size_t)gi].items[(size_t)i];
    RECT r = e.text;
    OffsetRect(&r, 0, -g_scroll);
    r.left -= 2;
    r.right = imax(r.right + 8, e.rc.right);
    r.top -= 2; r.bottom = r.top + 18;
    g_rename_group = gi; g_rename_item = i;
    std::u16string t = W(e.label);
    g_rename = CreateWindowExW(0, L"Edit", (LPCWSTR)t.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        r.left, r.top, r.right - r.left, r.bottom - r.top, g_wnd, nullptr, nullptr, nullptr);
    SendMessageW(g_rename, WM_SETFONT, (WPARAM)g_font, FALSE);
    g_rename_old = (WNDPROC)SetWindowLongPtrW(g_rename, GWLP_WNDPROC, (LONG_PTR)rename_proc);
    SendMessageW(g_rename, EM_SETSEL, 0, -1);
    SetFocus(g_rename);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

static void end_rename(bool commit)
{
    if (!g_rename) return;
    HWND ed = g_rename;
    g_rename = nullptr;
    WCHAR buf[512] = {};
    GetWindowTextW(ed, buf, 511);
    DestroyWindow(ed);
    if (commit && g_rename_group >= 0 && (size_t)g_rename_group < g_groups.size() &&
        (size_t)g_rename_item < g_groups[(size_t)g_rename_group].items.size()) {
        const Entry& e = g_groups[(size_t)g_rename_group].items[(size_t)g_rename_item];
        std::string name = w32::utf8_of(buf);
        if (name != e.label) {
            std::vector<shell::Bookmark> list = shell::load_bookmarks();
            for (shell::Bookmark& b : list)
                if (same_uri(b, e)) {
                    b.label = name;
                    if (b.label == shell::base_of(b.path()) || b.label.empty()) b.label.clear();   // the default name again
                }
            shell::save_bookmarks(list);
        }
    }
    g_rename_group = g_rename_item = -1;
    reload_bookmarks();
}

enum { CMD_OPEN = 1, CMD_NEWWINDOW, CMD_RENAME, CMD_REMOVE };

static void context_menu(POINT screen)
{
    POINT pt = screen;
    if (pt.x == -1 && pt.y == -1) { pt.x = 8; pt.y = 8; ClientToScreen(g_wnd, &pt); screen = pt; }
    else ScreenToClient(g_wnd, &pt);
    int gi = -1, i = -2;
    bool on = hit(pt, &gi, &i) && i >= 0;
    const Entry* e = on ? &g_groups[(size_t)gi].items[(size_t)i] : nullptr;
    if (!e) return;   // (no "Add Current Folder" here: folders are dropped on Bookmarks)
    HMENU m = CreatePopupMenu();
    {
        UINT gray = e->path.empty() ? MF_GRAYED : 0;
        AppendMenuW(m, MF_STRING | gray, CMD_OPEN, L"&Open");
        if (g_opts.new_window) AppendMenuW(m, MF_STRING | gray, CMD_NEWWINDOW, L"Open in New &Window");
        SetMenuDefaultItem(m, CMD_OPEN, FALSE);
        if (e->kind == Entry::Bookmark) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, CMD_RENAME, L"Rena&me");
            AppendMenuW(m, MF_STRING, CMD_REMOVE, L"&Remove");
        }
    }
    Entry copy = e ? *e : Entry();   // the menu loop runs timers: the list may be re-read meanwhile
    if (e) set_hot(gi, i);
    int cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, screen.x, screen.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
    set_hot(-1, -1);
    if (cmd <= 0) return;
    switch (cmd) {
    case CMD_OPEN: open_path(copy.path); break;
    case CMD_NEWWINDOW: open_new_window(copy.path); break;
    case CMD_RENAME: {   // find it again (see above)
        int bg = bookmarks_group();
        if (bg >= 0)
            for (size_t k = 0; k < g_groups[(size_t)bg].items.size(); k++)
                if (g_groups[(size_t)bg].items[k].kind == Entry::Bookmark && g_groups[(size_t)bg].items[k].uri == copy.uri) { begin_rename(bg, (int)k); break; }
        break;
    }
    case CMD_REMOVE: remove_bookmark(copy); break;
    }
}

static void scroll_to(int pos)
{
    RECT rc; GetClientRect(g_wnd, &rc);
    pos = imax(0, imin(pos, g_content_h - (int)rc.bottom));
    if (pos == g_scroll) return;
    g_scroll = pos;
    if (g_rename) end_rename(true);
    layout();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// the Longhorn Filter box
static WNDPROC g_filter_old;

static LRESULT CALLBACK filter_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { SetWindowTextW(h, L""); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { if (g_cb.filter_done) g_cb.filter_done(); return 0; }
    if (msg == WM_CHAR && (wp == '\r' || wp == 27)) return 0;
    return CallWindowProcW(g_filter_old, h, msg, wp, lp);
}

void clear_filter()
{
    if (!g_filter) return;
    if (GetWindowTextLengthW(g_filter)) SetWindowTextW(g_filter, L"");
}

// ---- reordering the bookmarks by dragging them -----------------------------------------

// the bookmark entries of the Bookmarks group (their rows, in the file's order)
static std::vector<RECT> bookmark_rows()
{
    std::vector<RECT> rows;
    int bg = bookmarks_group();
    if (bg < 0 || g_groups[(size_t)bg].collapsed) return rows;
    for (const Entry& e : g_groups[(size_t)bg].items) if (e.kind == Entry::Bookmark) rows.push_back(e.rc);
    return rows;
}

// The Bookmarks group's area - its header, its entries and the gaps between
// them, and the empty pane under it when it is the last group: the gap k a
// drop at pt (client) inserts before (k = the count: after the last), -1 when
// pt is elsewhere (the other groups, whose entries take the files).
static int insert_at(POINT pt)
{
    int bg = bookmarks_group();
    if (bg < 0) return -1;
    const Group& g = g_groups[(size_t)bg];
    std::vector<RECT> rows = bookmark_rows();
    int y = pt.y + g_scroll, top = g.head.top, bottom;
    bottom = (size_t)bg + 1 == g_groups.size() ? 1 << 30 : (int)g.body.bottom + (g_grp ? (int)GR_GAP : (int)SECTION_GAP) / 2;
    if (y < top || y >= bottom) return -1;
    int k = 0;
    for (size_t j = 0; j < rows.size(); j++) if (y > (rows[j].top + rows[j].bottom) / 2) k = (int)j + 1;
    return k;
}

static void set_insert(int k)
{
    if (k == g_insert) return;
    g_insert = k;
    InvalidateRect(g_wnd, nullptr, FALSE);
}

// the insertion mark: a 2 px line in the link colour between two rows, with
// short ticks at its ends (comctl32's insert mark)
static void draw_insert(HDC dc)
{
    std::vector<RECT> rows = bookmark_rows();
    if (g_insert < 0 || rows.empty()) return;
    size_t k = (size_t)g_insert;
    int y;
    if (k == 0) y = rows[0].top - 1;
    else if (k >= rows.size()) y = rows.back().bottom;
    else y = (rows[k - 1].bottom + rows[k].top) / 2;
    y -= g_scroll;
    int x0 = rows[0].left - 4, x1 = rows[0].right - 2;
    COLORREF c = g_look.link_hot;
    RECT line = { x0, y - 1, x1, y + 1 };
    fill(dc, line, c);
    RECT t0 = { x0, y - 3, x0 + 2, y + 3 }, t1 = { x1 - 2, y - 3, x1, y + 3 };
    fill(dc, t0, c);
    fill(dc, t1, c);
}

// the drop of a dragged bookmark at gap k: the GTK bookmarks file in the new order
static void move_bookmark(int from, int k)
{
    std::vector<shell::Bookmark> list = shell::load_bookmarks();
    if (from < 0 || from >= (int)list.size() || k < 0 || k > (int)list.size()) return;
    int to = k > from ? k - 1 : k;
    if (to == from) return;
    shell::Bookmark b = list[(size_t)from];
    list.erase(list.begin() + from);
    list.insert(list.begin() + to, b);
    shell::save_bookmarks(list);
    reload_bookmarks();
}

// a press on an entry moved far enough: a folder drag of it (out of the pane:
// into a folder, onto the desktop, another program), over the bookmarks a reorder
static void begin_drag(int gi, int i)
{
    const Entry& e = g_groups[(size_t)gi].items[(size_t)i];
    if ((e.kind != Entry::Bookmark && e.kind != Entry::Place) || e.path.empty() || e.path[0] != '/') return;
    g_drag_bm = -1;
    if (e.kind == Entry::Bookmark) {
        int n = 0;
        for (int j = 0; j < i; j++) if (g_groups[(size_t)gi].items[(size_t)j].kind == Entry::Bookmark) n++;
        g_drag_bm = n;
    }
    shell::Item it;
    it.path = e.path;
    it.name = e.label;
    it.icon = e.icon.empty() ? "folder" : e.icon;
    it.flags = shell::IsFolder;
    set_hot(-1, -1);
    std::string icon = it.icon;
    verbs::drag_items(g_wnd, { it }, uiicons::small(), { uiicons::index(icon) });
    g_drag_bm = -1;
    set_insert(-1);
}

// Folders dropped at gap k of the Bookmarks group: bookmarks there (one that
// exists already moves to k)
static void insert_bookmarks(const FLXPDROP* d, int k)
{
    std::vector<std::string> paths;
    for (UINT j = 0; j < d->cFiles; j++) {
        std::string q = d->ppszFiles[j];
        while (q.size() > 1 && q.back() == '/') q.pop_back();
        paths.push_back(q);
        shell::add_bookmark(q);
    }
    std::vector<shell::Bookmark> list = shell::load_bookmarks(), moved;
    for (const std::string& q : paths)
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].path() == q || list[i].uri == shell::uri_from_path(q)) {
                moved.push_back(list[i]);
                if ((int)i < k) k--;
                list.erase(list.begin() + (long)i);
                break;
            }
    k = imax(0, imin(k, (int)list.size()));
    list.insert(list.begin() + k, moved.begin(), moved.end());
    shell::save_bookmarks(list);
    reload_bookmarks();
}

// Drops. On the Bookmarks group (its header, entries, gaps): folders become
// bookmarks at the gap under the pointer (the insertion mark shows it), our own
// bookmark is reordered; files are refused - a bookmark is never a drop
// target for files. On an entry of the other groups (Desktop, Home, a drive):
// the files go into that folder (not the Trash: it needs the trash verb).
static DWORD drop_here(HWND h, UINT msg, const FLXPDROP* d)
{
    POINT p = { d->x, d->y };
    ScreenToClient(h, &p);
    int k = insert_at(p);
    if (k >= 0) {
        set_hot(-1, -1);
        DWORD eff = 0;
        if (g_drag_bm >= 0 && d->fLocal) eff = DROPEFFECT_MOVE;   // one of ours: a new place in the list
        else if (d->cFiles && (d->dwAllowed & DROPEFFECT_LINK)) {
            eff = DROPEFFECT_LINK;
            for (UINT j = 0; j < d->cFiles; j++) {
                struct stat st;
                if (stat(d->ppszFiles[j], &st) != 0 || !S_ISDIR(st.st_mode)) eff = 0;
            }
        }
        set_insert(eff && msg != WM_FLXP_DROP ? k : -1);
        if (eff && msg == WM_FLXP_DROP) {
            if (eff == DROPEFFECT_MOVE) move_bookmark(g_drag_bm, k);
            else insert_bookmarks(d, k);
        }
        return eff;
    }
    set_insert(-1);
    int gi = -1, i = -1;
    std::string dest;
    if (hit(p, &gi, &i) && i >= 0) {
        const Entry& e = g_groups[(size_t)gi].items[(size_t)i];
        std::string to = e.path == "::desktop" ? shell::home_dir() + "/Desktop" : e.path;   // the desktop's files
        struct stat st;
        if (e.kind == Entry::Place && !to.empty() && to != trash_dir() &&
            stat(to.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            dest = to;
    }
    if (dest.empty()) { set_hot(-1, -1); return 0; }
    set_hot(gi, i);
    return msg == WM_FLXP_DROP ? verbs::drop(d, dest) : verbs::drop_effect(d, dest);
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_FLXP_DRAGOVER:
    case WM_FLXP_DROP:
        return (LRESULT)drop_here(h, msg, (const FLXPDROP*)lp);
    case WM_FLXP_DRAGLEAVE:
        set_hot(-1, -1);
        set_insert(-1);
        return 0;
    case WM_SIZE:
        layout();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        break;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        load_look(h);
        make_fonts();
        if (g_filter) SendMessageW(g_filter, WM_SETFONT, (WPARAM)g_font, TRUE);
        layout();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (!g_tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            g_tracking = true;
        }
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if ((wp & MK_LBUTTON) && GetCapture() == h && g_down_group >= 0 && g_down_item >= 0 &&
            (abs(pt.x - g_down_pt.x) > GetSystemMetrics(SM_CXDRAG) || abs(pt.y - g_down_pt.y) > GetSystemMetrics(SM_CYDRAG))) {
            int dg = g_down_group, di = g_down_item;
            ReleaseCapture();
            g_down_group = -1; g_down_item = -2;
            begin_drag(dg, di);
            return 0;
        }
        int gi = -1, i = -2;
        if (hit(pt, &gi, &i)) set_hot(gi, i); else set_hot(-1, -1);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        set_hot(-1, -1);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hot_group >= 0) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: {
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (g_rename) end_rename(true);
        g_down_group = -1; g_down_item = -2;
        g_down_pt = pt;
        hit(pt, &g_down_group, &g_down_item);
        if (g_down_group >= 0) SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() == h) ReleaseCapture();
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        int gi = -1, i = -2;
        if (hit(pt, &gi, &i) && gi == g_down_group && i == g_down_item) activate(gi, i);
        g_down_group = -1; g_down_item = -2;
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        context_menu(pt);
        return 0;
    }
    case WM_RBUTTONUP: {   // DefWindowProc turns this into WM_CONTEXTMENU
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        ClientToScreen(h, &pt);
        context_menu(pt);
        return 0;
    }
    case WM_MOUSEWHEEL:
        scroll_to(g_scroll - (SHORT)HIWORD(wp) / 120 * 3 * 20);
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si = {}; si.cbSize = sizeof si; si.fMask = SIF_ALL;
        GetScrollInfo(h, SB_VERT, &si);
        int pos = g_scroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= 20; break;
        case SB_LINEDOWN: pos += 20; break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = g_content_h; break;
        }
        scroll_to(pos);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_BOOKMARKS) {
            struct stat st;
            read_bookmarks_stat(st);
            if (st.st_mtime != g_bm_stat.st_mtime || st.st_size != g_bm_stat.st_size || st.st_ino != g_bm_stat.st_ino) reload_bookmarks();
            return 0;
        }
        break;
    case WM_RENAME_DONE:
        end_rename(wp != 0);
        return 0;
    case WM_COMMAND:
        if ((HWND)lp == g_filter && g_filter && HIWORD(wp) == EN_CHANGE) {
            WCHAR buf[256] = {};
            GetWindowTextW(g_filter, buf, 255);
            if (g_cb.filter) g_cb.filter(w32::utf8_of(buf));
            return 0;
        }
        break;
    case WM_DETAILS_PICTURE:
        if (g_pics && g_pics->take()) { layout(); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_BOOKMARKS);
        delete g_pics;
        g_pics = nullptr;
        g_wnd = g_filter = g_rename = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND window() { return g_wnd; }

static void make_filter()
{
    g_filter = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        GR_X, GR_BOX_Y, 110, GR_BOX_H, g_wnd, nullptr, nullptr, nullptr);
    // the pane's font (the icon title font): 4074-dwm.png's "Filter by" is Segoe UI 9 under Jade, as "Storage Favorites"
    SendMessageW(g_filter, WM_SETFONT, (WPARAM)g_font, FALSE);
    SendMessageW(g_filter, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by");
    g_filter_old = (WNDPROC)SetWindowLongPtrW(g_filter, GWLP_WNDPROC, (LONG_PTR)filter_proc);
}

// the Details section open: places_details once the user folded or opened
// it, else XP's default (CDUIView::_ShowSectionExpanded: open where the
// folder's web view layout is SFVMWVL_DETAILS, My Computer)
static bool details_open()
{
    int s = shell::setting_flag_auto("places_details");
    return s < 0 ? g_details_open_default : s == 1;
}

// the groups for the look: the XP task pane's Places and Bookmarks, the
// groups look's Options::spec or standard_spec(); folded ones from
// Options::remember
static void build_groups()
{
    std::vector<GroupSpec> spec;
    if (g_grp) spec = g_opts.spec.empty() ? standard_spec() : g_opts.spec;
    else {
        spec.resize(2);
        spec[0].kind = GroupSpec::Places; spec[0].title = "Places";
        spec[1].kind = GroupSpec::Bookmarks; spec[1].title = "Bookmarks";
        if (g_opts.details) {
            GroupSpec d;
            d.kind = GroupSpec::Details; d.title = "Details";
            spec.push_back(d);
        }
    }
    std::string folded = ";" + (g_opts.remember.empty() ? std::string() : shell::setting(g_opts.remember.c_str())) + ";";
    g_groups.clear();
    read_bookmarks_stat(g_bm_stat);
    for (const GroupSpec& gs : spec) {
        Group g;
        g.kind = gs.kind;
        g.title = gs.title;
        g.custom = gs.places;
        g.collapsed = gs.collapsed || (g_grp && folded.find(";" + gs.title + ";") != std::string::npos);
        if (g.kind == GroupSpec::Details) { g.collapsed = !details_open(); g.hidden = !g_details_set; }
        fill_group(g);
        g_groups.push_back(g);
    }
    g_hot_group = g_hot_item = -1;
    g_scroll = 0;
}

// a fold remembered: Options::remember = the folded groups' titles, ';'-separated
static void remember_fold(const std::string& title, bool collapsed)
{
    for (const Group& g : g_groups)
        if (g.kind == GroupSpec::Details && g.title == title && !g_grp) { shell::write_setting("places_details", collapsed ? "0" : "1"); return; }
    if (g_cb.group_toggled) g_cb.group_toggled(title, collapsed);
    if (g_opts.remember.empty() || !g_grp) return;
    std::string cur = shell::setting(g_opts.remember.c_str()), out;
    size_t i = 0;
    while (i <= cur.size()) {
        size_t j = cur.find(';', i);
        if (j == std::string::npos) j = cur.size();
        std::string t = cur.substr(i, j - i);
        if (!t.empty() && t != title) out += (out.empty() ? "" : ";") + t;
        i = j + 1;
    }
    if (collapsed) out += (out.empty() ? "" : ";") + title;
    shell::write_setting(g_opts.remember.c_str(), out);
}

GroupSpec computer_group()
{
    GroupSpec computer;
    computer.title = "Computer";
    computer.places.push_back({ "My Computer", "::computer", "computer" });
    if (auto c = shell::folder_for("::computer")) {
        std::vector<shell::Item> drives;
        c->enumerate(drives, false);
        for (const shell::Item& dv : drives) if (dv.path != shell::home_dir()) computer.places.push_back({ dv.name, dv.path, dv.icon });
    }
    return computer;
}

std::vector<GroupSpec> standard_spec()
{
    GroupSpec common;
    common.title = "Common Places";
    common.places.push_back({ "Desktop", "::desktop", "desktop" });
    common.places.push_back({ "Home", shell::home_dir(), "home" });
    std::string docs = user_dir("XDG_DOCUMENTS_DIR", "Documents"), pics = user_dir("XDG_PICTURES_DIR", "Pictures");
    if (!docs.empty()) common.places.push_back({ "Documents", docs, "folder" });
    if (!pics.empty()) common.places.push_back({ "Pictures", pics, "folder" });
    common.places.push_back({ "Trash", trash_dir(), "trash" });
    GroupSpec bm;
    bm.kind = GroupSpec::Bookmarks;
    bm.title = "Bookmarks";
    return { common, computer_group(), bm };
}

HWND create(HWND parent, int id, const Callbacks& cb, const Options& o)
{
    g_cb = cb;
    g_opts = o;
    g_grp = o.groups;
    g_icons = shell::setting_flag("places_icons", true);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_places";
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, L"flxp_places", nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    load_look(g_wnd);
    make_fonts();
    build_groups();
    if (g_grp) make_filter();
    SetTimer(g_wnd, TIMER_BOOKMARKS, 1000, nullptr);   // other programs change the bookmarks too
    return g_wnd;
}

bool icons() { return g_icons; }

void set_icons(bool on)
{
    g_icons = on;
    shell::write_setting("places_icons", on ? "1" : "0");
    if (!g_wnd) return;
    layout();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void folder_changed() { if (g_wnd && g_grp) InvalidateRect(g_wnd, nullptr, FALSE); }

void set_top_inset(int px)
{
    if (px == g_inset) return;
    g_inset = px;
    if (!g_wnd) return;
    layout();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void set_details(const shell::details::Info& in, bool preview_ok, bool open_by_default)
{
    g_details = in;
    g_details_set = true;
    g_details_preview = preview_ok;
    g_details_open_default = open_by_default;
    if (!g_wnd) return;
    std::vector<std::string> want;
    if (preview_ok && in.xp_preview && !in.previews.empty()) want.push_back(in.previews[0]);
    if (!g_pics && !want.empty()) g_pics = new preview::Pictures(g_wnd, WM_DETAILS_PICTURE);
    if (g_pics) g_pics->want(want, PREVIEW_W);
    for (Group& g : g_groups)
        if (g.kind == GroupSpec::Details) {
            g.hidden = shell::details::xp_title(in).empty() && shell::details::xp_lines(in).empty();
            g.collapsed = !details_open();
        }
    layout();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void destroy()
{
    if (g_wnd) DestroyWindow(g_wnd);
    g_wnd = g_filter = g_rename = nullptr;
    g_groups.clear();
    g_scroll = 0;
    g_hot_group = g_hot_item = -1;
    g_tracking = false;
}

void set_longhorn(bool longhorn)
{
    if (!g_wnd || longhorn == g_grp) return;
    if (g_rename) end_rename(false);
    g_grp = longhorn;
    if (!g_grp && g_filter) { DestroyWindow(g_filter); g_filter = nullptr; }
    load_look(g_wnd);
    make_fonts();
    if (g_grp) make_filter();   // in the new font
    build_groups();
    layout();
    InvalidateRect(g_wnd, nullptr, TRUE);
}

} // namespace places
