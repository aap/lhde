#include <string.h>
#include <pthread.h>
#include <deque>
#include <mutex>
#include <vector>
#include <set>
#include "window.h"
#include "sys/sys.h"

namespace win {

struct Toplevel {
    sys::NativeWindow* native;
    gdi::Bitmap* back;
    gdi::Region dirty;      // backbuffer areas changed since last present
    bool argb = false;      // native window is ARGB, backbuffer tracks alpha
};

static bool g_last_input_keyboard;   // the last input was a key press, not a click (XP's LINP_KEYBOARD)

struct Timer { Window* w; uintptr_t id; uint32_t interval; uint32_t due; void* callback; };

struct Caret {
    Window* w = nullptr;
    int32_t width = 0, height = 0;
    Point pos{0, 0};
    int hidden = 1;         // hide count; visible when 0
    bool drawn = false;     // currently inverted into the backbuffer
    uint32_t blink = 500;
};

static std::vector<Class> g_classes;
static std::vector<Window*> g_toplevels;
static std::set<Window*> g_live;
static std::deque<Message> g_queue;
static std::vector<Timer> g_timers;
static Window* g_capture;
static bool g_capture_screen;
static Window* g_focus;
static Window* g_active;
static Window* g_hover;
static Window* g_nc_hover;
static uint8_t g_keys[256];
static bool g_quit;
static int g_exit_code;
static Caret g_caret;
static struct { Window* w; int button; uint32_t time; Point pos; } g_lastclick;
static struct { int button; uint32_t time; Point root; } g_lastpress;   // for begin_move_resize

bool init() { return sys::init(); }
void shutdown() { sys::shutdown(); }

void register_class(const Class& c)
{
    for (Class& e : g_classes) if (!strcmp(e.name, c.name)) { e = c; return; }
    g_classes.push_back(c);
}

const Class* find_class(const char* name)
{
    for (const Class& e : g_classes) if (!strcmp(e.name, name)) return &e;
    return nullptr;
}

bool is_window(Window* w) { return w && g_live.count(w) != 0; }

// ---- tree helpers ---------------------------------------------------------

static void link_after(Window* w, Window* parent, Window* after)
{
    // after == WIN_HWND_TOP: topmost; WIN_HWND_BOTTOM: last; else below `after`
    w->parent = parent;
    if (!parent) return;
    if (after == WIN_HWND_TOP) {
        w->prev = nullptr; w->next = parent->child;
        if (w->next) w->next->prev = w;
        parent->child = w;
    } else if (after == WIN_HWND_BOTTOM) {
        Window* last = parent->child;
        while (last && last->next) last = last->next;
        w->prev = last; w->next = nullptr;
        if (last) last->next = w; else parent->child = w;
    } else {
        w->prev = after; w->next = after->next;
        if (after->next) after->next->prev = w;
        after->next = w;
    }
}

static void unlink(Window* w)
{
    if (!w->parent) return;
    if (w->prev) w->prev->next = w->next; else w->parent->child = w->next;
    if (w->next) w->next->prev = w->prev;
    w->parent = nullptr; w->next = w->prev = nullptr;
}

Window* toplevel_of(Window* w)
{
    while (w->parent) w = w->parent;
    return w;
}

// Origin of w's window rect in its toplevel's backbuffer coords.
static Point device_window_origin(Window* w)
{
    Point o{0, 0};
    for (Window* p = w; p->parent; p = p->parent) {
        o.x += p->rect.left; o.y += p->rect.top;
        o.x += p->parent->client.left; o.y += p->parent->client.top;
    }
    return o;
}

Point device_origin(Window* w)
{
    Point o = device_window_origin(w);
    return Point{o.x + w->client.left, o.y + w->client.top};
}

static Rect device_window_rect(Window* w)
{
    Point o = device_window_origin(w);
    return gdi::rect_xywh(o.x, o.y, w->rect.width(), w->rect.height());
}

static Rect device_client_rect(Window* w)
{
    Point o = device_origin(w);
    return gdi::rect_xywh(o.x, o.y, w->client.width(), w->client.height());
}

bool is_visible(Window* w)
{
    for (; w; w = w->parent) if (!(w->style & WS_VISIBLE)) return false;
    return true;
}

bool is_enabled(Window* w)
{
    for (; w; w = w->parent) if (w->style & WS_DISABLED) return false;
    return true;
}

// The part of w (its client area, or its whole window rect) that is actually
// on screen, in backbuffer coords: clipped by every ancestor's client area,
// at every level by the siblings above if that level's window has
// WS_CLIPSIBLINGS (win32: without it a child draws over its siblings; the
// first child is topmost and painted first, so later siblings paint over it,
// which is what lets the controls inside a group box show), and by own
// children if WS_CLIPCHILDREN.
static gdi::Region visible_region(Window* w, bool client)
{
    gdi::Region r;
    if (!is_visible(w)) return r;
    r.set(client ? device_client_rect(w) : device_window_rect(w));
    for (Window* p = w; p; p = p->parent) {
        if (p->parent) r.intersect(device_client_rect(p->parent));
        if (p->style & WS_CLIPSIBLINGS)
            for (Window* s = p->prev; s; s = s->prev)
                if (s->style & WS_VISIBLE) r.subtract(device_window_rect(s));
        if (r.empty()) break;
    }
    if (client && (w->style & WS_CLIPCHILDREN))
        for (Window* c = w->child; c; c = c->next)
            if (c->style & WS_VISIBLE) r.subtract(device_window_rect(c));
    return r;
}

static void mark_dirty(Window* w, const gdi::Region& dev)
{
    toplevel_of(w)->top->dirty.add(dev);
}

// ---- caret ----------------------------------------------------------------

static void caret_toggle_draw()
{
    Caret& c = g_caret;
    if (!is_window(c.w) || !is_visible(c.w)) { c.drawn = false; return; }
    gdi::DC* dc = get_dc(c.w);
    Rect r = gdi::rect_xywh(c.pos.x, c.pos.y, c.width, c.height);
    // invert
    Rect d = gdi::offset(r, dc->origin.x, dc->origin.y);
    for (const Rect& cr : dc->clip.rects()) {
        Rect i = gdi::intersect(d, cr);
        for (int32_t y = i.top; y < i.bottom; y++) {
            uint32_t* p = dc->target->row(y);
            for (int32_t x = i.left; x < i.right; x++) p[x] ^= 0x00ffffff;
        }
    }
    release_dc(c.w, dc);
    c.drawn = !c.drawn;
}

static void caret_undraw() { if (g_caret.drawn) caret_toggle_draw(); }

static void caret_update()
{
    // ensure drawn state matches visibility
    bool want = is_window(g_caret.w) && g_caret.hidden == 0;
    if (!want && g_caret.drawn) caret_toggle_draw();
    if (want && !g_caret.drawn) caret_toggle_draw();
}

bool create_caret(Window* w, int32_t width, int32_t height)
{
    destroy_caret();
    g_caret.w = w;
    g_caret.width = width > 0 ? width : 1;
    g_caret.height = height > 0 ? height : 1;
    g_caret.pos = Point{0, 0};
    g_caret.hidden = 1;
    g_caret.drawn = false;
    return true;
}

void destroy_caret()
{
    caret_undraw();
    if (g_caret.w) kill_timer(nullptr, (uintptr_t)-2);
    g_caret.w = nullptr;
    g_caret.hidden = 1;
}

void set_caret_pos(Point p)
{
    if (g_caret.pos.x == p.x && g_caret.pos.y == p.y) return;
    caret_undraw();
    g_caret.pos = p;
    caret_update();
}

Point caret_pos() { return g_caret.pos; }
Window* caret_window() { return g_caret.w; }

void show_caret(Window* w)
{
    if (!is_window(g_caret.w) || (w && w != g_caret.w)) return;
    if (g_caret.hidden > 0) g_caret.hidden--;
    if (g_caret.hidden == 0) {
        caret_update();
        set_timer(nullptr, (uintptr_t)-2, g_caret.blink);
    }
}

void hide_caret(Window* w)
{
    if (!is_window(g_caret.w) || (w && w != g_caret.w)) return;
    g_caret.hidden++;
    caret_undraw();
    if (g_caret.hidden == 1) kill_timer(nullptr, (uintptr_t)-2);
}

// ---- creation and destruction --------------------------------------------

void recalc_client(Window* w)
{
    Rect r = Rect{0, 0, w->rect.width(), w->rect.height()};
    Rect c = r;
    send(w, WM_NCCALCSIZE, 0, (LParam)&c);
    // clamp each edge into the window rect; a zero-width window keeps its height
    auto clamp = [](int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; };
    c.left = clamp(c.left, r.left, r.right);
    c.right = clamp(c.right, c.left, r.right);
    c.top = clamp(c.top, r.top, r.bottom);
    c.bottom = clamp(c.bottom, c.top, r.bottom);
    w->client = c;
}

Window* create(const char* clsname, const char* text, uint32_t style, uint32_t ex_style, const Rect& r,
               Window* parent, Window* owner, intptr_t id, void* param)
{
    const Class* cls = find_class(clsname);
    if (!cls) return nullptr;
    if (parent) style |= WS_CHILD; else style &= ~WS_CHILD;
    Window* w = new Window;
    w->cls = cls;
    w->proc = cls->proc;
    w->parent = w->child = w->next = w->prev = nullptr;
    w->owner = owner;
    w->rect = r;
    w->client = Rect{0, 0, r.width(), r.height()};
    w->style = style & ~WS_VISIBLE;
    w->ex_style = ex_style;
    w->class_style = cls->style;
    w->id = id;
    w->text = text ? text : "";
    w->userdata = nullptr;
    w->w32 = nullptr;
    w->erase = false;
    w->nc_invalid = false;
    w->track_leave = w->track_nc_leave = false;
    w->destroying = false;
    w->top = nullptr;
    // a desktop window is undecorated like a popup but managed (typed DESKTOP), not override-redirect
    // (a dock, WS_EX_FLXP_DOCK, likewise: typed DOCK)
    bool desktop = !parent && (ex_style & (WS_EX_FLXP_DESKTOP | WS_EX_FLXP_DOCK));
    bool popup = (style & WS_POPUP) && !(style & WS_CAPTION) && !(style & WS_THICKFRAME) && !desktop;
    w->wm_decorated = !parent && !popup && !desktop;
    g_live.insert(w);

    if (parent) {
        link_after(w, parent, WIN_HWND_BOTTOM);   // as win32: last created = bottom-most, so GW_CHILD/GW_HWNDNEXT run in creation order
    } else {
        Toplevel* t = new Toplevel;
        t->native = sys::create_window(r, w->text.c_str(), w, popup);
        if (desktop) sys::set_window_type(t->native, (ex_style & WS_EX_FLXP_DOCK) ? sys::WindowType::Dock : sys::WindowType::Desktop);
        t->back = gdi::create_bitmap(r.width(), r.height());
        w->top = t;
        g_toplevels.push_back(w);
        style_changed(w);   // a caption without a sizing border (a dialog, a message box): the WM may neither resize nor maximize it
    }

    if (send(w, WM_NCCREATE, 0, (LParam)param) == 0 && w->w32) { destroy(w); return nullptr; }
    if (!is_window(w)) return nullptr;
    recalc_client(w);
    if (send(w, WM_CREATE, 0, (LParam)param) == -1) {
        destroy(w);
        return nullptr;
    }
    if (!is_window(w)) return nullptr;
    if (style & WS_VISIBLE) show_window(w, SW_SHOW);
    return is_window(w) ? w : nullptr;
}

static void forget(Window* w)
{
    if (g_capture == w) g_capture = nullptr;
    if (g_focus == w) g_focus = nullptr;
    if (g_active == w) g_active = nullptr;
    if (g_hover == w) g_hover = nullptr;
    if (g_nc_hover == w) g_nc_hover = nullptr;
    if (g_lastclick.w == w) g_lastclick.w = nullptr;
    if (g_caret.w == w) { g_caret.drawn = false; destroy_caret(); }
    for (size_t i = 0; i < g_timers.size();) {
        if (g_timers[i].w == w) g_timers.erase(g_timers.begin() + i); else i++;
    }
    for (size_t i = 0; i < g_queue.size();) {
        if (g_queue[i].w == w) g_queue.erase(g_queue.begin() + i); else i++;
    }
}

void destroy(Window* w)
{
    if (!is_window(w) || w->destroying) return;
    w->destroying = true;
    if (w->style & WS_VISIBLE) show_window(w, SW_HIDE);
    // owned toplevels go first
    std::vector<Window*> owned;
    for (Window* t : g_toplevels) if (t->owner == w) owned.push_back(t);
    for (Window* t : owned) destroy(t);
    send(w, WM_DESTROY);
    while (w->child) destroy(w->child);
    send(w, WM_NCDESTROY);
    forget(w);
    unlink(w);
    if (w->top) {
        for (size_t i = 0; i < g_toplevels.size(); i++)
            if (g_toplevels[i] == w) { g_toplevels.erase(g_toplevels.begin() + i); break; }
        sys::destroy_window(w->top->native);
        gdi::destroy_bitmap(w->top->back);
        delete w->top;
    }
    g_live.erase(w);
    delete w;
}

// ---- geometry and state ---------------------------------------------------

void show_window(Window* w, int cmd)
{
    bool on = cmd != SW_HIDE;
    if (!!(w->style & WS_VISIBLE) == on) return;
    send(w, WM_SHOWWINDOW, on, 0);
    if (on) w->style |= WS_VISIBLE; else w->style &= ~WS_VISIBLE;
    if (w->top) {
        sys::show_window(w->top->native, on);
        if (on) { w->nc_invalid = true; invalidate(w); }
    } else if (on) {
        w->nc_invalid = true;
        invalidate(w);
    } else if (w->parent) {
        Rect r = gdi::offset(w->rect, -w->parent->client.left, -w->parent->client.top);
        invalidate(w->parent, &r);
    }
    if (!on) {
        if (g_focus && is_window(g_focus) && (g_focus == w)) set_focus(w->parent);
        if (g_capture == w) release_capture();
    }
    uintptr_t sw = on ? SIZE_RESTORED : SIZE_RESTORED;
    (void)sw;
}

void enable(Window* w, bool on)
{
    if (!!(w->style & WS_DISABLED) == !on) return;
    if (on) w->style &= ~WS_DISABLED; else w->style |= WS_DISABLED;
    if (!on) {
        for (Window* f = g_focus; f; f = f->parent) if (f == w) { set_focus(nullptr); break; }
        if (g_capture == w) release_capture();
    }
    send(w, WM_ENABLE, on, 0);
}

bool set_window_pos(Window* w, Window* after, const Rect& nr, uint32_t flags)
{
    if (!is_window(w)) return false;
    Rect old = w->rect;
    Rect r = old;
    if (!(flags & SWP_NOMOVE)) { r.left = nr.left; r.top = nr.top; r.right = r.left + old.width(); r.bottom = r.top + old.height(); }
    if (!(flags & SWP_NOSIZE)) { r.right = r.left + nr.width(); r.bottom = r.top + nr.height(); }
    bool resized = r.width() != old.width() || r.height() != old.height();
    bool moved = r.left != old.left || r.top != old.top;
    bool visible_before = (w->style & WS_VISIBLE) != 0;

    // a toplevel is shown where it goes (a menu or a drop-down list would map at its old place first, then move; the
    // drop shadow is taken from the screen under the new place)
    bool show_late = (flags & SWP_SHOWWINDOW) && w->top;
    if ((flags & SWP_SHOWWINDOW) && !show_late) show_window(w, SW_SHOW);
    if (flags & SWP_HIDEWINDOW) show_window(w, SW_HIDE);

    // z-order
    if (!(flags & SWP_NOZORDER) && w->parent && after != w) {
        if (after == WIN_HWND_TOP || after == WIN_HWND_BOTTOM || (is_window(after) && after->parent == w->parent)) {
            Window* parent = w->parent;
            unlink(w);
            link_after(w, parent, after);
            if (w->style & WS_VISIBLE) { w->nc_invalid = true; invalidate(w); }
        }
    } else if (!(flags & SWP_NOZORDER) && w->top && !w->wm_decorated && after == WIN_HWND_TOP && (w->style & WS_VISIBLE) &&
               !(w->ex_style & (WS_EX_FLXP_DESKTOP | WS_EX_FLXP_DOCK))) {
        sys::raise_window(w->top->native);   // a visible popup to HWND_TOP/TOPMOST (managed windows: the WM's call)
    }

    if (moved || resized) {
        Rect oldc = w->client;
        w->rect = r;
        if (resized || (flags & SWP_FRAMECHANGED)) recalc_client(w);
        if (w->top) {
            // X rejects zero-sized windows (tooltips and dropdowns start out empty)
            Rect nr = r;
            if (nr.width() <= 0) nr.right = nr.left + 1;
            if (nr.height() <= 0) nr.bottom = nr.top + 1;
            sys::set_window_rect(w->top->native, nr);
            if (resized) {
                gdi::destroy_bitmap(w->top->back);
                w->top->back = gdi::create_bitmap(r.width(), r.height());
                w->top->dirty.clear();
            }
        } else if (w->parent && visible_before && !(flags & SWP_NOREDRAW)) {
            Rect o = gdi::offset(old, -w->parent->client.left, -w->parent->client.top);
            invalidate(w->parent, &o);
        }
        if (resized || w->client != oldc || (flags & SWP_FRAMECHANGED)) {
            w->nc_invalid = true;
            if (!(flags & SWP_NOREDRAW)) invalidate(w);
        } else if (moved && !w->top && (w->style & WS_VISIBLE) && !(flags & SWP_NOREDRAW)) {
            w->nc_invalid = true;
            invalidate(w);
        }
    } else if (flags & SWP_FRAMECHANGED) {
        w->rect = r;
        recalc_client(w);
        w->nc_invalid = true;
        if (!(flags & SWP_NOREDRAW)) invalidate(w);
    }

    if (show_late) show_window(w, SW_SHOW);

    WindowPos wp{w, after, r, flags};
    if (moved || resized || (flags & SWP_FRAMECHANGED) || !(flags & SWP_NOZORDER))
        send(w, WM_WINDOWPOSCHANGED, 0, (LParam)&wp);
    return true;
}

void set_parent(Window* w, Window* parent)
{
    if (!is_window(w) || w->parent == parent || (!parent && w->top)) return;
    bool vis = (w->style & WS_VISIBLE) != 0;
    if (vis) show_window(w, SW_HIDE);
    if (w->top && parent) {
        // toplevel becomes a child: drop the native window
        for (size_t i = 0; i < g_toplevels.size(); i++)
            if (g_toplevels[i] == w) { g_toplevels.erase(g_toplevels.begin() + i); break; }
        sys::destroy_window(w->top->native);
        gdi::destroy_bitmap(w->top->back);
        delete w->top;
        w->top = nullptr;
        w->owner = nullptr;
        link_after(w, parent, WIN_HWND_TOP);
    } else if (!parent) {
        // child becomes an (undecorated) popup toplevel, as SetParent(hwnd, desktop) does
        Window* old_top = toplevel_of(w);
        unlink(w);
        w->owner = old_top;
        w->wm_decorated = false;
        Toplevel* t = new Toplevel;
        t->native = sys::create_window(w->rect, w->text.c_str(), w, true);
        t->back = gdi::create_bitmap(w->rect.width(), w->rect.height());
        w->top = t;
        g_toplevels.push_back(w);
    } else {
        unlink(w);
        link_after(w, parent, WIN_HWND_TOP);
    }
    recalc_client(w);
    w->nc_invalid = true;
    if (vis) show_window(w, SW_SHOW);
}

Rect client_rect(Window* w) { return Rect{0, 0, w->client.width(), w->client.height()}; }

Point window_origin(Window* w)
{
    Window* t = toplevel_of(w);
    Point o = device_window_origin(w);
    Rect sr = sys::window_rect(t->top->native);
    return Point{o.x + sr.left, o.y + sr.top};
}

Rect window_rect(Window* w)
{
    Point o = window_origin(w);
    return gdi::rect_xywh(o.x, o.y, w->rect.width(), w->rect.height());
}

Point client_to_screen(Window* w, Point p)
{
    Point o = window_origin(w);
    return Point{p.x + o.x + w->client.left, p.y + o.y + w->client.top};
}

Point screen_to_client(Window* w, Point p)
{
    Point o = window_origin(w);
    return Point{p.x - o.x - w->client.left, p.y - o.y - w->client.top};
}

Point map_point(Window* from, Window* to, Point p)
{
    if (from) p = client_to_screen(from, p);
    if (to) p = screen_to_client(to, p);
    return p;
}

// Deepest visible window under a point given in w's client coords; windows
// answering HTTRANSPARENT to WM_NCHITTEST (and disabled ones) are skipped.
static Window* hit_test_from(Window* w, Point client_pt, Point screen_pt, intptr_t* ht_out)
{
    for (Window* c = w->child; c; c = c->next) {
        if (!(c->style & WS_VISIBLE) || !c->rect.contains(client_pt)) continue;
        if (c->style & WS_DISABLED) continue;
        Point cp{client_pt.x - c->rect.left - c->client.left, client_pt.y - c->rect.top - c->client.top};
        intptr_t ht;
        Window* hit = hit_test_from(c, cp, screen_pt, &ht);
        if (hit) { *ht_out = ht; return hit; }
        ht = send(c, WM_NCHITTEST, 0, lparam_of(screen_pt));
        if (ht == HTTRANSPARENT) continue;
        *ht_out = ht;
        return c;
    }
    return nullptr;
}

static Window* hit_test(Window* t, Point dev, intptr_t* ht_out)
{
    Rect sr = sys::window_rect(t->top->native);
    Point screen{dev.x + sr.left, dev.y + sr.top};
    Point cp{dev.x - t->client.left, dev.y - t->client.top};
    Window* hit = hit_test_from(t, cp, screen, ht_out);
    if (hit) return hit;
    *ht_out = send(t, WM_NCHITTEST, 0, lparam_of(screen));
    return t;
}

Window* window_from_point(Point screen)
{
    for (Window* t : g_toplevels) {
        if (!(t->style & WS_VISIBLE)) continue;
        Rect sr = sys::window_rect(t->top->native);
        if (!sr.contains(screen)) continue;
        intptr_t ht;
        return hit_test(t, Point{screen.x - sr.left, screen.y - sr.top}, &ht);
    }
    return nullptr;
}

void set_text(Window* w, const char* t)
{
    // A caller that already stored the text (the w32 DefWindowProc) passes
    // w->text itself: only sync the native title, don't re-send WM_SETTEXT.
    if (is_window(w) && t == w->text.c_str()) {
        if (w->top) sys::set_title(w->top->native, t);
        return;
    }
    send(w, WM_SETTEXT, 0, (LParam)t);
}
const std::string& text(Window* w) { return w->text; }

// ---- painting -------------------------------------------------------------

void invalidate_region(Window* w, const gdi::Region& reg, bool erase)
{
    if (!is_visible(w)) return;
    gdi::Region r = reg;
    r.intersect(client_rect(w));
    if (r.empty()) return;
    w->update.add(r);
    if (erase) w->erase = true;
    for (Window* c = w->child; c; c = c->next) {
        if (!(c->style & WS_VISIBLE)) continue;
        gdi::Region cr = r;
        cr.offset(-(c->rect.left + c->client.left), -(c->rect.top + c->client.top));
        if (cr.overlaps(client_rect(c))) {
            Rect wr = gdi::offset(c->rect, -(c->rect.left + c->client.left), -(c->rect.top + c->client.top));
            (void)wr;
            c->nc_invalid = true;
            invalidate_region(c, cr, erase);
        }
    }
}

void invalidate(Window* w, const Rect* pr, bool erase)
{
    gdi::Region r(pr ? *pr : client_rect(w));
    invalidate_region(w, r, erase);
}

void invalidate_frame(Window* w) { if (is_visible(w)) w->nc_invalid = true; }

void validate(Window* w, const Rect* pr)
{
    if (pr) w->update.subtract(*pr); else w->update.clear();
    if (w->update.empty()) w->erase = false;
}

void validate_region(Window* w, const gdi::Region& r)
{
    w->update.subtract(r);
    if (w->update.empty()) w->erase = false;
}

static void paint_window_now(Window* w, bool children);

void redraw(Window* w, const Rect* r, uint32_t f)
{
    if (!is_window(w)) return;
    if (f & RDW_INVALIDATE) {
        invalidate(w, r, (f & RDW_ERASE) != 0);
        if (f & RDW_FRAME) w->nc_invalid = true;
    }
    if (f & RDW_VALIDATE) {
        validate(w, r);
        if (f & RDW_NOFRAME) w->nc_invalid = false;
    }
    if (f & RDW_ALLCHILDREN) {
        // children already invalidated by invalidate(); nothing extra
    }
    if (f & (RDW_UPDATENOW | RDW_ERASENOW)) paint_window_now(w, !(f & RDW_NOCHILDREN));
}

gdi::DC* begin_paint(Window* w, PaintInfo& pi, bool send_erase)
{
    Window* t = toplevel_of(w);
    if (w->nc_invalid) { w->nc_invalid = false; send(w, WM_NCPAINT, 1, 0); }
    if (g_caret.w == w) caret_undraw();
    gdi::Region clip = visible_region(w, true);
    Point o = device_origin(w);
    gdi::Region upd = w->update;
    upd.offset(o.x, o.y);
    clip.intersect(upd);
    // As Wine's server (validate_parents): what this window paints now is no
    // longer invalid in ancestors without WS_CLIPCHILDREN, so a parent painting
    // later (listview after UpdateWindow on its header) doesn't cover it.
    for (Window* p = w->parent; p; p = p->parent) {
        if ((p->style & WS_CLIPCHILDREN) || p->update.empty()) continue;
        Point po = device_origin(p);
        gdi::Region r = upd;
        r.offset(-po.x, -po.y);
        p->update.subtract(r);
    }
    gdi::DC* dc = gdi::create_dc(t->top->back);
    gdi::set_origin(dc, o);
    gdi::set_clip(dc, clip);
    pi.dc = dc;
    pi.rect = gdi::clip_box(dc);
    pi.erase = w->erase;
    w->update.clear();
    w->erase = false;
    if (send_erase && pi.erase && send(w, WM_ERASEBKGND, (WParam)dc, 0)) pi.erase = false;
    t->top->dirty.add(clip);
    return dc;
}

void end_paint(Window* w, PaintInfo& pi)
{
    gdi::destroy_dc(pi.dc);
    pi.dc = nullptr;
    if (g_caret.w == w) caret_update();
}

gdi::DC* get_dc(Window* w)
{
    Window* t = toplevel_of(w);
    gdi::DC* dc = gdi::create_dc(t->top->back);
    gdi::set_origin(dc, device_origin(w));
    gdi::set_clip(dc, visible_region(w, true));
    return dc;
}

gdi::DC* get_window_dc(Window* w)
{
    Window* t = toplevel_of(w);
    gdi::DC* dc = gdi::create_dc(t->top->back);
    gdi::set_origin(dc, device_window_origin(w));
    gdi::set_clip(dc, visible_region(w, false));
    return dc;
}

void release_dc(Window* w, gdi::DC* dc)
{
    mark_dirty(w, dc->clip);
    gdi::destroy_dc(dc);
}

void scroll_window(Window* w, int32_t dx, int32_t dy, const Rect* scroll, const Rect* clip,
                   bool move_children, gdi::Region* uncovered)
{
    Rect cr = client_rect(w);
    Rect sr = scroll ? gdi::intersect(*scroll, cr) : cr;
    if (clip) sr = gdi::intersect(sr, *clip);
    gdi::Region vis = visible_region(w, true);
    Point o = device_origin(w);
    gdi::Region src = vis;
    src.intersect(gdi::offset(sr, o.x, o.y));
    // destination must be visible too and inside the scroll rect
    gdi::Region dst = src;
    dst.offset(dx, dy);
    dst.intersect(src);
    gdi::Bitmap* back = toplevel_of(w)->top->back;
    // copy each dst rect from its source (dst - d); order rows to handle overlap
    for (const Rect& d : dst.rects()) {
        int32_t h = d.height(), wdt = d.width();
        if (dy <= 0) {
            for (int32_t y = 0; y < h; y++)
                memmove(back->row(d.top + y) + d.left, back->row(d.top + y - dy) + d.left - dx, (size_t)wdt * 4);
        } else {
            for (int32_t y = h - 1; y >= 0; y--)
                memmove(back->row(d.top + y) + d.left, back->row(d.top + y - dy) + d.left - dx, (size_t)wdt * 4);
        }
    }
    gdi::Region unc = src;
    unc.subtract(dst);
    mark_dirty(w, src);
    // shift the pending update region along
    gdi::Region upd = w->update;
    upd.intersect(sr);
    w->update.subtract(sr);
    upd.offset(dx, dy);
    upd.intersect(sr);
    w->update.add(upd);
    unc.offset(-o.x, -o.y);
    if (uncovered) *uncovered = unc;
    if (move_children) {
        for (Window* c = w->child; c; c = c->next) {
            Rect wr = gdi::offset(c->rect, -w->client.left, -w->client.top);
            if (!scroll || gdi::overlaps(wr, sr)) {
                c->rect = gdi::offset(c->rect, dx, dy);
                send(c, WM_MOVE, 0, make_lparam(c->rect.left, c->rect.top));
            }
        }
    }
}

static void paint_window_now(Window* w, bool children)
{
    if (!is_window(w) || !is_visible(w)) return;
    if (w->nc_invalid) { w->nc_invalid = false; send(w, WM_NCPAINT, 1, 0); }
    if (!is_window(w)) return;
    if (!w->update.empty()) {
        send(w, WM_PAINT);
        if (!is_window(w)) return;
        w->update.clear();
        w->erase = false;
    }
    if (!children) return;
    std::vector<Window*> kids;
    for (Window* c = w->child; c; c = c->next) kids.push_back(c);
    for (Window* c : kids) if (is_window(c)) paint_window_now(c, true);
}

void update(Window* w) { paint_window_now(w, true); }

// Find a window that needs painting (frame or client), depth-first.
static Window* find_dirty(Window* w)
{
    if (!(w->style & WS_VISIBLE)) return nullptr;
    if (w->nc_invalid || !w->update.empty()) return w;
    for (Window* c = w->child; c; c = c->next) {
        Window* d = find_dirty(c);
        if (d) return d;
    }
    return nullptr;
}

void present_now()
{
    caret_update();
    for (Window* t : g_toplevels) {
        Toplevel* tl = t->top;
        if (tl->dirty.empty()) continue;
        if (t->style & WS_VISIBLE)
            for (const Rect& r : tl->dirty.rects()) sys::present(tl->native, tl->back, r);
        tl->dirty.clear();
    }
}

// ---- messages -------------------------------------------------------------

Result send(Window* w, uint32_t msg, WParam wp, LParam lp)
{
    if (!is_window(w)) return 0;
    return w->proc(w, msg, wp, lp);
}

static void post_full(Window* w, uint32_t msg, WParam wp, LParam lp, uint32_t time, Point pt, const char* text)
{
    Message m;
    m.w = w; m.msg = msg; m.wp = wp; m.lp = lp; m.time = time; m.pt = pt;
    memset(m.text, 0, sizeof m.text);
    if (text) memcpy(m.text, text, sizeof m.text);
    g_queue.push_back(m);
}

// Posting from other threads (worker threads report results this way): the
// message waits in its own locked queue and the loop is woken (sys::wake);
// the UI thread moves it into g_queue before it next looks for messages.
static const pthread_t g_ui_thread = pthread_self();   // static init runs on the main thread
static std::mutex g_xq_lock;
static std::vector<Message> g_xqueue;

bool is_ui_thread() { return pthread_equal(pthread_self(), g_ui_thread) != 0; }

static void drain_cross_thread()
{
    std::vector<Message> in;
    {
        std::lock_guard<std::mutex> l(g_xq_lock);
        if (g_xqueue.empty()) return;
        in.swap(g_xqueue);
    }
    for (Message& m : in) {
        if (m.w && !is_window(m.w)) continue;   // destroyed meanwhile
        m.pt = sys::pointer_pos();
        g_queue.push_back(m);
    }
}

void post(Window* w, uint32_t msg, WParam wp, LParam lp)
{
    if (!is_ui_thread()) {
        Message m;
        memset(&m, 0, sizeof m);
        m.w = w; m.msg = msg; m.wp = wp; m.lp = lp; m.time = sys::ticks();
        { std::lock_guard<std::mutex> l(g_xq_lock); g_xqueue.push_back(m); }
        sys::wake();
        return;
    }
    post_full(w, msg, wp, lp, sys::ticks(), sys::pointer_pos(), nullptr);
}

Result def_proc(Window* w, uint32_t msg, WParam wp, LParam lp)
{
    switch (msg) {
    case WM_NCCREATE:
        return 1;
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCPAINT:
        return 0;
    case WM_PAINT: {
        PaintInfo pi;
        begin_paint(w, pi);
        end_paint(w, pi);
        return 0;
    }
    case WM_ERASEBKGND:
        if (w->cls->paint_background) {
            gdi::fill_rect((gdi::DC*)wp, client_rect(w), w->cls->background);
            return 1;
        }
        return 0;
    case WM_WINDOWPOSCHANGED: {
        WindowPos* p = (WindowPos*)lp;
        if (!(p->flags & SWP_NOSIZE))
            send(w, WM_SIZE, SIZE_RESTORED, make_lparam(w->client.width(), w->client.height()));
        if (!(p->flags & SWP_NOMOVE))
            send(w, WM_MOVE, 0, make_lparam(w->rect.left + w->client.left, w->rect.top + w->client.top));
        return 0;
    }
    case WM_CLOSE:
        destroy(w);
        return 0;
    case WM_SETTEXT:
        w->text = lp ? (const char*)lp : "";
        if (w->top) sys::set_title(w->top->native, w->text.c_str());
        return 1;
    case WM_GETTEXT: {
        char* buf = (char*)lp;
        size_t n = wp;
        if (!buf || n == 0) return 0;
        size_t len = w->text.size() < n - 1 ? w->text.size() : n - 1;
        memcpy(buf, w->text.data(), len);
        buf[len] = 0;
        return (Result)len;
    }
    case WM_GETTEXTLENGTH:
        return (Result)w->text.size();
    case WM_NCHITTEST: {
        Point p = screen_to_client(w, point_of(lp));
        return client_rect(w).contains(p) ? HTCLIENT : HTBORDER;
    }
    case WM_NCLBUTTONDOWN:
        begin_move_resize(w, (intptr_t)wp);
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if ((intptr_t)wp == HTCAPTION && w->top && w->wm_decorated) set_maximized(w, 2);
        return 0;
    case WM_SETCURSOR:
        if (w->parent) return send(w->parent, WM_SETCURSOR, wp, lp);
        set_cursor(Cursor::Arrow);
        return 1;
    case WM_MOUSEACTIVATE:
        if (w->parent) return send(w->parent, WM_MOUSEACTIVATE, wp, lp);
        return MA_ACTIVATE;
    case WM_FLXP_DRAGOVER:
    case WM_FLXP_DRAGLEAVE:
    case WM_FLXP_DROP:
        if (w->parent) return send(w->parent, msg, wp, lp);
        return 0;
    default:
        return 0;
    }
}

// ---- window manager frames (docs/CLIENT-PROTOCOL.md) --------------------

bool set_argb(Window* w, bool on)
{
    if (!is_window(w) || !w->top) return false;
    Toplevel* t = w->top;
    if (!sys::set_argb(t->native, on)) return false;
    if (t->argb == on) return true;
    t->argb = on;
    t->back->has_alpha = on;
    // whatever is in the backbuffer was drawn for the other kind of surface
    memset(t->back->pixels, 0, (size_t)t->back->stride * t->back->height * 4);
    w->nc_invalid = true;
    invalidate(w);
    return true;
}

void style_changed(Window* w)
{
    if (!is_window(w) || !w->top || !w->wm_decorated) return;
    sys::set_fixed_size(w->top->native, (w->style & WS_CAPTION) == WS_CAPTION && !(w->style & WS_THICKFRAME));
}

bool is_argb(Window* w) { return is_window(w) && w->top && w->top->argb; }

void set_frame_extension(Window* w, const int32_t* ltrb)
{
    if (is_window(w) && w->top) sys::set_frame_extension(w->top->native, ltrb);
}

void set_frame_flags(Window* w, uint32_t flags)
{
    if (is_window(w) && w->top) sys::set_frame_flags(w->top->native, flags);
}

uint32_t frame_flags(Window* w) { return is_window(w) && w->top ? sys::frame_flags(w->top->native) : 0; }

bool composited() { return sys::argb_supported() && sys::frame_info().composited; }

bool frame_metrics(FrameMetrics& m)
{
    const sys::FrameInfo& f = sys::frame_info();
    m.caption_h = f.caption_h; m.border_l = f.border_l; m.border_r = f.border_r; m.border_b = f.border_b;
    m.text = gdi::rgb(f.text_rgb >> 16, f.text_rgb >> 8, f.text_rgb);
    m.glow = gdi::rgb(f.glow_rgb >> 16, f.glow_rgb >> 8, f.glow_rgb);
    m.inactive_text = gdi::rgb(f.inactive_text_rgb >> 16, f.inactive_text_rgb >> 8, f.inactive_text_rgb);
    m.have_max = f.have_max;
    m.max_text = gdi::rgb(f.max_text_rgb >> 16, f.max_text_rgb >> 8, f.max_text_rgb);
    m.max_inactive_text = gdi::rgb(f.max_inactive_text_rgb >> 16, f.max_inactive_text_rgb >> 8, f.max_inactive_text_rgb);
    m.max_glow_none = f.max_glow_rgb == 0xFFFFFFFFu;
    m.max_glow = gdi::rgb(f.max_glow_rgb >> 16, f.max_glow_rgb >> 8, f.max_glow_rgb);
    return f.have_metrics;
}

std::string frame_theme() { return sys::frame_info().theme; }

gdi::Bitmap* header_pixmap(Window* w) { return is_window(w) && w->top ? sys::header_pixmap(w->top->native) : nullptr; }
bool caption_rect(Window* w, Rect& r) { return is_window(w) && w->top && sys::caption_rect(w->top->native, r); }
void set_input_hole(Window* w, const Rect* r) { if (is_window(w) && w->top) sys::set_input_hole(w->top->native, r); }
void set_shape(Window* w, const std::vector<Rect>* rects) { if (is_window(w) && w->top) sys::set_shape(w->top->native, rects); }
static bool g_drop_shadows = true;
bool drop_shadows() { return g_drop_shadows; }
void set_drop_shadows(bool on) { g_drop_shadows = on; }
static bool g_keyboard_cues = false;
bool keyboard_cues() { return g_keyboard_cues; }
void set_keyboard_cues(bool on) { g_keyboard_cues = on; }
bool last_input_keyboard() { return g_last_input_keyboard; }

void set_drop_shadow(Window* w, int size, int (*alpha)(int, int, int, int))
{
    if (is_window(w) && w->top) sys::set_drop_shadow(w->top->native, size, alpha);
}

// HT* -> _NET_WM_MOVERESIZE direction (HTCAPTION move, the sizing borders resize)
static int moveresize_direction(intptr_t ht)
{
    switch (ht) {
    case HTCAPTION: return 8;
    case HTTOPLEFT: return 0;
    case HTTOP: return 1;
    case HTTOPRIGHT: return 2;
    case HTRIGHT: return 3;
    case HTBOTTOMRIGHT: case HTSIZE: return 4;
    case HTBOTTOM: return 5;
    case HTBOTTOMLEFT: return 6;
    case HTLEFT: return 7;
    case MOVE_KEYBOARD: return 10;
    case SIZE_KEYBOARD: return 9;
    default: return -1;
    }
}

bool begin_move_resize(Window* w, intptr_t ht)
{
    if (!is_window(w) || !w->top || !w->wm_decorated) return false;
    int dir = moveresize_direction(ht);
    if (dir < 0) return false;
    if (dir >= 9) {
        sys::begin_move_resize(w->top->native, sys::pointer_pos(), dir, 0, 0);
        return true;
    }
    // the window manager owns the drag now: we will not see the release
    static const uint32_t vkb[] = {0, VK_LBUTTON, VK_MBUTTON, VK_RBUTTON};
    int button = g_lastpress.button >= 1 && g_lastpress.button <= 3 ? g_lastpress.button : 1;
    g_keys[vkb[button]] = 0;
    if (is_window(g_capture)) release_capture();
    sys::begin_move_resize(w->top->native, g_lastpress.time ? g_lastpress.root : sys::pointer_pos(), dir, button, g_lastpress.time);
    return true;
}

bool is_maximized(Window* w) { return is_window(w) && w->top && sys::is_maximized(w->top->native); }

void set_maximized(Window* w, int action)
{
    if (is_window(w) && w->top && w->wm_decorated) sys::set_maximized(w->top->native, action);
}

// ---- input state ----------------------------------------------------------

void set_capture(Window* w, bool screen_coords)
{
    Window* old = g_capture;
    g_capture = w;
    g_capture_screen = screen_coords;
    if (old && old != w) send(old, WM_CAPTURECHANGED, 0, (LParam)w);
}

void release_capture()
{
    Window* old = g_capture;
    g_capture = nullptr;
    g_capture_screen = false;
    if (old) send(old, WM_CAPTURECHANGED, 0, 0);
}

Window* capture() { return g_capture; }

void set_focus(Window* w)
{
    Window* old = g_focus;
    if (old == w) return;
    if (w && !is_window(w)) w = nullptr;
    g_focus = w;
    if (old && is_window(old)) send(old, WM_KILLFOCUS, (WParam)w, 0);
    if (w && is_window(w) && g_focus == w) send(w, WM_SETFOCUS, (WParam)old, 0);
}

Window* focus() { return g_focus; }
Window* active_window() { return g_active; }

void track_mouse_leave(Window* w, bool nc)
{
    if (nc) w->track_nc_leave = true; else w->track_leave = true;
}

int16_t key_state(uint32_t vk) { return vk < 256 && g_keys[vk] ? (int16_t)0x8000 : 0; }
bool key_down(uint32_t vk) { return vk < 256 && g_keys[vk]; }

// US keyboard set-1 scan codes. Extended keys (arrows, Home, ... on the
// grey block) share codes with the numpad and carry bit 24 in WM_KEYDOWN.
static const struct { uint8_t vk, scan; bool ext; } scan_table[] = {
    {0x1b,0x01,0}, {'1',0x02,0}, {'2',0x03,0}, {'3',0x04,0}, {'4',0x05,0}, {'5',0x06,0}, {'6',0x07,0},
    {'7',0x08,0}, {'8',0x09,0}, {'9',0x0a,0}, {'0',0x0b,0}, {0xbd,0x0c,0}, {0xbb,0x0d,0}, {0x08,0x0e,0},
    {0x09,0x0f,0}, {'Q',0x10,0}, {'W',0x11,0}, {'E',0x12,0}, {'R',0x13,0}, {'T',0x14,0}, {'Y',0x15,0},
    {'U',0x16,0}, {'I',0x17,0}, {'O',0x18,0}, {'P',0x19,0}, {0xdb,0x1a,0}, {0xdd,0x1b,0}, {0x0d,0x1c,0},
    {0x11,0x1d,0}, {'A',0x1e,0}, {'S',0x1f,0}, {'D',0x20,0}, {'F',0x21,0}, {'G',0x22,0}, {'H',0x23,0},
    {'J',0x24,0}, {'K',0x25,0}, {'L',0x26,0}, {0xba,0x27,0}, {0xde,0x28,0}, {0xc0,0x29,0}, {0x10,0x2a,0},
    {0xdc,0x2b,0}, {'Z',0x2c,0}, {'X',0x2d,0}, {'C',0x2e,0}, {'V',0x2f,0}, {'B',0x30,0}, {'N',0x31,0},
    {'M',0x32,0}, {0xbc,0x33,0}, {0xbe,0x34,0}, {0xbf,0x35,0}, {0xa1,0x36,0}, {0x6a,0x37,0}, {0x12,0x38,0},
    {0x20,0x39,0}, {0x14,0x3a,0}, {0x70,0x3b,0}, {0x71,0x3c,0}, {0x72,0x3d,0}, {0x73,0x3e,0}, {0x74,0x3f,0},
    {0x75,0x40,0}, {0x76,0x41,0}, {0x77,0x42,0}, {0x78,0x43,0}, {0x79,0x44,0}, {0x90,0x45,0}, {0x91,0x46,0},
    {0x67,0x47,0}, {0x68,0x48,0}, {0x69,0x49,0}, {0x6d,0x4a,0}, {0x64,0x4b,0}, {0x65,0x4c,0}, {0x66,0x4d,0},
    {0x6b,0x4e,0}, {0x61,0x4f,0}, {0x62,0x50,0}, {0x63,0x51,0}, {0x60,0x52,0}, {0x6e,0x53,0}, {0x7a,0x57,0},
    {0x7b,0x58,0}, {0xa0,0x2a,0}, {0xa2,0x1d,0}, {0xa4,0x38,0},
    {0x24,0x47,1}, {0x26,0x48,1}, {0x21,0x49,1}, {0x25,0x4b,1}, {0x27,0x4d,1}, {0x23,0x4f,1}, {0x28,0x50,1},
    {0x22,0x51,1}, {0x2d,0x52,1}, {0x2e,0x53,1}, {0x6f,0x35,1}, {0x5b,0x5b,1}, {0x5c,0x5c,1}, {0x5d,0x5d,1},
    {0xa3,0x1d,1}, {0xa5,0x38,1},
};

uint32_t scan_from_vk(uint32_t vk, bool* extended)
{
    for (auto& e : scan_table)
        if (e.vk == vk) { if (extended) *extended = e.ext; return e.scan; }
    if (extended) *extended = false;
    return 0;
}

uint32_t vk_from_scan(uint32_t scan, bool extended)
{
    for (auto& e : scan_table)
        if (e.scan == scan && e.ext == extended) return e.vk;
    for (auto& e : scan_table)
        if (e.scan == scan) return e.vk;
    return 0;
}

Point cursor_pos() { return sys::pointer_pos(); }

void set_cursor(Cursor c)
{
    for (Window* t : g_toplevels) sys::set_cursor(t->top->native, (sys::PointerShape)c);
}

gdi::Size screen_size() { return sys::screen_size(); }

void set_timer(Window* w, uintptr_t id, uint32_t ms, void* cb)
{
    if (ms < 10) ms = 10;
    for (Timer& t : g_timers)
        if (t.w == w && t.id == id) { t.interval = ms; t.due = sys::ticks() + ms; t.callback = cb; return; }
    g_timers.push_back(Timer{w, id, ms, sys::ticks() + ms, cb});
}

void kill_timer(Window* w, uintptr_t id)
{
    for (size_t i = 0; i < g_timers.size(); i++)
        if (g_timers[i].w == w && g_timers[i].id == id) { g_timers.erase(g_timers.begin() + i); return; }
}

// ---- event dispatch -------------------------------------------------------

static uint32_t mk_from_mods(uint32_t m)
{
    uint32_t mk = 0;
    if (m & sys::ModShift) mk |= MK_SHIFT;
    if (m & sys::ModControl) mk |= MK_CONTROL;
    if (m & sys::ModLButton) mk |= MK_LBUTTON;
    if (m & sys::ModMButton) mk |= MK_MBUTTON;
    if (m & sys::ModRButton) mk |= MK_RBUTTON;
    return mk;
}

static uint32_t vk_from_keysym(uint32_t ks)
{
    if (ks >= 'a' && ks <= 'z') return ks - 'a' + 'A';
    if (ks >= 'A' && ks <= 'Z') return ks;
    if (ks >= '0' && ks <= '9') return ks;
    if (ks == ' ') return VK_SPACE;
    if (ks >= 0xffbe && ks <= 0xffc9) return VK_F1 + (ks - 0xffbe);
    if (ks >= 0xffb0 && ks <= 0xffb9) return VK_NUMPAD0 + (ks - 0xffb0);
    switch (ks) {
    case 0xff08: return VK_BACK;
    case 0xff09: case 0xfe20: return VK_TAB;
    case 0xff0d: case 0xff8d: return VK_RETURN;
    case 0xffe1: case 0xffe2: return VK_SHIFT;
    case 0xffe3: case 0xffe4: return VK_CONTROL;
    case 0xffe9: case 0xffea: return VK_MENU;
    case 0xff1b: return VK_ESCAPE;
    case 0xff55: case 0xff9a: return VK_PRIOR;
    case 0xff56: case 0xff9b: return VK_NEXT;
    case 0xff57: case 0xff9c: return VK_END;
    case 0xff50: case 0xff95: return VK_HOME;
    case 0xff51: case 0xff96: return VK_LEFT;
    case 0xff52: case 0xff97: return VK_UP;
    case 0xff53: case 0xff98: return VK_RIGHT;
    case 0xff54: case 0xff99: return VK_DOWN;
    case 0xff63: case 0xff9e: return VK_INSERT;
    case 0xffff: case 0xff9f: return VK_DELETE;
    case 0xffaa: return VK_MULTIPLY;
    case 0xffab: return VK_ADD;
    case 0xffad: return VK_SUBTRACT;
    case 0xffae: return VK_DECIMAL;
    case 0xffaf: return VK_DIVIDE;
    case 0xff7f: return VK_NUMLOCK;
    case 0xffe5: return VK_CAPITAL;
    case 0xff13: return VK_PAUSE;
    case 0xff61: return VK_SNAPSHOT;
    case 0xffeb: return VK_LWIN;
    case 0xff67: return VK_APPS;
    default:
        if (ks < 0x100) {
            // punctuation: use the OEM-ish mapping GDI apps rarely care about
            switch (ks) {
            case ';': case ':': return 0xBA; case '=': case '+': return 0xBB;
            case ',': case '<': return 0xBC; case '-': case '_': return 0xBD;
            case '.': case '>': return 0xBE; case '/': case '?': return 0xBF;
            case '`': case '~': return 0xC0; case '[': case '{': return 0xDB;
            case '\\': case '|': return 0xDC; case ']': case '}': return 0xDD;
            case '\'': case '"': return 0xDE;
            case '!': return '1'; case '@': return '2'; case '#': return '3'; case '$': return '4';
            case '%': return '5'; case '^': return '6'; case '&': return '7'; case '*': return '8';
            case '(': return '9'; case ')': return '0';
            }
        }
        return 0;
    }
}

static void update_hover(Window* client_target, Window* nc_target, uint32_t time, Point pt)
{
    if (g_hover != client_target) {
        Window* old = g_hover;
        g_hover = client_target;
        if (old && old->track_leave && is_window(old)) {
            old->track_leave = false;
            post_full(old, WM_MOUSELEAVE, 0, 0, time, pt, nullptr);
        }
    }
    if (g_nc_hover != nc_target) {
        Window* old = g_nc_hover;
        g_nc_hover = nc_target;
        if (old && old->track_nc_leave && is_window(old)) {
            old->track_nc_leave = false;
            post_full(old, WM_NCMOUSELEAVE, 0, 0, time, pt, nullptr);
        }
    }
}

static void dispatch_mouse(Window* t, const sys::Event& ev)
{
    Point dev = ev.pos;
    Point screen = ev.root;
    Window* target;
    intptr_t ht = HTCLIENT;
    if (is_window(g_capture)) {
        target = g_capture;
    } else {
        target = hit_test(t, dev, &ht);
        if (!is_window(target)) return;
    }
    bool client = ht == HTCLIENT;
    if (!is_window(g_capture)) update_hover(client ? target : nullptr, client ? nullptr : target, ev.time, screen);
    Point local = screen_to_client(target, screen);
    uint32_t mk = mk_from_mods(ev.mods);
    LParam lp = (client && !(is_window(g_capture) && g_capture_screen)) ? lparam_of(local) : lparam_of(screen);
    WParam wp = client ? (WParam)mk : (WParam)ht;

    switch (ev.kind) {
    case sys::EventKind::MouseMove:
        if (!is_window(g_capture)) send(target, WM_SETCURSOR, (WParam)target, make_lparam((int32_t)ht, WM_MOUSEMOVE));
        post_full(target, client ? WM_MOUSEMOVE : WM_NCMOUSEMOVE, wp, lp, ev.time, screen, nullptr);
        break;
    case sys::EventKind::ButtonDown: {
        static const uint32_t down[] = {0, WM_LBUTTONDOWN, WM_MBUTTONDOWN, WM_RBUTTONDOWN};
        static const uint32_t dbl[]  = {0, WM_LBUTTONDBLCLK, WM_MBUTTONDBLCLK, WM_RBUTTONDBLCLK};
        static const uint32_t mkb[]  = {0, MK_LBUTTON, MK_MBUTTON, MK_RBUTTON};
        static const uint32_t vkb[]  = {0, VK_LBUTTON, VK_MBUTTON, VK_RBUTTON};
        g_keys[vkb[ev.button]] = 1;
        g_lastpress.button = ev.button; g_lastpress.time = ev.time; g_lastpress.root = ev.root;
        if (client) wp = mk | mkb[ev.button];
        bool is_dbl = g_lastclick.w == target && g_lastclick.button == ev.button &&
                      ev.time - g_lastclick.time < 500 &&
                      ev.root.x - g_lastclick.pos.x < 4 && g_lastclick.pos.x - ev.root.x < 4 &&
                      ev.root.y - g_lastclick.pos.y < 4 && g_lastclick.pos.y - ev.root.y < 4;
        uint32_t msg = down[ev.button];
        // win32: nonclient double-clicks come without CS_DBLCLKS; we do that
        // for the caption (also one the app claims inside the client area)
        if (is_dbl && ((target->class_style & CS_DBLCLKS) || (!client && ht == HTCAPTION))) { g_lastclick.w = nullptr; msg = dbl[ev.button]; }
        else { g_lastclick.w = target; g_lastclick.button = ev.button; g_lastclick.time = ev.time; g_lastclick.pos = ev.root; }
        if (!client) msg += WM_NCLBUTTONDOWN - WM_LBUTTONDOWN;
        if (!is_window(g_capture)) {
            Result ma = send(target, WM_MOUSEACTIVATE, (WParam)toplevel_of(target), make_lparam((int32_t)ht, (int32_t)msg));
            if (ma == MA_ACTIVATEANDEAT || ma == MA_NOACTIVATEANDEAT) break;
        }
        post_full(target, msg, wp, lp, ev.time, screen, nullptr);
        break;
    }
    case sys::EventKind::ButtonUp: {
        static const uint32_t up[] = {0, WM_LBUTTONUP, WM_MBUTTONUP, WM_RBUTTONUP};
        static const uint32_t vkb[] = {0, VK_LBUTTON, VK_MBUTTON, VK_RBUTTON};
        g_keys[vkb[ev.button]] = 0;
        uint32_t msg = up[ev.button];
        if (!client) msg += WM_NCLBUTTONDOWN - WM_LBUTTONDOWN;
        post_full(target, msg, wp, lp, ev.time, screen, nullptr);
        break;
    }
    case sys::EventKind::Wheel: {
        Window* f = is_window(g_focus) ? g_focus : target;
        post_full(f, WM_MOUSEWHEEL, (WParam)make_lparam((int32_t)mk, ev.wheel * 120), lparam_of(screen), ev.time, screen, nullptr);
        break;
    }
    default: break;
    }
}

// The WM-framed toplevel owned by t that is last in z-order, visible ones
// first (GetLastActivePopup, roughly), following disabled ones down to the
// dialog that disabled them. A modal dialog can still be hidden here: the
// dialog manager shows it once its queue is idle, and the owner's FocusIn can
// come before that.
static Window* enabled_owned_popup(Window* t)
{
    for (int depth = 0; t && depth < 16; depth++) {
        Window* best = nullptr;
        for (int pass = 0; pass < 2 && !best; pass++)
            for (Window* p : g_toplevels)
                if (p != t && is_window(p->owner) && toplevel_of(p->owner) == t && p->wm_decorated && !p->destroying &&
                    (pass || (p->style & WS_VISIBLE)))
                    best = p;
        if (!best || !(best->style & WS_DISABLED)) return best;
        t = best;
    }
    return nullptr;
}

// win32 ends modes (menu tracking, captures, drags) with WM_CANCELMODE, which
// the system sends when a window is deactivated, moved or sized from outside,
// or the input is taken away. Here: to the capture window (a context menu's
// popup, a pressed button) and to the toplevel t concerned (a menu's owner);
// DefWindowProc ends a menu that window owns or shows and releases its capture.
static void cancel_modes(Window* t)
{
    Window* c = g_capture;
    if (is_window(c)) send(c, WM_CANCELMODE, 0, 0);
    if (is_window(t) && t != c) send(t, WM_CANCELMODE, 0, 0);
}

struct RootHook { uint32_t id; void (*fn)(); bool pending; };
static std::vector<RootHook> g_root_hooks;

// the on_root_property_change callbacks, once the X queue is drained
static void run_root_hooks()
{
    for (size_t i = 0; i < g_root_hooks.size(); i++)
        if (g_root_hooks[i].pending) { g_root_hooks[i].pending = false; g_root_hooks[i].fn(); }
}

// ---- drag and drop -------------------------------------------------------------

static Window* g_drop_last;     // where the last WM_FLXP_DRAGOVER went

static void drop_event(Window* t, const sys::Event& ev)
{
    if (ev.kind == sys::EventKind::DragLeave) {
        if (is_window(g_drop_last)) send(g_drop_last, WM_FLXP_DRAGLEAVE, 0, 0);
        g_drop_last = nullptr;
        return;
    }
    bool drop = ev.kind == sys::EventKind::DragDrop;
    intptr_t ht = HTCLIENT;
    Window* deep = (t->style & WS_DISABLED) ? nullptr : hit_test(t, ev.pos, &ht);   // a modal dialog is up: no drops
    std::vector<std::string> paths;
    std::vector<const char*> ptrs;
    if (is_window(deep)) sys::drop_paths(paths);
    for (const std::string& p : paths) ptrs.push_back(p.c_str());
    sys::NativeWindow* sw = sys::drop_source_window();
    Window* source = sw ? (Window*)sys::window_user(sw) : nullptr;
    uint32_t keys = (ev.mods & sys::ModShift ? MK_SHIFT : 0) | (ev.mods & sys::ModControl ? MK_CONTROL : 0) | (ev.mods & sys::ModAlt ? MK_ALT : 0);
    DropInfo di{ deep, ev.root.x, ev.root.y, ev.drag_allowed, keys, ev.drag_action, sw != nullptr,
                 (uint32_t)ptrs.size(), ptrs.data(), is_window(source) ? source : nullptr };
    if (is_window(g_drop_last) && g_drop_last != deep && !drop) send(g_drop_last, WM_FLXP_DRAGLEAVE, 0, 0);
    uint32_t effect = 0;
    if (is_window(deep) && !ptrs.empty()) effect = (uint32_t)send(deep, drop ? WM_FLXP_DROP : WM_FLXP_DRAGOVER, 0, (LParam)&di) & ev.drag_allowed;
    if (!drop) { sys::drop_status(effect); g_drop_last = deep; return; }
    sys::drop_finish(effect);
    if (is_window(deep)) send(deep, WM_FLXP_DRAGLEAVE, 0, 0);
    if (is_window(g_drop_last) && g_drop_last != deep) send(g_drop_last, WM_FLXP_DRAGLEAVE, 0, 0);
    g_drop_last = nullptr;
}

uint32_t drag_files(Window* source, const std::vector<std::string>& paths, uint32_t allowed, const gdi::Bitmap* image, Point hotspot)
{
    Window* t = toplevel_of(source);
    if (!is_window(t) || !t->top || paths.empty()) return 0;
    release_capture();
    if (!sys::drag_begin(t->top->native, paths, allowed, image, hotspot)) return 0;
    while (sys::drag_active()) {
        Message m;
        if (!peek_message(m, nullptr, 0, 0, true)) { wait_message(); continue; }
        if (m.msg == WM_QUIT) break;    // g_quit stays set for the caller's loop
        translate_message(m);
        dispatch_message(m);
    }
    // the button release and key changes went to the drag
    uint32_t mods = sys::query_mods();
    g_keys[VK_LBUTTON] = (mods & sys::ModLButton) != 0;
    g_keys[VK_RBUTTON] = (mods & sys::ModRButton) != 0;
    g_keys[VK_MBUTTON] = (mods & sys::ModMButton) != 0;
    g_keys[VK_SHIFT] = g_keys[VK_LSHIFT] = (mods & sys::ModShift) != 0;
    g_keys[VK_CONTROL] = g_keys[VK_LCONTROL] = (mods & sys::ModControl) != 0;
    g_keys[VK_MENU] = g_keys[VK_LMENU] = (mods & sys::ModAlt) != 0;
    return sys::drag_result();
}

static void handle_event(const sys::Event& ev)
{
    if (ev.kind == sys::EventKind::Nothing) return;
    if (ev.kind == sys::EventKind::CompositionChanged || ev.kind == sys::EventKind::FrameThemeChanged) {
        // to every toplevel, as Windows broadcasts WM_DWMCOMPOSITIONCHANGED;
        // once per burst (luxwm rewrites _LHWM_THEME and _LHWM_FRAME_METRICS together)
        uint32_t msg = ev.kind == sys::EventKind::CompositionChanged ? WM_DWMCOMPOSITIONCHANGED : WM_DWMCOLORIZATIONCOLORCHANGED;
        for (Window* t : g_toplevels) {
            bool queued = false;
            for (const Message& q : g_queue) if (q.w == t && q.msg == msg) { queued = true; break; }
            if (!queued) post_full(t, msg, 0, 0, ev.time, sys::pointer_pos(), nullptr);
        }
        return;
    }
    if (ev.kind == sys::EventKind::RootPropertyChanged) {
        for (RootHook& h : g_root_hooks) if (h.id == ev.keysym) h.pending = true;
        for (Window* t : g_toplevels) post_full(t, WM_FLXP_ROOTPROPERTY, (WParam)ev.keysym, 0, ev.time, sys::pointer_pos(), nullptr);
        return;
    }
    Window* t = (Window*)sys::window_user(ev.window);
    if (!is_window(t)) return;
    Toplevel* tl = t->top;
    if (ev.kind == sys::EventKind::WindowPropertyChanged) {
        uint32_t msg = ev.keysym == sys::WindowHeaderPixmap ? WM_FLXP_HEADERPIXMAP :
                       ev.keysym == sys::WindowState ? WM_FLXP_WINDOWSTATE : WM_FLXP_CAPTIONRECT;
        for (const Message& q : g_queue) if (q.w == t && q.msg == msg) return;
        post_full(t, msg, 0, 0, ev.time, sys::pointer_pos(), nullptr);
        return;
    }
    switch (ev.kind) {
    case sys::EventKind::Exposed:
        tl->dirty.add(ev.rect);
        break;
    case sys::EventKind::Configured: {
        Rect r = ev.rect;
        bool resized = r.width() != t->rect.width() || r.height() != t->rect.height();
        bool moved = r.left != t->rect.left || r.top != t->rect.top;
        if (!resized && !moved) break;
        // moved or sized by the window manager (our own SetWindowPos has set
        // t->rect already): end menus and captures, as win32 does
        if (t->wm_decorated) cancel_modes(t);
        t->rect = r;
        if (resized) {
            gdi::destroy_bitmap(tl->back);
            tl->back = gdi::create_bitmap(r.width(), r.height());
            tl->back->has_alpha = tl->argb;
            tl->dirty.clear();
            recalc_client(t);
            t->nc_invalid = true;
            invalidate(t);
        }
        WindowPos wp{t, nullptr, r, (uint32_t)((resized ? 0 : SWP_NOSIZE) | (moved ? 0 : SWP_NOMOVE) | SWP_NOZORDER)};
        send(t, WM_WINDOWPOSCHANGED, 0, (LParam)&wp);
        break;
    }
    case sys::EventKind::Closed:
        post_full(t, WM_CLOSE, 0, 0, ev.time, ev.root, nullptr);
        break;
    case sys::EventKind::MouseMove:
    case sys::EventKind::ButtonDown:
    case sys::EventKind::ButtonUp:
    case sys::EventKind::Wheel:
        if (ev.kind == sys::EventKind::ButtonDown) g_last_input_keyboard = false;
        dispatch_mouse(t, ev);
        break;
    case sys::EventKind::PointerLeft:
        if (!is_window(g_capture)) update_hover(nullptr, nullptr, ev.time, ev.root);
        break;
    case sys::EventKind::DragOver:
    case sys::EventKind::DragLeave:
    case sys::EventKind::DragDrop:
        drop_event(t, ev);
        break;
    case sys::EventKind::GrabbedElsewhere:
        // the pointer belongs to someone else now (the WM's move/resize, ...)
        cancel_modes(t->wm_decorated ? t : (is_window(t->owner) ? toplevel_of(t->owner) : t));
        if (!is_window(g_capture)) update_hover(nullptr, nullptr, ev.time, ev.root);
        break;
    case sys::EventKind::KeyDown:
    case sys::EventKind::KeyUp: {
        uint32_t vk = vk_from_keysym(ev.keysym);
        bool down = ev.kind == sys::EventKind::KeyDown;
        if (down) g_last_input_keyboard = true;
        if (vk < 256) g_keys[vk] = down;
        if (vk == VK_SHIFT) g_keys[VK_LSHIFT] = down;
        if (vk == VK_CONTROL) g_keys[VK_LCONTROL] = down;
        if (vk == VK_MENU) g_keys[VK_LMENU] = down;
        Window* f = is_window(g_focus) ? g_focus : (is_window(g_active) ? g_active : t);
        bool alt = (ev.mods & sys::ModAlt) != 0 || (down && vk == VK_MENU);
        uint32_t msg = down ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN) : (alt ? WM_SYSKEYUP : WM_KEYUP);
        bool ext = false;
        uint32_t scan = scan_from_vk(vk, &ext);
        LParam lp = 1 | (LParam)scan << 16 | (ext ? 0x01000000 : 0) | (down ? 0 : (LParam)0xC0000000) | (alt ? 0x20000000 : 0);
        // Alt+key keeps its text: translate_message turns it into WM_SYSCHAR (dialog and menu mnemonics)
        post_full(f, msg, vk, lp, ev.time, ev.root, (down && !(ev.mods & sys::ModControl)) ? ev.text : nullptr);
        if (down && (ev.mods & sys::ModControl) && !alt && ev.text[0]) {
            // control characters still produce WM_CHAR (e.g. ^A, ^C) the way X reports them
            Message m; memset(&m, 0, sizeof m);
            m.w = f; m.msg = WM_KEYDOWN; m.wp = vk; m.time = ev.time; m.pt = ev.root;
            memcpy(m.text, ev.text, sizeof m.text);
            g_queue.back().text[0] = 0;
            memcpy(g_queue.back().text, ev.text, sizeof m.text);
        }
        break;
    }
    case sys::EventKind::FocusGained:
        if (t->style & WS_DISABLED) {
            // win32: activating a disabled window activates its enabled owned popup
            // (the modal dialog) instead; the WM is asked to follow
            if (Window* p = enabled_owned_popup(t)) { sys::activate_window(p->top->native); t = p; }
        }
        if (g_active == t && is_window(g_focus) && toplevel_of(g_focus) == t) break;   // already redirected here
        g_active = t;
        if (!is_window(g_focus) || toplevel_of(g_focus) != t) set_focus(t);
        else send(g_focus, WM_SETFOCUS, 0, 0);
        send(t, WM_ACTIVATE, 1, 0);
        break;
    case sys::EventKind::FocusLost:
        cancel_modes(t);    // deactivation (or a keyboard grab) ends menus first
        if (is_window(g_focus) && toplevel_of(g_focus) == t) send(g_focus, WM_KILLFOCUS, 0, 0);
        send(t, WM_ACTIVATE, 0, 0);
        if (g_active == t) g_active = nullptr;
        break;
    default:
        break;
    }
}

