/* lhwm: popup menus shown by the window manager (menu.h; docs/LUXWM.md "The window menu"). The pixels are wuxtheme's popup menu
   painter (ut_menu_draw: the same code draws wux's user32 menus); this file keeps the geometry on screen, the shadow and input. */
#include "config.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <poll.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <cairo/cairo-xlib.h>
#include <glib.h>
#include <wuxtheme/ut.h>
#include <lhde/settings.h>

#define SHADOW 4   /* XP's menu drop shadow: right and bottom, 4 px (measured on the 4074 Slate window menu) */

/* what the painter is asked: the frame theme's visual style (its Menu class, [SysMetrics] colours, FLATMENUS, MENUFONT), or
   Windows Classic from the scheme's colours and Metrics.menufont; the text smoothed as the system's (sys_smoothing), else as the
   frames' captions are */
struct look {
    ut_menu_spec spec; ut_font font; ut_menu_item it[64]; int n;
};

struct geom { int w, h, pad, y[64], ih[64]; };

/* the system's font smoothing, as wux's GDI has it for flxp's menus (flxp shell::setup_fonts): settings.ini [fonts] smoothing, else
   the session theme's Fonts.smoothing (wmmenu_set_theme_smoothing); -1 when neither says */
static char theme_smoothing[16];
void wmmenu_set_theme_smoothing(const char *v) { snprintf(theme_smoothing, sizeof theme_smoothing, "%s", v ? v : ""); }
static int sys_smoothing(void) {
    char b[32]; const char *v = lhde_settings_get("fonts", "smoothing", b, sizeof b);
    if (!v || !*v) v = theme_smoothing;
    if (!*v) return -1;
    return !strcasecmp(v, "cleartype") ? 2 : !strcasecmp(v, "off") || !strcasecmp(v, "none") || !strcmp(v, "0") ? 0 : 1;
}

static void look_build(const struct theme *t, const struct wmmenu_item *items, int n, struct look *L) {
    memset(L, 0, sizeof *L);
    ut_menu_spec *s = &L->spec;
    if (t->classic) {
        s->colors = t->cl; s->flat = 0;
        snprintf(L->font.face, sizeof L->font.face, "%s", t->cl_mface);
        L->font.height = -t->cl_mpx; L->font.weight = t->cl_mbold ? 700 : 400; L->font.italic = t->cl_mitalic;
        s->font = &L->font; s->smoothing = t->cl_smooth;
    } else {
        s->theme = (ut_theme *)t->ut; s->flat = -1;
        s->smoothing = t->mode != THEME_MSSTYLES || t->ms_font_aa == CAIRO_ANTIALIAS_SUBPIXEL ? 2 : t->ms_font_aa == CAIRO_ANTIALIAS_NONE ? 0 : 1;
    }
    if (sys_smoothing() >= 0) s->smoothing = sys_smoothing();   /* the same text as flxp's menus, not the caption's own tuning */
    L->n = n < 64 ? n : 64;
    for (int i = 0; i < L->n; i++) {   /* WMMENU_* flags and glyphs are ut_menu's */
        L->it[i].text = items[i].text; L->it[i].accel = items[i].accel;
        L->it[i].flags = items[i].flags & (UT_MENU_DISABLED | UT_MENU_SEPARATOR | UT_MENU_DEFAULT | UT_MENU_CHECKED);
        if (items[i].sub) L->it[i].flags |= UT_MENU_SUBMENU;   /* a cascading item (the desktops submenu): the painter draws its arrow */
        L->it[i].glyph = items[i].glyph;
    }
}

static void measure(const struct look *L, struct geom *g) {
    memset(g, 0, sizeof *g);
    ut_menu_layout(&L->spec, L->it, L->n, &g->w, &g->h, &g->pad, g->y, g->ih);
}

