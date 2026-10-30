// Build 4074's view control (viewbutton.h). Geometry from the 4074 XAML and
// lh4074-195348/-195353/-195358/-225249 (the button and the popup measured):
//   the split button: 60 x 22 (imageres 7410-7412 stretched, corners kept) and
//     16 x 22 (7407-7409, the arrows 7413 centred), 2 px from the pane's left
//     and 1 px above its bottom; the view's picture fitted into 22 x 20 at
//     (3, 1), "View" in Segoe UI 11 px 3 px after it
//   the popup: 52 x 187, white in a 1 px #C0C0C0 border; the slider 15 px wide
//     at x 3, from y 11 to 176 (a 5 px groove 180 | 152 179 206 | 180 at x 8,
//     a 15 x 10 thumb, border 180, filled 254 -> 234 across); the stops' 30 x 25
//     pictures at x 19, y 9, 40, 71, 112, 153 (the 37 x 25 art stretched)
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>
#include "viewbutton.h"
#include "shell/lhdata.h"
#include "gdi/image.h"
#include "w32/user.h"

namespace viewbutton {

using shell::viewmodel::View;
using shell::viewmodel::Mode;
namespace vm = shell::viewmodel;

struct Pic { HBITMAP bmp = nullptr; int w = 0, h = 0; };

// a view button: one per browsing window (the explorer's, the file dialog's)
struct Inst {
    HWND strip = nullptr;
    browser::Browser* b = nullptr;
    int hot = -1, down = -1;           // the part under the pointer, pressed: 0 the left part, 1 the arrows
};
static std::vector<Inst*> g_all;
static Inst* g_in;                     // the one whose window has a message
static Inst* g_pi;                     // the one whose popup is open
static Pic g_left[3], g_right[3], g_arrows, g_stop[5], g_stop_small[5], g_hot, g_pressed;
static bool g_art;                     // the 4074 pack's pictures are there
static HFONT g_font;

enum { BTN_X = 2, BTN_Y = 2, LEFT_W = 60, RIGHT_W = 16, BTN_H = 22 };
enum { POP_W = 52, POP_H = 187, SL_X = 3, SL_W = 15, SL_TOP = 11, SL_BOTTOM = 176, THUMB_H = 10, STOP_X = 19, STOP_W = 30, STOP_H = 25 };
static const int k_stop_y[5] = { 9, 40, 71, 112, 153 };
// the thumb's centre travels [16, 171]; each stop's band reaches halfway to its neighbours
static const int k_band[6] = { SL_TOP + THUMB_H / 2, 37, 68, 104, 145, SL_BOTTOM - THUMB_H / 2 };

// ---- pictures -------------------------------------------------------------------------

static void free_pic(Pic& p) { if (p.bmp) DeleteObject(p.bmp); p = Pic(); }

// imageres <id> of the 4074 pack, scaled to w x h (0: as it is), as a premultiplied DIB
static Pic load(int id, int w = 0, int h = 0)
{
    Pic out;
    std::string path = shell::lhde_find("packs/4074/imageres/" + std::to_string(id) + ".png");
    if (path.empty()) return out;
    gdi::Bitmap* b = gdi::load_image(path);
    if (!b) return out;
    if (w > 0 && h > 0 && (w != b->width || h != b->height)) {
        gdi::Bitmap* s = gdi::resample(b, w, h);
        gdi::destroy_bitmap(b);
        b = s;
        if (!b) return out;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = b->width; bi.bmiHeader.biHeight = -b->height; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    out.bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (out.bmp) {
        uint32_t* d = (uint32_t*)bits;
        for (int y = 0; y < b->height; y++)
            for (int x = 0; x < b->width; x++) {
                uint32_t v = b->row(y)[x], a = v >> 24;
                d[(size_t)y * b->width + x] = a << 24 | ((v >> 16 & 255) * a / 255) << 16 | ((v >> 8 & 255) * a / 255) << 8 | (v & 255) * a / 255;
            }
        out.w = b->width; out.h = b->height;
    }
    gdi::destroy_bitmap(b);
    return out;
}

static void load_all()
{
    static bool loaded;
    if (loaded) return;
    loaded = true;
    for (int i = 0; i < 3; i++) { g_left[i] = load(7410 + i); g_right[i] = load(7407 + i); }
    g_arrows = load(7413);
    for (int i = 0; i < 5; i++) {
        g_stop[i] = load(7400 + i, STOP_W, STOP_H);           // the popup's: stretched across (as 4074's)
        g_stop_small[i] = load(7400 + i, 22, 15);             // the button's: fitted into 22 x 20
    }
    g_hot = load(7405, STOP_W, STOP_H);
    g_pressed = load(7406, STOP_W, STOP_H);
    g_art = g_left[0].bmp && g_right[0].bmp && g_stop[0].bmp;
    g_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void blend(HDC dc, const Pic& p, int x, int y, int w = -1, int h = -1, int sx = 0, int sy = 0, int sw = -1, int sh = -1)
{
    if (!p.bmp) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, p.bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x, y, w < 0 ? p.w : w, h < 0 ? p.h : h, mem, sx, sy, sw < 0 ? p.w : sw, sh < 0 ? p.h : sh, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// a picture stretched into r with its 4 px edges kept (the rounded corners)
static void blend_nine(HDC dc, const Pic& p, const RECT& r)
{
    if (!p.bmp) return;
    const int m = 4;
    int sx[4] = { 0, m, p.w - m, p.w }, sy[4] = { 0, m, p.h - m, p.h };
    int w = r.right - r.left, h = r.bottom - r.top;
    int dx[4] = { 0, m, w - m, w }, dy[4] = { 0, m, h - m, h };
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            if (dx[i + 1] > dx[i] && dy[j + 1] > dy[j])
                blend(dc, p, r.left + dx[i], r.top + dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j], sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]);
}

static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

// without the pack: a plain picture of stop i in r (lines, two tiles, one, four, nine)
static void glyph(HDC dc, int i, const RECT& r)
{
    COLORREF ink = RGB(90, 110, 140), pale = RGB(150, 150, 150);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    switch (i) {
    case 0: for (int k = 0; k < 5; k++) { fill(dc, cx - 9, cy - 7 + 3 * k, 2, 2, ink); fill(dc, cx - 6, cy - 7 + 3 * k, 15, 1, pale); } break;
    case 1: for (int k = 0; k < 2; k++) { fill(dc, cx - 9, cy - 7 + 8 * k, 6, 6, ink); fill(dc, cx - 1, cy - 6 + 8 * k, 10, 1, pale); } break;
    case 2: fill(dc, cx - 8, cy - 7, 16, 14, ink); break;
    case 3: for (int k = 0; k < 4; k++) fill(dc, cx - 8 + (k & 1) * 9, cy - 7 + (k >> 1) * 8, 7, 6, ink); break;
    default: for (int k = 0; k < 9; k++) fill(dc, cx - 7 + (k % 3) * 6, cy - 7 + (k / 3) * 6, 3, 3, ink); break;
    }
}

// the stop the button shows: the view's (Icons: the icon stop of its size)
static int shown_stop()
{
    if (!g_in || !g_in->b) return vm::STOP_MEDIUM;
    return vm::stop_of(browser::current_view(*g_in->b));
}

// ---- the split button ---------------------------------------------------------------------

static RECT part_rect(int part)
{
    return part == 0 ? RECT{ BTN_X, BTN_Y, BTN_X + LEFT_W, BTN_Y + BTN_H } : RECT{ BTN_X + LEFT_W, BTN_Y, BTN_X + LEFT_W + RIGHT_W, BTN_Y + BTN_H };
}

static int part_at(int x, int y)
{
    for (int p = 0; p < 2; p++) {
        RECT r = part_rect(p);
        POINT pt = { x, y };
        if (PtInRect(&r, pt)) return p;
    }
    return -1;
}

static void paint_button(HDC dc)
{
    load_all();
    RECT rc;
    GetClientRect(g_in->strip, &rc);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));   // the pane's white
    for (int p = 0; p < 2; p++) {
        RECT r = part_rect(p);
        int st = g_in->down == p && g_in->hot == p ? 2 : g_in->hot == p || g_in->down == p ? 1 : 0;
        if (g_art) blend_nine(dc, p == 0 ? g_left[st] : g_right[st], r);
        else {
            fill(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, st ? RGB(232, 240, 250) : RGB(248, 248, 248));
            HBRUSH br = CreateSolidBrush(RGB(170, 190, 210));
            FrameRect(dc, &r, br);
            DeleteObject(br);
        }
    }
    // the arrows
    RECT ar = part_rect(1);
    if (g_arrows.bmp) blend(dc, g_arrows, (ar.left + ar.right - g_arrows.w) / 2, (ar.top + ar.bottom - g_arrows.h) / 2);
    else {
        int cx = (ar.left + ar.right) / 2, cy = (ar.top + ar.bottom) / 2;
        for (int k = 0; k < 3; k++) { fill(dc, cx - k, cy - 4 + k, 2 * k + 1, 1, RGB(0, 0, 0)); fill(dc, cx - k, cy + 4 - k, 2 * k + 1, 1, RGB(0, 0, 0)); }
    }
    // the view's picture (fitted into 22 x 20 at 3, 1) and "View"
    int stop = shown_stop();
    RECT pic = { BTN_X + 3, BTN_Y + 1, BTN_X + 3 + 22, BTN_Y + 1 + 20 };
    if (g_stop_small[stop].bmp) blend(dc, g_stop_small[stop], pic.left, pic.top + (20 - g_stop_small[stop].h) / 2);
    else glyph(dc, stop, pic);
    HGDIOBJ of = SelectObject(dc, g_font ? (HGDIOBJ)g_font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    RECT tr = { pic.right + 3, BTN_Y, BTN_X + LEFT_W - 2, BTN_Y + BTN_H };
    DrawTextW(dc, L"View", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
}

// ---- the slider popup ---------------------------------------------------------------------

static HWND g_pop;
static int g_thumb = 16;               // the thumb's centre (popup coordinates)
static int g_pop_hot = -1, g_pop_down = -1;
static bool g_dragging;

static int stop_of_y(int y)
{
    for (int i = 0; i < 5; i++) if (y < k_band[i + 1]) return i;
    return 4;
}

static int thumb_y(const View& v)
{
    int i = vm::stop_of(v);
    int top = k_band[i], h = k_band[i + 1] - k_band[i];
    // 3 px dead ends, so each band's ends are reachable
    return top + 3 + vm::pos_of(i, v.size, h - 6);
}

static View view_at(int y)
{
    int i = stop_of_y(y);
    int top = k_band[i], h = k_band[i + 1] - k_band[i];
    View v;
    v.mode = vm::stop(i).mode;
    v.size = vm::size_at(i, y - top - 3, h - 6);
    v.hide_names = g_pi && g_pi->b ? g_pi->b->hide_names : false;
    return vm::clamped(v);
}

static int pop_stop_at(int x, int y)
{
    if (x < STOP_X || x >= STOP_X + STOP_W) return -1;
    for (int i = 0; i < 5; i++) if (y >= k_stop_y[i] && y < k_stop_y[i] + STOP_H) return i;
    return -1;
}

static void paint_popup(HWND h, HDC dc)
{
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HBRUSH border = CreateSolidBrush(RGB(192, 192, 192));
    FrameRect(dc, &rc, border);
    DeleteObject(border);
    // the groove: 180 | 152 179 206 | 180, its ends fading
    int gx = SL_X + 5;
    static const COLORREF groove[5] = { RGB(180, 180, 180), RGB(152, 152, 152), RGB(179, 179, 179), RGB(206, 206, 206), RGB(180, 180, 180) };
    for (int k = 0; k < 5; k++) fill(dc, gx + k, SL_TOP + 1, 1, SL_BOTTOM - SL_TOP - 2, groove[k]);
    static const COLORREF end1[5] = { RGB(198, 198, 198), RGB(175, 175, 175), RGB(179, 179, 179), RGB(189, 189, 189), RGB(197, 197, 197) };
    static const COLORREF end2[5] = { RGB(254, 254, 254), RGB(219, 219, 219), RGB(217, 217, 217), RGB(218, 218, 218), RGB(251, 251, 251) };
    for (int k = 0; k < 5; k++) {
        fill(dc, gx + k, SL_BOTTOM - 1, 1, 1, end1[k]); fill(dc, gx + k, SL_BOTTOM, 1, 1, end2[k]);
        fill(dc, gx + k, SL_TOP + 1, 1, 1, end1[k]); fill(dc, gx + k, SL_TOP, 1, 1, end2[k]);
    }
    // the thumb: 15 x 10, border 180, filled 254 -> 234 across
    int ty = g_thumb - THUMB_H / 2;
    for (int x = 1; x < SL_W - 1; x++) {
        int v = 254 - 20 * (x - 1) / (SL_W - 3);
        fill(dc, SL_X + x, ty + 1, 1, THUMB_H - 2, RGB(v, v, v));
    }
    RECT th = { SL_X, ty, SL_X + SL_W, ty + THUMB_H };
    HBRUSH tb = CreateSolidBrush(RGB(180, 180, 180));
    FrameRect(dc, &th, tb);
    DeleteObject(tb);
    // the stops (hot and pressed under the picture: ReverseZOrder)
    for (int i = 0; i < 5; i++) {
        RECT r = { STOP_X, k_stop_y[i], STOP_X + STOP_W, k_stop_y[i] + STOP_H };
        if (g_pop_down == i && g_pop_hot == i) blend(dc, g_pressed, r.left, r.top);
        else if (g_pop_hot == i) blend(dc, g_hot, r.left, r.top);
        if (g_stop[i].bmp) blend(dc, g_stop[i], r.left, r.top);
        else glyph(dc, i, r);
    }
}

static void move_thumb(int y)
{
    if (y < k_band[0]) y = k_band[0];
    if (y > k_band[5]) y = k_band[5];
    if (y == g_thumb) return;
    g_thumb = y;
    InvalidateRect(g_pop, nullptr, FALSE);
    if (!g_pi || !g_pi->b) return;
    View v = view_at(y);
    if (v != browser::current_view(*g_pi->b)) browser::show_view(*g_pi->b, v);   // live, as 4074's _OnSliderValueChanged
}

static LRESULT CALLBACK popup_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint_popup(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (g_dragging) { move_thumb(y); return 0; }
        int s = pop_stop_at(x, y);
        if (s != g_pop_hot) { g_pop_hot = s; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        SetCapture(h);
        if (x < STOP_X) { g_dragging = true; move_thumb(y); return 0; }   // the slider: the thumb goes there
        g_pop_down = pop_stop_at(x, y);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        ReleaseCapture();
        if (g_dragging) { g_dragging = false; return 0; }
        int s = pop_stop_at(x, y);
        if (s >= 0 && s == g_pop_down && g_pi && g_pi->b) {   // the stop as it was last; the popup closes
            View v = browser::view_memory().at(s);
            v.hide_names = g_pi->b->hide_names;
            browser::show_view(*g_pi->b, v);
            DestroyWindow(h);
            return 0;
        }
        g_pop_down = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void open_popup(Inst* in)
{
    if (!in || !in->b || !in->strip || g_pop) return;
    g_pi = in;
    static bool registered;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = popup_proc;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"flxp_viewslider";
        RegisterClassW(&wc);
        registered = true;
    }
    load_all();
    // beside the arrows, from the button's top; kept on the screen
    POINT at = { BTN_X + LEFT_W, BTN_Y };
    ClientToScreen(in->strip, &at);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (at.x + POP_W > sw) at.x = sw - POP_W;
    if (at.y + POP_H > sh) at.y = sh - POP_H;
    if (at.y < 0) at.y = 0;
    g_thumb = thumb_y(browser::current_view(*in->b));
    g_pop_hot = g_pop_down = -1;
    g_dragging = false;
    g_pop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"flxp_viewslider", nullptr, WS_POPUP,   // (no WS_BORDER: the toolkit would hand it to the window manager)
                            at.x, at.y, POP_W, POP_H, GetAncestor(in->strip, GA_ROOT), nullptr, nullptr, nullptr);
    ShowWindow(g_pop, SW_SHOWNA);
    HWND pop = g_pop;
    MSG msg;
    while (IsWindow(pop) && GetMessageW(&msg, nullptr, 0, 0)) {   // a small modal loop, as a menu's
        bool ours = msg.hwnd == pop;
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE || msg.wParam == VK_RETURN) break;
            if (msg.wParam == VK_UP || msg.wParam == VK_DOWN) {   // the thumb a few pixels up or down
                move_thumb(g_thumb + (msg.wParam == VK_UP ? -3 : 3));
                continue;
            }
        }
        if (!ours && (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN ||
                      msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN)) break;   // eaten, as a menu's
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(pop)) DestroyWindow(pop);
    g_pop = nullptr;
    g_pi = nullptr;
    in->down = -1;
    InvalidateRect(in->strip, nullptr, FALSE);
    if (in->b && IsWindow(in->b->list)) SetFocus(in->b->list);
}

