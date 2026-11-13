/* luxwm: renders the Alt+Tab switcher (switcher.c) offline, as luxwm draws it: ./switchertest THEMESPEC out.png N [SEL]
   N entries ("Window 1" ...) with 32 px icons of distinct colours, SEL selected (default 1); prints the window size WxH.
   THEMESPEC as frametest's (a directory, DIR|STYLE|COLOR|SIZE, |STYLE|COLOR|SIZE or |classic|...). LUXWM.md "Task switcher". */
#include "../config.h"
#include <stdio.h>
#include <stdlib.h>
#include "../frame.h"
#include "../switcher.h"

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s THEMESPEC out.png N [SEL]\n", argv[0]); return 2; }
    struct theme t;
    if (!wm_theme_load(&t, argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    int n = atoi(argv[3]), sel = argc > 4 ? atoi(argv[4]) : 1;
    if (n < 1 || n > 64) { fprintf(stderr, "N: 1..64\n"); return 2; }
    struct wm_switcher_item it[64]; char title[64][32];
    static const unsigned col[] = { 0x3a6ea5, 0xc04040, 0x40a040, 0xd0a020, 0x8040c0, 0x20a0a0, 0x606060 };
    for (int i = 0; i < n; i++) {
        snprintf(title[i], sizeof title[i], "Window %d", i + 1);
        cairo_surface_t *ic = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32); cairo_t *cr = cairo_create(ic);
        unsigned c = col[i % 7];
        cairo_set_source_rgb(cr, (c >> 16 & 255) / 255.0, (c >> 8 & 255) / 255.0, (c & 255) / 255.0); cairo_rectangle(cr, 2, 2, 28, 28); cairo_fill(cr);
        cairo_destroy(cr);
        it[i].title = title[i]; it[i].icon = ic; it[i].window = 0;
    }
    int w, h; cairo_surface_t *s = wm_switcher_render(&t, it, n, sel, &w, &h);
    cairo_surface_write_to_png(s, argv[2]);
    printf("%dx%d\n", w, h);
    cairo_surface_destroy(s);
    for (int i = 0; i < n; i++) cairo_surface_destroy(it[i].icon);
    wm_theme_free(&t);
    return 0;
}
