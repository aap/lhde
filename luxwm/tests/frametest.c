/* offline render of the frame painter.
   frametest THEMEDIR out.png                         -> 4 frames: focused, hover close, pressed max, inactive (360x90 each)
   frametest THEMEDIR out.png W H TITLE FOCUSED [MAXIMIZED [HOVER [ICON.png]]]  -> one W x H frame (HOVER: close|max|min)
   (env FRAMETEST_EXT="l r t b [flags]": extended frame; FRAMETEST_FADE=F: maximised-plate crossfade weight 0..1;
    FRAMETEST_FONTS="DIR[;DIR]": font directories as the theme's path.fonts;
    FRAMETEST_SOURCE=dir|lib: the theme directory's PNG cache, or the visual style through libuxtheme (default: the library when luxwm
    has it and a style is named). THEMEDIR may be a theme spec, "DIR|STYLE.msstyles|COLOR|SIZE" or "|STYLE.msstyles|COLOR|SIZE")
   also prints the frame margins, hit-test probes and the window shape the painter's alpha produces */
#include "../frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: frametest THEMEDIR out.png [W H TITLE FOCUSED [MAXIMIZED [HOVER [ICON]]]]\n"); return 2; }
    /* FRAMETEST_FONTS="DIR[;DIR]": the theme's path.fonts, registered as luxwm does before the theme loads */
    if (getenv("FRAMETEST_FONTS")) frame_fonts_set(getenv("FRAMETEST_FONTS"));
    const char *src = getenv("FRAMETEST_SOURCE");
    if (src && !strcmp(src, "dir")) frame_theme_source = FRAME_SRC_DIR;
    else if (src && !strcmp(src, "lib")) frame_theme_source = FRAME_SRC_LIB;
    else if (src && *src) { fprintf(stderr, "FRAMETEST_SOURCE: dir or lib\n"); return 2; }
    struct theme t; if (!theme_load(&t, argv[1])) return 1;
    if (getenv("LHWM_DEBUG")) fprintf(stderr, "frametest: %s: %s\n", argv[1], t.ut ? t.ut_src : "PNG cache");
    if (getenv("LHWM_DEBUG")) frame_fonts_report(&t);
    if (argc >= 7) {
        int w = atoi(argv[3]), h = atoi(argv[4]), foc = atoi(argv[6]), max = argc > 7 ? atoi(argv[7]) : 0;
        int hov = argc > 8 ? (!strcmp(argv[8], "close") ? HIT_CLOSE : !strcmp(argv[8], "max") ? HIT_MAX : !strcmp(argv[8], "min") ? HIT_MIN : 0) : 0;
        cairo_surface_t *icon = argc > 9 ? cairo_image_surface_create_from_png(argv[9]) : NULL;
        cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        /* FRAMETEST_EXT="l r t b [flags]" paints an extended frame (CLIENT-PROTOCOL.md; -1 -1 -1 -1 = sheet of glass) */
        struct frame_ext ext = {0}; const char *es = getenv("FRAMETEST_EXT"); int el = 0, er = 0, et = 0, eb = 0;
        if (es && sscanf(es, "%d %d %d %d %u", &el, &er, &et, &eb, &ext.flags) >= 4) {
            int l, r, tp, b; theme_margins_ex(&t, max, &l, &r, &tp, &b);
            if (el < 0 && er < 0 && et < 0 && eb < 0) { ext.sheet = 1; ext.t = h - tp - b; } else { ext.l = el; ext.r = er; ext.t = et; ext.b = eb; } }
        /* FRAMETEST_FADE=F: the crossfade weight of the maximised plate (0 normal .. 1 maximised; default by MAXIMIZED) */
        const char *fs = getenv("FRAMETEST_FADE"); double fade = fs ? atof(fs) : -1;
        cairo_t *cr = cairo_create(img); frame_paint_cr3(cr, &t, w, h, argv[5], icon, foc, hov, 0, max, es ? &ext : NULL, fade); cairo_destroy(cr);
        int l, r, tp, b; theme_margins_ex(&t, max, &l, &r, &tp, &b); printf("margins l=%d r=%d t=%d b=%d\n", l, r, tp, b);
        XRectangle rc[128]; int n = frame_shape_rects(&t, img, max, rc, 128); printf("shape: %d bands, first %dx%d+%d+%d\n", n, rc[0].width, rc[0].height, rc[0].x, rc[0].y);
        /* apply the shape to the PNG so it shows what the window looks like (pixels outside the shape cleared) */
        cairo_surface_flush(img); unsigned char *px = cairo_image_surface_get_data(img); int st = cairo_image_surface_get_stride(img);
        for (int y = 0; y < h; y++) { int in_l = 0, in_r = w;
            for (int i = 0; i < n; i++) if (y >= rc[i].y && y < rc[i].y + rc[i].height) { in_l = rc[i].x; in_r = rc[i].x + rc[i].width; }
            for (int x = 0; x < w; x++) if (x < in_l || x >= in_r) ((unsigned *)(px + y * st))[x] = 0; }
        cairo_surface_mark_dirty(img); cairo_surface_write_to_png(img, argv[2]);
        return 0;
    }
    int w = 360, h = 90; cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h * 4);
    cairo_t *cr = cairo_create(img); cairo_set_source_rgb(cr, 0.3, 0.5, 0.3); cairo_paint(cr);
    int hov[4] = {0, HIT_CLOSE, 0, HIT_MIN}, prs[4] = {0, 0, HIT_MAX, 0}, foc[4] = {1, 1, 1, 0};
    for (int i = 0; i < 4; i++) { cairo_save(cr); cairo_rectangle(cr, 0, i * h, w, h); cairo_clip(cr); cairo_translate(cr, 0, i * h);
        cairo_push_group(cr); frame_paint_cr(cr, &t, w, h, "Frame test window", NULL, foc[i], hov[i], prs[i], 0); cairo_pop_group_to_source(cr); cairo_paint(cr); cairo_restore(cr); }
    cairo_surface_write_to_png(img, argv[2]);
    /* probe the button centres the theme lays out */
    int hits[4] = {0};
    for (int x = 0; x < w; x++) { int hh = frame_hit(&t, w, h, x, t.mode == THEME_MSSTYLES ? 3 + t.ms_border + 2 + 5 : 12); if (hh >= HIT_CLOSE && hh <= HIT_MIN) hits[hh - HIT_CLOSE]++; }
    printf("hit columns close: %d max: %d min: %d, caption at (100,12): %d, left edge: %d, top-left: %d\n", hits[0], hits[1], hits[2],
        frame_hit(&t, w, h, 100, 12), frame_hit(&t, w, h, 1, 45), frame_hit(&t, w, h, 1, 1));
    return 0;
}
