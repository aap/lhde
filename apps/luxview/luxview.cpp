// luxview: lhde's picture viewer (docs/USAGE.md "luxview"), an application on the public entry point (wux/wux.h,
// shell/shell.h, apps/app.mk; docs/APPLICATIONS.md).
//
//   luxview [FILE | FOLDER]...
//
// Under XP's styles (and classic) XP's Windows Picture and Fax Viewer: the picture on the window colour, the toolbar of
// small buttons centred along the bottom (Previous, Next | Best Fit, Actual Size, Start Slide Show | Zoom In, Zoom Out |
// Rotate Clockwise, Rotate Counterclockwise | Delete, Print, Copy To, Edit | Help), the picture's name in the title. Under
// a Longhorn build's theme 4051's Windows Picture Viewer: black, the picture in a dark box with a grey 1 px frame, the
// rotate and file buttons along the top with Slide Show, Properties and Help on the right, the zoom buttons and Previous
// / Next under the box, the filmstrip of the folder's pictures below. The glyphs are shimgvw.dll's toolbar strips (XP's,
// 4051's, ReactOS's single bitmaps) when the import has them, else drawn here.
//
// The window, its toolbar and its title are up before anything is decoded; the picture follows from the view model's
// loader thread (model.h): the cached thumbnail at once, the decode at the window's size after. What it decodes, keeps
// and throws away is the model's; this file draws, takes input and talks to the shell layer.
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "wux/wux.h"
#include "shell/shell.h"
#include "lhde/settings.h"
#include "gdi/image.h"
#include "model.h"

using luxview::Viewer;

