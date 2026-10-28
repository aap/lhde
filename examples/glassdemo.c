/* glassdemo: an ARGB X11 client that paints its own Longhorn frame plate (9-sliced PNG from the theme)
   and leaves the 'extended frame' translucent so a compositor (picom blur-background) shows blurred desktop
   through it -- the X11 analogue of 4074's MILExplorer composited Explorer window.
   build: make (build/examples/glassdemo)   run: build/examples/glassdemo [frame.png]; the default plate, overlay and aurora strip
   come from lhde's data directories (frames/aero-patch, resources/demo; tools/lhde-import) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lhde_data.h"

static void nine(cairo_t *cr, cairo_surface_t *img, double x, double y, double w, double h, double t, double r, double b, double l) {
    double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
    double sx[4] = {0, l, iw - r, iw}, sy[4] = {0, t, ih - b, ih};
    double dx[4] = {x, x + l, x + w - r, x + w}, dy[4] = {y, y + t, y + h - b, y + h};
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) {
        double sw = sx[i+1] - sx[i], sh = sy[j+1] - sy[j], dw = dx[i+1] - dx[i], dh = dy[j+1] - dy[j];
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) continue;
        cairo_save(cr); cairo_rectangle(cr, dx[i], dy[j], dw, dh); cairo_clip(cr);
        cairo_translate(cr, dx[i], dy[j]); cairo_scale(cr, dw / sw, dh / sh); cairo_translate(cr, -sx[i], -sy[j]);
        cairo_set_source_surface(cr, img, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(cr); cairo_restore(cr);
    }
}

int main(int argc, char **argv) {
    /* lhde: the art from the data directories (lhde-import): the aero-patch frame set's plate and edge overlay, the demo's aurora */
    char fb[4200], ob[4200], ab[4200];
    const char *framepng = argc > 1 ? argv[1] : (lhde_data_find("frames/aero-patch/plate_active.png", fb, sizeof fb), fb);
    lhde_data_find("frames/aero-patch/overlay.png", ob, sizeof ob); lhde_data_find("resources/demo/aurora_strip.png", ab, sizeof ab);
    int W = 640, H = 420, caption = 27, navband = 70;   /* 'tall' explorer band: caption + nav bar on glass */
    Display *dpy = XOpenDisplay(NULL); if (!dpy) { fprintf(stderr, "no display\n"); return 1; }
    int scr = DefaultScreen(dpy);
    XVisualInfo vinfo; if (!XMatchVisualInfo(dpy, scr, 32, TrueColor, &vinfo)) { fprintf(stderr, "no ARGB visual\n"); return 1; }
    XSetWindowAttributes a; a.colormap = XCreateColormap(dpy, RootWindow(dpy, scr), vinfo.visual, AllocNone);
    a.border_pixel = 0; a.background_pixel = 0; a.event_mask = ExposureMask | ButtonPressMask | KeyPressMask | StructureNotifyMask;
    Window win = XCreateWindow(dpy, RootWindow(dpy, scr), 700, 250, W, H, 0, vinfo.depth, InputOutput, vinfo.visual,
                               CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &a);
    /* undecorated: we are our own frame (MWM hints) */
    Atom mwm = XInternAtom(dpy, "_MOTIF_WM_HINTS", False); long hints[5] = {2, 0, 0, 0, 0};
    XChangeProperty(dpy, win, mwm, mwm, 32, PropModeReplace, (unsigned char *)hints, 5);
    XStoreName(dpy, win, "glassdemo: Documents");
    XClassHint ch = {"glassdemo", "glassdemo"}; XSetClassHint(dpy, win, &ch);
    XSizeHints sh; sh.flags = USPosition | PPosition; sh.x = 700; sh.y = 250; XSetWMNormalHints(dpy, win, &sh);
    XMapWindow(dpy, win);
    cairo_surface_t *frame = cairo_image_surface_create_from_png(framepng);
    cairo_surface_t *overlay = cairo_image_surface_create_from_png(ob);
    cairo_surface_t *aurora = cairo_image_surface_create_from_png(ab);
    cairo_surface_t *xs = cairo_xlib_surface_create(dpy, win, vinfo.visual, W, H);
    for (;;) {
        XEvent e; XNextEvent(dpy, &e);
        if (e.type == ConfigureNotify) { W = e.xconfigure.width; H = e.xconfigure.height; cairo_xlib_surface_set_size(xs, W, H); }
        if (e.type == KeyPress) break;
        if (e.type != Expose && e.type != ConfigureNotify) continue;
        cairo_t *cr = cairo_create(xs);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        /* 1. the glass plate: LGFRAME texture 9-sliced over the WHOLE window (its client area is translucent too) */
        nine(cr, frame, 0, 0, W, H, 27, 9, 10, 9);
        /* 2. 'legacy content' = the opaque client, but we leave the tall nav band translucent like Explorer with MILExplorer=1 */
        cairo_set_source_rgb(cr, 1, 1, 1); cairo_rectangle(cr, 9, caption + navband, W - 18, H - caption - navband - 10); cairo_fill(cr);
        /* preview pane on aurora at the bottom */
        if (cairo_surface_status(aurora) == CAIRO_STATUS_SUCCESS) {
            cairo_save(cr); cairo_rectangle(cr, 9, H - 10 - 60, W - 18, 60); cairo_clip(cr);
            double aw = cairo_image_surface_get_width(aurora), ah = cairo_image_surface_get_height(aurora);
            cairo_translate(cr, 9, H - 70); cairo_scale(cr, (W - 18) / aw, 60 / ah); cairo_set_source_surface(cr, aurora, 0, 0); cairo_paint(cr); cairo_restore(cr);
        }
        /* 3. nav bar content drawn directly on the glass */
        cairo_set_source_rgba(cr, 1, 1, 1, 0.85); cairo_rectangle(cr, 60, caption + 22, W - 80, 24); cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.2, 0.25, 0.3, 0.9); cairo_set_line_width(cr, 1); cairo_rectangle(cr, 60.5, caption + 22.5, W - 80, 24); cairo_stroke(cr);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13); cairo_set_source_rgb(cr, 0.05, 0.05, 0.05); cairo_move_to(cr, 68, caption + 39); cairo_show_text(cr, "Documents");
        for (int i = 0; i < 2; i++) { cairo_arc(cr, 28 + i * 24, caption + 34, 9, 0, 6.2832); cairo_set_source_rgba(cr, 1, 1, 1, 0.8); cairo_fill_preserve(cr); cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.9); cairo_stroke(cr); }
        /* caption text with TextGlow */
        cairo_set_font_size(cr, 12);
        for (int d = 3; d >= 1; d--) { cairo_set_source_rgba(cr, 1, 1, 1, 0.35); for (int k = 0; k < 8; k++) { cairo_move_to(cr, 30 + d * 0.7 * ((k & 1) ? 1 : -1), 18 + d * 0.7 * ((k & 2) ? 1 : -1)); cairo_show_text(cr, "Documents"); } }
        cairo_set_source_rgb(cr, 0, 0, 0); cairo_move_to(cr, 30, 18); cairo_show_text(cr, "Documents");
        /* 4. edge overlay (white+alpha), 5px outside in the DWM; here inset to stay inside our window */
        if (cairo_surface_status(overlay) == CAIRO_STATUS_SUCCESS) nine(cr, overlay, 0, 0, W, H, 30, 30, 30, 30);
        cairo_destroy(cr); cairo_surface_flush(xs); XFlush(dpy);
    }
    return 0;
}
