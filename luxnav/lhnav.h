/* lhnav: the Longhorn navigation bar as a component (../NAVBAR.md).

   The title / back-forward / breadcrumb / search header of a Longhorn Explorer window, as the concept art drew it, for any
   client: layout, painting, hit-testing and visual state live here so the header looks the same in every program; what a click
   *does* (history, the path model, the address and search text) is the client's.

   Conventions follow libuxtheme (~/.local/include/uxtheme/ut.h), so flxp can wrap it unchanged:
   - plain C, no window system, no cairo, no pango: liblhnav depends on libuxtheme (and libpng); pixels are 32-bit premultiplied
     ARGB in native byte order (cairo's CAIRO_FORMAT_ARGB32, a 32-bit DIB section's), rows `stride` bytes apart; lhnav_paint
     composites OVER what is in the buffer (glass: transparent; opaque: the header pixmap);
   - theme art comes from the visual style through libuxtheme (TravelBack/TravelFwd::AnimationButton of 4051-4074 and the aero
     patch, or Vista's TravelBand of 5048/5112; the breadcrumb toolbar's SPLITBUTTON and SPLITBUTTONHORZARROW glyph,
     CompositedBreadcrumbBar::Toolbar's font and glow) with drawn fallbacks for styles that lack them (nav.ini `art_style`
     takes the art from another style; lhnav_art_sources says where each piece came from); all text is libuxtheme's DrawThemeText (GDI text on FreeType, as flxp draws it) in theme fonts
     (lhnav_add_font_dir for the theme's font directories); with no style there is no text;
   - a header-only cairo helper (define LHNAV_WITH_CAIRO before including) for cairo clients.

   Coordinates are the client window's. The geometry (NAVBAR.md, aap's measurements) is measured from the frame's top edge:
     rows [0, deco_h)                    the frame's decoration (caption buttons); the title is drawn here, baseline on deco_h
     rows [deco_h, deco_h + orb_h)       the back/forward orb (no drop-down arrows), the breadcrumb box and the search box,
                                         both boxes box_h high and centred in the row (the big variant: the whole travel art
                                         with its drop-down arrow part, the boxes centred against the orb part)
     rows [.., + gap)                    spacing; 15 + 30 + 2 = 47 rows of header
   A CLIENT_CAPTION client's row 0 is the frame's row border_t (_LHWM_FRAME_METRICS 12), so in client rows everything is
   border_t higher; lhnav_height() is the header in client rows (47 - border_t). Below it the client asks luxwm for `lines` more
   rows of glass (lhnav_extend_top): luxwm draws the plate's inner light/dark line pair there (header_mode = crop), the body starts
   after them.

   Usage:
     lhnav_metrics m; lhnav_metrics_default(&m); lhnav_metrics_load(&m, "THEME_DIR/nav.ini");   (tuning, optional)
     m.border_t = ...; m.caption_x = ...;          (from luxwm: _LHWM_FRAME_METRICS, _LHWM_CAPTION_RECT)
     lhnav *n = lhnav_new(style, &m);              (style: ut_open of the session's msstyles, may be NULL)
     lhnav_set_title / _path / _history; lhnav_layout(n, width); lhnav_paint(n, px, w, h, stride, composited);
     pointer: lhnav_pointer(n, x, y, buttons) -> repaint?; lhnav_hit(n, x, y) -> what is there
*/
#ifndef LHNAV_H
#define LHNAV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ut_theme;   /* libuxtheme's ut_theme (uxtheme/ut.h); lhnav works without one (drawn fallbacks) */
typedef struct lhnav lhnav;