namespace {

enum {
    C_PREV = 1, C_NEXT, C_FIT, C_ACTUAL, C_SLIDE, C_ZOOMIN, C_ZOOMOUT, C_ROTCW, C_ROTCCW, C_DELETE, C_PRINT, C_COPYTO,
    C_EDIT, C_HELP, C_PROPS, C_FILMSTRIP, C_SS_PLAY, C_SS_PAUSE, C_SS_CLOSE,
};
// the glyphs, cells of the strip luxview builds (whatever the source)
enum { G_PREV, G_NEXT, G_ZOOMIN, G_ZOOMOUT, G_FIT, G_ACTUAL, G_SLIDE, G_ROTCCW, G_ROTCW, G_PRINT, G_PROPS, G_SAVE, G_HELP,
       G_DELETE, G_EDIT, G_FILMSTRIP, G_SS_PLAY, G_SS_PAUSE, G_SS_CLOSE, G_COUNT };
// the cells of shimgvw.dll's toolbar strip (bitmap 105: XP's 27 cells, 4051's 34) and its slide show strip (109: play,
// pause, previous, next, close)
const int STRIP_CELL[G_FILMSTRIP + 1] = { 0, 1, 2, 3, 5, 6, 8, 11, 12, 20, 21, 22, 24, 25, 26, 33 };
const int SS_CELL[3] = { 0, 1, 4 };
// ReactOS's shimgvw.dll: one 16 x 16 bitmap a button, white for transparent (0: none)
const int REACTOS_ID[G_COUNT] = { 50, 51, 55, 56, 52, 53, 54, 58, 57, 62, 0, 63, 65, 61, 64, 0, 0, 0, 0 };

const UINT WM_LOADED = WM_APP + 1, WM_THUMBS = WM_APP + 2;
const UINT_PTR T_SLIDE = 1, T_SSBAR = 2;
const int SLIDE_MS = 5000, SSBAR_MS = 3000;

struct Button {
    int cmd = 0, glyph = 0;
    const char* tip = "";
    const char* text = nullptr;        // a labelled button (4051's Slide Show, Properties)
    RECT rc = {};
    bool enabled = true, visible = true;
};

struct App {
    HWND wnd = nullptr, tips = nullptr;
    Viewer* view = nullptr;
    bool longhorn = false;              // 4051's look
    bool slideshow = false, paused = false, ssbar = false;
    bool filmstrip = true;
    int cx = 0, cy = 0;
    RECT area = {};                     // where the picture goes (inside 4051's box)
    RECT box = {};                      // 4051's box (its frame included)
    std::vector<Button> buttons;
    int hot = -1, pressed = -1;
    bool tracking = false;
    // the back buffer
    HDC mem = nullptr;
    HBITMAP back = nullptr;
    uint32_t* bits = nullptr;
    int bw = 0, bh = 0;                 // its size
    // the picture as drawn: resampled to the drawn size when that is smaller than the bitmap (or a preview blown up)
    gdi::Bitmap* disp = nullptr;
    const gdi::Bitmap* disp_from = nullptr;
    int disp_serial = -1, serial = 0;
    double pan_x = 0.5, pan_y = 0.5;    // the picture's point at the area's centre, as fractions of it
    bool dragging = false;
    POINT drag_at = {};
    double drag_px = 0, drag_py = 0;
    int wheel = 0;
    // glyphs: a premultiplied 32 bpp strip of G_COUNT 16 px cells, and its greyed copy
    HBITMAP strip = nullptr, grey = nullptr;
    std::string strip_src;
    HTHEME toolbar_theme = nullptr;
    HFONT font = nullptr;
    // the filmstrip's thumbnails (4051)
    shell::thumbs::Loader* thumbs = nullptr;
    uint32_t thumbs_gen = 0;
    std::map<std::string, gdi::Bitmap*> thumb;
    std::map<std::string, bool> thumb_asked;
    std::map<std::string, gdi::Bitmap*> standin;   // the cells' stand-ins: the file type's icon at 32 px, by icon key
    size_t strip_first = 0;
    std::string title;
    bool trace = false;
    // before the slide show
    bool was_fit = true;
    double was_zoom = 1;
};
App g;

void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void trace(const char* fmt, ...)
{
    if (!g.trace) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

long rss_kb()
{
    FILE* f = fopen("/proc/self/statm", "r");
    long pages = 0, res = 0;
    if (f) { if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0; fclose(f); }
    return res * (sysconf(_SC_PAGESIZE) / 1024);
}

std::string base_of(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }

// ---- glyphs ------------------------------------------------------------------------------------------------------------

HBITMAP new_dib(int w, int h, uint32_t** bits)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* p = nullptr;
    HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
    *bits = (uint32_t*)p;
    return b;
}

// a bitmap's pixels, 32 bpp top-down; false when it cannot be read
bool read_bits(HBITMAP b, int* w, int* h, std::vector<uint32_t>& px)
{
    BITMAP bm = {};
    if (!b || !GetObjectW(b, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    px.assign((size_t)bm.bmWidth * bm.bmHeight, 0);
    HDC dc = GetDC(nullptr);
    int n = GetDIBits(dc, b, 0, bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    *w = bm.bmWidth; *h = bm.bmHeight;
    return n > 0;
}

// one 16 x 16 cell of a source into the strip at cell dst: straight alpha (all zero: opaque, white keyed when asked)
void put_cell(uint32_t* strip, int sw, int dst, const std::vector<uint32_t>& px, int pw, int cell, bool white_key)
{
    bool any_alpha = false;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (px[(size_t)y * pw + cell * 16 + x] >> 24) any_alpha = true;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            uint32_t p = px[(size_t)y * pw + cell * 16 + x];
            uint32_t a = any_alpha ? p >> 24 : 255;
            if (!any_alpha && (p & 0xffffff) == (white_key ? 0xffffffu : 0x00ff00u)) a = 0;   // 8 bpp strips: green
            uint32_t r = (p >> 16 & 0xff) * a / 255, gg = (p >> 8 & 0xff) * a / 255, b = (p & 0xff) * a / 255;
            strip[(size_t)y * sw + dst * 16 + x] = a << 24 | r << 16 | gg << 8 | b;
        }
}

// the glyphs drawn when no shimgvw.dll is imported: plain shapes in XP's colours on a key colour made transparent
void draw_glyph(HDC dc, int i, int ox)
{
    auto pen = [&](COLORREF c, int w) { return SelectObject(dc, CreatePen(PS_SOLID, w, c)); };
    auto brush = [&](COLORREF c) { return SelectObject(dc, CreateSolidBrush(c)); };
    auto done = [&](HGDIOBJ op, HGDIOBJ ob) { DeleteObject(SelectObject(dc, op)); if (ob) DeleteObject(SelectObject(dc, ob)); };
    const COLORREF blue = RGB(33, 93, 214), dark = RGB(10, 50, 140), white = RGB(255, 255, 255);
    HGDIOBJ op, ob;
    switch (i) {
    case G_PREV: case G_NEXT: case G_SS_PLAY: case G_SS_PAUSE: case G_SS_CLOSE: {
        COLORREF fill = i == G_SS_PLAY ? RGB(40, 150, 40) : i == G_SS_CLOSE ? RGB(220, 40, 30) : blue;
        op = pen(dark, 1); ob = brush(fill);
        Ellipse(dc, ox + 0, 0, ox + 16, 16);
        done(op, ob);
        op = pen(white, 1); ob = brush(white);
        if (i == G_PREV) { POINT t[3] = { { ox + 10, 4 }, { ox + 10, 11 }, { ox + 6, 7 } }; Polygon(dc, t, 3); Rectangle(dc, ox + 4, 4, ox + 6, 12); }
        else if (i == G_NEXT) { POINT t[3] = { { ox + 5, 4 }, { ox + 5, 11 }, { ox + 9, 7 } }; Polygon(dc, t, 3); Rectangle(dc, ox + 10, 4, ox + 12, 12); }
        else if (i == G_SS_PLAY) { POINT t[3] = { { ox + 6, 4 }, { ox + 6, 12 }, { ox + 11, 8 } }; Polygon(dc, t, 3); }
        else if (i == G_SS_PAUSE) { Rectangle(dc, ox + 5, 4, ox + 7, 12); Rectangle(dc, ox + 9, 4, ox + 11, 12); }
        else { done(op, ob); op = pen(white, 2); ob = nullptr; MoveToEx(dc, ox + 5, 5, nullptr); LineTo(dc, ox + 11, 11); MoveToEx(dc, ox + 11, 5, nullptr); LineTo(dc, ox + 5, 11); }
        done(op, ob);
        break;
    }
    case G_ZOOMIN: case G_ZOOMOUT:
        op = pen(RGB(200, 140, 30), 3); ob = nullptr;
        MoveToEx(dc, ox + 3, 13, nullptr); LineTo(dc, ox + 7, 9);
        done(op, ob);
        op = pen(dark, 1); ob = brush(RGB(220, 235, 255));
        Ellipse(dc, ox + 5, 1, ox + 15, 11);
        done(op, ob);
        op = pen(blue, 1); ob = nullptr;
        MoveToEx(dc, ox + 7, 6, nullptr); LineTo(dc, ox + 13, 6);
        if (i == G_ZOOMIN) { MoveToEx(dc, ox + 10, 3, nullptr); LineTo(dc, ox + 10, 9); }
        done(op, ob);
        break;
    case G_FIT: case G_ACTUAL: case G_EDIT:
        op = pen(dark, 1); ob = brush(RGB(120, 180, 240));
        Rectangle(dc, ox + 2, 3, ox + 14, 13);
        done(op, ob);
        op = pen(RGB(40, 140, 40), 1); ob = brush(RGB(60, 170, 60));
        { POINT m[4] = { { ox + 3, 12 }, { ox + 7, 7 }, { ox + 10, 10 }, { ox + 13, 12 } }; Polygon(dc, m, 4); }
        done(op, ob);
        op = pen(blue, 1); ob = nullptr;
        if (i == G_FIT) {
            MoveToEx(dc, ox + 0, 3, nullptr); LineTo(dc, ox + 0, 0); LineTo(dc, ox + 4, 0);
            MoveToEx(dc, ox + 12, 0, nullptr); LineTo(dc, ox + 15, 0); LineTo(dc, ox + 15, 4);
            MoveToEx(dc, ox + 0, 12, nullptr); LineTo(dc, ox + 0, 15); LineTo(dc, ox + 4, 15);
            MoveToEx(dc, ox + 12, 15, nullptr); LineTo(dc, ox + 15, 15); LineTo(dc, ox + 15, 11);
        } else if (i == G_EDIT) {
            done(op, ob); op = pen(RGB(230, 160, 30), 2); ob = nullptr;
            MoveToEx(dc, ox + 8, 12, nullptr); LineTo(dc, ox + 15, 2);
        }
        done(op, ob);
        break;
    case G_SLIDE:
        op = pen(RGB(90, 90, 90), 1); ob = brush(RGB(235, 235, 245));
        Rectangle(dc, ox + 2, 1, ox + 15, 10);
        done(op, ob);
        op = pen(RGB(90, 90, 90), 1); ob = nullptr;
        MoveToEx(dc, ox + 8, 10, nullptr); LineTo(dc, ox + 8, 14);
        MoveToEx(dc, ox + 8, 12, nullptr); LineTo(dc, ox + 4, 15);
        MoveToEx(dc, ox + 8, 12, nullptr); LineTo(dc, ox + 13, 15);
        done(op, ob);
        break;
    case G_ROTCW: case G_ROTCCW: {
        op = pen(RGB(30, 120, 30), 1); ob = brush(RGB(40, 150, 40));
        POINT m[3] = { { ox + 1, 15 }, { ox + 5, 8 }, { ox + 9, 15 } };
        POINT n[3] = { { ox + 7, 15 }, { ox + 11, 8 }, { ox + 15, 15 } };
        Polygon(dc, m, 3); Polygon(dc, n, 3);
        done(op, ob);
        op = pen(blue, 2); ob = nullptr;
        bool cw = i == G_ROTCW;
        int a = cw ? 4 : 12, b = cw ? 12 : 4;
        MoveToEx(dc, ox + a, 8, nullptr); LineTo(dc, ox + a, 4); LineTo(dc, ox + b, 4);
        done(op, ob);
        op = pen(blue, 1); ob = brush(blue);
        { POINT t[3] = { { ox + b, 1 }, { ox + b, 7 }, { ox + b + (cw ? 3 : -3), 4 } }; Polygon(dc, t, 3); }
        done(op, ob);
        break;
    }
    case G_PRINT:
        op = pen(RGB(90, 90, 90), 1); ob = brush(RGB(250, 250, 250));
        Rectangle(dc, ox + 4, 1, ox + 12, 7);
        done(op, ob);
        op = pen(RGB(90, 90, 90), 1); ob = brush(RGB(190, 190, 190));
        Rectangle(dc, ox + 1, 6, ox + 15, 13);
        done(op, ob);
        break;
    case G_PROPS:
        op = pen(RGB(90, 110, 160), 1); ob = brush(white);
        Rectangle(dc, ox + 3, 0, ox + 13, 16);
        done(op, ob);
        op = pen(RGB(220, 30, 30), 2); ob = nullptr;
        MoveToEx(dc, ox + 5, 8, nullptr); LineTo(dc, ox + 7, 11); LineTo(dc, ox + 11, 4);
        done(op, ob);
        break;
    case G_SAVE:
        op = pen(dark, 1); ob = brush(RGB(90, 110, 200));
        Rectangle(dc, ox + 1, 1, ox + 15, 15);
        done(op, ob);
        op = pen(white, 1); ob = brush(white);
        Rectangle(dc, ox + 4, 2, ox + 12, 7);
        done(op, ob);
        op = pen(RGB(60, 60, 60), 1); ob = brush(RGB(200, 200, 200));
        Rectangle(dc, ox + 5, 10, ox + 11, 15);
        done(op, ob);
        break;
    case G_HELP: {
        op = pen(dark, 1); ob = brush(blue);
        Ellipse(dc, ox + 0, 0, ox + 16, 16);
        done(op, ob);
        HFONT f = CreateFontW(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma");
        HGDIOBJ of = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, white);
        RECT r = { ox, 0, ox + 16, 16 };
        DrawTextW(dc, L"?", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(SelectObject(dc, of));
        break;
    }
    case G_DELETE:
        op = pen(RGB(220, 30, 30), 3); ob = nullptr;
        MoveToEx(dc, ox + 3, 3, nullptr); LineTo(dc, ox + 13, 13);
        MoveToEx(dc, ox + 13, 3, nullptr); LineTo(dc, ox + 3, 13);
        done(op, ob);
        break;
    case G_FILMSTRIP:
        op = pen(dark, 1); ob = brush(RGB(60, 100, 200));
        Rectangle(dc, ox + 1, 2, ox + 15, 12);
        done(op, ob);
        for (int x = 1; x < 15; x += 3) SetPixel(dc, ox + x, 14, RGB(60, 100, 200));
        break;
    }
}

// the source of the glyphs: shimgvw.dll of the theme's build, 4051's under a Longhorn theme, XP's, ReactOS's
std::string shimgvw_for(bool longhorn, bool* reactos)
{
    *reactos = false;
    std::vector<std::string> builds;
    if (longhorn) {
        std::string b = shell::theme_build(shell::session_theme());
        if (!b.empty() && b != "4051") builds.push_back(b);
        builds.push_back("4051");
    }
    builds.push_back("xp");
    for (const std::string& b : builds) {
        std::string f = shell::resource_file(b, "shimgvw.dll");
        if (!f.empty()) return f;
    }
    std::string f = shell::resource_file("reactos", "shimgvw.dll");
    if (!f.empty()) *reactos = true;
    return f;
}

void load_glyphs()
{
    bool reactos = false;
    std::string dll = shimgvw_for(g.longhorn, &reactos);
    if (g.strip && dll == g.strip_src) return;
    if (g.strip) DeleteObject(g.strip);
    if (g.grey) DeleteObject(g.grey);
    g.strip_src = dll;
    const int sw = G_COUNT * 16;
    uint32_t* px = nullptr;
    g.strip = new_dib(sw, 16, &px);
    std::vector<bool> have(G_COUNT, false);
    if (!dll.empty() && !reactos) {
        int w = 0, h = 0;
        std::vector<uint32_t> src;
        HBITMAP b = (HBITMAP)shell::load_image("pe:" + dll + "#105", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        if (b && read_bits(b, &w, &h, src) && h >= 16)
            for (int i = 0; i <= G_FILMSTRIP; i++)
                if ((STRIP_CELL[i] + 1) * 16 <= w) { put_cell(px, sw, i, src, w, STRIP_CELL[i], false); have[i] = true; }
        if (b) DeleteObject(b);
        b = (HBITMAP)shell::load_image("pe:" + dll + "#109", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        if (b && read_bits(b, &w, &h, src) && w >= 80 && h >= 16)
            for (int i = 0; i < 3; i++) { put_cell(px, sw, G_SS_PLAY + i, src, w, SS_CELL[i], false); have[G_SS_PLAY + i] = true; }
        if (b) DeleteObject(b);
    } else if (reactos) {
        for (int i = 0; i < G_COUNT; i++) {
            if (!REACTOS_ID[i]) continue;
            int w = 0, h = 0;
            std::vector<uint32_t> src;
            HBITMAP b = (HBITMAP)shell::load_image("pe:" + dll + "#" + std::to_string(REACTOS_ID[i]), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            if (b && read_bits(b, &w, &h, src) && w >= 16 && h >= 16) { put_cell(px, sw, i, src, w, 0, true); have[i] = true; }
            if (b) DeleteObject(b);
        }
    }
    // what no source had: drawn on magenta, made transparent
    uint32_t* kp = nullptr;
    HBITMAP key = new_dib(16, 16, &kp);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, key);
    for (int i = 0; i < G_COUNT; i++) {
        if (have[i]) continue;
        HBRUSH m = CreateSolidBrush(RGB(255, 0, 255));
        RECT r = { 0, 0, 16, 16 };
        FillRect(dc, &r, m);
        DeleteObject(m);
        draw_glyph(dc, i, 0);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                uint32_t p = kp[y * 16 + x] & 0xffffff;
                px[(size_t)y * sw + i * 16 + x] = p == 0xff00ff ? 0 : (0xff000000u | p);
            }
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(key);
    // the greyed strip: grey from the luminance, half the alpha (a disabled toolbar button)
    uint32_t* gp = nullptr;
    g.grey = new_dib(sw, 16, &gp);
    for (int i = 0; i < sw * 16; i++) {
        uint32_t p = px[i], a = p >> 24;
        if (!a) { gp[i] = 0; continue; }
        uint32_t l = ((p >> 16 & 0xff) * 30 + (p >> 8 & 0xff) * 59 + (p & 0xff) * 11) / 100;   // premultiplied already
        l = (l + a) / 2;                                                                   // lighter
        if (l > a) l = a;
        uint32_t na = a / 2;
        l = l * na / (a ? a : 1);
        gp[i] = na << 24 | l << 16 | l << 8 | l;
    }
    trace("glyphs %s", dll.empty() ? "drawn" : dll.c_str());
}

void draw_glyph_cell(HDC dc, int glyph, int x, int y, bool enabled)
{
    HDC s = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(s, enabled ? g.strip : g.grey);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x, y, 16, 16, s, glyph * 16, 0, 16, 16, bf);
    SelectObject(s, old);
    DeleteDC(s);
}

// ---- layout ------------------------------------------------------------------------------------------------------------

// XP's toolbar: buttons of 23 x 22 (comctl32's for 16 px images), separators 8 wide, in a band along the bottom; the band's
// height and the margin around the picture are luxview's choice, not XP's sizes
const int XP_BTN_W = 23, XP_BTN_H = 22, XP_SEP = 8, XP_BAND = 34, XP_MARGIN = 8;
// 4051's: the top row's glyphs at y 12, x 45 + 25 a button, 8 more between groups; the box from (40, 40)
// to 40 from the right and 141 from the bottom (its 1 px frame 128 grey, inside 31 grey); the zoom row at 128 from the
// bottom; the filmstrip's cells 62 x 76 from x 2, at 97 from the bottom. Without the filmstrip the box reaches down to 44 from
// the bottom (luxview's choice)
const int LH_TOP_Y = 12, LH_X0 = 45, LH_STEP = 25, LH_GAP = 8, LH_BOX = 40, LH_BOX_BOTTOM = 141, LH_ROW = 128;
const int LH_STRIP = 97, LH_CELL_W = 62, LH_CELL_H = 76, LH_CELL_X = 2, LH_NOSTRIP = 44;
const COLORREF LH_FRAME = RGB(128, 128, 128), LH_INSIDE = RGB(31, 31, 31), LH_SEL_BORDER = RGB(124, 88, 49),
               LH_SEL_LIGHT = RGB(200, 195, 180), LH_SEL_LIGHT_B = RGB(200, 183, 159), LH_SEL_TOP = RGB(157, 147, 117),
               LH_SEL_BOTTOM = RGB(157, 122, 75);

Button* button(int cmd)
{
    for (Button& b : g.buttons) if (b.cmd == cmd) return &b;
    return nullptr;
}

void add(int cmd, int glyph, const char* tip, const char* text = nullptr)
{
    Button b;
    b.cmd = cmd; b.glyph = glyph; b.tip = tip; b.text = text;
    g.buttons.push_back(b);
}

void make_buttons()
{
    g.buttons.clear();
    if (g.longhorn) {
        add(C_ROTCW, G_ROTCW, "Rotate Clockwise (Ctrl+K)");
        add(C_ROTCCW, G_ROTCCW, "Rotate Counterclockwise (Ctrl+L)");
        add(C_PRINT, G_PRINT, "Print (Ctrl+P)");
        add(C_COPYTO, G_SAVE, "Copy To (Ctrl+S)");
        add(C_EDIT, G_EDIT, "Closes this program and opens the image for editing (Ctrl+E)");
        add(C_DELETE, G_DELETE, "Delete (Delete)");
        add(C_SLIDE, G_SLIDE, "Start Slide Show (F11)", "Slide Show");
        add(C_PROPS, G_PROPS, "Properties (Alt+Enter)", "Properties");
        add(C_HELP, G_HELP, "Help (F1)");
        add(C_FIT, G_FIT, "Best Fit (Ctrl+B)");
        add(C_ACTUAL, G_ACTUAL, "Actual Size (Ctrl+A)");
        add(C_ZOOMIN, G_ZOOMIN, "Zoom In (+)");
        add(C_ZOOMOUT, G_ZOOMOUT, "Zoom Out (-)");
        add(C_FILMSTRIP, G_FILMSTRIP, "Filmstrip");
        add(C_PREV, G_PREV, "Previous Image (Left Arrow)");
        add(C_NEXT, G_NEXT, "Next Image (Right Arrow)");
    } else {
        add(C_PREV, G_PREV, "Previous Image (Left Arrow)");
        add(C_NEXT, G_NEXT, "Next Image (Right Arrow)");
        add(0, 0, nullptr);
        add(C_FIT, G_FIT, "Best Fit (Ctrl+B)");
        add(C_ACTUAL, G_ACTUAL, "Actual Size (Ctrl+A)");
        add(C_SLIDE, G_SLIDE, "Start Slide Show (F11)");
        add(0, 0, nullptr);
        add(C_ZOOMIN, G_ZOOMIN, "Zoom In (+)");
        add(C_ZOOMOUT, G_ZOOMOUT, "Zoom Out (-)");
        add(0, 0, nullptr);
        add(C_ROTCW, G_ROTCW, "Rotate Clockwise (Ctrl+K)");
        add(C_ROTCCW, G_ROTCCW, "Rotate Counterclockwise (Ctrl+L)");
        add(0, 0, nullptr);
        add(C_DELETE, G_DELETE, "Delete (Delete)");
        add(C_PRINT, G_PRINT, "Print (Ctrl+P)");
        add(C_COPYTO, G_SAVE, "Copy To (Ctrl+S)");
        add(C_EDIT, G_EDIT, "Closes this program and opens the image for editing (Ctrl+E)");
        add(0, 0, nullptr);
        add(C_HELP, G_HELP, "Help (F1)");
    }
    // the slide show's own buttons, top right while the pointer moves
    add(C_SS_PLAY, G_SS_PLAY, "Start Slide Show");
    add(C_SS_PAUSE, G_SS_PAUSE, "Pause Slide Show");
    add(C_PREV, G_PREV, "Previous Picture");
    add(C_NEXT, G_NEXT, "Next Picture");
    add(C_SS_CLOSE, G_SS_CLOSE, "Close the window");
}

int text_width(const char* s)
{
    HDC dc = GetDC(g.wnd);
    HGDIOBJ of = SelectObject(dc, g.font);
    std::u16string w = WuxWide(s);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
    SelectObject(dc, of);
    ReleaseDC(g.wnd, dc);
    return sz.cx;
}

void layout()
{
    RECT cr;
    GetClientRect(g.wnd, &cr);
    g.cx = cr.right; g.cy = cr.bottom;
    size_t n = g.buttons.size();
    for (size_t i = 0; i < n; i++) g.buttons[i].visible = false;
    if (g.slideshow) {
        g.area = cr;
        g.box = cr;
        int x = g.cx - 5 * XP_BTN_W - 6;
        for (size_t i = n - 5; i < n; i++) {
            Button& b = g.buttons[i];
            b.rc = { x, 6, x + XP_BTN_W, 6 + XP_BTN_H };
            b.visible = g.ssbar;
            x += XP_BTN_W;
        }
    } else if (g.longhorn) {
        int bottom = g.filmstrip ? LH_BOX_BOTTOM : LH_NOSTRIP + LH_ROW - LH_STRIP;
        g.box = { LH_BOX, LH_BOX, std::max(LH_BOX + 2, g.cx - LH_BOX), std::max(LH_BOX + 2, g.cy - bottom + 1) };
        g.area = { g.box.left + 1, g.box.top + 1, g.box.right - 1, g.box.bottom - 1 };
        int x = LH_X0, k = 0;
        auto place = [&](int cmd, int px, int py) {
            Button* b = button(cmd);
            if (!b) return;
            b->rc = { px - 3, py - 3, px + 19, py + 19 };
            b->visible = true;
        };
        for (int cmd : { C_ROTCW, C_ROTCCW, C_PRINT, C_COPYTO, C_EDIT, C_DELETE }) {
            if (k == 2) x += LH_GAP;
            place(cmd, x, LH_TOP_Y);
            x += LH_STEP; k++;
        }
        // right to left: Help, then the labelled ones, their text 4 px after the glyph, 13 px before the next glyph
        int hx = g.cx - 60;
        place(C_HELP, hx, LH_TOP_Y);
        int right = hx - 13;
        for (int cmd : { C_PROPS, C_SLIDE }) {
            Button* b = button(cmd);
            int tw = text_width(b->text);
            int gx = right - tw - 20;
            b->rc = { gx - 3, LH_TOP_Y - 3, right + 3, LH_TOP_Y + 19 };
            b->visible = true;
            right = gx - 13;
        }
        int ry = g.box.bottom - 1 + (LH_BOX_BOTTOM - LH_ROW);
        x = LH_X0;
        for (int cmd : { C_FIT, C_ACTUAL, C_ZOOMIN, C_ZOOMOUT, C_FILMSTRIP }) { place(cmd, x, ry); x += LH_STEP; }
        place(C_PREV, g.cx / 2 - 20, ry);
        place(C_NEXT, g.cx / 2 + 5, ry);
    } else {
        int total = 0;
        for (size_t i = 0; i + 5 < n; i++) total += g.buttons[i].cmd ? XP_BTN_W : XP_SEP;
        int x = (g.cx - total) / 2, y = g.cy - XP_BAND + (XP_BAND - XP_BTN_H) / 2;
        for (size_t i = 0; i + 5 < n; i++) {
            Button& b = g.buttons[i];
            int w = b.cmd ? XP_BTN_W : XP_SEP;
            b.rc = { x, y, x + w, y + XP_BTN_H };
            b.visible = b.cmd != 0;
            x += w;
        }
        g.area = { XP_MARGIN, XP_MARGIN, std::max(XP_MARGIN + 1, g.cx - XP_MARGIN), std::max(XP_MARGIN + 1, g.cy - XP_BAND - XP_MARGIN) };
        g.box = g.area;
    }
    g.view->set_viewport(g.area.right - g.area.left, g.area.bottom - g.area.top);
    // the tooltips' rectangles
    if (g.tips) {
        for (size_t i = 0; i < n; i++) {
            TTTOOLINFOW ti = {};
            ti.cbSize = sizeof ti;
            ti.hwnd = g.wnd;
            ti.uId = i + 1;
            ti.rect = g.buttons[i].visible ? g.buttons[i].rc : RECT{ -10, -10, -9, -9 };
            SendMessageW(g.tips, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
        }
    }
}

void make_tips()
{
    if (g.tips) DestroyWindow(g.tips);
    g.tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                             g.wnd, nullptr, nullptr, nullptr);
    for (size_t i = 0; i < g.buttons.size(); i++) {
        if (!g.buttons[i].cmd) continue;
        static std::vector<std::u16string> texts;
        if (texts.size() < g.buttons.size() * 2) texts.resize(g.buttons.size() * 2);
        texts[i] = WuxWide(g.buttons[i].tip);
        TTTOOLINFOW ti = {};
        ti.cbSize = sizeof ti;
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = g.wnd;
        ti.uId = i + 1;
        ti.rect = RECT{ -10, -10, -9, -9 };
        ti.lpszText = (LPWSTR)texts[i].c_str();
        SendMessageW(g.tips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

// ---- state -------------------------------------------------------------------------------------------------------------

std::string state_path()
{
    char buf[4096];
    return lhde_state_file("luxview.ini", buf, sizeof buf) ? buf : "";
}

int state_int(lhde_ini* ini, const char* sec, const char* key, int def)
{
    char buf[64];
    if (!ini || !lhde_ini_get(ini, sec, key, buf, sizeof buf) || !*buf) return def;
    return atoi(buf);
}

void save_state()
{
    if (g.slideshow) return;
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof wp;
    GetWindowPlacement(g.wnd, &wp);
    std::string p = state_path();
    if (p.empty()) return;
    lhde_ini* s = lhde_ini_load(p.c_str());
    if (!s) s = lhde_ini_new();
    if (!s) return;
    lhde_ini_header(s, "what luxview remembers (docs/USAGE.md \"luxview\"): where its window was, the filmstrip");
    auto put = [&](const char* sec, const char* k, long v) { char b[32]; snprintf(b, sizeof b, "%ld", v); lhde_ini_set(s, sec, k, b); };
    put("window", "x", wp.rcNormalPosition.left);
    put("window", "y", wp.rcNormalPosition.top);
    put("window", "width", wp.rcNormalPosition.right - wp.rcNormalPosition.left);
    put("window", "height", wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
    put("window", "maximized", wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED) ? 1 : 0);
    put("view", "filmstrip", g.filmstrip ? 1 : 0);
    lhde_ini_save(s, p.c_str());
    lhde_ini_free(s);
}

// ---- the picture -------------------------------------------------------------------------------------------------------

void drop_disp()
{
    if (g.disp) gdi::destroy_bitmap(g.disp);
    g.disp = nullptr;
    g.disp_from = nullptr;
}

// the drawn size of the picture
void drawn_size(int* dw, int* dh)
{
    const gdi::Bitmap* b = g.view->bitmap();
    int pw = g.view->picture_w(), ph = g.view->picture_h();
    if (pw <= 0 || ph <= 0) { pw = b ? b->width : 1; ph = b ? b->height : 1; }
    double z = g.view->zoom();
    *dw = std::max(1, (int)lround(pw * z));
    *dh = std::max(1, (int)lround(ph * z));
}

// the drawn rectangle's top left: centred when it fits the area, else at the pan point
void drawn_origin(int dw, int dh, int* x, int* y)
{
    int aw = g.area.right - g.area.left, ah = g.area.bottom - g.area.top;
    if (dw <= aw) *x = g.area.left + (aw - dw) / 2;
    else {
        g.pan_x = std::max((double)aw / 2 / dw, std::min(1 - (double)aw / 2 / dw, g.pan_x));
        *x = g.area.left + aw / 2 - (int)lround(g.pan_x * dw);
    }
    if (dh <= ah) *y = g.area.top + (ah - dh) / 2;
    else {
        g.pan_y = std::max((double)ah / 2 / dh, std::min(1 - (double)ah / 2 / dh, g.pan_y));
        *y = g.area.top + ah / 2 - (int)lround(g.pan_y * dh);
    }
}

bool pannable()
{
    if (!g.view->bitmap()) return false;
    int dw, dh;
    drawn_size(&dw, &dh);
    return dw > g.area.right - g.area.left || dh > g.area.bottom - g.area.top;
}

// src (straight alpha) drawn into the back buffer at (dx, dy) as dw x dh, nearest pixel, clipped to clip, over what is there
void blit(const gdi::Bitmap* s, int dx, int dy, int dw, int dh, RECT clip)
{
    int x0 = std::max<int>({ (int)clip.left, dx, 0 }), x1 = std::min<int>({ (int)clip.right, dx + dw, g.cx });
    int y0 = std::max<int>({ (int)clip.top, dy, 0 }), y1 = std::min<int>({ (int)clip.bottom, dy + dh, g.cy });
    if (x0 >= x1 || y0 >= y1) return;
    std::vector<int> xs((size_t)(x1 - x0));
    for (int x = x0; x < x1; x++) xs[(size_t)(x - x0)] = std::min(s->width - 1, (int)((int64_t)(x - dx) * s->width / dw));
    for (int y = y0; y < y1; y++) {
        int sy = std::min(s->height - 1, (int)((int64_t)(y - dy) * s->height / dh));
        const uint32_t* row = s->row(sy);
        uint32_t* out = g.bits + (size_t)y * g.cx;
        for (int x = x0; x < x1; x++) {
            uint32_t p = row[xs[(size_t)(x - x0)]], a = p >> 24;
            if (a == 255) out[x] = p;
            else if (a) {
                uint32_t d = out[x], na = 255 - a;
                uint32_t r = ((p >> 16 & 0xff) * a + (d >> 16 & 0xff) * na) / 255;
                uint32_t gg = ((p >> 8 & 0xff) * a + (d >> 8 & 0xff) * na) / 255;
                uint32_t b = ((p & 0xff) * a + (d & 0xff) * na) / 255;
                out[x] = 0xff000000u | r << 16 | gg << 8 | b;
            }
        }
    }
}

void draw_picture(HDC dc)
{
    const gdi::Bitmap* b = g.view->bitmap();
    if (!b) {
        // XP's texts while there is no picture
        const char* t = g.view->count() == 0 ? "No file selected." : g.view->failed() ? "No preview available." : "Generating preview...";
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g.longhorn || g.slideshow ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT));
        HGDIOBJ of = SelectObject(dc, g.font);
        std::u16string w = WuxWide(t);
        DrawTextW(dc, (LPCWSTR)w.c_str(), (int)w.size(), &g.area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);
        return;
    }
    int dw, dh, x, y;
    drawn_size(&dw, &dh);
    drawn_origin(dw, dh, &x, &y);
    // smaller than the bitmap, or a thumbnail blown up: resampled once to the drawn size (area average down, bilinear up);
    // a picture magnified past its decode: its pixels
    const gdi::Bitmap* src = b;
    bool resample = (dw < b->width && dh < b->height) || (g.view->is_preview() && (int64_t)dw * dh <= (int64_t)g.cx * g.cy * 2);
    if (resample) {
        if (!g.disp || g.disp_from != b || g.disp_serial != g.serial || g.disp->width != dw || g.disp->height != dh) {
            drop_disp();
            g.disp = gdi::resample(b, dw, dh);
            g.disp_from = b;
            g.disp_serial = g.serial;
        }
        if (g.disp) src = g.disp;
    } else if (g.disp) drop_disp();
    blit(src, x, y, dw, dh, g.area);
    // XP's look: a 1 px frame around the picture in the button shadow colour (luxview's choice of colour)
    if (!g.longhorn && !g.slideshow) {
        RECT f = { std::max<int>(x - 1, g.area.left - 1), std::max<int>(y - 1, g.area.top - 1),
                   std::min<int>(x + dw + 1, g.area.right + 1), std::min<int>(y + dh + 1, g.area.bottom + 1) };
        HBRUSH fb = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        FrameRect(dc, &f, fb);
        DeleteObject(fb);
    }
    static int last_w, last_h;
    if (dw != last_w || dh != last_h) { trace("drawn %dx%d zoom %d%%", dw, dh, (int)lround(g.view->zoom() * 100)); last_w = dw; last_h = dh; }
}

// ---- the filmstrip (4051) ---------------------------------------------------------------------------------------------

void thumbs_notify(void*) { PostMessageW(g.wnd, WM_THUMBS, 0, 0); }

int strip_cells() { return std::max(1, (g.cx - LH_CELL_X) / LH_CELL_W); }

// the strip scrolled so that the current picture's cell shows
void strip_follow()
{
    size_t n = (size_t)strip_cells(), cur = g.view->index();
    if (cur < g.strip_first) g.strip_first = cur;
    if (cur >= g.strip_first + n) g.strip_first = cur - n + 1;
}

// Every cell on screen gets its thumbnail: asked for once per picture (a cell whose thumbnail was dropped asks again), the
// cells as they will be drawn. The strip asks only for what it shows, so a network folder's pictures are read whole past
// thumbnails_network_budget (shell/thumbs.h unbudgeted): the budget would leave every cell after the first few empty.
void ask_thumbs()
{
    if (!g.longhorn || !g.filmstrip || g.slideshow || g.view->count() == 0) return;
    if (g.view->loading()) return;   // the picture first
    if (!g.thumbs) {
        g.thumbs = new shell::thumbs::Loader(thumbs_notify, nullptr, 1);
        g.thumbs->unbudgeted();
        g.thumbs_gen = g.thumbs->generation();
    }
    strip_follow();
    size_t n = (size_t)strip_cells();
    for (size_t i = g.strip_first; i < g.view->count() && i < g.strip_first + n; i++) {
        const std::string& p = g.view->files()[i];
        if (g.thumb_asked.count(p)) continue;
        g.thumb_asked[p] = true;
        g.thumbs->request(g.thumbs_gen, (int)i, p, 0, 46);
    }
}

// past 400 thumbnails kept, those of the cells off screen go (asked for again when their cells show)
void trim_thumbs()
{
    if (g.thumb.size() <= 400) return;
    std::set<std::string> shown;
    size_t n = (size_t)strip_cells();
    for (size_t i = g.strip_first; i < g.view->count() && i < g.strip_first + n; i++) shown.insert(g.view->files()[i]);
    for (auto it = g.thumb.begin(); it != g.thumb.end();) {
        if (shown.count(it->first)) { ++it; continue; }
        if (it->second) gdi::destroy_bitmap(it->second);
        g.thumb_asked.erase(it->first);
        it = g.thumb.erase(it);
    }
}

// 4051's selected cell: a brown 1 px border with corners of radius 4, a light ring inside it, a fill from tan to brown, the
// ring following the fill; the corners' pixels over black, mirrored for the right side
const uint8_t SEL_TL[4][4][3] = {
    { { 0, 0, 0 }, { 13, 9, 5 }, { 64, 45, 25 }, { 111, 78, 43 } },
    { { 15, 11, 6 }, { 127, 91, 53 }, { 175, 156, 133 }, { 196, 188, 172 } },
    { { 65, 46, 26 }, { 176, 157, 134 }, { 181, 173, 152 }, { 162, 152, 124 } },
    { { 114, 80, 44 }, { 197, 190, 174 }, { 161, 150, 122 }, { 157, 146, 116 } } };
const uint8_t SEL_BL[4][4][3] = {
    { { 114, 80, 44 }, { 197, 179, 155 }, { 161, 126, 82 }, { 157, 122, 75 } },
    { { 65, 46, 26 }, { 176, 153, 128 }, { 181, 156, 122 }, { 161, 126, 82 } },
    { { 15, 11, 6 }, { 127, 91, 53 }, { 177, 155, 131 }, { 197, 179, 155 } },
    { { 0, 0, 0 }, { 18, 13, 7 }, { 69, 49, 27 }, { 112, 79, 44 } } };

void draw_selected_cell(int x0, int y0)
{
    auto mix = [](int a, int b, int t, int n) { return a + (b - a) * t / n; };
    for (int y = 0; y < LH_CELL_H; y++)
        for (int x = 0; x < LH_CELL_W; x++) {
            int px = x0 + x, py = y0 + y;
            if (px < 0 || px >= g.cx || py < 0 || py >= g.cy) continue;
            int mx = x < 4 ? x : LH_CELL_W - 1 - x < 4 ? LH_CELL_W - 1 - x : -1;
            int my = y < 4 ? y : LH_CELL_H - 1 - y < 4 ? y - (LH_CELL_H - 4) : -1;
            int r, gg, b;
            if (mx >= 0 && my >= 0) {
                const uint8_t* c = y < 4 ? SEL_TL[my][mx] : SEL_BL[my][mx];
                r = c[0]; gg = c[1]; b = c[2];
                if (!r && !gg && !b) continue;
            } else if (x == 0 || y == 0 || x == LH_CELL_W - 1 || y == LH_CELL_H - 1) {
                r = GetRValue(LH_SEL_BORDER); gg = GetGValue(LH_SEL_BORDER); b = GetBValue(LH_SEL_BORDER);
            } else if (x == 1 || y == 1 || x == LH_CELL_W - 2 || y == LH_CELL_H - 2) {
                r = mix(GetRValue(LH_SEL_LIGHT), GetRValue(LH_SEL_LIGHT_B), y - 1, LH_CELL_H - 3);
                gg = mix(GetGValue(LH_SEL_LIGHT), GetGValue(LH_SEL_LIGHT_B), y - 1, LH_CELL_H - 3);
                b = mix(GetBValue(LH_SEL_LIGHT), GetBValue(LH_SEL_LIGHT_B), y - 1, LH_CELL_H - 3);
            } else {
                r = mix(GetRValue(LH_SEL_TOP), GetRValue(LH_SEL_BOTTOM), y - 2, LH_CELL_H - 5);
                gg = mix(GetGValue(LH_SEL_TOP), GetGValue(LH_SEL_BOTTOM), y - 2, LH_CELL_H - 5);
                b = mix(GetBValue(LH_SEL_TOP), GetBValue(LH_SEL_BOTTOM), y - 2, LH_CELL_H - 5);
            }
            g.bits[(size_t)py * g.cx + px] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)gg << 8 | (uint32_t)b;
        }
}

// A cell whose thumbnail is not in (or that has none): the frame a 4:3 picture's thumbnail gets, its 3 px of white around
// the thumbnail's box at the cell's centre, the strip's black inside, and the file type's icon at 32 px in the middle, as
// XP's Filmstrip view shows a file's icon in its thumbnail's place until the thumbnail replaces it. The cache's, the
// EXIF and the full thumbnail then take its place in turn.
void fill_bits(int x0, int y0, int x1, int y1, uint32_t c)
{
    for (int y = std::max(y0, 0); y < std::min(y1, g.cy); y++)
        for (int x = std::max(x0, 0); x < std::min(x1, g.cx); x++) g.bits[(size_t)y * g.cx + x] = c;
}

gdi::Bitmap* standin_icon(const std::string& key)
{
    auto it = g.standin.find(key);
    if (it != g.standin.end()) return it->second;
    std::vector<unsigned long> im = shell::key_icon(key, 32);
    if (im.size() < 3 && key != "image") im = shell::key_icon("image", 32);
    gdi::Bitmap* b = nullptr;
    if (im.size() >= 3 && im.size() >= 2 + (size_t)(im[0] * im[1])) {   // { w, h, ARGB with straight alpha... }
        b = gdi::create_bitmap((int)im[0], (int)im[1]);
        for (int y = 0; y < b->height; y++)
            for (int x = 0; x < b->width; x++) b->row(y)[x] = (uint32_t)im[2 + (size_t)y * b->width + x];
    }
    g.standin[key] = b;
    return b;
}

void draw_standin(int cx0, int top, const std::string& path)
{
    int w, h;
    luxview::fit_into(640, 480, 46, 46, &w, &h);   // (a 4:3 picture)
    int tx = cx0 + 31 - w / 2, ty = top + 36 - h / 2;
    fill_bits(tx - 3, ty - 3, tx + w + 3, ty + h + 3, 0xffffffffu);
    fill_bits(tx, ty, tx + w, ty + h, 0xff000000u);
    if (gdi::Bitmap* ic = standin_icon(shell::icon_key(shell::base_of(path), false))) {
        RECT all = { 0, 0, g.cx, g.cy };
        blit(ic, cx0 + 31 - ic->width / 2, top + 36 - ic->height / 2, ic->width, ic->height, all);
    }
}

void draw_filmstrip(HDC dc)
{
    if (!g.longhorn || !g.filmstrip || g.slideshow) return;
    int n = strip_cells();
    size_t cur = g.view->index();
    strip_follow();
    int top = g.cy - LH_STRIP;
    for (int k = 0; k < n && g.strip_first + (size_t)k < g.view->count(); k++) {
        size_t i = g.strip_first + (size_t)k;
        int cx0 = LH_CELL_X + k * LH_CELL_W;
        if (i == cur) draw_selected_cell(cx0, top);
        auto it = g.thumb.find(g.view->files()[i]);
        if (it == g.thumb.end() || !it->second) { draw_standin(cx0, top, g.view->files()[i]); continue; }
        const gdi::Bitmap* t = it->second;
        // the thumbnail fitted into 46 x 46 around the cell's centre (31, 36), in a 3 px white frame
        int tx = cx0 + 31 - t->width / 2, ty = top + 36 - t->height / 2;
        RECT white = { tx - 3, ty - 3, tx + t->width + 3, ty + t->height + 3 };
        FillRect(dc, &white, (HBRUSH)GetStockObject(WHITE_BRUSH));
        RECT all = { 0, 0, g.cx, g.cy };
        blit(t, tx, ty, t->width, t->height, all);
    }
}

// ---- painting ----------------------------------------------------------------------------------------------------------

void make_back();

void paint(HDC wdc)
{
    if (g.cx <= 0 || g.cy <= 0) return;
    if (!g.mem || g.bw != g.cx || g.bh != g.cy) make_back();
    if (!g.back) return;
    HDC dc = g.mem;
    RECT all = { 0, 0, g.cx, g.cy };
    COLORREF field = g.longhorn || g.slideshow ? RGB(0, 0, 0) : GetSysColor(COLOR_WINDOW);
    HBRUSH fb = CreateSolidBrush(field);
    FillRect(dc, &all, fb);
    DeleteObject(fb);
    if (g.longhorn && !g.slideshow) {
        HBRUSH ib = CreateSolidBrush(LH_INSIDE), frb = CreateSolidBrush(LH_FRAME);
        FillRect(dc, &g.area, ib);
        FrameRect(dc, &g.box, frb);
        DeleteObject(ib);
        DeleteObject(frb);
    }
    draw_picture(dc);
    draw_filmstrip(dc);
    // the buttons
    if (!g.toolbar_theme && IsThemeActive()) g.toolbar_theme = OpenThemeData(g.wnd, L"Toolbar");
    HGDIOBJ of = SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    for (size_t i = 0; i < g.buttons.size(); i++) {
        Button& b = g.buttons[i];
        if (!b.visible || !b.cmd) continue;
        bool hot = (int)i == g.hot && b.enabled, down = hot && (int)i == g.pressed;
        if (hot) {
            if (g.toolbar_theme) DrawThemeBackground(g.toolbar_theme, dc, TP_BUTTON, down ? TS_PRESSED : TS_HOT, &b.rc, nullptr);
            else { RECT r = b.rc; DrawEdge(dc, &r, down ? BDR_SUNKENOUTER : BDR_RAISEDINNER, BF_RECT); }
        }
        int gx, gy;
        if (b.text) { gx = b.rc.left + 3; gy = b.rc.top + 3; }
        else { gx = (b.rc.left + b.rc.right) / 2 - 8; gy = (b.rc.top + b.rc.bottom) / 2 - 8; }
        if (down) { gx++; gy++; }
        draw_glyph_cell(dc, b.glyph, gx, gy, b.enabled);
        if (b.text) {
            SetTextColor(dc, b.enabled ? RGB(255, 255, 255) : RGB(128, 128, 128));
            RECT tr = { gx + 20, b.rc.top, b.rc.right, b.rc.bottom };
            std::u16string w = WuxWide(b.text);
            DrawTextW(dc, (LPCWSTR)w.c_str(), (int)w.size(), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    SelectObject(dc, of);
    BitBlt(wdc, 0, 0, g.cx, g.cy, dc, 0, 0, SRCCOPY);
}

void make_back()
{
    if (g.back) { SelectObject(g.mem, GetStockObject(DEFAULT_GUI_FONT)); DeleteObject(g.back); g.back = nullptr; }
    if (!g.mem) g.mem = CreateCompatibleDC(nullptr);
    g.bw = g.bh = 0;
    if (g.cx <= 0 || g.cy <= 0) return;
    g.back = new_dib(g.cx, g.cy, &g.bits);
    if (!g.back) return;
    g.bw = g.cx; g.bh = g.cy;
    SelectObject(g.mem, g.back);
}

// ---- state of the buttons and the title -------------------------------------------------------------------------------

void update()
{
    bool any = g.view->count() > 0;
    double z = g.view->zoom();
    auto en = [&](int cmd, bool on) { for (Button& b : g.buttons) if (b.cmd == cmd) b.enabled = on; };
    en(C_PREV, g.view->count() > 1);
    en(C_NEXT, g.view->count() > 1);
    en(C_FIT, any && !g.view->best_fit());
    en(C_ACTUAL, any && fabs(z - 1) > 1e-6);
    en(C_ZOOMIN, any && z < luxview::ZOOM_STEPS[luxview::ZOOM_STEP_COUNT - 1] / 100.0 - 1e-6);
    en(C_ZOOMOUT, any && z > luxview::ZOOM_STEPS[0] / 100.0 + 1e-6);
    for (int c : { C_ROTCW, C_ROTCCW, C_DELETE, C_COPYTO, C_EDIT, C_PROPS, C_SLIDE }) en(c, any);
    en(C_PRINT, false);   // lhde has no printing
    en(C_SS_PLAY, g.paused);
    en(C_SS_PAUSE, !g.paused);
    std::string t;
    if (!any) t = "luxview";
    else {
        t = base_of(g.view->path());
        if (g.longhorn && g.view->picture_w() > 0) t += " (" + std::to_string((int)lround(z * 100)) + "%)";
        t += " - luxview";
    }
    if (t != g.title) {
        g.title = t;
        SetWindowTextW(g.wnd, (LPCWSTR)WuxWide(t).c_str());
        trace("title %s", t.c_str());
    }
}

void refresh()
{
    update();
    InvalidateRect(g.wnd, nullptr, FALSE);
}

// ---- commands ----------------------------------------------------------------------------------------------------------

void shown()
{
    g.serial++;
    g.pan_x = g.pan_y = 0.5;
    drop_disp();
    const gdi::Bitmap* b = g.view->bitmap();
    trace("shown %s %s %dx%d of %dx%d rss %ld KB", base_of(g.view->path()).c_str(), !b ? "none" : g.view->is_preview() ? "preview" : "picture",
          b ? b->width : 0, b ? b->height : 0, g.view->picture_w(), g.view->picture_h(), rss_kb());
    ask_thumbs();
    refresh();
}

void slide_show(bool on);

void go_to(bool (Viewer::*how)())
{
    if ((g.view->*how)()) { shown(); trace("index %zu of %zu %s", g.view->index() + 1, g.view->count(), base_of(g.view->path()).c_str()); }
    if (g.slideshow && !g.paused) SetTimer(g.wnd, T_SLIDE, SLIDE_MS, nullptr);
}

// a zoom keeping the picture's point at (ax, ay) of the window where it is (the area's centre for the keys and buttons)
void zoom_to(double z, bool fit, int ax = -1, int ay = -1)
{
    if (!g.view->bitmap()) { if (fit) g.view->set_best_fit(); else g.view->set_zoom(z); refresh(); return; }
    int dw, dh, x, y;
    drawn_size(&dw, &dh);
    drawn_origin(dw, dh, &x, &y);
    if (ax < 0) { ax = (g.area.left + g.area.right) / 2; ay = (g.area.top + g.area.bottom) / 2; }
    double fx = std::max(0.0, std::min(1.0, (double)(ax - x) / dw)), fy = std::max(0.0, std::min(1.0, (double)(ay - y) / dh));
    if (fit) g.view->set_best_fit(); else g.view->set_zoom(z);
    int nw, nh;
    drawn_size(&nw, &nh);
    int cxa = (g.area.left + g.area.right) / 2, cya = (g.area.top + g.area.bottom) / 2;
    g.pan_x = fx + (double)(cxa - ax) / nw;
    g.pan_y = fy + (double)(cya - ay) / nh;
    trace("zoom %d%%", (int)lround(g.view->zoom() * 100));
    refresh();
}

void pan_by(int dx, int dy)
{
    int dw, dh;
    drawn_size(&dw, &dh);
    g.pan_x += (double)dx / dw;
    g.pan_y += (double)dy / dh;
    InvalidateRect(g.wnd, nullptr, FALSE);
}

bool copy_file(const std::string& from, const std::string& to, std::string* err)
{
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) { *err = strerror(errno); return false; }
    std::string tmp = to + ".luxview-tmp";
    FILE* out = fopen(tmp.c_str(), "wb");
    if (!out) { *err = strerror(errno); fclose(in); return false; }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, out) != n) { ok = false; *err = strerror(errno); break; }
    if (ferror(in)) { ok = false; *err = strerror(errno); }
    fclose(in);
    if (fclose(out) != 0 && ok) { ok = false; *err = strerror(errno); }
    if (ok && rename(tmp.c_str(), to.c_str()) != 0) { ok = false; *err = strerror(errno); }
    if (!ok) unlink(tmp.c_str());
    return ok;
}

void copy_to()
{
    if (!g.view->count()) return;
    std::string src = g.view->path(), name = base_of(src);
    size_t dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    WCHAR file[4096] = {};
    std::u16string wn = WuxWide(name);
    lstrcpynW(file, (LPCWSTR)wn.c_str(), 4096);
    std::u16string filter = WuxWide((ext.empty() ? std::string("All Files") : ext + " files (*." + ext + ")"));
    filter.push_back(0);
    filter += WuxWide(ext.empty() ? "*.*" : "*." + ext);
    filter.push_back(0);
    filter += WuxWide("All Files (*.*)");
    filter.push_back(0);
    filter += WuxWide("*.*");
    filter.push_back(0);
    std::u16string dir = WuxWide(src.substr(0, src.rfind('/')));
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = (LPCWSTR)filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = 4096;
    ofn.lpstrInitialDir = (LPCWSTR)dir.c_str();
    ofn.lpstrTitle = L"Copy To";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    std::string to = WuxUtf8(file);
    if (to == src) return;
    std::string err;
    if (!copy_file(src, to, &err))
        shell::message_box(g.wnd, "luxview", "Failed to save to the specified location. Make sure the file is not write-protected and there "
                           "is enough space on the drive to save the image. Copy To Aborted.\n\n" + err, MB_OK | MB_ICONEXCLAMATION);
    else trace("copied %s", to.c_str());
}

void delete_current()
{
    if (!g.view->count()) return;
    std::string p = g.view->path();
    if (shell::message_box(g.wnd, "Confirm File Delete", "Are you sure you want to send '" + base_of(p) + "' to the Trash?",
                           MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::string err;
    if (!shell::trash(p, &err)) {
        shell::message_box(g.wnd, "Error Deleting File", "Cannot delete " + base_of(p) + ": " + err, MB_OK | MB_ICONHAND);
        return;
    }
    trace("trashed %s", p.c_str());
    g.thumb.erase(p);
    g.view->remove_current();
    shown();
}

void edit_current()
{
    if (!g.view->count()) return;
    std::string p = g.view->path();
    // the type's programs but this one (Open With's list): the image editor, else the next viewer
    const shell::App* app = nullptr;
    for (const shell::App* a : shell::apps_for(shell::mime_type(p)))
        if (a && a->id != "luxview.desktop") { app = a; break; }
    if (!app) {
        shell::message_box(g.wnd, "luxview", "No other program that opens " + base_of(p) + " is installed.", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string err;
    if (!shell::launch(*app, { p }, &err)) { shell::message_box(g.wnd, "luxview", err, MB_OK | MB_ICONHAND); return; }
    trace("edit %s with %s", p.c_str(), app->id.c_str());
    PostMessageW(g.wnd, WM_CLOSE, 0, 0);   // XP's: closes this program and opens the image for editing
}

void properties(int page)
{
    if (!g.view->count()) return;
    shell::PropertiesOptions o;
    o.page = page;
    trace("properties %s", base_of(g.view->path()).c_str());
    shell::properties(g.wnd, { g.view->path() }, o);
}

void help()
{
    shell::message_box(g.wnd, "luxview Help",
        "Previous / Next: Left / Right (or the wheel), Page Up / Page Down; Home / End: the first and the last picture\n"
        "Best Fit: Ctrl+B    Actual Size: Ctrl+A    Zoom: + and -, Ctrl+wheel; drag or the arrows to move a zoomed picture\n"
        "Rotate: Ctrl+K clockwise, Ctrl+L counterclockwise (in the view only)\n"
        "Slide show: F11 (Escape ends it)    Delete: Delete    Copy To: Ctrl+S    Open for editing: Ctrl+E\n"
        "Properties: Alt+Enter (General), Ctrl+Enter (Metadata: description, tags)", MB_OK | MB_ICONINFORMATION);
}

void command(int cmd)
{
    switch (cmd) {
    case C_PREV: go_to(&Viewer::prev); break;
    case C_NEXT: go_to(&Viewer::next); break;
    case C_FIT: zoom_to(0, true); break;
    case C_ACTUAL: zoom_to(1, false); break;
    case C_ZOOMIN: zoom_to(luxview::zoom_step_in(g.view->zoom()), false); break;
    case C_ZOOMOUT: zoom_to(luxview::zoom_step_out(g.view->zoom()), false); break;
    case C_ROTCW: g.view->rotate(1); g.serial++; drop_disp(); refresh(); trace("rotation %d", g.view->rotation()); break;
    case C_ROTCCW: g.view->rotate(-1); g.serial++; drop_disp(); refresh(); trace("rotation %d", g.view->rotation()); break;
    case C_DELETE: delete_current(); break;
    case C_PRINT: MessageBeep(MB_OK); break;
    case C_COPYTO: copy_to(); break;
    case C_EDIT: edit_current(); break;
    case C_HELP: help(); break;
    case C_PROPS: properties(0); break;
    case C_SLIDE: slide_show(!g.slideshow); break;
    case C_FILMSTRIP: g.filmstrip = !g.filmstrip; layout(); ask_thumbs(); refresh(); break;
    case C_SS_PLAY: g.paused = false; SetTimer(g.wnd, T_SLIDE, SLIDE_MS, nullptr); refresh(); break;
    case C_SS_PAUSE: g.paused = true; KillTimer(g.wnd, T_SLIDE); refresh(); break;
    case C_SS_CLOSE: slide_show(false); break;
    }
}

void slide_show(bool on)
{
    if (on == g.slideshow || (on && !g.view->count())) return;
    g.slideshow = on;
    g.paused = false;
    g.ssbar = false;
    g.hot = g.pressed = -1;
    if (on) {
        g.was_fit = g.view->best_fit();
        g.was_zoom = g.view->zoom();
        g.view->set_best_fit();
        SetTimer(g.wnd, T_SLIDE, SLIDE_MS, nullptr);
    } else {
        KillTimer(g.wnd, T_SLIDE);
        KillTimer(g.wnd, T_SSBAR);
        if (!g.was_fit) g.view->set_zoom(g.was_zoom);
    }
    FlxpSetFullscreen(g.wnd, on);
    trace("slideshow %d", on ? 1 : 0);
    layout();
    refresh();
}

int button_at(int x, int y)
{
    POINT p = { x, y };
    for (size_t i = 0; i < g.buttons.size(); i++)
        if (g.buttons[i].visible && g.buttons[i].cmd && PtInRect(&g.buttons[i].rc, p)) return (int)i;
    return -1;
}

void set_hot(int i)
{
    if (i == g.hot) return;
    g.hot = i;
    InvalidateRect(g.wnd, nullptr, FALSE);
}

void loaded_notify(void*) { PostMessageW(g.wnd, WM_LOADED, 0, 0); }

void theme_changed()
{
    if (g.toolbar_theme) { CloseThemeData(g.toolbar_theme); g.toolbar_theme = nullptr; }
    for (auto& kv : g.standin) if (kv.second) gdi::destroy_bitmap(kv.second);   // (the theme's icons)
    g.standin.clear();
    // a Longhorn build's theme (Theme.build 4051, 4074, ...; XP's says xp)
    std::string build = shell::theme_build(shell::session_theme());
    bool lh = !build.empty() && build.find_first_not_of("0123456789") == std::string::npos;
    if (g.font) DeleteObject(g.font);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (lh != g.longhorn || g.buttons.empty()) {
        g.longhorn = lh;
        make_buttons();
        make_tips();
        g.title.clear();
    }
    load_glyphs();
    layout();
    ask_thumbs();
    trace("look %s", g.longhorn ? "4051" : "xp");
    refresh();
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        layout();
        make_back();
        drop_disp();
        ask_thumbs();
        refresh();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LOADED:
        if (g.view->pump()) shown();
        else { update(); ask_thumbs(); }
        return 0;
    case WM_THUMBS: {
        shell::thumbs::Loader::Result r;
        bool any = false;
        while (g.thumbs && g.thumbs->take(r)) {
            auto it = g.thumb.find(r.path);
            if (it != g.thumb.end() && it->second && !r.bmp) { shell::thumbs::free_result(r); continue; }   // the EXIF thumbnail stays
            if (it != g.thumb.end() && it->second) gdi::destroy_bitmap(it->second);
            g.thumb[r.path] = r.bmp;
            r.bmp = nullptr;
            shell::thumbs::free_result(r);
            any = true;
        }
        trim_thumbs();
        if (any) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == T_SLIDE) { go_to(&Viewer::next); return 0; }
        if (wp == T_SSBAR) { KillTimer(hwnd, T_SSBAR); g.ssbar = false; layout(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        break;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        theme_changed();
        break;
    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (g.dragging) {
            int dw, dh;
            drawn_size(&dw, &dh);
            g.pan_x = g.drag_px - (double)(x - g.drag_at.x) / dw;
            g.pan_y = g.drag_py - (double)(y - g.drag_at.y) / dh;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g.slideshow) {
            static POINT last = { -1, -1 };
            if (last.x != x || last.y != y) {
                last = { x, y };
                if (!g.ssbar) { g.ssbar = true; layout(); InvalidateRect(hwnd, nullptr, FALSE); }
                SetTimer(hwnd, T_SSBAR, SSBAR_MS, nullptr);
            }
        }
        if (!g.tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
            g.tracking = TrackMouseEvent(&t);
        }
        set_hot(button_at(x, y));
        return 0;
    }
    case WM_MOUSELEAVE:
        g.tracking = false;
        set_hot(-1);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            bool hand = pannable() && PtInRect(&g.area, p) && button_at(p.x, p.y) < 0;
            SetCursor(LoadCursorW(nullptr, hand ? (LPCWSTR)IDC_SIZEALL : (LPCWSTR)IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        SetFocus(hwnd);
        int b = button_at(x, y);
        if (b >= 0) {
            if (g.buttons[b].enabled) { g.pressed = b; SetCapture(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        }
        POINT p = { x, y };
        if (pannable() && PtInRect(&g.area, p)) {
            g.dragging = true;
            g.drag_at = p;
            g.drag_px = g.pan_x; g.drag_py = g.pan_y;
            SetCapture(hwnd);
            return 0;
        }
        if (g.longhorn && g.filmstrip && !g.slideshow && y >= g.cy - LH_STRIP && y < g.cy - LH_STRIP + LH_CELL_H && x >= LH_CELL_X) {
            size_t i = g.strip_first + (size_t)((x - LH_CELL_X) / LH_CELL_W);
            if (i < g.view->count() && g.view->go(i)) shown();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g.dragging) { g.dragging = false; ReleaseCapture(); return 0; }
        if (g.pressed >= 0) {
            int b = g.pressed;
            g.pressed = -1;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            if (button_at((short)LOWORD(lp), (short)HIWORD(lp)) == b) command(g.buttons[b].cmd);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        g.dragging = false;
        g.pressed = -1;
        return 0;
    case WM_MOUSEWHEEL: {
        int d = (short)HIWORD(wp);
        g.wheel += d;
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &p);
        while (g.wheel >= WHEEL_DELTA || g.wheel <= -WHEEL_DELTA) {
            bool up = g.wheel > 0;
            g.wheel += up ? -WHEEL_DELTA : WHEEL_DELTA;
            if (LOWORD(wp) & MK_CONTROL) {
                double z = g.view->zoom();
                zoom_to(up ? luxview::zoom_step_in(z) : luxview::zoom_step_out(z), false, p.x, p.y);
            } else go_to(up ? &Viewer::prev : &Viewer::next);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        int step = 40;
        switch (wp) {
        case VK_LEFT: if (pannable() && !g.slideshow) pan_by(-step, 0); else command(C_PREV); return 0;
        case VK_RIGHT: if (pannable() && !g.slideshow) pan_by(step, 0); else command(C_NEXT); return 0;
        case VK_UP: if (pannable() && !g.slideshow) pan_by(0, -step); else command(C_PREV); return 0;
        case VK_DOWN: if (pannable() && !g.slideshow) pan_by(0, step); else command(C_NEXT); return 0;
        case VK_PRIOR: case VK_BACK: command(C_PREV); return 0;
        case VK_NEXT: case VK_SPACE: command(C_NEXT); return 0;
        case VK_HOME: go_to(&Viewer::first); return 0;
        case VK_END: go_to(&Viewer::last); return 0;
        case VK_ADD: case VK_OEM_PLUS: command(C_ZOOMIN); return 0;
        case VK_SUBTRACT: case VK_OEM_MINUS: command(C_ZOOMOUT); return 0;
        case VK_DELETE: command(C_DELETE); return 0;
        case VK_F1: command(C_HELP); return 0;
        case VK_F11: command(C_SLIDE); return 0;
        case VK_ESCAPE: if (g.slideshow) slide_show(false); return 0;
        case VK_RETURN: if (ctrl) properties(1); return 0;
        }
        if (ctrl) {
            switch (wp) {
            case 'B': command(C_FIT); return 0;
            case 'A': command(C_ACTUAL); return 0;
            case 'K': command(C_ROTCW); return 0;
            case 'L': command(C_ROTCCW); return 0;
            case 'S': command(C_COPYTO); return 0;
            case 'E': command(C_EDIT); return 0;
            case 'P': command(C_PRINT); return 0;
            }
        }
        break;
    }
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN) { properties(0); return 0; }
        break;
    case WM_CLOSE:
        save_state();
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void usage(FILE* f)
{
    fputs("usage: luxview [FILE | FOLDER]...\n"
          "  The pictures (PNG, JPEG, BMP, ICO, WebP): one file opens its folder's pictures at it, a folder its first picture,\n"
          "  several files make the list. LUXVIEW_TRACE=1 prints what is shown and drawn on stdout.\n", f);
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    bool opts = true;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (opts && a == "--") { opts = false; continue; }
        if (opts && (a == "--help" || a == "-h")) { usage(stdout); return 0; }
        if (opts && a.size() > 1 && a[0] == '-') { fprintf(stderr, "luxview: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
        args.push_back(a);
    }
    g.trace = getenv("LUXVIEW_TRACE") && *getenv("LUXVIEW_TRACE") == '1';
    WuxInit();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"luxview";
    RegisterClassW(&wc);
    // the window where it was (luxview.ini in the state directory), else 800 x 600 where the window manager puts it
    lhde_ini* st = lhde_ini_load(state_path().c_str());
    int x = state_int(st, "window", "x", CW_USEDEFAULT), y = state_int(st, "window", "y", CW_USEDEFAULT);
    int w = state_int(st, "window", "width", 800), h = state_int(st, "window", "height", 600);
    bool maxed = state_int(st, "window", "maximized", 0) != 0;
    g.filmstrip = state_int(st, "view", "filmstrip", 1) != 0;
    if (st) lhde_ini_free(st);
    if (w < 200) w = 200;
    if (h < 150) h = 150;

    g.view = new Viewer(loaded_notify, nullptr);
    g.wnd = CreateWindowExW(0, L"luxview", L"luxview", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, nullptr, nullptr);
    if (!g.wnd) { fprintf(stderr, "luxview: no window\n"); return 1; }
    shell::set_window_icon_key(g.wnd, "image");
    theme_changed();
    ShowWindow(g.wnd, maxed ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(g.wnd);
    trace("mapped");
    // then the list and the first picture: its thumbnail and decode come from the loader thread
    size_t start = 0;
    std::vector<std::string> files = luxview::list_for(args, &start);
    g.view->set_files(files, start);
    if (!files.empty()) trace("index %zu of %zu %s", start + 1, files.size(), base_of(files[start]).c_str());
    refresh();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    // closed, maybe during a decode: the loaders end and join their threads before the bitmaps go
    delete g.view;
    g.view = nullptr;
    drop_disp();
    if (g.thumbs) { delete g.thumbs; g.thumbs = nullptr; }
    for (auto& kv : g.thumb) if (kv.second) gdi::destroy_bitmap(kv.second);
    trace("exit rss %ld KB", rss_kb());
    return 0;
}
