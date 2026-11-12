/* luxwm: the Alt+Tab task switcher window (switcher.h; docs/LUXWM.md "Task switcher"). The layout and the drawing follow ReactOS's
   user32 controls/appswitch.c (OnPaint, PrepareWindow, the CoolSwitch window styles), which reimplements XP's user32 switcher:
   the client area is COLOR_3DFACE; each entry is a 40 x 40 item (the 32 px icon and 4 px around it) in a 48 px cell, 8 px from the
   edge; the selected item gets two 1 px rectangles in COLOR_HIGHLIGHT; fewer entries than columns are centred; below the grid
   the selected window's title in a pushed push button (DrawFrameControl DFCS_BUTTONPUSH | DFCS_PUSHED), centred, one line,
   end ellipsis, COLOR_BTNTEXT, as tall as the font plus 4 px above and below; the window has WS_EX_DLGMODALFRAME, a 3 px dialog
   frame (EDGE_RAISED and one row of COLOR_3DFACE). More entries than 7 x 3: the grid shows the last 21 up to the selection. */
#include "config.h"
#include "switcher.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <wuxtheme/ut.h>

#define DT_SWITCH 0x8825   /* DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS (appswitch.c), and DT_NOPREFIX: a title's '&' is text */

struct wm_switcher {
    struct wm_switcher_env env;
    struct wm_switcher_item *items; int n, sel;
    Window win; Pixmap back; int W, H;
};

/* the dialog font: DEFAULT_GUI_FONT, MS Shell Dlg 8 (wux's FontSubstitutes: Tahoma, as XP's and 4074's) */
static void dlg_font(ut_font *f) {
    memset(f, 0, sizeof *f);
    snprintf(f->face, sizeof f->face, "MS Shell Dlg"); f->height = -11; f->weight = 400;   /* 8 pt at 96 dpi */
}
static int font_height(void) {
    static int h;
    if (!h) { ut_font f; dlg_font(&f); ut_text_metrics m; h = ut_get_text_metrics(NULL, NULL, 0, 0, &f, &m) && m.height > 0 ? m.height : 13; }
    return h;
}

struct lay { int shown, rows, cw, ch, itemsW, itemsH; };
static void layout(int n, struct lay *L) {
    L->shown = n;
    L->rows = (n + SWITCH_COLS - 1) / SWITCH_COLS;
    if (L->rows > SWITCH_ROWS) { L->rows = SWITCH_ROWS; L->shown = SWITCH_ROWS * SWITCH_COLS; }
    if (L->rows < 1) L->rows = 1;
    L->itemsW = SWITCH_COLS * SWITCH_CELL; L->itemsH = L->rows * SWITCH_CELL;
    L->cw = L->itemsW + 2 * SWITCH_MARGIN;
    L->ch = L->itemsH + 2 * SWITCH_MARGIN + font_height() + 2 * SWITCH_TEXT_MARGIN;
}
void wm_switcher_size(int n, int *w, int *h) { struct lay L; layout(n, &L); *w = L.cw + 2 * SWITCH_FRAME; *h = L.ch + 2 * SWITCH_FRAME; }

/* the first entry shown: the grid scrolls so that the selection is its last item when it lies past it (appswitch.c nShift) */
static int shift_of(int sel, int shown) { return sel >= shown ? sel - shown + 1 : 0; }
/* the top left of shown item i (window coordinates: the frame included) */
static void item_pos(const struct lay *L, int i, int *x, int *y) {
    *x = SWITCH_FRAME + SWITCH_MARGIN + SWITCH_CELL * (i % SWITCH_COLS) + SWITCH_ITEM_MARGIN;
    *y = SWITCH_FRAME + SWITCH_MARGIN + SWITCH_CELL * (i / SWITCH_COLS) + SWITCH_ITEM_MARGIN;
    if (L->shown < SWITCH_COLS) *x += (L->itemsW - L->shown * SWITCH_CELL) / 2;   /* centring */
}

