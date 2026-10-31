/* libuxtheme: Windows visual styles (.msstyles) without a window system.
 *
 * One reader and renderer for XP styles (PACKTHEM_VERSION 3: luna.msstyles,
 * the Slate/Jade XP backports) and Longhorn-compiled ones (version 4: the
 * 4042-4074 Luna/Slate/Jade, 5048's aero.msstyles). Inside it is Wine's
 * uxtheme (msstyles.c, draw.c) drawing through flxp's GDI, so parts look as
 * DrawThemeBackground draws them.
 *
 * Names: classes are strings ("Button", "Explorer::ListView", "Window";
 * "sysmetrics" holds the [SysMetrics] values, "globals" the defaults);
 * parts, states and properties are numbers (vssym32.h's BP_PUSHBUTTON,
 * PBS_HOT, TMT_TEXTCOLOR ...), looked up from names with ut_part/ut_state/
 * ut_prop. Property lookup follows uxtheme: state, part, class, globals.
 *
 * Pixels: 32-bit premultiplied ARGB in native byte order (cairo's
 * CAIRO_FORMAT_ARGB32), rows `stride` bytes apart.
 *
 * Not thread-safe per call sequence, but every call takes one global lock,
 * so separate threads may use the library.
 */
#ifndef UXTHEME_UT_H
#define UXTHEME_UT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ut_theme ut_theme;

typedef struct { int left, top, right, bottom; } ut_rect;
typedef struct { int left, right, top, bottom; } ut_margins;     /* MARGINS order */
typedef struct {
    char face[96];            /* UTF-8 */
    int height;               /* LOGFONT lfHeight at 96 DPI: negative = character height in pixels */
    int weight;               /* 400 normal, 700 bold */
    int italic, underline, strikeout;
} ut_font;

/* Open a style. color/size: a scheme and size name ("NormalColor", "Metallic",
 * "Default"; "NormalSize", "Normal"); NULL or unknown = the style's first.
 * NULL on failure (not a visual style, unreadable). */
ut_theme *ut_open(const char *path, const char *color, const char *size);
void ut_close(ut_theme *t);

int ut_format(ut_theme *t);                       /* 3 (XP) or 4 (Longhorn-compiled) */
const char *ut_color(ut_theme *t);                /* the selected scheme */
const char *ut_size(ut_theme *t);                 /* the selected size */
/* The schemes/sizes the style has; returns the count, fills up to max names
 * (valid until ut_close). */
int ut_list_colors(ut_theme *t, const char **names, int max);
int ut_list_sizes(ut_theme *t, const char **names, int max);

/* Listings: return the count, fill up to max entries. Class names stay valid
 * until ut_close. Parts/states are those that have properties of their own
 * (0 = the class level / part level). */
int ut_list_classes(ut_theme *t, const char **names, int max);
int ut_list_parts(ut_theme *t, const char *cls, int *parts, int max);
int ut_list_states(ut_theme *t, const char *cls, int part, int *states, int max);
typedef struct { int id; int type; } ut_prop_info;   /* type: TMT_INT ... (200-211), or the style's own (streams) */
int ut_list_props(ut_theme *t, const char *cls, int part, int state, ut_prop_info *props, int max);

/* Names <-> numbers. The lookups accept decimal numbers too; -1 = unknown.
 * The *_name functions return "" for unknown ids (static or theme-owned). */
int ut_part(ut_theme *t, const char *cls, const char *part);
int ut_state(ut_theme *t, const char *cls, int part, const char *state);
int ut_prop(ut_theme *t, const char *prop);
const char *ut_part_name(ut_theme *t, const char *cls, int part);
const char *ut_state_name(ut_theme *t, const char *cls, int part, int state);
const char *ut_prop_name(ut_theme *t, int prop);
const char *ut_enum_name(ut_theme *t, int prop, int value);

