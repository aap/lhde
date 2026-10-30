/* extendframe: the reference client for lhwm's extended glass frames (lhwm/CLIENT-PROTOCOL.md).

   A Longhorn-Explorer-style window: a 60 px header on the window manager's glass (big folder title, icon placeholder, fake
   breadcrumb) above an opaque white body. Read it as the spec for porting to another toolkit; every X call that the protocol
   needs is here and commented. The rules in short:

     1. 32-bit ARGB toplevel (so the header can be transparent);
     2. _LHWM_EXTEND_FRAME = 0 0 HEADER 0 and _LHWM_FRAME_FLAGS = NO_TITLE before mapping (lhwm paints its plate under the header);
     3. paint the header fully transparent and draw only text/icons on it; paint the body opaque;
     4. while root _LHWM_COMPOSITED is 0 (no compositor, classic theme) paint the header opaque instead: copy lhwm's
        _LHWM_HEADER_PIXMAP (the theme's caption band, continued down over the header) under the header content, or, when
        there is none (another WM), a gradient of our own; keep the extension set, it is what asks lhwm for the pixmap;
     5. caption behaviour in the header comes from _NET_WM_MOVERESIZE (drag) and _NET_WM_STATE (double-click maximise);
     6. a PropertyNotify on the root for _LH_THEME, _LH_THEME_RESOLVED, _LHWM_COMPOSITED or _LHWM_FRAME_METRICS is
        WM_THEMECHANGED: take the session theme's record (_LH_THEME_RESOLVED when it is current, else liblhde resolves
        _LH_THEME itself; docs/THEME-FORMAT.md section 4; a uxtheme client loads its path.style), re-read _LHWM_COMPOSITED
        and repaint (glass or the header pixmap); the extension stays set.

   -c (experimental, CLIENT-PROTOCOL.md "CLIENT_CAPTION"): the window also takes the caption rows. lhwm grows it up to the frame's
   top border and publishes _LHWM_CAPTION_RECT (the caption buttons, client coordinates); we leave that rectangle empty and cut it
   out of our input shape, so clicks there reach lhwm's buttons on the decoration window below, and draw the title ourselves.
   The caption rows are caption_h - border_t of _LHWM_FRAME_METRICS, or max_caption_h - max_border_t while maximised
   (_NET_WM_STATE); lhwm republishes _LHWM_CAPTION_RECT on every maximise/restore, so we relayout on it (and on _NET_WM_STATE
   and theme changes). The top TOP_EDGE rows outside the caption rect start a resize from the top (_NET_WM_MOVERESIZE direction 1):
   above us the frame keeps only its border rows, which resize too.

   -n: _LHWM_FRAME_FLAGS NO_HEADER_PIXMAP (16): without composition lhwm publishes no _LHWM_HEADER_PIXMAP and we draw the header
   gradient ourselves (flxp draws its own navbar for Slate/Jade). The key n toggles the flag live (lhwm frees the pixmap and
   deletes the property when it is set, renders one again when it is cleared). With -c the caption buttons were part of that
   pixmap; without it they are only on lhwm's decoration window, so we cut _LHWM_CAPTION_RECT out of our bounding shape as well
   (not only the input shape) and they show through.

   Build: make (Xlib + Xext + cairo + pangocairo, liblhde). Run: DISPLAY=:3 ./extendframe [-c] [-n]. Escape or q quits. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>
#include "lhde/theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 60          /* header height in client pixels: the part of the window that is glass (-c: below the caption rows) */
#define EDGE   6           /* right-edge strip that resizes the window */
#define TOP_EDGE 4         /* -c: top rows that resize the window from the top (_NET_WM_MOVERESIZE direction 1) */
#define NO_TITLE 1         /* _LHWM_FRAME_FLAGS bits */
#define NO_ICON  2
#define NO_FOG   4
#define CLIENT_CAPTION 8   /* experimental: the client covers the caption rows too, lhwm keeps only the buttons */
#define NO_HEADER_PIXMAP 16 /* no _LHWM_HEADER_PIXMAP: we draw the opaque header ourselves */

