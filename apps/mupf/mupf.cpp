// mupf: lhde's document viewer (docs/USAGE.md "mupf"), an application on the public entry point (wux/wux.h, shell/shell.h,
// apps/app.mk; docs/APPLICATIONS.md).
//
//   mupf [FILE]
//
// A window in the idiom of XP's programs: the menu bar (File, Edit, View, Go, Help), a toolbar of small buttons, the pages
// on the application workspace colour, the status bar. XP had no PDF viewer of its own; Acrobat Reader 6 (2003) is what
// one looked like there, and where mupf follows it (the page layouts, the keys, the grey around the pages, the black
// frame and shadow of a page) the names say so; the toolbar's order and glyphs are mupf's taste, not a measured layout.
// Under the Longhorn themes the Explorer's header is above it all (shell/lhheader.h: "the Longhorn header" below).
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
#include "explorer/lhbar.h"   // the Explorer's 4051 command bar under the header (docs/APPLICATIONS.md "mupf")
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
const int ID_TOOLBAR = 10, ID_STATUS = 11, ID_VIEW = 12, ID_PAGE_EDIT = 13, ID_PAGE_OF = 14, ID_TREE = 15, ID_FIND_EDIT = 16,
          ID_REBAR = 17, ID_MENUBAND = 18;
// the Contents pane: XP's Explorer bar (a title row with its close button over a tree, a splitter beside it); the sizes
// are mupf's
const int PANE_TITLE = 22, SPLITTER = 4, PANE_MIN = 80, PANE_DEFAULT = 200;

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
    bool password_tried = false;        // the document was opened with a password (a FAILED one asks again)
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
    bool large_header = false;          // View > Large Header: the Explorer's size, else the Small one (sync_header)
    // the Contents pane: the document's outline in a tree
    HWND tree = nullptr;
    bool show_toc = false, splitting = false;
    int toc_w = PANE_DEFAULT;
    std::vector<TocItem> toc;
    std::vector<HTREEITEM> toc_items;   // the tree's row of each outline entry
    int toc_at = -1;                    // the entry the crumbs show (-1: none)
    int toc_sel = -2;                   // the entry selected in the pane (-1: none; -2: not yet)
    // the Longhorn header (shell/lhheader.h) under the Longhorn themes, the toolbar below it; none under the XP themes
    HWND header = nullptr;
    HWND rebar = nullptr;               // under the header: the menu band (the menu bar would lie above the header)
    std::string crumbs_for;             // what the crumbs were made of: the document and the outline entry
    RECT pane = {}, pane_close = {};
    // the search: its hits in page order, each a rectangle a line, the one shown
    HWND find_edit = nullptr;           // the toolbar's Find box (the XP themes; a header without its search box)
    bool header_find = false;           // the Find box is the Longhorn header's search box
    struct Hit { int page = 0, index = 0; std::vector<RectF> rects; };
    struct {
        uint32_t id = 0;
        std::string needle;
        bool done = true;
        int searched = 0, current = -1;
        bool shown_first = false;
        std::vector<Hit> hits;
    } search;
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

// the toolbar is a band on the menu band's row (under the Longhorn header: make_toolbar)
bool in_band() { return g.toolbar && GetParent(g.toolbar) == g.rebar && g.rebar; }
bool bar_drawn() { return in_band() && lhbar::enabled(); }   // lhbar draws the buttons (no TBSTATE_CHECKED: its dither)

bool band_checked(int id) { return (id == CMD_FIT_PAGE && g.zmode == Z_FIT_PAGE) || (id == CMD_FIT_WIDTH && g.zmode == Z_FIT_WIDTH); }


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
    put("view", "contents", g.show_toc);
    put("view", "contents_width", g.toc_w);
    put("view", "large_header", g.large_header);
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
    g.show_toc = state_int(st, "view", "contents", 0) != 0;
    g.toc_w = std::max(PANE_MIN, state_int(st, "view", "contents_width", PANE_DEFAULT));
    g.large_header = state_int(st, "view", "large_header", 0) != 0;
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

void update_header();
void configure_header();
void layout_chrome();
void follow_toc();

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
    follow_toc();
    update_header();
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

// a hit's rectangle in the view: the page's (x, y) and its box
RECT hit_rect(int page, const RectF& r, int x, int y)
{
    double ax, ay, bx, by;
    page_to_px(r.x0, r.y0, g.sizes[page], g.rot, g.zoom, &ax, &ay);
    page_to_px(r.x1, r.y1, g.sizes[page], g.rot, g.zoom, &bx, &by);
    RECT o = { x + (int)floor(std::min(ax, bx)), y + (int)floor(std::min(ay, by)), x + (int)ceil(std::max(ax, bx)),
               y + (int)ceil(std::max(ay, by)) };
    return o;
}

