/* luxshell tray: freedesktop System Tray (XEmbed) manager hosted in a sidebar tile (SHELL-DESIGN 10).
   Owns _NET_SYSTEM_TRAY_S<screen>, announces MANAGER on the root, docks icon windows on REQUEST_DOCK by reparenting them
   into the surface that holds the N_TRAY node, and lays them out in rows (icon_size, pitch_x, pitch_y of the node).
   Alpha: we advertise our 32-bit visual (_NET_SYSTEM_TRAY_VISUAL). An ARGB child of an ARGB window would simply replace
   the parent's pixels (the glass under a transparent icon pixel becomes a hole), so when the Composite extension is there
   each icon is redirected manually and painted by us (cairo OVER, from the redirected window) in tray_paint; XDamage tells
   us when to repaint. Input still goes to the icon window itself. libXcomposite/libXdamage headers are not installed on
   this machine, so the five entry points are loaded with dlopen (no build dependency; without them icons draw themselves). */
#include "luxshell.h"
#include <X11/Xatom.h>
#include <cairo/cairo-xlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef XID Damage;
typedef struct { int type; unsigned long serial; Bool send_event; Display *display; Drawable drawable; Damage damage; int level; Bool more; Time timestamp; XRectangle area, geometry; } DamageNotifyEv;   /* = XDamageNotifyEvent */
static Bool (*pXCompositeQueryExtension)(Display *, int *, int *);
static void (*pXCompositeRedirectWindow)(Display *, Window, int);
static void (*pXCompositeUnredirectWindow)(Display *, Window, int);
static Bool (*pXDamageQueryExtension)(Display *, int *, int *);
static Damage (*pXDamageCreate)(Display *, Drawable, int);
static void (*pXDamageSubtract)(Display *, Damage, XID, XID);
static int damage_event = -1; static bool composite_ok;
#define REDIRECT_MANUAL 1
#define DAMAGE_REPORT_NON_EMPTY 3

struct icon { Window w; Damage dmg; int x, y, size; bool mapped, viewable_at_dock; int ignore_unmap; Visual *vis; cairo_surface_t *cs; };
static struct icon icons[64]; static int nicons;
static Window mgr; static struct node *tray_node; static struct surface *host;
static Atom a_sel, a_opcode, a_manager, a_xembed, a_xembed_info, a_orient, a_visual, a_iconsize, a_data;

static void load_ext(void) {
    void *c = dlopen("libXcomposite.so.1", RTLD_NOW), *d = dlopen("libXdamage.so.1", RTLD_NOW);
    if (c) { pXCompositeQueryExtension = dlsym(c, "XCompositeQueryExtension"); pXCompositeRedirectWindow = dlsym(c, "XCompositeRedirectWindow"); pXCompositeUnredirectWindow = dlsym(c, "XCompositeUnredirectWindow"); }
    if (d) { pXDamageQueryExtension = dlsym(d, "XDamageQueryExtension"); pXDamageCreate = dlsym(d, "XDamageCreate"); pXDamageSubtract = dlsym(d, "XDamageSubtract"); }
    int ev, er;
    if (pXCompositeQueryExtension && pXCompositeRedirectWindow && pXDamageQueryExtension && pXDamageCreate && pXDamageSubtract
        && pXCompositeQueryExtension(dpy, &ev, &er) && pXDamageQueryExtension(dpy, &ev, &er)) { composite_ok = true; damage_event = ev; }
    if (!composite_ok) fprintf(stderr, "luxshell: tray: no Composite/Damage, icons draw themselves (no alpha over the glass)\n");
}

/* a server timestamp for the selection: append to a property on our own window and take the PropertyNotify time */
static Time server_time(Window w) {
    XEvent e; unsigned char c = 0; XChangeProperty(dpy, w, a_data, XA_STRING, 8, PropModeAppend, &c, 0);
    XWindowEvent(dpy, w, PropertyChangeMask, &e); return e.xproperty.time;
}

Window tray_manager(void) { return mgr; }
int tray_count(void) { int n = 0; for (int i = 0; i < nicons; i++) if (icons[i].mapped) n++; return n; }
static void changed(void) { if (host) surface_damage(host); lua_pushinteger(L, tray_count()); emit("tray", 1); }

