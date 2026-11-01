// luxshot's capture (capture.h): Xlib only, compiled without the win32 headers (apps/app.mk: *_native.cpp).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include "capture.h"

namespace capture {

namespace {

void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0) {}
}

// a channel of a pixel by its visual mask, scaled to 8 bits
inline uint32_t channel(unsigned long px, unsigned long mask)
{
    if (!mask) return 0;
    int shift = __builtin_ctzl(mask);
    unsigned long v = (px & mask) >> shift, max = mask >> shift;
    return max == 255 ? (uint32_t)v : (uint32_t)(v * 255 / max);
}

bool grab(Display* d, Window root, int x, int y, int w, int h, Shot& out)
{
    XImage* im = XGetImage(d, root, x, y, (unsigned)w, (unsigned)h, AllPlanes, ZPixmap);
    if (!im) return false;
    out.width = w; out.height = h; out.x = x; out.y = y;
    out.pixels.resize((size_t)w * h);
    bool fast = im->bits_per_pixel == 32 && im->byte_order == LSBFirst && im->red_mask == 0xff0000 &&
                im->green_mask == 0xff00 && im->blue_mask == 0xff;
    for (int r = 0; r < h; r++) {
        uint32_t* dst = &out.pixels[(size_t)r * w];
        if (fast) {
            const uint32_t* src = (const uint32_t*)(im->data + (size_t)r * im->bytes_per_line);
            for (int c = 0; c < w; c++) dst[c] = src[c] | 0xff000000u;
        } else {
            for (int c = 0; c < w; c++) {
                unsigned long p = XGetPixel(im, c, r);
                dst[c] = 0xff000000u | channel(p, im->red_mask) << 16 | channel(p, im->green_mask) << 8 | channel(p, im->blue_mask);
            }
        }
    }
    XDestroyImage(im);
    return true;
}

std::string window_title(Display* d, Window w)
{
    Atom utf8 = XInternAtom(d, "UTF8_STRING", False), net_name = XInternAtom(d, "_NET_WM_NAME", False), type;
    int format;
    unsigned long n, after;
    unsigned char* data = nullptr;
    std::string t;
    if (XGetWindowProperty(d, w, net_name, 0, 1024, False, utf8, &type, &format, &n, &after, &data) == Success && data && n)
        t.assign((const char*)data, n);
    if (data) XFree(data);
    if (t.empty()) {
        char* name = nullptr;
        if (XFetchName(d, w, &name) && name) { t = name; XFree(name); }
    }
    return t;
}

bool cardinals(Display* d, Window w, const char* prop, long* v, int count)
{
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char* data = nullptr;
    bool ok = XGetWindowProperty(d, w, XInternAtom(d, prop, False), 0, count, False, AnyPropertyType, &type, &format, &n,
                                 &after, &data) == Success && data && format == 32 && (int)n >= count;
    if (ok) for (int i = 0; i < count; i++) v[i] = ((long*)data)[i];
    if (data) XFree(data);
    return ok;
}

// the active window (EWMH), with its frame: luxwm does not reparent, its frame is a window of its own under the client,
// and _NET_FRAME_EXTENTS says how far it reaches around the client
bool active_window_rect(Display* d, Window root, bool frame, int& x, int& y, int& w, int& h, std::string& title, std::string* err)
{
    long active = 0;
    if (!cardinals(d, root, "_NET_ACTIVE_WINDOW", &active, 1) || !active) {
        if (err) *err = "There is no active window.";
        return false;
    }
    Window win = (Window)active, child;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(d, win, &wa) || wa.map_state != IsViewable) {
        if (err) *err = "The active window is not shown.";
        return false;
    }
    int rx = 0, ry = 0;
    XTranslateCoordinates(d, win, root, 0, 0, &rx, &ry, &child);
    x = rx; y = ry; w = wa.width; h = wa.height;
    long ext[4] = { 0, 0, 0, 0 };   // left, right, top, bottom
    if (frame && cardinals(d, win, "_NET_FRAME_EXTENTS", ext, 4)) {
        x -= (int)ext[0]; y -= (int)ext[2]; w += (int)(ext[0] + ext[1]); h += (int)(ext[2] + ext[3]);
    }
    title = window_title(d, win);
    return true;
}