// the search's hits on a page: each multiplied with yellow (a highlighter's: the text stays black), the one shown blended
// with the selection colour instead (mupf's colours; Acrobat 6 selected the hit shown alone)
void paint_hits(int page, int x, int y, const Box& b)
{
    auto& hits = g.search.hits;
    auto it = std::lower_bound(hits.begin(), hits.end(), page, [](const App::Hit& h, int p) { return h.page < p; });
    COLORREF sel = GetSysColor(COLOR_HIGHLIGHT);
    for (; it != hits.end() && it->page == page; ++it) {
        bool current = g.search.current == (int)(it - hits.begin());
        for (const RectF& rf : it->rects) {
            RECT r = hit_rect(page, rf, x, y);
            int x0 = std::max({ 0, (int)r.left, x }), y0 = std::max({ 0, (int)r.top, y });
            int x1 = std::min({ g.bw, (int)r.right, x + b.w }), y1 = std::min({ g.bh, (int)r.bottom, y + b.h });
            for (int yy = y0; yy < y1; yy++) {
                uint32_t* row = g.bits + (size_t)yy * g.bw;
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t p = row[xx];
                    uint32_t r8 = p >> 16 & 0xff, g8 = p >> 8 & 0xff, b8 = p & 0xff;
                    if (current) {
                        r8 = (r8 + GetRValue(sel)) / 2;
                        g8 = (g8 + GetGValue(sel)) / 2;
                        b8 = (b8 + GetBValue(sel)) / 2;
                    } else
                        b8 = b8 * 40 / 255;   // times (255, 255, 40)
                    row[xx] = 0xff000000u | r8 << 16 | g8 << 8 | b8;
                }
            }
        }
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
            paint_hits(p, x, y, b);
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

void fill_toc();
void trace_toc();
bool ask_password(std::string* pw);

void close_doc()
{
    if (g.eng) g.eng->close();
    g.cache->clear();
    g.opened = false;
    g.pages = 0;
    g.sizes.clear();
    g.links.clear();
    g.toc.clear();
    g.search.needle.clear();
    g.search.hits.clear();
    g.search.current = -1;
    g.search.done = true;
    fill_toc();
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

void open_doc(const std::string& path, const std::string& password = std::string())
{
    close_doc();
    char* abs = realpath(path.c_str(), nullptr);
    g.path = abs ? abs : path;
    free(abs);
    g.password_tried = !password.empty();
    g.gen = g.eng->open(g.path, password);
    set_status(0, "Opening...");
    update_title();
    trace("open %s", g.path.c_str());
}

void take_hits(const Event& e);

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
            g.toc = std::move(e.toc);
            fill_toc();
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
            if (e.password) {   // asked for, as Acrobat does, again after a wrong one; Cancel leaves the window empty
                std::string pw;
                if (ask_password(&pw)) open_doc(g.path, pw);
                break;
            }
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
        case Event::HITS: case Event::SEARCH_DONE:
            take_hits(e);
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

// ---- the search --------------------------------------------------------------------------------------------------------

void search_status()
{
    auto& s = g.search;
    char b[128];
    int n = (int)s.hits.size();
    if (s.needle.empty()) b[0] = 0;
    else if (!s.done && n == 0) snprintf(b, sizeof b, "Searching... page %d of %d", std::min(s.searched + 1, g.pages), g.pages);
    else if (!s.done) snprintf(b, sizeof b, "Searching... match %d of %d", s.current + 1, n);
    else if (n == 0) snprintf(b, sizeof b, "No matches were found.");
    else snprintf(b, sizeof b, "Match %d of %d", s.current + 1, n);
    set_status(0, b);
}

// to hit i: its page, scrolled so its first line is in view (a third down the view when it was not)
void show_hit(int i)
{
    auto& s = g.search;
    if (i < 0 || i >= (int)s.hits.size()) return;
    s.current = i;
    const App::Hit& h = s.hits[i];
    if (!is_continuous(g.mode) && h.page != g.current) go_page(h.page, 0, false);
    const Box& b = g.lay.page[h.page];
    if (!b.empty() && !h.rects.empty()) {
        RECT r = hit_rect(h.page, h.rects[0], b.x, b.y);   // in the plane
        int x = g.sx, y = g.sy;
        if (r.top < g.sy || r.bottom > g.sy + g.vh) y = r.top - g.vh / 3;
        if (r.left < g.sx || r.right > g.sx + g.vw) x = r.left - g.vw / 3;
        if (is_continuous(g.mode)) {
            g.sx = x;
            g.sy = y;
            clamp_scroll();
            update_scrollbars();
            g.current = h.page;   // the hit's page, even when the next shows more of itself
            update_page_box();
            request();
            InvalidateRect(g.view, nullptr, FALSE);
        } else scroll_to(x, y);
    }
    InvalidateRect(g.view, nullptr, FALSE);
    search_status();
    trace("match %d of %zu page %d", i + 1, s.hits.size(), h.page + 1);
}

void start_search(const std::string& needle)
{
    auto& s = g.search;
    s.needle = needle;
    s.hits.clear();
    s.current = -1;
    s.searched = 0;
    s.shown_first = false;
    s.done = needle.empty() || !g.opened;
    if (g.opened) s.id = g.eng->search(needle, g.current);
    trace("search %s", needle.c_str());
    search_status();
    InvalidateRect(g.view, nullptr, FALSE);
}

// the Find box shown and focused, its text selected: the header's search box (it may hide behind its magnifier) or the
// toolbar's
void focus_find()
{
    if (g.header_find) { shell::lhheader::open_search(); return; }
    if (!g.find_edit) return;
    if (!g.show_toolbar) { g.show_toolbar = true; layout_chrome(); }
    SetFocus(g.find_edit);
    SendMessageW(g.find_edit, EM_SETSEL, 0, -1);
}

std::string find_text()
{
    HWND box = g.header_find ? shell::lhheader::search_box() : g.find_edit;
    return box ? WuxWindowText(box) : "";
}

// F3, Shift+F3, Return in the box: the next or previous hit; a new word in the box searches for it
void find(int dir)
{
    std::string text = find_text();
    if (text.empty()) { focus_find(); return; }
    auto& s = g.search;
    if (text != s.needle) { start_search(text); return; }
    int n = (int)s.hits.size();
    if (n == 0) {
        if (s.done) MessageBeep(MB_OK);
        return;
    }
    show_hit(((s.current < 0 ? (dir > 0 ? -1 : 0) : s.current) + dir + n) % n);
}

// a page's hits from the engine: in page order among the others; the first to come (the first after the current page) shown
void take_hits(const Event& e)
{
    auto& s = g.search;
    if (e.search != s.id) return;
    s.searched = e.searched;
    if (e.kind == Event::SEARCH_DONE) {
        s.done = true;
        trace("search done %zu", s.hits.size());
        search_status();
        return;
    }
    std::vector<App::Hit> add;
    for (size_t i = 0; i < e.hits.size(); i++) {
        int k = i < e.hit_of.size() ? e.hit_of[i] : 0;
        if (add.empty() || add.back().index != k) {
            App::Hit h;
            h.page = e.page;
            h.index = k;
            add.push_back(h);
        }
        add.back().rects.push_back(e.hits[i]);
    }
    auto at = std::lower_bound(s.hits.begin(), s.hits.end(), e.page, [](const App::Hit& h, int p) { return h.page < p; });
    int pos = (int)(at - s.hits.begin());
    s.hits.insert(at, add.begin(), add.end());
    if (s.current >= pos) s.current += (int)add.size();
    if (!s.shown_first && !add.empty()) {
        s.shown_first = true;
        show_hit(pos);
    } else {
        search_status();
        InvalidateRect(g.view, nullptr, FALSE);
    }
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

// a protected document's password (IDD_PASSWORD): true with the text typed on OK
INT_PTR CALLBACK password_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        std::string t = "'" + base_of(g.path) + "' is protected. Please enter a Document Open Password.";
        SetDlgItemTextW(dlg, IDC_PASSWORD_TEXT, WS(t));
        SetDlgItemTextW(dlg, IDC_PASSWORD_NOTE, g.password_tried ? L"The password is not right. Please try again." : L"");
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetFocus(GetDlgItem(dlg, IDC_PASSWORD));
        trace("password asked%s", g.password_tried ? " again" : "");
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            *(std::string*)GetWindowLongPtrW(dlg, DWLP_USER) = WuxWindowText(GetDlgItem(dlg, IDC_PASSWORD));
            EndDialog(dlg, 1);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, 0); return TRUE; }
        break;
    }
    return FALSE;
}

