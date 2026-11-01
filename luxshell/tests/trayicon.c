/* trayicon: minimal XEmbed system-tray icon for testing luxshell's notification-area tile.
   Uses the tray's _NET_SYSTEM_TRAY_VISUAL (32-bit ARGB) when offered, paints an anti-aliased circle with a transparent
   surround, docks with SYSTEM_TRAY_REQUEST_DOCK, re-docks when a new tray announces MANAGER, cycles colour on click.
   usage: trayicon [#rrggbb] [seconds]   (seconds > 0: exit after that long, which tests undocking on destroy) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cairo/cairo-xlib.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static Display *d; static Window win, rootw; static Visual *vis; static int depth, size = 16; static Atom sel, opcode, info, vatom, manager;
static double col[3] = { 0.2, 0.6, 1.0 };
static void paint(void) {
    cairo_surface_t *s = cairo_xlib_surface_create(d, win, vis, size, size); cairo_t *cr = cairo_create(s);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, depth == 32 ? 0 : 1); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_pattern_t *p = cairo_pattern_create_radial(size * 0.4, size * 0.35, 1, size / 2.0, size / 2.0, size / 2.0);
    cairo_pattern_add_color_stop_rgb(p, 0, fmin(1, col[0] + 0.5), fmin(1, col[1] + 0.5), fmin(1, col[2] + 0.5)); cairo_pattern_add_color_stop_rgb(p, 1, col[0] * 0.6, col[1] * 0.6, col[2] * 0.6);
    cairo_arc(cr, size / 2.0, size / 2.0, size / 2.0 - 1, 0, 2 * M_PI); cairo_set_source(cr, p); cairo_fill(cr);
    cairo_pattern_destroy(p); cairo_destroy(cr); cairo_surface_destroy(s); XFlush(d);
}
static void dock(void) {
    Window owner = XGetSelectionOwner(d, sel); if (owner == None) { fprintf(stderr, "trayicon: no tray yet, waiting for MANAGER\n"); return; }
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = owner; e.xclient.message_type = opcode; e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime; e.xclient.data.l[1] = 0; e.xclient.data.l[2] = win;
    XSendEvent(d, owner, False, NoEventMask, &e); XFlush(d); fprintf(stderr, "trayicon: dock request to 0x%lx\n", owner);
}
int main(int argc, char **argv) {
    if (argc > 1 && argv[1][0] == '#') { unsigned v; sscanf(argv[1] + 1, "%x", &v); col[0] = (v >> 16 & 255) / 255.0; col[1] = (v >> 8 & 255) / 255.0; col[2] = (v & 255) / 255.0; }
    int secs = argc > 2 ? atoi(argv[2]) : 0;
    d = XOpenDisplay(NULL); if (!d) return 1; int scr = DefaultScreen(d); rootw = RootWindow(d, scr);
    char nm[64]; snprintf(nm, sizeof nm, "_NET_SYSTEM_TRAY_S%d", scr);
    sel = XInternAtom(d, nm, False); opcode = XInternAtom(d, "_NET_SYSTEM_TRAY_OPCODE", False); info = XInternAtom(d, "_XEMBED_INFO", False);
    vatom = XInternAtom(d, "_NET_SYSTEM_TRAY_VISUAL", False); manager = XInternAtom(d, "MANAGER", False);
    vis = DefaultVisual(d, scr); depth = DefaultDepth(d, scr);
    Window owner = XGetSelectionOwner(d, sel);
    if (owner) { Atom t; int f; unsigned long n, a; unsigned char *p = NULL;
        if (XGetWindowProperty(d, owner, vatom, 0, 1, False, XA_VISUALID, &t, &f, &n, &a, &p) == Success && p && n) {
            VisualID id = *(unsigned long *)p; XVisualInfo tmpl = { .visualid = id }; int cnt; XVisualInfo *vi = XGetVisualInfo(d, VisualIDMask, &tmpl, &cnt);
            if (vi && cnt) { vis = vi->visual; depth = vi->depth; XFree(vi); } }
        if (p) XFree(p); }
    XSetWindowAttributes wa = { .colormap = XCreateColormap(d, rootw, vis, AllocNone), .border_pixel = 0, .background_pixel = 0,
        .event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask };
    win = XCreateWindow(d, rootw, 0, 0, size, size, 0, depth, InputOutput, vis, CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &wa);
    XStoreName(d, win, "trayicon"); long xi[2] = { 0, 1 /* XEMBED_MAPPED */ };
    XChangeProperty(d, win, info, info, 32, PropModeReplace, (unsigned char *)xi, 2);
    XSelectInput(d, rootw, StructureNotifyMask);
    fprintf(stderr, "trayicon: window 0x%lx depth %d\n", win, depth);
    dock();
    time_t start = time(NULL);
    for (;;) {
        while (XPending(d)) { XEvent e; XNextEvent(d, &e);
            if (e.type == Expose && e.xexpose.count == 0) paint();
            else if (e.type == ConfigureNotify && e.xconfigure.window == win) { if (e.xconfigure.width != size) { size = e.xconfigure.width; } paint(); }
            else if (e.type == ButtonPress) { fprintf(stderr, "trayicon: click %d at %d,%d\n", e.xbutton.button, e.xbutton.x, e.xbutton.y); double t = col[0]; col[0] = col[1]; col[1] = col[2]; col[2] = t; paint(); }
            else if (e.type == ClientMessage && e.xclient.message_type == manager && (Atom)e.xclient.data.l[1] == sel) dock();
            else if (e.type == ReparentNotify) fprintf(stderr, "trayicon: reparented to 0x%lx\n", e.xreparent.parent);
        }
        if (secs > 0 && time(NULL) - start >= secs) { fprintf(stderr, "trayicon: exiting\n"); return 0; }
        struct pollfd p = { ConnectionNumber(d), POLLIN, 0 }; poll(&p, 1, 500);
    }
}