static int next_timer_timeout()
{
    if (g_timers.empty()) return -1;
    uint32_t now = sys::ticks();
    int best = -1;
    for (const Timer& t : g_timers) {
        int d = (int)(t.due - now);
        if (d < 0) d = 0;
        if (best < 0 || d < best) best = d;
    }
    return best;
}

static bool matches(const Message& m, Window* filter, uint32_t min, uint32_t max)
{
    if (filter && m.w != filter) return false;
    if (max && (m.msg < min || m.msg > max)) return false;
    return true;
}

// Try to produce a message without blocking: queued, timer, or paint.
static bool next_message(Message& m, Window* filter, uint32_t min, uint32_t max, bool remove)
{
    for (size_t i = 0; i < g_queue.size(); i++) {
        if (!matches(g_queue[i], filter, min, max)) continue;
        m = g_queue[i];
        if (remove) g_queue.erase(g_queue.begin() + i);
        return true;
    }
    // timers
    uint32_t now = sys::ticks();
    for (size_t i = 0; i < g_timers.size(); i++) {
        Timer& t = g_timers[i];
        if ((int)(t.due - now) > 0) continue;
        if (t.id == (uintptr_t)-2 && !t.w) {        // caret blink
            if (remove) { t.due = now + t.interval; if (g_caret.hidden == 0) caret_toggle_draw(); }
            continue;
        }
        if (!is_window(t.w) && t.w) continue;
        Message tm; memset(&tm, 0, sizeof tm);
        tm.w = t.w; tm.msg = WM_TIMER; tm.wp = t.id; tm.lp = (LParam)t.callback; tm.time = now; tm.pt = sys::pointer_pos();
        if (!matches(tm, filter, min, max)) continue;
        if (remove) t.due = now + t.interval;
        m = tm;
        return true;
    }
    // paint
    if (!max || (WM_PAINT >= min && WM_PAINT <= max)) {
        std::vector<Window*> tops = g_toplevels;
        for (Window* t : tops) {
            if (!is_window(t)) continue;
            Window* d = find_dirty(t);
            while (d) {
                if (filter && d != filter && !(filter && toplevel_of(filter) == t && false)) {
                    // a filter for another window: skip this toplevel
                    if (filter && d != filter) { d = nullptr; break; }
                }
                if (d->nc_invalid) { d->nc_invalid = false; send(d, WM_NCPAINT, 1, 0); if (!is_window(d)) { d = find_dirty(t); continue; } }
                if (!d->update.empty()) {
                    Message pm; memset(&pm, 0, sizeof pm);
                    pm.w = d; pm.msg = WM_PAINT; pm.time = now; pm.pt = sys::pointer_pos();
                    m = pm;
                    return true;
                }
                d = find_dirty(t);
            }
        }
    }
    return false;
}

