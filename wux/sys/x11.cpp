#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include "sys.h"

namespace sys {

struct NativeWindow {
    ::Window xw;
    GC gc;
    void* user;
    gdi::Rect rect;
    bool popup;
    bool argb;              // 32-bit TrueColor visual, own colormap; premultiplied ARGB contents
    bool mapped;
    Colormap cmap;          // argb only
    std::string title;
    bool has_extend;        // _LHWM_EXTEND_FRAME set
    long extend[4];         // left right top bottom
    uint32_t frame_flags;   // _LHWM_FRAME_FLAGS (0 = property absent)
    WindowType type;
    // set_transient_for / set_min_size, put back when set_argb recreates the X window
    bool transient = false, transient_modal = false;
    uint64_t transient_owner = 0;
    int min_w = 0, min_h = 0;
    std::vector<unsigned long> icon;   // _NET_WM_ICON (set_icon), put back likewise
};

static Display* dpy;
static int screen;
static ::Window root;
static Atom wm_delete, wm_protocols;
static Atom a_extend, a_frame_flags, a_composited, a_theme, a_metrics;
static Atom a_moveresize, a_state, a_max_v, a_max_h;
static Atom a_header_pixmap, a_caption_rect;
static std::vector<NativeWindow*> windows;
static XVisualInfo argb_visual;     // depth 32 TrueColor, if the server has one
static bool have_argb_visual;
static FrameInfo frame;             // root properties, refreshed on PropertyNotify
static std::vector<Atom> watched;    // watch_root_property(): id = index + 1

static void read_frame_info();
static void wake_init();
static void xdnd_init();
static void set_xdnd_aware(::Window xw);

// The X connection is gone (the server ended, or the client was killed): end the process at once. Xlib's default handler
// calls exit(), which runs the static destructors on whichever thread noticed while the others still run; a program with a
// thread of its own (flxp-taskbar's X worker, a std::thread at namespace scope) was then aborted by std::terminate, and two
// threads noticing together raced in exit(). Process-wide: it covers every connection the program opens, not only this one.
static int on_io_error(Display* d)
{
    const char* name = d ? DisplayString(d) : nullptr;
    fprintf(stderr, "X connection to %s broken (explicit kill or server shutdown).\n", name && *name ? name : "the display");
    fflush(nullptr);
    _exit(1);
}

bool init()
{
    XSetIOErrorHandler(on_io_error);
    dpy = XOpenDisplay(nullptr);
    if (!dpy) { fprintf(stderr, "sys: cannot open display\n"); return false; }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    a_extend = XInternAtom(dpy, "_LHWM_EXTEND_FRAME", False);
    a_frame_flags = XInternAtom(dpy, "_LHWM_FRAME_FLAGS", False);
    a_composited = XInternAtom(dpy, "_LHWM_COMPOSITED", False);
    a_theme = XInternAtom(dpy, "_LHWM_THEME", False);
    a_metrics = XInternAtom(dpy, "_LHWM_FRAME_METRICS", False);
    a_moveresize = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False);
    a_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    a_max_v = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    a_max_h = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    a_header_pixmap = XInternAtom(dpy, "_LHWM_HEADER_PIXMAP", False);
    a_caption_rect = XInternAtom(dpy, "_LHWM_CAPTION_RECT", False);
    have_argb_visual = XMatchVisualInfo(dpy, screen, 32, TrueColor, &argb_visual) != 0;
    // the window manager's frame state (lhwm/CLIENT-PROTOCOL.md) lives on the root
    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    XSelectInput(dpy, root, ra.your_event_mask | PropertyChangeMask);
    read_frame_info();
    wake_init();
    xdnd_init();
    return true;
}

void shutdown()
{
    if (dpy) XCloseDisplay(dpy);
    dpy = nullptr;
}

static NativeWindow* lookup(::Window xw)
{
    for (NativeWindow* w : windows) if (w->xw == xw) return w;
    return nullptr;
}

static void put_frame_props(NativeWindow* w)
{
    if (w->has_extend)
        XChangeProperty(dpy, w->xw, a_extend, XA_CARDINAL, 32, PropModeReplace, (const unsigned char*)w->extend, 4);
    else
        XDeleteProperty(dpy, w->xw, a_extend);
    if (w->frame_flags) {
        long v = (long)w->frame_flags;
        XChangeProperty(dpy, w->xw, a_frame_flags, XA_CARDINAL, 32, PropModeReplace, (const unsigned char*)&v, 1);
    } else {
        XDeleteProperty(dpy, w->xw, a_frame_flags);
    }
}

// (Re)create the X window of w with its current flags; rect, title and the
// frame properties carry over.
static void make_xwindow(NativeWindow* w)
{
    const gdi::Rect& r = w->rect;
    XSetWindowAttributes attr;
    unsigned long mask = CWEventMask | CWOverrideRedirect | CWSaveUnder;
    attr.override_redirect = w->popup ? True : False;
    attr.save_under = w->popup ? True : False;
    attr.event_mask = ExposureMask | StructureNotifyMask |
                      ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                      KeyPressMask | KeyReleaseMask | FocusChangeMask |
                      EnterWindowMask | LeaveWindowMask;
    if (!w->popup) attr.event_mask |= PropertyChangeMask;   // lhwm's _LHWM_HEADER_PIXMAP / _LHWM_CAPTION_RECT
    Visual* visual = CopyFromParent;
    int depth = CopyFromParent;
    if (w->argb) {
        // as x11/glassdemo.c: own colormap, border and background pixel 0
        // (transparent until the first present; None would show garbage)
        if (!w->cmap) w->cmap = XCreateColormap(dpy, root, argb_visual.visual, AllocNone);
        attr.colormap = w->cmap;
        attr.border_pixel = 0;
        attr.background_pixel = 0;
        mask |= CWColormap | CWBorderPixel | CWBackPixel;
        visual = argb_visual.visual;
        depth = 32;
    } else {
        attr.background_pixmap = None;
        mask |= CWBackPixmap;
    }
    w->xw = XCreateWindow(dpy, root, r.left, r.top,
                          r.width() > 0 ? r.width() : 1, r.height() > 0 ? r.height() : 1,
                          0, depth, InputOutput, visual, mask, &attr);
    w->gc = XCreateGC(dpy, w->xw, 0, nullptr);
    XSetWMProtocols(dpy, w->xw, &wm_delete, 1);
    XSizeHints hints;
    hints.flags = PPosition | PSize;
    hints.x = r.left; hints.y = r.top; hints.width = r.width(); hints.height = r.height();
    XSetWMNormalHints(dpy, w->xw, &hints);
    // WM_CLASS = <program>, "Flxp": lets picom/WM rules and xdotool --class find us
    XClassHint ch;
    ch.res_name = program_invocation_short_name;
    ch.res_class = (char*)"Flxp";
    XSetClassHint(dpy, w->xw, &ch);
    if (w->has_extend || w->frame_flags) put_frame_props(w);
    if (w->type != WindowType::Normal) set_window_type(w, w->type);
    if (!w->popup) set_xdnd_aware(w->xw);   // every toplevel takes drops (win32: RegisterDragDrop per window)
}

NativeWindow* create_window(const gdi::Rect& r, const char* title, void* user, bool popup, bool argb)
{
    NativeWindow* w = new NativeWindow;
    w->user = user;
    w->rect = r;
    w->popup = popup;
    w->argb = argb && have_argb_visual;
    w->mapped = false;
    w->cmap = 0;
    w->has_extend = false;
    w->extend[0] = w->extend[1] = w->extend[2] = w->extend[3] = 0;
    w->frame_flags = 0;
    w->type = WindowType::Normal;
    make_xwindow(w);
    set_title(w, title);
    windows.push_back(w);
    return w;
}

bool argb_supported() { return have_argb_visual; }
bool is_argb(NativeWindow* w) { return w->argb; }

bool set_argb(NativeWindow* w, bool on)
{
    if (on && !have_argb_visual) return false;
    if (w->argb == on) return true;
    // the visual of an X window is fixed: replace it (cheap before mapping,
    // which is when DwmExtendFrameIntoClientArea is normally called)
    bool was_mapped = w->mapped;
    XFreeGC(dpy, w->gc);
    XDestroyWindow(dpy, w->xw);
    w->argb = on;
    make_xwindow(w);
    std::string t = w->title;
    set_title(w, t.c_str());
    // what was set on the old X window (a dialog's owner, its minimum size)
    if (w->transient) set_transient_for(w, w->transient_owner, w->transient_modal);
    if (w->min_w || w->min_h) set_min_size(w, w->min_w, w->min_h);
    if (!w->icon.empty()) set_icon(w, std::vector<unsigned long>(w->icon));
    if (was_mapped) XMapWindow(dpy, w->xw);
    XFlush(dpy);
    return true;
}

void destroy_window(NativeWindow* w)
{
    for (size_t i = 0; i < windows.size(); i++)
        if (windows[i] == w) { windows.erase(windows.begin() + i); break; }
    XFreeGC(dpy, w->gc);
    XDestroyWindow(dpy, w->xw);
    if (w->cmap) XFreeColormap(dpy, w->cmap);
    XFlush(dpy);
    delete w;
}

// ---- lhwm frame protocol (lhwm/CLIENT-PROTOCOL.md) ------------------------

void set_frame_extension(NativeWindow* w, const int32_t* ltrb)
{
    w->has_extend = ltrb != nullptr;
    for (int i = 0; i < 4; i++) w->extend[i] = ltrb ? (long)(uint32_t)ltrb[i] : 0;
    put_frame_props(w);
    XFlush(dpy);
}

void set_frame_flags(NativeWindow* w, uint32_t flags)
{
    w->frame_flags = flags;
    put_frame_props(w);
    XFlush(dpy);
}

