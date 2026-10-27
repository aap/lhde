// traytest: a minimal freedesktop system tray client (XEmbed), for testing
// flxp-taskbar's notification area without a real tray application.
//   build/traytest [COLOR [SECONDS]]    e.g. build/traytest red 30
// Docks one 16x16 icon (a filled disc on the tray's background), redraws on
// Expose, prints what happens; button 1 toggles XEMBED_MAPPED off for 2 s,
// button 3 quits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

int main(int argc, char** argv)
{
    const char* color = argc > 1 ? argv[1] : "red";
    int seconds = argc > 2 ? atoi(argv[2]) : 60;
    Display* d = XOpenDisplay(nullptr);
    if (!d) { fprintf(stderr, "traytest: no display\n"); return 1; }
    int scr = DefaultScreen(d);
    Window root = RootWindow(d, scr);
    char name[64];
    snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", scr);
    Atom sel = XInternAtom(d, name, False), opcode = XInternAtom(d, "_NET_SYSTEM_TRAY_OPCODE", False);
    Atom info = XInternAtom(d, "_XEMBED_INFO", False), xembed = XInternAtom(d, "_XEMBED", False);
    Atom manager = XInternAtom(d, "MANAGER", False);
    XSetWindowAttributes a = {};
    a.background_pixmap = ParentRelative;
    a.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask;
    Window w = XCreateWindow(d, root, 0, 0, 16, 16, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixmap | CWEventMask, &a);
    XStoreName(d, w, "traytest");
    long inf[2] = { 0, 1 /* XEMBED_MAPPED */ };
    XChangeProperty(d, w, info, info, 32, PropModeReplace, (unsigned char*)inf, 2);
    XColor c, exact;
    XAllocNamedColor(d, DefaultColormap(d, scr), color, &c, &exact);
    GC gc = XCreateGC(d, w, 0, nullptr);
    XSetForeground(d, gc, c.pixel);
    XSelectInput(d, root, StructureNotifyMask);

    auto dock = [&]() {
        Window owner = XGetSelectionOwner(d, sel);
        if (!owner) { printf("traytest: no tray (%s has no owner)\n", name); return; }
        XEvent e = {};
        e.xclient.type = ClientMessage;
        e.xclient.window = owner;
        e.xclient.message_type = opcode;
        e.xclient.format = 32;
        e.xclient.data.l[0] = CurrentTime;
        e.xclient.data.l[1] = 0;        // SYSTEM_TRAY_REQUEST_DOCK
        e.xclient.data.l[2] = (long)w;
        XSendEvent(d, owner, False, NoEventMask, &e);
        XFlush(d);
        printf("traytest: dock request to 0x%lx\n", owner);
    };
    dock();
    time_t end = time(nullptr) + seconds, unhide = 0;
    while (time(nullptr) < end) {
        while (XPending(d)) {
            XEvent e;
            XNextEvent(d, &e);
            if (e.type == Expose && e.xexpose.count == 0) {
                XClearWindow(d, w);
                XFillArc(d, w, gc, 2, 2, 12, 12, 0, 360 * 64);
            } else if (e.type == ReparentNotify) {
                printf("traytest: reparented to 0x%lx\n", e.xreparent.parent);
            } else if (e.type == ClientMessage && e.xclient.message_type == xembed) {
                printf("traytest: XEMBED message %ld\n", e.xclient.data.l[1]);
            } else if (e.type == ClientMessage && e.xclient.message_type == manager) {
                printf("traytest: a new tray manager\n");
                dock();
            } else if (e.type == ButtonPress && e.xbutton.button == 3) {
                end = 0;
            } else if (e.type == ButtonPress && e.xbutton.button == 1) {
                inf[1] = 0;
                XChangeProperty(d, w, info, info, 32, PropModeReplace, (unsigned char*)inf, 2);
                unhide = time(nullptr) + 2;
                printf("traytest: hidden\n");
            }
        }
        if (unhide && time(nullptr) >= unhide) {
            inf[1] = 1;
            XChangeProperty(d, w, info, info, 32, PropModeReplace, (unsigned char*)inf, 2);
            unhide = 0;
            printf("traytest: shown again\n");
        }
        fflush(stdout);
        usleep(50000);
    }
    XCloseDisplay(d);
    return 0;
}