/* parts, as lhnav_hit returns them; a breadcrumb segment i (0 = the leftmost of the path) is LHNAV_CRUMB + i */
enum {
    LHNAV_NONE = 0,     /* not in the header */
    LHNAV_CAPTION,      /* empty header area: the client sends _NET_WM_MOVERESIZE (move) / double-click maximise */
    LHNAV_RESIZE_TOP,   /* the top `resize_rows` rows outside the caption buttons: _NET_WM_MOVERESIZE direction 1 */
    LHNAV_BUTTONS,      /* luxwm's caption buttons (_LHWM_CAPTION_RECT): the client keeps it out of its input shape */
    LHNAV_TITLE,        /* the title text (behaves as caption) */
    LHNAV_BACK,
    LHNAV_FWD,
    LHNAV_ADDRESS,      /* the breadcrumb box outside the segments: the client turns it into an edit field */
    LHNAV_OVERFLOW,     /* the "«" item standing for the collapsed leftmost segments */
    LHNAV_SEARCH,       /* the search box */
    LHNAV_BACK_MENU,    /* the big variant's drop-down arrow part under the back orb (the travel art below CUSTOMSPLITRECT.top):
                           the client opens its back-history list; only when back is possible */
    LHNAV_FWD_MENU,     /* the same under the forward orb */
    LHNAV_CRUMB = 100,  /* + segment index (< 9900) */
    LHNAV_SEP = 10000,  /* + segment index i: the chevron after segment i, a drop-down button (the client lists segment i's
                           subfolders, segment i + 1 highlighted); the chevron after "«" is LHNAV_SEP + lhnav_hidden() - 1; the one
                           after the last segment shows on hover only (nav.ini sep_last) */
    LHNAV_TOOL = 20000  /* + item index of the right-hand toolbar (nav.ini `tools`; lhnav_tool tells the kind) */
};
#define LHNAV_IS_CRUMB(p) ((p) >= LHNAV_CRUMB && (p) < LHNAV_SEP)
#define LHNAV_IS_SEP(p)   ((p) >= LHNAV_SEP && (p) < LHNAV_TOOL)
#define LHNAV_IS_TOOL(p)  ((p) >= LHNAV_TOOL)

/* the kinds of toolbar items (nav.ini `tools = KIND KIND ...`, left to right, right-aligned before pad_r) */
enum {
    LHNAV_TOOL_NONE = 0,
    LHNAV_TOOL_SEARCH,        /* `search`: the magnifier glyph button (the concept's look); the client opens the search field
                                 with lhnav_set_search_open (it appears left of the magnifier) and focuses it */
    LHNAV_TOOL_SEARCH_FIELD,  /* `search_field`: the search box (its part stays LHNAV_SEARCH; not an LHNAV_TOOL hit) */
    LHNAV_TOOL_FAVORITES,     /* `favorites`: the star orb (Favorites::AnimationButton): the client's bookmarks list */
    LHNAV_TOOL_BOOKMARK,      /* `bookmark:PATH[:LABEL]`: a folder button going to PATH (arg = PATH, label = LABEL or "") */
    LHNAV_TOOL_VIEWS,         /* `views`: XP's Views button with its drop-down arrow (whole drop-down: one part) */
    LHNAV_TOOL_NEWFOLDER,     /* `newfolder`: XP's New Folder button */
    LHNAV_TOOL_SEPARATOR      /* `separator`: a vertical line (no hit) */
};

/* layout variants (nav.ini `variant`) */
enum {
    LHNAV_SMALL = 0,    /* the orb only: the travel art's rows above CUSTOMSPLITRECT.top, scaled to orb_h (NAVBAR.md, 47 rows) */
    LHNAV_BIG = 1       /* the full travel art with the drop-down arrow part (hot/pressed show the box), scaled to orb_h
                           (0 = the art's own height, aero-patch 54: unscaled); boxes centred against the orb part */
};

/* "automatic" for the hand-placement keys (nav.ini: an empty value or `auto`) */
#define LHNAV_AUTO (-32768)

/* hand placement of one element, applied after the automatic layout (each element against its own automatic place, so moving
   one does not move the others): x / y absolute (x: client pixels from the client's left edge, negative = from the right edge;
   y: frame rows from the frame's top edge, like deco_h), then dx / dy added; w / h replace the size (boxes only). Any field
   LHNAV_AUTO = keep the automatic value (dx / dy: 0). */