bool peek_message(Message& m, Window* filter, uint32_t min, uint32_t max, bool remove)
{
    sys::Event ev;
    while (sys::wait_event(ev, 0)) handle_event(ev);
    run_root_hooks();
    sys::dispatch_fd_watches();
    drain_cross_thread();
    if (g_quit) { memset(&m, 0, sizeof m); m.msg = WM_QUIT; m.wp = (WParam)g_exit_code; return true; }
    return next_message(m, filter, min, max, remove);
}

bool get_message(Message& m, Window* filter, uint32_t min, uint32_t max)
{
    for (;;) {
        sys::Event ev;
        while (sys::wait_event(ev, 0)) handle_event(ev);
        run_root_hooks();
        sys::dispatch_fd_watches();
        drain_cross_thread();
        if (g_quit) { memset(&m, 0, sizeof m); m.msg = WM_QUIT; m.wp = (WParam)g_exit_code; return false; }
        if (next_message(m, filter, min, max, true)) return true;
        if (g_toplevels.empty()) { memset(&m, 0, sizeof m); m.msg = WM_QUIT; return false; }
        present_now();
        int timeout = next_timer_timeout();
        if (sys::wait_event(ev, timeout)) handle_event(ev);
    }
}

void wait_message()
{
    sys::Event ev;
    while (sys::wait_event(ev, 0)) handle_event(ev);
    run_root_hooks();
    sys::dispatch_fd_watches();
    drain_cross_thread();
    Message m;
    if (!g_queue.empty() || g_quit) return;
    if (next_message(m, nullptr, 0, 0, false)) return;
    present_now();
    if (sys::wait_event(ev, next_timer_timeout())) handle_event(ev);
}