static Display *dpy;
static Window root, win;
static Atom a_composited, a_theme, a_moveresize, a_state, a_max_v, a_max_h, a_delete;
static Atom a_lh_theme, a_lh_resolved, a_lh_serial, a_metrics, a_extend, a_flags, a_header_pixmap, a_caption_rect;
static int client_caption;     /* -c */
static int no_header_pixmap;   /* -n, toggled with the key n */
static int maximized;          /* _NET_WM_STATE has MAXIMIZED_VERT and MAXIMIZED_HORZ */
static int bounding_hole;      /* -c with NO_HEADER_PIXMAP and no composition: lhwm's buttons are only on its decoration window, so
                                  the caption rect is cut out of our bounding shape too and they show through */
static long cap_rect[4];       /* _LHWM_CAPTION_RECT x y w h (client coordinates), all 0 while there is none */
static int top_rows;           /* header rows in client pixels: HEADER, plus the caption zone with -c */
static int x_error;            /* set by the error handler (a header pixmap freed under our feet) */
static char theme_name[64], theme_msstyles[1024];   /* _LH_THEME and its path.style (what a uxtheme client would load) */

/* root CARDINAL _LHWM_COMPOSITED: 1 = lhwm draws glass and honours the extension. Missing (another WM, or lhwm not running) = 0. */
static int read_composited(void) {
    Atom type; int format; unsigned long n, after; unsigned char *data = NULL; int v = 0;
    if (XGetWindowProperty(dpy, root, a_composited, 0, 1, False, XA_CARDINAL, &type, &format, &n, &after, &data) == Success && data) {
        if (format == 32 && n == 1) v = (int)((unsigned long *)data)[0];
        XFree(data);
    }
    return v;
}

/* root CARDINAL[14] _LHWM_FRAME_METRICS: caption_h border_l border_r border_b text_rgb glow_rgb inactive_text_rgb 0, then
   max_caption_h max_border_l max_border_r max_border_b border_t max_border_t (lhwm before 2026-10-24: only the first 8; dflt then) */
static unsigned long read_metric(int i, unsigned long dflt) {
    Atom type; int format; unsigned long n, after; unsigned char *data = NULL; unsigned long v = dflt;
    if (XGetWindowProperty(dpy, root, a_metrics, 0, 32, False, XA_CARDINAL, &type, &format, &n, &after, &data) == Success && data) {
        if (format == 32 && (unsigned long)i < n) v = ((unsigned long *)data)[i];
        XFree(data);
    }
    return v;
}

/* a root string property (_LH_THEME: UTF8_STRING) into buf */
static void read_root_string(Atom a, char *buf, size_t n) {
    Atom type; int format; unsigned long cnt, after; unsigned char *data = NULL;
    buf[0] = 0;
    if (XGetWindowProperty(dpy, root, a, 0, 256, False, AnyPropertyType, &type, &format, &cnt, &after, &data) == Success && data) {
        if (format == 8) snprintf(buf, n, "%.*s", (int)cnt, (char *)data);
        XFree(data);
    }
}

/* the session theme (docs/THEME-FORMAT.md section 4): _LH_THEME names it; _LH_THEME_RESOLVED is its resolved record, current
   when its theme= is that name and its serial= is the root's _LH_SETTINGS_SERIAL; else liblhde resolves the name itself. We only
   want path.style (the .msstyles, "" for classic); a real client takes the style with VisualStyle.color / .size and reloads it
   (flxp: shell/lhtheme.cpp). */
static void read_theme(void) {
    Atom type; int format; unsigned long cnt, after; unsigned char *data = NULL;
    char *rec = NULL; unsigned long serial = 0, rs = 0;
    read_root_string(a_lh_theme, theme_name, sizeof theme_name);
    theme_msstyles[0] = 0;
    if (!theme_name[0]) return;
    if (XGetWindowProperty(dpy, root, a_lh_serial, 0, 1, False, XA_CARDINAL, &type, &format, &cnt, &after, &data) == Success && data) {
        if (format == 32 && cnt == 1) serial = (unsigned long)*(long *)data;
        XFree(data); data = NULL;
    }
    if (XGetWindowProperty(dpy, root, a_lh_resolved, 0, 1 << 20, False, AnyPropertyType, &type, &format, &cnt, &after, &data) == Success && data) {
        if (format == 8 && (rec = malloc(cnt + 1))) { memcpy(rec, data, cnt); rec[cnt] = 0; }
        XFree(data);
    }
    lhde_theme *t = NULL;
    size_t nl = strlen(theme_name);
    if (rec && !strncmp(rec, "theme=", 6) && !strncmp(rec + 6, theme_name, nl) && rec[6 + nl] == '\n') {
        t = lhde_theme_parse(rec, &rs);
        if (t && rs != serial) { lhde_theme_free(t); t = NULL; }   /* stale: resolve ourselves */
    }
    free(rec);
    if (!t) t = lhde_theme_resolve(theme_name, NULL, 0);
    const char *p = t ? lhde_theme_get(t, "path.style") : NULL;
    snprintf(theme_msstyles, sizeof theme_msstyles, "%s", p ? p : "");
    lhde_theme_free(t);
}