typedef struct lhnav_place { int x, y, dx, dy, w, h; } lhnav_place;

typedef struct lhnav_metrics {
    /* where the client lies in the frame (luxwm: _LHWM_FRAME_METRICS 12 border_t, 1 border_l; 0 0 for a frameless window) */
    int border_t, border_l;
    /* the caption buttons' rectangle in client coordinates (_LHWM_CAPTION_RECT), w = 0: none */
    int caption_x, caption_y, caption_w, caption_h;
    /* the vertical geometry, frame rows (NAVBAR.md): deco 15, orb row 30, gap 2; boxes 23 high, centred in the orb row */
    int deco_h, orb_h, gap, box_h;   /* orb_h: the orb row's height = the travel art's drawn rows scaled to it; 0 = unscaled (big) */
    int lines;          /* rows of the frame's inner line pair below the header (lhnav_extend_top), 2 */
    int resize_rows;    /* top rows that resize (outside the caption buttons), 3; 0 = none (maximised) */
    /* horizontal layout, client pixels */
    int pad_l, pad_r;   /* orb from the left edge, search box to the right edge */
    int title_x;        /* title start */
    int box_gap;        /* orb -> breadcrumb box, breadcrumb box -> search box */
    int search_w;       /* search box width (shrinks with the window down to search_min) */
    int search_min;
    int crumb_pad;      /* text padding inside a segment */
    int sep_w;          /* separator cell (the chevron glyph centred in it) */
    int crumb_x;        /* first segment's offset inside the box */
    int title_dy;       /* baseline correction, rows (+ = down) */
    int crumb_dy;       /* breadcrumb text correction, rows */
    /* fonts: "" = the style's CompositedBreadcrumbBar::Toolbar FONT (else Segoe UI 12 px); else a
       description "Family[, Family2] [Bold] [Italic] SIZE[px]" (pango's form; SIZE without px = points at 96 dpi), drawn
       as an explicit font (ut_font) */
    char title_font[96], crumb_font[96];
    /* colours 0xRRGGBB; 0xFFFFFFFF = from the style (crumbs: TEXTCOLOR; glow: GLOWCOLOR) or the built-in default */
    uint32_t title_rgb, crumb_rgb, glow_rgb, plain_rgb;   /* plain_rgb: all text when not composited (no glow), e.g. the caption text colour */
    int glow_size;      /* blur radius of the white halo behind text on glass (-1 = the style's TEXTGLOWSIZE, else 4) */
    double glow_strength;   /* halo opacity gain (the blurred mask is multiplied by it and clamped): "liberal" = 2.5 */
    int address_field;  /* the breadcrumb box's field: 0 never (only while editing), 1 on hover, 2 always */
    int search_field;   /* the search box's field: 0 hover, 1 always */
    double field_alpha; /* the fields' white at rest (translucent on glass) */
    /* the variant and the hand placement (nav.ini "[preset NAME]" sections; ../NAVBAR.md "Presets") */
    int variant;        /* LHNAV_SMALL, LHNAV_BIG */
    int header_h;       /* the whole header, frame rows (LHNAV_AUTO: deco_h + orb row + gap) */
    lhnav_place orb;    /* the back/forward pair's top-left (w, h unused: orb_h scales it) */
    lhnav_place address, search;   /* the breadcrumb box and the search box (search width: search_w, which narrow windows shrink) */
    int title_dx;       /* added to title_x */
    int title_baseline; /* the title's baseline, frame row (LHNAV_AUTO: deco_h); title_dy is added */
    int crumb_text_dx, crumb_text_dy;   /* the breadcrumb segments' text inside their cells (cells and hover boxes stay) */
    /* the right-hand toolbar (NAVBAR.md "Toolbar"): `tools` lists the items; tool_size = the orb items' size (star, magnifier),
       0 = auto (the orb part's height - 2); tool_icon = the icon buttons' icon size (16 or 24, 32); tool_gap between items;
       tools_dx / tools_dy move every item except the search field (which keeps search_*); the items are centred on the search
       box's row (after search_dy) */
    char tools[512];    /* default "search_field" (the look before the toolbar) */
    int tool_size, tool_icon, tool_gap, tools_dx, tools_dy;
    int sep_last;       /* the chevron after the last segment: 0 never, 1 always, 2 while the breadcrumb is hovered (default) */
    char icon_dir[512]; /* a build's system32 with shell32.dll: the toolbar's icons where the style has none ("" = drawn) */
    char preset[48];    /* the preset lhnav_metrics_load chose ("" = none) */
    char art_dir[512];  /* optional directory with nav_back.png / nav_fwd.png (orb-only state strips: disabled, normal, hot,
                           pressed, stacked vertically) overriding the style's travel art; "" = none */
    char art_style[256];/* nav.ini `art_style`: where the travel, favorites and breadcrumb art comes from. "" or "auto" = the
                           style given to lhnav_new / lhnav_set_style (the theme's own); a NAME = styles/NAME.msstyles in lhde's
                           data directories (lhde_data_find; e.g. aero-patch), an absolute path = that file. Text (fonts, the
                           breadcrumb's colours and glow) stays the theme's style's. A style that cannot be opened: auto, logged */
} lhnav_metrics;