static void xembed_send(Window w, long msg, long detail, long d1, long d2) {
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = w; e.xclient.message_type = a_xembed; e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime; e.xclient.data.l[1] = msg; e.xclient.data.l[2] = detail; e.xclient.data.l[3] = d1; e.xclient.data.l[4] = d2;
    XSendEvent(dpy, w, False, NoEventMask, &e);
}
/* _XEMBED_INFO flags bit 0 = XEMBED_MAPPED; no property = map it (most clients set it, some old ones do not) */
static bool wants_map(Window w) {
    Atom t; int f; unsigned long n, after; unsigned char *d = NULL; bool m = true;
    if (XGetWindowProperty(dpy, w, a_xembed_info, 0, 2, False, a_xembed_info, &t, &f, &n, &after, &d) == Success && d) { if (n >= 2) m = (((long *)d)[1] & 1) != 0; XFree(d); }
    return m;
}
static int host_depth(void) { XWindowAttributes a; return host && XGetWindowAttributes(dpy, host->win, &a) ? a.depth : 32; }
/* hand an icon back to the root for the next tray: not redirected any more (a tray without compositing could not show it),
   unmapped (a WM would manage a mapped one), out of our save-set */
static void give_back(Window w) {
    XSelectInput(dpy, w, NoEventMask);
    xerror_quiet++;
    if (composite_ok && pXCompositeUnredirectWindow) pXCompositeUnredirectWindow(dpy, w, REDIRECT_MANUAL);
    XUnmapWindow(dpy, w); XReparentWindow(dpy, w, root, 0, 0); XRemoveFromSaveSet(dpy, w); XSync(dpy, False);
    xerror_quiet--;
}
static struct icon *find(Window w) { for (int i = 0; i < nicons; i++) if (icons[i].w == w) return &icons[i]; return NULL; }

static void dock(Window w) {
    if (find(w) || nicons >= 64 || !host) return;
    XWindowAttributes wa; if (!XGetWindowAttributes(dpy, w, &wa)) return;
    struct icon *ic = &icons[nicons++]; memset(ic, 0, sizeof *ic);
    ic->w = w; ic->size = tray_node ? tray_node->icon_size : 16; ic->vis = wa.visual; ic->x = -100; ic->y = -100;
    XSelectInput(dpy, w, StructureNotifyMask | PropertyChangeMask);
    XAddToSaveSet(dpy, w);                                     /* back to the root if luxshell goes away */
    if (wa.map_state == IsViewable) ic->ignore_unmap = 1;      /* the reparent below unmaps it once */
    /* an icon that sat in another tray may have a ParentRelative background (flxp-taskbar sets it on default-depth icons):
       reparenting that into our 32-bit window is a BadMatch and the icon would stay on the root. We paint it ourselves. */
    if (wa.depth != host_depth()) XSetWindowBackgroundPixmap(dpy, w, None);
    XReparentWindow(dpy, w, host->win, ic->x, ic->y);
    XResizeWindow(dpy, w, ic->size, ic->size);
    if (composite_ok) { XSync(dpy, False); xerror_quiet++; pXCompositeRedirectWindow(dpy, w, REDIRECT_MANUAL); XSync(dpy, False); xerror_quiet--;   /* BadAccess when still redirected from an earlier dock here */ ic->dmg = pXDamageCreate(dpy, w, DAMAGE_REPORT_NON_EMPTY);
        ic->cs = cairo_xlib_surface_create(dpy, w, ic->vis, ic->size, ic->size); }
    xembed_send(w, 0 /* XEMBED_EMBEDDED_NOTIFY */, 0, (long)host->win, 0 /* protocol version */);
    if (wants_map(w)) { XMapRaised(dpy, w); ic->mapped = true; }
    fprintf(stderr, "luxshell: tray: docked 0x%lx (depth %d%s)\n", w, wa.depth, composite_ok ? ", redirected" : "");
    changed();
}
static void undock(struct icon *ic, bool gone) {
    if (ic->cs) { if (gone) { XSync(dpy, False); xerror_quiet++; } cairo_surface_destroy(ic->cs); if (gone) { XSync(dpy, False); xerror_quiet--; } }   /* the Picture died with the window */
    if (!gone) XSelectInput(dpy, ic->w, NoEventMask);         /* the damage object dies with the window or with us */
    int i = (int)(ic - icons); memmove(icons + i, icons + i + 1, (nicons - i - 1) * sizeof *icons); nicons--; changed();   /* keep the docking order */
}

/* clean exit: hand the icons back to the root *unmapped* (the save-set would map them, and a WM that then manages them and
   puts passive button grabs on them leaves a frozen pointer grab behind once the next tray re-docks them), drop the selection */
