// Measured on lh4051-225221.png (the Administrator folder, a Document folder):
// the row under the header is the top of the Document::Rebar.BACKGROUND
// strip (slate.msstyles bitmap 179 in 4051, 196 in 4074; 838x8, sizing
// margins 1 1 1 0): a 1 px highlight row, then edge (36,67,142) rising to
// (85,115,187) at the centre and falling again, symmetric about the window's
// centre; 1 px side borders (104,122,165). The text is white (ExplorerMenu::
// Toolbar TEXTCOLOR), the checked button has the ExplorerMenu::Toolbar.BUTTON
// frame (bitmap 340 / 370: 20x115, five 23-row images, sizing margins 5).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "lhbar.h"
#include "shell/config.h"
#include "shell/lhtheme.h"

namespace lhbar {

namespace {

struct Strip { int w = 0, h = 0; std::vector<uint32_t> px; };   // 0x00RRGGBB

bool g_init, g_enabled = true;
Strip g_bg;                         // the gradient strip, empty = drawn gradient
COLORREF g_left = RGB(36, 67, 142), g_centre = RGB(85, 115, 187), g_right = RGB(36, 67, 142);
COLORREF g_text = RGB(255, 255, 255);
HBITMAP g_frames;                   // premultiplied 20x115, or null = drawn frames
HBITMAP g_cache;                    // the background at g_cw x g_ch
int g_cw, g_ch;
WNDPROC g_rebar_old;

bool parse_colour(const char* s, COLORREF* out)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') s++;
    char* end = nullptr;
    unsigned long v = strtoul(s, &end, 16);
    if (end - s != 6) return false;
    *out = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    return true;
}

// "#a #b" = edge, centre (symmetric); "#a #b #c" = left, centre, right
bool parse_gradient(const std::string& s)
{
    std::vector<COLORREF> c;
    size_t a = 0;
    while (a < s.size()) {
        while (a < s.size() && (s[a] == ' ' || s[a] == ',' || s[a] == '\t')) a++;
        if (a >= s.size()) break;
        size_t b = s.find_first_of(" ,\t", a);
        if (b == std::string::npos) b = s.size();
        COLORREF col;
        if (!parse_colour(s.substr(a, b - a).c_str(), &col)) return false;
        c.push_back(col);
        a = b;
    }
    if (c.size() == 1) c.push_back(c[0]);
    if (c.size() == 2) c.push_back(c[0]);
    if (c.size() != 3) return false;
    g_left = c[0]; g_centre = c[1]; g_right = c[2];
    return true;
}

HMODULE load_module(const std::string& path)
{
    std::u16string w = w32::utf16_of(path.c_str());
    return LoadLibraryExW((LPCWSTR)w.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
}

// a bitmap resource's pixels (0xAARRGGBB as stored), via a DIB section
bool read_bitmap(HMODULE m, int id, Strip& out, bool* has_alpha = nullptr)
{
    HBITMAP b = (HBITMAP)LoadImageW(m, MAKEINTRESOURCEW(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!b) return false;
    DIBSECTION ds = {};
    bool ok = GetObjectW(b, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32;
    if (ok) {
        int w = ds.dsBm.bmWidth, h = ds.dsBmih.biHeight < 0 ? -ds.dsBmih.biHeight : ds.dsBmih.biHeight;
        bool topdown = ds.dsBmih.biHeight < 0;
        out.w = w; out.h = h;
        out.px.resize((size_t)w * h);
        const uint8_t* bits = (const uint8_t*)ds.dsBm.bmBits;
        bool alpha = false;
        for (int y = 0; y < h; y++) {
            const uint32_t* row = (const uint32_t*)(bits + (size_t)(topdown ? y : h - 1 - y) * ds.dsBm.bmWidthBytes);
            for (int x = 0; x < w; x++) { out.px[(size_t)y * w + x] = row[x]; if ((row[x] >> 24) != 0xff) alpha = true; }
        }
        if (has_alpha) *has_alpha = alpha;
    }
    DeleteObject(b);
    return ok;
}

struct Part { const char* name; int id4074, id4051; };
const Part parts[] = {
    { "default", 194, 177 }, { "document", 196, 179 }, { "music", 197, 180 }, { "picture", 198, 181 },
    { "video", 198, 181 }, { "contact", 199, 182 }, { "game", 200, 183 },
};

// The strip and frames from a Longhorn slate.msstyles: the 4074 resource ids,
// or 4051's when the 4074 ones are not an 838x8 strip.
void load_msstyles(const std::string& style)
{
    std::string path = shell::theme_setting("menubar_msstyles");
    if (path.empty()) {
        std::string sys = shell::theme_setting("longhorn_system32");
        if (sys.empty()) return;
        path = sys + "/../Resources/Themes/Slate/slate.msstyles";
    }
    HMODULE m = load_module(path);
    if (!m) return;
    const Part* p = nullptr;
    for (const Part& q : parts) if (style == q.name) p = &q;
    if (!p) { fprintf(stderr, "flxp: menubar_style=%s unknown, using document\n", style.c_str()); p = &parts[1]; }
    Strip s;
    bool is4051 = false;
    if (!read_bitmap(m, p->id4074, s) || s.w != 838 || s.h != 8) {
        s = Strip();
        if (read_bitmap(m, p->id4051, s) && s.w == 838 && s.h == 8) is4051 = true;
        else s = Strip();
    }
    if (s.w) {
        for (uint32_t& v : s.px) v &= 0xffffff;
        g_bg = s;
    }
    // ExplorerMenu::Toolbar.BUTTON: 20x115, alpha
    Strip f;
    bool alpha = false;
    if (s.w && read_bitmap(m, is4051 ? 340 : 370, f, &alpha) && f.w == 20 && f.h == 115 && alpha) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = 20; bi.bmiHeader.biHeight = -115; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        g_frames = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (g_frames) {
            uint32_t* d = (uint32_t*)bits;
            for (size_t i = 0; i < f.px.size(); i++) {
                uint32_t v = f.px[i], a = v >> 24;
                d[i] = a << 24 | (((v >> 16) & 255) * a / 255) << 16 | (((v >> 8) & 255) * a / 255) << 8 | ((v & 255) * a / 255);
            }
        }
    }
    FreeLibrary(m);
}

uint32_t mix(uint32_t a, uint32_t b, int t, int n)   // a + (b - a) * t / n, 0x00RRGGBB
{
    uint32_t r = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (a >> sh) & 255, cb = (b >> sh) & 255;
        r |= (uint32_t)(ca + (cb - ca) * t / (n ? n : 1)) << sh;
    }
    return r;
}

uint32_t rgb(COLORREF c) { return (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c); }

// one column of the background: its top row and body colour
void column(int x, int W, uint32_t* top, uint32_t* body)
{
    if (g_bg.w) {
        // margins 1 1: the strip's first and last columns stay at the edges
        int sx;
        if (x == 0) sx = 0;
        else if (x == W - 1) sx = g_bg.w - 1;
        else sx = 1 + (int)((long long)(x - 1) * (g_bg.w - 2) / (W > 2 ? W - 2 : 1));
        *top = g_bg.px[(size_t)sx];
        *body = g_bg.px[(size_t)g_bg.w * 3 + sx];
        return;
    }
    int half = W / 2;
    *body = x < half ? mix(rgb(g_left), rgb(g_centre), x, half) : mix(rgb(g_centre), rgb(g_right), x - half, W - 1 - half);
    *top = mix(*body, 0xffffff, 24, 100);   // the highlight row: about 24% white
}

void build_cache(int W, int H)
{
    if (g_cache && g_cw == W && g_ch == H) return;
    if (g_cache) DeleteObject(g_cache);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    g_cache = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    g_cw = W; g_ch = H;
    if (!g_cache) return;
    uint32_t* d = (uint32_t*)bits;
    for (int x = 0; x < W; x++) {
        uint32_t top, body;
        column(x, W, &top, &body);
        for (int y = 0; y < H; y++) d[(size_t)y * W + x] = 0xff000000 | (y == 0 ? top : body);
    }
}

// The drawn frames: hot = light border on a light wash, pressed = dark
// border on a dark wash, checked = dark border on a light wash.
void drawn_frame(HDC dc, const RECT& r, int state)
{
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!b) return;
    uint32_t* d = (uint32_t*)bits;
    uint32_t edge = state == 1 ? 0xffffff : 0x101828, fill = state == 2 ? 0x000000 : 0xffffff;
    int ea = state == 1 ? 150 : 170, fa = state == 1 ? 40 : state == 2 ? 50 : 45;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            bool corner = (x == 0 || x == w - 1) && (y == 0 || y == h - 1);
            bool border = x == 0 || y == 0 || x == w - 1 || y == h - 1;
            uint32_t c = border ? edge : fill;
            int a = corner ? 0 : border ? ea : fa;
            d[(size_t)y * w + x] = (uint32_t)a << 24 | (((c >> 16) & 255) * a / 255) << 16 | (((c >> 8) & 255) * a / 255) << 8 | ((c & 255) * a / 255);
        }
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, b);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, r.left, r.top, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(b);
}