static void paint(const struct theme *t, const struct wm_switcher_item *items, int n, int sel, cairo_surface_t *img) {
    struct lay L; layout(n, &L);
    int W = L.cw + 2 * SWITCH_FRAME, H = L.ch + 2 * SWITCH_FRAME;
    uint32_t C[UT_SYSCOLORS]; wm_theme_syscolors(t, C);
    cairo_surface_flush(img);
    uint32_t *px = (uint32_t *)cairo_image_surface_get_data(img); int stride = cairo_image_surface_get_stride(img);
    uint32_t face = 0xff000000u | C[15];   /* COLOR_3DFACE */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) px[y * (stride / 4) + x] = face;
    ut_rect fr = { 0, 0, W, H };
    ut_draw_edge(px, W, H, stride, &fr, UT_EDGE_RAISED, UT_BF_RECT, C);   /* the dialog frame; its third row is the face fill */
    /* the text box: from the grid's bottom to the margin, the client's width less the margins */
    ut_rect tr = { SWITCH_FRAME + SWITCH_MARGIN, SWITCH_FRAME + SWITCH_MARGIN + L.itemsH, SWITCH_FRAME + L.cw - SWITCH_MARGIN, SWITCH_FRAME + L.ch - SWITCH_MARGIN };
    ut_draw_frame_control(px, W, H, stride, &tr, UT_DFC_BUTTON, UT_DFCS_BUTTONPUSH | UT_DFCS_PUSHED, C);
    if (sel >= 0 && sel < n && items[sel].title && *items[sel].title) {
        ut_font f; dlg_font(&f);
        ut_set_font_smoothing(wm_menu_text_smoothing(t));
        ut_draw_text_font(&f, C[18], px, W, H, stride, items[sel].title, DT_SWITCH, &tr);   /* COLOR_BTNTEXT */
    }
    cairo_surface_mark_dirty(img);
    cairo_t *cr = cairo_create(img);
    int shift = shift_of(sel, L.shown);
    for (int i = 0; i < L.shown && i + shift < n; i++) {
        int x, y; item_pos(&L, i, &x, &y);
        if (i + shift == sel) {   /* two 1 px rectangles, Rectangle(x, y, x + 40, y + 40) and one inside it */
            uint32_t h = C[13];
            cairo_set_source_rgb(cr, (h >> 16 & 255) / 255.0, (h >> 8 & 255) / 255.0, (h & 255) / 255.0);
            int s = SWITCH_ICON + 2 * SWITCH_ICON_MARGIN;
            cairo_rectangle(cr, x, y, s, s); cairo_rectangle(cr, x + 2, y + 2, s - 4, s - 4);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD); cairo_fill(cr);
        }
        cairo_surface_t *ic = items[i + shift].icon;
        if (!ic) continue;
        int iw = cairo_image_surface_get_width(ic), ih = cairo_image_surface_get_height(ic);
        if (iw <= 0 || ih <= 0) continue;
        cairo_save(cr);
        cairo_translate(cr, x + SWITCH_ICON_MARGIN, y + SWITCH_ICON_MARGIN);
        if (iw != SWITCH_ICON || ih != SWITCH_ICON) {   /* DrawIconEx at 32 x 32 (aspect kept, centred) */
            double k = (double)SWITCH_ICON / (iw > ih ? iw : ih);
            cairo_translate(cr, (SWITCH_ICON - iw * k) / 2, (SWITCH_ICON - ih * k) / 2); cairo_scale(cr, k, k);
        }
        cairo_set_source_surface(cr, ic, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST); cairo_paint(cr);
        cairo_restore(cr);
    }
    cairo_destroy(cr);
    cairo_surface_flush(img);
}

cairo_surface_t *wm_switcher_render(const struct theme *t, const struct wm_switcher_item *items, int n, int sel, int *w, int *h) {
    int W, H; wm_switcher_size(n, &W, &H);
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    paint(t, items, n, sel, img);
    if (w) *w = W;
    if (h) *h = H;
    return img;
}

/* _LHWM_SWITCHER on the window: the selected entry, the count, the first entry shown, the selected client (tests, LUXWM.md) */
static void publish(struct wm_switcher *s) {
    struct lay L; layout(s->n, &L);
    long v[4] = { s->sel, s->n, shift_of(s->sel, L.shown), s->sel >= 0 && s->sel < s->n ? (long)s->items[s->sel].window : 0 };
    XChangeProperty(s->env.dpy, s->win, XInternAtom(s->env.dpy, "_LHWM_SWITCHER", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)v, 4);
}