uint32_t frame_flags(NativeWindow* w) { return w->frame_flags; }

// up to n cardinals of a root property; the number read (0: absent)
static int read_cardinals(Atom prop, long* out, int n)
{
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    int got = 0;
    if (XGetWindowProperty(dpy, root, prop, 0, n, False, XA_CARDINAL, &type, &format, &count, &after, &data) == Success && data) {
        if (format == 32) {
            got = (int)count < n ? (int)count : n;
            for (int i = 0; i < got; i++) out[i] = ((long*)data)[i];
        }
        XFree(data);
    }
    return got;
}

static bool get_cardinals(Atom prop, long* out, int n) { return read_cardinals(prop, out, n) == n; }

static void read_frame_info()
{
    long v = 0;
    frame.composited = get_cardinals(a_composited, &v, 1) && v == 1;
    long m[18];
    int got = read_cardinals(a_metrics, m, 18);   // 8, 14 or 18 items by lhwm's age (CLIENT-PROTOCOL.md)
    frame.have_metrics = got >= 8;
    frame.have_max = got >= 17;
    if (frame.have_max) {
        frame.max_text_rgb = (uint32_t)m[14] & 0xffffff; frame.max_inactive_text_rgb = (uint32_t)m[15] & 0xffffff;
        frame.max_glow_rgb = (uint32_t)m[16];
    }
    if (frame.have_metrics) {
        frame.caption_h = (int32_t)m[0]; frame.border_l = (int32_t)m[1];
        frame.border_r = (int32_t)m[2]; frame.border_b = (int32_t)m[3];
        frame.text_rgb = (uint32_t)m[4] & 0xffffff; frame.glow_rgb = (uint32_t)m[5] & 0xffffff;
        frame.inactive_text_rgb = (uint32_t)m[6] & 0xffffff;
    }
    frame.theme.clear();
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, root, a_theme, 0, 1024, False, AnyPropertyType, &type, &format, &count, &after, &data) == Success && data) {
        if (format == 8) frame.theme.assign((const char*)data, count);
        XFree(data);
    }
}

const FrameInfo& frame_info() { return frame; }

static bool x_failed;
static int note_x_error(Display*, XErrorEvent*) { x_failed = true; return 0; }

static bool window_cardinals(NativeWindow* w, Atom prop, Atom type, long* out, int n)
{
    Atom t; int format; unsigned long count, after; unsigned char* data = nullptr;
    bool ok = false;
    if (XGetWindowProperty(dpy, w->xw, prop, 0, n, False, type, &t, &format, &count, &after, &data) == Success && data) {
        if (format == 32 && (int)count == n) {
            for (int i = 0; i < n; i++) out[i] = ((long*)data)[i];
            ok = true;
        }
        XFree(data);
    }
    return ok;
}

// lhwm frees the previous pixmap right after replacing the property, so the
// id read here can be gone by the time it is copied: errors are caught and
// the PropertyNotify of the new one repaints.
gdi::Bitmap* header_pixmap(NativeWindow* w)
{
    long id = 0;
    if (!window_cardinals(w, a_header_pixmap, XA_PIXMAP, &id, 1) || !id) return nullptr;
    XSync(dpy, False);
    x_failed = false;
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(note_x_error);
    ::Window r; int px, py; unsigned pw = 0, ph = 0, bw, depth = 0;
    XImage* img = nullptr;
    if (XGetGeometry(dpy, (Pixmap)id, &r, &px, &py, &pw, &ph, &bw, &depth) && !x_failed && pw && ph)
        img = XGetImage(dpy, (Pixmap)id, 0, 0, pw, ph, AllPlanes, ZPixmap);
    XSync(dpy, False);
    XSetErrorHandler(old);
    if (!img) return nullptr;
    gdi::Bitmap* b = nullptr;
    if (!x_failed) {
        b = gdi::create_bitmap((int32_t)pw, (int32_t)ph);
        for (unsigned y = 0; y < ph; y++) {
            uint32_t* d = b->row((int32_t)y);
            for (unsigned x = 0; x < pw; x++) {
                uint32_t v = (uint32_t)XGetPixel(img, (int)x, (int)y);
                d[x] = depth == 32 ? v : (v | 0xff000000u);   // depth 32: premultiplied ARGB as the visual has it
            }
        }
    }
    XDestroyImage(img);
    return b;
}

bool caption_rect(NativeWindow* w, gdi::Rect& r)
{
    long v[4];
    if (!window_cardinals(w, a_caption_rect, XA_CARDINAL, v, 4) || v[2] <= 0 || v[3] <= 0) return false;
    r = gdi::rect_xywh((int32_t)v[0], (int32_t)v[1], (int32_t)v[2], (int32_t)v[3]);
    return true;
}

void set_shape(NativeWindow* w, const std::vector<gdi::Rect>* rects)
{
    if (!rects) {
        XShapeCombineMask(dpy, w->xw, ShapeBounding, 0, 0, None, ShapeSet);
    } else {
        std::vector<XRectangle> xr;
        for (const gdi::Rect& r : *rects)
            if (r.width() > 0 && r.height() > 0)
                xr.push_back(XRectangle{ (short)r.left, (short)r.top, (unsigned short)r.width(), (unsigned short)r.height() });
        XShapeCombineRectangles(dpy, w->xw, ShapeBounding, 0, 0, xr.data(), (int)xr.size(), ShapeSet, Unsorted);
    }
    XFlush(dpy);
}

void set_input_hole(NativeWindow* w, const gdi::Rect* hole)
{
    if (!hole) {
        XShapeCombineMask(dpy, w->xw, ShapeInput, 0, 0, None, ShapeSet);   // the whole window again
    } else {
        XWindowAttributes a;
        XGetWindowAttributes(dpy, w->xw, &a);
        XRectangle all = { 0, 0, (unsigned short)a.width, (unsigned short)a.height };
        XShapeCombineRectangles(dpy, w->xw, ShapeInput, 0, 0, &all, 1, ShapeSet, Unsorted);
        XRectangle h = { (short)hole->left, (short)hole->top, (unsigned short)hole->width(), (unsigned short)hole->height() };
        XShapeCombineRectangles(dpy, w->xw, ShapeInput, 0, 0, &h, 1, ShapeSubtract, Unsorted);
    }
    XFlush(dpy);
}

// EWMH: client messages about our toplevel go to the root.
static void root_message(NativeWindow* w, Atom type, long l0, long l1, long l2, long l3, long l4)
{
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = w->xw;
    e.xclient.message_type = type;
    e.xclient.format = 32;
    e.xclient.data.l[0] = l0; e.xclient.data.l[1] = l1; e.xclient.data.l[2] = l2;
    e.xclient.data.l[3] = l3; e.xclient.data.l[4] = l4;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(dpy);
}

void begin_move_resize(NativeWindow* w, gdi::Point root_pos, int direction, int button, uint32_t time)
{
    // the press gave us an implicit pointer grab; the WM cannot grab while we hold it
    XUngrabPointer(dpy, time ? (Time)time : CurrentTime);
    root_message(w, a_moveresize, root_pos.x, root_pos.y, direction, button, 1 /* source: application */);
}

static bool own_grab;   // grab_pointer holds one: its LeaveNotify/NotifyGrab is not another client's

bool grab_pointer(NativeWindow* w, bool on)
{
    own_grab = on;
    if (!on) { XUngrabPointer(dpy, CurrentTime); XFlush(dpy); return true; }
    int r = XGrabPointer(dpy, w->xw, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XFlush(dpy);
    own_grab = r == GrabSuccess;
    return own_grab;
}

bool is_maximized(NativeWindow* w)
{
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    bool v = false, h = false;
    if (XGetWindowProperty(dpy, w->xw, a_state, 0, 64, False, XA_ATOM, &type, &format, &count, &after, &data) == Success && data) {
        for (unsigned long i = 0; i < count; i++) {
            Atom a = (Atom)((long*)data)[i];
            if (a == a_max_v) v = true;
            if (a == a_max_h) h = true;
        }
        XFree(data);
    }
    return v && h;
}

void set_maximized(NativeWindow* w, int action)
{
    root_message(w, a_state, action, (long)a_max_v, (long)a_max_h, 1, 0);
}

void* window_user(NativeWindow* w) { return w->user; }

void set_cursor(NativeWindow* w, PointerShape shape)
{
    static ::Cursor cache[12];
    static const unsigned shapes[] = { 68 /*XC_left_ptr*/, 152 /*xterm*/, 150 /*watch*/, 34 /*crosshair*/,
        108 /*sb_h_double_arrow*/, 116 /*sb_v_double_arrow*/, 134 /*top_left_corner*/,
        136 /*top_right_corner*/, 52 /*fleur*/, 60 /*hand2*/, 0 /*X_cursor*/, 0 };
    int i = (int)shape;
    if (shape == PointerShape::Hidden) { XUndefineCursor(dpy, w->xw); return; }
    if (!cache[i]) cache[i] = XCreateFontCursor(dpy, shapes[i]);
    XDefineCursor(dpy, w->xw, cache[i]);
    XFlush(dpy);
}

void raise_window(NativeWindow* w) { XRaiseWindow(dpy, w->xw); XFlush(dpy); }

static int ignore_x_error(Display*, XErrorEvent*);

void activate_window(NativeWindow* w)
{
    root_message(w, XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False), 1, CurrentTime, 0, 0, 0);
    XRaiseWindow(dpy, w->xw);
    XWindowAttributes a;
    if (w->mapped && XGetWindowAttributes(dpy, w->xw, &a) && a.map_state == IsViewable) {
        XSync(dpy, False);
        int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);
        XSetInputFocus(dpy, w->xw, RevertToParent, CurrentTime);
        XSync(dpy, False);
        XSetErrorHandler(old);
    }
    XFlush(dpy);
}

