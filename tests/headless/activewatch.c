/* activewatch: what a focus change paints, as fast as the server answers (tests/headless/activation.sh).
 *
 *   activewatch SECONDS PROBE...    PROBE = NAME:KIND:WINDOW:X:Y
 *
 * KIND c reads the pixel (X, Y) of the client window WINDOW itself, f the same point (client coordinates, so the caption is at a
 * negative Y) of its frame (luxwm's decoration window: the unnamed top-level enclosing the client), r the same point on the root
 * (what the compositor shows). The windows' own pixels are their backing store under a compositor: c and f show who painted what
 * in which order, r what reached the screen. Every probe is read in a tight loop for SECONDS; a line is printed on each change:
 *
 *   T probe NAME RRGGBB          T: ms since the start
 *   T prop WINDOW ATOM new|del   property changes on the root and on the probed clients (_NET_ACTIVE_WINDOW, _LHWM_ACTIVE_DRAWN,
 *                                the test's _LHTEST_MARK)
 *
 * Needs only Xlib. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static int ignore(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }   /* a window gone meanwhile */

/* the frame of client w: the unnamed, unclassed top-level that encloses it with the least area */
static Window frame_of(Display *d, Window root, Window w)
{
    XWindowAttributes ca; Window ch;
    int cx, cy;
    if (!XGetWindowAttributes(d, w, &ca)) return None;
    XTranslateCoordinates(d, w, root, 0, 0, &cx, &cy, &ch);
    Window r, p, *kids = NULL; unsigned n = 0;
    if (!XQueryTree(d, root, &r, &p, &kids, &n)) return None;
    Window best = None; long area = 0;
    for (unsigned i = 0; i < n; i++) {
        XWindowAttributes a; XClassHint h = { 0 };
        if (kids[i] == w || !XGetWindowAttributes(d, kids[i], &a) || a.map_state != IsViewable || a.override_redirect) continue;
        if (XGetClassHint(d, kids[i], &h)) { XFree(h.res_name); XFree(h.res_class); continue; }
        if (a.x > cx || a.y > cy || a.x + a.width < cx + ca.width || a.y + a.height < cy + ca.height) continue;
        long ar = (long)a.width * a.height;
        if (best == None || ar < area) { best = kids[i]; area = ar; }
    }
    if (kids) XFree(kids);
    return best;
}

struct probe { char name[32]; Window win; int x, y; unsigned long last; };

static double t0;

/* print the first n queued events (property changes) */
static void drain(Display *d, int n)
{
    while (n-- > 0) {
        XEvent e; XNextEvent(d, &e);
        if (e.type != PropertyNotify) continue;
        char *a = XGetAtomName(d, e.xproperty.atom);
        printf("%.2f prop 0x%lx %s %s\n", now_ms() - t0, e.xproperty.window, a ? a : "?", e.xproperty.state == PropertyDelete ? "del" : "new");
        if (a) XFree(a);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: activewatch SECONDS NAME:KIND:WINDOW:X:Y...\n"); return 2; }
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "activewatch: no display\n"); return 1; }
    XSetErrorHandler(ignore);
    Window root = DefaultRootWindow(d);
    XSelectInput(d, root, PropertyChangeMask);
    double secs = atof(argv[1]);
    struct probe pr[64]; int n = 0;
    for (int i = 2; i < argc && n < 64; i++) {
        char name[32], kind; unsigned long w; int x, y;
        if (sscanf(argv[i], "%31[^:]:%c:%li:%d:%d", name, &kind, (long *)&w, &x, &y) != 5) { fprintf(stderr, "activewatch: bad probe %s\n", argv[i]); return 2; }
        struct probe *p = &pr[n++];
        snprintf(p->name, sizeof p->name, "%s", name); p->last = ~0UL;
        XSelectInput(d, (Window)w, PropertyChangeMask);
        if (kind == 'c') { p->win = (Window)w; p->x = x; p->y = y; continue; }
        Window ch; int ox, oy;
        Window base = kind == 'f' ? frame_of(d, root, (Window)w) : root;
        if (base == None) { fprintf(stderr, "activewatch: no frame around 0x%lx\n", w); return 1; }
        XTranslateCoordinates(d, (Window)w, base, x, y, &ox, &oy, &ch);
        p->win = base; p->x = ox; p->y = oy;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    t0 = now_ms();
    while (now_ms() - t0 < secs * 1000) {
        drain(d, XPending(d));
        for (int i = 0; i < n; i++) {
            XImage *im = XGetImage(d, pr[i].win, pr[i].x, pr[i].y, 1, 1, AllPlanes, ZPixmap);
            /* events that came in before the reply happened before the read: printed first, so the lines keep the server's order */
            drain(d, XEventsQueued(d, QueuedAlready));
            if (!im) continue;
            unsigned long v = XGetPixel(im, 0, 0) & 0xFFFFFF;
            XDestroyImage(im);
            if (v != pr[i].last) { printf("%.2f probe %s %06lx\n", now_ms() - t0, pr[i].name, v); pr[i].last = v; }
        }
    }
    XCloseDisplay(d);
    return 0;
}
