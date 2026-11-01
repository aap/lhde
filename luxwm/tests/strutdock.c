/* A _NET_WM_WINDOW_TYPE_DOCK window with a bottom _NET_WM_STRUT_PARTIAL, for the work area tests (LUXWM.md "EWMH and the shell").
   strutdock X Y W H : the window at X,Y WxH, bottom strut = screen height - Y over the x-range X..X+W-1.
   cc -o strutdock strutdock.c -lX11 ; DISPLAY=:3 ./strutdock 0 770 1280 30 & DISPLAY=:3 ./strutdock 1131 600 149 170 & */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: strutdock X Y W H\n"); return 2; }
    int x = atoi(argv[1]), y = atoi(argv[2]), w = atoi(argv[3]), h = atoi(argv[4]);
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    int s = DefaultScreen(d);
    Window win = XCreateSimpleWindow(d, RootWindow(d, s), x, y, w, h, 0, 0, 0x406080);
    Atom type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), dock = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(d, win, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&dock, 1);
    long p[12] = { 0 };
    p[3] = DisplayHeight(d, s) - y; p[10] = x; p[11] = x + w - 1;
    XChangeProperty(d, win, XInternAtom(d, "_NET_WM_STRUT_PARTIAL", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)p, 12);
    XStoreName(d, win, "strutdock");
    XMapWindow(d, win);
    XFlush(d);
    for (;;) { XEvent e; XNextEvent(d, &e); }
}