void show_window(NativeWindow* w, bool show)
{
    w->mapped = show;
    // win32 shows a popup on top (ShowWindow and SWP_SHOWWINDOW without SWP_NOZORDER;
    // a combo's list and menus use HWND_TOPMOST). An override-redirect window
    // keeps its old place in the stack when mapped again, under windows the WM
    // raised in the meantime (a dropdown under its dialog after a nested dialog)
    if (show && w->popup) XMapRaised(dpy, w->xw);
    else if (show) XMapWindow(dpy, w->xw); else XUnmapWindow(dpy, w->xw);
    if (show && w->type == WindowType::Desktop) XLowerWindow(dpy, w->xw);   // without a window manager too
    XFlush(dpy);
}

void set_title(NativeWindow* w, const char* title)
{
    if (title != w->title.c_str()) w->title = title;
    XStoreName(dpy, w->xw, title);
    Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    XChangeProperty(dpy, w->xw, net_name, utf8, 8, PropModeReplace,
                    (const unsigned char*)title, (int)strlen(title));
}

void set_window_rect(NativeWindow* w, const gdi::Rect& r)
{
    XMoveResizeWindow(dpy, w->xw, r.left, r.top, r.width(), r.height());
    XFlush(dpy);
}

gdi::Rect window_rect(NativeWindow* w) { return w->rect; }

void present(NativeWindow* w, const gdi::Bitmap* bmp, const gdi::Rect& a)
{
    gdi::Rect r = gdi::intersect(a, bmp->rect());
    if (r.empty()) return;
    // ARGB windows: the backbuffer is premultiplied BGRA, which is exactly
    // the depth-32 TrueColor visual's ZPixmap layout (a 32bpp image).
    Visual* vis = w->argb ? argb_visual.visual : DefaultVisual(dpy, screen);
    unsigned depth = w->argb ? 32u : (unsigned)DefaultDepth(dpy, screen);
    XImage* img = XCreateImage(dpy, vis, depth, ZPixmap, 0, (char*)bmp->pixels, (unsigned)bmp->width, (unsigned)bmp->height,
                               32, bmp->stride * 4);
    if (!img) return;
    XPutImage(dpy, w->xw, w->gc, img, r.left, r.top, r.left, r.top, (unsigned)r.width(), (unsigned)r.height());
    img->data = nullptr;
    XDestroyImage(img);
    XFlush(dpy);
}

static uint32_t mods_from_state(unsigned state)
{
    uint32_t m = 0;
    if (state & ShiftMask) m |= ModShift;
    if (state & ControlMask) m |= ModControl;
    if (state & Mod1Mask) m |= ModAlt;
    if (state & Button1Mask) m |= ModLButton;
    if (state & Button2Mask) m |= ModMButton;
    if (state & Button3Mask) m |= ModRButton;
    return m;
}


// ---- drag and drop: XDND 5 ----------------------------------------------------------
// Source (drag_begin) and target (every toplevel XdndAware) on the main
// connection. Our own windows are found and messaged like foreign ones, so a
// drag between two windows of this process takes the same path through the
// server; only the data is not converted through the selection then.

static Atom a_aware, a_enter, a_position, a_status, a_leave, a_drop, a_finished, a_xsel, a_typelist,
    a_actionlist, a_copy, a_move, a_link, a_uri_list, a_gnome, a_utf8, a_text_plain, a_dnd_prop, a_targets, a_cm;
static Time last_time = CurrentTime;    // the last input event's server time
static bool drag_changed;               // wait_event reports a Nothing event (the source's drag ended)

static void xdnd_init()
{
    a_aware = XInternAtom(dpy, "XdndAware", False);
    a_enter = XInternAtom(dpy, "XdndEnter", False);
    a_position = XInternAtom(dpy, "XdndPosition", False);
    a_status = XInternAtom(dpy, "XdndStatus", False);
    a_leave = XInternAtom(dpy, "XdndLeave", False);
    a_drop = XInternAtom(dpy, "XdndDrop", False);
    a_finished = XInternAtom(dpy, "XdndFinished", False);
    a_xsel = XInternAtom(dpy, "XdndSelection", False);
    a_typelist = XInternAtom(dpy, "XdndTypeList", False);
    a_actionlist = XInternAtom(dpy, "XdndActionList", False);
    a_copy = XInternAtom(dpy, "XdndActionCopy", False);
    a_move = XInternAtom(dpy, "XdndActionMove", False);
    a_link = XInternAtom(dpy, "XdndActionLink", False);
    a_uri_list = XInternAtom(dpy, "text/uri-list", False);
    a_gnome = XInternAtom(dpy, "x-special/gnome-copied-files", False);
    a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    a_text_plain = XInternAtom(dpy, "text/plain;charset=utf-8", False);
    a_dnd_prop = XInternAtom(dpy, "FLXP_XDND", False);
    a_targets = XInternAtom(dpy, "TARGETS", False);
    char cm[32];
    snprintf(cm, sizeof cm, "_NET_WM_CM_S%d", screen);
    a_cm = XInternAtom(dpy, cm, False);
}

static void set_xdnd_aware(::Window xw)
{
    long v = 5;
    XChangeProperty(dpy, xw, a_aware, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&v, 1);
}

static Atom action_atom(uint32_t e) { return e & DropMove ? a_move : e & DropCopy ? a_copy : e & DropLink ? a_link : None; }
static uint32_t effect_of(Atom a) { return a == a_move ? DropMove : a == a_copy ? DropCopy : a == a_link ? DropLink : 0; }

// file: URIs (RFC 8089 as file managers write them: file:///path, percent-encoded)
static std::string uri_of(const std::string& path)
{
    static const char* hex = "0123456789ABCDEF";
    std::string u = "file://";
    for (unsigned char c : path) {
        if (isalnum(c) || strchr("/-_.~!$&'()*+,;=:@", c)) u += (char)c;
        else { u += '%'; u += hex[c >> 4]; u += hex[c & 15]; }
    }
    return u;
}

static std::string path_of_uri(const std::string& u)
{
    if (u.compare(0, 5, "file:") != 0) return "";
    std::string rest = u.substr(5);
    if (rest.compare(0, 2, "//") == 0) {
        size_t slash = rest.find('/', 2);
        if (slash == std::string::npos) return "";
        std::string host = rest.substr(2, slash - 2);
        char me[256] = "";
        gethostname(me, sizeof me - 1);
        if (!host.empty() && host != "localhost" && host != me) return "";   // another machine's file
        rest = rest.substr(slash);
    }
    std::string p;
    for (size_t i = 0; i < rest.size(); i++) {
        if (rest[i] == '%' && i + 2 < rest.size() && isxdigit((unsigned char)rest[i + 1]) && isxdigit((unsigned char)rest[i + 2])) {
            p += (char)strtol(rest.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else p += rest[i];
    }
    return p.empty() || p[0] != '/' ? "" : p;
}

static void parse_uri_list(const std::string& s, bool gnome, std::vector<std::string>& out)
{
    size_t at = 0;
    bool first = true;
    while (at < s.size()) {
        size_t nl = s.find('\n', at);
        std::string line = s.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? s.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (gnome && first) { first = false; if (line == "copy" || line == "cut") continue; }
        first = false;
        if (line.empty() || line[0] == '#') continue;
        std::string p = path_of_uri(line);
        if (!p.empty()) out.push_back(p);
    }
}

static bool dnd_debug() { static int on = getenv("FLXP_DEBUG_DND") ? 1 : 0; return on; }
static std::string atom_name(Atom a)
{
    char* n = a ? XGetAtomName(dpy, a) : nullptr;
    std::string s = n ? n : "None";
    if (n) XFree(n);
    return s;
}

static void client_message(::Window to, Atom type, long l0, long l1, long l2, long l3, long l4)
{
    if (dnd_debug()) fprintf(stderr, "xdnd: -> 0x%lx %s %lx %lx %lx %lx %s\n", to, atom_name(type).c_str(), l0, l1, l2, l3,
                             type == a_position || type == a_status ? atom_name((Atom)l4).c_str() : "");
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = to;
    e.xclient.message_type = type;
    e.xclient.format = 32;
    e.xclient.data.l[0] = l0; e.xclient.data.l[1] = l1; e.xclient.data.l[2] = l2;
    e.xclient.data.l[3] = l3; e.xclient.data.l[4] = l4;
    XSync(dpy, False);
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);   // the target may be gone
    XSendEvent(dpy, to, False, NoEventMask, &e);
    XSync(dpy, False);
    XSetErrorHandler(old);
}

uint32_t query_mods()
{
    ::Window r, c; int rx, ry, wx, wy; unsigned mask = 0;
    XQueryPointer(dpy, root, &r, &c, &rx, &ry, &wx, &wy, &mask);
    return mods_from_state(mask);
}

// ---- source ----

// The drag cursors: XP's arrow with a dotted rectangle (move), a plus (copy),
// a curved arrow (link), and the "no" circle. 'X' black, 'o' white.
static const char* const cur_arrow[] = {
    "X", "XX", "XoX", "XooX", "XoooX", "XooooX", "XoooooX", "XooooooX", "XoooooooX", "XooooooooX",
    "XoooooXXXXX", "XooXooX", "XoX XooX", "XX  XooX", "X    XooX", "     XooX", "      XooX", "      XooX", "       XX",
};
static const char* const badge_plus[] = {
    "XXXXXXXXXXX", "XoooooooooX", "XooooXooooX", "XooooXooooX", "XooooXooooX", "XoXXXXXXXoX",
    "XooooXooooX", "XooooXooooX", "XooooXooooX", "XoooooooooX", "XXXXXXXXXXX",
};
static const char* const badge_link[] = {
    "XXXXXXXXXXX", "XoooooooooX", "XoooooXXXoX", "XooooooXXoX", "XoooooXoXoX", "XooXXXooooX",
    "XoXoooooooX", "XoXoooooooX", "XoooooooooX", "XoooooooooX", "XXXXXXXXXXX",
};
static const char* const cur_no[] = {
    "     XXXXXX     ", "   XXooooooXX   ", "  XooXXXXXXooX  ", " XoXXXX  XXXXoX ", " XoXXX     XXoX ",
    "XoXXXXX     XXoX", "XoXX XXX     XoX", "XoX   XXX    XoX", "XoX    XXX   XoX", "XoX     XXX XXoX",
    "XoXX     XXXXXoX", " XoXX     XXXoX ", " XoXXXX  XXXXoX ", "  XooXXXXXXooX  ", "   XXooooooXX   ", "     XXXXXX     ",
};

static ::Cursor make_cursor(int kind)   // 0 no, 1 copy, 2 move, 3 link
{
    const int N = 32;
    char art[N][N];
    memset(art, ' ', sizeof art);
    auto put = [&](const char* const* rows, int nrows, int x0, int y0) {
        for (int y = 0; y < nrows; y++)
            for (int x = 0; rows[y][x]; x++)
                if (rows[y][x] != ' ' && y0 + y < N && x0 + x < N) art[y0 + y][x0 + x] = rows[y][x];
    };
    if (kind == 0) put(cur_no, 16, 0, 0);
    else {
        put(cur_arrow, 19, 0, 0);
        for (int x = 8; x <= 20; x++) { art[20][x] = art[28][x] = (x & 1) ? 'X' : 'o'; }   // the dotted drag rectangle
        for (int y = 20; y <= 28; y++) { art[y][8] = art[y][20] = (y & 1) ? 'X' : 'o'; }
        if (kind == 1) put(badge_plus, 11, 16, 21);
        if (kind == 3) put(badge_link, 11, 16, 21);
    }
    unsigned char src[N * N / 8] = {}, mask[N * N / 8] = {};
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int bit = y * (N / 8) * 8 + x;
            if (art[y][x] == 'X') src[bit / 8] |= (unsigned char)(1 << (bit % 8));
            if (art[y][x] != ' ') mask[bit / 8] |= (unsigned char)(1 << (bit % 8));
        }
    Pixmap ps = XCreateBitmapFromData(dpy, root, (const char*)src, N, N);
    Pixmap pm = XCreateBitmapFromData(dpy, root, (const char*)mask, N, N);
    XColor black = {}, white = {};
    white.red = white.green = white.blue = 0xffff;
    black.flags = white.flags = DoRed | DoGreen | DoBlue;
    ::Cursor c = XCreatePixmapCursor(dpy, ps, pm, &black, &white, kind == 0 ? 7 : 0, kind == 0 ? 7 : 0);
    XFreePixmap(dpy, ps);
    XFreePixmap(dpy, pm);
    return c;
}