void toplevels(std::vector<Window*>& out) { out = g_toplevels; }

void watch_fd(int fd, void (*callback)(int, void*), void* user) { sys::watch_fd(fd, callback, user); }
void unwatch_fd(int fd) { sys::unwatch_fd(fd); }

bool translate_message(const Message& m)
{
    if (m.msg != WM_KEYDOWN && m.msg != WM_SYSKEYDOWN) return false;
    if (!m.text[0]) return false;
    // decode utf-8 → code points, post WM_CHAR per code point
    const unsigned char* s = (const unsigned char*)m.text;
    bool any = false;
    while (*s) {
        uint32_t cp; int n;
        if (*s < 0x80) { cp = *s; n = 1; }
        else if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; n = 2; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; n = 3; }
        else { cp = *s & 0x07; n = 4; }
        for (int i = 1; i < n && s[i]; i++) cp = (cp << 6) | (s[i] & 0x3F);
        s += n;
        post_full(m.w, m.msg == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR, cp, m.lp, m.time, m.pt, nullptr);
        any = true;
    }
    return any;
}

Result dispatch_message(const Message& m)
{
    if (!is_window(m.w)) return 0;
    if (m.msg == WM_PAINT) {
        Window* w = m.w;
        Result r = send(w, WM_PAINT, m.wp, m.lp);
        if (is_window(w)) { w->update.clear(); w->erase = false; }   // never spin on a proc that ignores WM_PAINT
        return r;
    }
    return send(m.w, m.msg, m.wp, m.lp);
}