/* rule 2: the extension and the flags. Kept in both modes: composited, lhwm paints glass under the header; not composited it
   paints the frame as usual (no title: NO_TITLE is honoured in every mode, our header carries the title) and hands us the header
   background as _LHWM_HEADER_PIXMAP. -c: the caption rows are ours too (CLIENT_CAPTION), so the header grows by them. */
static void set_extension(void) {
    int zone = 0;   /* -c: the caption rows lhwm grew us by: caption_h - border_t (maximised: max_caption_h - max_border_t) */
    if (client_caption) {
        long ch = (long)read_metric(maximized ? 8 : 0, 0), bt = (long)read_metric(maximized ? 13 : 12, 9999);
        if (ch > 0 && bt != 9999 && ch > bt) zone = (int)(ch - bt);
        else if (cap_rect[3]) { zone = (int)(cap_rect[1] + cap_rect[3]) + 2; if (zone < 18) zone = 18; }   /* older lhwm: below the buttons */
    }
    top_rows = zone + HEADER;
    long extend[4] = { 0, 0, top_rows, 0 },
         flags = NO_TITLE | (client_caption ? CLIENT_CAPTION | NO_ICON : 0) | (no_header_pixmap ? NO_HEADER_PIXMAP : 0);
    XChangeProperty(dpy, win, a_extend, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)extend, 4);
    XChangeProperty(dpy, win, a_flags, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&flags, 1);
}

/* -c: _LHWM_CAPTION_RECT, and the input shape without it (clicks there go through to lhwm's caption buttons) */
static void read_caption_rect(int w, int h) {
    Atom type; int format; unsigned long n, after; unsigned char *data = NULL;
    memset(cap_rect, 0, sizeof cap_rect);
    if (XGetWindowProperty(dpy, win, a_caption_rect, 0, 4, False, XA_CARDINAL, &type, &format, &n, &after, &data) == Success && data) {
        if (format == 32 && n == 4) for (int i = 0; i < 4; i++) cap_rect[i] = ((long *)data)[i];
        XFree(data);
    }
    XRectangle all = { 0, 0, (unsigned short)w, (unsigned short)h };
    XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, &all, 1, ShapeSet, Unsorted);
    XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0, &all, 1, ShapeSet, Unsorted);
    if (cap_rect[2] > 0 && cap_rect[3] > 0) {
        XRectangle hole = { (short)cap_rect[0], (short)cap_rect[1], (unsigned short)cap_rect[2], (unsigned short)cap_rect[3] };
        XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, &hole, 1, ShapeSubtract, Unsorted);
        if (bounding_hole) XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0, &hole, 1, ShapeSubtract, Unsorted);
    }
}

/* _NET_WM_STATE: maximised (both MAXIMIZED atoms) */
static int read_maximized(void) {
    Atom type; int format; unsigned long n, after; unsigned char *data = NULL; int v = 0, h = 0;
    if (XGetWindowProperty(dpy, win, a_state, 0, 64, False, XA_ATOM, &type, &format, &n, &after, &data) == Success && data) {
        for (unsigned long i = 0; i < n; i++) { Atom a = ((Atom *)data)[i]; v |= a == a_max_v; h |= a == a_max_h; }
        XFree(data);
    }
    return v && h;
}

/* lhwm replaces _LHWM_HEADER_PIXMAP with a new pixmap and frees the old one: a copy from an id we read a moment earlier can fail
   with BadDrawable/BadPixmap. Ignore those; the PropertyNotify for the new pixmap repaints. */
static int on_x_error(Display *d, XErrorEvent *e) { (void)d; (void)e; x_error = 1; return 0; }

