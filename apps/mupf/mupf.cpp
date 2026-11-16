// mupf: lhde's document viewer (docs/USAGE.md "mupf"), an application on the public entry point (wux/wux.h, shell/shell.h,
// apps/app.mk; docs/APPLICATIONS.md).
//
//   mupf [FILE]
//
// A window in the idiom of XP's programs: the menu bar (File, Edit, View, Go, Help), a toolbar of small buttons, the pages
// on mupdf's grey, the status bar. XP had no PDF viewer of its own; Acrobat Reader 6 (2003) is what one looked like
// there, and where mupf follows it (the page layouts, the keys, the black frame and shadow of a page) the names say so;
// where it follows mupdf (the grey, a page centred in the view) too; the toolbar's order and glyphs are mupf's taste, not
// a measured layout.
// Under the Longhorn themes the Explorer's header is above it all (shell/lhheader.h: "the Longhorn header" below).
//
// The window never waits on mupdf: the engine (engine.h) opens, renders and searches on its thread, and posts what it has;
// the view draws what the cache (layout.h) holds, a page at another zoom scaled until its rendering at this one arrives,
// white where nothing is yet. What is wanted is asked again on every scroll and zoom, so a page scrolled past is dropped
// from the engine's queue (its render aborted) before it costs anything.
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
#include "sys/sys.h"     // sys::watch_fd: the message loop reads the Markdown file's DirWatch (docs/APPLICATIONS.md)
#include "compile.h"
#include "engine.h"
#include "layout.h"
#include "markdown.h"
#include "uifont.h"
#include "resource.h"

WUX_RESOURCES(mupf_res, "apps/mupf/mupf.res");

using namespace mupf;