/* the defaults above (Aero vision, small-button variant) */
void lhnav_metrics_default(lhnav_metrics *m);
/* key = value lines (the field names above; colours #RRGGBB or "style"; '#' or ';' starts a comment line); unknown keys are
   ignored. The placement keys are orb_x orb_y orb_dx orb_dy, address_x .. address_h, search_x .. search_h (search_w is the
   width), title_dx, title_baseline, crumb_text_dx, crumb_text_dy, header_h; `variant = small | big`.
   Presets: keys before the first section apply to every preset; `preset = NAME` (outside the sections) picks the section
   `[preset NAME]`, whose keys are read after them (no `preset` key: the first section). Other sections are skipped. 1 when the
   file was read. */
int  lhnav_metrics_load(lhnav_metrics *m, const char *path);
/* the same with the preset given (NULL or "": the file's own choice); m->preset says which one was read */
int  lhnav_metrics_load_preset(lhnav_metrics *m, const char *path, const char *preset);
/* the preset names of a nav.ini in file order (up to max, each < 48 bytes); returns how many */
int  lhnav_presets(const char *path, char (*names)[48], int max);

/* register a font directory for lhnav's text (libuxtheme's ut_add_font_dir: the theme's `fonts`, e.g. the 4074 Fonts directory
   with Segoe UI 0.7); process-wide, before or after lhnav_new */
void lhnav_add_font_dir(const char *dir);

lhnav *lhnav_new(struct ut_theme *style, const lhnav_metrics *m);
void   lhnav_free(lhnav *n);
/* new metrics or a new style (theme change, maximise: the caller re-reads luxwm's properties); call lhnav_layout afterwards.
   lhnav_set_metrics also rebuilds the toolbar from m->tools (a client with its own list puts it there, or calls lhnav_set_tools
   after it) */
void   lhnav_set_metrics(lhnav *n, const lhnav_metrics *m);
void   lhnav_set_style(lhnav *n, struct ut_theme *style);   /* the art is reloaded; the style must stay open while lhnav uses it */
/* the client's name for that style (its file, e.g. the msstyles path), for the log and lhnav_art_sources; NULL = "the theme's
   style". Call before lhnav_set_style / lhnav_new's first paint (takes effect on the next art load: set_style, set_metrics) */