/* Properties: 1 if found (with uxtheme's fallback), else 0. */
int ut_get_int(ut_theme *t, const char *cls, int part, int state, int prop, int *v);
int ut_get_bool(ut_theme *t, const char *cls, int part, int state, int prop, int *v);
int ut_get_enum(ut_theme *t, const char *cls, int part, int state, int prop, int *v);
int ut_get_color(ut_theme *t, const char *cls, int part, int state, int prop, uint32_t *rgb);   /* 0xRRGGBB */
int ut_get_margins(ut_theme *t, const char *cls, int part, int state, int prop, ut_margins *m);
int ut_get_rect(ut_theme *t, const char *cls, int part, int state, int prop, ut_rect *r);
int ut_get_position(ut_theme *t, const char *cls, int part, int state, int prop, int *x, int *y);
int ut_get_font(ut_theme *t, const char *cls, int part, int state, int prop, ut_font *f);
int ut_get_intlist(ut_theme *t, const char *cls, int part, int state, int prop, int *values, int max, int *count);
/* Any property as its text (the ini form; a filename/stream is "#<resource id>"). UTF-8. */
int ut_get_string(ut_theme *t, const char *cls, int part, int state, int prop, char *buf, size_t size);

/* Raw bytes of a resource-valued property, valid until ut_close:
 * PNGSTREAM/D3DXSTREAM/BAMLSTREAM, IMAGEDATA (PNG; 4042 BLOB) in v4 styles;
 * FILENAME properties (IMAGEFILE...) give the RT_BITMAP DIB in both formats. */
int ut_get_stream(ut_theme *t, const char *cls, int part, int state, int prop, const void **data, size_t *size);
/* Any resource of the style file (type/name: a string, or "#123"). */
int ut_get_resource(ut_theme *t, const char *type, const char *name, const void **data, size_t *size);
/* A FILENAME (bitmap, after uxtheme's alpha rules) or PNG stream property
 * decoded to premultiplied ARGB, stride = 4 * w. Free with ut_free. */
uint32_t *ut_get_bitmap(ut_theme *t, const char *cls, int part, int state, int prop, int *w, int *h);
void ut_free(void *p);

/* GetThemePartSize: which = 0 TS_MIN, 1 TS_TRUE, 2 TS_DRAW (bounds may be NULL). */
int ut_part_size(ut_theme *t, const char *cls, int part, int state, int which, const ut_rect *bounds, int *cx, int *cy);
/* GetThemeBackgroundContentRect. */
int ut_content_rect(ut_theme *t, const char *cls, int part, int state, const ut_rect *bounds, ut_rect *content);

/* DrawThemeBackground into a premultiplied ARGB buffer (composited over what
 * is there). clip may be NULL. 1 on success. */
int ut_draw_background(ut_theme *t, const char *cls, int part, int state,
                       uint32_t *pixels, int width, int height, int stride,
                       const ut_rect *rect, const ut_rect *clip);
/* DrawThemeText (UTF-8, DT_* flags of winuser.h, e.g. 0x25 = DT_CENTER |
 * DT_VCENTER | DT_SINGLELINE). Fonts come from fontconfig and ut_add_font_dir (a directory or one font file, searched before
 * the system's). */
int ut_draw_text(ut_theme *t, const char *cls, int part, int state,
                 uint32_t *pixels, int width, int height, int stride,
                 const char *text, unsigned flags, const ut_rect *rect);
void ut_add_font_dir(const char *dir);

/* GetThemeTextExtent: the rectangle DrawText would use (DT_CALCRECT,
 * advance-based, honours DT_SINGLELINE / DT_WORDBREAK / DT_END_ELLIPSIS /
 * DT_NOPREFIX). bounds may be NULL (unbounded); with DT_WORDBREAK its width
 * is the wrap width. 1 on success. */
int ut_text_extent(ut_theme *t, const char *cls, int part, int state,
                   const char *text, unsigned flags, const ut_rect *bounds, ut_rect *extent);