/* paint into the back pixmap, then one XClearWindow puts it on the window (the pixmap is the window's background): no flicker */
static void repaint(struct wm_switcher *s) {
    Display *d = s->env.dpy;
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, s->W, s->H);
    paint(s->env.theme, s->items, s->n, s->sel, img);
    cairo_surface_t *xs = cairo_xlib_surface_create(d, s->back, DefaultVisual(d, s->env.screen), s->W, s->H);
    cairo_t *cr = cairo_create(xs); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_surface(cr, img, 0, 0); cairo_paint(cr);
    cairo_destroy(cr); cairo_surface_flush(xs); cairo_surface_destroy(xs); cairo_surface_destroy(img);
    XClearWindow(d, s->win);
    publish(s);
    XFlush(d);
}

struct wm_switcher *wm_switcher_open(const struct wm_switcher_env *e, const struct wm_switcher_item *items, int n, int sel) {
    if (n <= 0) return NULL;
    struct wm_switcher *s = calloc(1, sizeof *s);
    s->env = *e; s->n = n; s->sel = sel < 0 || sel >= n ? 0 : sel;
    s->items = calloc((size_t)n, sizeof *s->items);
    for (int i = 0; i < n; i++) {
        s->items[i].title = strdup(items[i].title ? items[i].title : "");
        s->items[i].icon = items[i].icon ? cairo_surface_reference(items[i].icon) : NULL;
        s->items[i].window = items[i].window;
    }
    wm_switcher_size(n, &s->W, &s->H);
    /* centred on the monitor (ResizeAndCenter: the client area centred, the frame around it) */
    int x = e->bx + (e->bw - s->W) / 2, y = e->by + (e->bh - s->H) / 2;
    Display *d = e->dpy;
    s->back = XCreatePixmap(d, e->root, s->W, s->H, DefaultDepth(d, e->screen));
    XSetWindowAttributes a = { .override_redirect = True, .background_pixmap = s->back, .border_pixel = 0, .save_under = True,
                               .event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask };
    s->win = XCreateWindow(d, e->root, x, y, s->W, s->H, 0, DefaultDepth(d, e->screen), InputOutput, DefaultVisual(d, e->screen),
                           CWOverrideRedirect | CWBackPixmap | CWBorderPixel | CWSaveUnder | CWEventMask, &a);
    XClassHint ch = { (char *)"lhwm-switcher", (char *)"lhwm" }; XSetClassHint(d, s->win, &ch);
    XStoreName(d, s->win, "lhwm task switcher");
    repaint(s);   /* before the map: the compositor's first frame of it is the finished window */
    XMapRaised(d, s->win);
    XFlush(d);
    return s;
}

void wm_switcher_select(struct wm_switcher *s, int sel) {
    if (!s || sel < 0 || sel >= s->n || sel == s->sel) return;
    s->sel = sel; repaint(s);
}

Window wm_switcher_window(const struct wm_switcher *s) { return s ? s->win : None; }

int wm_switcher_item_at(const struct wm_switcher *s, int x, int y) {
    if (!s) return -1;
    struct lay L; layout(s->n, &L);
    int shift = shift_of(s->sel, L.shown);
    for (int i = 0; i < L.shown && i + shift < s->n; i++) {
        int ix, iy; item_pos(&L, i, &ix, &iy);   /* the whole cell counts */
        if (x >= ix - SWITCH_ITEM_MARGIN && x < ix - SWITCH_ITEM_MARGIN + SWITCH_CELL && y >= iy - SWITCH_ITEM_MARGIN && y < iy - SWITCH_ITEM_MARGIN + SWITCH_CELL)
            return i + shift;
    }
    return -1;
}

void wm_switcher_close(struct wm_switcher *s) {
    if (!s) return;
    XDestroyWindow(s->env.dpy, s->win); XFreePixmap(s->env.dpy, s->back); XFlush(s->env.dpy);
    for (int i = 0; i < s->n; i++) { free((char *)s->items[i].title); if (s->items[i].icon) cairo_surface_destroy(s->items[i].icon); }
    free(s->items); free(s);
}