bool ask_password(std::string* pw)
{
    return DialogBoxParamW(g.res, MAKEINTRESOURCEW(IDD_PASSWORD), g.wnd, password_proc, (LPARAM)pw) == 1;
}

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
    if (!g.rebar) SetMenu(g.wnd, on ? nullptr : g.menu);   // (under the header the menu is the band's)
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
    check(CMD_CONTENTS, g.show_toc);
    check(CMD_LARGE_HEADER, g.large_header);
    enable(CMD_LARGE_HEADER, g.header != nullptr);   // (the XP themes have no header)
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
    if (bar_drawn()) InvalidateRect(g.toolbar, nullptr, TRUE);   // band_checked
    else {
        SendMessageW(g.toolbar, TB_CHECKBUTTON, CMD_FIT_PAGE, MAKELONG(g.zmode == Z_FIT_PAGE, 0));
        SendMessageW(g.toolbar, TB_CHECKBUTTON, CMD_FIT_WIDTH, MAKELONG(g.zmode == Z_FIT_WIDTH, 0));
    }
    EnableWindow(g.page_edit, d);
    if (g.header) shell::lhheader::set_history(d && !g.back_pages.empty(), d && !g.fwd_pages.empty());
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
    case CMD_LARGE_HEADER:
        g.large_header = !g.large_header;
        if (g.header) {
            configure_header();
            shell::lhheader::refit(g.wnd, layout_chrome);
            RedrawWindow(g.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        trace("large header %s", g.large_header ? "on" : "off");
        break;
    case CMD_CONTENTS:
        g.show_toc = !g.show_toc;
        trace("contents %s", g.show_toc ? "shown" : "hidden");
        layout_chrome();
        if (g.show_toc) SetFocus(g.tree);
        else SetFocus(g.view);
        break;
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
    case CMD_FIND: focus_find(); break;
    case CMD_FIND_NEXT: find(1); break;
    case CMD_FIND_PREV: find(-1); break;
    case CMD_ABOUT: DialogBoxParamW(g.res, MAKEINTRESOURCEW(IDD_ABOUT), g.wnd, about_proc, 0); break;
    }
    update_toolbar();
}

// ---- the toolbar -------------------------------------------------------------------------------------------------------

enum { GL_PREV, GL_NEXT, GL_ZOOMOUT, GL_ZOOMIN, GL_ACTUAL, GL_FITPAGE, GL_FITWIDTH, GL_CONTENTS, GL_FIND, GL_COUNT };

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
    case CMD_PREV: return "Previous Page";
    case CMD_NEXT: return "Next Page";
    case CMD_ZOOM_OUT: return "Zoom Out";
    case CMD_ZOOM_IN: return "Zoom In";
    case CMD_ACTUAL: return "Actual Size";
    case CMD_FIT_PAGE: return "Fit Page";
    case CMD_FIT_WIDTH: return "Fit Width";
    case CMD_FIND_NEXT: return "Find Next";
    }
    return "";
}

WNDPROC edit_base = nullptr;

