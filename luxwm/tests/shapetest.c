/* the msstyles window shape (wm_frame_shape_rects) against synthetic captions: no style file, so it runs everywhere.
   A caption with rounded alpha-0 corners must have every one of them outside the shape, whatever its SIZINGMARGINS top:
   with a top margin the corner rows come 1:1 from the source bitmap, below it (or with none: Slate Athens and many XP styles
   have SIZINGMARGINS top 0, their corners in the stretched middle) from the painted caption. shapetest prints "ok" or what failed */
#include "../frame.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int cut[] = { 4, 2, 1, 1 };   /* leading/trailing alpha-0 pixels of the corner rows */
#define CUTROWS 4

/* a w x h caption: top `blank` rows transparent, then the rounded corners, the rest opaque */
static cairo_surface_t *caption(int w, int h, int blank) {
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(s);
    unsigned char *d = cairo_image_surface_get_data(s); int st = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int k = y - blank, c = k >= 0 && k < CUTROWS ? cut[k] : 0;
        ((uint32_t *)(d + y * st))[x] = y < blank || x < c || x >= w - c ? 0 : 0xff4e5150;
    }
    cairo_surface_mark_dirty(s);
    return s;
}

static int check(const char *name, int src_w, int top_margin, int blank) {
    struct mspart ms[MP_LAST]; memset(ms, 0, sizeof ms);
    struct theme t; memset(&t, 0, sizeof t);
    t.mode = THEME_MSSTYLES; t.shape_alpha = 1; t.ms_border = 1; t.ms_cap_h = 24; t.ms = ms;
    int rows = 3 + t.ms_border + t.ms_cap_h + 1, w = 300, h = 200;
    ms[MP_CAPTION].img[0][0] = caption(src_w, rows, blank);
    ms[MP_CAPTION].sm[2] = top_margin;
    /* the painted frame: the caption over the full width, the rest opaque */
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h), *cap = caption(w, rows, blank);
    cairo_t *cr = cairo_create(img);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3); cairo_rectangle(cr, 0, rows, w, h - rows); cairo_fill(cr);
    cairo_set_source_surface(cr, cap, 0, 0); cairo_paint(cr); cairo_destroy(cr); cairo_surface_destroy(cap);
    XRectangle r[128]; int n = wm_frame_shape_rects(&t, img, 0, r, 128), bad = 0;
    if (n <= 0) { printf("FAIL %s: no shape\n", name); return 1; }
    for (int y = 0; y < h; y++) {
        int l = -1, rr = -1;
        for (int i = 0; i < n; i++) if (y >= r[i].y && y < r[i].y + r[i].height) { l = r[i].x; rr = w - r[i].x - r[i].width; }
        int k = y - blank, want = y < blank ? -1 : k < CUTROWS ? cut[k] : 0;
        if (l != want || rr != want) { printf("FAIL %s: row %d: shape cuts %d / %d, want %d\n", name, y, l, rr, want); bad = 1; }
    }
    cairo_surface_destroy(ms[MP_CAPTION].img[0][0]); cairo_surface_destroy(img);
    return bad;
}

int main(void) {
    int bad = 0;
    bad |= check("top margin covering the corner", 120, 9, 0);
    bad |= check("top margin 0 (Slate Athens)", 385, 0, 0);
    bad |= check("top margin 0, three blank rows", 385, 0, 3);
    bad |= check("corner deeper than the top margin", 120, 2, 0);
    if (!bad) printf("ok    luxwm shapetest: msstyles caption corners outside the window shape\n");
    return bad;
}
