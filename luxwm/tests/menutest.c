/* lhwm: renders the window menu (menu.c) offline, as wmmenu_popup draws it: ./menutest THEMESPEC out.png [SEL [MAXIMIZED [KEYBOARD]]]
   THEMESPEC as frametest's (a directory, DIR|STYLE|COLOR|SIZE or |STYLE|COLOR|SIZE); MENUTEST_FONTS="DIR[;DIR]" registers font
   directories as the registry's fonts = does. Build: see README "The window menu". */
#include "../config.h"
#include <stdio.h>
#include <stdlib.h>
#include "../frame.h"
#include "../menu.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s THEMESPEC out.png [SEL [MAXIMIZED [KEYBOARD]]]\n", argv[0]); return 2; }
    if (getenv("MENUTEST_FONTS")) frame_fonts_set(getenv("MENUTEST_FONTS"));
    struct theme t;
    if (!theme_load(&t, argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    int sel = argc > 3 ? atoi(argv[3]) : -1, max = argc > 4 ? atoi(argv[4]) : 0, kb = argc > 5 ? atoi(argv[5]) : 1;
    unsigned mx = max ? WMMENU_DISABLED : 0;
    struct wmmenu_item it[] = {
        { "&Restore", NULL, max ? 0 : WMMENU_DISABLED, WMMENU_GLYPH_RESTORE, 0, NULL, NULL },
        { "&Move", NULL, mx, 0, 1, NULL, NULL },
        { "&Size", NULL, mx, 0, 2, NULL, NULL },
        { "Mi&nimize", NULL, 0, WMMENU_GLYPH_MINIMIZE, 3, NULL, NULL },
        { "Ma&ximize", NULL, mx, WMMENU_GLYPH_MAXIMIZE, 4, NULL, NULL },
        { NULL, NULL, WMMENU_SEPARATOR, 0, 5, NULL, NULL },
        { "&Close", "Alt+F4", WMMENU_DEFAULT, WMMENU_GLYPH_CLOSE, 6, NULL, NULL },
    };
    int w, h; cairo_surface_t *s = wmmenu_render(&t, it, 7, sel, kb ? WMMENU_KEYBOARD : 0, &w, &h);
    cairo_surface_write_to_png(s, argv[2]);
    printf("%dx%d\n", w, h);
    cairo_surface_destroy(s); theme_free(&t);
    return 0;
}