/* the header background without composition: the bottom top_rows rows of lhwm's pixmap (its last row lies against the bottom
   of our header), or 0 when there is none */
static int paint_header_pixmap(cairo_t *cr, Visual *vis, int w) {
    Atom type; int format; unsigned long n, after; unsigned char *data = NULL; Pixmap pix = None;
    if (XGetWindowProperty(dpy, win, a_header_pixmap, 0, 1, False, XA_PIXMAP, &type, &format, &n, &after, &data) == Success && data) {
        if (format == 32 && n == 1) pix = (Pixmap)((unsigned long *)data)[0];
        XFree(data);
    }
    if (pix == None) return 0;
    Window r; int px, py; unsigned pw, ph, bw, pd;
    XSync(dpy, False); x_error = 0;
    XErrorHandler old = XSetErrorHandler(on_x_error);
    Status ok = XGetGeometry(dpy, pix, &r, &px, &py, &pw, &ph, &bw, &pd);
    XSync(dpy, False);
    if (!ok || x_error) { XSetErrorHandler(old); return 0; }
    cairo_surface_t *ps = cairo_xlib_surface_create(dpy, pix, vis, (int)pw, (int)ph);
    cairo_save(cr); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_rectangle(cr, 0, 0, w < (int)pw ? w : (int)pw, top_rows); cairo_clip(cr);
    cairo_set_source_surface(cr, ps, 0, top_rows - (int)ph); cairo_paint(cr); cairo_restore(cr);
    cairo_surface_flush(cairo_get_target(cr)); cairo_surface_destroy(ps);
    XSync(dpy, False); XSetErrorHandler(old);
    return !x_error;
}

/* EWMH client messages go to the root window with SubstructureRedirect|SubstructureNotify */
static void send_root_message(Atom type, long l0, long l1, long l2, long l3, long l4) {
    XEvent e; memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage; e.xclient.window = win; e.xclient.message_type = type; e.xclient.format = 32;
    e.xclient.data.l[0] = l0; e.xclient.data.l[1] = l1; e.xclient.data.l[2] = l2; e.xclient.data.l[3] = l3; e.xclient.data.l[4] = l4;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(dpy);
}

/* hand a button press over to the WM: it runs its own move/resize drag (with snapping/maximise rules) until the button is
   released. Direction: 0..7 resize (top-left, top, top-right, right, bottom-right, bottom, bottom-left, left), 8 move.
   The press gave us an implicit pointer grab; release it first or the WM cannot grab the pointer. */
static void start_moveresize(XButtonEvent *b, int direction) {
    XUngrabPointer(dpy, b->time);
    send_root_message(a_moveresize, b->x_root, b->y_root, direction, b->button, 1 /* source: application */);
}

/* text with a soft white halo, like Longhorn's TextGlow: readable on any glass */
static void glow_text(cairo_t *cr, PangoLayout *lay, double x, double y, int glow) {
    if (glow)
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            if (dx * dx + dy * dy > 5) continue;
            cairo_set_source_rgba(cr, 1, 1, 1, 0.20); cairo_move_to(cr, x + dx, y + dy); pango_cairo_show_layout(cr, lay);
        }
    cairo_set_source_rgb(cr, 0.10, 0.12, 0.30); cairo_move_to(cr, x, y); pango_cairo_show_layout(cr, lay);
}

static void text_at(cairo_t *cr, const char *font, const char *s, double x, double y, int glow) {
    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(font);
    pango_layout_set_font_description(lay, fd); pango_layout_set_text(lay, s, -1);
    glow_text(cr, lay, x, y, glow);
    pango_font_description_free(fd); g_object_unref(lay);
}

