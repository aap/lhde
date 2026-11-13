/* A plain managed window for luxwm's scripted tests (tests/headless/alttab.sh): testwin TITLE RRGGBB [skip | transient OWNER-ID]
   Title TITLE, background and a 32 x 32 _NET_WM_ICON in the colour (a black 1 px rim), WM_CLASS testwin; "skip" adds
   _NET_WM_STATE_SKIP_TASKBAR, "transient" makes it a dialog (_NET_WM_WINDOW_TYPE_DIALOG) transient for window OWNER-ID.
   Prints its window id, runs until closed (WM_DELETE_WINDOW) or killed. cc -o testwin testwin.c -lX11 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: testwin TITLE RRGGBB [skip | transient OWNER-ID]\n"); return 2; }
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    unsigned long rgb = strtoul(argv[2], NULL, 16) & 0xffffff;
    int s = DefaultScreen(d);
    Window w = XCreateSimpleWindow(d, RootWindow(d, s), 100, 100, 320, 200, 0, 0, rgb);
    XStoreName(d, w, argv[1]);
    Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_NAME", False), utf8, 8, PropModeReplace, (unsigned char *)argv[1], (int)strlen(argv[1]));
    XClassHint ch = { (char *)"testwin", (char *)"testwin" }; XSetClassHint(d, w, &ch);
    unsigned long icon[2 + 32 * 32]; icon[0] = icon[1] = 32;
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++)
        icon[2 + y * 32 + x] = 0xff000000ul | (x == 0 || y == 0 || x == 31 || y == 31 ? 0 : rgb);
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_ICON", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)icon, 2 + 32 * 32);
    if (argc > 3 && !strcmp(argv[3], "skip")) {
        Atom st = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);
        XChangeProperty(d, w, XInternAtom(d, "_NET_WM_STATE", False), XA_ATOM, 32, PropModeReplace, (unsigned char *)&st, 1);
    } else if (argc > 4 && !strcmp(argv[3], "transient")) {
        Atom dlg = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        XChangeProperty(d, w, XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32, PropModeReplace, (unsigned char *)&dlg, 1);
        XSetTransientForHint(d, w, (Window)strtoul(argv[4], NULL, 0));
    }
    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &del, 1);
    XMapWindow(d, w);
    printf("0x%lx\n", w); fflush(stdout);
    for (;;) {
        XEvent ev; XNextEvent(d, &ev);
        if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == del) break;
    }
    XCloseDisplay(d);
    return 0;
}
