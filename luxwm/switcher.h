/* luxwm: the Alt+Tab task switcher window, XP's CoolSwitch (docs/LUXWM.md "Task switcher"). wm.c runs the keys (cycle_run); this
   file draws the window: the candidates' 32 px icons in rows of 7, the selected one framed in COLOR_HIGHLIGHT, its title in a
   sunken box below, all on COLOR_3DFACE inside a raised dialog frame, drawn the user32 way through wuxtheme (ut_draw_edge,
   ut_draw_frame_control, ut_draw_text_font) in the theme's system colours (wm_theme_syscolors) and the dialog font (MS Shell Dlg 8). */
#ifndef LHWM_SWITCHER_H
#define LHWM_SWITCHER_H
#include <X11/Xlib.h>
#include "frame.h"

/* the layout: CoolSwitch's defaults and constants (not checked against XP's pixels) */
enum { SWITCH_COLS = 7, SWITCH_ROWS = 3,       /* CoolSwitchColumns, CoolSwitchRows */
       SWITCH_ICON = 32, SWITCH_ICON_MARGIN = 4, SWITCH_ITEM_MARGIN = 4, SWITCH_MARGIN = 8, SWITCH_TEXT_MARGIN = 4,
       SWITCH_CELL = SWITCH_ICON + 2 * SWITCH_ICON_MARGIN + 2 * SWITCH_ITEM_MARGIN,   /* CX_ITEM_SPACE: 48 */
       SWITCH_FRAME = 3 };                     /* WS_EX_DLGMODALFRAME: SM_CXDLGFRAME, EDGE_RAISED + 1 px of COLOR_3DFACE */

struct wm_switcher_item {
    const char *title;                  /* UTF-8 (copied) */
    cairo_surface_t *icon;              /* any size, drawn into 32 x 32 (referenced while the switcher lives); NULL: none */
    Window window;                      /* the client, for _LHWM_SWITCHER */
};

struct wm_switcher_env {
    Display *dpy; int screen; Window root;
    const struct theme *theme;
    int bx, by, bw, bh;                 /* the monitor it is centred on, root coordinates */
};

struct wm_switcher;
/* create, paint and map the window with sel selected (the paint is done before the map: the first frame is the finished one) */
struct wm_switcher *wm_switcher_open(const struct wm_switcher_env *e, const struct wm_switcher_item *items, int n, int sel);
/* select another entry: repainted into the back pixmap, then put on the window in one request */
void wm_switcher_select(struct wm_switcher *s, int sel);
Window wm_switcher_window(const struct wm_switcher *s);
/* the entry under (x, y) in the switcher window's coordinates, -1 none */
int wm_switcher_item_at(const struct wm_switcher *s, int x, int y);
void wm_switcher_close(struct wm_switcher *s);

/* offline (tests): the window's pixels with sel selected; *w, *h its size. Caller destroys it. */
cairo_surface_t *wm_switcher_render(const struct theme *t, const struct wm_switcher_item *items, int n, int sel, int *w, int *h);
/* the window size for n entries, without drawing */
void wm_switcher_size(int n, int *w, int *h);
#endif