struct DragSource {
    bool active = false;
    NativeWindow* w = nullptr;
    ::Window xw = 0;                        // owns XdndSelection (kept after the drag for late requests)
    std::vector<std::string> paths;
    uint32_t allowed = 0, result = 0;
    ::Window img = 0;                       // the drag image (override-redirect, not one of ours)
    gdi::Point hot{0, 0};
    std::vector<std::pair<::Window, gdi::Rect>> tops;   // root's mapped children, topmost first
    ::Window target = 0;
    int version = 0;
    bool status_pending = false, position_pending = false, release_pending = false, dropped = false;
    bool accepted = false;
    uint32_t accepted_effect = 0;
    gdi::Point at{0, 0};
    uint32_t deadline = 0;                  // ticks: give up waiting for XdndStatus / XdndFinished
    ::Cursor cursors[4] = {};
    int cursor = -1;
    // Windows' "hovered window comes to the front": the toplevel the pointer rests over
    ::Window hover = 0;                     // root child under the pointer (a WM frame or a bare client)
    gdi::Point hover_at{0, 0};              // where the rest began
    uint32_t hover_since = 0;
    bool hover_done = false;                // raised (or not raisable): once per rest
    uint32_t restack_at = 0;                // ticks: re-read the stacking order after a raise
};
static DragSource ds;

static uint32_t requested_effect(uint32_t mods)
{
    // XP: Ctrl copies, Ctrl+Shift links; Shift also copies here (the user's
    // "hold Shift to copy"; XP's Shift forces a move). Plain: a move, which
    // a flxp target turns into a copy across filesystems.
    uint32_t want = (mods & ModControl) && (mods & ModShift) ? DropLink : (mods & (ModControl | ModShift)) ? DropCopy : DropMove;
    if (want & ds.allowed) return want;
    for (uint32_t e : { DropMove, DropCopy, DropLink }) if (e & ds.allowed) return e;
    return 0;
}

static void set_drag_cursor()
{
    int k = !ds.target || !ds.accepted ? 0 : ds.accepted_effect == DropCopy ? 1 : ds.accepted_effect == DropLink ? 3 : 2;
    if (k == ds.cursor) return;
    ds.cursor = k;
    if (!ds.cursors[k]) ds.cursors[k] = make_cursor(k);
    XChangeActivePointerGrab(dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, ds.cursors[k], CurrentTime);
    XFlush(dpy);
}

static bool xdnd_aware(::Window w, int& version)
{
    Atom type; int format; unsigned long n, after; unsigned char* data = nullptr;
    bool ok = false;
    if (XGetWindowProperty(dpy, w, a_aware, 0, 1, False, XA_ATOM, &type, &format, &n, &after, &data) == Success && data) {
        if (format == 32 && n == 1) { version = (int)((long*)data)[0]; ok = version >= 3; }
        XFree(data);
    }
    return ok;
}

static ::Window find_target(gdi::Point p, int& version)
{
    ::Window w = 0;
    for (auto& t : ds.tops) if (t.second.contains(p)) { w = t.first; break; }
    XSync(dpy, False);
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);
    ::Window found = 0;
    for (int depth = 0; w && depth < 10; depth++) {
        if (xdnd_aware(w, version)) { found = w; break; }
        int x, y; ::Window child = 0;
        if (!XTranslateCoordinates(dpy, root, w, p.x, p.y, &x, &y, &child)) break;
        w = child;
    }
    XSync(dpy, False);
    XSetErrorHandler(old);
    return found;
}

static void snapshot_tops()
{
    ds.tops.clear();   // (the drag image is skipped below)
    ::Window r, parent, *kids = nullptr; unsigned n = 0;
    if (!XQueryTree(dpy, root, &r, &parent, &kids, &n)) return;
    XSync(dpy, False);
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);
    for (unsigned i = n; i-- > 0;) {
        XWindowAttributes a;
        if (kids[i] == ds.img) continue;
        if (!XGetWindowAttributes(dpy, kids[i], &a) || a.map_state != IsViewable || a.c_class != InputOutput) continue;
        ds.tops.push_back({ kids[i], gdi::rect_xywh(a.x, a.y, a.width + 2 * a.border_width, a.height + 2 * a.border_width) });
    }
    XSync(dpy, False);
    XSetErrorHandler(old);
    if (kids) XFree(kids);
}

static void send_position()
{
    client_message(ds.target, a_position, (long)ds.xw, 0, ((long)ds.at.x << 16) | (ds.at.y & 0xffff), (long)last_time,
                   (long)action_atom(requested_effect(query_mods())));
    ds.status_pending = true;
    ds.position_pending = false;
    ds.deadline = ticks() + 2000;
}

static void drag_end(uint32_t result)
{
    ds.active = false;
    ds.result = result;
    ds.target = 0;
    drag_changed = true;
}

static void ungrab_drag()
{
    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    own_grab = false;
    if (ds.img) { XDestroyWindow(dpy, ds.img); ds.img = 0; }
    XFlush(dpy);
}

// after the button went up (and the last position was answered): drop or give up
static void source_release()
{
    ds.release_pending = false;
    if (ds.target && ds.accepted) {
        client_message(ds.target, a_drop, (long)ds.xw, 0, (long)last_time, 0, 0);
        ds.dropped = true;
        ds.deadline = ticks() + 60000;     // the target may ask questions first (a flxp target's message boxes)
    } else {
        if (ds.target) client_message(ds.target, a_leave, (long)ds.xw, 0, 0, 0, 0);
        drag_end(0);
    }
}

// The pointer resting over another toplevel for a second raises it (Windows: a drag across
// windows brings the one under the pointer to the front, and leaves it there). "Resting":
// within 8 px; a toplevel already on top (the first that is not override-redirect) is left.
static const uint32_t HOVER_RAISE_MS = 1000;
static void track_hover(gdi::Point p)
{
    ::Window top = 0;
    for (auto& t : ds.tops) if (t.second.contains(p)) { top = t.first; break; }
    int dx = p.x - ds.hover_at.x, dy = p.y - ds.hover_at.y;
    if (top != ds.hover || dx > 8 || dx < -8 || dy > 8 || dy < -8) {
        if (top != ds.hover) ds.hover_done = false;
        ds.hover = top;
        ds.hover_at = p;
        ds.hover_since = ticks();
    }
}

// the managed client inside a WM frame: the window with WM_STATE (XmuClientWindow's search)
static ::Window client_of(::Window w, int depth = 0)
{
    static Atom wm_state = XInternAtom(dpy, "WM_STATE", False);
    Atom type = None; int format; unsigned long n, after; unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, w, wm_state, 0, 0, False, AnyPropertyType, &type, &format, &n, &after, &data) == Success) {
        if (data) XFree(data);
        if (type != None) return w;
    }
    if (depth > 3) return 0;
    ::Window r, parent, *kids = nullptr; unsigned nk = 0;
    ::Window found = 0;
    if (XQueryTree(dpy, w, &r, &parent, &kids, &nk))
        for (unsigned i = nk; i-- > 0 && !found;) found = client_of(kids[i], depth + 1);
    if (kids) XFree(kids);
    return found;
}

