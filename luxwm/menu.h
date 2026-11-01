/* luxwm: popup menus shown by the window manager (docs/LUXWM.md "The window menu"). The system menu of a window (wm.c wm_menu_open)
   is the first user; the API takes a plain item list so other context menus can reuse it.

   Look: wuxtheme's popup menu painter (ut_menu_draw), the one that draws wux's user32 menus too: the frame theme's visual style
   (its Menu class with the POPUP* parts where it has one, else XP's own drawing in its [SysMetrics] colours, FLATMENUS and
   MENUFONT), or Windows Classic from the scheme's colours and menu font. */
#ifndef LHWM_MENU_H
#define LHWM_MENU_H
#include <X11/Xlib.h>
#include "frame.h"

enum { WMMENU_DISABLED = 1, WMMENU_SEPARATOR = 2, WMMENU_DEFAULT = 4 /* bold (the system menu's Close) */, WMMENU_CHECKED = 8 };
/* the system menu bitmaps (XP HBMMENU_POPUP_*; Vista Menu.SYSTEM* parts) */
enum { WMMENU_GLYPH_NONE, WMMENU_GLYPH_RESTORE, WMMENU_GLYPH_MINIMIZE, WMMENU_GLYPH_MAXIMIZE, WMMENU_GLYPH_CLOSE };

struct wm_menu_item {
    const char *text;                   /* "&Move": '&' marks the mnemonic, "&&" is a literal '&' */
    const char *accel;                  /* shortcut shown right-aligned ("Alt+F4"), or NULL */
    unsigned flags;                     /* WMMENU_* */
    int glyph;                          /* WMMENU_GLYPH_* */
    int id;
    void (*cb)(void *arg, int id);      /* called after the menu has closed (may be NULL) */
    void *arg;
    const struct wm_menu_item *sub;      /* a submenu (nsub items): the item shows an arrow and opens it (hover 400 ms, click, Right, Enter) */
    int nsub;
};

/* how wm_menu_popup was asked: WMMENU_KEYBOARD = opened from the keyboard (first item selected, mnemonics underlined, as XP's
   keyboard cues); WMMENU_FLIP = a context menu: put it left of / above the point when it does not fit (else it is shifted) */
enum { WMMENU_KEYBOARD = 1, WMMENU_FLIP = 2 };
#define WMMENU_CANCEL   (-1)
#define WMMENU_DBLCLICK (-2)

struct wm_menu_env {
    Display *dpy; int screen; Window root;
    Visual *visual; Colormap cmap; int depth;     /* an ARGB visual (depth 32) for the shadow; any other visual works without it */
    const struct theme *theme;
    int composited;                     /* a compositor runs: the drop shadow is real alpha; otherwise it is blended over a copy of the screen */
    int bx, by, bw, bh;                 /* the area (monitor) the menu has to fit in, root coordinates */
    /* events for other windows that arrive while the menu runs (Expose, MapRequest, ConfigureRequest); others stay queued */
    void (*dispatch)(XEvent *ev, void *arg); void *dispatch_arg;
    /* a press inside this root rectangle within dbl_ms of the opening returns WMMENU_DBLCLICK (double-click on the caption icon) */
    int dbl_x, dbl_y, dbl_w, dbl_h; unsigned dbl_ms;
};

/* show the menu with its top-left corner at x, y (root) and run it until an item is chosen or it is dismissed (Esc, Alt, a click
   outside). button: the mouse button that opened it and is still held (0: none): releasing it over an item chooses that item.
   Returns the chosen index (its cb has been called; for a choice in a submenu the index of the item that holds the submenu),
   WMMENU_CANCEL or WMMENU_DBLCLICK. Modal: grabs pointer and keyboard. Submenus (item.sub) open beside their item as XP's cascades:
   after MenuShowDelay (400 ms) of hovering, on a click, Right, Enter or the item's mnemonic; Left or Esc closes only the submenu,
   the pointer back over another item of the parent closes it too. */
/* the session theme's Fonts.smoothing (standard | cleartype | off): the menus' text smoothing when settings.ini [fonts] smoothing
   does not say, as for flxp's menus */
void wm_menu_set_theme_smoothing(const char *v);

int wm_menu_popup(const struct wm_menu_env *env, int x, int y, const struct wm_menu_item *items, int n, unsigned flags, unsigned button);

/* LHWM_MENUTEST: render the menu as wm_menu_popup would draw it (selected item sel, -1 none) into a new image (caller destroys it),
   for offline comparisons; *w, *h get the menu size without the shadow */
cairo_surface_t *wm_menu_render(const struct theme *t, const struct wm_menu_item *items, int n, int sel, unsigned flags, int *w, int *h);
#endif