/* The same with an explicit font instead of a theme part (t is not needed).
 * rgb is 0xRRGGBB. These use the same GDI-faithful renderer, hinting and
 * font smoothing as the themed calls. */
int ut_draw_text_font(const ut_font *f, uint32_t rgb,
                      uint32_t *pixels, int width, int height, int stride,
                      const char *text, unsigned flags, const ut_rect *rect);
int ut_text_extent_font(const ut_font *f, const char *text, unsigned flags,
                        const ut_rect *bounds, ut_rect *extent);

/* GetTextMetrics of a font: height = ascent + descent, ave_width, the
 * external leading. f NULL = the theme part's TMT_FONT (t, cls, part, state). */
typedef struct { int height, ascent, descent, internal_leading, external_leading, ave_width, max_width; } ut_text_metrics;
int ut_get_text_metrics(ut_theme *t, const char *cls, int part, int state,
                        const ut_font *f, ut_text_metrics *m);

/* Coverage of the text as an 8-bit mask (0 = none, 255 = full), the same
 * glyphs the draw calls produce, for building a glow or shadow behind text.
 * f non-NULL: that font; else the theme part's font. The mask is not cleared
 * first; coverage is max'ed into it. 1 on success. */
int ut_draw_text_mask(ut_theme *t, const char *cls, int part, int state, const ut_font *f,
                      uint8_t *mask, int width, int height, int stride,
                      const char *text, unsigned flags, const ut_rect *rect);

/* ---- Windows Classic drawing: user32's DrawEdge, DrawFrameControl and DrawCaption into a premultiplied ARGB buffer, with a
 * caller-given table of the 31 system colours (0xRRGGBB by COLOR_* index: scrollbar, background, activecaption,
 * inactivecaption, menu, window, windowframe, menutext, windowtext, captiontext, activeborder, inactiveborder, appworkspace,
 * highlight, highlighttext, btnface, btnshadow, graytext, btntext, inactivecaptiontext, btnhighlight, 3ddkshadow, 3dlight,
 * infotext, infobk, alternatebtnface, hotlight, gradientactivecaption, gradientinactivecaption, menuhilight, menubar; lhde's
 * Colors.* order), or NULL for the process's own. The window manager's frames, flxp's own classic windows and Display
 * Properties' sample are drawn by this one implementation. The constants are winuser.h's. 1 on success. ---- */
#define UT_SYSCOLORS 31
enum { UT_BDR_RAISEDOUTER = 1, UT_BDR_SUNKENOUTER = 2, UT_BDR_RAISEDINNER = 4, UT_BDR_SUNKENINNER = 8,
       UT_EDGE_RAISED = 5, UT_EDGE_SUNKEN = 10, UT_EDGE_ETCHED = 6, UT_EDGE_BUMP = 9 };
enum { UT_BF_LEFT = 1, UT_BF_TOP = 2, UT_BF_RIGHT = 4, UT_BF_BOTTOM = 8, UT_BF_RECT = 15, UT_BF_MIDDLE = 0x800, UT_BF_SOFT = 0x1000,
       UT_BF_ADJUST = 0x2000, UT_BF_FLAT = 0x4000, UT_BF_MONO = 0x8000 };
enum { UT_DFC_CAPTION = 1, UT_DFC_MENU = 2, UT_DFC_SCROLL = 3, UT_DFC_BUTTON = 4 };
enum { UT_DFCS_CAPTIONCLOSE = 0, UT_DFCS_CAPTIONMIN = 1, UT_DFCS_CAPTIONMAX = 2, UT_DFCS_CAPTIONRESTORE = 3, UT_DFCS_CAPTIONHELP = 4,
       UT_DFCS_BUTTONPUSH = 0x10, UT_DFCS_INACTIVE = 0x100, UT_DFCS_PUSHED = 0x200, UT_DFCS_CHECKED = 0x400, UT_DFCS_FLAT = 0x4000,
       UT_DFCS_MONO = 0x8000 };