void quit(int code) { g_quit = true; g_exit_code = code; }

int run()
{
    Message m;
    while (get_message(m)) {
        translate_message(m);
        dispatch_message(m);
    }
    return g_exit_code;
}

// ---- shells ---------------------------------------------------------------------

uint32_t watch_root_property(const char* prop) { return sys::watch_root_property(prop); }
std::string root_string(const char* prop) { return sys::root_string(prop); }
void set_root_string(const char* prop, const std::string& value) { sys::set_root_string(prop, value); }

int wm_active(Window* w)
{
    uint64_t id = 0;
    if (!sys::root_window("_NET_ACTIVE_WINDOW", id)) return -1;
    if (!is_window(w) || !w->top || !w->top->native) return 0;
    return id != 0 && id == sys::native_id(w->top->native) ? 1 : 0;
}

void on_root_property_change(const char* prop, void (*fn)())
{
    uint32_t id = sys::watch_root_property(prop);
    for (RootHook& h : g_root_hooks) if (h.id == id && h.fn == fn) return;
    g_root_hooks.push_back(RootHook{ id, fn, false });
}
int root_cardinals(const char* prop, long* out, int max) { return sys::root_cardinals(prop, out, max); }

Rect work_area()
{
    Rect r;
    if (sys::work_area(r)) return r;
    gdi::Size ss = sys::screen_size();
    return Rect{0, 0, ss.cx, ss.cy};
}

} // namespace win