static void paint(cairo_t *cr, const struct look *L, const struct geom *g, int sel, int cues) {
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g->w, g->h);
    cairo_surface_flush(s);
    ut_menu_draw(&L->spec, L->it, L->n, sel, cues, (uint32_t *)cairo_image_surface_get_data(s), g->w, g->h, cairo_image_surface_get_stride(s));
    cairo_surface_mark_dirty(s);
    cairo_set_source_surface(cr, s, 0, 0); cairo_paint(cr); cairo_surface_destroy(s);
}

/* "&Move" -> "Move" and the byte offset of the mnemonic (-1: none) */
static void strip_amp(const char *s, char *out, size_t n, int *mn) {
    size_t o = 0; *mn = -1;
    for (const char *p = s ? s : ""; *p && o + 1 < n; p++) {
        if (*p == '&' && p[1] == '&') { out[o++] = '&'; p++; continue; }
        if (*p == '&' && p[1]) { if (*mn < 0) *mn = (int)o; continue; }
        out[o++] = *p;
    }
    out[o] = 0;
}

/* the drop shadow right of and below the W x H menu, alpha per pixel (4074 Slate measurement: 0.44 0.33 0.17 0.055 outwards, starting
   5 px in from the far corner with a 4 px ramp) */
static double shadow_alpha(int x, int y, int W, int H) {
    static const double p[5] = { 0, 0.44, 0.33, 0.17, 0.055 }, q[5] = { 0.12, 0.33, 0.65, 0.88, 1 };
    int dx = x - W + 1, dy = y - H + 1;   /* 1..SHADOW outside the right / bottom edge */
    if (dx >= 1 && dy >= 1) return dx <= SHADOW && dy <= SHADOW ? p[dx] * p[dy] / p[1] : 0;
    if (dx >= 1 && dx <= SHADOW) { int k = y - 5; return k < 0 ? 0 : p[dx] * (k < 5 ? q[k] : 1); }
    if (dy >= 1 && dy <= SHADOW) { int k = x - 5; return k < 0 ? 0 : p[dy] * (k < 5 ? q[k] : 1); }
    return 0;
}

cairo_surface_t *wmmenu_render(const struct theme *t, const struct wmmenu_item *items, int n, int sel, unsigned flags, int *w, int *h) {
    struct look L; struct geom g; look_build(t, items, n, &L); measure(&L, &g);
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.w + SHADOW, g.h + SHADOW);
    cairo_t *cr = cairo_create(img);
    paint(cr, &L, &g, sel, flags & WMMENU_KEYBOARD);
    cairo_destroy(cr); cairo_surface_flush(img);
    unsigned char *d = cairo_image_surface_get_data(img); int st = cairo_image_surface_get_stride(img);
    for (int y = 0; y < g.h + SHADOW; y++) for (int x = 0; x < g.w + SHADOW; x++) {
        if (x < g.w && y < g.h) continue;
        double a = shadow_alpha(x, y, g.w, g.h); ((uint32_t *)(d + y * st))[x] = (uint32_t)(a * 255 + 0.5) << 24;   /* black, premultiplied */
    }
    cairo_surface_mark_dirty(img);
    if (w) *w = g.w;
    if (h) *h = g.h;
    return img;
}

/* ---- the popup ---- */
struct run {
    const struct wmmenu_env *env; const struct wmmenu_item *items; int n;
    struct look L; struct geom g; Window win; int x, y, W, H; int sel, cues;
    cairo_surface_t *under;             /* no compositor: the screen under the menu + shadow, for the blended shadow */
};

