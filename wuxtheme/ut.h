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
 * DT_VCENTER | DT_SINGLELINE). Fonts come from fontconfig and ut_add_font_dir. */
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