static void paint(cairo_surface_t *xs, Visual *vis, int w, int h, int composited) {
    cairo_t *cr = cairo_create(xs);
    int zone = top_rows - HEADER;   /* -c: the caption rows at the top of the window */
    /* 1. everything transparent first (premultiplied 0,0,0,0): where we draw nothing, lhwm's plate shows through */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    /* 2. the header. Composited: leave it transparent (the glass is lhwm's). Not composited: the X server would show the
          transparent pixels as black, so paint it opaque: lhwm's header pixmap (the theme's caption band, so the header
          continues the frame like the 4074 Slate Explorer's), else a gradient of our own */
    if (!composited && (no_header_pixmap || !paint_header_pixmap(cr, vis, w))) {
        cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, top_rows);
        cairo_pattern_add_color_stop_rgb(g, 0, 0.93, 0.95, 0.98); cairo_pattern_add_color_stop_rgb(g, 1, 0.78, 0.84, 0.92);
        cairo_set_source(cr, g); cairo_rectangle(cr, 0, 0, w, top_rows); cairo_fill(cr); cairo_pattern_destroy(g);
    }
    /* -c: the window title in the caption row, left of lhwm's buttons (whose rectangle we leave empty); without glass in the
       theme's caption text colour (_LHWM_FRAME_METRICS text_rgb) */
    if (zone > 0) {
        PangoLayout *lay = pango_cairo_create_layout(cr); PangoFontDescription *fd = pango_font_description_from_string("Tahoma Bold 8");
        pango_layout_set_font_description(lay, fd); pango_layout_set_text(lay, "Documents (client-drawn caption)", -1);
        if (composited) glow_text(cr, lay, 8, (zone - 14) / 2, 1);
        else {
            unsigned long rgb = read_metric(4, 0xffffff);
            cairo_set_source_rgb(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
            cairo_move_to(cr, 8, (zone - 14) / 2); pango_cairo_show_layout(cr, lay);
        }
        pango_font_description_free(fd); g_object_unref(lay);
    }
    cairo_translate(cr, 0, zone);
    /* icon placeholder: a folder-ish rounded square (a real client draws its icon here, OVER the glass) */
    cairo_new_sub_path(cr); cairo_arc(cr, 14 + 34, 10 + 6, 6, -1.5708, 0); cairo_arc(cr, 14 + 34, 10 + 34, 6, 0, 1.5708);
    cairo_arc(cr, 14 + 6, 10 + 34, 6, 1.5708, 3.1416); cairo_arc(cr, 14 + 6, 10 + 6, 6, 3.1416, 4.7124); cairo_close_path(cr);
    cairo_pattern_t *ig = cairo_pattern_create_linear(0, 10, 0, 50);
    cairo_pattern_add_color_stop_rgba(ig, 0, 1.0, 0.92, 0.55, 0.95); cairo_pattern_add_color_stop_rgba(ig, 1, 0.85, 0.62, 0.15, 0.95);
    cairo_set_source(cr, ig); cairo_fill_preserve(cr); cairo_pattern_destroy(ig);
    cairo_set_source_rgba(cr, 0.45, 0.30, 0.05, 0.8); cairo_set_line_width(cr, 1); cairo_stroke(cr);
    /* big title and the breadcrumb, with glow when on glass */
    text_at(cr, "Segoe UI, Tahoma, Sans 15", "Documents", 64, 4, composited);
    text_at(cr, "Segoe UI, Tahoma, Sans 9", "Computer", 78, 37, composited);
    text_at(cr, "Segoe UI, Tahoma, Sans 9", "▸", 146, 37, composited);
    text_at(cr, "Segoe UI, Tahoma, Sans 9", "Documents", 166, 37, composited);
    /* 3. the body: ordinary opaque content */
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_rectangle(cr, 0, HEADER, w, h - top_rows); cairo_fill(cr);
    const char *lines[] = { "The header above is lhwm's frame plate, extended 60 px into this window.",
                            "Drag the header to move, double-click it to maximise or restore,",
                            client_caption ? "drag the right or the top edge to resize. Escape or q quits." : "drag the right edge to resize. Escape or q quits.",
                            composited ? "Composited: the header is glass (_LHWM_COMPOSITED = 1)."
                            : no_header_pixmap ? "Not composited, NO_HEADER_PIXMAP: our own header gradient (n toggles)."
                                               : "Not composited: the header is lhwm's _LHWM_HEADER_PIXMAP (n: NO_HEADER_PIXMAP)." };
    for (int i = 0; i < 4; i++) text_at(cr, "Segoe UI, Tahoma, Sans 9", lines[i], 16, HEADER + 14 + i * 20, 0);
    char th[1200]; const char *ms = strrchr(theme_msstyles, '/');
    snprintf(th, sizeof th, "Theme (_LH_THEME): %s%s%s%s", theme_name[0] ? theme_name : "(none)", ms ? ", msstyles " : "", ms ? ms + 1 : "",
             theme_name[0] && !ms ? ", no visual style (classic, or no such theme)" : "");
    text_at(cr, "Segoe UI, Tahoma, Sans 9", th, 16, HEADER + 14 + 4 * 20, 0);
    cairo_destroy(cr);
    cairo_surface_flush(xs); XFlush(dpy);
}

