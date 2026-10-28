/* Two dock windows A (mapped first) and B; then A (or B) reorders itself, for the docks_raise() order tests.
   dockorder raise|lower|below|above : A is mapped first, B second, then A XRaiseWindow / B XLowerWindow / A ConfigureRequest
   stack_mode Above with sibling B... ; prints the XQueryTree order bottom to top afterwards.
   cc -o dockorder dockorder.c -lX11 ; DISPLAY=:3 ./dockorder raise */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static Display *d;
static Window mk(int x, const char *n)
{
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), x, 700, 300, 30, 0, 0, 0x406080);
    Atom t = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32, PropModeReplace, (unsigned char *)&t, 1);
    XStoreName(d, w, n);
    XMapWindow(d, w);
    XSync(d, False);
    usleep(200000);
    return w;
}
static void order(const char *what, Window a, Window b)
{
    Window r, p, *k; unsigned n;
    XSync(d, False); usleep(200000);
    XQueryTree(d, DefaultRootWindow(d), &r, &p, &k, &n);
    int ia = -1, ib = -1;
    for (unsigned i = 0; i < n; i++) { if (k[i] == a) ia = i; if (k[i] == b) ib = i; }
    printf("%s: A=%d B=%d -> %s above\n", what, ia, ib, ia > ib ? "A" : "B");
    XFree(k);
}
int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "raise";
    if (!(d = XOpenDisplay(NULL))) return 1;
    Window a = mk(0, "dockA"), b = mk(400, "dockB");
    order("mapped A,B", a, b);
    if (!strcmp(m, "raise")) XRaiseWindow(d, a);
    else if (!strcmp(m, "lower")) XLowerWindow(d, b);
    else if (!strcmp(m, "above")) { XWindowChanges c; c.sibling = b; c.stack_mode = Above; XConfigureWindow(d, a, CWSibling | CWStackMode, &c); }
    else if (!strcmp(m, "below")) { XWindowChanges c; c.sibling = a; c.stack_mode = Below; XConfigureWindow(d, b, CWSibling | CWStackMode, &c); }
    order(m, a, b);
    printf("A=0x%lx B=0x%lx\n", a, b);
    fflush(stdout);
    if (argc > 2) pause();
    return 0;
}