static void hover_raise()
{
    if (!ds.hover || ds.hover_done || ds.dropped || ds.release_pending) return;
    if ((int)(ticks() - ds.hover_since) < (int)HOVER_RAISE_MS) return;
    ds.hover_done = true;
    XSync(dpy, False);
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);
    ::Window topmost = 0;
    for (auto& t : ds.tops) {
        XWindowAttributes a;
        if (XGetWindowAttributes(dpy, t.first, &a) && !a.override_redirect) { topmost = t.first; break; }
    }
    XWindowAttributes ha;
    bool redirect = XGetWindowAttributes(dpy, ds.hover, &ha) && ha.override_redirect;   // menus, docks' popups, ...
    ::Window client = 0;
    if (ds.hover != topmost && !redirect) {
        client = client_of(ds.hover);
        if (!client) client = ds.hover;     // unmanaged (no window manager): the root child is the client
    }
    if (client) {
        // EWMH activation, source indication 2 (a pager: the user asked for it); no window
        // manager (nobody owns _NET_SUPPORTING_WM_CHECK's window): restack it ourselves
        Atom check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
        Atom type = None; int format; unsigned long n = 0, after; unsigned char* data = nullptr;
        bool wm = XGetWindowProperty(dpy, root, check, 0, 1, False, XA_WINDOW, &type, &format, &n, &after, &data) == Success
                  && data && n == 1 && type == XA_WINDOW;
        if (data) XFree(data);
        if (dnd_debug()) fprintf(stderr, "xdnd: raise 0x%lx (client 0x%lx)%s\n", ds.hover, client, wm ? "" : " without a window manager");
        XEvent e;
        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = client;
        e.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        e.xclient.format = 32;
        e.xclient.data.l[0] = 2;
        e.xclient.data.l[1] = (long)last_time;
        e.xclient.data.l[2] = (long)ds.xw;
        XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
        if (!wm) XRaiseWindow(dpy, ds.hover);
        if (ds.img) XRaiseWindow(dpy, ds.img);
        ds.restack_at = ticks() + 150;      // then the new order decides which window gets the drop
    }
    XSync(dpy, False);
    XSetErrorHandler(old);
}

static void source_motion(gdi::Point p)
{
    ds.at = p;
    track_hover(p);
    if (ds.img) XMoveWindow(dpy, ds.img, p.x - ds.hot.x, p.y - ds.hot.y);
    int version = 0;
    ::Window t = find_target(p, version);
    if (t != ds.target) {
        if (ds.target) client_message(ds.target, a_leave, (long)ds.xw, 0, 0, 0, 0);
        ds.target = t;
        ds.accepted = false;
        ds.status_pending = ds.position_pending = false;
        if (t) {
            ds.version = version < 5 ? version : 5;
            long types[3] = { (long)a_uri_list, (long)a_gnome, (long)a_utf8 };
            client_message(t, a_enter, (long)ds.xw, ((long)ds.version << 24) | 1, types[0], types[1], types[2]);
        }
    }
    if (ds.target) {
        if (ds.status_pending) ds.position_pending = true;
        else send_position();
    }
    set_drag_cursor();
}

bool drag_begin(NativeWindow* w, const std::vector<std::string>& paths, uint32_t allowed, const gdi::Bitmap* image, gdi::Point hotspot)
{
    if (ds.active || paths.empty() || !allowed) return false;
    DragSource n;
    for (int i = 0; i < 4; i++) n.cursors[i] = ds.cursors[i];
    n.w = w; n.xw = w->xw; n.paths = paths; n.allowed = allowed; n.hot = hotspot;
    ds = n;
    if (!ds.cursors[0]) ds.cursors[0] = make_cursor(0);
    if (XGrabPointer(dpy, w->xw, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
                     None, ds.cursors[0], CurrentTime) != GrabSuccess)
        return false;
    own_grab = true;
    XGrabKeyboard(dpy, w->xw, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    XSetSelectionOwner(dpy, a_xsel, w->xw, last_time);
    Atom types[] = { a_uri_list, a_gnome, a_utf8, a_text_plain, XA_STRING };
    XChangeProperty(dpy, w->xw, a_typelist, XA_ATOM, 32, PropModeReplace, (const unsigned char*)types, 5);
    Atom acts[3]; int na = 0;
    for (uint32_t e : { DropCopy, DropMove, DropLink }) if (allowed & e) acts[na++] = action_atom(e);
    XChangeProperty(dpy, w->xw, a_actionlist, XA_ATOM, 32, PropModeReplace, (const unsigned char*)acts, na);
    snapshot_tops();
    if (image && image->width > 0 && image->height > 0) {
        // semi-transparent under a compositing manager (ARGB); else XP's fallback
        // without alpha: the opaque pixels, every other one (a dithered ghost)
        bool argb = have_argb_visual && XGetSelectionOwner(dpy, a_cm) != None;
        XSetWindowAttributes attr = {};
        unsigned long mask = CWOverrideRedirect | CWSaveUnder | CWBorderPixel;
        attr.override_redirect = True; attr.save_under = True;
        Visual* vis = DefaultVisual(dpy, screen);
        int depth = DefaultDepth(dpy, screen);
        if (argb) {
            attr.colormap = XCreateColormap(dpy, root, argb_visual.visual, AllocNone);
            attr.background_pixel = 0;
            mask |= CWColormap | CWBackPixel;
            vis = argb_visual.visual; depth = 32;
        }
        gdi::Point p = pointer_pos();
        ds.img = XCreateWindow(dpy, root, p.x - hotspot.x, p.y - hotspot.y, (unsigned)image->width, (unsigned)image->height, 0,
                                depth, InputOutput, vis, mask, &attr);
        XRectangle none = {};
        XShapeCombineRectangles(dpy, ds.img, ShapeInput, 0, 0, &none, 0, ShapeSet, Unsorted);
        std::vector<uint32_t> px((size_t)image->width * (size_t)image->height);
        std::vector<XRectangle> shape;
        for (int y = 0; y < image->height; y++) {
            const uint32_t* row = image->row(y);
            for (int x = 0; x < image->width; x++) {
                uint32_t v = row[x], a = v >> 24;
                if (argb) {   // premultiplied, at 60%
                    uint32_t r = ((v >> 16) & 255) * 6 / 10, g = ((v >> 8) & 255) * 6 / 10, b = (v & 255) * 6 / 10;
                    px[(size_t)y * (size_t)image->width + (size_t)x] = (a * 6 / 10) << 24 | r << 16 | g << 8 | b;
                } else {
                    uint32_t r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
                    if (a) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
                    px[(size_t)y * (size_t)image->width + (size_t)x] = 0xff000000u | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
                    if (a >= 128 && ((x + y) & 1) == 0) shape.push_back(XRectangle{ (short)x, (short)y, 1, 1 });
                }
            }
        }
        if (!argb) XShapeCombineRectangles(dpy, ds.img, ShapeBounding, 0, 0, shape.data(), (int)shape.size(), ShapeSet, YXBanded);
        XMapRaised(dpy, ds.img);
        XImage* xi = XCreateImage(dpy, vis, (unsigned)depth, ZPixmap, 0, (char*)px.data(), (unsigned)image->width,
                                  (unsigned)image->height, 32, image->width * 4);
        if (xi) {
            GC gc = XCreateGC(dpy, ds.img, 0, nullptr);
            XPutImage(dpy, ds.img, gc, xi, 0, 0, 0, 0, (unsigned)image->width, (unsigned)image->height);
            XFreeGC(dpy, gc);
            xi->data = nullptr;
            XDestroyImage(xi);
        }
    }
    ds.active = true;
    source_motion(pointer_pos());
    XFlush(dpy);
    return true;
}

bool drag_active() { return ds.active; }
uint32_t drag_result() { return ds.result; }

static void answer_selection(const XSelectionRequestEvent& rq)
{
    if (dnd_debug()) fprintf(stderr, "xdnd: selection request %s from 0x%lx\n", atom_name(rq.target).c_str(), rq.requestor);
    XSelectionEvent ev = {};
    ev.type = SelectionNotify;
    ev.requestor = rq.requestor;
    ev.selection = rq.selection;
    ev.target = rq.target;
    ev.time = rq.time;
    ev.property = rq.property ? rq.property : rq.target;
    std::string s;
    if (rq.target == a_targets) {
        Atom t[] = { a_targets, a_uri_list, a_gnome, a_utf8, a_text_plain, XA_STRING };
        XChangeProperty(dpy, rq.requestor, ev.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)t, 6);
    } else if (rq.target == a_uri_list || rq.target == a_gnome || rq.target == a_utf8 || rq.target == a_text_plain || rq.target == XA_STRING) {
        if (rq.target == a_gnome) s = "copy";
        for (size_t i = 0; i < ds.paths.size(); i++) {
            if (rq.target == a_uri_list) s += uri_of(ds.paths[i]) + "\r\n";
            else if (rq.target == a_gnome) s += "\n" + uri_of(ds.paths[i]);
            else s += (i ? "\n" : "") + ds.paths[i];
        }
        XChangeProperty(dpy, rq.requestor, ev.property, rq.target, 8, PropModeReplace, (const unsigned char*)s.data(), (int)s.size());
    } else ev.property = None;
    XSync(dpy, False);
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);
    XSendEvent(dpy, rq.requestor, False, 0, (XEvent*)&ev);
    XSync(dpy, False);
    XSetErrorHandler(old);
}