enum { UT_DC_ACTIVE = 1, UT_DC_SMALLCAP = 2, UT_DC_ICON = 4, UT_DC_TEXT = 8, UT_DC_INBUTTON = 0x10, UT_DC_GRADIENT = 0x20 };
/* DrawEdge: edge = UT_EDGE_* / UT_BDR_*, flags = UT_BF_*; UT_BF_ADJUST shrinks rect to the inside */
int ut_draw_edge(uint32_t *pixels, int width, int height, int stride, ut_rect *rect, unsigned edge, unsigned flags,
                 const uint32_t *colors);
/* DrawFrameControl: type UT_DFC_*, state UT_DFCS_* (a caption button: the push button in btnface with its Marlett glyph in
 * btntext; pushed: sunken, the glyph one pixel down and right) */
int ut_draw_frame_control(uint32_t *pixels, int width, int height, int stride, const ut_rect *rect, unsigned type, unsigned state,
                          const uint32_t *colors);
/* DrawCaptionTemp: the caption bar in rect. UT_DC_ACTIVE: the active colours (else inactive); UT_DC_GRADIENT: GradientFill
 * from the caption colour to its gradient colour, as GDI computes it; UT_DC_ICON: a slot as wide as the bar is high at the
 * left in the solid caption colour, holding icon (premultiplied ARGB icon_w x icon_h, drawn 1:1, centred one pixel right of the
 * slot's centre; NULL: the slot only, the caller draws its own); UT_DC_TEXT: title (UTF-8) in font and the caption text colour,
 * CXEDGE from the left of the text area, vertically centred as DrawText does it, "..." when it does not fit. */
int ut_draw_caption(uint32_t *pixels, int width, int height, int stride, const ut_rect *rect, unsigned flags, const char *title,
                    const uint32_t *icon, int icon_w, int icon_h, const ut_font *font, const uint32_t *colors);
/* GradientFill(GRADIENT_FILL_RECT_H) as GDI computes it: c0 (0xRRGGBB) at rect's left edge to c1 at its right */
int ut_gradient_fill_h(uint32_t *pixels, int width, int height, int stride, const ut_rect *rect, uint32_t c0, uint32_t c1);
/* the text renderer's "Smooth edges of screen fonts" for every draw call of this process: 0 off, 1 standard (grayscale where the
 * font's gasp table allows it: Tahoma 8 stays crisp), 2 ClearType (RGB subpixel coverage on opaque pixels, ignoring the gasp
 * table; grey where the target keeps alpha). Default 1. */
void ut_set_font_smoothing(int mode);

#ifdef UT_WITH_CAIRO
/* cairo: the part is rendered at rect's size (device pixels = user units) and
 * painted at rect.left/top through the current clip. Header-only, so the
 * library does not depend on cairo. */
#include <cairo.h>
#include <stdlib.h>
static inline int ut_draw_background_cairo(ut_theme *t, const char *cls, int part, int state,
                                           cairo_t *cr, const ut_rect *rect)
{
    int w = rect->right - rect->left, h = rect->bottom - rect->top, ok;
    cairo_surface_t *s;
    ut_rect r;
    if (w <= 0 || h <= 0) return 1;
    s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) return 0;
    r.left = 0; r.top = 0; r.right = w; r.bottom = h;
    cairo_surface_flush(s);
    ok = ut_draw_background(t, cls, part, state, (uint32_t *)cairo_image_surface_get_data(s), w, h,
                            cairo_image_surface_get_stride(s), &r, NULL);
    cairo_surface_mark_dirty(s);
    if (ok) {
        cairo_save(cr);
        cairo_set_source_surface(cr, s, rect->left, rect->top);
        cairo_rectangle(cr, rect->left, rect->top, w, h);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    cairo_surface_destroy(s);
    return ok;
}
#endif

#ifdef __cplusplus
}
#endif
#endif