static void run_paint(struct run *r) {
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r->W, r->H); cairo_t *cr = cairo_create(img);
    if (r->under) { cairo_set_source_surface(cr, r->under, 0, 0); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_paint(cr); cairo_set_operator(cr, CAIRO_OPERATOR_OVER); }
    /* shadow */
    cairo_surface_t *sh = cairo_image_surface_create(CAIRO_FORMAT_A8, r->W, r->H);
    unsigned char *d = cairo_image_surface_get_data(sh); int st = cairo_image_surface_get_stride(sh);
    cairo_surface_flush(sh);
    for (int y = 0; y < r->H; y++) for (int x = 0; x < r->W; x++) d[y * st + x] = (unsigned char)(shadow_alpha(x, y, r->g.w, r->g.h) * 255 + 0.5);
    cairo_surface_mark_dirty(sh);
    cairo_set_source_rgb(cr, 0, 0, 0); cairo_mask_surface(cr, sh, 0, 0); cairo_surface_destroy(sh);
    paint(cr, &r->L, &r->g, r->sel, r->cues);
    cairo_destroy(cr);
    const struct wmmenu_env *e = r->env;
    cairo_surface_t *xs = cairo_xlib_surface_create(e->dpy, r->win, e->visual, r->W, r->H); cairo_t *xc = cairo_create(xs);
    cairo_set_operator(xc, CAIRO_OPERATOR_SOURCE); cairo_set_source_surface(xc, img, 0, 0); cairo_paint(xc);
    cairo_destroy(xc); cairo_surface_flush(xs); cairo_surface_destroy(xs); cairo_surface_destroy(img);
    XFlush(e->dpy);
}

static int item_at(const struct run *r, int rx, int ry) {
    int x = rx - r->x, y = ry - r->y;
    if (x < r->g.pad || x >= r->g.w - r->g.pad || y < r->g.pad || y >= r->g.h - r->g.pad) return -1;
    for (int i = 0; i < r->n && i < 64; i++)
        if (y >= r->g.y[i] && y < r->g.y[i] + r->g.ih[i]) return (r->items[i].flags & WMMENU_SEPARATOR) ? -1 : i;
    return -1;
}
static int inside(const struct run *r, int rx, int ry) { return rx >= r->x && rx < r->x + r->g.w && ry >= r->y && ry < r->y + r->g.h; }

static int next_item(const struct run *r, int from, int dir) {
    for (int k = 0, i = from; k < r->n; k++) {
        i = (i + dir + r->n) % r->n;
        if (!(r->items[i].flags & WMMENU_SEPARATOR)) return i;
    }
    return -1;
}

static Bool menu_pred(Display *d, XEvent *ev, XPointer arg) {
    (void)d; (void)arg;
    switch (ev->type) {
    case MotionNotify: case ButtonPress: case ButtonRelease: case KeyPress:
    case Expose: case MapRequest: case ConfigureRequest:
        return True;
    }
    return False;
}

static double ms_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }

#define MENU_SHOW_DELAY 400   /* SPI_GETMENUSHOWDELAY's default: hovering a submenu item this long opens it */
#define SUB_BACK   (-3)       /* a submenu returns: the pointer went back to its parent (the event is put back for the parent) */
#define SUB_ALL    (-4)       /* a submenu returns: the whole menu is dismissed */

/* the next menu event, waiting at most timeout ms (-1: no limit); false on timeout */
static bool menu_next(Display *dpy, XEvent *ev, int timeout) {
    for (;;) {
        if (XCheckIfEvent(dpy, ev, menu_pred, NULL)) return true;
        XFlush(dpy);
        struct pollfd pfd = { .fd = ConnectionNumber(dpy), .events = POLLIN };
        if (poll(&pfd, 1, timeout) == 0) return false;
    }
}