// the rubber band: an XOR rectangle on the frozen picture
void band(Display* d, Window w, GC gc, int x0, int y0, int x1, int y1)
{
    int x = std::min(x0, x1), y = std::min(y0, y1);
    unsigned bw = (unsigned)std::abs(x1 - x0), bh = (unsigned)std::abs(y1 - y0);
    if (bw && bh) XDrawRectangle(d, w, gc, x, y, bw - 1, bh - 1);
}

// a rectangle dragged over the frozen screen: an override-redirect window showing the picture, the pointer and keyboard
// grabbed (retried: luxwm still holds the key that started us), a crosshair, Escape or the right button cancel
bool drag_region(Display* d, Window root, const Shot& screen, int& rx, int& ry, int& rw, int& rh, std::string* err)
{
    int scr = DefaultScreen(d);
    Visual* vis = DefaultVisual(d, scr);
    int depth = DefaultDepth(d, scr);
    Pixmap pm = XCreatePixmap(d, root, (unsigned)screen.width, (unsigned)screen.height, (unsigned)depth);
    XImage* im = XCreateImage(d, vis, (unsigned)depth, ZPixmap, 0, nullptr, (unsigned)screen.width, (unsigned)screen.height, 32, 0);
    bool ok_image = im && im->bits_per_pixel == 32;
    if (ok_image) {
        im->data = (char*)malloc((size_t)im->bytes_per_line * screen.height);
        for (int r = 0; r < screen.height; r++)
            for (int c = 0; c < screen.width; c++) {
                uint32_t p = screen.pixels[(size_t)r * screen.width + c];
                XPutPixel(im, c, r, (unsigned long)(p & 0xffffff));
            }
        GC pgc = XCreateGC(d, pm, 0, nullptr);
        XPutImage(d, pm, pgc, im, 0, 0, 0, 0, (unsigned)screen.width, (unsigned)screen.height);
        XFreeGC(d, pgc);
    }
    if (im) XDestroyImage(im);
    XSetWindowAttributes a = {};
    a.override_redirect = True;
    a.background_pixmap = ok_image ? pm : None;
    a.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | ExposureMask;
    Window w = XCreateWindow(d, root, 0, 0, (unsigned)screen.width, (unsigned)screen.height, 0, CopyFromParent, InputOutput,
                             CopyFromParent, CWOverrideRedirect | CWBackPixmap | CWEventMask, &a);
    XStoreName(d, w, "luxshot");
    Cursor cross = XCreateFontCursor(d, XC_crosshair);
    XDefineCursor(d, w, cross);
    XMapRaised(d, w);
    XSync(d, False);
    bool grabbed = false;
    for (int i = 0; i < 100 && !grabbed; i++) {
        grabbed = XGrabPointer(d, w, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
                               w, cross, CurrentTime) == GrabSuccess;
        if (!grabbed) sleep_ms(20);
    }
    for (int i = 0; i < 100 && grabbed; i++) {
        if (XGrabKeyboard(d, w, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) break;
        sleep_ms(20);
    }
    XGCValues gv;
    gv.function = GXxor;
    gv.foreground = WhitePixel(d, scr) ^ BlackPixel(d, scr);
    gv.line_width = 1;
    gv.subwindow_mode = IncludeInferiors;
    GC gc = XCreateGC(d, w, GCFunction | GCForeground | GCLineWidth | GCSubwindowMode, &gv);
    bool done = false, cancelled = !grabbed, dragging = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!grabbed && err) *err = "Cannot grab the pointer to drag a rectangle.";
    while (!done && !cancelled) {
        XEvent ev;
        XNextEvent(d, &ev);
        switch (ev.type) {
        case Expose:
            if (dragging && ev.xexpose.count == 0) band(d, w, gc, x0, y0, x1, y1);
            break;
        case ButtonPress:
            if (ev.xbutton.button == Button1) { dragging = true; x0 = x1 = ev.xbutton.x; y0 = y1 = ev.xbutton.y; }
            else if (ev.xbutton.button == Button3) cancelled = true;
            break;
        case MotionNotify:
            if (!dragging) break;
            while (XCheckTypedWindowEvent(d, w, MotionNotify, &ev)) {}
            band(d, w, gc, x0, y0, x1, y1);   // XOR: the old band off
            x1 = ev.xmotion.x; y1 = ev.xmotion.y;
            band(d, w, gc, x0, y0, x1, y1);
            break;
        case ButtonRelease:
            if (ev.xbutton.button != Button1 || !dragging) break;
            band(d, w, gc, x0, y0, x1, y1);
            x1 = ev.xbutton.x; y1 = ev.xbutton.y;
            dragging = false;
            if (std::abs(x1 - x0) >= 2 && std::abs(y1 - y0) >= 2) done = true;   // a click is no rectangle: drag again
            break;
        case KeyPress: {
            KeySym k = XLookupKeysym(&ev.xkey, 0);
            if (k == XK_Escape) cancelled = true;
            break;
        }
        }
    }
    XUngrabKeyboard(d, CurrentTime);
    XUngrabPointer(d, CurrentTime);
    XFreeGC(d, gc);
    XDestroyWindow(d, w);
    XFreeCursor(d, cross);
    XFreePixmap(d, pm);
    XSync(d, False);
    if (cancelled) return false;
    rx = std::min(x0, x1); ry = std::min(y0, y1); rw = std::abs(x1 - x0); rh = std::abs(y1 - y0);
    return true;
}

} // namespace

