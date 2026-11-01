/* luxshell scene graph: the handful of node kinds the Longhorn XAML reduces to (SHELL-DESIGN.md 4.1) */
#ifndef LHSHELL_SCENE_H
#define LHSHELL_SCENE_H
#include <cairo/cairo.h>
#include <stdbool.h>
#include <stddef.h>

enum kind { N_PANEL, N_NINEGRID, N_IMAGE, N_TEXT, N_RECT, N_BUTTON, N_TRANSFORM, N_CLOCKHAND, N_TRAY };
enum dock { D_NONE, D_LEFT, D_TOP, D_RIGHT, D_BOTTOM, D_FILL };
enum layout { L_DOCK, L_FILL, L_CANVAS, L_HBOX, L_VBOX };
enum bstate { B_NORMAL, B_HOT, B_PRESSED, B_CHECKED, B_HOTCHECKED, B_FLASH, B_DISABLED, B_LAST };

struct dim { double v; int pct; int is_auto; };      /* size: value, percent flag, auto flag */

struct node {
    enum kind kind; enum layout layout; enum dock dock;
    struct dim w, h; double x, y;                      /* x,y for canvas children */
    double ml, mt, mr, mb;                             /* margin (Border thickness) around the content/children */
    double opacity; bool visible, clip, flip_h, hoverable;
    /* ninegrid / image / button */
    cairo_surface_t *img; cairo_surface_t *simg[B_LAST]; int gt, gr, gb, gl; bool tile;
    int strip_count; int strip_index;                 /* uxtheme-style state strips (vertical) */
    enum bstate state; bool checked, flash;
    /* text */
    char *text, *font; double color[4]; bool ellipsis, wrap, markup; int halign, valign;   /* markup: Pango markup (menu mnemonics <u>x</u>) */
    double shadow_dx, shadow_dy, shadow[4]; bool has_shadow; double glow[4]; bool has_glow; int glow_size; double glow_strength;   /* glow: the blurred coverage (text.c) */
    /* rect */
    double fill[4]; bool has_fill; double radius;
    double stroke[4]; bool has_stroke; double stroke_w;    /* rect outline (inside the rect) */
    int ngrad; bool grad_h; double gpos[8], gcol[8][4];     /* rect gradient fill (LinearGradientBrush, vertical unless grad_h) */
    /* tray: XEmbed icons laid out in rows of icon_size at pitch px,py (luxshell.c places and paints them) */
    int icon_size, pitch_x, pitch_y;
    unsigned long wmicon;                              /* client window whose _NET_WM_ICON_GEOMETRY is this node's rect */
    int cursor;                                        /* pointer shape while hot or pressed: 0 inherit, 1 arrow, 2 SizeWE, 3 SizeNS, 4 hand (luxshell.c) */
    cairo_surface_t *scaled, *scaled_src; int scaled_w, scaled_h, scaled_filter;   /* N_IMAGE: client-side prescaled copy (once, not per frame on the server) */
    int filter;                                        /* image scaling: 0 = the pack default (scene_filter), else an enum sfilter value */
    /* transform: rotate about (ox,oy) by angle degrees, then translate (tx,ty); scale sx,sy */
    double angle, ox, oy, tx, ty, sx, sy;
    /* computed layout */
    double cx, cy, cw, ch;                             /* content rect in surface coords */
    /* tree */
    struct node *parent, **kids; int nkids, cap;
    int refs; int id;                                  /* reference count (see node_ref), unique id */
    void *owner;                                       /* struct surface* */
};

/* Lifetime: nodes are reference counted. node_new returns a node holding one reference (the creator's; in luxshell the Lua
   handle's, released by its __gc). A parent holds one on each child; luxshell's surfaces hold one on their root and on their
   hot / pressed node. node_unref frees the node on the last release (scene_node_freed is called first) and releases its
   children; a child that is still referenced elsewhere survives detached (parent NULL). So clearing or removing a subtree
   never frees a node someone still points at. */
struct node *node_new(enum kind k);
void node_ref(struct node *n);
void node_unref(struct node *n);
void node_add(struct node *parent, struct node *child);   /* takes a reference on child (moves it from its old parent) */
void node_remove(struct node *n);                         /* detach from the parent, dropping the parent's reference */
void node_clear(struct node *n);                          /* detach every child */
extern void (*scene_node_freed)(struct node *n);          /* called just before a node is freed (luxshell.c) */
void scene_desired(struct node *n, double availw, double availh, double *w, double *h);   /* auto size of a subtree */
void scene_layout(struct node *root, double x, double y, double w, double h);
void scene_paint(struct node *root, cairo_t *cr);
struct node *scene_hit(struct node *root, double x, double y);  /* deepest hoverable node */
extern void (*scene_custom_paint)(struct node *n, cairo_t *cr);  /* N_TRAY painter (luxshell.c) */
cairo_surface_t *image_get(const char *path);                      /* cached PNG loader */
int image_flush(void);                                             /* drop cache entries whose file changed (pack reload) */
void text_measure_w(const struct node *n, double wrap_w, double *w, double *h);
void text_measure(const struct node *n, double *w, double *h);
void text_paint(cairo_t *cr, struct node *n);                     /* text.c: GDI text through libuxtheme */
void text_add_font_dir(const char *dir);                           /* a theme font directory (lh.fonts) */
void text_resolve(const char *desc, char *face, size_t fn, char *file, size_t fl);
extern const char *default_font;
/* image scaling filter (SHELL-DESIGN 2.2 "filter"): 4074's HackImages magnify nearest-neighbour (the blocky start flag),
   4051 and the builds up to ~4060 bilinear. scene_filter is the pack default (lh.image_filter), a node's `filter` overrides it. */
enum sfilter { SF_DEFAULT, SF_NEAREST, SF_BILINEAR, SF_BEST };
extern int scene_filter;
#endif