LRESULT CALLBACK rebar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ERASEBKGND) {
        RECT rc; GetClientRect(h, &rc);
        paint_background((HDC)wp, rc);
        return 1;
    }
    return CallWindowProcW(g_rebar_old, h, msg, wp, lp);
}

} // namespace

void init()
{
    if (g_init) return;
    g_init = true;
    std::string style = shell::theme_setting("menubar_style");
    if (style == "theme") { g_enabled = false; return; }
    std::string grad = shell::theme_setting("menubar_gradient");
    if (!grad.empty() && !parse_gradient(grad))
        fprintf(stderr, "flxp: menubar_gradient=%s: want '#rrggbb #rrggbb [#rrggbb]'\n", grad.c_str());
    // an explicit gradient wins unless a style is named as well
    if (style != "gradient" && (grad.empty() || !style.empty())) load_msstyles(style.empty() ? "document" : style);
    std::string t = shell::theme_setting("menubar_text");
    if (!t.empty() && !parse_colour(t.c_str(), &g_text)) fprintf(stderr, "flxp: menubar_text=%s: want #rrggbb\n", t.c_str());
}

bool enabled() { init(); return g_enabled; }

void theme_changed()
{
    // the session theme may name another strip (flxp_menubar_*): start over
    if (g_frames) DeleteObject(g_frames);
    if (g_cache) DeleteObject(g_cache);
    g_frames = g_cache = nullptr;
    g_cw = g_ch = 0;
    g_bg = Strip();
    g_left = g_right = RGB(36, 67, 142); g_centre = RGB(85, 115, 187);
    g_text = RGB(255, 255, 255);
    g_init = false;
    bool was = g_enabled;
    g_enabled = true;
    init();
    if (was && !g_enabled) g_enabled = true;   // the rebar stays subclassed: keep drawing the bar
}

