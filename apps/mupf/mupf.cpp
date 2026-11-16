// mupf: lhde's document viewer (docs/USAGE.md "mupf"), an application on the public entry point (wux/wux.h, shell/shell.h,
// apps/app.mk; docs/APPLICATIONS.md).
//
//   mupf [FILE]
//
// A window in the idiom of XP's programs: the menu bar (File, Edit, View, Go, Help), a toolbar of small buttons, the pages
// on the application workspace colour, the status bar. XP had no PDF viewer of its own; Acrobat Reader 6 (2003) is what
// one looked like there, and where mupf follows it (the page layouts, the keys, the grey around the pages, the black
// frame and shadow of a page) the names say so; the toolbar's order and glyphs are mupf's taste, not a measured layout.
//
// The window never waits on mupdf: the engine (engine.h) opens, renders and searches on its thread, and posts what it has;
// the view draws what the cache (layout.h) holds, a page at another zoom scaled until its rendering at this one arrives,
// white where nothing is yet. What is wanted is asked again on every scroll and zoom, so a page scrolled past is dropped
// from the engine's queue (its render aborted) before it costs anything.
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "wux/wux.h"
#include "shell/shell.h"
#include "lhde/settings.h"
#include "engine.h"
#include "layout.h"
#include "resource.h"

WUX_RESOURCES(mupf_res, "apps/mupf/mupf.res");

using namespace mupf;