void tray_stop(void) {
    if (!mgr) return;
    for (int i = 0; i < nicons; i++) { if (icons[i].cs) { cairo_surface_destroy(icons[i].cs); icons[i].cs = NULL; } give_back(icons[i].w); }
    nicons = 0; XSetSelectionOwner(dpy, a_sel, None, CurrentTime); XDestroyWindow(dpy, mgr); mgr = None; XSync(dpy, False);
}

bool tray_start(struct node *n) {
    struct surface *was = host;
    tray_node = n; host = surface_of_node(n);
    if (!host) { fprintf(stderr, "luxshell: tray: node is not on a surface yet\n"); return false; }
    if (mgr && was && was != host) {   /* the tray moved to another surface (sidebar ⇄ taskbar, minimized sidebar mode): re-parent the icons */
        for (int i = 0; i < nicons; i++) { struct icon *ic = &icons[i];
            if (ic->mapped) ic->ignore_unmap++;                      /* the reparent unmaps a mapped window once */
            ic->x = ic->y = -100; ic->size = n->icon_size;
            XReparentWindow(dpy, ic->w, host->win, ic->x, ic->y); XResizeWindow(dpy, ic->w, ic->size, ic->size);
            if (ic->mapped) XMapRaised(dpy, ic->w); }
        XFlush(dpy); changed();
    }
    if (mgr) return true;
    load_ext();
    char name[64]; snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", screen);
    a_sel = XInternAtom(dpy, name, False); a_opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False); a_manager = XInternAtom(dpy, "MANAGER", False);
    a_xembed = XInternAtom(dpy, "_XEMBED", False); a_xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False); a_orient = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    a_visual = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False); a_iconsize = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ICON_SIZE", False); a_data = XInternAtom(dpy, "_LHSHELL_TIMESTAMP", False);
    /* one tray per display. Another client's tray (flxp-taskbar in the classic mode) is left alone: the notifications
       tile hides itself and calls tray_start again when that owner goes away (runtime.lua watches the selection) */
    Window prev = XGetSelectionOwner(dpy, a_sel);
    if (prev != None) { fprintf(stderr, "luxshell: tray: %s is owned by 0x%lx, not taking it\n", name, prev); return false; }
    XSetWindowAttributes a = { .event_mask = PropertyChangeMask | StructureNotifyMask };
    mgr = XCreateWindow(dpy, root, -1, -1, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWEventMask, &a);
    XStoreName(dpy, mgr, "luxshell-tray");
    long orient = 0 /* _NET_SYSTEM_TRAY_ORIENTATION_HORZ */, vid = (long)XVisualIDFromVisual(argb), size = n->icon_size;
    XChangeProperty(dpy, mgr, a_orient, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&orient, 1);
    XChangeProperty(dpy, mgr, a_visual, XA_VISUALID, 32, PropModeReplace, (unsigned char *)&vid, 1);
    XChangeProperty(dpy, mgr, a_iconsize, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&size, 1);
    Time t = server_time(mgr);
    XSetSelectionOwner(dpy, a_sel, mgr, t);
    if (XGetSelectionOwner(dpy, a_sel) != mgr) { fprintf(stderr, "luxshell: tray: could not own %s\n", name); XDestroyWindow(dpy, mgr); mgr = None; return false; }
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = root; e.xclient.message_type = a_manager; e.xclient.format = 32;
    e.xclient.data.l[0] = t; e.xclient.data.l[1] = a_sel; e.xclient.data.l[2] = mgr;
    XSendEvent(dpy, root, False, StructureNotifyMask, &e); XFlush(dpy);
    fprintf(stderr, "luxshell: tray: own %s (manager 0x%lx, visual 0x%lx)\n", name, mgr, vid);
    return true;
}