// the page box: Return goes to the page typed; the find box: Return finds (Shift+Return backwards), the box keeping the
// focus for the next; Escape back to the pages
LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN && h == g.find_edit) {
        find(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
        update_toolbar();
        return 0;
    }
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

// the toolbar (mupf's taste: Previous, Next, the page box | Zoom Out, Zoom In | Actual Size, Fit Page, Fit Width | the find
// box and Find Next, as Acrobat 6 had its Find box in the toolbar; under the Longhorn header the header's search box is the
// Find box, and the toolbar ends with Fit Width). Under the XP themes a toolbar of its own at the
// top (CCS_NOMOVEY: it stays where layout_chrome puts it); under the Longhorn header a band on the menu band's row, as the
// Explorer's 4051 command bar (flxp/explorer/explorer.cpp, lhtool): a flat list toolbar in the rebar, no gripper, its band
// header 30 px (4051's gap from Help to the first button), on the folder type's gradient (lhbar) with its frames.
void make_toolbar(bool band)
{
    DWORD style = WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER;
    style |= band ? WS_CLIPSIBLINGS | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NORESIZE | CCS_NOPARENTALIGN : CCS_NOMOVEY;
    g.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr, style, 0, 0, 0, 0, band ? g.rebar : g.wnd,
                                (HMENU)(INT_PTR)ID_TOOLBAR, nullptr, nullptr);
    if (band && lhbar::enabled()) SetWindowTheme(g.toolbar, L"", L"");   // drawn by lhbar on the gradient
    SendMessageW(g.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    if (band) {
        SendMessageW(g.toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
        SendMessageW(g.toolbar, TB_SETMAXTEXTROWS, 1, 0);
        SendMessageW(g.toolbar, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
        SendMessageW(g.toolbar, TB_SETBITMAPSIZE, 0, MAKELONG(16, 16));
    }
    if (!g.glyphs) g.glyphs = make_glyphs();
    SendMessageW(g.toolbar, TB_SETIMAGELIST, 0, (LPARAM)g.glyphs);
    const BYTE E = TBSTATE_ENABLED;
    const BYTE CHECK = band && lhbar::enabled() ? BTNS_BUTTON : BTNS_CHECK;
    TBBUTTON b[] = {
        { GL_PREV, CMD_PREV, E, BTNS_BUTTON, {0}, 0, -1 },
        { GL_NEXT, CMD_NEXT, E, BTNS_BUTTON, {0}, 0, -1 },
        { 96, ID_PAGE_EDIT, 0, BTNS_SEP, {0}, 0, -1 },    // the page box and "of N" go over this
        { 8, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { GL_ZOOMOUT, CMD_ZOOM_OUT, E, BTNS_BUTTON, {0}, 0, -1 },
        { GL_ZOOMIN, CMD_ZOOM_IN, E, BTNS_BUTTON, {0}, 0, -1 },
        { 8, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { GL_ACTUAL, CMD_ACTUAL, E, BTNS_BUTTON, {0}, 0, -1 },
        { GL_FITPAGE, CMD_FIT_PAGE, E, CHECK, {0}, 0, -1 },
        { GL_FITWIDTH, CMD_FIT_WIDTH, E, CHECK, {0}, 0, -1 },
        { 8, 0, 0, BTNS_SEP, {0}, 0, -1 },
        { 150, ID_FIND_EDIT, 0, BTNS_SEP, {0}, 0, -1 },   // the find box goes over this
        { GL_FIND, CMD_FIND_NEXT, E, BTNS_BUTTON, {0}, 0, -1 },
    };
    // the header's search box is the Find box: the toolbar ends with Fit Width
    const int n = (int)(sizeof b / sizeof b[0]) - (band && g.header_find ? 3 : 0);
    // on the bar the gaps are disabled empty buttons, as the Explorer's (a flat separator would draw its etch)
    if (band) for (TBBUTTON& t : b) if (t.fsStyle == BTNS_SEP && !t.idCommand) { t.dwData = t.iBitmap; t.iBitmap = I_IMAGENONE; t.fsStyle = BTNS_BUTTON; }
    SendMessageW(g.toolbar, TB_ADDBUTTONSW, n, (LPARAM)b);
    if (band)
        for (int i = 0; i < n; i++) {
            if (b[i].iBitmap != I_IMAGENONE) continue;
            TBBUTTONINFOW bi = {};
            bi.cbSize = sizeof bi; bi.dwMask = TBIF_BYINDEX | TBIF_SIZE; bi.cx = (WORD)b[i].dwData;
            SendMessageW(g.toolbar, TB_SETBUTTONINFOW, (WPARAM)i, (LPARAM)&bi);
        }
    SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
    RECT r;
    SendMessageW(g.toolbar, TB_GETITEMRECT, 2, (LPARAM)&r);
    int eh = 20, ey = r.top + (r.bottom - r.top - eh) / 2;
    g.page_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
                                  r.left + 4, ey, 40, eh, g.toolbar, (HMENU)(INT_PTR)ID_PAGE_EDIT, nullptr, nullptr);
    g.page_of = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, r.left + 48, ey, r.right - r.left - 50,
                                eh, g.toolbar, (HMENU)(INT_PTR)ID_PAGE_OF, nullptr, nullptr);
    edit_base = (WNDPROC)SetWindowLongPtrW(g.page_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    if (n == (int)(sizeof b / sizeof b[0])) {
        SendMessageW(g.toolbar, TB_GETITEMRECT, n - 2, (LPARAM)&r);
        g.find_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, r.left + 2, ey,
                                      r.right - r.left - 4, eh, g.toolbar, (HMENU)(INT_PTR)ID_FIND_EDIT, nullptr, nullptr);
        SendMessageW(g.find_edit, EM_SETCUEBANNER, FALSE, (LPARAM)L"Find");
        SetWindowLongPtrW(g.find_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    }
    if (g.font) for (HWND h : { g.page_edit, g.page_of, g.find_edit }) SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    if (!band) return;
    // the band: as high as a button, after the menu band on its row
    SIZE sz = {0, 0};
    SendMessageW(g.toolbar, TB_GETMAXSIZE, 0, (LPARAM)&sz);
    RECT item = {0, 0, 0, 0};
    SendMessageW(g.toolbar, TB_GETITEMRECT, 0, (LPARAM)&item);
    REBARBANDINFOW rb = {};
    rb.cbSize = sizeof rb;
    rb.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID | RBBIM_HEADERSIZE;
    rb.fStyle = RBBS_NOGRIPPER | RBBS_USECHEVRON;      // the menu band's row
    rb.hwndChild = g.toolbar;
    rb.wID = ID_TOOLBAR;
    rb.cxHeader = 30;                                  // the Explorer's: 4051's gap after Help
    rb.cxMinChild = (UINT)sz.cx; rb.cyMinChild = (UINT)(item.bottom - item.top); rb.cyChild = rb.cyMinChild; rb.cx = (UINT)sz.cx;
    SendMessageW(g.rebar, RB_INSERTBANDW, 1, (LPARAM)&rb);
    trace("toolbar band %dx%d", (int)sz.cx, (int)(item.bottom - item.top));
}

void destroy_toolbar()
{
    if (!g.toolbar) return;
    if (in_band()) {
        int i = (int)SendMessageW(g.rebar, RB_IDTOINDEX, ID_TOOLBAR, 0);
        if (i >= 0) SendMessageW(g.rebar, RB_DELETEBAND, (WPARAM)i, 0);
    }
    if (GetFocus() == g.find_edit || GetFocus() == g.page_edit) SetFocus(g.view);
    DestroyWindow(g.toolbar);   // its edits with it
    g.toolbar = g.page_edit = g.page_of = g.find_edit = nullptr;
}

void apply_font()
{
    if (g.font) DeleteObject(g.font);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    for (HWND h : { g.page_edit, g.page_of, g.tree, g.find_edit })
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
}

// ---- the Contents pane -------------------------------------------------------------------------------------------------

// the outline into the tree: an item a row, its children under it, expanded when the document says so
void fill_toc()
{
    if (!g.tree) return;
    SendMessageW(g.tree, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    std::vector<HTREEITEM> parents, open;
    g.toc_items.clear();
    g.toc_at = -1;
    g.toc_sel = -2;
    for (size_t i = 0; i < g.toc.size(); i++) {
        const TocItem& t = g.toc[i];
        int level = std::min(t.level, (int)parents.size());
        parents.resize(level);
        std::u16string text = W(t.title);
        TVINSERTSTRUCTW ins = {};
        ins.hParent = parents.empty() ? TVI_ROOT : parents.back();
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = (LPWSTR)text.c_str();
        ins.item.lParam = (LPARAM)i;
        HTREEITEM h = (HTREEITEM)SendMessageW(g.tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
        parents.push_back(h);
        g.toc_items.push_back(h);
        if (t.open) open.push_back(h);
    }
    for (HTREEITEM h : open) SendMessageW(g.tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)h);   // once the children are in
    SendMessageW(g.tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.tree, nullptr, TRUE);
    trace("toc %zu", g.toc.size());
    trace_toc();
}

// the trace of the rows on screen: "tocitem INDEX X Y TITLE", the middle of each row's text in root coordinates
void trace_toc()
{
    if (!g.trace || !g.tree || !IsWindowVisible(g.tree)) return;
    for (HTREEITEM h = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_FIRSTVISIBLE, 0); h;
         h = (HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_NEXTVISIBLE, (LPARAM)h)) {
        RECT r;
        *(HTREEITEM*)&r = h;
        if (!SendMessageW(g.tree, TVM_GETITEMRECT, TRUE, (LPARAM)&r)) continue;
        TVITEMW it = {};
        it.mask = TVIF_PARAM;
        it.hItem = h;
        SendMessageW(g.tree, TVM_GETITEMW, 0, (LPARAM)&it);
        POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
        ClientToScreen(g.tree, &c);
        if (it.lParam >= 0 && (size_t)it.lParam < g.toc.size())
            trace("tocitem %d %d %d %s", (int)it.lParam, (int)c.x, (int)c.y, g.toc[it.lParam].title.c_str());
    }
}

// to outline entry i: its page, the crumbs and the pane showing it (not another entry on the same page)
void go_entry(int i)
{
    if (i < 0 || (size_t)i >= g.toc.size() || g.toc[(size_t)i].page < 0) return;
    g.toc_at = i;
    go_page(g.toc[(size_t)i].page, g.toc[(size_t)i].y);
    update_page_box();    // (go_page did not when the page stayed)
}

void toc_go(HTREEITEM h)
{
    if (!h) return;
    TVITEMW it = {};
    it.mask = TVIF_PARAM;
    it.hItem = h;
    if (!SendMessageW(g.tree, TVM_GETITEMW, 0, (LPARAM)&it) || it.lParam < 0 || (size_t)it.lParam >= g.toc.size()) return;
    trace("toc go %s page %d", g.toc[it.lParam].title.c_str(), g.toc[it.lParam].page + 1);
    g.toc_sel = (int)it.lParam;   // (the row is selected already)
    go_entry((int)it.lParam);
}

// the pane's title row: the name on the face colour and the close button's cross (the two of XP's Explorer bar)
void paint_pane(HDC dc)
{
    if (IsRectEmpty(&g.pane)) return;
    RECT title = { g.pane.left, g.pane.top, g.pane.right, g.pane.top + PANE_TITLE };
    FillRect(dc, &title, GetSysColorBrush(COLOR_3DFACE));
    HGDIOBJ of = SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    RECT tr = { title.left + 6, title.top, g.pane_close.left - 4, title.bottom };
    DrawTextW(dc, L"Contents", -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, of);
    HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNTEXT));
    HGDIOBJ op = SelectObject(dc, pen);
    int cx = (g.pane_close.left + g.pane_close.right) / 2, cy = (g.pane_close.top + g.pane_close.bottom) / 2;
    for (int d = 0; d < 2; d++) {
        MoveToEx(dc, cx - 4 + d, cy - 4, nullptr); LineTo(dc, cx + 4 + d, cy + 4);
        MoveToEx(dc, cx + 3 + d, cy - 4, nullptr); LineTo(dc, cx - 5 + d, cy + 4);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
}

// ---- the Longhorn header -----------------------------------------------------------------------------------------------
// Under the Longhorn themes the window has the Explorer's header (shell/lhheader.h): Back and Forward are mupf's history,
// the breadcrumb the document's name, then the outline's path to the page shown (the entries above the current one:
// "Part II > Chapter 4 > 4.2"), then the leaf "Page N of M"; a segment's drop-down lists the entries beside it and goes
// there (the document's: the top level). The address edit takes a page number. The header's search box is the Find box
// (with header_search off the toolbar's comes back); the favourites star is hidden: mupf keeps no favourites (a choice,
// not Longhorn's: its viewers had no header).

// the outline entry covering page p: the last one in the outline's order that starts on p or before it; -1 when none
int toc_entry_at(int p)
{
    int at = -1;
    for (size_t i = 0; i < g.toc.size(); i++)
        if (g.toc[i].page >= 0 && g.toc[i].page <= p) at = (int)i;
    return at;
}

// entry i and the entries above it, the top first
std::vector<int> toc_chain(int i)
{
    std::vector<int> out;
    for (int j = i, level = i >= 0 ? g.toc[i].level + 1 : 0; j >= 0 && level > 0; j--)
        if (g.toc[j].level < level) { out.insert(out.begin(), j); level = g.toc[j].level; }
    return out;
}

// the entries at entry i's level under its parent (-1: the top level)
std::vector<int> toc_siblings(int parent)
{
    std::vector<int> out;
    int level = parent >= 0 ? g.toc[parent].level + 1 : 0;
    for (size_t j = parent + 1; j < g.toc.size(); j++) {
        if (g.toc[j].level < level) break;
        if (g.toc[j].level == level) out.push_back((int)j);
    }
    return out;
}

int toc_parent(int i)
{
    for (int j = i - 1; j >= 0; j--) if (g.toc[j].level < g.toc[i].level) return j;
    return -1;
}

std::string leaf_text()
{
    char b[64];
    snprintf(b, sizeof b, "Page %d of %d", g.current + 1, g.pages);
    return b;
}

// the Contents pane follows the page: the entry the page is in selected (Acrobat 7's), its parents opened. Of the
// entries that start on the same page the one chosen (a row clicked, a drop-down entry: go_entry) stays.
void follow_toc()
{
    int at = g.opened ? toc_entry_at(g.current) : -1;
    if (at >= 0 && g.toc_at >= 0 && (size_t)g.toc_at < g.toc.size() && g.toc[(size_t)g.toc_at].page == g.toc[(size_t)at].page)
        at = g.toc_at;
    g.toc_at = at;
    if (at == g.toc_sel) return;
    g.toc_sel = at;
    if (g.tree && at >= 0 && (size_t)at < g.toc_items.size()) {
        SendMessageW(g.tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)g.toc_items[(size_t)at]);   // TVC_UNKNOWN: no jump
        trace("toc follows %d %s", at, g.toc[(size_t)at].title.c_str());
    } else if (g.tree) SendMessageW(g.tree, TVM_SELECTITEM, TVGN_CARET, 0);
}

void update_header()
{
    if (!g.header) return;
    if (!g.opened) {
        if (!g.crumbs_for.empty()) { g.crumbs_for.clear(); shell::lhheader::set_crumbs({}); }
        return;
    }
    std::string name = base_of(g.path);
    int at = g.toc_at;
    std::string key = g.path + "\n" + std::to_string(at);
    shell::lhheader::set_address(std::to_string(g.current + 1));
    if (key == g.crumbs_for) {
        shell::lhheader::set_leaf(leaf_text());
    } else {
        g.crumbs_for = key;
        std::vector<shell::lhheader::Crumb> crumbs;
        shell::lhheader::Crumb doc;
        doc.text = name;
        doc.key = "doc";
        doc.icon = "pdf";
        doc.dropdown = !g.toc.empty();
        doc.tip = g.toc.empty() ? "" : "Contents";
        crumbs.push_back(doc);
        for (int i : toc_chain(at)) {
            shell::lhheader::Crumb c;
            c.text = g.toc[(size_t)i].title;
            c.key = "toc:" + std::to_string(i);
            c.tip = "Entries next to " + c.text;
            crumbs.push_back(c);
        }
        shell::lhheader::Crumb leaf;
        leaf.text = leaf_text();
        leaf.key = "page";
        leaf.dropdown = false;
        crumbs.push_back(leaf);
        shell::lhheader::set_crumbs(crumbs);
        std::string shown;
        for (const auto& c : crumbs) shown += (shown.empty() ? "" : " > ") + c.text;
        trace("crumbs %s", shown.c_str());
    }
    shell::lhheader::set_title(g.title.empty() ? name : g.title);
}

// a crumb's drop-down: the entries beside it (the document's crumb: the top level), the one on the path bold; children:
// the entries under it (vision's header_crumb_menu = children)
std::vector<shell::lhheader::Entry> header_entries(int i, bool children)
{
    std::vector<int> chain = toc_chain(g.toc_at);
    // crumb 0 the document, 1..chain.size() the outline's entries
    int entry = i >= 1 && (size_t)i <= chain.size() ? chain[(size_t)i - 1] : -1;
    int parent = children ? entry : entry >= 0 ? toc_parent(entry) : -1;
    if (i == 0 && !children) parent = -1;
    std::vector<int> list = toc_siblings(parent);
    int bold = children ? ((size_t)i < chain.size() ? chain[(size_t)i] : -1) : i == 0 ? (chain.empty() ? -1 : chain[0]) : entry;
    std::vector<shell::lhheader::Entry> out;
    for (int j : list) {
        shell::lhheader::Entry e;
        e.text = g.toc[(size_t)j].title;
        e.key = "toc:" + std::to_string(j);
        e.bold = j == bold;
        out.push_back(e);
        if (out.size() >= 250) break;
    }
    return out;
}

void header_go(const std::string& key)
{
    trace("header go %s", key.c_str());
    if (key == "doc") go_page(0);
    else if (!key.compare(0, 4, "toc:")) go_entry(atoi(key.c_str() + 4));
    update_toolbar();
}

// the history's drop-down (the Explorer's: the places, the nearest first): the pages Back or Forward would go to
void header_history(bool back, int x, int y)
{
    std::vector<int>& from = back ? g.back_pages : g.fwd_pages;
    HMENU m = CreatePopupMenu();
    int n = 0;
    for (int k = (int)from.size() - 1; k >= 0 && n < 12; k--, n++) {
        int p = from[(size_t)k], at = toc_entry_at(p);
        std::string t = "Page " + std::to_string(p + 1) + (at >= 0 ? " - " + g.toc[(size_t)at].title : "");
        AppendMenuW(m, MF_STRING, (UINT)n + 1, WS(t));
    }
    int cmd = n ? TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, x, y, 0, g.wnd, nullptr) : 0;
    DestroyMenu(m);
    for (int k = 0; k < cmd; k++) command(back ? CMD_BACK : CMD_FORWARD);
}

void layout_chrome();

// The header's size: the Small one (4074's Toolbar Size > Small, the file dialog's: one row under the frame's caption,
// which keeps the title) unless View > Large Header asks for the Explorer's (its header_size, else the theme's
// Explorer.navbar_preset, else Medium); flxp.ini's mupf_header_size wins over both. Which mupf should have is not decided:
// the toggle is there to compare them.
void configure_header()
{
    shell::lhheader::configure("mupf_", g.large_header ? "" : "small");
}

LRESULT menu_draw(NMTBCUSTOMDRAW* cd, bool active) { return lhbar::custom_draw(cd, active, nullptr); }

// the header when the session theme wants the Longhorn chrome, none when not (at the start and on a theme switch)
void sync_header()
{
    bool want = shell::lhheader::want_longhorn();
    if (want == (g.header != nullptr)) return;
    if (want) {
        shell::lhheader::set_lhwm(shell::lhheader::lhwm_running());   // under another window manager: the Small size
        configure_header();
        shell::lhheader::hide(shell::lhheader::SHOW_FAVORITES);
        shell::lhheader::Callbacks cb;
        cb.go = header_go;
        cb.entries = header_entries;
        cb.address = [](const std::string& text) {
            int n = atoi(text.c_str());
            if (n >= 1 && n <= g.pages) go_page(n - 1);
            update_toolbar();
        };
        cb.command = [](int id) { command(id == shell::lhheader::TRAVEL_BACK ? CMD_BACK : CMD_FORWARD); };
        cb.history = header_history;
        // the search box is the Find box: Return searches (Shift+Return backwards), Escape clears the search; nothing while
        // typing (a search runs over the whole document)
        cb.filter = [](const std::string&) {};
        cb.done = [](bool keep) {
            if (keep) find(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
            else { start_search(""); SetFocus(g.view); }
            update_toolbar();
        };
        g.header = shell::lhheader::create(g.wnd, cb);
        g.header_find = shell::lhheader::shown(shell::lhheader::SHOW_SEARCH) && shell::lhheader::search_box();
        trace("find box %s", g.header_find ? "header" : "toolbar");
        // the menu below the header and the toolbar on its row: the Explorer's rebar (its styles) with the menu band and,
        // on the same row, the toolbar's band, on the 4051 command bar's gradient (lhbar)
        SetMenu(g.wnd, nullptr);
        g.rebar = CreateWindowExW(WS_EX_TOOLWINDOW, REBARCLASSNAMEW, nullptr,
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | RBS_VARHEIGHT | RBS_BANDBORDERS |
                                  RBS_AUTOSIZE | CCS_NODIVIDER | CCS_NOPARENTALIGN, 0, 0, 0, 0, g.wnd, (HMENU)(INT_PTR)ID_REBAR,
                                  nullptr, nullptr);
        REBARINFO rbi = { sizeof rbi, 0, nullptr };
        SendMessageW(g.rebar, RB_SETBARINFO, 0, (LPARAM)&rbi);
        menuband::create(g.rebar, g.wnd, g.menu, ID_MENUBAND);
        destroy_toolbar();
        make_toolbar(true);
        if (lhbar::enabled()) {
            lhbar::attach(g.rebar);
            menuband::set_custom_draw(menu_draw);
        }
        shell::lhheader::extend_frame(g.wnd);
        shell::lhheader::set_active(shell::lhheader::frame_active(g.wnd, GetForegroundWindow() == g.wnd));
        g.crumbs_for.clear();
        update_header();
        update_toolbar();
    } else {
        shell::lhheader::destroy();
        g.header = nullptr;
        g.header_find = false;
        destroy_toolbar();
        lhbar::detach(g.rebar);
        menuband::set_custom_draw(nullptr);
        DestroyWindow(g.rebar);
        g.rebar = nullptr;
        menuband::forget();
        make_toolbar(false);
        if (!g.fullscreen) SetMenu(g.wnd, g.menu);
        shell::lhheader::unextend_frame(g.wnd);
        trace("header none");
    }
    RedrawWindow(g.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

// ---- the frame's layout ------------------------------------------------------------------------------------------------

void layout_chrome()
{
    RECT rc;
    GetClientRect(g.wnd, &rc);
    bool tb = g.show_toolbar && !g.fullscreen, sb = g.show_status && !g.fullscreen;
    bool band = in_band();
    if (band) {
        int i = (int)SendMessageW(g.rebar, RB_IDTOINDEX, ID_TOOLBAR, 0);
        if (i >= 0) SendMessageW(g.rebar, RB_SHOWBAND, (WPARAM)i, g.show_toolbar);
    } else ShowWindow(g.toolbar, tb ? SW_SHOW : SW_HIDE);
    ShowWindow(g.status, sb ? SW_SHOW : SW_HIDE);
    int top = 0, bottom = rc.bottom;
    if (g.header) {
        ShowWindow(g.header, g.fullscreen ? SW_HIDE : SW_SHOW);
        if (!g.fullscreen) {
            shell::lhheader::relayout(rc.right, shell::lhheader::height());
            top = shell::lhheader::height();
            static int traced = -1;     // the header's rows, on every change (the frame's caption decides them)
            if (top != traced) trace("header %d rows", traced = top);
        }
    }
    if (g.rebar) {
        ShowWindow(g.rebar, g.fullscreen ? SW_HIDE : SW_SHOW);
        if (!g.fullscreen) {
            int h = (int)SendMessageW(g.rebar, RB_GETBARHEIGHT, 0, 0);
            MoveWindow(g.rebar, 0, top, rc.right, h, TRUE);
            top += h;
            static int traced = -1;     // the row's height (the menu band's and the toolbar's)
            if (h != traced) trace("rebar %d rows", traced = h);
        }
    }
    if (tb && !band) {
        SetWindowPos(g.toolbar, nullptr, 0, top, rc.right, 10, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
        SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
        RECT t;
        GetWindowRect(g.toolbar, &t);
        top += t.bottom - t.top;
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
    bool pane = g.show_toc && !g.fullscreen;
    ShowWindow(g.tree, pane ? SW_SHOW : SW_HIDE);
    if (pane) {
        int w = std::max(PANE_MIN, std::min(g.toc_w, (int)rc.right - PANE_MIN));
        g.pane = { 0, top, w, bottom };
        g.pane_close = { w - 4 - 16, top + (PANE_TITLE - 14) / 2, w - 4, top + (PANE_TITLE - 14) / 2 + 14 };
        MoveWindow(g.tree, 0, top + PANE_TITLE, w, std::max(1, bottom - top - PANE_TITLE), TRUE);
        left = w + SPLITTER;
    } else g.pane = g.pane_close = {};
    InvalidateRect(g.wnd, nullptr, TRUE);
    MoveWindow(g.view, left, top, std::max(1, (int)rc.right - left), std::max(1, bottom - top), TRUE);
    update_menu();
    trace_toc();
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
    LRESULT hr;
    if (g.header && shell::lhheader::frame_message(hwnd, msg, wp, lp, &hr, layout_chrome)) return hr;
    switch (msg) {
    case WM_SIZE:
        if (g.view) layout_chrome();
        return 0;
    case WM_SETFOCUS:
        if (g.view) SetFocus(g.view);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_pane(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if ((HWND)wp == hwnd && LOWORD(lp) == HTCLIENT && !IsRectEmpty(&g.pane)) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            if (p.x >= g.pane.right && p.x < g.pane.right + SPLITTER && p.y >= g.pane.top && p.y < g.pane.bottom) {
                SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (PtInRect(&g.pane_close, p)) { command(CMD_CONTENTS); return 0; }
        if (!IsRectEmpty(&g.pane) && p.x >= g.pane.right && p.x < g.pane.right + SPLITTER) {
            g.splitting = true;
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g.splitting) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            g.toc_w = std::max(PANE_MIN, std::min((int)(short)LOWORD(lp), (int)rc.right - PANE_MIN));
            layout_chrome();
        }
        return 0;
    case WM_LBUTTONUP:
        if (g.splitting) { g.splitting = false; ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
        g.splitting = false;
        return 0;
    case WM_INITMENUPOPUP:
        update_menu();
        return 0;
    case WM_COMMAND:
        if (lp == 0 || (HWND)lp == g.toolbar) { command(LOWORD(wp)); return 0; }
        break;
    case WM_MENUSELECT:
        if (g.rebar) menuband::on_menuselect(wp, lp);
        break;
    case WM_SYSCOMMAND:
        if (g.rebar && (wp & 0xfff0) == SC_KEYMENU && menuband::key_menu((WCHAR)lp)) return 0;   // Alt, Alt+F: the band
        break;
    case WM_ACTIVATE:
        if (g.rebar) menuband::on_activate(LOWORD(wp) != WA_INACTIVE);
        break;
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (g.rebar) {
            bool handled;
            LRESULT r = menuband::on_notify(n, &handled);
            if (handled) return r;
        }
        if (n->hwndFrom == g.toolbar && n->code == NM_CUSTOMDRAW && bar_drawn())
            return lhbar::custom_draw((NMTBCUSTOMDRAW*)lp, menuband::active(), band_checked);
        if (n->hwndFrom == g.tree) {
            if (n->code == NM_CLICK) {   // a click on a row goes there, the row already selected or not
                TVHITTESTINFO hit = {};
                GetCursorPos(&hit.pt);
                ScreenToClient(g.tree, &hit.pt);
                HTREEITEM h = (HTREEITEM)SendMessageW(g.tree, TVM_HITTEST, 0, (LPARAM)&hit);
                if (h && (hit.flags & TVHT_ONITEM)) {
                    SendMessageW(g.tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)h);
                    toc_go(h);
                    update_toolbar();
                }
                return 0;
            }
            if (n->code == TVN_SELCHANGEDW && ((NMTREEVIEWW*)lp)->action == TVC_BYKEYBOARD) {
                toc_go(((NMTREEVIEWW*)lp)->itemNew.hItem);
                update_toolbar();
                return 0;
            }
            if (n->code == TVN_KEYDOWN && ((NMTVKEYDOWN*)lp)->wVKey == VK_RETURN) {
                toc_go((HTREEITEM)SendMessageW(g.tree, TVM_GETNEXTITEM, TVGN_CARET, 0));
                SetFocus(g.view);
                return 0;
            }
            if (n->code == TVN_ITEMEXPANDEDW) trace_toc();
        }
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
        if (msg == WM_THEMECHANGED) {
            lhbar::theme_changed();   // the theme's gradient, before a command bar is made for it
            sync_header();   // to or from a Longhorn theme
            if (g.rebar) menuband::fonts_changed();
        }
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
    make_toolbar(false);   // (sync_header puts it on the menu band's row under the Longhorn header)
    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g.wnd,
                               (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);
    g.view = CreateWindowExW(WS_EX_CLIENTEDGE, L"mupf_view", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                             0, 0, 10, 10, g.wnd, (HMENU)(INT_PTR)ID_VIEW, nullptr, nullptr);
    g.tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
                             WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                             0, 0, 10, 10, g.wnd, (HMENU)(INT_PTR)ID_TREE, nullptr, nullptr);
    apply_font();
    DragAcceptFiles(g.wnd, TRUE);
    g.eng = new Engine(engine_notify, nullptr);
    sync_header();
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
        // Alt+Left and Alt+Right (Back, Forward) go on to DefWindowProc as well, which then knows Alt was not pressed alone
        // and does not open the menu bar when it is released (an accelerator's key never reaches it)
        if (m.message == WM_SYSKEYDOWN && (m.wParam == VK_LEFT || m.wParam == VK_RIGHT) && (HIWORD(m.lParam) & KF_ALTDOWN))
            command(m.wParam == VK_LEFT ? CMD_BACK : CMD_FORWARD);
        else if (TranslateAcceleratorW(g.wnd, g.accel, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    delete g.eng;   // ends the thread, an abandoned render with it
    g.eng = nullptr;
    delete g.cache;
    trace("exit");
    return 0;
}