namespace {

const UINT WM_ENGINE = WM_APP + 1;
const int CACHE_BUDGET = 64 << 20;     // the rendered pages kept (layout.h's Cache)
const int LINE = 40;                   // an arrow key's or a wheel notch's third, in pixels
const int PREFETCH_AHEAD = 2, PREFETCH_BEHIND = 1;
const int ID_TOOLBAR = 10, ID_STATUS = 11, ID_VIEW = 12, ID_PAGE_EDIT = 13, ID_PAGE_OF = 14;

enum ZoomMode { Z_CUSTOM, Z_FIT_PAGE, Z_FIT_WIDTH };

struct App {
    HWND wnd = nullptr, toolbar = nullptr, status = nullptr, view = nullptr, page_edit = nullptr, page_of = nullptr;
    HINSTANCE res = nullptr;
    HACCEL accel = nullptr;
    HMENU menu = nullptr;
    HFONT font = nullptr;
    HIMAGELIST glyphs = nullptr;
    Engine* eng = nullptr;
    Cache* cache = nullptr;
    // the document
    uint32_t gen = 0;
    bool opened = false;
    std::string path, title, format, error;
    int pages = 0;
    std::vector<SizeF> sizes;
    std::map<int, std::vector<Link>> links;
    // the view
    Mode mode = MODE_CONTINUOUS;
    bool cover = false;
    int rot = 0;
    ZoomMode zmode = Z_FIT_WIDTH;
    double zoom = 1;
    Layout lay;
    int sx = 0, sy = 0, vw = 0, vh = 0;
    int current = 0;
    bool in_layout = false;
    // chrome
    bool show_toolbar = true, show_status = true, fullscreen = false;
    // the back buffer
    HDC mem = nullptr;
    HBITMAP back = nullptr;
    uint32_t* bits = nullptr;
    int bw = 0, bh = 0;
    // history: pages left by a jump
    std::vector<int> back_pages, fwd_pages;
    // the hand: a drag pans; a click on a link follows it
    bool panning = false, pan_moved = false;
    POINT pan_at = {};
    int pan_sx = 0, pan_sy = 0;
    int wheel = 0;
    bool trace = false;
    std::string status_text[3], shown_trace;
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

std::u16string W(const std::string& s) { return WuxWide(s); }
#define WS(s) ((LPCWSTR)W(s).c_str())
std::string base_of(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }

// ---- state -------------------------------------------------------------------------------------------------------------

std::string state_path()
{
    char buf[4096];
    return lhde_state_file("mupf.ini", buf, sizeof buf) ? buf : "";
}

int state_int(lhde_ini* ini, const char* sec, const char* key, int def)
{
    char buf[64];
    if (!ini || !lhde_ini_get(ini, sec, key, buf, sizeof buf) || !*buf) return def;
    return atoi(buf);
}

void save_state()
{
    if (g.fullscreen) return;
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof wp;
    GetWindowPlacement(g.wnd, &wp);
    std::string p = state_path();
    if (p.empty()) return;
    lhde_ini* s = lhde_ini_load(p.c_str());
    if (!s) s = lhde_ini_new();
    if (!s) return;
    lhde_ini_header(s, "what mupf remembers (docs/USAGE.md \"mupf\"): where its window was, the bars and panes shown, the "
                       "page layout and zoom");
    auto put = [&](const char* sec, const char* k, long v) { char b[32]; snprintf(b, sizeof b, "%ld", v); lhde_ini_set(s, sec, k, b); };
    put("window", "x", wp.rcNormalPosition.left);
    put("window", "y", wp.rcNormalPosition.top);
    put("window", "width", wp.rcNormalPosition.right - wp.rcNormalPosition.left);
    put("window", "height", wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
    put("window", "maximized", wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED) ? 1 : 0);
    put("view", "toolbar", g.show_toolbar);
    put("view", "statusbar", g.show_status);
    put("view", "layout", g.mode);
    put("view", "cover", g.cover);
    put("view", "zoom_mode", g.zmode);
    put("view", "zoom", lround(g.zoom * 1000));
    lhde_ini_save(s, p.c_str());
    lhde_ini_free(s);
}

void load_state(int* x, int* y, int* w, int* h, bool* maxed)
{
    lhde_ini* st = lhde_ini_load(state_path().c_str());
    *x = state_int(st, "window", "x", CW_USEDEFAULT);
    *y = state_int(st, "window", "y", CW_USEDEFAULT);
    *w = std::max(300, state_int(st, "window", "width", 800));
    *h = std::max(200, state_int(st, "window", "height", 700));
    *maxed = state_int(st, "window", "maximized", 0) != 0;
    g.show_toolbar = state_int(st, "view", "toolbar", 1) != 0;
    g.show_status = state_int(st, "view", "statusbar", 1) != 0;
    int m = state_int(st, "view", "layout", MODE_CONTINUOUS);
    g.mode = m >= MODE_SINGLE && m <= MODE_CONTINUOUS_FACING ? (Mode)m : MODE_CONTINUOUS;
    g.cover = state_int(st, "view", "cover", 0) != 0;
    int z = state_int(st, "view", "zoom_mode", Z_FIT_WIDTH);
    g.zmode = z >= Z_CUSTOM && z <= Z_FIT_WIDTH ? (ZoomMode)z : Z_FIT_WIDTH;
    g.zoom = clamp_zoom(state_int(st, "view", "zoom", 1000) / 1000.0);
    if (st) lhde_ini_free(st);
}

// ---- the status bar and the title ------------------------------------------------------------------------------------

void set_status(int part, const std::string& text)
{
    if (g.status_text[part] == text) return;
    g.status_text[part] = text;
    if (g.status) SendMessageW(g.status, SB_SETTEXTW, part, (LPARAM)WS(text));
    trace("status %s|%s|%s", g.status_text[0].c_str(), g.status_text[1].c_str(), g.status_text[2].c_str());
}

void update_page_box()
{
    char b[64];
    if (g.opened) snprintf(b, sizeof b, "Page %d of %d", g.current + 1, g.pages);
    else b[0] = 0;
    set_status(1, b);
    int z = (int)lround(g.zoom * 100);
    if (g.opened) snprintf(b, sizeof b, "%d%%", z);
    else b[0] = 0;
    set_status(2, b);
    if (g.page_edit && GetFocus() != g.page_edit) {
        if (g.opened) snprintf(b, sizeof b, "%d", g.current + 1);
        else b[0] = 0;
        SetWindowTextW(g.page_edit, WS(b));
    }
    if (g.page_of) {
        if (g.opened) snprintf(b, sizeof b, "of %d", g.pages);
        else b[0] = 0;
        SetWindowTextW(g.page_of, WS(b));
    }
}

void update_title()
{
    std::string t = g.opened || !g.path.empty() ? base_of(g.path) + " - mupf" : "mupf";
    SetWindowTextW(g.wnd, WS(t));
    trace("title %s", t.c_str());
}

// ---- layout and scrolling ----------------------------------------------------------------------------------------------

void request();

void view_size()
{
    RECT r;
    GetClientRect(g.view, &r);
    g.vw = std::max(1, (int)r.right);
    g.vh = std::max(1, (int)r.bottom);
}

void update_scrollbars()
{
    SCROLLINFO si = {};
    si.cbSize = sizeof si;
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;   // the vertical bar always there, as Acrobat's
    si.nMin = 0;
    si.nMax = g.lay.height - 1;
    si.nPage = g.vh;
    si.nPos = g.sy;
    SetScrollInfo(g.view, SB_VERT, &si, TRUE);
    si.fMask = SIF_ALL;
    si.nMax = g.lay.width - 1;
    si.nPage = g.vw;
    si.nPos = g.sx;
    SetScrollInfo(g.view, SB_HORZ, &si, TRUE);
}

void clamp_scroll()
{
    g.sx = std::max(0, std::min(g.sx, g.lay.width - g.vw));
    g.sy = std::max(0, std::min(g.sy, g.lay.height - g.vh));
}

void compute_zoom()
{
    if (g.zmode == Z_FIT_WIDTH) g.zoom = fit_width(g.sizes, g.rot, g.mode, g.cover, g.current, g.vw);
    else if (g.zmode == Z_FIT_PAGE) g.zoom = fit_page(g.sizes, g.rot, g.mode, g.cover, g.current, g.vw, g.vh);
}

// the page under a point of the view and where on it, as fractions of the page box; false when none is
struct Anchor { int page = -1; double fx = 0, fy = 0; int vx = 0, vy = 0; };
Anchor anchor_at(int vx, int vy)
{
    Anchor a;
    a.vx = vx;
    a.vy = vy;
    if (!g.opened || g.lay.page.empty()) return a;
    int px = g.sx + vx, py = g.sy + vy, best = -1;
    long best_d = 0;
    for (int i = 0; i < (int)g.lay.page.size(); i++) {
        const Box& b = g.lay.page[i];
        if (b.empty()) continue;
        long dy = py < b.y ? b.y - py : py > b.y + b.h ? py - (b.y + b.h) : 0;
        long dx = px < b.x ? b.x - px : px > b.x + b.w ? px - (b.x + b.w) : 0;
        long d = dx + dy;
        if (best < 0 || d < best_d) { best = i; best_d = d; }
    }
    if (best < 0) return a;
    const Box& b = g.lay.page[best];
    a.page = best;
    a.fx = (double)(px - b.x) / b.w;
    a.fy = (double)(py - b.y) / b.h;
    return a;
}

void scrolled();

// lays the pages out again (a new zoom, mode, size or rotation), the anchor's page point kept where it was in the view
void relayout(const Anchor& a)
{
    if (g.in_layout) return;
    g.in_layout = true;
    view_size();
    if (g.opened && a.page >= 0 && !is_continuous(g.mode)) g.current = a.page;
    compute_zoom();
    g.lay = make_layout(g.sizes, g.rot, g.zoom, g.mode, g.cover, g.current, g.vw, g.vh);
    update_scrollbars();
    view_size();   // a scroll bar shown or hidden
    if (g.zmode != Z_CUSTOM) compute_zoom();
    g.lay = make_layout(g.sizes, g.rot, g.zoom, g.mode, g.cover, g.current, g.vw, g.vh);
    if (a.page >= 0 && a.page < (int)g.lay.page.size() && !g.lay.page[a.page].empty()) {
        const Box& b = g.lay.page[a.page];
        g.sx = b.x + (int)lround(a.fx * b.w) - a.vx;
        g.sy = b.y + (int)lround(a.fy * b.h) - a.vy;
    }
    clamp_scroll();
    update_scrollbars();
    g.in_layout = false;
    scrolled();
}

// the view moved: the current page, what to render, the page box
void scrolled()
{
    if (g.opened && is_continuous(g.mode)) {
        int c = current_page(g.lay, g.sx, g.sy, g.vw, g.vh);
        if (c >= 0) g.current = c;
    }
    update_page_box();
    request();
    InvalidateRect(g.view, nullptr, FALSE);
}

void scroll_to(int x, int y)
{
    g.sx = x;
    g.sy = y;
    clamp_scroll();
    update_scrollbars();
    scrolled();
}

// to page p (y points down it): the page's top at the view's top
void go_page(int p, float y_pt = 0, bool history = true)
{
    if (!g.opened || g.pages <= 0) return;
    p = std::max(0, std::min(p, g.pages - 1));
    if (history && p != g.current) {
        g.back_pages.push_back(g.current);
        g.fwd_pages.clear();
    }
    g.current = p;
    if (!is_continuous(g.mode)) {
        Anchor none;
        relayout(none);
    }
    const Box& b = g.lay.page[p];
    if (b.empty()) return;
    double px = 0, py = 0;
    page_to_px(0, y_pt, g.sizes[p], g.rot, g.zoom, &px, &py);
    int top = y_pt > 0 && g.rot == 0 ? b.y + (int)py : b.y - PAGE_GAP / 2;
    if (!is_continuous(g.mode)) top = y_pt > 0 && g.rot == 0 ? (int)py : 0;
    g.sy = top;
    clamp_scroll();
    update_scrollbars();
    if (is_continuous(g.mode)) {
        g.current = p;   // the page asked for, even when the next shows more of itself
        update_page_box();
        request();
        InvalidateRect(g.view, nullptr, FALSE);
    } else scrolled();
}

// the next or previous spread's first page
int step_page(int dir)
{
    int s = spread_of(g.current, g.mode, g.cover) + dir;
    int n = spread_count(g.pages, g.mode, g.cover);
    if (s < 0 || s >= n) return -1;
    int l, r;
    spread_pages(s, g.pages, g.mode, g.cover, &l, &r);
    return l >= 0 ? l : r;
}

void next_page(int dir, bool to_bottom = false)
{
    int p = step_page(dir);
    if (p < 0) return;
    go_page(p, 0, false);
    if (to_bottom && !is_continuous(g.mode)) scroll_to(g.sx, g.lay.height);
}

// a screen down or up (Space): past the end of the page in the single modes to the next one
void scroll_screen(int dir)
{
    int step = std::max(LINE, g.vh - LINE);
    if (!is_continuous(g.mode)) {
        if (dir > 0 && g.sy + g.vh >= g.lay.height) { next_page(1); return; }
        if (dir < 0 && g.sy <= 0) { next_page(-1, true); return; }
    }
    scroll_to(g.sx, g.sy + dir * step);
}

void scroll_lines(int dy)
{
    if (!is_continuous(g.mode) && g.opened) {
        if (dy > 0 && g.sy + g.vh >= g.lay.height) { next_page(1); return; }
        if (dy < 0 && g.sy <= 0) { next_page(-1, true); return; }
    }
    scroll_to(g.sx, g.sy + dy);
}

void set_zoom(ZoomMode m, double z, const Anchor& a)
{
    g.zmode = m;
    if (m == Z_CUSTOM) g.zoom = clamp_zoom(z);
    relayout(a);
    trace("zoom %d%%", (int)lround(g.zoom * 100));
}

Anchor centre_anchor() { return anchor_at(g.vw / 2, is_continuous(g.mode) ? 0 : g.vh / 2); }
Anchor top_anchor() { return anchor_at(g.vw / 2, 0); }

// ---- rendering ---------------------------------------------------------------------------------------------------------

// what the view needs: the parts of the pages in it that the cache lacks, then the next pages whole (the way the reader
// goes: ahead two, behind one) while the cache has room
void request()
{
    if (!g.opened || !g.eng) return;
    std::vector<Job> jobs;
    std::vector<int> in = pages_in_view(g.lay, g.sx, g.sy, g.vw, g.vh);
    size_t room = g.cache->budget();
    for (int p : in) {
        Box part = render_part(g.lay.page[p], g.sx, g.sy, g.vw, g.vh);
        if (part.empty()) continue;
        room -= std::min(room, (size_t)part.w * part.h * 4);
        if (g.cache->covered(p, g.rot, g.zoom, part)) continue;
        jobs.push_back({ p, g.rot, g.zoom, part });
    }
    auto prefetch = [&](int p) {
        if (p < 0 || p >= g.pages) return;
        int w, h;
        page_px(g.sizes[p], g.rot, g.zoom, &w, &h);
        size_t bytes = (size_t)w * h * 4;
        if (bytes > WHOLE_TILE_MAX || bytes > room) return;
        room -= bytes;
        Box whole = { 0, 0, w, h };
        if (!g.cache->covered(p, g.rot, g.zoom, whole)) jobs.push_back({ p, g.rot, g.zoom, whole });
    };
    int first = in.empty() ? g.current : in.front(), last = in.empty() ? g.current : in.back();
    // the single modes: the neighbouring spreads
    if (!is_continuous(g.mode)) {
        int l, r, s = spread_of(g.current, g.mode, g.cover);
        spread_pages(s, g.pages, g.mode, g.cover, &l, &r);
        first = l >= 0 ? l : r;
        last = r >= 0 ? r : l;
    }
    for (int i = 1; i <= PREFETCH_AHEAD; i++) prefetch(last + i);
    for (int i = 1; i <= PREFETCH_BEHIND; i++) prefetch(first - i);
    g.eng->want(jobs);
}

void make_back()
{
    if (g.back && g.bw == g.vw && g.bh == g.vh) return;
    if (g.back) DeleteObject(g.back);
    if (!g.mem) g.mem = CreateCompatibleDC(nullptr);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = g.vw;
    bi.bmiHeader.biHeight = -g.vh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* p = nullptr;
    g.back = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
    g.bits = (uint32_t*)p;
    g.bw = g.vw;
    g.bh = g.vh;
    SelectObject(g.mem, g.back);
}

uint32_t px_of(COLORREF c) { return 0xff000000u | GetRValue(c) << 16 | GetGValue(c) << 8 | GetBValue(c); }

void fill(int x, int y, int w, int h, uint32_t c)
{
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(g.bw, x + w), y1 = std::min(g.bh, y + h);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* row = g.bits + (size_t)yy * g.bw;
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

// a tile into the page box (bx, by, bw x bh in the view), scaled from its zoom to the box's (nearest pixel)
void draw_tile(const Tile* t, int bx, int by, int bwid, int bhei, bool same_zoom)
{
    if (t->full_w <= 0 || t->full_h <= 0) return;
    bool exact = same_zoom || (bwid == t->full_w && bhei == t->full_h);   // the box and mupdf's may differ by a pixel
    double kx = exact ? 1 : (double)bwid / t->full_w, ky = exact ? 1 : (double)bhei / t->full_h;
    int dx0 = bx + (int)floor(t->x * kx), dy0 = by + (int)floor(t->y * ky);
    int dx1 = bx + (int)ceil((t->x + t->w) * kx), dy1 = by + (int)ceil((t->y + t->h) * ky);
    dx1 = std::min(dx1, bx + bwid);
    dy1 = std::min(dy1, by + bhei);
    int x0 = std::max(0, dx0), y0 = std::max(0, dy0), x1 = std::min(g.bw, dx1), y1 = std::min(g.bh, dy1);
    if (x1 <= x0 || y1 <= y0) return;
    std::vector<int> col;
    if (!exact) {
        col.resize(x1 - x0);
        for (int x = x0; x < x1; x++) col[x - x0] = std::min(t->w - 1, std::max(0, (int)((x - bx + 0.5) / kx) - t->x));
    }
    for (int y = y0; y < y1; y++) {
        int sy = exact ? y - by - t->y : std::min(t->h - 1, std::max(0, (int)((y - by + 0.5) / ky) - t->y));
        if (sy < 0 || sy >= t->h) continue;
        const uint32_t* src = &t->px[(size_t)sy * t->w];
        uint32_t* dst = g.bits + (size_t)y * g.bw;
        if (exact) {
            int sx0 = x0 - bx - t->x;
            if (sx0 < 0) continue;
            memcpy(dst + x0, src + sx0, (size_t)std::min(x1 - x0, t->w - sx0) * 4);
        } else
            for (int x = x0; x < x1; x++) dst[x] = src[col[x - x0]];
    }
}

void paint_view(HDC dc)
{
    make_back();
    // Acrobat's grey around the pages: the application workspace colour (128 grey under XP's styles and classic)
    fill(0, 0, g.bw, g.bh, px_of(GetSysColor(COLOR_APPWORKSPACE)));
    std::string shown;
    if (g.opened) {
        for (int p : pages_in_view(g.lay, g.sx, g.sy, g.vw, g.vh)) {
            const Box& b = g.lay.page[p];
            int x = b.x - g.sx, y = b.y - g.sy;
            // a page: white, a 1 px black frame, a 2 px shadow below and to the right (Acrobat's)
            fill(x + 2, y + b.h, b.w, 2, 0xff404040);
            fill(x + b.w, y + 2, 2, b.h, 0xff404040);
            fill(x, y, b.w, b.h, 0xffffffff);
            bool exact = false;
            for (const Tile* t : g.cache->tiles_for(p, g.rot, g.zoom)) {
                bool same = t->same_zoom(g.zoom);
                draw_tile(t, x, y, b.w, b.h, same);
                exact = exact || same;
            }
            fill(x - 1, y - 1, b.w + 2, 1, 0xff000000);
            fill(x - 1, y + b.h, b.w + 2, 1, 0xff000000);
            fill(x - 1, y, 1, b.h, 0xff000000);
            fill(x + b.w, y, 1, b.h, 0xff000000);
            if (exact && g.trace) {
                POINT o = { x, y };
                ClientToScreen(g.view, &o);
                char buf[96];
                snprintf(buf, sizeof buf, " %d:%d,%d,%dx%d", p + 1, (int)o.x, (int)o.y, b.w, b.h);
                shown += buf;
            }
        }
    }
    BitBlt(dc, 0, 0, g.bw, g.bh, g.mem, 0, 0, SRCCOPY);
    if (g.trace && shown != g.shown_trace) {
        g.shown_trace = shown;
        if (!shown.empty()) trace("shown%s", shown.c_str());
    }
}

// ---- the document ------------------------------------------------------------------------------------------------------

void engine_notify(void*) { PostMessageW(g.wnd, WM_ENGINE, 0, 0); }

void close_doc()
{
    if (g.eng) g.eng->close();
    g.cache->clear();
    g.opened = false;
    g.pages = 0;
    g.sizes.clear();
    g.links.clear();
    g.title.clear();
    g.format.clear();
    g.error.clear();
    g.current = 0;
    g.back_pages.clear();
    g.fwd_pages.clear();
    g.sx = g.sy = 0;
    set_status(0, "");
    Anchor none;
    relayout(none);
}

void open_doc(const std::string& path)
{
    close_doc();
    char* abs = realpath(path.c_str(), nullptr);
    g.path = abs ? abs : path;
    free(abs);
    g.gen = g.eng->open(g.path);
    set_status(0, "Opening...");
    update_title();
    trace("open %s", g.path.c_str());
}

void on_engine()
{
    Event e;
    bool sizes_changed = false;
    while (g.eng->take(e)) {
        if (e.gen != g.gen) { delete e.tile; continue; }
        switch (e.kind) {
        case Event::OPENED:
            g.opened = true;
            g.pages = e.pages;
            g.sizes = e.sizes;
            g.title = e.title;
            g.format = e.format;
            g.current = 0;
            g.sx = g.sy = 0;
            set_status(0, "");
            trace("opened %d pages %s", g.pages, g.format.c_str());
            update_title();
            {
                Anchor none;
                relayout(none);
            }
            break;
        case Event::FAILED: {
            g.error = e.error;
            set_status(0, "");
            update_title();
            trace("failed %s", e.error.c_str());
            std::string msg = "There was an error opening this document. " + e.error;
            shell::message_box(g.wnd, "mupf", msg, MB_OK | MB_ICONEXCLAMATION);
            break;
        }
        case Event::SIZES:
            for (size_t i = 0; i < e.sizes.size() && e.first + i < g.sizes.size(); i++) {
                SizeF& s = g.sizes[e.first + i];
                if (s.w != e.sizes[i].w || s.h != e.sizes[i].h) { s = e.sizes[i]; sizes_changed = true; }
            }
            break;
        case Event::RENDERED:
            trace("rendered %d %dx%d of %dx%d at %d%%", e.page + 1, e.tile->w, e.tile->h, e.tile->full_w, e.tile->full_h,
                  (int)lround(e.tile->zoom * 100));
            g.cache->put(e.tile, g.current, g.zoom);
            e.tile = nullptr;
            InvalidateRect(g.view, nullptr, FALSE);
            break;
        case Event::LINKS:
            g.links[e.page] = std::move(e.links);
            break;
        default:
            break;
        }
    }
    if (sizes_changed) relayout(top_anchor());
}

void open_dialog()
{
    WCHAR file[4096] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.wnd;
    ofn.lpstrFilter = L"All Documents (*.pdf, *.xps, *.oxps, *.epub, *.cbz, *.fb2, *.mobi)\0*.pdf;*.xps;*.oxps;*.epub;*.cbz;*.fb2;*.mobi\0"
                      L"Adobe PDF Files (*.pdf)\0*.pdf\0XPS Documents (*.xps, *.oxps)\0*.xps;*.oxps\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file / sizeof file[0];
    ofn.lpstrTitle = L"Open";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    open_doc(WuxUtf8(file));
}

// ---- the window --------------------------------------------------------------------------------------------------------

void layout_chrome();

INT_PTR CALLBACK goto_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG: {
        char b[32];
        snprintf(b, sizeof b, "%d", g.current + 1);
        SetDlgItemTextW(dlg, IDC_GOTO_PAGE, WS(b));
        snprintf(b, sizeof b, "of %d", g.pages);
        SetDlgItemTextW(dlg, IDC_GOTO_OF, WS(b));
        SendDlgItemMessageW(dlg, IDC_GOTO_PAGE, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dlg, IDC_GOTO_PAGE));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            int n = (int)GetDlgItemInt(dlg, IDC_GOTO_PAGE, nullptr, FALSE);
            if (n < 1 || n > g.pages) {
                char b[96];
                snprintf(b, sizeof b, "There is no page numbered %d in this document.", n);
                shell::message_box(dlg, "Go To Page", b, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            EndDialog(dlg, n);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, 0); return TRUE; }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK about_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        SendDlgItemMessageW(dlg, IDC_ABOUT_ICON, STM_SETICON, (WPARAM)LoadIconW(nullptr, (LPCWSTR)IDI_INFORMATION), 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) EndDialog(dlg, LOWORD(wp));
        return TRUE;
    }
    return FALSE;
}

void set_fullscreen(bool on)
{
    if (on == g.fullscreen) return;
    if (on) save_state();
    g.fullscreen = on;
    SetMenu(g.wnd, on ? nullptr : g.menu);
    FlxpSetFullscreen(g.wnd, on);
    layout_chrome();
    SetFocus(g.view);
}

void update_menu()
{
    HMENU m = g.menu;
    auto check = [&](UINT id, bool on) { CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
    auto enable = [&](UINT id, bool on) { EnableMenuItem(m, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    check(CMD_TOOLBAR, g.show_toolbar);
    check(CMD_STATUSBAR, g.show_status);
    CheckMenuRadioItem(m, CMD_SINGLE, CMD_CONT_FACING, CMD_SINGLE + g.mode, MF_BYCOMMAND);
    check(CMD_COVER, g.cover);
    enable(CMD_COVER, is_facing(g.mode));
    check(CMD_FIT_PAGE, g.zmode == Z_FIT_PAGE);
    check(CMD_FIT_WIDTH, g.zmode == Z_FIT_WIDTH);
    check(CMD_FULLSCREEN, g.fullscreen);
    bool d = g.opened;
    for (UINT id : { CMD_CLOSE, CMD_PROPERTIES, CMD_FIRST, CMD_LAST, CMD_GOTO, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ACTUAL,
                     CMD_FIT_PAGE, CMD_FIT_WIDTH, CMD_ROT_CW, CMD_ROT_CCW })
        enable(id, d);
    enable(CMD_PREV, d && step_page(-1) >= 0);
    enable(CMD_NEXT, d && step_page(1) >= 0);
    enable(CMD_BACK, d && !g.back_pages.empty());
    enable(CMD_FORWARD, d && !g.fwd_pages.empty());
}

void update_toolbar()
{
    if (!g.toolbar) return;
    auto en = [](int id, bool on) { SendMessageW(g.toolbar, TB_ENABLEBUTTON, id, MAKELONG(on, 0)); };
    bool d = g.opened;
    en(CMD_PREV, d && step_page(-1) >= 0);
    en(CMD_NEXT, d && step_page(1) >= 0);
    for (int id : { CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ACTUAL, CMD_FIT_PAGE, CMD_FIT_WIDTH }) en(id, d);
    SendMessageW(g.toolbar, TB_CHECKBUTTON, CMD_FIT_PAGE, MAKELONG(g.zmode == Z_FIT_PAGE, 0));
    SendMessageW(g.toolbar, TB_CHECKBUTTON, CMD_FIT_WIDTH, MAKELONG(g.zmode == Z_FIT_WIDTH, 0));
    EnableWindow(g.page_edit, d);
}

void set_mode(Mode m)
{
    Anchor a = top_anchor();
    g.mode = m;
    relayout(a);
    if (!is_continuous(m)) go_page(g.current, 0, false);
    trace("layout %d", (int)m);
}

void rotate(int by)
{
    Anchor a = top_anchor();
    g.rot = (g.rot + by + 360) % 360;
    relayout(a);
    trace("rotation %d", g.rot);
}

void command(int id)
{
    switch (id) {
    case CMD_OPEN: open_dialog(); break;
    case CMD_CLOSE: close_doc(); g.path.clear(); update_title(); break;
    case CMD_PROPERTIES:
        if (g.opened) shell::properties(g.wnd, { g.path });
        break;
    case CMD_EXIT: PostMessageW(g.wnd, WM_CLOSE, 0, 0); break;
    case CMD_TOOLBAR: g.show_toolbar = !g.show_toolbar; layout_chrome(); break;
    case CMD_STATUSBAR: g.show_status = !g.show_status; layout_chrome(); break;
    case CMD_ZOOM_IN: if (g.opened) set_zoom(Z_CUSTOM, zoom_in(g.zoom), centre_anchor()); break;
    case CMD_ZOOM_OUT: if (g.opened) set_zoom(Z_CUSTOM, zoom_out(g.zoom), centre_anchor()); break;
    case CMD_ACTUAL: if (g.opened) set_zoom(Z_CUSTOM, 1.0, centre_anchor()); break;
    case CMD_FIT_PAGE: if (g.opened) { set_zoom(Z_FIT_PAGE, 0, top_anchor()); go_page(g.current, 0, false); } break;
    case CMD_FIT_WIDTH: if (g.opened) set_zoom(Z_FIT_WIDTH, 0, top_anchor()); break;
    case CMD_SINGLE: case CMD_CONTINUOUS: case CMD_FACING: case CMD_CONT_FACING: set_mode((Mode)(id - CMD_SINGLE)); break;
    case CMD_COVER: g.cover = !g.cover; set_mode(g.mode); break;
    case CMD_ROT_CW: if (g.opened) rotate(90); break;
    case CMD_ROT_CCW: if (g.opened) rotate(-90); break;
    case CMD_FULLSCREEN: set_fullscreen(!g.fullscreen); break;
    case CMD_FIRST: go_page(0); break;
    case CMD_LAST: go_page(g.pages - 1); break;
    case CMD_PREV: next_page(-1); break;
    case CMD_NEXT: next_page(1); break;
    case CMD_GOTO:
        if (g.opened) {
            int n = (int)DialogBoxParamW(g.res, MAKEINTRESOURCEW(IDD_GOTO), g.wnd, goto_proc, 0);
            if (n > 0) go_page(n - 1);
        }
        break;
    case CMD_BACK:
        if (!g.back_pages.empty()) {
            int p = g.back_pages.back();
            g.back_pages.pop_back();
            g.fwd_pages.push_back(g.current);
            go_page(p, 0, false);
        }
        break;
    case CMD_FORWARD:
        if (!g.fwd_pages.empty()) {
            int p = g.fwd_pages.back();
            g.fwd_pages.pop_back();
            g.back_pages.push_back(g.current);
            go_page(p, 0, false);
        }
        break;
    case CMD_ABOUT: DialogBoxParamW(g.res, MAKEINTRESOURCEW(IDD_ABOUT), g.wnd, about_proc, 0); break;
    }
    update_toolbar();
}

// ---- the toolbar -------------------------------------------------------------------------------------------------------

enum { GL_OPEN, GL_PREV, GL_NEXT, GL_ZOOMOUT, GL_ZOOMIN, GL_ACTUAL, GL_FITPAGE, GL_FITWIDTH, GL_CONTENTS, GL_FIND, GL_COUNT };

// the glyphs: plain shapes in XP's toolbar colours on a key colour (mupf's own drawing, 16 x 16)
void draw_glyph(HDC dc, int i, int ox)
{
    auto pen = [&](COLORREF c, int w) { return SelectObject(dc, CreatePen(PS_SOLID, w, c)); };
    auto brush = [&](COLORREF c) { return SelectObject(dc, CreateSolidBrush(c)); };
    auto done = [&](HGDIOBJ op, HGDIOBJ ob) { DeleteObject(SelectObject(dc, op)); if (ob) DeleteObject(SelectObject(dc, ob)); };
    const COLORREF blue = RGB(33, 93, 214), dark = RGB(10, 50, 140), white = RGB(255, 255, 255), grey = RGB(90, 90, 90);
    HGDIOBJ op, ob;
    auto page = [&](int x0, int y0, int x1, int y1) {
        HGDIOBJ p1 = pen(grey, 1), b1 = brush(white);
        Rectangle(dc, ox + x0, y0, ox + x1, y1);
        done(p1, b1);
    };
    auto magnifier = [&](int sign) {
        HGDIOBJ p1 = pen(RGB(200, 140, 30), 3);
        MoveToEx(dc, ox + 3, 13, nullptr); LineTo(dc, ox + 7, 9);
        done(p1, nullptr);
        p1 = pen(dark, 1);
        HGDIOBJ b1 = brush(RGB(220, 235, 255));
        Ellipse(dc, ox + 5, 1, ox + 15, 11);
        done(p1, b1);
        if (sign) {
            p1 = pen(blue, 1);
            MoveToEx(dc, ox + 7, 6, nullptr); LineTo(dc, ox + 13, 6);
            if (sign > 0) { MoveToEx(dc, ox + 10, 3, nullptr); LineTo(dc, ox + 10, 9); }
            done(p1, nullptr);
        }
    };
    switch (i) {
    case GL_OPEN: {
        op = pen(RGB(150, 110, 20), 1); ob = brush(RGB(250, 210, 90));
        POINT f[6] = { { ox + 1, 3 }, { ox + 6, 3 }, { ox + 7, 5 }, { ox + 14, 5 }, { ox + 14, 13 }, { ox + 1, 13 } };
        Polygon(dc, f, 6);
        done(op, ob);
        op = pen(RGB(150, 110, 20), 1); ob = brush(RGB(255, 235, 150));
        POINT l[4] = { { ox + 3, 7 }, { ox + 15, 7 }, { ox + 13, 13 }, { ox + 1, 13 } };
        Polygon(dc, l, 4);
        done(op, ob);
        break;
    }
    case GL_PREV: case GL_NEXT: {
        op = pen(dark, 1); ob = brush(blue);
        Ellipse(dc, ox + 0, 0, ox + 16, 16);
        done(op, ob);
        op = pen(white, 1); ob = brush(white);
        if (i == GL_PREV) { POINT t[3] = { { ox + 10, 4 }, { ox + 10, 11 }, { ox + 5, 7 } }; Polygon(dc, t, 3); }
        else { POINT t[3] = { { ox + 6, 4 }, { ox + 6, 11 }, { ox + 11, 7 } }; Polygon(dc, t, 3); }
        done(op, ob);
        break;
    }
    case GL_ZOOMOUT: magnifier(-1); break;
    case GL_ZOOMIN: magnifier(1); break;
    case GL_FIND: magnifier(0); break;
    case GL_ACTUAL: {
        page(3, 0, 13, 16);
        HFONT f = CreateFontW(-9, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma");
        HGDIOBJ of = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, blue);
        RECT r = { ox + 3, 1, ox + 13, 15 };
        DrawTextW(dc, L"1", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(SelectObject(dc, of));
        break;
    }
    case GL_FITPAGE:
        page(4, 2, 12, 14);
        op = pen(blue, 1); ob = nullptr;
        MoveToEx(dc, ox + 0, 3, nullptr); LineTo(dc, ox + 0, 0); LineTo(dc, ox + 4, 0);
        MoveToEx(dc, ox + 12, 0, nullptr); LineTo(dc, ox + 15, 0); LineTo(dc, ox + 15, 4);
        MoveToEx(dc, ox + 0, 12, nullptr); LineTo(dc, ox + 0, 15); LineTo(dc, ox + 4, 15);
        MoveToEx(dc, ox + 12, 15, nullptr); LineTo(dc, ox + 15, 15); LineTo(dc, ox + 15, 11);
        done(op, ob);
        break;
    case GL_FITWIDTH: {
        page(2, 0, 14, 16);
        op = pen(blue, 1); ob = brush(blue);
        MoveToEx(dc, ox + 3, 8, nullptr); LineTo(dc, ox + 13, 8);
        POINT l[3] = { { ox + 3, 8 }, { ox + 6, 5 }, { ox + 6, 11 } };
        POINT r[3] = { { ox + 12, 8 }, { ox + 9, 5 }, { ox + 9, 11 } };
        Polygon(dc, l, 3); Polygon(dc, r, 3);
        done(op, ob);
        break;
    }
    case GL_CONTENTS:
        page(0, 0, 16, 16);
        op = pen(grey, 1); ob = nullptr;
        MoveToEx(dc, ox + 3, 3, nullptr); LineTo(dc, ox + 3, 12); LineTo(dc, ox + 6, 12);
        MoveToEx(dc, ox + 3, 7, nullptr); LineTo(dc, ox + 6, 7);
        done(op, ob);
        op = pen(blue, 1); ob = brush(blue);
        Rectangle(dc, ox + 2, 2, ox + 5, 5);
        Rectangle(dc, ox + 7, 6, ox + 13, 8);
        Rectangle(dc, ox + 7, 11, ox + 13, 13);
        done(op, ob);
        break;
    }
}

// the strip drawn on magenta, made a 32 bpp image with alpha (magenta transparent, the rest opaque)
HIMAGELIST make_glyphs()
{
    HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR32, GL_COUNT, 0);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = GL_COUNT * 16;
    bi.bmiHeader.biHeight = -16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP strip = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, strip);
    HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
    RECT r = { 0, 0, GL_COUNT * 16, 16 };
    FillRect(dc, &r, key);
    DeleteObject(key);
    for (int i = 0; i < GL_COUNT; i++) draw_glyph(dc, i, i * 16);
    SelectObject(dc, old);
    DeleteDC(dc);
    uint32_t* px = (uint32_t*)bits;
    for (int i = 0; i < GL_COUNT * 16 * 16; i++) px[i] = (px[i] & 0xffffff) == 0xff00ff ? 0 : (px[i] | 0xff000000u);
    ImageList_Add(il, strip, nullptr);
    DeleteObject(strip);
    return il;
}

const char* tip_for(int id)
{
    switch (id) {
    case CMD_OPEN: return "Open";
    case CMD_PREV: return "Previous Page";
    case CMD_NEXT: return "Next Page";
    case CMD_ZOOM_OUT: return "Zoom Out";
    case CMD_ZOOM_IN: return "Zoom In";
    case CMD_ACTUAL: return "Actual Size";
    case CMD_FIT_PAGE: return "Fit Page";
    case CMD_FIT_WIDTH: return "Fit Width";
    }
    return "";
}

WNDPROC edit_base = nullptr;

// the page box: Return goes to the page typed, Escape back to the pages
LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        if (wp == VK_RETURN && h == g.page_edit) {
            int n = atoi(WuxWindowText(h).c_str());
            SetFocus(g.view);
            if (n >= 1 && n <= g.pages) go_page(n - 1);
            else update_page_box();
            update_toolbar();
        } else {
            SetFocus(g.view);
            update_page_box();
        }
        return 0;
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 27)) return 0;   // no beep
    return CallWindowProcW(edit_base, h, msg, wp, lp);
}

