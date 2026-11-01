// flxp-taskbar's X side: see xwm.h. One thread owns a connection of its own;
// the model is guarded by a mutex, requests from the UI thread are queued
// and the thread is woken through a pipe.
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include "xwm.h"

namespace xwm {

namespace {

Display* xd;
::Window root, own;
int scr;
std::thread worker;
std::atomic<bool> running;
int wake_fds[2] = { -1, -1 };
Notify notify_fn;

enum {
    A_CLIENT_LIST, A_ACTIVE, A_CURRENT_DESKTOP, A_WM_DESKTOP, A_NAME, A_UTF8, A_ICON, A_STATE, A_TYPE,
    A_HIDDEN, A_ATTENTION, A_MAX_V, A_MAX_H, A_SKIP_TASKBAR,
    A_T_DOCK, A_T_DESKTOP, A_T_TOOLBAR, A_T_MENU, A_T_SPLASH, A_T_UTILITY, A_T_NOTIFICATION, A_T_DROPDOWN,
    A_T_POPUP, A_T_TOOLTIP, A_T_COMBO, A_T_DND,
    A_CLOSE, A_ICON_GEOMETRY, A_STRUT, A_STRUT_PARTIAL, A_CHECK, A_PID,
    A_TRAY_OPCODE, A_MANAGER, A_XEMBED, A_XEMBED_INFO, A_TRAY_ORIENTATION, A_TRAY_VISUAL, A_TRAY_ICON_SIZE,
    A_TIMESTAMP, A_LHSHELL_START,
    A_COUNT
};
const char* atom_names[A_COUNT] = {
    "_NET_CLIENT_LIST", "_NET_ACTIVE_WINDOW", "_NET_CURRENT_DESKTOP", "_NET_WM_DESKTOP", "_NET_WM_NAME", "UTF8_STRING",
    "_NET_WM_ICON", "_NET_WM_STATE", "_NET_WM_WINDOW_TYPE",
    "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_DEMANDS_ATTENTION", "_NET_WM_STATE_MAXIMIZED_VERT",
    "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_SKIP_TASKBAR",
    "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_MENU",
    "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_TOOLTIP",
    "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND",
    "_NET_CLOSE_WINDOW", "_NET_WM_ICON_GEOMETRY", "_NET_WM_STRUT", "_NET_WM_STRUT_PARTIAL",
    "_NET_SUPPORTING_WM_CHECK", "_NET_WM_PID",
    "_NET_SYSTEM_TRAY_OPCODE", "MANAGER", "_XEMBED", "_XEMBED_INFO", "_NET_SYSTEM_TRAY_ORIENTATION",
    "_NET_SYSTEM_TRAY_VISUAL", "_NET_SYSTEM_TRAY_ICON_SIZE", "_FLXP_TASKBAR_TIMESTAMP", "_LHSHELL_START",
};
Atom atoms[A_COUNT];

struct Client {
    Task t;
    bool eligible = false;      // a taskbar window at all (type, skip-taskbar, transient)
    long desktop = -1;          // _NET_WM_DESKTOP, -1 absent
};

std::mutex model_lock;          // guards the members below and the request queue
std::vector<Client> clients;
uint64_t active;
long current_desktop;
uint32_t changes;
bool have_wm;
bool notified;
StartRequest start_req;         // the last _LHSHELL_START (root or our owner window)
uint32_t icon_serial;
std::vector<std::function<void()>> requests;

// ---- X helpers ----------------------------------------------------------------

XErrorHandler previous_handler;
int on_x_error(Display* d, XErrorEvent* e)
{
    if (d == xd) return 0;      // our windows vanish under us all the time (clients, tray icons)
    return previous_handler ? previous_handler(d, e) : 0;
}

bool get_prop(::Window w, Atom prop, Atom type, long max, std::vector<unsigned long>& out, int* format_out = nullptr)
{
    Atom t; int format; unsigned long count, after; unsigned char* data = nullptr;
    out.clear();
    if (XGetWindowProperty(xd, w, prop, 0, max, False, type, &t, &format, &count, &after, &data) != Success || !data)
        return false;
    if (format_out) *format_out = format;
    if (format == 32) out.assign((unsigned long*)data, (unsigned long*)data + count);
    else if (format == 8) out.assign((unsigned char*)data, (unsigned char*)data + count);
    XFree(data);
    return t != None;
}

std::string get_utf8(::Window w, Atom prop)
{
    Atom t; int format; unsigned long count, after; unsigned char* data = nullptr;
    std::string s;
    if (XGetWindowProperty(xd, w, prop, 0, 4096, False, atoms[A_UTF8], &t, &format, &count, &after, &data) == Success && data) {
        if (format == 8) s.assign((const char*)data, count);
        XFree(data);
    }
    return s;
}

std::string get_title(::Window w)
{
    std::string s = get_utf8(w, atoms[A_NAME]);
    if (!s.empty()) return s;
    XTextProperty tp;
    if (XGetWMName(xd, w, &tp) && tp.value) {
        char** list = nullptr; int n = 0;
        if (Xutf8TextPropertyToTextList(xd, &tp, &list, &n) >= Success && n > 0 && list) {
            s = list[0];
            XFreeStringList(list);
        } else {
            s.assign((const char*)tp.value, tp.nitems);
        }
        XFree(tp.value);
    }
    return s;
}

std::vector<unsigned long> hints_icon(::Window w);

// _NET_WM_ICON: the smallest image of at least 16 px (else the largest),
// box-filtered (or enlarged) to 16x16, straight alpha. Else the WM_HINTS
// icon pixmap and mask (xterm, xclock and other Xt programs).
std::vector<uint32_t> get_icon(::Window w)
{
    std::vector<uint32_t> out;
    std::vector<unsigned long> v;
    if (!get_prop(w, atoms[A_ICON], XA_CARDINAL, 1 << 20, v) || v.size() < 3) v = hints_icon(w);
    if (v.size() < 3) return out;
    size_t best = (size_t)-1; unsigned long bw = 0, bh = 0;
    for (size_t i = 0; i + 2 <= v.size();) {
        unsigned long iw = v[i], ih = v[i + 1];
        if (!iw || !ih || i + 2 + iw * ih > v.size()) break;
        bool better = best == (size_t)-1 ||
            (iw >= 16 && (bw < 16 || iw < bw)) || (bw < 16 && iw > bw);
        if (better) { best = i; bw = iw; bh = ih; }
        i += 2 + iw * ih;
    }
    if (best == (size_t)-1) return out;
    const unsigned long* src = &v[best + 2];
    out.resize(16 * 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            unsigned long x0 = x * bw / 16, x1 = (x + 1) * bw / 16, y0 = y * bh / 16, y1 = (y + 1) * bh / 16;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            uint64_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (unsigned long sy = y0; sy < y1 && sy < bh; sy++)
                for (unsigned long sx = x0; sx < x1 && sx < bw; sx++) {
                    uint32_t p = (uint32_t)src[sy * bw + sx];
                    uint32_t pa = p >> 24;
                    a += pa; r += ((p >> 16) & 255) * pa; g += ((p >> 8) & 255) * pa; b += (p & 255) * pa; n++;
                }
            uint32_t px = 0;
            if (n && a) px = (uint32_t)(a / n) << 24 | (uint32_t)(r / a) << 16 | (uint32_t)(g / a) << 8 | (uint32_t)(b / a);
            out[y * 16 + x] = px;
        }
    return out;
}

// WM_HINTS icon_pixmap (+ icon_mask) as a _NET_WM_ICON-style array {w, h, ARGB...}
std::vector<unsigned long> hints_icon(::Window w)
{
    std::vector<unsigned long> v;
    XWMHints* h = XGetWMHints(xd, w);
    if (!h) return v;
    Pixmap pm = (h->flags & IconPixmapHint) ? h->icon_pixmap : None;
    Pixmap mk = (h->flags & IconMaskHint) ? h->icon_mask : None;
    XFree(h);
    if (!pm) return v;
    ::Window r; int x, y; unsigned pw, ph, bw, depth, mw = 0, mh = 0, md = 0;
    if (!XGetGeometry(xd, pm, &r, &x, &y, &pw, &ph, &bw, &depth) || !pw || !ph || pw > 256 || ph > 256) return v;
    XImage* img = XGetImage(xd, pm, 0, 0, pw, ph, AllPlanes, ZPixmap);
    if (!img) return v;
    XImage* mimg = nullptr;
    if (mk && XGetGeometry(xd, mk, &r, &x, &y, &mw, &mh, &bw, &md) && mw >= pw && mh >= ph)
        mimg = XGetImage(xd, mk, 0, 0, pw, ph, 1, ZPixmap);
    v.resize(2 + (size_t)pw * ph);
    v[0] = pw; v[1] = ph;
    for (unsigned yy = 0; yy < ph; yy++)
        for (unsigned xx = 0; xx < pw; xx++) {
            unsigned long p = XGetPixel(img, (int)xx, (int)yy);
            uint32_t rgb = depth == 1 ? (p ? 0x000000u : 0xffffffu) : (uint32_t)(p & 0xffffff);
            bool on = mimg ? XGetPixel(mimg, (int)xx, (int)yy) != 0 : true;
            v[2 + (size_t)yy * pw + xx] = on ? (0xff000000u | rgb) : 0;
        }
    XDestroyImage(img);
    if (mimg) XDestroyImage(mimg);
    return v;
}

bool has_atom(const std::vector<unsigned long>& v, Atom a)
{
    for (unsigned long x : v) if ((Atom)x == a) return true;
    return false;
}

void read_state(Client& c)
{
    std::vector<unsigned long> v;
    get_prop((::Window)c.t.id, atoms[A_STATE], XA_ATOM, 64, v);
    c.t.minimized = has_atom(v, atoms[A_HIDDEN]);
    c.t.maximized = has_atom(v, atoms[A_MAX_V]) && has_atom(v, atoms[A_MAX_H]);
    bool attention = has_atom(v, atoms[A_ATTENTION]);
    XWMHints* h = XGetWMHints(xd, (::Window)c.t.id);
    if (h) { if (h->flags & XUrgencyHint) attention = true; XFree(h); }
    c.t.attention = attention;
    bool skip = has_atom(v, atoms[A_SKIP_TASKBAR]);
    // window types that are not tasks (XP: tool windows; EWMH: panels, menus, utilities, ...)
    get_prop((::Window)c.t.id, atoms[A_TYPE], XA_ATOM, 16, v);
    for (int a = A_T_DOCK; a <= A_T_DND; a++) if (has_atom(v, atoms[a])) skip = true;
    // owned windows (dialogs of another window) are not tasks, as in XP
    ::Window tf = None;
    if (XGetTransientForHint(xd, (::Window)c.t.id, &tf) && tf != None && tf != root) skip = true;
    if ((::Window)c.t.id == own) skip = true;
    c.eligible = !skip;
}

void read_desktop(Client& c)
{
    std::vector<unsigned long> v;
    c.desktop = get_prop((::Window)c.t.id, atoms[A_WM_DESKTOP], XA_CARDINAL, 1, v) && !v.empty() ? (long)v[0] : -1;
}

void update_shown(Client& c)
{
    c.t.shown = c.desktop < 0 || c.desktop == 0xFFFFFFFFL || (unsigned long)c.desktop == 0xFFFFFFFFUL ||
                current_desktop < 0 || c.desktop == current_desktop;
}

void read_client(Client& c)
{
    ::Window w = (::Window)c.t.id;
    c.t.title = get_title(w);
    c.t.icon = get_icon(w);
    c.t.icon_serial = ++icon_serial;
    read_state(c);
    read_desktop(c);
    update_shown(c);
}

Client* find_client(::Window w)
{
    for (Client& c : clients) if ((::Window)c.t.id == w) return &c;
    return nullptr;
}

void read_active()
{
    std::vector<unsigned long> v;
    uint64_t a = get_prop(root, atoms[A_ACTIVE], XA_WINDOW, 1, v) && !v.empty() ? (uint64_t)v[0] : 0;
    if (a != active) { active = a; changes |= ChangeActive; }
}

void read_current_desktop()
{
    std::vector<unsigned long> v;
    current_desktop = get_prop(root, atoms[A_CURRENT_DESKTOP], XA_CARDINAL, 1, v) && !v.empty() ? (long)v[0] : -1;
    for (Client& c : clients) update_shown(c);
    changes |= ChangeDesktop;
}

void read_client_list()
{
    std::vector<unsigned long> v;
    bool wm = get_prop(root, atoms[A_CLIENT_LIST], XA_WINDOW, 4096, v);
    if (wm != have_wm) { have_wm = wm; changes |= ChangeList; }
    std::vector<Client> next;
    next.reserve(v.size());
    for (unsigned long id : v) {
        Client* old = find_client((::Window)id);
        if (old) { next.push_back(*old); continue; }
        Client c;
        c.t.id = id;
        XSelectInput(xd, (::Window)id, PropertyChangeMask | StructureNotifyMask);
        read_client(c);
        next.push_back(c);
    }
    if (next.size() != clients.size()) changes |= ChangeList;
    else for (size_t i = 0; i < next.size(); i++) if (next[i].t.id != clients[i].t.id) changes |= ChangeList;
    clients.swap(next);
}

void client_message(::Window w, Atom type, long l0, long l1, long l2, long l3, long l4)
{
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = type;
    e.xclient.format = 32;
    e.xclient.data.l[0] = l0; e.xclient.data.l[1] = l1; e.xclient.data.l[2] = l2;
    e.xclient.data.l[3] = l3; e.xclient.data.l[4] = l4;
    XSendEvent(xd, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
}

// ---- system tray ----------------------------------------------------------------

struct Icon {
    ::Window w;
    bool mapped;                // XEMBED_MAPPED (or no _XEMBED_INFO)
    bool same_depth;            // can show the container through ParentRelative
    long docked_ms;             // when we embedded it (a racing hand-back from the previous tray, see tray_reparented)
};
std::vector<Icon> icons;
::Window tray_owner, container;
Atom tray_selection;
bool tray_wanted, tray_takeover;   // tray_start was called: claim the selection whenever it is free
bool tray_waiting_logged;
Atom luxshell_selection;         // _LHSHELL_S<screen>: luxshell runs (it forwards _LHSHELL_START, it may own the tray)
// The start-menu provider (docs/SHELL-DESIGN.md "Start menu ownership, revised"): we own _LH_STARTMENU_S<screen>
// with this window while our menu is the display's start menu; luxshell sends _LHSHELL_START to it.
::Window menu_owner;
Atom menu_selection;
bool menu_wanted;
bool menu_open;                 // our panel or classic menu is up: _LH_STARTMENU_OPEN = 1

long now_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
bool tray_vertical;
int tray_x, tray_y, tray_w, tray_h, tray_slot = 22, tray_icon = 16;
std::vector<uint32_t> tray_bg;
int tray_bg_w, tray_bg_h;

Time server_time(::Window w)
{
    // a zero-length append gives a PropertyNotify carrying the server's time
    XSelectInput(xd, w, PropertyChangeMask | StructureNotifyMask);
    unsigned char c = 0;
    XChangeProperty(xd, w, atoms[A_TIMESTAMP], XA_INTEGER, 8, PropModeAppend, &c, 0);
    XEvent e;
    XWindowEvent(xd, w, PropertyChangeMask, &e);
    return e.xproperty.time;
}

void apply_tray_background()
{
    if (!container || tray_bg.empty()) return;
    Visual* vis = DefaultVisual(xd, scr);
    int depth = DefaultDepth(xd, scr);
    XImage* img = XCreateImage(xd, vis, (unsigned)depth, ZPixmap, 0, (char*)tray_bg.data(), (unsigned)tray_bg_w,
                               (unsigned)tray_bg_h, 32, tray_bg_w * 4);
    if (!img) return;
    Pixmap pm = XCreatePixmap(xd, container, (unsigned)tray_bg_w, (unsigned)tray_bg_h, (unsigned)depth);
    GC gc = XCreateGC(xd, pm, 0, nullptr);
    XPutImage(xd, pm, gc, img, 0, 0, 0, 0, (unsigned)tray_bg_w, (unsigned)tray_bg_h);
    XFreeGC(xd, gc);
    img->data = nullptr;
    XDestroyImage(img);
    XSetWindowBackgroundPixmap(xd, container, pm);
    XFreePixmap(xd, pm);
    XClearWindow(xd, container);
    for (Icon& i : icons)
        if (i.mapped) {
            if (i.same_depth) XSetWindowBackgroundPixmap(xd, i.w, ParentRelative);
            XClearArea(xd, i.w, 0, 0, 0, 0, True);   // the client redraws over its new background
        }
}

void tray_relayout()
{
    if (!container) return;
    int n = 0;
    for (Icon& i : icons) {
        if (!i.mapped) { XUnmapWindow(xd, i.w); continue; }
        int off = (tray_slot - tray_icon) / 2;
        int x = tray_vertical ? (tray_w - tray_icon) / 2 : n * tray_slot + off;
        int y = tray_vertical ? n * tray_slot + off : (tray_h - tray_icon) / 2;
        XMoveResizeWindow(xd, i.w, x, y, (unsigned)tray_icon, (unsigned)tray_icon);
        XMapRaised(xd, i.w);
        n++;
    }
    if (n && tray_w > 0 && tray_h > 0) {
        XMoveResizeWindow(xd, container, tray_x, tray_y, (unsigned)tray_w, (unsigned)tray_h);
        XMapWindow(xd, container);
    } else {
        XUnmapWindow(xd, container);
    }
}

bool xembed_mapped(::Window w)
{
    std::vector<unsigned long> v;
    if (!get_prop(w, atoms[A_XEMBED_INFO], atoms[A_XEMBED_INFO], 2, v) || v.size() < 2) return true;
    return (v[1] & 1) != 0;     // XEMBED_MAPPED
}

void tray_dock(::Window w)
{
    for (Icon& i : icons) if (i.w == w) return;
    XWindowAttributes a;
    XSync(xd, False);
    if (!XGetWindowAttributes(xd, w, &a)) return;
    XSelectInput(xd, w, StructureNotifyMask | PropertyChangeMask);
    XAddToSaveSet(xd, w);       // back to the root if we go away
    Icon ic;
    ic.w = w;
    ic.mapped = xembed_mapped(w);
    ic.same_depth = a.depth == DefaultDepth(xd, scr);
    XUnmapWindow(xd, w);        // XEmbed: the embedder maps it
    XReparentWindow(xd, w, container, 0, 0);
    std::vector<unsigned long> info;
    long version = get_prop(w, atoms[A_XEMBED_INFO], atoms[A_XEMBED_INFO], 2, info) && !info.empty() ? (long)info[0] : 0;
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = atoms[A_XEMBED];
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = 0;    // XEMBED_EMBEDDED_NOTIFY
    e.xclient.data.l[3] = (long)container;
    e.xclient.data.l[4] = version < 0 ? 0 : version;
    XSendEvent(xd, w, False, NoEventMask, &e);
    ic.docked_ms = now_ms();
    icons.push_back(ic);
    if (ic.same_depth) XSetWindowBackgroundPixmap(xd, w, ParentRelative);
    tray_relayout();
    changes |= ChangeTray;
}

void tray_forget(::Window w)
{
    for (size_t i = 0; i < icons.size(); i++)
        if (icons[i].w == w) {
            icons.erase(icons.begin() + (long)i);
            tray_relayout();
            changes |= ChangeTray;
            return;
        }
}

// The icons go back to the root as luxshell's tray_stop does: unmapped (a mapped toplevel would be managed by
// the window manager as a window until the next tray docks it), without our ParentRelative background, out of
// our save-set; the next tray's MANAGER message makes them dock again
void tray_release_icons()
{
    for (Icon& i : icons) {
        XSelectInput(xd, i.w, NoEventMask);
        XUnmapWindow(xd, i.w);
        XSetWindowBackgroundPixmap(xd, i.w, None);
        XReparentWindow(xd, i.w, root, 0, 0);
        XRemoveFromSaveSet(xd, i.w);
    }
    icons.clear();
    if (container) XUnmapWindow(xd, container);
    changes |= ChangeTray;
}

::Window parent_of(::Window w)
{
    ::Window r = None, parent = None, *kids = nullptr;
    unsigned n = 0;
    if (!XQueryTree(xd, w, &r, &parent, &kids, &n)) return None;
    if (kids) XFree(kids);
    return parent;
}

// A ReparentNotify for one of our icons. Events can be stale: when we take an icon over from another tray,
// that tray's hand-back (reparent to the root) may reach the server after our XSelectInput but before our
// reparent (docs/SHELL-DESIGN.md M8), so the notify says "root" while the icon sits in our container.
// Ask the server; an icon the old tray moved out again right after we docked it is taken back once.
void tray_reparented(::Window w, ::Window to)
{
    if (to == container) return;
    for (size_t k = 0; k < icons.size(); k++) {
        if (icons[k].w != w) continue;
        ::Window now = parent_of(w);
        if (now == container) return;
        if (now == root && tray_owner && now_ms() - icons[k].docked_ms < 3000) {
            icons[k].docked_ms = 0;     // once
            XReparentWindow(xd, w, container, 0, 0);
            tray_relayout();
            return;
        }
        icons.erase(icons.begin() + (long)k);
        tray_relayout();
        changes |= ChangeTray;
        return;
    }
}

bool owned(Atom selection) { return selection && XGetSelectionOwner(xd, selection) != None; }

// Claim _NET_SYSTEM_TRAY_S<screen> (System Tray Protocol 0.3: owner window with a real timestamp, then MANAGER on
// the root). Never from luxshell: while luxshell runs (_LHSHELL_S<screen>) and holds the tray we wait until it hands it
// over (it does before starting us as its classic bar); from another tray only with takeover; else we wait too
// (run() retries every 2 s) and take it when it is free.
void do_tray_start(bool takeover)
{
    char name[64];
    snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", scr);
    tray_selection = XInternAtom(xd, name, False);
    tray_wanted = true;
    tray_takeover = takeover;
    if (tray_owner) return;
    ::Window holder = XGetSelectionOwner(xd, tray_selection);
    if (holder != None && (!takeover || owned(luxshell_selection))) {
        if (!tray_waiting_logged)
            fprintf(stderr, "flxp-taskbar: %s is owned by 0x%lx (%s); no notification icons until it is free%s\n", name, holder,
                    owned(luxshell_selection) ? "luxshell runs" : "another tray", owned(luxshell_selection) ? "" : " (tray_takeover = 1 takes it)");
        tray_waiting_logged = true;
        return;
    }
    tray_waiting_logged = false;
    XSetWindowAttributes sa = {};
    sa.override_redirect = True;
    tray_owner = XCreateWindow(xd, root, -1, -1, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWOverrideRedirect, &sa);
    sa.background_pixmap = ParentRelative;
    if (!container)
        container = XCreateWindow(xd, own, 0, 0, 1, 1, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixmap, &sa);
    long orient = tray_vertical ? 1 : 0;
    XChangeProperty(xd, tray_owner, atoms[A_TRAY_ORIENTATION], XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&orient, 1);
    long vid = (long)XVisualIDFromVisual(DefaultVisual(xd, scr));
    XChangeProperty(xd, tray_owner, atoms[A_TRAY_VISUAL], XA_VISUALID, 32, PropModeReplace, (unsigned char*)&vid, 1);
    long isz = tray_icon;
    XChangeProperty(xd, tray_owner, atoms[A_TRAY_ICON_SIZE], XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&isz, 1);
    Time t = server_time(tray_owner);
    XSetSelectionOwner(xd, tray_selection, tray_owner, t);
    if (XGetSelectionOwner(xd, tray_selection) != tray_owner) {
        fprintf(stderr, "flxp-taskbar: could not own %s\n", name);
        XDestroyWindow(xd, tray_owner); tray_owner = None;
        return;
    }
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = root;
    e.xclient.message_type = atoms[A_MANAGER];
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)t;
    e.xclient.data.l[1] = (long)tray_selection;
    e.xclient.data.l[2] = (long)tray_owner;
    XSendEvent(xd, root, False, StructureNotifyMask, &e);
    XFlush(xd);
    apply_tray_background();
}

// ---- the start-menu provider --------------------------------------------------------

// _LH_STARTMENU_OPEN (CARDINAL 1/0) on the owner window and on the root: our menu is open (for lux's
// Start button, which cannot see our menu otherwise). Re-published when the owner window is (re)created.
void publish_menu_open()
{
    Atom a = XInternAtom(xd, "_LH_STARTMENU_OPEN", False);
    long v = menu_open ? 1 : 0;
    if (menu_owner) XChangeProperty(xd, menu_owner, a, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&v, 1);
    XChangeProperty(xd, root, a, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&v, 1);
    XFlush(xd);
}

void do_menu_provide(bool on)
{
    menu_wanted = on;
    if (!on) {
        if (menu_owner) {
            if (XGetSelectionOwner(xd, menu_selection) == menu_owner) XSetSelectionOwner(xd, menu_selection, None, server_time(menu_owner));
            XDestroyWindow(xd, menu_owner);
            menu_owner = None;
        }
        return;
    }
    if (menu_owner) return;
    ::Window holder = XGetSelectionOwner(xd, menu_selection);
    if (holder != None) {   // another provider (a second flxp-taskbar): it keeps the menu
        fprintf(stderr, "flxp-taskbar: _LH_STARTMENU_S%d is owned by 0x%lx (another start menu); ours opens from our button only\n",
                scr, holder);
        return;
    }
    XSetWindowAttributes sa = {};
    sa.override_redirect = True;
    menu_owner = XCreateWindow(xd, root, -1, -1, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWOverrideRedirect, &sa);
    XStoreName(xd, menu_owner, "flxp-taskbar start menu");
    Time t = server_time(menu_owner);
    XSetSelectionOwner(xd, menu_selection, menu_owner, t);
    if (XGetSelectionOwner(xd, menu_selection) != menu_owner) {
        XDestroyWindow(xd, menu_owner);
        menu_owner = None;
    }
    publish_menu_open();
}

void start_message(const XClientMessageEvent& m, bool from_root)
{
    StartRequest r;
    r.toggle = from_root && !(m.data.l[3] > 0 && m.data.l[4] > 0) ? true : (m.data.l[0] & 1) != 0;   // lhwm's: l[0] is a time
    r.x = (int)m.data.l[1]; r.y = (int)m.data.l[2]; r.w = (int)m.data.l[3]; r.h = (int)m.data.l[4];
    if (r.w <= 0 || r.h <= 0) r.x = r.y = r.w = r.h = 0;
    r.from_root = from_root;
    r.serial = start_req.serial + 1;
    start_req = r;
    changes |= ChangeStartKey;
}

// ---- the thread ----------------------------------------------------------------

void handle(XEvent& e)
{
    switch (e.type) {
    case PropertyNotify: {
        Atom a = e.xproperty.atom;
        ::Window w = e.xproperty.window;
        if (w == root) {
            if (a == atoms[A_CLIENT_LIST]) read_client_list();
            else if (a == atoms[A_ACTIVE]) read_active();
            else if (a == atoms[A_CURRENT_DESKTOP]) read_current_desktop();
            return;
        }
        if (Client* c = find_client(w)) {
            if (a == atoms[A_NAME] || a == XA_WM_NAME) {
                std::string t = get_title(w);
                if (t != c->t.title) { c->t.title = t; changes |= ChangeTitle; }
            } else if (a == atoms[A_ICON]) {
                c->t.icon = get_icon(w); c->t.icon_serial = ++icon_serial; changes |= ChangeIcon;
            } else if (a == atoms[A_STATE] || a == XA_WM_HINTS || a == atoms[A_TYPE] || a == XA_WM_TRANSIENT_FOR) {
                read_state(*c); changes |= ChangeState;
                if (a == XA_WM_HINTS) {   // an icon pixmap may have come or changed
                    std::vector<uint32_t> ic = get_icon(w);
                    if (ic != c->t.icon) { c->t.icon = ic; c->t.icon_serial = ++icon_serial; changes |= ChangeIcon; }
                }
            } else if (a == atoms[A_WM_DESKTOP]) {
                read_desktop(*c); update_shown(*c); changes |= ChangeDesktop;
            }
            return;
        }
        for (Icon& i : icons)
            if (i.w == w && a == atoms[A_XEMBED_INFO]) {
                bool m = xembed_mapped(w);
                if (m != i.mapped) { i.mapped = m; tray_relayout(); changes |= ChangeTray; }
            }
        return;
    }
    case ClientMessage:
        if (e.xclient.message_type == atoms[A_LHSHELL_START]) {
            if (menu_owner && e.xclient.window == menu_owner) start_message(e.xclient, false);
            // the root broadcast (lhwm's Super key, or a classic button leaving the menu to luxshell): luxshell answers it
            // or forwards it to the provider while it runs; another provider owns the menu: theirs
            else if (e.xclient.window == root && !owned(luxshell_selection) && (menu_owner || !owned(menu_selection)))
                start_message(e.xclient, true);
            return;
        }
        if (e.xclient.window == tray_owner && tray_owner && e.xclient.message_type == atoms[A_TRAY_OPCODE]) {
            if (e.xclient.data.l[1] == 0 /* SYSTEM_TRAY_REQUEST_DOCK */) tray_dock((::Window)e.xclient.data.l[2]);
        }
        return;
    case DestroyNotify:
        tray_forget(e.xdestroywindow.window);
        return;
    case ReparentNotify:
        tray_reparented(e.xreparent.window, e.xreparent.parent);
        return;
    case SelectionClear:
        if (tray_owner && e.xselectionclear.window == tray_owner) {
            // another manager took it (ICCCM handover): give the icons back and wait until it is free again
            fprintf(stderr, "flxp-taskbar: another tray took the selection; icons handed back\n");
            tray_release_icons();
            XDestroyWindow(xd, tray_owner); tray_owner = None;
        } else if (menu_owner && e.xselectionclear.window == menu_owner) {
            fprintf(stderr, "flxp-taskbar: another start menu took _LH_STARTMENU_S%d\n", scr);
            XDestroyWindow(xd, menu_owner); menu_owner = None;
        }
        return;
    }
}

void run()
{
    int fd = ConnectionNumber(xd);
    for (;;) {
        std::vector<std::function<void()>> todo;
        {
            std::lock_guard<std::mutex> l(model_lock);
            todo.swap(requests);
        }
        for (auto& f : todo) { std::lock_guard<std::mutex> l(model_lock); f(); }
        if (!todo.empty()) XFlush(xd);
        if (!running) break;        // after the requests queued before stop()
        while (XPending(xd)) {
            XEvent e;
            XNextEvent(xd, &e);
            std::lock_guard<std::mutex> l(model_lock);
            handle(e);
        }
        bool tell = false;
        {
            std::lock_guard<std::mutex> l(model_lock);
            if (changes && !notified) { notified = true; tell = true; }
        }
        if (tell && notify_fn) notify_fn();
        XFlush(xd);
        struct pollfd p[2] = { { fd, POLLIN, 0 }, { wake_fds[0], POLLIN, 0 } };
        if (XPending(xd)) continue;
        bool waiting = tray_wanted && !tray_owner;
        if (poll(p, 2, waiting ? 2000 : -1) < 0 && errno != EINTR) break;
        if (waiting) {      // the tray selection was taken: claim it once it is free (luxshell yields, another tray quits)
            std::lock_guard<std::mutex> l(model_lock);
            if (XGetSelectionOwner(xd, tray_selection) == None) do_tray_start(tray_takeover);
        }
        if (p[1].revents & POLLIN) { char b[64]; while (read(wake_fds[0], b, sizeof b) > 0) {} }
    }
}

void queue(std::function<void()> f)
{
    {
        std::lock_guard<std::mutex> l(model_lock);
        requests.push_back(std::move(f));
    }
    if (wake_fds[1] >= 0) { char c = 1; ssize_t r = write(wake_fds[1], &c, 1); (void)r; }
}

} // namespace

void init_threads() { XInitThreads(); }

bool start(uint64_t own_window, Notify notify)
{
    xd = XOpenDisplay(nullptr);
    if (!xd) return false;
    previous_handler = XSetErrorHandler(on_x_error);
    scr = DefaultScreen(xd);
    root = RootWindow(xd, scr);
    own = (::Window)own_window;
    notify_fn = notify;
    XInternAtoms(xd, (char**)atom_names, A_COUNT, False, atoms);
    char name[64];
    snprintf(name, sizeof name, "_LHSHELL_S%d", scr);
    luxshell_selection = XInternAtom(xd, name, False);
    snprintf(name, sizeof name, "_LH_STARTMENU_S%d", scr);
    menu_selection = XInternAtom(xd, name, False);
    if (pipe(wake_fds) == 0)
        for (int f : wake_fds) { fcntl(f, F_SETFL, fcntl(f, F_GETFL) | O_NONBLOCK); fcntl(f, F_SETFD, FD_CLOEXEC); }
    XSelectInput(xd, root, PropertyChangeMask | SubstructureNotifyMask);   // Substructure: lhwm's _LHSHELL_START
    current_desktop = -1;
    read_current_desktop();
    read_client_list();
    read_active();
    changes = ChangeList | ChangeActive;
    running = true;
    worker = std::thread(run);
    return true;
}

void stop()
{
    if (!running) return;
    // the icons go back to the root: the save-set alone would put them into the
    // taskbar's own window (the nearest ancestor this connection did not create),
    // which goes away with the other connection
    queue([] {
        tray_release_icons();
        if (tray_owner) { XDestroyWindow(xd, tray_owner); tray_owner = None; }   // the selection goes with it
        do_menu_provide(false);
        menu_open = false;
        publish_menu_open();            // not left at 1 on the root after we are gone
        XSync(xd, False);
    });
    running = false;
    queue([] {});
    worker.join();
    XCloseDisplay(xd);
    xd = nullptr;
}

void snapshot(Snapshot& out)
{
    std::lock_guard<std::mutex> l(model_lock);
    out.tasks.clear();
    for (const Client& c : clients) if (c.eligible) out.tasks.push_back(c.t);
    out.active = active;
    out.changes = changes;
    out.have_wm = have_wm;
    out.start = start_req;
    out.menu_provider = menu_owner != None;
    changes = 0;
    notified = false;
}

void activate(uint64_t id)
{
    queue([id] { client_message((::Window)id, atoms[A_ACTIVE], 2 /* pager */, CurrentTime, (long)own, 0, 0); });
}

void minimize(uint64_t id)
{
    queue([id] { XIconifyWindow(xd, (::Window)id, scr); });
}

void close(uint64_t id)
{
    queue([id] { client_message((::Window)id, atoms[A_CLOSE], CurrentTime, 2, 0, 0, 0); });
}

void maximize(uint64_t id, int action)
{
    queue([id, action] { client_message((::Window)id, atoms[A_STATE], action, (long)atoms[A_MAX_V], (long)atoms[A_MAX_H], 2, 0); });
}

void set_icon_geometry(uint64_t id, int x, int y, int w, int h)
{
    queue([=] {
        long v[4] = { x, y, w, h };
        XChangeProperty(xd, (::Window)id, atoms[A_ICON_GEOMETRY], XA_CARDINAL, 32, PropModeReplace, (unsigned char*)v, 4);
    });
}

void focus_own()
{
    queue([] { XSetInputFocus(xd, own, RevertToPointerRoot, CurrentTime); });
}

void set_root_utf8(const std::string& prop, const std::string& value)
{
    queue([prop, value] {
        XChangeProperty(xd, root, XInternAtom(xd, prop.c_str(), False), atoms[A_UTF8], 8, PropModeReplace,
                        (const unsigned char*)value.data(), (int)value.size());
    });
}

void send_root_message(const std::string& type, long l0, long l1, long l2, long l3, long l4)
{
    queue([=] {
        XEvent e;
        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = root;
        e.xclient.message_type = XInternAtom(xd, type.c_str(), False);
        e.xclient.format = 32;
        e.xclient.data.l[0] = l0; e.xclient.data.l[1] = l1; e.xclient.data.l[2] = l2;
        e.xclient.data.l[3] = l3; e.xclient.data.l[4] = l4;
        XSendEvent(xd, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    });
}

void set_struts(int edge, int thickness, int start, int end)
{
    queue([=] {
        int order[4] = { 0, 2, 1, 3 };  // our edge numbering (left top right bottom) -> EWMH's (left right top bottom)
        int e = order[edge];
        long p[12] = {};
        p[e] = thickness;
        p[4 + e * 2] = start;
        p[5 + e * 2] = end;
        long st[4] = { p[0], p[1], p[2], p[3] };
        XChangeProperty(xd, own, atoms[A_STRUT_PARTIAL], XA_CARDINAL, 32, PropModeReplace, (unsigned char*)p, 12);
        XChangeProperty(xd, own, atoms[A_STRUT], XA_CARDINAL, 32, PropModeReplace, (unsigned char*)st, 4);
    });
}

static ::Window wm_check(Display* d, Atom check)
{
    Atom t; int f; unsigned long n, after; unsigned char* data = nullptr;
    ::Window w = None;
    if (XGetWindowProperty(d, DefaultRootWindow(d), check, 0, 1, False, XA_WINDOW, &t, &f, &n, &after, &data) == Success && data) {
        if (n == 1) w = (::Window)((unsigned long*)data)[0];
        XFree(data);
    }
    return w;
}

std::string wm_name()
{
    Display* d = XOpenDisplay(nullptr);
    if (!d) return "";
    std::string s;
    ::Window w = wm_check(d, XInternAtom(d, "_NET_SUPPORTING_WM_CHECK", False));
    if (w) {
        Atom t; int f; unsigned long n, after; unsigned char* data = nullptr;
        if (XGetWindowProperty(d, w, XInternAtom(d, "_NET_WM_NAME", False), 0, 256, False, AnyPropertyType, &t, &f, &n, &after,
                               &data) == Success && data) {
            if (f == 8) s.assign((const char*)data, n);
            XFree(data);
        }
    }
    XCloseDisplay(d);
    return s;
}

std::string wm_executable()
{
    Display* d = XOpenDisplay(nullptr);
    if (!d) return "";
    std::string s;
    ::Window w = wm_check(d, XInternAtom(d, "_NET_SUPPORTING_WM_CHECK", False));
    if (w) {
        Atom t; int f; unsigned long n, after; unsigned char* data = nullptr;
        if (XGetWindowProperty(d, w, XInternAtom(d, "_NET_WM_PID", False), 0, 1, False, XA_CARDINAL, &t, &f, &n, &after,
                               &data) == Success && data) {
            if (n == 1) {
                char link[64], buf[4096];
                snprintf(link, sizeof link, "/proc/%lu/exe", ((unsigned long*)data)[0]);
                ssize_t r = readlink(link, buf, sizeof buf - 1);
                if (r > 0) s.assign(buf, (size_t)r);
            }
            XFree(data);
        }
    }
    XCloseDisplay(d);
    return s;
}

void provide_start_menu(bool on)
{
    queue([on] { do_menu_provide(on); });
}

void set_start_menu_open(bool open)
{
    queue([open] { menu_open = open; publish_menu_open(); });
}

bool tray_start(bool takeover, bool vertical)
{
    tray_vertical = vertical;
    queue([takeover] { do_tray_start(takeover); });
    return true;
}

int tray_count()
{
    std::lock_guard<std::mutex> l(model_lock);
    int n = 0;
    for (const Icon& i : icons) if (i.mapped) n++;
    return n;
}

void tray_layout(int x, int y, int w, int h, int slot, int icon_size, bool vertical)
{
    queue([=] {
        tray_x = x; tray_y = y; tray_w = w; tray_h = h; tray_slot = slot; tray_icon = icon_size; tray_vertical = vertical;
        tray_relayout();
    });
}

void tray_background(const uint32_t* pixels, int w, int h, int stride)
{
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++) memcpy(&px[(size_t)y * w], pixels + (size_t)y * stride, (size_t)w * 4);
    queue([px, w, h]() mutable {
        tray_bg.swap(px); tray_bg_w = w; tray_bg_h = h;
        apply_tray_background();
    });
}

} // namespace xwm
