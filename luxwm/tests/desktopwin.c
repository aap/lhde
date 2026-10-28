/* Stand-in for flxp-desktop (focus tests, README "EWMH and the shell"): an unmanaged ARGB _NET_WM_WINDOW_TYPE_DESKTOP window over
   the whole screen that takes the input focus itself on every click (XSetInputFocus, as flxp-desktop does) and pops an
   override-redirect "context menu" on a right click, which grabs pointer and keyboard until the next click.
   cc -o desktopwin desktopwin.c -lX11 ; DISPLAY=:3 ./desktopwin */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    int s = DefaultScreen(d); Window root = RootWindow(d, s);
    XVisualInfo vi; Visual *vis = DefaultVisual(d, s); int depth = DefaultDepth(d, s); Colormap cm = DefaultColormap(d, s);
    if (XMatchVisualInfo(d, s, 32, TrueColor, &vi)) { vis = vi.visual; depth = 32; cm = XCreateColormap(d, root, vis, AllocNone); }
    XSetWindowAttributes wa; memset(&wa, 0, sizeof wa);
    wa.colormap = cm; wa.background_pixel = 0x40204060; wa.border_pixel = 0;
    wa.event_mask = ButtonPressMask | KeyPressMask | FocusChangeMask;
    Window w = XCreateWindow(d, root, 0, 0, DisplayWidth(d, s), DisplayHeight(d, s), 0, depth, InputOutput, vis,
                             CWColormap | CWBackPixel | CWBorderPixel | CWEventMask, &wa);
    Atom type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), desk = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(d, w, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&desk, 1);
    XStoreName(d, w, "desktopwin");
    XMapWindow(d, w);
    wa.override_redirect = True; wa.background_pixel = 0xffe0e0e0; wa.event_mask = ButtonPressMask | KeyPressMask;
    Window menu = XCreateWindow(d, root, 0, 0, 160, 120, 1, depth, InputOutput, vis,
                                CWColormap | CWBackPixel | CWBorderPixel | CWEventMask | CWOverrideRedirect, &wa);
    int menu_up = 0;
    printf("desktop 0x%lx menu 0x%lx\n", w, menu); fflush(stdout);
    for (;;) {
        XEvent e; XNextEvent(d, &e);
        if (e.type == ButtonPress) {
            if (menu_up) {   /* any click closes the menu (consumed, like a real menu) */
                XUngrabPointer(d, e.xbutton.time); XUngrabKeyboard(d, e.xbutton.time); XUnmapWindow(d, menu); menu_up = 0;
                printf("menu closed\n"); fflush(stdout); continue;
            }
            if (e.xbutton.window == w) {
                XSetInputFocus(d, w, RevertToPointerRoot, e.xbutton.time);
                printf("click %d: focus taken\n", e.xbutton.button); fflush(stdout);
                if (e.xbutton.button == 3) {
                    XMoveWindow(d, menu, e.xbutton.x_root, e.xbutton.y_root); XMapRaised(d, menu);
                    XGrabPointer(d, menu, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, e.xbutton.time);
                    XGrabKeyboard(d, menu, False, GrabModeAsync, GrabModeAsync, e.xbutton.time);
                    menu_up = 1; printf("menu open\n"); fflush(stdout);
                }
            }
        } else if (e.type == KeyPress) {
            printf("key %u on 0x%lx\n", e.xkey.keycode, e.xkey.window); fflush(stdout);
        } else if (e.type == FocusIn || e.type == FocusOut) {
            printf("%s mode %d detail %d\n", e.type == FocusIn ? "FocusIn" : "FocusOut", e.xfocus.mode, e.xfocus.detail); fflush(stdout);
        }
    }
}