/* -c: the top resize zone inside our window (the frame above us is only its border): TOP_EDGE rows, not in the caption buttons'
   hole, not while maximised (lhwm ignores resizes then) */
static int in_top_edge(int x, int y) {
    if (!client_caption || maximized || y >= TOP_EDGE) return 0;
    return !(cap_rect[2] > 0 && x >= cap_rect[0] && x < cap_rect[0] + cap_rect[2] && y >= cap_rect[1] && y < cap_rect[1] + cap_rect[3]);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c")) client_caption = 1;
        else if (!strcmp(argv[i], "-n")) no_header_pixmap = 1;
        else { fprintf(stderr, "usage: extendframe [-c] [-n]\n"); return 2; }
    }
    if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "extendframe: cannot open display\n"); return 1; }
    int scr = DefaultScreen(dpy); root = RootWindow(dpy, scr);
    a_composited = XInternAtom(dpy, "_LHWM_COMPOSITED", False); a_theme = XInternAtom(dpy, "_LHWM_THEME", False);
    a_moveresize = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False); a_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    a_max_v = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False); a_max_h = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    a_lh_theme = XInternAtom(dpy, "_LH_THEME", False); a_lh_resolved = XInternAtom(dpy, "_LH_THEME_RESOLVED", False);
    a_lh_serial = XInternAtom(dpy, "_LH_SETTINGS_SERIAL", False);
    a_metrics = XInternAtom(dpy, "_LHWM_FRAME_METRICS", False);
    a_extend = XInternAtom(dpy, "_LHWM_EXTEND_FRAME", False); a_flags = XInternAtom(dpy, "_LHWM_FRAME_FLAGS", False);
    a_header_pixmap = XInternAtom(dpy, "_LHWM_HEADER_PIXMAP", False); a_caption_rect = XInternAtom(dpy, "_LHWM_CAPTION_RECT", False);

    /* rule 1: a 32-bit TrueColor visual with its own colormap; border and background pixel 0 (a background of None would
       leave garbage until the first paint). Without one there is no transparency at all: behave as not composited. */
    XVisualInfo vi; int argb = XMatchVisualInfo(dpy, scr, 32, TrueColor, &vi);
    Visual *vis = argb ? vi.visual : DefaultVisual(dpy, scr); int depth = argb ? 32 : DefaultDepth(dpy, scr);
    XSetWindowAttributes a; memset(&a, 0, sizeof a);
    a.colormap = XCreateColormap(dpy, root, vis, AllocNone); a.border_pixel = 0; a.background_pixel = 0;
    a.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | PointerMotionMask | KeyPressMask | PropertyChangeMask;
    int w = 640, h = 420;
    win = XCreateWindow(dpy, root, 120, 90, w, h, 0, depth, InputOutput, vis, CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &a);

    /* ordinary hints: the title still matters (taskbar, Alt+Tab) even though lhwm does not draw it in the caption */
    XStoreName(dpy, win, "Documents");
    XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_NAME", False), XInternAtom(dpy, "UTF8_STRING", False), 8,
                    PropModeReplace, (unsigned char *)"Documents", 9);
    XClassHint ch = { "extendframe", "ExtendFrame" }; XSetClassHint(dpy, win, &ch);
    XSetWMProtocols(dpy, win, &a_delete, 1);

    /* rule 4/6: follow composition and theme changes via PropertyNotify on the root (selected before reading, so no change
       between the read and the selection is missed) */
    XSelectInput(dpy, root, PropertyChangeMask);
    int composited = argb && read_composited();
    bounding_hole = no_header_pixmap && !composited;
    read_theme();

    /* rule 2: where the frame extends into us (left right top bottom, CARDINAL[4]) and what lhwm should leave out: _LHWM_EXTEND_FRAME
       = 0 0 HEADER 0, _LHWM_FRAME_FLAGS = NO_TITLE (-c: | CLIENT_CAPTION | NO_ICON); set in both modes */
    set_extension();

    Cursor arrow = XCreateFontCursor(dpy, XC_left_ptr), east = XCreateFontCursor(dpy, XC_right_side), north = XCreateFontCursor(dpy, XC_top_side);
    int on_edge = 0;
    cairo_surface_t *xs = cairo_xlib_surface_create(dpy, win, vis, w, h);
    XMapWindow(dpy, win);
    Time last_press = 0;
    for (;;) {
        XEvent e; XNextEvent(dpy, &e);
        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0) paint(xs, vis, w, h, composited);
            break;
        case ConfigureNotify:
            if (e.xconfigure.width != w || e.xconfigure.height != h) {
                w = e.xconfigure.width; h = e.xconfigure.height; cairo_xlib_surface_set_size(xs, w, h);
                if (client_caption) read_caption_rect(w, h);   /* the input shape follows the size */
                paint(xs, vis, w, h, composited); }
            break;
        case PropertyNotify:   /* rule 6: WM_THEMECHANGED */
            if (e.xproperty.window == root && (e.xproperty.atom == a_composited || e.xproperty.atom == a_theme || e.xproperty.atom == a_lh_theme
                                               || e.xproperty.atom == a_lh_resolved || e.xproperty.atom == a_metrics)) {
                int now = argb && read_composited();
                if (e.xproperty.atom == a_lh_theme || e.xproperty.atom == a_lh_resolved) read_theme();   /* a uxtheme client reloads theme_msstyles here */
                if (client_caption && e.xproperty.atom == a_metrics) set_extension();   /* the caption rows follow the theme */
                composited = now;
                if (client_caption && bounding_hole != (no_header_pixmap && !composited)) { bounding_hole = !bounding_hole; read_caption_rect(w, h); }
                paint(xs, vis, w, h, composited);
            } else if (e.xproperty.window == win && e.xproperty.atom == a_header_pixmap && !composited) {
                paint(xs, vis, w, h, composited);   /* a new header background: like an Expose of the header */
            } else if (e.xproperty.window == win && (e.xproperty.atom == a_caption_rect || e.xproperty.atom == a_state) && client_caption) {
                /* republished on every maximise/restore (after _NET_WM_STATE): relayout the header for the caption rows */
                long was = top_rows;
                maximized = read_maximized(); read_caption_rect(w, h); set_extension();
                if (top_rows != was) paint(xs, vis, w, h, composited);
            }
            break;
        case MotionNotify: {   /* resize cursors over the right edge and (-c) the top edge */
            int edge = e.xmotion.x >= w - EDGE ? 1 : in_top_edge(e.xmotion.x, e.xmotion.y) ? 2 : 0;
            if (edge != on_edge) { on_edge = edge; XDefineCursor(dpy, win, edge == 1 ? east : edge == 2 ? north : arrow); }
            break; }
        case ButtonPress: {
            XButtonEvent *b = &e.xbutton;
            if (b->button != 1) break;
            if (b->x >= w - EDGE) { start_moveresize(b, 3 /* _NET_WM_MOVERESIZE_SIZE_RIGHT */); break; }
            if (in_top_edge(b->x, b->y)) { start_moveresize(b, 1 /* _NET_WM_MOVERESIZE_SIZE_TOP */); break; }
            if (b->y >= top_rows) break;            /* the body is ordinary client area */
            /* rule 5: the header acts as a caption. A second press within 400 ms toggles maximised (2 = toggle, both
               MAXIMIZED atoms, source 1); anything else hands the press to the WM as a move (8) */
            if (last_press && b->time - last_press < 400) {
                last_press = 0;
                send_root_message(a_state, 2, (long)a_max_v, (long)a_max_h, 1, 0);
            } else {
                last_press = b->time;
                start_moveresize(b, 8 /* _NET_WM_MOVERESIZE_MOVE */);
            }
            break; }
        case KeyPress: {
            KeySym k = XLookupKeysym(&e.xkey, 0);
            if (k == XK_Escape || k == XK_q) goto out;
            if (k == XK_n) {   /* live opt-out */
                no_header_pixmap = !no_header_pixmap; set_extension();
                bounding_hole = no_header_pixmap && !composited; if (client_caption) read_caption_rect(w, h);
                paint(xs, vis, w, h, composited); }
            break; }
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == a_delete) goto out;
            break;
        }
    }
out:
    cairo_surface_destroy(xs);
    XDestroyWindow(dpy, win); XCloseDisplay(dpy);
    return 0;
}