void   lhnav_set_style_name(lhnav *n, const char *name);
/* where every piece of art comes from, one line per element ("back: aero5112.msstyles TravelBand TRAVELBUTTONS left half,
   states NORMAL HOT PRESSED DISABLED", "favorites: drawn", "views: shell32.dll bitmap 216/217 cell 32", ...), for debugging
   (LHNAV_DEBUG=1 in the environment prints it on every art load; navdemo prints it with NAVDEMO_DEBUG=1). Valid until the next
   art load */
const char *lhnav_art_sources(lhnav *n);

void   lhnav_set_title(lhnav *n, const char *utf8);
void   lhnav_set_path(lhnav *n, const char *const *segments, int count);   /* copied */
void   lhnav_set_history(lhnav *n, int can_back, int can_fwd);
/* the frame state: picks the style's Inactive / Max / MaxInactive variants of the travel art where it has them */
void   lhnav_set_frame_state(lhnav *n, int focused, int maximized);
/* the edit fields (painting only; the text, caret and keys are the client's): part = LHNAV_ADDRESS or LHNAV_SEARCH.
   editing: the field is shown opaque white with text and a caret at byte offset `caret` (-1: none); for LHNAV_ADDRESS the
   breadcrumb is hidden meanwhile. text NULL or "" with LHNAV_SEARCH shows the placeholder. */
void   lhnav_set_field(lhnav *n, int part, const char *utf8, int caret, int editing);
void   lhnav_set_placeholder(lhnav *n, const char *utf8);   /* default "Search" */

void   lhnav_layout(lhnav *n, int width);   /* computes every rect; call after the setters and on every resize */
int    lhnav_height(lhnav *n);              /* the header in client rows: deco_h + orb row + gap - border_t (small 47 - 2 = 45),
                                               or header_h - border_t */
int    lhnav_extend_top(lhnav *n);          /* _LHWM_EXTEND_FRAME top for it: lhnav_height + lines */
/* paint the header into rows [0, lhnav_extend_top) of the buffer, over its contents. composited: text gets the white halo
   (glass); 0: plain text in plain_rgb (opaque header) */
void   lhnav_paint(lhnav *n, uint32_t *px, int w, int h, int stride, int composited);

/* the part at (x, y): LHNAV_* above, LHNAV_CRUMB + i, LHNAV_SEP + i (a chevron), LHNAV_TOOL + i (a toolbar item; the search
   field answers LHNAV_SEARCH, a separator item nothing) */
int    lhnav_hit(lhnav *n, int x, int y);
/* pointer state: buttons = bit 0 for button 1 down. Tracks the hovered part and the pressed one (the part under the pointer
   when button 1 went down; it shows pressed while the pointer stays on it). Returns 1 when the picture changed. */
int    lhnav_pointer(lhnav *n, int x, int y, int buttons);
int    lhnav_hover(lhnav *n);               /* the hovered part, LHNAV_NONE when the pointer left (lhnav_pointer(-1, -1, 0)) */
int    lhnav_pressed(lhnav *n);             /* the pressed part, LHNAV_NONE when none */
/* where a part is (client coordinates): LHNAV_ADDRESS / LHNAV_SEARCH = the boxes (where the client's edit fields go),
   LHNAV_BACK, LHNAV_FWD (the whole travel image), LHNAV_BACK_MENU, LHNAV_FWD_MENU (big variant: the drop-down arrow parts, where
   the client puts its history list), LHNAV_TITLE, LHNAV_OVERFLOW, LHNAV_CRUMB + i, LHNAV_SEP + i (the chevron cells; the
   drop-down list goes under it, or flush under LHNAV_ADDRESS), LHNAV_TOOL + i. 0 when the part is not shown (LHNAV_SEARCH: no
   search field out). */