// the toolbar (mupf's taste: Open | Previous, Next, the page box | Zoom Out, Zoom In | Actual Size, Fit Page, Fit Width)
void make_toolbar()
{
    g.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NODIVIDER, 0, 0, 0, 0, g.wnd,
                                (HMENU)(INT_PTR)ID_TOOLBAR, nullptr, nullptr);
    SendMessageW(g.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    g.glyphs = make_glyphs();
    SendMessageW(g.toolbar, TB_SETIMAGELIST, 0, (LPARAM)g.glyphs);
    const BYTE E = TBSTATE_ENABLED;
    TBBUTTON b[] = {
        { GL_OPEN, CMD_OPEN, E, BTNS_BUTTON, {0}, 0, -1 },
        { 8, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { GL_PREV, CMD_PREV, E, BTNS_BUTTON, {0}, 0, -1 },
        { GL_NEXT, CMD_NEXT, E, BTNS_BUTTON, {0}, 0, -1 },
        { 96, ID_PAGE_EDIT, 0, BTNS_SEP, {0}, 0, -1 },    // the page box and "of N" go over this
        { 8, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { GL_ZOOMOUT, CMD_ZOOM_OUT, E, BTNS_BUTTON, {0}, 0, -1 },
        { GL_ZOOMIN, CMD_ZOOM_IN, E, BTNS_BUTTON, {0}, 0, -1 },
        { 8, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { GL_ACTUAL, CMD_ACTUAL, E, BTNS_BUTTON, {0}, 0, -1 },
        { GL_FITPAGE, CMD_FIT_PAGE, E, BTNS_CHECK, {0}, 0, -1 },
        { GL_FITWIDTH, CMD_FIT_WIDTH, E, BTNS_CHECK, {0}, 0, -1 },
    };
    SendMessageW(g.toolbar, TB_ADDBUTTONSW, sizeof b / sizeof b[0], (LPARAM)b);
    SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
    RECT r;
    SendMessageW(g.toolbar, TB_GETITEMRECT, 4, (LPARAM)&r);
    int eh = 20, ey = r.top + (r.bottom - r.top - eh) / 2;
    g.page_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
                                  r.left + 4, ey, 40, eh, g.toolbar, (HMENU)(INT_PTR)ID_PAGE_EDIT, nullptr, nullptr);
    g.page_of = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, r.left + 48, ey, r.right - r.left - 50,
                                eh, g.toolbar, (HMENU)(INT_PTR)ID_PAGE_OF, nullptr, nullptr);
    edit_base = (WNDPROC)SetWindowLongPtrW(g.page_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
}

void apply_font()
{
    if (g.font) DeleteObject(g.font);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    for (HWND h : { g.page_edit, g.page_of })
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
}

// ---- the frame's layout ------------------------------------------------------------------------------------------------

void layout_chrome()
{
    RECT rc;
    GetClientRect(g.wnd, &rc);
    bool tb = g.show_toolbar && !g.fullscreen, sb = g.show_status && !g.fullscreen;
    ShowWindow(g.toolbar, tb ? SW_SHOW : SW_HIDE);
    ShowWindow(g.status, sb ? SW_SHOW : SW_HIDE);
    int top = 0, bottom = rc.bottom;
    if (tb) {
        SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
        RECT t;
        GetWindowRect(g.toolbar, &t);
        top = t.bottom - t.top;
    }
    if (sb) {
        SendMessageW(g.status, WM_SIZE, 0, 0);
        RECT s;
        GetWindowRect(g.status, &s);
        bottom -= s.bottom - s.top;
        int w = rc.right;
        int parts[3] = { std::max(10, w - 220), std::max(20, w - 80), -1 };
        SendMessageW(g.status, SB_SETPARTS, 3, (LPARAM)parts);
    }
    int left = 0;
    MoveWindow(g.view, left, top, std::max(1, (int)rc.right - left), std::max(1, bottom - top), TRUE);
    update_menu();
}

// ---- the view's window -------------------------------------------------------------------------------------------------

const Link* link_at(int vx, int vy)
{
    Anchor a = anchor_at(vx, vy);
    if (a.page < 0 || a.fx < 0 || a.fx > 1 || a.fy < 0 || a.fy > 1) return nullptr;
    auto it = g.links.find(a.page);
    if (it == g.links.end()) return nullptr;
    const Box& b = g.lay.page[a.page];
    float x, y;
    px_to_page(a.fx * b.w, a.fy * b.h, g.sizes[a.page], g.rot, g.zoom, &x, &y);
    for (const Link& l : it->second)
        if (x >= l.rect.x0 && x <= l.rect.x1 && y >= l.rect.y0 && y <= l.rect.y1) return &l;
    return nullptr;
}

void follow(const Link& l)
{
    if (l.page >= 0) { trace("link page %d", l.page + 1); go_page(l.page, l.y); return; }
    trace("link %s", l.uri.c_str());
    std::string err;
    if (!shell::spawn({ "xdg-open", l.uri }, "", &err)) shell::message_box(g.wnd, "mupf", err, MB_OK | MB_ICONEXCLAMATION);
}

LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (!g.in_layout) relayout(top_anchor());
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_view(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_VSCROLL: case WM_HSCROLL: {
        bool v = msg == WM_VSCROLL;
        SCROLLINFO si = {};
        si.cbSize = sizeof si;
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, v ? SB_VERT : SB_HORZ, &si);
        int pos = v ? g.sy : g.sx, page = v ? g.vh : g.vw;
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= LINE; break;
        case SB_LINEDOWN: pos += LINE; break;
        case SB_PAGEUP: pos -= page - LINE; break;
        case SB_PAGEDOWN: pos += page - LINE; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = 1 << 30; break;
        default: return 0;
        }
        if (v) scroll_to(g.sx, pos);
        else scroll_to(pos, g.sy);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        g.wheel += (short)HIWORD(wp);
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &p);
        while (g.wheel >= WHEEL_DELTA || g.wheel <= -WHEEL_DELTA) {
            bool up = g.wheel > 0;
            g.wheel += up ? -WHEEL_DELTA : WHEEL_DELTA;
            if (LOWORD(wp) & MK_CONTROL) {
                if (g.opened) set_zoom(Z_CUSTOM, up ? zoom_in(g.zoom) : zoom_out(g.zoom), anchor_at(p.x, p.y));
            } else if (LOWORD(wp) & MK_SHIFT) scroll_to(g.sx + (up ? -3 : 3) * LINE, g.sy);
            else scroll_lines((up ? -3 : 3) * LINE);
        }
        update_toolbar();
        return 0;
    }
    case WM_MOUSEHWHEEL:
        scroll_to(g.sx + ((short)HIWORD(wp) > 0 ? 3 : -3) * LINE, g.sy);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        g.panning = true;
        g.pan_moved = false;
        g.pan_at = { (short)LOWORD(lp), (short)HIWORD(lp) };
        g.pan_sx = g.sx;
        g.pan_sy = g.sy;
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (g.panning) {
            int dx = (short)LOWORD(lp) - g.pan_at.x, dy = (short)HIWORD(lp) - g.pan_at.y;
            if (abs(dx) + abs(dy) > 3) g.pan_moved = true;
            if (g.pan_moved) scroll_to(g.pan_sx - dx, g.pan_sy - dy);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g.panning) {
            g.panning = false;
            ReleaseCapture();
            if (!g.pan_moved) {
                const Link* l = link_at((short)LOWORD(lp), (short)HIWORD(lp));
                if (l) { Link copy = *l; follow(copy); }
            }
            update_toolbar();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g.panning = false;
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            LPCWSTR c = g.panning && g.pan_moved ? (LPCWSTR)IDC_SIZEALL : link_at(p.x, p.y) ? (LPCWSTR)IDC_HAND : (LPCWSTR)IDC_ARROW;
            SetCursor(LoadCursorW(nullptr, c));
            return TRUE;
        }
        break;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        bool hscroll = g.lay.width > g.vw;
        switch (wp) {
        case VK_UP: scroll_lines(-LINE); break;
        case VK_DOWN: scroll_lines(LINE); break;
        case VK_LEFT: if (hscroll) scroll_to(g.sx - LINE, g.sy); else next_page(-1); break;
        case VK_RIGHT: if (hscroll) scroll_to(g.sx + LINE, g.sy); else next_page(1); break;
        case VK_PRIOR: next_page(-1); break;
        case VK_NEXT: next_page(1); break;
        case VK_SPACE: scroll_screen(shift ? -1 : 1); break;
        case VK_BACK: scroll_screen(-1); break;
        case VK_HOME: go_page(0); break;
        case VK_END: go_page(g.pages - 1); break;
        case VK_ESCAPE: if (g.fullscreen) set_fullscreen(false); break;
        default:
            (void)ctrl;
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        update_toolbar();
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g.view) layout_chrome();
        return 0;
    case WM_SETFOCUS:
        if (g.view) SetFocus(g.view);
        return 0;
    case WM_INITMENUPOPUP:
        update_menu();
        return 0;
    case WM_COMMAND:
        if (lp == 0 || (HWND)lp == g.toolbar) { command(LOWORD(wp)); return 0; }
        break;
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->code == TTN_GETDISPINFOW) {
            NMTTDISPINFOW* d = (NMTTDISPINFOW*)lp;
            static std::u16string tip;
            tip = W(tip_for((int)n->idFrom));
            d->lpszText = (LPWSTR)tip.c_str();
            return 0;
        }
        break;
    }
    case WM_ENGINE:
        on_engine();
        update_toolbar();
        return 0;
    case WM_THEMECHANGED: case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE:
        apply_font();
        layout_chrome();
        InvalidateRect(g.view, nullptr, FALSE);
        break;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        WCHAR file[4096];
        if (DragQueryFileW(drop, 0, file, sizeof file / sizeof file[0])) open_doc(WuxUtf8(file));
        DragFinish(drop);
        return 0;
    }
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
    fputs("usage: mupf [FILE]\n"
          "  A PDF, XPS, EPUB, CBZ, FB2 or MOBI document, drawn by mupdf. MUPF_TRACE=1 prints what is opened, shown and\n"
          "  rendered on stdout.\n", f);
}

}   // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    bool opts = true;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (opts && a == "--") { opts = false; continue; }
        if (opts && (a == "--help" || a == "-h")) { usage(stdout); return 0; }
        if (opts && a.size() > 1 && a[0] == '-') { fprintf(stderr, "mupf: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
        args.push_back(a);
    }
    g.trace = getenv("MUPF_TRACE") && *getenv("MUPF_TRACE") == '1';
    WuxInit();
    g.res = WuxLoadResources(mupf_res_start, (size_t)(mupf_res_end - mupf_res_start));
    g.accel = LoadAcceleratorsW(g.res, MAKEINTRESOURCEW(IDA_MAIN));
    g.menu = LoadMenuW(g.res, MAKEINTRESOURCEW(IDM_MAIN));
    g.cache = new Cache(CACHE_BUDGET);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = L"mupf";
    RegisterClassW(&wc);
    WNDCLASSW vc = {};
    vc.lpfnWndProc = view_proc;
    vc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    vc.lpszClassName = L"mupf_view";
    RegisterClassW(&vc);

    int x, y, w, h;
    bool maxed;
    load_state(&x, &y, &w, &h, &maxed);
    g.wnd = CreateWindowExW(WS_EX_ACCEPTFILES, L"mupf", L"mupf", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, g.menu,
                            nullptr, nullptr);
    if (!g.wnd) { fprintf(stderr, "mupf: no window\n"); return 1; }
    shell::set_window_icon_key(g.wnd, "pdf");
    make_toolbar();
    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g.wnd,
                               (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);
    g.view = CreateWindowExW(WS_EX_CLIENTEDGE, L"mupf_view", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                             0, 0, 10, 10, g.wnd, (HMENU)(INT_PTR)ID_VIEW, nullptr, nullptr);
    apply_font();
    DragAcceptFiles(g.wnd, TRUE);
    g.eng = new Engine(engine_notify, nullptr);
    layout_chrome();
    update_title();
    update_page_box();
    update_toolbar();
    ShowWindow(g.wnd, maxed ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(g.wnd);
    SetFocus(g.view);
    trace("mapped");
    if (!args.empty()) open_doc(args[0]);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(g.wnd, g.accel, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    delete g.eng;   // ends the thread, an abandoned render with it
    g.eng = nullptr;
    delete g.cache;
    trace("exit");
    return 0;
}