namespace {

const UINT WM_ENGINE = WM_APP + 1;
const int CACHE_BUDGET = 64 << 20;     // the rendered pages kept (layout.h's Cache)
const int LINE = 40;                   // an arrow key's or a wheel notch's third, in pixels
const int PREFETCH_AHEAD = 2, PREFETCH_BEHIND = 1;
const int ID_TOOLBAR = 10, ID_STATUS = 11, ID_VIEW = 12, ID_PAGE_EDIT = 13, ID_TREE = 15, ID_FIND_EDIT = 16,
          ID_REBAR = 17, ID_MENUBAND = 18, ID_ERRORS = 19;
const int ERROR_LINES = 9;             // the compiler's pane: this many lines of its diagnostics
// the Contents pane: XP's Explorer bar (a title row with its close button over a tree, a splitter beside it); the sizes
// are mupf's
const int PANE_TITLE = 22, SPLITTER = 4, PANE_MIN = 80, PANE_DEFAULT = 200;
// the document's file written: read again once the writes have settled; a read that fails (a file caught half written)
// tried again RELOAD_TRIES times RELOAD_RETRY ms apart
const UINT_PTR TIMER_RELOAD = 1;
const int RELOAD_DELAY = 150, RELOAD_RETRY = 300, RELOAD_TRIES = 3;
// the view's timers: a reflowable document laid out again once the window's size has settled (a drag-resize lays it out
// once, not at every step); the middle button's autoscroll, a step a frame
const UINT_PTR TIMER_REFLOW = 2, TIMER_AUTOSCROLL = 3;
const int REFLOW_DELAY = 120, AUTOSCROLL_TICK = 16;
// mupdf's grey around the pages: mupdf's X11 viewer (the `mupdf` command) clears its window to 0x707070 (measured: its
// window beside a page, 112,112,112), darker than Acrobat 6's application workspace colour (128 under XP's styles)
const uint32_t WORKSPACE = 0xff707070;

// a place in the history: a document, its page and, for a place in another file, where the view was (-1: the page's top)
struct Place { std::string path; int page = 0, sx = -1, sy = -1; };

// Z_FIT_HEIGHT: mupdf's H, the page's height in the view whatever its width (no menu item: Fit Page is that or less)
enum ZoomMode { Z_CUSTOM, Z_FIT_PAGE, Z_FIT_WIDTH, Z_FIT_HEIGHT };

struct App {
    HWND wnd = nullptr, toolbar = nullptr, status = nullptr, view = nullptr;
    HWND page_edit = nullptr;           // the status bar's page number, as an edit while it is typed in (page_nav)
    HFONT status_font = nullptr;
    int nav_pressed = 0;                // the page part's arrow held down: -1 previous, 1 next
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
    std::string password;               // the password it was opened with (a reload opens it with the same)
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
    // a reflowable document (Markdown, EPUB, ...): no page, white from edge to edge, laid out by the engine to the view's
    // width (flow_w points, asked again when the view's width changes: TIMER_REFLOW), always continuous and upright
    bool flow = false, relaying = false;
    float flow_w = 0;
    double flow_frac = 0;               // where the view was before a new layout, a fraction of the document's height
    int mode_before_flow = -1;          // the page layout chosen, given back when a paged document opens
    int current = 0;
    bool in_layout = false;
    // chrome
    bool show_toolbar = true, show_status = true, fullscreen = false;
    bool large_header = false;          // View > Large Header: the Explorer's size, else the Small one (sync_header)
    // flxp.ini's mupf_autoscroll: the middle button's autoscroll, on by default; 0 turns it off (for a TrackPoint whose
    // middle button scrolls in the X server, libinput's button scrolling, and still delivers a click on release)
    bool autoscroll_on = true;
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
    RECT clip = {};     // the paint's rectangle in the view: paint_view draws into the back buffer there and nowhere else
    // history: pages left by a jump
    std::vector<Place> back_pages, fwd_pages;
    // a Markdown file (markdown.h): its title the first H1
    bool markdown = false;
    FontFiles md_font;                  // its body's faces: the theme's UI font
    // every document is followed on disk: a write opens it again, the page, the scroll position and the zoom kept
    shell::DirWatch* watch = nullptr;   // the file's directory (an editor may write a new file and rename it over)
    long long file_stamp = -1;          // the file's mtime and size when read, to tell a change of it from its neighbours'
    bool reload = false;                // the open under way is the same file read again: the pages shown stay until it is in
    int reload_tries = 0;
    bool keep_history = false;          // the next open_doc keeps Back and Forward (a file followed)
    bool restore = false;               // once opened: to restore_at (a reload's view, Back to another file)
    Place restore_at;
    std::string restore_anchor;         // once opened: to this heading's anchor (a link to "other.md#part")
    // the hand: a drag pans; a click on a link follows it
    bool panning = false, pan_moved = false;
    POINT pan_at = {};
    int pan_sx = 0, pan_sy = 0;
    int wheel = 0;
    int count = 0;                      // mupdf's count: the digits typed before g (123g: page 123)
    // the middle button's autoscroll (Firefox's): held and moved, or clicked and left on (sticky) until a click or a key
    bool autoscroll = false, auto_sticky = false, auto_moved = false;
    POINT auto_at = {}, auto_pt = {};   // where it began and where the pointer is, in the view
    DWORD auto_tick = 0;
    double auto_rx = 0, auto_ry = 0;    // the fractions of a pixel not scrolled yet
    // a typesetting source (compile.h): the compiler's PDF shown, its diagnostics in a pane under the pages
    Compiler comp;
    std::string source;                 // the .typ or .tex file ("": the document is what it is)
    std::string comp_out;               // the PDF, its real path
    std::string comp_status;            // what the compiler said last (the status bar's message part)
    HWND errors = nullptr;              // the pane (a read-only edit), shown while the last compile failed
    bool errors_on = false;
    bool trace = false;
    std::string status_text[3], shown_trace, view_trace, scroll_trace;
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
    lhde_ini_set(s, "view", "autoscroll", nullptr);   // (the switch is flxp.ini's mupf_autoscroll: a choice, not a memory)
    put("view", "statusbar", g.show_status);
    put("view", "contents", g.show_toc);
    put("view", "contents_width", g.toc_w);
    put("view", "large_header", g.large_header);
    put("view", "layout", g.mode_before_flow >= 0 ? g.mode_before_flow : g.mode);
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
    // (not mupf.ini's [view] autoscroll, which a build that had it off by default wrote into every mupf.ini it saved)
    g.autoscroll_on = shell::setting_flag("mupf_autoscroll", true);
    int m = state_int(st, "view", "layout", MODE_CONTINUOUS);
    g.mode = m >= MODE_SINGLE && m <= MODE_CONTINUOUS_FACING ? (Mode)m : MODE_CONTINUOUS;
    g.cover = state_int(st, "view", "cover", 0) != 0;
    int z = state_int(st, "view", "zoom_mode", Z_FIT_WIDTH);
    g.zmode = z >= Z_CUSTOM && z <= Z_FIT_HEIGHT ? (ZoomMode)z : Z_FIT_WIDTH;
    g.zoom = clamp_zoom(state_int(st, "view", "zoom", 1000) / 1000.0);
    if (st) lhde_ini_free(st);
}

// ---- the status bar and the title ------------------------------------------------------------------------------------

void set_status(int part, const std::string& text)
{
    if (g.status_text[part] == text) return;
    g.status_text[part] = text;
    // the page part is drawn by the window (page_nav: the arrows around the text)
    if (g.status) SendMessageW(g.status, SB_SETTEXTW, part == 1 ? 1 | SBT_OWNERDRAW : part, part == 1 ? 0 : (LPARAM)WS(text));
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
    follow_toc();
    update_header();
}

// the document's name: the file's, a compiled source's (not its PDF's)
std::string doc_name() { return base_of(g.source.empty() ? g.path : g.source); }

void update_title()
{
    // a Markdown file's title is its first H1 (the page's own name, as a browser shows a README's), else the file's name
    std::string name = !g.source.empty() ? base_of(g.source) : g.markdown && !g.title.empty() ? g.title : base_of(g.path);
    std::string t = g.opened || !g.path.empty() || !g.source.empty() ? name + " - mupf" : "mupf";
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

// the trace of where the view is: "view X Y W H" (the view in root coordinates), "scroll SX SY PW PH" (in the plane)
void trace_where()
{
    if (!g.trace) return;
    POINT o = { 0, 0 };
    ClientToScreen(g.view, &o);
    char v[64], at[64];
    snprintf(v, sizeof v, "%d %d %d %d", (int)o.x, (int)o.y, g.vw, g.vh);
    snprintf(at, sizeof at, "%d %d %d %d", g.sx, g.sy, g.lay.width, g.lay.height);
    if (g.view_trace != v) trace("view %s", v);
    if (g.scroll_trace != at) trace("scroll %s", at);
    g.view_trace = v;
    g.scroll_trace = at;
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
    si.nMax = (g.lay.flow ? g.vw : g.lay.width) - 1;   // a reflowable document never scrolls across: no bar
    si.nPage = g.vw;
    si.nPos = g.lay.flow ? 0 : g.sx;
    SetScrollInfo(g.view, SB_HORZ, &si, TRUE);
    trace_where();
}

// the scroll position within the plane (the pages and their margin: a drag, the wheel or a key stops at their edge); a
// reflowable document's column centred (wider than the view only until it is laid out again for it)
void clamp_scroll()
{
    if (g.lay.flow) g.sx = (g.lay.width - g.vw) / 2;
    g.sx = std::max(0, std::min(g.sx, g.lay.width - g.vw));
    g.sy = std::max(0, std::min(g.sy, g.lay.height - g.vh));
}

// a reflowable document has no page to fit: Fit Width and Fit Page are its text at 100%, a zoom of its own its text larger
// or smaller (laid out again to the width, as a browser's zoom)
void compute_zoom()
{
    if (g.flow) { if (g.zmode != Z_CUSTOM) g.zoom = 1; }
    else if (g.zmode == Z_FIT_WIDTH) g.zoom = fit_width(g.sizes, g.rot, g.mode, g.cover, g.current, g.vw);
    else if (g.zmode == Z_FIT_PAGE) g.zoom = fit_page(g.sizes, g.rot, g.mode, g.cover, g.current, g.vw, g.vh);
    else if (g.zmode == Z_FIT_HEIGHT) g.zoom = fit_page(g.sizes, g.rot, g.mode, g.cover, g.current, 1 << 20, g.vh);   // (no width to fit)
}

// the width in points a reflowable document is laid out at for the view: the view's width, at most GitHub's column
// (md::MAX_WIDTH_PX, scaled by the zoom as a browser scales it), at least the engine's narrowest
float flow_width()
{
    double z = g.zmode == Z_CUSTOM ? g.zoom : 1;
    double col = std::min((double)g.vw, md::MAX_WIDTH_PX * z);
    return (float)std::max((double)FLOW_MIN_PT, col / (z * PX_PER_PT));
}

// the view's width changed for a reflowable document: lay it out again when it has settled
void want_flow()
{
    if (!g.flow || !g.opened || fabsf(flow_width() - g.flow_w) < 0.5f) return;
    SetTimer(g.view, TIMER_REFLOW, REFLOW_DELAY, nullptr);
}

void reflow()
{
    KillTimer(g.view, TIMER_REFLOW);
    if (g.reload) { SetTimer(g.view, TIMER_REFLOW, REFLOW_DELAY, nullptr); return; }   // (laid out once it is in)
    float w = flow_width();
    if (!g.flow || !g.opened || fabsf(w - g.flow_w) < 0.5f) return;
    g.flow_frac = g.lay.height > 0 ? (double)g.sy / g.lay.height : 0;
    g.flow_w = w;
    g.gen = g.eng->layout(w);
    g.relaying = true;   // (no renders asked until its pages come)
    trace("reflow %g", w);
}

// the page under a point of the view and where on it, as fractions of the page box; false when none is
struct Anchor { int page = -1; double fx = 0, fy = 0; int vx = 0, vy = 0; bool centre = false; };
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

void scrolled(bool repaint = true);

// lays the pages out again (a new zoom, mode, size or rotation), the anchor's page point kept where it was in the view (a
// centre anchor: at the view's new centre, so a resize keeps the centre)
void relayout(const Anchor& anchor)
{
    if (g.in_layout) return;
    g.in_layout = true;
    view_size();
    Anchor a = anchor;
    if (a.centre) { a.vx = g.vw / 2; a.vy = g.vh / 2; }
    if (g.opened && a.page >= 0 && !is_continuous(g.mode)) g.current = a.page;
    compute_zoom();
    g.lay = make_layout(g.sizes, g.rot, g.zoom, g.mode, g.cover, g.current, g.vw, g.vh, g.flow);
    update_scrollbars();
    view_size();   // a scroll bar shown or hidden
    if (g.zmode != Z_CUSTOM) compute_zoom();
    g.lay = make_layout(g.sizes, g.rot, g.zoom, g.mode, g.cover, g.current, g.vw, g.vh, g.flow);
    if (a.page >= 0 && a.page < (int)g.lay.page.size() && !g.lay.page[a.page].empty()) {
        const Box& b = g.lay.page[a.page];
        g.sx = b.x + (int)lround(a.fx * b.w) - a.vx;
        g.sy = b.y + (int)lround(a.fy * b.h) - a.vy;
    }
    clamp_scroll();
    update_scrollbars();
    g.in_layout = false;
    want_flow();
    scrolled();
}

// the view moved: the current page, what to render, the page box; repaint: the whole view painted again (false: the caller
// moved the pixels and invalidated what they do not cover)
void scrolled(bool repaint)
{
    if (g.opened && is_continuous(g.mode)) {
        int c = current_page(g.lay, g.sx, g.sy, g.vw, g.vh);
        if (c >= 0) g.current = c;
    }
    update_page_box();
    request();
    if (repaint) InvalidateRect(g.view, nullptr, FALSE);
}

RECT autoscroll_mark_rect();

// the view's pixels moved with the pages (ScrollWindowEx, a copy on the X server; Acrobat's and mupdf's views scroll so), the
// strip uncovered painted; the autoscroll mark stays where it is, so it is painted again there and where the copy took it
void scroll_to(int x, int y)
{
    int ox = g.sx, oy = g.sy;
    g.sx = x;
    g.sy = y;
    clamp_scroll();
    update_scrollbars();
    int dx = ox - g.sx, dy = oy - g.sy;
    if (!g.back || (!dx && !dy) || abs(dx) >= g.vw || abs(dy) >= g.vh) { scrolled(); return; }
    ScrollWindowEx(g.view, dx, dy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
    if (g.autoscroll) {
        RECT m = autoscroll_mark_rect();
        InvalidateRect(g.view, &m, FALSE);
        OffsetRect(&m, dx, dy);
        InvalidateRect(g.view, &m, FALSE);
    }
    scrolled(false);
}

// to page p (y points down it): the page's top at the view's top
void go_page(int p, float y_pt = 0, bool history = true)
{
    if (!g.opened || g.pages <= 0) return;
    p = std::max(0, std::min(p, g.pages - 1));
    if (history && p != g.current) {
        Place here;
        here.path = g.path;
        here.page = g.current;
        g.back_pages.push_back(here);
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
    int top = y_pt > 0 && g.rot == 0 ? b.y + (int)py : b.y - (g.lay.flow ? 0 : PAGE_GAP / 2);
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

// Page Up and Down (and mupdf's , and .): the previous or next page or spread (Acrobat's); a reflowable document has no
// pages to step through (a Markdown file is one tall page, an EPUB's chapter one): a screen up or down, as a browser's
void page_key(int dir)
{
    if (g.lay.flow) scroll_screen(dir);
    else next_page(dir);
}

// Home and End (mupdf's g and G): the first or last page; a reflowable document's top or bottom
void go_end(int dir)
{
    if (!g.opened) return;
    if (g.lay.flow) scroll_to(g.sx, dir < 0 ? 0 : g.lay.height);
    else go_page(dir < 0 ? 0 : g.pages - 1);
}

void set_zoom(ZoomMode m, double z, const Anchor& a)
{
    g.zmode = m;
    if (m == Z_CUSTOM) g.zoom = clamp_zoom(z);
    relayout(a);
    trace("zoom %d%%", (int)lround(g.zoom * 100));
}

Anchor centre_anchor() { return anchor_at(g.vw / 2, is_continuous(g.mode) ? 0 : g.vh / 2); }
Anchor resize_anchor() { Anchor a = anchor_at(g.vw / 2, g.vh / 2); a.centre = true; return a; }
Anchor top_anchor() { return anchor_at(g.vw / 2, 0); }

// ---- rendering ---------------------------------------------------------------------------------------------------------

// what the view needs: the parts of the pages in it that the cache lacks, then the next pages whole (the way the reader
// goes: ahead two, behind one) while the cache has room
void request()
{
    if (!g.opened || !g.eng || g.relaying) return;
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

void fill(int x, int y, int w, int h, uint32_t c)
{
    int x0 = std::max((int)g.clip.left, x), y0 = std::max((int)g.clip.top, y), x1 = std::min((int)g.clip.right, x + w),
        y1 = std::min((int)g.clip.bottom, y + h);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* row = g.bits + (size_t)yy * g.bw;
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

// a tile into the page box (bx, by, bw x bh in the view), scaled from its zoom to the box's (nearest pixel); a stale one (a
// reflowable document's former layout) at the scale of the zoom alone, its lines where they were until the new ones come
void draw_tile(const Tile* t, int bx, int by, int bwid, int bhei, bool same_zoom, bool stale)
{
    if (t->full_w <= 0 || t->full_h <= 0) return;
    bool exact = same_zoom || (!stale && bwid == t->full_w && bhei == t->full_h);   // the box and mupdf's may differ by a pixel
    double kx = exact ? 1 : stale ? g.zoom / t->zoom : (double)bwid / t->full_w;
    double ky = exact ? 1 : stale ? g.zoom / t->zoom : (double)bhei / t->full_h;
    int dx0 = bx + (int)floor(t->x * kx), dy0 = by + (int)floor(t->y * ky);
    int dx1 = bx + (int)ceil((t->x + t->w) * kx), dy1 = by + (int)ceil((t->y + t->h) * ky);
    dx1 = std::min(dx1, bx + bwid);
    dy1 = std::min(dy1, by + bhei);
    int x0 = std::max((int)g.clip.left, dx0), y0 = std::max((int)g.clip.top, dy0), x1 = std::min((int)g.clip.right, dx1),
        y1 = std::min((int)g.clip.bottom, dy1);
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
            int x0 = std::max({ (int)g.clip.left, (int)r.left, x }), y0 = std::max({ (int)g.clip.top, (int)r.top, y });
            int x1 = std::min({ (int)g.clip.right, (int)r.right, x + b.w }), y1 = std::min({ (int)g.clip.bottom, (int)r.bottom, y + b.h });
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

void paint_autoscroll_mark();

// the view's rectangle rc (the paint's): the back buffer drawn there, then copied to the window. A scroll moves what is in
// the window already (scroll_to), and the strip it uncovers is all a step paints.
void paint_view(HDC dc, const RECT& rc)
{
    make_back();
    g.clip = { std::max(0, (int)rc.left), std::max(0, (int)rc.top), std::min(g.bw, (int)rc.right), std::min(g.bh, (int)rc.bottom) };
    if (g.clip.right <= g.clip.left || g.clip.bottom <= g.clip.top) return;
    // mupdf's grey around the pages (WORKSPACE); a reflowable document white from edge to edge, as a browser shows a page
    fill(0, 0, g.bw, g.bh, g.lay.flow ? 0xffffffff : WORKSPACE);
    std::string shown;
    if (g.opened) {
        for (int p : pages_in_view(g.lay, g.sx, g.sy, g.vw, g.vh)) {
            const Box& b = g.lay.page[p];
            int x = b.x - g.sx, y = b.y - g.sy;
            // a page: white, a 1 px black frame, a 2 px shadow below and to the right (Acrobat's); a reflowable
            // document's pages are not pages: no frame, no shadow
            if (!g.lay.flow) {
                fill(x + 2, y + b.h, b.w, 2, 0xff404040);
                fill(x + b.w, y + 2, 2, b.h, 0xff404040);
            }
            fill(x, y, b.w, b.h, 0xffffffff);
            bool exact = false;
            for (const Tile* t : g.cache->tiles_for(p, g.rot, g.zoom)) {
                bool fresh = g.cache->fresh(t), same = fresh && t->same_zoom(g.zoom);
                draw_tile(t, x, y, b.w, b.h, same, !fresh);
                exact = exact || same;
            }
            paint_hits(p, x, y, b);
            if (!g.lay.flow) {
                fill(x - 1, y - 1, b.w + 2, 1, 0xff000000);
                fill(x - 1, y + b.h, b.w + 2, 1, 0xff000000);
                fill(x - 1, y, 1, b.h, 0xff000000);
                fill(x + b.w, y, 1, b.h, 0xff000000);
            }
            if (exact && g.trace) {
                POINT o = { x, y };
                ClientToScreen(g.view, &o);
                char buf[96];
                snprintf(buf, sizeof buf, " %d:%d,%d,%dx%d", p + 1, (int)o.x, (int)o.y, b.w, b.h);
                shown += buf;
            }
        }
    }
    paint_autoscroll_mark();
    BitBlt(dc, g.clip.left, g.clip.top, g.clip.right - g.clip.left, g.clip.bottom - g.clip.top, g.mem, g.clip.left, g.clip.top,
           SRCCOPY);
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

long long file_stamp(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return ((long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec) ^ ((long long)st.st_size << 40);
}

void unwatch_file()
{
    if (!g.watch) return;
    if (g.watch->fd() >= 0) sys::unwatch_fd(g.watch->fd());
    delete g.watch;
    g.watch = nullptr;
    KillTimer(g.wnd, TIMER_RELOAD);
    trace("watch off");
}

// the document's directory followed (DirWatch): an entry written, created or renamed there arms the reload timer,
// which reads the file again when its stamp changed
void watch_file()
{
    unwatch_file();
    size_t slash = g.path.rfind('/');
    if (slash == std::string::npos) return;
    g.watch = new shell::DirWatch;
    if (!g.watch->watch(slash ? g.path.substr(0, slash) : "/") || g.watch->fd() < 0) {
        delete g.watch;
        g.watch = nullptr;
        trace("watch none");
        return;
    }
    sys::watch_fd(g.watch->fd(), [](int, void*) {
        if (g.watch && g.watch->take()) SetTimer(g.wnd, TIMER_RELOAD, RELOAD_DELAY, nullptr);
    }, nullptr);
    trace("watch %s", g.watch->dir().c_str());
}

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
    unwatch_file();
    KillTimer(g.wnd, TIMER_RELOAD);
    g.reload = false;
    g.flow = g.relaying = false;
    g.flow_w = 0;
    KillTimer(g.view, TIMER_REFLOW);
    if (g.mode_before_flow >= 0) { g.mode = (Mode)g.mode_before_flow; g.mode_before_flow = -1; }
    g.current = 0;
    if (!g.keep_history) {
        g.back_pages.clear();
        g.fwd_pages.clear();
    }
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
    g.password = password;
    g.markdown = md::is_markdown(g.path);
    g.file_stamp = file_stamp(g.path);
    // (a reflowable document is laid out to the view at once: the width is asked of every document, used by those)
    view_size();
    g.gen = g.eng->open(g.path, password, flow_width());
    g.cache->set_gen(g.gen);
    set_status(0, "Opening...");
    update_title();
    trace("open %s", g.path.c_str());
}

// the same file again (written on disk, File > Reload, the theme's font changed), the view where it was: the engine opens
// it as a new generation while the pages shown stay, drawn from the cache's tiles (stale: never covering a need, so the new
// document's replace them as they come); nothing is asked of the engine until it is in (relaying). The page, the scroll
// position and the zoom are kept (OPENED: restore), the history, the outline's expansion when it is the same, the search.
void reload_doc()
{
    if (g.path.empty()) return;
    if (!g.opened) { open_doc(g.path, g.password); return; }   // (nothing shown: an open)
    KillTimer(g.wnd, TIMER_RELOAD);
    g.restore = true;
    g.restore_at = { g.path, g.current, g.sx, g.sy };
    g.restore_anchor.clear();
    g.reload = true;
    g.file_stamp = file_stamp(g.path);
    KillTimer(g.view, TIMER_REFLOW);
    view_size();
    g.gen = g.eng->open(g.path, g.password, flow_width());
    g.cache->set_gen(g.gen);
    g.relaying = true;
    trace("reload %s", g.path.c_str());
}

// another document from the history or a link, the history kept; to the place once it is open
void stop_compile();

void open_place(const Place& p, const std::string& anchor = std::string())
{
    if (!g.source.empty() && p.path != g.comp_out) stop_compile();
    g.restore = true;
    g.restore_at = p;
    g.restore_anchor = anchor;
    g.keep_history = true;
    open_doc(p.path);
    g.keep_history = false;
}

// ---- a typesetting source: its compiler's PDF -------------------------------------------------------------------------

// the compiler's diagnostics in the pane under the pages ("": the pane hidden)
void show_errors(const std::string& text)
{
    if (!g.errors) return;
    std::string t;
    for (char c : text) { if (c == '\n') t += '\r'; t += c; }   // (the edit's line ends)
    bool want = !text.empty();
    if (want) SetWindowTextW(g.errors, WS(t));
    if (g.errors_on != want) {
        g.errors_on = want;
        layout_chrome();
    }
}

void stop_compile()
{
    if (g.comp.fd() >= 0) sys::unwatch_fd(g.comp.fd());
    if (g.comp.running()) trace("compiler stopped");
    g.comp.stop();
    g.source.clear();
    g.comp_out.clear();
    g.comp_status.clear();
    show_errors("");
}

// the compiler said something: a compile done opens its PDF (again, the view kept); a failed one shows its diagnostics,
// the last good PDF staying
void on_compiler()
{
    int fd = g.comp.fd();
    std::vector<Compiler::Result> rs = g.comp.take();
    if (g.comp.fd() < 0 && fd >= 0) sys::unwatch_fd(fd);   // (it ended: take closed the pipe)
    std::string prog = Compiler::program(g.comp.kind());
    for (const Compiler::Result& r : rs) {
        switch (r.what) {
        case Compiler::Result::COMPILED: {
            trace("compiled %s", r.summary.c_str());
            show_errors("");
            g.comp_status = prog + ": " + r.summary;
            set_status(0, g.comp_status);
            if (g.path != g.comp_out || (!g.opened && !g.reload)) open_doc(g.comp_out);
            else if (file_stamp(g.comp_out) != g.file_stamp) reload_doc();
            break;
        }
        case Compiler::Result::FAILED:
            trace("compile failed %s", r.summary.c_str());
            show_errors(r.text);
            g.comp_status = prog + ": " + r.summary + (g.opened ? " (the last good document is shown)" : "");
            set_status(0, g.comp_status);
            break;
        case Compiler::Result::ENDED:
            trace("compiler ended %s", r.summary.c_str());
            if (!r.text.empty()) show_errors(r.text);
            g.comp_status = prog + " " + r.summary;
            set_status(0, g.comp_status);
            break;
        }
    }
}

// FILE.typ, FILE.tex: shown as the PDF their compiler makes of them (typst watch, latexmk -pvc: compile.h), into
// mupf-PID in mupf's state directory; true when path is such a source (shown or, without its compiler, said so)
bool open_source(const std::string& path)
{
    Compiler::Kind k = Compiler::kind_of(path);
    if (k == Compiler::NONE) return false;
    stop_compile();
    close_doc();
    char* abs = realpath(path.c_str(), nullptr);
    g.source = abs ? abs : path;
    free(abs);
    g.path.clear();
    char st[4096];
    std::string parent = lhde_state_dir(st, sizeof st) ? st : "/tmp";
    Compiler::sweep(parent, "mupf-");
    std::string dir = parent + "/mupf-" + std::to_string((long)getpid());
    std::string err;
    if (!g.comp.start(k, g.source, dir, &err)) {
        g.comp_status = "Cannot show " + base_of(g.source) + ": " + err;
        set_status(0, g.comp_status);
        trace("compile none %s", err.c_str());
        update_title();
        return true;
    }
    char* real = realpath(dir.c_str(), nullptr);
    g.comp_out = (real ? std::string(real) : dir) + "/" + base_of(g.comp.output());
    free(real);
    sys::watch_fd(g.comp.fd(), [](int, void*) { on_compiler(); }, nullptr);
    g.comp_status = std::string(Compiler::program(k)) + ": compiling...";
    set_status(0, g.comp_status);
    trace("compile %s %s", Compiler::program(k), g.comp_out.c_str());
    update_title();
    return true;
}

// a file to show (the command line, File > Open, a drop): a source through its compiler, anything else as it is
void open_file(const std::string& path)
{
    if (open_source(path)) return;
    stop_compile();
    open_doc(path);
}

// to a place: in this document at once, another one opened
void go_place(const Place& p)
{
    if (p.path != g.path) { open_place(p); return; }
    if (p.sy >= 0) scroll_to(p.sx, p.sy);
    else go_page(p.page, 0, false);
}

// where the view is, for the history: a page in this document; in another one also the scroll position
Place here_place(bool position)
{
    Place p;
    p.path = g.path;
    p.page = g.current;
    if (position) { p.sx = g.sx; p.sy = g.sy; }
    return p;
}

void take_hits(const Event& e);
void start_search(const std::string& needle);

// a reflowable document laid out again at the view's new width: its pages and outline anew, the view at the same fraction
// of it; the tiles of the former layout drawn until the new ones come; the search done again
void relaid(Event& e)
{
    g.relaying = false;
    g.cache->set_gen(g.gen);
    g.pages = e.pages;
    g.sizes = e.sizes;
    // the same entries (a Markdown file's headings): the tree as it is, expanded as it was; their places are new
    bool same = e.toc.size() == g.toc.size();
    for (size_t i = 0; same && i < e.toc.size(); i++) same = e.toc[i].title == g.toc[i].title && e.toc[i].level == g.toc[i].level;
    g.toc = std::move(e.toc);
    g.flow_w = e.flow_w;
    g.links.clear();
    if (!same) fill_toc();
    g.current = std::min(g.current, g.pages - 1);
    Anchor none;
    relayout(none);
    g.sy = (int)lround(g.flow_frac * g.lay.height);
    clamp_scroll();
    update_scrollbars();
    scrolled();
    trace("relaid %d pages %g", g.pages, g.flow_w);
    if (!g.search.needle.empty()) start_search(g.search.needle);
}

void on_engine()
{
    Event e;
    bool sizes_changed = false, reload = false;
    while (g.eng->take(e)) {
        if (e.gen != g.gen) { delete e.tile; continue; }
        switch (e.kind) {
        case Event::OPENED:
            if (e.relaid) { relaid(e); break; }
            reload = g.reload;
            g.reload = g.relaying = false;
            g.reload_tries = 0;
            g.opened = true;
            g.pages = e.pages;
            g.sizes = e.sizes;
            g.title = e.title;
            g.format = e.format;
            if (reload) {   // the same entries: the tree as it is, expanded as it was
                bool same = e.toc.size() == g.toc.size();
                for (size_t i = 0; same && i < e.toc.size(); i++) same = e.toc[i].title == g.toc[i].title && e.toc[i].level == g.toc[i].level;
                g.toc = std::move(e.toc);
                if (!same) fill_toc();
                g.links.clear();   // (the stale tiles stay behind until the new renders replace them)
            } else {
                g.toc = std::move(e.toc);
                fill_toc();
            }
            g.markdown = e.markdown;
            g.flow = e.reflowable;
            g.flow_w = e.flow_w;
            if (g.flow) {   // a column of text: continuous and upright, the page layout chosen kept for the next PDF
                if (g.mode != MODE_CONTINUOUS && g.mode_before_flow < 0) g.mode_before_flow = g.mode;
                g.mode = MODE_CONTINUOUS;
                g.rot = 0;
            }
            // a reload: at the page it was on (a Single Page layout lays that page out alone)
            g.current = reload ? std::max(0, std::min(g.restore_at.page, g.pages - 1)) : 0;
            g.sx = g.sy = 0;
            set_status(0, g.source.empty() ? "" : g.comp_status);
            trace("opened %d pages %s", g.pages, g.format.c_str());
            for (size_t i = 0; g.trace && i < g.toc.size(); i++)   // the outline: where each entry goes, in points
                trace("tocentry %zu %d %d %g %s", i, g.toc[i].level, g.toc[i].page + 1, g.toc[i].y, g.toc[i].title.c_str());
            update_title();
            {
                Anchor none;
                relayout(none);
                scroll_to((g.lay.width - g.vw) / 2, 0);   // a page wider than the view: its middle (mupdf centres it)
            }
            if (g.flow) trace("flow %g", g.flow_w);   // laid out to the view: this many points wide
            if (!g.watch && g.source.empty()) watch_file();   // (a compiler's PDF: opened again when it says so)
            if (g.restore) {
                g.restore = false;
                std::string anchor = g.restore_anchor;
                g.restore_anchor.clear();
                int entry = -1;
                for (size_t i = 0; !anchor.empty() && i < g.toc.size(); i++)
                    if (md::slug(g.toc[i].title) == anchor) { entry = (int)i; break; }
                if (entry >= 0) {
                    g.toc_at = entry;
                    go_page(g.toc[(size_t)entry].page, g.toc[(size_t)entry].y, false);
                } else if (g.restore_at.sy >= 0) scroll_to(g.restore_at.sx, g.restore_at.sy);
                else if (g.restore_at.page > 0) go_page(g.restore_at.page, 0, false);
                trace("restored %d,%d page %d", g.sx, g.sy, g.current + 1);
            }
            if (reload && !g.search.needle.empty()) {   // the hits found again, the view left where it is
                start_search(g.search.needle);
                g.search.shown_first = true;
            }
            break;
        case Event::FAILED: {
            if (g.reload) {   // the file caught half written, or broken now: the pages as they were, tried again
                trace("reload failed %s", e.error.c_str());
                if (++g.reload_tries <= RELOAD_TRIES) SetTimer(g.wnd, TIMER_RELOAD, RELOAD_RETRY, nullptr);
                else {
                    g.reload = false;   // (relaying stays: the next write opens it again)
                    g.reload_tries = 0;
                    set_status(0, "The document could not be read again: " + e.error);
                }
                break;
            }
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
            for (const Link& l : e.links)   // a page's links: their rectangles in points and where they go
                trace("linkrect %d %g %g %g %g %s", e.page + 1, l.rect.x0, l.rect.y0, l.rect.x1, l.rect.y1,
                      l.page >= 0 ? ("page " + std::to_string(l.page + 1)).c_str() : l.uri.c_str());
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
    ofn.lpstrFilter = L"All Documents (*.pdf, *.xps, *.oxps, *.epub, *.cbz, *.fb2, *.mobi, *.md, *.typ, *.tex)\0*.pdf;*.xps;*.oxps;*.epub;*.cbz;*.fb2;*.mobi;*.md;*.markdown;*.mkd;*.typ;*.tex\0"
                      L"Adobe PDF Files (*.pdf)\0*.pdf\0XPS Documents (*.xps, *.oxps)\0*.xps;*.oxps\0"
                      L"Markdown Files (*.md, *.markdown, *.mkd)\0*.md;*.markdown;*.mkd\0"
                      L"Typst and LaTeX Sources (*.typ, *.tex)\0*.typ;*.tex\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file / sizeof file[0];
    ofn.lpstrTitle = L"Open";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    open_file(WuxUtf8(file));
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
    enable(CMD_COVER, is_facing(g.mode) && !g.flow);
    for (UINT id : { CMD_SINGLE, CMD_CONTINUOUS, CMD_FACING, CMD_CONT_FACING }) enable(id, !g.flow);   // (a column of text)
    check(CMD_FIT_PAGE, g.zmode == Z_FIT_PAGE);
    check(CMD_FIT_WIDTH, g.zmode == Z_FIT_WIDTH);
    check(CMD_FULLSCREEN, g.fullscreen);
    bool d = g.opened;
    for (UINT id : { CMD_CLOSE, CMD_RELOAD, CMD_PROPERTIES, CMD_FIRST, CMD_LAST, CMD_GOTO, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ACTUAL,
                     CMD_FIT_PAGE, CMD_FIT_WIDTH, CMD_ROT_CW, CMD_ROT_CCW })
        enable(id, d);
    enable(CMD_ROT_CW, d && !g.flow);
    enable(CMD_ROT_CCW, d && !g.flow);
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
    for (int id : { CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ACTUAL, CMD_FIT_PAGE, CMD_FIT_WIDTH }) en(id, d);
    if (bar_drawn()) InvalidateRect(g.toolbar, nullptr, TRUE);   // band_checked
    else {
        SendMessageW(g.toolbar, TB_CHECKBUTTON, CMD_FIT_PAGE, MAKELONG(g.zmode == Z_FIT_PAGE, 0));
        SendMessageW(g.toolbar, TB_CHECKBUTTON, CMD_FIT_WIDTH, MAKELONG(g.zmode == Z_FIT_WIDTH, 0));
    }
    if (g.status) InvalidateRect(g.status, nullptr, FALSE);   // the page part's arrows, enabled or not
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
    case CMD_CLOSE: stop_compile(); close_doc(); g.path.clear(); update_title(); break;
    case CMD_PROPERTIES:
        if (g.opened) shell::properties(g.wnd, { g.source.empty() ? g.path : g.source });
        break;
    case CMD_EXIT: PostMessageW(g.wnd, WM_CLOSE, 0, 0); break;
    case CMD_RELOAD: reload_doc(); break;
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
        // the keys stay with the pages: the tree takes the focus when it is clicked (or tabbed to), not when it is shown
        if (!g.show_toc && GetFocus() == g.tree) SetFocus(g.view);
        break;
    case CMD_ZOOM_IN: if (g.opened) set_zoom(Z_CUSTOM, zoom_in(g.zoom), centre_anchor()); break;
    case CMD_ZOOM_OUT: if (g.opened) set_zoom(Z_CUSTOM, zoom_out(g.zoom), centre_anchor()); break;
    case CMD_ACTUAL: if (g.opened) set_zoom(Z_CUSTOM, 1.0, centre_anchor()); break;
    case CMD_FIT_PAGE: if (g.opened) { set_zoom(Z_FIT_PAGE, 0, top_anchor()); go_page(g.current, 0, false); } break;
    case CMD_FIT_WIDTH: if (g.opened) set_zoom(Z_FIT_WIDTH, 0, top_anchor()); break;
    case CMD_SINGLE: case CMD_CONTINUOUS: case CMD_FACING: case CMD_CONT_FACING:
        if (!g.flow) set_mode((Mode)(id - CMD_SINGLE));
        break;
    case CMD_COVER: if (!g.flow) { g.cover = !g.cover; set_mode(g.mode); } break;
    case CMD_ROT_CW: if (g.opened && !g.flow) rotate(90); break;
    case CMD_ROT_CCW: if (g.opened && !g.flow) rotate(-90); break;
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
            Place p = g.back_pages.back();
            g.back_pages.pop_back();
            g.fwd_pages.push_back(here_place(p.path != g.path));
            go_place(p);
        }
        break;
    case CMD_FORWARD:
        if (!g.fwd_pages.empty()) {
            Place p = g.fwd_pages.back();
            g.fwd_pages.pop_back();
            g.back_pages.push_back(here_place(p.path != g.path));
            go_place(p);
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

enum { GL_PREV, GL_NEXT, GL_PREV_OFF, GL_NEXT_OFF, GL_ZOOMOUT, GL_ZOOMIN, GL_ACTUAL, GL_FITPAGE, GL_FITWIDTH, GL_CONTENTS, GL_FIND, GL_COUNT };

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
    case GL_PREV: case GL_NEXT: case GL_PREV_OFF: case GL_NEXT_OFF: {
        bool off = i == GL_PREV_OFF || i == GL_NEXT_OFF;
        op = pen(off ? RGB(150, 150, 150) : dark, 1); ob = brush(off ? RGB(190, 190, 190) : blue);
        Ellipse(dc, ox + 0, 0, ox + 16, 16);
        done(op, ob);
        op = pen(white, 1); ob = brush(white);
        if (i == GL_PREV || i == GL_PREV_OFF) { POINT t[3] = { { ox + 10, 4 }, { ox + 10, 11 }, { ox + 5, 7 } }; Polygon(dc, t, 3); }
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
            SetFocus(g.view);   // (the edit hides: WM_KILLFOCUS)
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
    if (msg == WM_KILLFOCUS && h == g.page_edit) {
        ShowWindow(h, SW_HIDE);   // the page part's text again
        InvalidateRect(g.status, nullptr, FALSE);
    }
    return CallWindowProcW(edit_base, h, msg, wp, lp);
}

// the toolbar (mupf's taste: Zoom Out, Zoom In | Actual Size, Fit Page, Fit Width | the find box and Find Next, as Acrobat 6
// had its Find box in the toolbar; the pages are the status bar's (page_nav); under the Longhorn header the header's search box is the
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
    if (n == (int)(sizeof b / sizeof b[0])) {
        SendMessageW(g.toolbar, TB_GETITEMRECT, n - 2, (LPARAM)&r);
        int eh = 20, ey = r.top + (r.bottom - r.top - eh) / 2;
        g.find_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, r.left + 2, ey,
                                      r.right - r.left - 4, eh, g.toolbar, (HMENU)(INT_PTR)ID_FIND_EDIT, nullptr, nullptr);
        SendMessageW(g.find_edit, EM_SETCUEBANNER, FALSE, (LPARAM)L"Find");
        if (!edit_base) edit_base = (WNDPROC)GetWindowLongPtrW(g.find_edit, GWLP_WNDPROC);
        SetWindowLongPtrW(g.find_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
        if (g.font) SendMessageW(g.find_edit, WM_SETFONT, (WPARAM)g.font, TRUE);
    }
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
    rb.cxMinChild = (UINT)sz.cx; rb.cyMinChild = (UINT)(item.bottom - item.top); rb.cyChild = rb.cyMinChild;
    // the row's room left after the menu band (the rebar gives extra room to the last band wider than the first band's
    // minimum, else to the first: a toolbar narrower than the menu would be pushed to the right edge)
    rb.cx = 0x7fff;
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
    if (GetFocus() == g.find_edit) SetFocus(g.view);
    DestroyWindow(g.toolbar);   // its edit with it
    g.toolbar = g.find_edit = nullptr;
}

void apply_font()
{
    if (g.font) DeleteObject(g.font);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    for (HWND h : { g.tree, g.find_edit })
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    // the status bar the style's status font, as the Explorer's (its page part and the page edit too)
    HFONT sf = CreateFontIndirectW(&ncm.lfStatusFont);
    for (HWND h : { g.status, g.page_edit })
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)sf, TRUE);
    if (g.status_font) DeleteObject(g.status_font);
    g.status_font = sf;
}

// Markdown's body in the theme's UI font (the message font's family, as the dialogs' text); a change of it lays an open
// Markdown file out again
void apply_md_font()
{
    if (!g.eng) return;
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    FontFiles f = ui_font_files(WuxUtf8(ncm.lfMessageFont.lfFaceName));
    if (f == g.md_font) return;
    g.md_font = f;
    g.eng->set_font(f);
    trace("markdown font %s", f.family.empty() ? "(sans-serif)" : f.family.c_str());
    if (g.markdown && g.opened) reload_doc();
}

// ---- the status bar's page part ----------------------------------------------------------------------------------------
// The page part of the status bar is the page navigation: [<] Page N of M [>], the arrows small buttons (the previous and
// the next page or spread, greyed at the ends), a click on the text the page number as an edit (Return goes there, Escape
// or a click elsewhere leaves it). Longhorn's and XP's viewers had no such part (Acrobat 6 had its page box and arrows in
// a toolbar at the bottom of the window): the layout is mupf's choice, the arrows mupf's glyphs.
const int NAV_ARROW = 16, NAV_PAD = 2;

// the part's arrows and text in the status bar's client coordinates; false without a status bar or a document
bool nav_rects(RECT* prev, RECT* text, RECT* next)
{
    RECT r;
    if (!g.status || !IsWindowVisible(g.status) || !SendMessageW(g.status, SB_GETRECT, 1, (LPARAM)&r)) return false;
    int cy = (r.top + r.bottom) / 2;
    *prev = { r.left + NAV_PAD, cy - NAV_ARROW / 2, r.left + NAV_PAD + NAV_ARROW, cy + NAV_ARROW / 2 };
    *next = { r.right - NAV_PAD - NAV_ARROW, cy - NAV_ARROW / 2, r.right - NAV_PAD, cy + NAV_ARROW / 2 };
    *text = { prev->right + NAV_PAD, r.top, next->left - NAV_PAD, r.bottom };
    return true;
}

void trace_nav()
{
    RECT p, t, n;
    if (!g.trace || !g.opened || !nav_rects(&p, &t, &n)) return;
    // from the window's top left corner (its X window's, the menu bar under the XP themes included: a script adds where
    // that is on the screen), which a move of the window does not change
    RECT wr;
    GetWindowRect(g.wnd, &wr);
    for (RECT* r : { &p, &t, &n }) {
        MapWindowPoints(g.status, nullptr, (POINT*)r, 2);
        OffsetRect(r, -wr.left, -wr.top);
    }
    char b[128];
    snprintf(b, sizeof b, "pagenav %d %d %d %d %d %d", (int)(p.left + p.right) / 2, (int)(p.top + p.bottom) / 2,
             (int)(t.left + t.right) / 2, (int)(t.top + t.bottom) / 2, (int)(n.left + n.right) / 2, (int)(n.top + n.bottom) / 2);
    static std::string last;
    if (last != b) { last = b; trace("%s", b); }
}

// WM_DRAWITEM of the page part
void draw_nav(const DRAWITEMSTRUCT* di)
{
    RECT p, t, n;
    if (!nav_rects(&p, &t, &n)) return;
    HDC dc = di->hDC;
    bool on = g.opened;
    bool can_prev = on && step_page(-1) >= 0, can_next = on && step_page(1) >= 0;
    if (on) {
        ImageList_Draw(g.glyphs, can_prev ? GL_PREV : GL_PREV_OFF, dc, p.left + (g.nav_pressed < 0), p.top + (g.nav_pressed < 0), ILD_NORMAL);
        ImageList_Draw(g.glyphs, can_next ? GL_NEXT : GL_NEXT_OFF, dc, n.left + (g.nav_pressed > 0), n.top + (g.nav_pressed > 0), ILD_NORMAL);
    }
    if (g.page_edit && IsWindowVisible(g.page_edit)) return;
    HGDIOBJ of = SelectObject(dc, g.status_font ? (HGDIOBJ)g.status_font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    std::u16string w = W(g.status_text[1]);
    DrawTextW(dc, (LPCWSTR)w.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
    trace_nav();
}

// a click on the text: the page number as an edit over it
void edit_page_number()
{
    RECT p, t, n;
    if (!g.opened || !nav_rects(&p, &t, &n)) return;
    if (!g.page_edit) {
        g.page_edit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 10, 10,
                                      g.status, (HMENU)(INT_PTR)ID_PAGE_EDIT, nullptr, nullptr);
        if (!edit_base) edit_base = (WNDPROC)GetWindowLongPtrW(g.page_edit, GWLP_WNDPROC);
        SetWindowLongPtrW(g.page_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
        if (g.status_font) SendMessageW(g.page_edit, WM_SETFONT, (WPARAM)g.status_font, FALSE);
    }
    int h = std::min((int)(t.bottom - t.top) - 2, 18), w = std::min((int)(t.right - t.left), 48);
    MoveWindow(g.page_edit, (t.left + t.right - w) / 2, (t.top + t.bottom - h) / 2, w, h, FALSE);
    SetWindowTextW(g.page_edit, WS(std::to_string(g.current + 1)));
    ShowWindow(g.page_edit, SW_SHOW);
    SetFocus(g.page_edit);
    SendMessageW(g.page_edit, EM_SETSEL, 0, -1);
    InvalidateRect(g.status, nullptr, FALSE);
    trace("page edit");
}

WNDPROC status_base = nullptr;

// the status bar's clicks on the page part
LRESULT CALLBACK status_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT p, t, n;
    POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        if (g.opened && nav_rects(&p, &t, &n)) {
            InflateRect(&p, NAV_PAD, NAV_PAD);
            InflateRect(&n, NAV_PAD, NAV_PAD);
            int dir = PtInRect(&p, pt) ? -1 : PtInRect(&n, pt) ? 1 : 0;
            if (dir && step_page(dir) >= 0) {
                g.nav_pressed = dir;
                SetCapture(h);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&t, pt)) { edit_page_number(); return 0; }
        }
        break;
    case WM_LBUTTONUP:
        if (g.nav_pressed) {
            int dir = g.nav_pressed;
            g.nav_pressed = 0;
            ReleaseCapture();
            nav_rects(&p, &t, &n);
            RECT r = dir < 0 ? p : n;
            InflateRect(&r, NAV_PAD, NAV_PAD);
            if (PtInRect(&r, pt)) {
                trace("pagenav %s", dir < 0 ? "previous" : "next");
                command(dir < 0 ? CMD_PREV : CMD_NEXT);
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (g.nav_pressed) { g.nav_pressed = 0; InvalidateRect(h, nullptr, FALSE); }
        break;
    case WM_COMMAND:   // the page edit's notifications: nothing to do
        if ((HWND)lp == g.page_edit) return 0;
        break;
    }
    return CallWindowProcW(status_base, h, msg, wp, lp);
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
    trace("toc go %s page %d y %g", g.toc[it.lParam].title.c_str(), g.toc[it.lParam].page + 1, g.toc[it.lParam].y);
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
    // a Markdown file is one tall page: of the headings on the page shown, those above a point a quarter down the view
    float top = 1e30f;
    if (g.markdown && p == g.current && p >= 0 && (size_t)p < g.lay.page.size() && !g.lay.page[p].empty() && g.rot == 0) {
        float x;
        px_to_page(0, g.sy + g.vh / 4 - g.lay.page[p].y, g.sizes[p], g.rot, g.zoom, &x, &top);
        if (g.sy <= g.lay.page[p].y) top = -1;   // the page's top in view: the first heading
    }
    int at = -1;
    for (size_t i = 0; i < g.toc.size(); i++)
        if (g.toc[i].page >= 0 && (g.toc[i].page < p || (g.toc[i].page == p && (g.toc[i].y <= top || at < 0)))) at = (int)i;
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
    if (!g.markdown && at >= 0 && g.toc_at >= 0 && (size_t)g.toc_at < g.toc.size() &&
        g.toc[(size_t)g.toc_at].page == g.toc[(size_t)at].page)
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
    std::string name = doc_name();
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
    std::vector<Place>& from = back ? g.back_pages : g.fwd_pages;
    HMENU m = CreatePopupMenu();
    int n = 0;
    for (int k = (int)from.size() - 1; k >= 0 && n < 12; k--, n++) {
        const Place& pl = from[(size_t)k];
        int p = pl.page, at = pl.path == g.path ? toc_entry_at(p) : -1;
        std::string t = pl.path != g.path ? base_of(pl.path)
                                          : "Page " + std::to_string(p + 1) + (at >= 0 ? " - " + g.toc[(size_t)at].title : "");
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
        if (g.page_edit && GetFocus() == g.page_edit) SetFocus(g.view);   // (the edit hides) the parts moved
    }
    // the compiler's diagnostics: across the window under the pages, above the status bar
    bool errs = g.errors && g.errors_on && !g.fullscreen;
    if (g.errors) ShowWindow(g.errors, errs ? SW_SHOWNA : SW_HIDE);
    if (errs) {
        TEXTMETRICW tm = {};
        HDC dc = GetDC(g.errors);
        HGDIOBJ of = SelectObject(dc, GetStockObject(ANSI_FIXED_FONT));
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, of);
        ReleaseDC(g.errors, dc);
        int h = std::min(ERROR_LINES * (int)std::max(8L, (long)tm.tmHeight) + 6, std::max(0, (bottom - top) / 2));
        bottom -= h;
        MoveWindow(g.errors, 0, bottom, rc.right, h, TRUE);
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
    trace_nav();   // (the status bar may have moved without being drawn again)
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

// a link's file on this machine: a file: URI's path, or a relative reference (no scheme) from the document's directory,
// %XX decoded; the fragment (after '#') apart. "" for a web address.
std::string local_target(const std::string& uri, std::string* fragment)
{
    std::string u = uri;
    size_t hash = u.find('#');
    if (hash != std::string::npos) { *fragment = u.substr(hash + 1); u.resize(hash); }
    bool file = !u.compare(0, 7, "file://");
    if (file) u = u.substr(7);
    else {
        size_t colon = u.find(':'), slash = u.find('/');
        if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) return "";   // a scheme
    }
    std::string path;
    for (size_t i = 0; i < u.size(); i++) {
        if (u[i] == '%' && i + 2 < u.size() && isxdigit((unsigned char)u[i + 1]) && isxdigit((unsigned char)u[i + 2])) {
            path += (char)strtol(u.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else path += u[i];
    }
    if (path.empty()) return "";
    if (path[0] != '/') {
        size_t slash = g.path.rfind('/');
        path = (slash == std::string::npos ? std::string(".") : g.path.substr(0, slash)) + "/" + path;
    }
    return path;
}

void follow(const Link& l)
{
    if (l.page >= 0) { trace("link page %d", l.page + 1); go_page(l.page, l.y); return; }
    trace("link %s", l.uri.c_str());
    std::string fragment, target = local_target(l.uri, &fragment);
    struct stat st;
    if (!target.empty() && stat(target.c_str(), &st) == 0) {
        // another Markdown file in this window, Back returning here (a README's links to its docs); any other file
        // to its program
        if (S_ISREG(st.st_mode) && md::is_markdown(target)) {
            g.back_pages.push_back(here_place(true));
            g.fwd_pages.clear();
            Place p;
            p.path = target;
            open_place(p, fragment);
            return;
        }
        std::string err;
        if (!shell::spawn({ "xdg-open", target }, "", &err)) shell::message_box(g.wnd, "mupf", err, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    std::string err;
    if (!shell::spawn({ "xdg-open", l.uri }, "", &err)) shell::message_box(g.wnd, "mupf", err, MB_OK | MB_ICONEXCLAMATION);
}

// ---- the middle button: Firefox's autoscroll ----
//
// Held down and moved, the view scrolls toward the pointer at a speed that grows with its distance from where the button went
// down, until the button comes up; clicked without moving, the same stays on (the pointer steers it) until the next click
// or key. The speed is after Firefox's (AutoScrollChild's accelerate()): a distance of d pixels scrolls
// (d/12)^1.5 - 1 pixels every 20 ms, nothing within 12 pixels. Across too when the pages are wider than the view.
// The pointer is read at every step (the timer message's GetMessagePos), not from WM_MOUSEMOVE alone: as in Firefox it
// steers from anywhere on the screen, beyond the window's edge and over another window too, where the sticky mode (no
// button held, so no implicit grab) gets no motion events. The pointer is the cursor theme's all-scroll (IDC_SIZEALL; wux falls back to the core
// cursor font's fleur when the theme has none), not XP's reader-mode cursors (user32's OCR_RDR2DIM, OCR_RDRNORTH ...),
// which are not among wux's cursors; where the mode began, the origin mark (paint_autoscroll_mark).

double autoscroll_speed(int d)
{
    double v = d / 12.0;
    if (v > 1) return v * sqrt(v) - 1;
    if (v < -1) return v * sqrt(-v) + 1;
    return 0;
}

bool autoscroll_across() { return !g.lay.flow && g.lay.width > g.vw; }

// The origin mark, where the button went down, over the pages while the mode is on: IE's and Firefox's round grey mark with
// its arrow heads, after XP's reader-mode origin (user32 draws one at the click). The bitmap is not ours to copy, so it is
// drawn here into the back buffer, antialiased (4x4 samples a pixel). The look is a choice: 28 px across, a near-white fill
// at 90% (Firefox's mark is translucent too), a 1 px grey edge, a dark dot in the middle and dark arrow heads up and down,
// left and right as well when the view scrolls across.
const int MARK_R = 14;

RECT autoscroll_mark_rect()
{
    return { g.auto_at.x - MARK_R - 1, g.auto_at.y - MARK_R - 1, g.auto_at.x + MARK_R + 2, g.auto_at.y + MARK_R + 2 };
}

void paint_autoscroll_mark()
{
    if (!g.autoscroll || !g.bits) return;
    bool across = autoscroll_across();
    const double r = MARK_R, edge = 1.25;
    // an arrow head pointing up, its tip 11 px from the middle, 9 px wide at its base 4.5 px nearer (in its own frame)
    auto head = [](double x, double y) { return y >= -11 && y <= -6.5 && fabs(x) <= y + 11; };
    RECT b = autoscroll_mark_rect();
    for (int y = std::max((int)g.clip.top, (int)b.top); y < std::min((int)g.clip.bottom, (int)b.bottom); y++)
        for (int x = std::max((int)g.clip.left, (int)b.left); x < std::min((int)g.clip.right, (int)b.right); x++) {
            double cr = 0, cg = 0, cb = 0, ca = 0;   // premultiplied, summed over the samples
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    double dx = x + (sx + .5) / 4 - (g.auto_at.x + .5), dy = y + (sy + .5) / 4 - (g.auto_at.y + .5);
                    double d = sqrt(dx * dx + dy * dy);
                    if (d > r) continue;
                    bool dark = d <= 1.75 || head(dx, dy) || head(dx, -dy) || (across && (head(dy, dx) || head(dy, -dx)));
                    uint32_t c = dark ? 0x303030 : d > r - edge ? 0x808080 : 0xf8f8f8;
                    double a = dark || d > r - edge ? 1 : .9;
                    cr += a * (c >> 16 & 255);
                    cg += a * (c >> 8 & 255);
                    cb += a * (c & 255);
                    ca += a;
                }
            if (ca <= 0) continue;
            cr /= 16; cg /= 16; cb /= 16; ca /= 16;
            uint32_t& px = g.bits[(size_t)y * g.bw + x];
            int nr = (int)((px >> 16 & 255) * (1 - ca) + cr + .5), ng = (int)((px >> 8 & 255) * (1 - ca) + cg + .5),
                nb = (int)((px & 255) * (1 - ca) + cb + .5);
            px = 0xff000000u | (uint32_t)nr << 16 | (uint32_t)ng << 8 | (uint32_t)nb;
        }
}

// the mode ends: by a click, a key, the button up after a move, the wheel, or the capture taken away (release: false)
void autoscroll_end(bool release)
{
    if (!g.autoscroll) return;
    g.autoscroll = g.auto_sticky = false;
    KillTimer(g.view, TIMER_AUTOSCROLL);
    RECT m = autoscroll_mark_rect();
    InvalidateRect(g.view, &m, FALSE);
    if (release && GetCapture() == g.view) ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW));
    update_toolbar();
    trace("autoscroll off %d,%d", g.sx, g.sy);
}

void autoscroll_stop() { autoscroll_end(true); }

void autoscroll_start(POINT at)
{
    g.autoscroll = true;
    g.auto_sticky = g.auto_moved = false;
    g.auto_at = g.auto_pt = at;
    g.auto_rx = g.auto_ry = 0;
    g.auto_tick = GetTickCount();
    SetCapture(g.view);
    SetTimer(g.view, TIMER_AUTOSCROLL, AUTOSCROLL_TICK, nullptr);
    SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_SIZEALL));
    RECT m = autoscroll_mark_rect();
    InvalidateRect(g.view, &m, FALSE);
    trace("autoscroll on %d,%d", g.sx, g.sy);
}

// the pointer at p (in the view): where the mode steers to; a move of more than 3 px makes the button's release end it
void autoscroll_pointer(POINT p)
{
    g.auto_pt = p;
    if (abs(p.x - g.auto_at.x) + abs(p.y - g.auto_at.y) > 3) g.auto_moved = true;
}

// a step, the pointer where it is on the screen: the distance scrolled; traced as "autoscroll step TICK DX,DY SX,SY" (TICK
// GetTickCount's)
void autoscroll_step(POINT screen)
{
    ScreenToClient(g.view, &screen);
    autoscroll_pointer(screen);
    DWORD now = GetTickCount();
    double frames = std::min(5.0, (now - g.auto_tick) / 20.0);   // Firefox's time compensation, a step a 20 ms
    g.auto_tick = now;
    bool across = autoscroll_across();
    g.auto_rx += across ? autoscroll_speed(g.auto_pt.x - g.auto_at.x) * frames : 0;
    g.auto_ry += autoscroll_speed(g.auto_pt.y - g.auto_at.y) * frames;
    int dx = (int)g.auto_rx, dy = (int)g.auto_ry;
    g.auto_rx -= dx;
    g.auto_ry -= dy;
    int sx = g.sx, sy = g.sy;
    if (dx || dy) scroll_to(g.sx + dx, g.sy + dy);
    trace("autoscroll step %u %d,%d %d,%d", (unsigned)now, g.sx - sx, g.sy - sy, g.sx, g.sy);
}

// mupdf's keys (its viewer's, mupdf-gl and mupdf-x11 alike), which mupf takes as they are where they do not clash with
// Acrobat's: , and . the previous and next page (< and > ten pages), W H Z fit width, height and page, + and - zoom, j k h
// l a line down, up, left and right, g the first page (a number typed before it: that page) and G the last, / the Find
// box, n N the next and previous match, r reload, q quit. In a reflowable document a page is a screen (page_key, go_end).
bool char_key(WCHAR c)
{
    if (c >= '0' && c <= '9') {
        g.count = std::min(g.count * 10 + (c - '0'), 1000000);
        return true;
    }
    int count = g.count;
    g.count = 0;
    switch (c) {
    case ',': page_key(-1); break;
    case '.': page_key(1); break;
    case '<': case '>':
        if (g.lay.flow) for (int i = 0; i < 10; i++) scroll_screen(c == '<' ? -1 : 1);
        else if (g.opened) go_page(std::max(0, std::min(g.pages - 1, g.current + (c == '<' ? -10 : 10))), 0, false);
        break;
    case 'W': command(CMD_FIT_WIDTH); break;
    case 'Z': command(CMD_FIT_PAGE); break;
    case 'H':
        if (g.opened) { set_zoom(Z_FIT_HEIGHT, 0, top_anchor()); go_page(g.current, 0, false); }
        break;
    case '+': command(CMD_ZOOM_IN); break;
    case '-': command(CMD_ZOOM_OUT); break;
    case 'j': scroll_lines(LINE); break;
    case 'k': scroll_lines(-LINE); break;
    case 'h': scroll_to(g.sx - LINE, g.sy); break;
    case 'l': scroll_to(g.sx + LINE, g.sy); break;
    case 'g':
        if (count > 0 && g.opened) go_page(std::min(count, g.pages) - 1);
        else go_end(-1);
        break;
    case 'G': go_end(1); break;
    case '/': command(CMD_FIND); break;
    case 'n': command(CMD_FIND_NEXT); break;
    case 'N': command(CMD_FIND_PREV); break;
    case 'r': command(CMD_RELOAD); break;
    case 'q': command(CMD_EXIT); break;
    default: return false;
    }
    trace("key %c", (char)c);
    return true;
}

LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (!g.in_layout) relayout(resize_anchor());
        return 0;
    case WM_TIMER:
        if (wp == TIMER_REFLOW) { reflow(); return 0; }
        if (wp == TIMER_AUTOSCROLL) {
            DWORD at = GetMessagePos();
            if (g.autoscroll) autoscroll_step({ (short)LOWORD(at), (short)HIWORD(at) });
            return 0;
        }
        break;
    case WM_MBUTTONDOWN:
        SetFocus(hwnd);
        if (!g.autoscroll_on) return 0;
        if (g.autoscroll) autoscroll_stop();   // a click ends the sticky mode
        else autoscroll_start({ (short)LOWORD(lp), (short)HIWORD(lp) });
        return 0;
    case WM_MBUTTONUP:
        if (!g.autoscroll_on) return 0;
        if (g.autoscroll && !g.auto_sticky) {
            if (g.auto_moved) autoscroll_stop();
            else { g.auto_sticky = true; trace("autoscroll sticky"); }   // a click: on until the next click or key
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (g.autoscroll) { autoscroll_stop(); return 0; }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_view(dc, ps.rcPaint);
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
        if (g.autoscroll) autoscroll_stop();
        g.wheel += (short)HIWORD(wp);
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &p);
        // a notch: the system's wheel lines (SPI_GETWHEELSCROLLLINES, a screen for WHEEL_PAGESCROLL) of LINE pixels down;
        // with Shift its wheel characters across (Firefox's Shift+wheel), LINE pixels each
        UINT lines = 3, chars = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &chars, 0);
        while (g.wheel >= WHEEL_DELTA || g.wheel <= -WHEEL_DELTA) {
            bool up = g.wheel > 0;
            g.wheel += up ? -WHEEL_DELTA : WHEEL_DELTA;
            if (LOWORD(wp) & MK_CONTROL) {
                if (g.opened) set_zoom(Z_CUSTOM, up ? zoom_in(g.zoom) : zoom_out(g.zoom), anchor_at(p.x, p.y));
            } else if (LOWORD(wp) & MK_SHIFT) scroll_to(g.sx + (up ? -1 : 1) * (int)std::min(chars, 100u) * LINE, g.sy);
            else if (lines == WHEEL_PAGESCROLL) scroll_screen(up ? -1 : 1);
            else scroll_lines((up ? -1 : 1) * (int)std::min(lines, 100u) * LINE);
        }
        update_toolbar();
        return 0;
    }
    case WM_MOUSEHWHEEL:
        scroll_to(g.sx + ((short)HIWORD(wp) > 0 ? 3 : -3) * LINE, g.sy);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        if (g.autoscroll) { autoscroll_stop(); return 0; }   // the click that ends the sticky mode does nothing else
        g.panning = true;
        g.pan_moved = false;
        g.pan_at = { (short)LOWORD(lp), (short)HIWORD(lp) };
        g.pan_sx = g.sx;
        g.pan_sy = g.sy;
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (g.autoscroll) { autoscroll_pointer({ (short)LOWORD(lp), (short)HIWORD(lp) }); return 0; }
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
        if (g.autoscroll && (HWND)lp != hwnd) autoscroll_end(false);
        return 0;
    case WM_SETCURSOR:
        if (g.autoscroll) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_SIZEALL)); return TRUE; }
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
        bool hscroll = g.lay.width > g.vw && !g.lay.flow;
        switch (wp) {
        case VK_UP: scroll_lines(-LINE); break;
        case VK_DOWN: scroll_lines(LINE); break;
        // a reflowable document's column is never wider than the view: Left and Right a screen, as Page Up and Down
        case VK_LEFT: if (hscroll) scroll_to(g.sx - LINE, g.sy); else page_key(-1); break;
        case VK_RIGHT: if (hscroll) scroll_to(g.sx + LINE, g.sy); else page_key(1); break;
        case VK_PRIOR: page_key(-1); break;
        case VK_NEXT: page_key(1); break;
        case VK_SPACE: scroll_screen(shift ? -1 : 1); break;
        case VK_BACK: scroll_screen(-1); break;
        case VK_HOME: go_end(-1); break;
        case VK_END: go_end(1); break;
        case VK_ESCAPE: if (g.fullscreen) set_fullscreen(false); break;
        default:
            (void)ctrl;
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        g.count = 0;
        update_toolbar();
        return 0;
    }
    case WM_CHAR:
        if (char_key((WCHAR)wp)) { update_toolbar(); return 0; }
        break;
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
    case WM_DRAWITEM:
        if (wp == (WPARAM)ID_STATUS && ((DRAWITEMSTRUCT*)lp)->itemID == 1) { draw_nav((DRAWITEMSTRUCT*)lp); return TRUE; }
        break;
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
    case WM_TIMER:
        if (wp == TIMER_RELOAD) {
            KillTimer(hwnd, TIMER_RELOAD);
            long long stamp = file_stamp(g.path);
            if (g.reload && g.reload_tries > 0) reload_doc();   // a read that failed: again
            else if (stamp != -1 && stamp != g.file_stamp) reload_doc();
            return 0;
        }
        break;
    case WM_THEMECHANGED: case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE:
        if (msg == WM_THEMECHANGED) {
            lhbar::theme_changed();   // the theme's gradient, before a command bar is made for it
            sync_header();   // to or from a Longhorn theme
            if (g.rebar) menuband::fonts_changed();
        }
        apply_font();
        apply_md_font();
        layout_chrome();
        InvalidateRect(g.view, nullptr, FALSE);
        break;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        WCHAR file[4096];
        if (DragQueryFileW(drop, 0, file, sizeof file / sizeof file[0])) open_file(WuxUtf8(file));
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
          "  A PDF, XPS, EPUB, CBZ, FB2 or MOBI document, or a Markdown file (.md), drawn by mupdf; a Typst (.typ) or LaTeX (.tex)\n"
          "  source shown as the PDF that typst watch or latexmk -pvc makes of it, again on every save. The file is followed: a\n"
          "  write shows it again. MUPF_TRACE=1 prints what is opened, shown and rendered on stdout.\n", f);
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
    status_base = (WNDPROC)SetWindowLongPtrW(g.status, GWLP_WNDPROC, (LONG_PTR)status_proc);
    g.view = CreateWindowExW(WS_EX_CLIENTEDGE, L"mupf_view", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                             0, 0, 10, 10, g.wnd, (HMENU)(INT_PTR)ID_VIEW, nullptr, nullptr);
    g.tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
                             WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                             0, 0, 10, 10, g.wnd, (HMENU)(INT_PTR)ID_TREE, nullptr, nullptr);
    g.errors = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                               ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0, 0, 10, 10, g.wnd, (HMENU)(INT_PTR)ID_ERRORS, nullptr, nullptr);
    SendMessageW(g.errors, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), FALSE);   // (typst's carets line up)
    apply_font();
    DragAcceptFiles(g.wnd, TRUE);
    g.eng = new Engine(engine_notify, nullptr);
    apply_md_font();
    sync_header();
    layout_chrome();
    update_title();
    update_page_box();
    update_toolbar();
    ShowWindow(g.wnd, maxed ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(g.wnd);
    SetFocus(g.view);
    trace("mapped");
    if (!args.empty()) open_file(args[0]);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        // a key ends the middle button's autoscroll and does nothing else (Firefox's), whatever has the focus and before the
        // accelerators see it; Shift, Ctrl and Alt alone do not (Alt's release alone would open the menu bar)
        if (g.autoscroll && (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) && m.wParam != VK_SHIFT &&
            m.wParam != VK_CONTROL && m.wParam != VK_MENU) {
            autoscroll_stop();
            continue;
        }
        // Alt+Left and Alt+Right (Back, Forward) go on to DefWindowProc as well, which then knows Alt was not pressed alone
        // and does not open the menu bar when it is released (an accelerator's key never reaches it)
        if (m.message == WM_SYSKEYDOWN && (m.wParam == VK_LEFT || m.wParam == VK_RIGHT) && (HIWORD(m.lParam) & KF_ALTDOWN))
            command(m.wParam == VK_LEFT ? CMD_BACK : CMD_FORWARD);
        else if (TranslateAcceleratorW(g.wnd, g.accel, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    stop_compile();   // (the compiler and its directory)
    delete g.eng;   // ends the thread, an abandoned render with it
    g.eng = nullptr;
    delete g.cache;
    trace("exit");
    return 0;
}