bool tray_handle_event(XEvent *e) {
    if (!mgr) return false;
    if (composite_ok && e->type == damage_event /* + XDamageNotify (0) */) {
        DamageNotifyEv *d = (DamageNotifyEv *)e;
        pXDamageSubtract(dpy, d->damage, None, None); if (find(d->drawable) && host) surface_damage(host); return true;
    }
    switch (e->type) {
    case ClientMessage:
        if (e->xclient.window == mgr && e->xclient.message_type == a_opcode) {
            if (e->xclient.data.l[1] == 0 /* SYSTEM_TRAY_REQUEST_DOCK */) dock((Window)e->xclient.data.l[2]);
            return true;   /* BEGIN_MESSAGE / CANCEL_MESSAGE (balloons) are ignored */
        }
        return false;
    case SelectionClear:
        /* another tray took it (flxp-taskbar with tray_takeover): its MANAGER message makes the icons dock there. Hand back
           only the icons still inside our window, unmapped (as tray_stop), so we never pull one out of the new tray */
        if (e->xselectionclear.window == mgr) { fprintf(stderr, "luxshell: tray: lost the selection\n");
            while (nicons) { Window r, par = None, *k = NULL; unsigned nk = 0; Window w = icons[0].w;
                xerror_quiet++; if (XQueryTree(dpy, w, &r, &par, &k, &nk) && k) XFree(k); XSync(dpy, False); xerror_quiet--;
                if (host && par == host->win) give_back(w); else XSelectInput(dpy, w, NoEventMask);
                undock(&icons[0], false); }
            XDestroyWindow(dpy, mgr); mgr = None; XFlush(dpy); emit("tray-lost", 0); return true; }
        return false;
    case DestroyNotify: { struct icon *ic = find(e->xdestroywindow.window); if (ic && e->xdestroywindow.event == ic->w) { undock(ic, true); return true; } return false; }
    case UnmapNotify: { struct icon *ic = find(e->xunmap.window); if (!ic || e->xunmap.event != ic->w) return false;
        if (ic->ignore_unmap > 0) { ic->ignore_unmap--; return true; }
        if (ic->mapped) { ic->mapped = false; changed(); } return true; }
    case MapNotify: { struct icon *ic = find(e->xmap.window); if (!ic || e->xmap.event != ic->w) return false; if (!ic->mapped) { ic->mapped = true; changed(); } return true; }
    case ReparentNotify: { struct icon *ic = find(e->xreparent.window); if (!ic || e->xreparent.event != ic->w) return false;
        if (e->xreparent.parent != host->win) undock(ic, false); return true; }
    case ConfigureNotify: { struct icon *ic = find(e->xconfigure.window); if (!ic || e->xconfigure.event != ic->w) return false;
        if (e->xconfigure.width != ic->size || e->xconfigure.height != ic->size) XResizeWindow(dpy, ic->w, ic->size, ic->size);   /* the slot decides */
        return true; }
    case PropertyNotify: { struct icon *ic = find(e->xproperty.window); if (!ic) return false;
        if (e->xproperty.atom == a_xembed_info) { bool m = wants_map(ic->w); if (m && !ic->mapped) { XMapRaised(dpy, ic->w); ic->mapped = true; changed(); } else if (!m && ic->mapped) { ic->ignore_unmap++; XUnmapWindow(dpy, ic->w); ic->mapped = false; changed(); } }
        return true; }
    }
    return false;
}

/* rows of icon_size at pitch_x/pitch_y from the node's content origin, as many per row as fit */
void tray_place(struct surface *s) {
    if (!tray_node || s != host) return;
    struct node *n = tray_node; double ox = n->cx + n->ml, oy = n->cy + n->mt, w = n->cw - n->ml - n->mr;
    int per = n->pitch_x > 0 ? (int)((w - n->icon_size) / n->pitch_x) + 1 : 1; if (per < 1) per = 1;
    bool shown = n->visible && n->cw > 0 && n->ch > 0; struct node *top = n;
    for (struct node *p = n->parent; p; p = p->parent) { if (!p->visible) shown = false; top = p; }
    if (top != s->root) shown = false;                         /* tile closed: the node is detached */
    int k = 0;
    for (int i = 0; i < nicons; i++) {
        struct icon *ic = &icons[i]; if (!ic->mapped) continue;
        int x = shown ? (int)(ox + (k % per) * n->pitch_x) : -100, y = shown ? (int)(oy + (k / per) * n->pitch_y) : -100; k++;
        if (shown && y + ic->size > n->cy + n->ch) { x = -100; y = -100; }      /* clipped by the tile: park it off-surface */
        if (x != ic->x || y != ic->y) { ic->x = x; ic->y = y; XMoveWindow(dpy, ic->w, x, y); }
    }
}
void tray_paint(struct node *n, cairo_t *cr) {
    if (n != tray_node || !composite_ok) return;
    for (int i = 0; i < nicons; i++) {
        struct icon *ic = &icons[i]; if (!ic->mapped || !ic->cs || ic->x < 0) continue;
        cairo_save(cr); cairo_rectangle(cr, ic->x, ic->y, ic->size, ic->size); cairo_clip(cr);
        cairo_set_source_surface(cr, ic->cs, ic->x, ic->y); cairo_paint(cr); cairo_restore(cr);
    }
}
/* a theme switch rebuilds the sidebar: the tray node may be freed before the new tile calls tray_start again */
void tray_forget(struct node *gone) { for (struct node *p = tray_node; p; p = p->parent) if (p == gone) { tray_node = NULL; return; } }