// ---- the strip ----------------------------------------------------------------------------

static LRESULT CALLBACK strip_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Inst* in = (Inst*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!in) return DefWindowProcW(h, msg, wp, lp);
    g_in = in;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint_button(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        int p = part_at((short)LOWORD(lp), (short)HIWORD(lp));
        if (p != in->hot) {
            in->hot = p;
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (in->hot >= 0) { in->hot = -1; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        int p = part_at((short)LOWORD(lp), (short)HIWORD(lp));
        in->down = p;
        InvalidateRect(h, nullptr, FALSE);
        UpdateWindow(h);
        if (p == 1) { open_popup(in); return 0; }   // the arrows: the slider at once, as a menu
        if (p == 0) SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        int p = part_at((short)LOWORD(lp), (short)HIWORD(lp));
        bool click = in->down == 0 && p == 0;
        in->down = -1;
        if (GetCapture() == h) ReleaseCapture();
        InvalidateRect(h, nullptr, FALSE);
        if (click && in->b) browser::view_command(*in->b, browser::VC_CYCLE);   // Details -> Tiles -> Icons
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void listener(browser::Browser& b) { changed(b); }

HWND create(HWND parent, browser::Browser& b)
{
    static bool registered;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = strip_proc;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"flxp_viewbutton";
        RegisterClassW(&wc);
        registered = true;
        browser::add_view_listener(listener);
    }
    Inst* in = new Inst;
    in->b = &b;
    in->strip = CreateWindowExW(0, L"flxp_viewbutton", nullptr, WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, STRIP_H, parent, nullptr, nullptr, nullptr);
    if (!in->strip) { delete in; return nullptr; }
    SetWindowLongPtrW(in->strip, GWLP_USERDATA, (LONG_PTR)in);
    g_all.push_back(in);
    return in->strip;
}

void destroy(HWND strip)
{
    for (size_t i = 0; i < g_all.size(); i++) {
        Inst* in = g_all[i];
        if (in->strip != strip) continue;
        if (g_pi == in && g_pop) DestroyWindow(g_pop);
        SetWindowLongPtrW(strip, GWLP_USERDATA, 0);
        DestroyWindow(strip);
        if (g_in == in) g_in = nullptr;
        g_all.erase(g_all.begin() + (long)i);
        delete in;
        return;
    }
}

void changed(browser::Browser& b)
{
    for (Inst* in : g_all) {
        if (in->b != &b) continue;
        InvalidateRect(in->strip, nullptr, FALSE);
        if (g_pop && g_pi == in && !g_dragging) {   // a change from elsewhere moves the thumb (4074's _SetThumbPointToView)
            g_thumb = thumb_y(browser::current_view(b));
            InvalidateRect(g_pop, nullptr, FALSE);
        }
    }
}

}