// the source's share of translate(): input while dragging, XdndStatus/Finished,
// requests for the data. True if the event was consumed.
static bool source_event(XEvent& x)
{
    if (x.type == SelectionRequest && x.xselectionrequest.selection == a_xsel) { answer_selection(x.xselectionrequest); return true; }
    if (x.type == ClientMessage && (x.xclient.message_type == a_status || x.xclient.message_type == a_finished)) {
        if (dnd_debug()) fprintf(stderr, "xdnd: <- %s from %lx %lx %s\n", atom_name(x.xclient.message_type).c_str(), x.xclient.data.l[0], x.xclient.data.l[1],
                                 atom_name((Atom)x.xclient.data.l[x.xclient.message_type == a_status ? 4 : 2]).c_str());
        if (!ds.active || (::Window)x.xclient.data.l[0] != ds.target) return true;
        if (x.xclient.message_type == a_status) {
            ds.status_pending = false;
            ds.accepted = x.xclient.data.l[1] & 1;
            ds.accepted_effect = ds.accepted ? (ds.version >= 2 ? effect_of((Atom)x.xclient.data.l[4]) : DropCopy) : 0;
            if (ds.accepted && !ds.accepted_effect) ds.accepted_effect = DropCopy;
            if (!ds.dropped) set_drag_cursor();
            if (ds.position_pending) send_position();
            else if (ds.release_pending) source_release();
        } else if (ds.dropped) {
            bool ok = ds.version < 5 || (x.xclient.data.l[1] & 1);
            uint32_t e = ds.version >= 5 ? effect_of((Atom)x.xclient.data.l[2]) : ds.accepted_effect;
            drag_end(ok ? (e ? e : ds.accepted_effect) : 0);
        }
        return true;
    }
    if (!ds.active || ds.dropped || ds.release_pending) {
        // after the release the input is the application's again
        return false;
    }
    switch (x.type) {
    case MotionNotify: {
        XEvent next;   // only the latest of a burst
        while (XCheckTypedEvent(dpy, MotionNotify, &next)) x = next;
        last_time = x.xmotion.time;
        source_motion(gdi::Point{x.xmotion.x_root, x.xmotion.y_root});
        return true;
    }
    case ButtonPress: return true;
    case ButtonRelease:
        if (x.xbutton.button > 3) return true;
        last_time = x.xbutton.time;
        ungrab_drag();
        if (ds.status_pending || ds.position_pending) { ds.release_pending = true; ds.deadline = ticks() + 2000; }
        else source_release();
        return true;
    case KeyPress: case KeyRelease: {
        KeySym ks = XLookupKeysym(&x.xkey, 0);
        last_time = x.xkey.time;
        if (ks == XK_Escape && x.type == KeyPress) {
            ungrab_drag();
            if (ds.target) client_message(ds.target, a_leave, (long)ds.xw, 0, 0, 0, 0);
            drag_end(0);
        } else if (ds.target) {   // Shift/Ctrl change the effect: tell the target
            if (ds.status_pending) ds.position_pending = true; else send_position();
        }
        return true;
    }
    case EnterNotify: case LeaveNotify: case FocusIn: case FocusOut:
        return true;
    }
    return false;
}

static void source_timeouts()
{
    if (!ds.active) return;
    hover_raise();
    if (ds.restack_at && (int)(ticks() - ds.restack_at) >= 0) {
        ds.restack_at = 0;
        snapshot_tops();
        if (ds.img) XRaiseWindow(dpy, ds.img);
        if (!ds.dropped && !ds.release_pending) source_motion(ds.at);
    }
    if ((ds.dropped || ds.release_pending || ds.status_pending) && (int)(ticks() - ds.deadline) >= 0) {
        if (ds.release_pending || ds.dropped) {
            ungrab_drag();
            if (ds.target && !ds.dropped) client_message(ds.target, a_leave, (long)ds.xw, 0, 0, 0, 0);
            drag_end(0);
        } else {   // an unanswered position: ask again
            ds.status_pending = false;
            send_position();
        }
    }
}

// ---- target ----

struct DropTarget {
    NativeWindow* w = nullptr;
    ::Window source = 0;
    int version = 0;
    bool files = false, gnome_only = false, fetched = false;
    int attempts = 0;                       // fetches of the data (retried at later positions)
    uint32_t allowed = 0;
    Time time = CurrentTime;
    gdi::Point at{0, 0};
    uint32_t action = 0;
    std::vector<std::string> paths;
};
static DropTarget dt;

static void read_atoms(::Window w, Atom prop, std::vector<Atom>& out)
{
    Atom type; int format; unsigned long n, after; unsigned char* data = nullptr;
    XSync(dpy, False);
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);
    if (XGetWindowProperty(dpy, w, prop, 0, 64, False, XA_ATOM, &type, &format, &n, &after, &data) == Success && data) {
        if (format == 32) for (unsigned long i = 0; i < n; i++) out.push_back((Atom)((long*)data)[i]);
        XFree(data);
    }
    XSync(dpy, False);
    XSetErrorHandler(old);
}

// -1: not XDND; 0: consumed; 1: ev filled
static int target_event(NativeWindow* w, XClientMessageEvent& m, Event& ev)
{
    Atom t = m.message_type;
    if (t != a_enter && t != a_position && t != a_leave && t != a_drop) return -1;
    if (dnd_debug()) fprintf(stderr, "xdnd: <- 0x%lx %s from %lx %lx %lx\n", w->xw, atom_name(t).c_str(), m.data.l[0], m.data.l[1], m.data.l[2]);
    ::Window from = (::Window)m.data.l[0];
    if (t == a_enter) {
        dt = DropTarget();
        dt.w = w; dt.source = from;
        dt.version = (int)((unsigned long)m.data.l[1] >> 24);
        std::vector<Atom> types;
        if (m.data.l[1] & 1) read_atoms(from, a_typelist, types);
        else for (int i = 2; i < 5; i++) if (m.data.l[i]) types.push_back((Atom)m.data.l[i]);
        bool uri = false, gnome = false;
        for (Atom a : types) {
            if (a == a_uri_list) uri = true;
            if (a == a_gnome) gnome = true;
            if (dnd_debug()) fprintf(stderr, "xdnd:    type %s\n", atom_name(a).c_str());
        }
        dt.files = uri || gnome;
        dt.gnome_only = gnome && !uri;
        std::vector<Atom> acts;
        read_atoms(from, a_actionlist, acts);
        for (Atom a : acts) dt.allowed |= effect_of(a);
        if (!dt.allowed) dt.allowed = DropCopy | DropMove | DropLink;   // no list: the target decides
        // GTK sources list only the action their own keys chose (Shift: move); copying
        // or linking files never touches the source's, so our keys may choose those
        dt.allowed |= DropCopy | DropLink;
        return 0;
    }
    if (from != dt.source || !dt.source) return 0;
    if (t == a_leave) {
        ev.kind = EventKind::DragLeave;
        ev.window = dt.w ? dt.w : w;
        dt = DropTarget();
        return 1;
    }
    if (t == a_position) {
        dt.w = w;
        dt.at = gdi::Point{ (int)((unsigned long)m.data.l[2] >> 16), (int)(m.data.l[2] & 0xffff) };
        if (dt.version >= 1) dt.time = (Time)m.data.l[3];
        dt.action = dt.version >= 2 ? effect_of((Atom)m.data.l[4]) : DropCopy;
        if (!dt.files) { drop_status(0); return 0; }
    } else {
        if (dt.version >= 1) dt.time = (Time)m.data.l[2];
    }
    ev.kind = t == a_position ? EventKind::DragOver : EventKind::DragDrop;
    ev.window = w;
    ev.root = dt.at;
    ev.pos = gdi::Point{ dt.at.x - w->rect.left, dt.at.y - w->rect.top };
    ev.mods = query_mods();
    ev.drag_allowed = dt.allowed;
    ev.drag_action = dt.action;
    if (dnd_debug()) fprintf(stderr, "xdnd:    at %d,%d mods %x allowed %x asked %x\n", dt.at.x, dt.at.y, ev.mods, dt.allowed, dt.action);
    ev.time = (uint32_t)dt.time;
    return 1;
}

NativeWindow* drop_source_window() { return dt.source ? lookup(dt.source) : nullptr; }

bool drop_paths(std::vector<std::string>& paths)
{
    paths.clear();
    if (!dt.source || !dt.files || !dt.w) return false;
    if (!dt.fetched && dt.attempts < 4) {
        dt.attempts++;
        if (ds.xw && dt.source == ds.xw) dt.paths = ds.paths;   // our own drag: no round trip through the selection
        else {
            ::Window me = dt.w->xw;
            Atom target = dt.gnome_only ? a_gnome : a_uri_list;
            XDeleteProperty(dpy, me, a_dnd_prop);
            XConvertSelection(dpy, a_xsel, target, a_dnd_prop, me, dt.time);
            XFlush(dpy);
            XEvent e;
            bool got = false;
            for (uint32_t start = ticks(); ticks() - start < 1500;) {
                if (XCheckTypedWindowEvent(dpy, me, SelectionNotify, &e)) { got = true; break; }
                fd_set fds; FD_ZERO(&fds); FD_SET(ConnectionNumber(dpy), &fds);
                struct timeval tv = { 0, 20000 };
                select(ConnectionNumber(dpy) + 1, &fds, nullptr, nullptr, &tv);
                XEventsQueued(dpy, QueuedAfterReading);
            }
            if (dnd_debug()) fprintf(stderr, "xdnd:    fetch %s: %s\n", atom_name(target).c_str(), !got ? "timeout" : e.xselection.property == None ? "refused" : "ok");
            if (got && e.xselection.property != None) {
                Atom type; int format; unsigned long n, after; unsigned char* data = nullptr;
                if (XGetWindowProperty(dpy, me, a_dnd_prop, 0, 1 << 22, True, AnyPropertyType, &type, &format, &n, &after, &data) == Success && data) {
                    if (format == 8) parse_uri_list(std::string((const char*)data, n), target == a_gnome, dt.paths);
                    XFree(data);
                }
            }
        }
    }
    dt.fetched = !dt.paths.empty();
    paths = dt.paths;
    if (dnd_debug()) fprintf(stderr, "xdnd:    %zu paths%s%s\n", paths.size(), paths.empty() ? "" : ", first ", paths.empty() ? "" : paths[0].c_str());
    return !paths.empty();
}