bool take(Mode mode, bool frame, double delay, Shot& out, std::string* err)
{
    if (err) err->clear();
    if (delay > 0) sleep_ms((long)lround(delay * 1000));
    Display* d = XOpenDisplay(nullptr);
    if (!d) { if (err) *err = "Cannot open the display."; return false; }
    Window root = DefaultRootWindow(d);
    int sw = DisplayWidth(d, DefaultScreen(d)), sh = DisplayHeight(d, DefaultScreen(d));
    bool ok = false;
    if (mode == Mode::Screen) {
        ok = grab(d, root, 0, 0, sw, sh, out);
        out.what = "Whole screen";
    } else if (mode == Mode::Window) {
        int x, y, w, h;
        std::string title;
        if (active_window_rect(d, root, frame, x, y, w, h, title, err)) {
            int x0 = std::max(x, 0), y0 = std::max(y, 0), x1 = std::min(x + w, sw), y1 = std::min(y + h, sh);   // on screen
            if (x1 > x0 && y1 > y0) ok = grab(d, root, x0, y0, x1 - x0, y1 - y0, out);
            else if (err) *err = "The active window is off the screen.";
            out.what = title.empty() ? std::string("Window") : title;
        }
    } else {
        Shot screen;
        int x, y, w, h;
        if (grab(d, root, 0, 0, sw, sh, screen) && drag_region(d, root, screen, x, y, w, h, err)) {
            out.width = w; out.height = h; out.x = x; out.y = y;
            out.pixels.resize((size_t)w * h);
            for (int r = 0; r < h; r++)
                memcpy(&out.pixels[(size_t)r * w], &screen.pixels[(size_t)(y + r) * sw + x], (size_t)w * 4);
            out.what = "Region";
            ok = true;
        }
    }
    if (!ok && err && err->empty() && mode != Mode::Region) *err = "Cannot read the screen.";
    XCloseDisplay(d);
    return ok;
}

} // namespace capture
