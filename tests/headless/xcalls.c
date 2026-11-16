/* xcalls: an LD_PRELOAD shim that logs a program's X round trips and presentations, for the tests that measure how a wux
 * program behaves under a busy X server (tests/headless/timerbusy.sh) and for finding where a program waits on the server.
 * With $XCALLS_LOG every call below is written there, one line each: milliseconds on CLOCK_MONOTONIC, the call, and for a
 * reply the milliseconds it waited ("12.5 reply 3.1"):
 *
 *   reply           xcb_wait_for_reply(64): every Xlib call that waits for the server's answer (XQueryPointer, XSync,
 *                   XGetGeometry, XGetWindowProperty, XInternAtom, ...) comes through it
 *   XQueryPointer   XSync    by name as well, so that a log can tell them apart
 *   XPutImage W H X Y, XShmPutImage W H X Y, XCopyArea W H SX SY X Y    the presentations: client pixels sent
 *                   (through the connection, through shared memory), and server-side copies
 *
 * With $XCALLS_STACK as well, each reply is followed by its call stack (addr2line -f -C -e PROGRAM names the frames).
 * Only the program's own calls into libX11 are seen by name; libX11's into libxcb all are (they cross a library). Portable
 * to FreeBSD: dlsym(RTLD_NEXT) only. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <X11/Xlib.h>
#include <execinfo.h>

static FILE* out;
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static void put(const char* fmt, ...)
{
    if (!out) {
        const char* p = getenv("XCALLS_LOG");
        if (!p || !(out = fopen(p, "a"))) return;
        setvbuf(out, NULL, _IOLBF, 0);
    }
    fprintf(out, "%.1f ", now_ms());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
}
/* with $XCALLS_STACK set, a reply's call stack follows it, a frame a line ("  module(symbol+off) [addr]") */
static void stack(void)
{
    static int on = -1;
    if (on < 0) on = getenv("XCALLS_STACK") != NULL;
    if (!on || !out) return;
    void* f[16];
    int n = backtrace(f, 16);
    char** s = backtrace_symbols(f, n);
    for (int i = 2; s && i < n; i++) fprintf(out, "  %s\n", s[i]);
    free(s);
}
/* with $XCALLS_DELAY_MS, every reply arrives that much later (a server held up behind a compositor's frame), from
 * $XCALLS_DELAY_AFTER milliseconds after the first reply on (0: from the start; the start-up's round trips are many) */
static void slow(void)
{
    static long ms = -1, after = 0;
    static double first;
    if (ms < 0) {
        const char* d = getenv("XCALLS_DELAY_MS"), *a = getenv("XCALLS_DELAY_AFTER");
        ms = d ? atol(d) : 0;
        after = a ? atol(a) : 0;
        first = now_ms();
    }
    if (ms > 0 && now_ms() - first >= after) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
}
#define NEXT(name) static __typeof__(name)* real; if (!real) real = (__typeof__(name)*)dlsym(RTLD_NEXT, #name)

/* libxcb's, declared here so that the shim needs no xcb headers */
typedef struct xcb_connection_t xcb_connection_t;
typedef struct xcb_generic_error_t xcb_generic_error_t;
void* xcb_wait_for_reply(xcb_connection_t* c, unsigned int request, xcb_generic_error_t** e);
void* xcb_wait_for_reply64(xcb_connection_t* c, uint64_t request, xcb_generic_error_t** e);

void* xcb_wait_for_reply(xcb_connection_t* c, unsigned int request, xcb_generic_error_t** e)
{
    NEXT(xcb_wait_for_reply);
    double t = now_ms();
    slow();
    void* r = real(c, request, e);
    put("reply %.1f", now_ms() - t);
    stack();
    return r;
}
void* xcb_wait_for_reply64(xcb_connection_t* c, uint64_t request, xcb_generic_error_t** e)
{
    NEXT(xcb_wait_for_reply64);
    double t = now_ms();
    slow();
    void* r = real(c, request, e);
    put("reply %.1f", now_ms() - t);
    stack();
    return r;
}
Bool XQueryPointer(Display* d, Window w, Window* r, Window* c, int* rx, int* ry, int* wx, int* wy, unsigned* m)
{
    NEXT(XQueryPointer);
    put("XQueryPointer");
    return real(d, w, r, c, rx, ry, wx, wy, m);
}
int XSync(Display* d, Bool discard)
{
    NEXT(XSync);
    put("XSync");
    return real(d, discard);
}
int XPutImage(Display* d, Drawable dr, GC gc, XImage* img, int sx, int sy, int dx, int dy, unsigned w, unsigned h)
{
    NEXT(XPutImage);
    put("XPutImage %u %u %d %d", w, h, dx, dy);
    return real(d, dr, gc, img, sx, sy, dx, dy, w, h);
}
int XCopyArea(Display* d, Drawable s, Drawable dst, GC gc, int sx, int sy, unsigned w, unsigned h, int dx, int dy)
{
    NEXT(XCopyArea);
    put("XCopyArea %u %u %d %d %d %d", w, h, sx, sy, dx, dy);
    return real(d, s, dst, gc, sx, sy, w, h, dx, dy);
}
/* libXext's, declared here so that the shim needs no extension headers */
Bool XShmPutImage(Display* d, Drawable dr, GC gc, XImage* img, int sx, int sy, int dx, int dy, unsigned w, unsigned h, Bool ev);
Bool XShmPutImage(Display* d, Drawable dr, GC gc, XImage* img, int sx, int sy, int dx, int dy, unsigned w, unsigned h, Bool ev)
{
    NEXT(XShmPutImage);
    put("XShmPutImage %u %u %d %d", w, h, dx, dy);
    return real(d, dr, gc, img, sx, sy, dx, dy, w, h, ev);
}