static bool menu_grab(Display *dpy, Window w, Window root, bool keyboard) {
    int gp = GrabNotViewable, gk = keyboard ? GrabNotViewable : GrabSuccess;
    for (int i = 0; i < 50; i++) {   /* a client's own grab (or lhwm's passive key grab) may be ending */
        if (gp != GrabSuccess) gp = XGrabPointer(dpy, w, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        if (gk != GrabSuccess) gk = XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        if (gp == GrabSuccess && gk == GrabSuccess) return true;
        struct timespec ts = { 0, 5000000 }; nanosleep(&ts, NULL);
    }
    fprintf(stderr, "luxwm: menu: grab failed (pointer %d, keyboard %d)\n", gp, gk);
    return false;
}

static int popup(const struct wmmenu_env *e, int x, int y, const struct wmmenu_item *items, int n, unsigned flags, unsigned button, struct run *parent);

/* open the submenu of item i of r beside it; returns what the submenu returned (>= 0: chosen, r is done) */
static int open_sub(struct run *r, int i, bool keyboard) {
    const struct wmmenu_item *it = &r->items[i];
    if (!it->sub || it->nsub <= 0 || (it->flags & WMMENU_DISABLED)) return WMMENU_CANCEL;
    r->sel = i; run_paint(r);
    int res = popup(r->env, r->x + r->g.w - 3, r->y + r->g.y[i] - r->g.pad, it->sub, it->nsub, keyboard ? WMMENU_KEYBOARD : 0, 0, r);
    if (res < 0 && res != SUB_ALL) menu_grab(r->env->dpy, r->win, r->env->root, false);   /* the pointer grab went with the submenu's window */
    return res;
}

int wmmenu_popup(const struct wmmenu_env *e, int x, int y, const struct wmmenu_item *items, int n, unsigned flags, unsigned button) {
    int r = popup(e, x, y, items, n, flags, button, NULL);
    return r == SUB_ALL || r == SUB_BACK ? WMMENU_CANCEL : r;
}

static int popup(const struct wmmenu_env *e, int x, int y, const struct wmmenu_item *items, int n, unsigned flags, unsigned button, struct run *parent) {
    struct run r; memset(&r, 0, sizeof r);
    if (n <= 0) return WMMENU_CANCEL;
    if (n > 64) n = 64;
    r.env = e; r.items = items; r.n = n; r.cues = (flags & WMMENU_KEYBOARD) != 0;
    look_build(e->theme, items, n, &r.L); measure(&r.L, &r.g);
    r.W = r.g.w + SHADOW; r.H = r.g.h + SHADOW;
    /* fit into the monitor: a context menu flips to the left of / above the point, the window menu is shifted (TrackPopupMenu) */
    int bx1 = e->bx + e->bw, by1 = e->by + e->bh;
    if (parent && x + r.g.w > bx1 && parent->x - r.g.w + 3 >= e->bx) x = parent->x - r.g.w + 3;   /* a cascade goes left of its parent */
    if (x + r.g.w > bx1) x = (flags & WMMENU_FLIP) && x - r.g.w >= e->bx ? x - r.g.w : bx1 - r.g.w;
    if (y + r.g.h > by1) y = (flags & WMMENU_FLIP) && y - r.g.h >= e->by ? y - r.g.h : by1 - r.g.h;
    if (x < e->bx) x = e->bx;
    if (y < e->by) y = e->by;
    r.x = x; r.y = y;
    r.sel = (flags & WMMENU_KEYBOARD) ? next_item(&r, -1, 1) : -1;   /* XP: the first item, greyed or not, when opened from the keyboard */
    Display *dpy = e->dpy;
    if (!e->composited) {   /* XP's drop shadow is a copy of what lies under it, darkened */
        int sw = DisplayWidth(dpy, e->screen), sh = DisplayHeight(dpy, e->screen);
        cairo_surface_t *rs = cairo_xlib_surface_create(dpy, e->root, DefaultVisual(dpy, e->screen), sw, sh);
        r.under = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.W, r.H); cairo_t *cr = cairo_create(r.under);
        cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr);
        cairo_set_source_surface(cr, rs, -x, -y); cairo_paint(cr); cairo_destroy(cr); cairo_surface_destroy(rs);
    }
    XSetWindowAttributes a = { .override_redirect = True, .colormap = e->cmap, .border_pixel = 0, .background_pixmap = None, .save_under = True,
                               .event_mask = ExposureMask };
    r.win = XCreateWindow(dpy, e->root, x, y, r.W, r.H, 0, e->depth, InputOutput, e->visual,
                          CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixmap | CWSaveUnder | CWEventMask, &a);
    Atom wt = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False), pm = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    XChangeProperty(dpy, r.win, wt, XA_ATOM, 32, PropModeReplace, (unsigned char *)&pm, 1);
    long one = 1; XChangeProperty(dpy, r.win, XInternAtom(dpy, "_LHWM_MENU", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
    XClassHint ch = { (char *)"lhwm-menu", (char *)"lhwm" }; XSetClassHint(dpy, r.win, &ch);
    XStoreName(dpy, r.win, "lhwm window menu");
    run_paint(&r);   /* before the map: the first frame the compositor sees is the finished menu */
    XMapRaised(dpy, r.win);
    int result = WMMENU_CANCEL;
    bool sub_chosen = false;
    if (!menu_grab(dpy, r.win, e->root, parent == NULL)) goto out;
    double t0 = ms_now(), sub_at = 0; bool down = button != 0, mouse_sel = false; int px0 = -1, py0 = -1;
    { Window rw, cw; int wx, wy; unsigned m; XQueryPointer(dpy, e->root, &rw, &cw, &px0, &py0, &wx, &wy, &m); }
    /* a submenu's answer: chosen (this menu is done too), dismissed altogether, or closed (this menu goes on) */
    #define SUB_RESULT(res) do { int sr_ = (res); \
        if (sr_ >= 0) { result = r.sel; sub_chosen = true; done = true; } \
        else if (sr_ == SUB_ALL) { result = parent ? SUB_ALL : WMMENU_CANCEL; done = true; } \
        else run_paint(&r); } while (0)
    for (bool done = false; !done; ) {
        XEvent ev;
        int wait = sub_at > 0 ? (int)(sub_at - ms_now() + 0.999) : -1;
        if (sub_at > 0 && wait < 0) wait = 0;
        if (!menu_next(dpy, &ev, wait)) {   /* hovered long enough over a submenu item */
            sub_at = 0;
            if (r.sel >= 0 && items[r.sel].sub) SUB_RESULT(open_sub(&r, r.sel, false));
            continue;
        }
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.window == r.win) { if (ev.xexpose.count == 0) run_paint(&r); }
            else if (e->dispatch) e->dispatch(&ev, e->dispatch_arg);
            break;
        case MapRequest: case ConfigureRequest:
            if (e->dispatch) e->dispatch(&ev, e->dispatch_arg);
            break;
        case MotionNotify: {
            while (XCheckTypedEvent(dpy, MotionNotify, &ev)) ;
            int i = item_at(&r, ev.xmotion.x_root, ev.xmotion.y_root);
            if (parent && !inside(&r, ev.xmotion.x_root, ev.xmotion.y_root) && inside(parent, ev.xmotion.x_root, ev.xmotion.y_root)
                && item_at(parent, ev.xmotion.x_root, ev.xmotion.y_root) != parent->sel) {
                XPutBackEvent(dpy, &ev); result = SUB_BACK; done = true; break;   /* back over another item of the parent */
            }
            if (i >= 0 || mouse_sel || inside(&r, ev.xmotion.x_root, ev.xmotion.y_root)) {
                if (i != r.sel) { r.sel = i; run_paint(&r); sub_at = i >= 0 && items[i].sub ? ms_now() + MENU_SHOW_DELAY : 0; }
                mouse_sel = i >= 0;
            }
            break;
        }
        case ButtonPress: {
            int rx = ev.xbutton.x_root, ry = ev.xbutton.y_root;
            if (!inside(&r, rx, ry)) {
                if (parent) {   /* in the parent: it handles the press; elsewhere the whole menu goes */
                    if (inside(parent, rx, ry)) { XPutBackEvent(dpy, &ev); result = SUB_BACK; } else result = SUB_ALL;
                    done = true; break;
                }
                if (e->dbl_w > 0 && rx >= e->dbl_x && rx < e->dbl_x + e->dbl_w && ry >= e->dbl_y && ry < e->dbl_y + e->dbl_h && ms_now() - t0 < e->dbl_ms)
                    result = WMMENU_DBLCLICK;
                done = true;   /* a click outside dismisses the menu (and is not passed on, as in XP) */
                break;
            }
            down = true; int i = item_at(&r, rx, ry);
            if (i != r.sel) { r.sel = i; run_paint(&r); }
            if (i >= 0 && items[i].sub) { down = false; sub_at = 0; SUB_RESULT(open_sub(&r, i, false)); }
            break;
        }
        case ButtonRelease: {
            int rx = ev.xbutton.x_root, ry = ev.xbutton.y_root, i = item_at(&r, rx, ry);
            if (!down) break;
            down = false;
            if (i >= 0 && items[i].sub) { sub_at = 0; SUB_RESULT(open_sub(&r, i, false)); }   /* the release of the button that opened the menu */
            else if (i >= 0 && !(items[i].flags & WMMENU_DISABLED)) { result = i; done = true; }
            /* the release of the press that opened the menu, where it was pressed or off the items: the menu stays */
            (void)px0; (void)py0;
            break;
        }
        case KeyPress: {
            KeySym k = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            r.cues = 1;   /* a key shows the keyboard cues from now on (XP) */
            if (k == XK_Alt_L || k == XK_Alt_R || k == XK_F10 || k == XK_Meta_L || k == XK_Meta_R) { result = parent ? SUB_ALL : WMMENU_CANCEL; done = true; break; }
            if (k == XK_Escape || (parent && (k == XK_Left || k == XK_KP_Left))) { done = true; break; }   /* a submenu: only it closes */
            if ((k == XK_Right || k == XK_KP_Right) && r.sel >= 0 && items[r.sel].sub) { sub_at = 0; SUB_RESULT(open_sub(&r, r.sel, true)); break; }
            if (k == XK_Down || k == XK_KP_Down || k == XK_Tab) r.sel = next_item(&r, r.sel < 0 ? -1 : r.sel, 1);
            else if (k == XK_Up || k == XK_KP_Up) r.sel = next_item(&r, r.sel < 0 ? 0 : r.sel, -1);
            else if (k == XK_Home || k == XK_KP_Home) r.sel = next_item(&r, -1, 1);
            else if (k == XK_End || k == XK_KP_End) r.sel = next_item(&r, 0, -1);
            else if (k == XK_Return || k == XK_KP_Enter || k == XK_space) {
                if (r.sel >= 0 && items[r.sel].sub) { SUB_RESULT(open_sub(&r, r.sel, true)); break; }
                if (r.sel >= 0 && !(items[r.sel].flags & WMMENU_DISABLED)) { result = r.sel; done = true; break; }
            } else {
                /* a mnemonic: the next item with it after the selection; the only one with it is chosen at once (greyed: selected) */
                char buf[256]; int mn, first = -1, count = 0, after = -1;
                gunichar kc = k < 0x100 ? (gunichar)k : (k & 0xff000000) == 0x01000000 ? (gunichar)(k & 0xffffff) : 0;   /* Latin-1 / Unicode keysyms */
                for (int i = 0; i < n; i++) {
                    if (items[i].flags & WMMENU_SEPARATOR) continue;
                    strip_amp(items[i].text, buf, sizeof buf, &mn);
                    if (mn < 0 || !kc || g_unichar_tolower(g_utf8_get_char(buf + mn)) != g_unichar_tolower(kc))
                        continue;
                    count++;
                    if (first < 0) first = i;
                    if (after < 0 && i > r.sel) after = i;
                }
                if (count == 1) {
                    r.sel = first;
                    if (items[first].sub) { SUB_RESULT(open_sub(&r, first, true)); break; }
                    if (!(items[first].flags & WMMENU_DISABLED)) { result = first; done = true; break; }
                } else if (count > 1) r.sel = after >= 0 ? after : first;
            }
            mouse_sel = false;
            run_paint(&r);
            break;
        }
        }
    }
    #undef SUB_RESULT
out:
    if (!parent) { XUngrabPointer(dpy, CurrentTime); XUngrabKeyboard(dpy, CurrentTime); }   /* a submenu leaves the grabs to its parent */
    XDestroyWindow(dpy, r.win); XFlush(dpy);
    if (r.under) cairo_surface_destroy(r.under);
    if (result >= 0 && !sub_chosen && items[result].cb) items[result].cb(items[result].arg, items[result].id);   /* a submenu's choice: its cb ran */
    return result;
}