void drop_status(uint32_t effect)
{
    if (!dt.source || !dt.w) return;
    effect &= dt.allowed;
    client_message(dt.source, a_status, (long)dt.w->xw, (effect ? 1 : 0) | 2, 0, 0, (long)action_atom(effect));
}

void drop_finish(uint32_t effect)
{
    if (!dt.source || !dt.w) return;
    effect &= dt.allowed;
    if (dt.version >= 2)
        client_message(dt.source, a_finished, (long)dt.w->xw, effect ? 1 : 0, (long)action_atom(effect), 0, 0);
    dt = DropTarget();
}

static bool translate(XEvent& x, Event& ev)
{
    memset(&ev, 0, sizeof ev);
    ev.kind = EventKind::Nothing;
    switch (x.type) {   // a server time for the selections and XDND messages we send
    case MotionNotify: last_time = x.xmotion.time; break;
    case ButtonPress: case ButtonRelease: last_time = x.xbutton.time; break;
    case KeyPress: case KeyRelease: last_time = x.xkey.time; break;
    case PropertyNotify: last_time = x.xproperty.time; break;
    }
    if (source_event(x)) return false;
    if (x.type == PropertyNotify && x.xproperty.window == root) {
        Atom a = x.xproperty.atom;
        for (size_t i = 0; i < watched.size(); i++)
            if (watched[i] == a) {
                ev.kind = EventKind::RootPropertyChanged;
                ev.keysym = (uint32_t)(i + 1);
                ev.time = (uint32_t)x.xproperty.time;
                return true;
            }
        if (a != a_composited && a != a_theme && a != a_metrics) return false;
        bool was = frame.composited;
        read_frame_info();
        if (a == a_composited) {
            if (frame.composited == was) return false;   // rewritten with the same value
            ev.kind = EventKind::CompositionChanged;
        } else {
            ev.kind = EventKind::FrameThemeChanged;
        }
        ev.time = (uint32_t)x.xproperty.time;
        return true;
    }
    // a root client message of a watched type (_LH_SETTINGS_CHANGED, x11/SETTINGS.md)
    // is reported like a change of a root property of that name
    if (x.type == ClientMessage && x.xclient.window == root) {
        for (size_t i = 0; i < watched.size(); i++)
            if (watched[i] == x.xclient.message_type) {
                ev.kind = EventKind::RootPropertyChanged;
                ev.keysym = (uint32_t)(i + 1);
                return true;
            }
        return false;
    }
    NativeWindow* w = lookup(x.xany.window);
    if (!w) return false;
    ev.window = w;
    if (x.type == PropertyNotify) {
        if (x.xproperty.atom == a_header_pixmap) ev.keysym = WindowHeaderPixmap;
        else if (x.xproperty.atom == a_caption_rect) ev.keysym = WindowCaptionRect;
        else if (x.xproperty.atom == a_state) ev.keysym = WindowState;
        else return false;
        ev.kind = EventKind::WindowPropertyChanged;
        ev.time = (uint32_t)x.xproperty.time;
        return true;
    }
    switch (x.type) {
    case Expose:
        ev.kind = EventKind::Exposed;
        ev.rect = gdi::rect_xywh(x.xexpose.x, x.xexpose.y, x.xexpose.width, x.xexpose.height);
        return true;
    case ConfigureNotify: {
        int rx = 0, ry = 0; ::Window child;
        XTranslateCoordinates(dpy, w->xw, RootWindow(dpy, screen), 0, 0, &rx, &ry, &child);
        w->rect = gdi::rect_xywh(rx, ry, x.xconfigure.width, x.xconfigure.height);
        ev.kind = EventKind::Configured;
        ev.rect = w->rect;
        return true;
    }
    case ClientMessage: {
        int r = target_event(w, x.xclient, ev);
        if (r >= 0) return r == 1;
    }
        if (x.xclient.message_type == wm_protocols && (Atom)x.xclient.data.l[0] == wm_delete) {
            ev.kind = EventKind::Closed;
            return true;
        }
        return false;
    case MotionNotify:
        ev.kind = EventKind::MouseMove;
        ev.pos = gdi::Point{x.xmotion.x, x.xmotion.y};
        ev.root = gdi::Point{x.xmotion.x_root, x.xmotion.y_root};
        ev.mods = mods_from_state(x.xmotion.state);
        ev.time = (uint32_t)x.xmotion.time;
        return true;
    case ButtonPress:
    case ButtonRelease:
        // unmanaged desktop windows: click to focus is ours (Windows' shell desktop is activated on click too)
        if (x.type == ButtonPress && w->type == WindowType::Desktop) XSetInputFocus(dpy, w->xw, RevertToPointerRoot, x.xbutton.time);
        ev.pos = gdi::Point{x.xbutton.x, x.xbutton.y};
        ev.root = gdi::Point{x.xbutton.x_root, x.xbutton.y_root};
        ev.mods = mods_from_state(x.xbutton.state);
        ev.time = (uint32_t)x.xbutton.time;
        if (x.xbutton.button == 4 || x.xbutton.button == 5) {
            if (x.type == ButtonRelease) return false;
            ev.kind = EventKind::Wheel;
            ev.wheel = x.xbutton.button == 4 ? 1 : -1;
            return true;
        }
        if (x.xbutton.button > 3) return false;
        ev.kind = x.type == ButtonPress ? EventKind::ButtonDown : EventKind::ButtonUp;
        ev.button = (int)x.xbutton.button;
        return true;
    case KeyPress:
    case KeyRelease: {
        KeySym ks = 0;
        char buf[8] = {0};
        int n = XLookupString(&x.xkey, buf, sizeof buf - 1, &ks, nullptr);
        ev.kind = x.type == KeyPress ? EventKind::KeyDown : EventKind::KeyUp;
        ev.keysym = (uint32_t)ks;
        ev.mods = mods_from_state(x.xkey.state);
        ev.time = (uint32_t)x.xkey.time;
        if (n > 0 && x.type == KeyPress) memcpy(ev.text, buf, (size_t)n);
        return true;
    }
    case FocusIn:  ev.kind = EventKind::FocusGained;  return true;
    case FocusOut:
        // mode NotifyGrab: a keyboard grab (the WM's Alt+Tab, a key binding):
        // still a FocusLost as before; the win layer also cancels modes then
        ev.kind = EventKind::FocusLost; return true;
    case LeaveNotify:
        if (x.xcrossing.mode == NotifyGrab && own_grab) return false;   // our own (grab_pointer)
        if (x.xcrossing.mode == NotifyGrab) {
            // the pointer was grabbed by another client while over us (the
            // window manager starting a move/resize, a launcher, ...)
            ev.kind = EventKind::GrabbedElsewhere;
            return true;
        }
        if (x.xcrossing.mode != NotifyNormal) return false;
        ev.kind = EventKind::PointerLeft;
        ev.pos = gdi::Point{x.xcrossing.x, x.xcrossing.y};
        return true;
    default:
        return false;
    }
}

// Other threads wake the event loop through a pipe (wake()); its read end is
// watched next to the X connection, and a wake-up ends a wait like a timeout.
static int wake_fds[2] = { -1, -1 };

struct FdWatch { int fd; void (*cb)(int, void*); void* user; };
static std::vector<FdWatch> fd_watches;

void watch_fd(int fd, void (*cb)(int, void*), void* user)
{
    if (fd < 0 || !cb) return;
    for (FdWatch& w : fd_watches) if (w.fd == fd) { w.cb = cb; w.user = user; return; }
    fd_watches.push_back(FdWatch{ fd, cb, user });
}

void unwatch_fd(int fd)
{
    for (size_t i = 0; i < fd_watches.size(); i++)
        if (fd_watches[i].fd == fd) { fd_watches.erase(fd_watches.begin() + (long)i); return; }
}

void dispatch_fd_watches()
{
    if (fd_watches.empty()) return;
    std::vector<pollfd> p;
    for (const FdWatch& w : fd_watches) p.push_back(pollfd{ w.fd, POLLIN, 0 });
    if (poll(p.data(), (nfds_t)p.size(), 0) <= 0) return;
    for (const pollfd& r : p) {
        if (!(r.revents & (POLLIN | POLLHUP | POLLERR))) continue;
        for (const FdWatch& w : fd_watches)   // (a callback may have removed it)
            if (w.fd == r.fd) { FdWatch c = w; c.cb(c.fd, c.user); break; }
    }
}

static void wake_init()
{
    if (wake_fds[0] >= 0) return;
    if (pipe(wake_fds) != 0) { wake_fds[0] = wake_fds[1] = -1; return; }
    for (int fd : wake_fds) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); fcntl(fd, F_SETFD, FD_CLOEXEC); }
}

void wake()
{
    if (wake_fds[1] < 0) return;
    char c = 1;
    ssize_t r = write(wake_fds[1], &c, 1);   // a full pipe means a wake-up is pending already
    (void)r;
}