void paint_background(HDC dc, const RECT& rc)
{
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) return;
    build_cache(W, H);
    if (!g_cache) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_cache);
    BitBlt(dc, rc.left, rc.top, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void draw_frame(HDC dc, const RECT& r, int state)
{
    if (state <= 0) return;
    if (!g_frames) { drawn_frame(dc, r, state == 4 ? 3 : state); return; }
    // image 1 hot, 2 pressed, 3 checked (the slate's 4th image; hot+checked: also 3)
    int img = state == 1 ? 1 : state == 2 ? 2 : 3;
    const int W = 20, H = 23, M = 5, sy = img * H;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w < 2 * M || h < 2 * M) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_frames);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    int xs[4] = { 0, M, W - M, W }, ys[4] = { 0, M, H - M, H };
    int xd[4] = { r.left, r.left + M, r.right - M, r.right }, yd[4] = { r.top, r.top + M, r.bottom - M, r.bottom };
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            if (xd[i + 1] > xd[i] && yd[j + 1] > yd[j])
                AlphaBlend(dc, xd[i], yd[j], xd[i + 1] - xd[i], yd[j + 1] - yd[j], mem, xs[i], sy + ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

COLORREF text_color(bool active)
{
    if (active) return g_text;
    // inactive: halfway to the bar's centre colour
    uint32_t c = mix(rgb(g_text), g_bg.w ? g_bg.px[(size_t)g_bg.w * 3 + g_bg.w / 2] : rgb(g_centre), 45, 100);
    return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

void attach(HWND rebar)
{
    if (!enabled() || g_rebar_old) return;
    g_rebar_old = (WNDPROC)SetWindowLongPtrW(rebar, GWLP_WNDPROC, (LONG_PTR)rebar_proc);
}

void detach(HWND rebar)
{
    if (!g_rebar_old) return;
    SetWindowLongPtrW(rebar, GWLP_WNDPROC, (LONG_PTR)g_rebar_old);
    g_rebar_old = nullptr;
    InvalidateRect(rebar, nullptr, TRUE);
}

LRESULT custom_draw(NMTBCUSTOMDRAW* cd, bool active, bool (*checked)(int id))
{
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (cd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    UINT st = cd->nmcd.uItemState;
    bool chk = checked && checked((int)cd->nmcd.dwItemSpec);
    int frame = 0;
    if (st & CDIS_DISABLED) frame = chk ? 3 : 0;
    else if (st & CDIS_SELECTED) frame = 2;
    else if (chk) frame = (st & CDIS_HOT) ? 4 : 3;
    else if (st & CDIS_HOT) frame = 1;
    draw_frame(cd->nmcd.hdc, cd->nmcd.rc, frame);
    cd->clrText = text_color(active);
    return TBCDRF_NOEDGES | TBCDRF_NOOFFSET | TBCDRF_NOBACKGROUND;
}

} // namespace lhbar