int    lhnav_rect(lhnav *n, int part, int *x, int *y, int *w, int *h);
/* the text area of an edit field (inside the box, for caret placement): x of byte offset `index` of `utf8` drawn there */
int    lhnav_field_x(lhnav *n, int part, const char *utf8, int index);
/* the inverse: the byte offset nearest to client x in field `part` showing `utf8` */
int    lhnav_field_index(lhnav *n, int part, const char *utf8, int x);
/* how many leading segments are collapsed into "«" (0: none) */
int    lhnav_hidden(lhnav *n);
/* the title's baseline, client row (deco_h or title_baseline, + title_dy, - border_t) */
int    lhnav_title_baseline(lhnav *n);

/* ---- the toolbar and the drop-downs (NAVBAR.md "Toolbar", "Breadcrumb drop-downs") ---- */
/* replace the metrics' `tools` list (same syntax: items separated by blanks, an item with blanks in "double quotes");
   call lhnav_layout afterwards. Returns the number of items */
int    lhnav_set_tools(lhnav *n, const char *spec);
int    lhnav_tool_count(lhnav *n);
/* item i: its kind (LHNAV_TOOL_*, 0 when out of range); *arg = the bookmark's path (NULL otherwise), *label = its label ("" none) */
int    lhnav_tool(lhnav *n, int i, const char **arg, const char **label);
/* the tooltip of a part ("Back", "Forward", "Search", "Favorites", a bookmark's label or folder name, "Views", "New Folder", a
   segment's chevron "Folders in NAME"); NULL for none. Valid until the next lhnav_set_tools / lhnav_set_path */
const char *lhnav_tip(lhnav *n, int part);
/* the search field of a `search` item: shown left of the magnifier while open (no `search` item: at the toolbar's right end);
   a `search_field` item shows it always. Relayout after changing it */
void   lhnav_set_search_open(lhnav *n, int open);
int    lhnav_search_open(lhnav *n);
/* the part whose drop-down list the client shows (LHNAV_SEP + i, LHNAV_OVERFLOW, a views / favorites tool, LHNAV_BACK_MENU,
   ...): drawn pressed (a chevron turns down) until it is set back to LHNAV_NONE */
void   lhnav_set_open(lhnav *n, int part);
int    lhnav_open(lhnav *n);
/* the client's own art for an item kind instead of the style's / shell32's (e.g. flxp's icon theme): name = "search" (the
   magnifier), "folder" (bookmarks), "views", "newfolder" (+ "_hot" for the hot image); premultiplied ARGB, stride 4 w, copied;
   px NULL drops it */
void   lhnav_set_icon(lhnav *n, const char *name, const uint32_t *px, int w, int h);

/* text as lhnav draws it, for the client's own popups (tooltips, drop-down lists): the breadcrumb's theme font (bold: the closest
   bold theme font), composited OVER the buffer in rgb (0xRRGGBB), the text cell's top-left at (x, y). lhnav_text_size: the
   advance; *height = the cell height (tmHeight) */
void   lhnav_text(lhnav *n, uint32_t *px, int w, int h, int stride, int x, int y, const char *utf8, int bold, uint32_t rgb);
int    lhnav_text_size(lhnav *n, const char *utf8, int bold, int *height);

#ifdef LHNAV_WITH_CAIRO
/* cairo: paint the header OVER the current target of cr at (0, 0), through its clip. Header-only, so liblhnav itself does not
   tie its callers to cairo (only this helper includes cairo.h). */
#include <cairo.h>
static inline void lhnav_paint_cairo(lhnav *n, cairo_t *cr, int width, int composited)
{
    int h = lhnav_extend_top(n);
    cairo_surface_t *s;
    if (width <= 0 || h <= 0) return;
    s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, h);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) return;
    cairo_surface_flush(s);
    lhnav_paint(n, (uint32_t *)cairo_image_surface_get_data(s), width, h, cairo_image_surface_get_stride(s), composited);
    cairo_surface_mark_dirty(s);
    cairo_save(cr);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_rectangle(cr, 0, 0, width, h);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_surface_destroy(s);
}
#endif

#ifdef __cplusplus
}
#endif
#endif