bool wait_event(Event& ev, int timeout_ms)
{
    uint32_t start = ticks();
    for (;;) {
        source_timeouts();
        if (drag_changed) {   // the drag ended: the caller's loop looks at drag_active()
            drag_changed = false;
            memset(&ev, 0, sizeof ev);
            ev.kind = EventKind::Nothing;
            return true;
        }
        while (XPending(dpy)) {
            XEvent x;
            XNextEvent(dpy, &x);
            if (translate(x, ev)) return true;
        }
        if (timeout_ms == 0) return false;
        int remaining = -1;
        if (timeout_ms > 0) {
            uint32_t el = ticks() - start;
            if (el >= (uint32_t)timeout_ms) return false;
            remaining = timeout_ms - (int)el;
        }
        int fd = ConnectionNumber(dpy);
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        int nfds = fd + 1;
        if (wake_fds[0] >= 0) { FD_SET(wake_fds[0], &fds); if (wake_fds[0] >= nfds) nfds = wake_fds[0] + 1; }
        for (const FdWatch& w : fd_watches) if (w.fd < FD_SETSIZE) { FD_SET(w.fd, &fds); if (w.fd >= nfds) nfds = w.fd + 1; }
        if (ds.active && (remaining < 0 || remaining > 100)) remaining = 100;   // drag timeouts
        struct timeval tv, *ptv = nullptr;
        if (remaining >= 0) { tv.tv_sec = remaining / 1000; tv.tv_usec = (remaining % 1000) * 1000; ptv = &tv; }
        int rc = select(nfds, &fds, nullptr, nullptr, ptv);
        if (rc == 0) return false;
        if (rc > 0) for (const FdWatch& w : fd_watches) if (w.fd < FD_SETSIZE && FD_ISSET(w.fd, &fds)) return false;   // dispatch_fd_watches
        if (rc > 0 && wake_fds[0] >= 0 && FD_ISSET(wake_fds[0], &fds)) {
            char buf[64];
            while (read(wake_fds[0], buf, sizeof buf) > 0) {}
            return false;   // the caller looks at what the other thread queued
        }
    }
}

gdi::Point pointer_pos()
{
    ::Window root, child; int rx = 0, ry = 0, wx, wy; unsigned mask;
    XQueryPointer(dpy, RootWindow(dpy, screen), &root, &child, &rx, &ry, &wx, &wy, &mask);
    return gdi::Point{rx, ry};
}

gdi::Size screen_size()
{
    // before the display is open: liblhde probes a style (ut_open -> SystemParametersInfo) while
    // flxp resolves the session theme at start-up
    if (!dpy) return gdi::Size{1024, 768};
    return gdi::Size{DisplayWidth(dpy, screen), DisplayHeight(dpy, screen)};
}

uint32_t ticks()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ---- dialog hints ---------------------------------------------------------------

NativeWindow* window_for_user(void* user)
{
    for (NativeWindow* w : windows) if (w->user == user) return w;
    return nullptr;
}

uint64_t native_id(NativeWindow* w) { return w ? (uint64_t)w->xw : 0; }

void set_transient_for(NativeWindow* w, uint64_t owner, bool modal)
{
    if (!w) return;
    w->transient = true; w->transient_owner = owner; w->transient_modal = modal;
    if (owner) XSetTransientForHint(dpy, w->xw, (::Window)owner);
    Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(dpy, w->xw, type, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&dialog, 1);
    if (modal) {   // before mapping; a mapped window would need a client message instead
        Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom m = XInternAtom(dpy, "_NET_WM_STATE_MODAL", False);
        XChangeProperty(dpy, w->xw, state, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&m, 1);
    }
    XFlush(dpy);
}

static int ignore_x_error(Display*, XErrorEvent*) { return 0; }

bool native_window_rect(uint64_t id, gdi::Rect& r)
{
    if (!id) return false;
    XSync(dpy, False);
    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_x_error);
    XWindowAttributes a;
    bool ok = XGetWindowAttributes(dpy, (::Window)id, &a) != 0;
    int x = 0, y = 0; ::Window child;
    if (ok) ok = XTranslateCoordinates(dpy, (::Window)id, RootWindow(dpy, screen), 0, 0, &x, &y, &child) != 0;
    XSync(dpy, False);
    XSetErrorHandler(old);
    if (!ok) return false;
    r = gdi::Rect{x, y, x + a.width, y + a.height};
    return true;
}

void set_icon(NativeWindow* w, const std::vector<unsigned long>& data)
{
    if (!w) return;
    w->icon = data;
    Atom a = XInternAtom(dpy, "_NET_WM_ICON", False);
    if (data.empty()) XDeleteProperty(dpy, w->xw, a);
    else XChangeProperty(dpy, w->xw, a, XA_CARDINAL, 32, PropModeReplace, (const unsigned char*)data.data(), (int)data.size());
    XFlush(dpy);
}

void set_min_size(NativeWindow* w, int cx, int cy)
{
    if (!w) return;
    w->min_w = cx; w->min_h = cy;
    XSizeHints hints = {};
    long supplied = 0;
    XGetWMNormalHints(dpy, w->xw, &hints, &supplied);
    // the creation-time PSize/PPosition would make the WM map the window at its first size
    hints.flags |= PMinSize | PSize;
    hints.min_width = cx; hints.min_height = cy;
    XWindowAttributes a;
    if (XGetWindowAttributes(dpy, w->xw, &a)) { hints.width = a.width; hints.height = a.height; }
    XSetWMNormalHints(dpy, w->xw, &hints);
    XFlush(dpy);
}

// ---- shells: window types, root properties ----------------------------------------

void set_window_type(NativeWindow* w, WindowType t)
{
    w->type = t;
    if (t == WindowType::Normal) return;
    bool dock = t == WindowType::Dock;
    Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom desk = XInternAtom(dpy, dock ? "_NET_WM_WINDOW_TYPE_DOCK" : "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(dpy, w->xw, type, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&desk, 1);
    Atom st[4] = { XInternAtom(dpy, dock ? "_NET_WM_STATE_ABOVE" : "_NET_WM_STATE_BELOW", False), XInternAtom(dpy, "_NET_WM_STATE_STICKY", False),
                   XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False), XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False) };
    XChangeProperty(dpy, w->xw, a_state, XA_ATOM, 32, PropModeReplace, (const unsigned char*)st, 4);
    long all = 0xFFFFFFFF;   // on every virtual desktop
    XChangeProperty(dpy, w->xw, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (const unsigned char*)&all, 1);
    if (dock) {   // the window manager never focuses a panel (the taskbar sets the focus itself for its menus)
        XWMHints hints = {};
        hints.flags = InputHint;
        hints.input = False;
        XSetWMHints(dpy, w->xw, &hints);
    }
    long mwm[5] = { 2 /* MWM_HINTS_DECORATIONS */, 0, 0, 0, 0 };
    Atom motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy, w->xw, motif, motif, 32, PropModeReplace, (const unsigned char*)mwm, 5);
    XFlush(dpy);
}

int root_cardinals(const char* prop, long* out, int max)
{
    Atom a = XInternAtom(dpy, prop, False);
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    int n = -1;
    if (XGetWindowProperty(dpy, root, a, 0, max, False, XA_CARDINAL, &type, &format, &count, &after, &data) == Success && data) {
        if (format == 32) {
            n = (int)count;
            for (int i = 0; i < n && i < max; i++) out[i] = ((long*)data)[i];
        }
        XFree(data);
    }
    return n;
}

std::string root_string(const char* prop)
{
    Atom a = XInternAtom(dpy, prop, False);
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    std::string out;
    if (XGetWindowProperty(dpy, root, a, 0, 4096, False, AnyPropertyType, &type, &format, &count, &after, &data) == Success && data) {
        if (format == 8) out.assign((const char*)data, count);
        XFree(data);
    }
    return out;
}

bool root_window(const char* prop, uint64_t& id)
{
    Atom a = XInternAtom(dpy, prop, False);
    Atom type; int format; unsigned long count, after; unsigned char* data = nullptr;
    bool ok = false;
    if (XGetWindowProperty(dpy, root, a, 0, 1, False, XA_WINDOW, &type, &format, &count, &after, &data) == Success && data) {
        if (format == 32 && count == 1) { id = (uint64_t)((unsigned long*)data)[0]; ok = true; }
        XFree(data);
    }
    return ok;
}

void set_root_string(const char* prop, const std::string& value)
{
    Atom a = XInternAtom(dpy, prop, False);
    if (value.empty()) XDeleteProperty(dpy, root, a);
    else XChangeProperty(dpy, root, a, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
                         (const unsigned char*)value.data(), (int)value.size());
    XFlush(dpy);
}

uint32_t watch_root_property(const char* prop)
{
    Atom a = XInternAtom(dpy, prop, False);
    for (size_t i = 0; i < watched.size(); i++) if (watched[i] == a) return (uint32_t)(i + 1);
    watched.push_back(a);
    if (!strcmp(prop, "_LH_SETTINGS_CHANGED")) {
        // sent to the root with SubstructureNotifyMask: only clients selecting it get it
        XWindowAttributes ra;
        XGetWindowAttributes(dpy, root, &ra);
        XSelectInput(dpy, root, ra.your_event_mask | PropertyChangeMask | SubstructureNotifyMask);
    }
    return (uint32_t)watched.size();
}

bool work_area(gdi::Rect& r)
{
    long cur = 0;
    if (root_cardinals("_NET_CURRENT_DESKTOP", &cur, 1) < 1) cur = 0;
    long wa[4 * 32];
    int n = root_cardinals("_NET_WORKAREA", wa, 4 * 32);
    if (n < 4) return false;
    if (cur < 0 || cur * 4 + 4 > n) cur = 0;
    r = gdi::rect_xywh((int32_t)wa[cur * 4], (int32_t)wa[cur * 4 + 1], (int32_t)wa[cur * 4 + 2], (int32_t)wa[cur * 4 + 3]);
    return r.width() > 0 && r.height() > 0;
}

} // namespace sys
