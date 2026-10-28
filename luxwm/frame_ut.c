/* lhwm: frames from the .msstyles itself through libuxtheme (~/src/flxp/src/uxtheme, ut.h; README "libuxtheme source").
   Classic msstyles mode: the Window parts (CAPTION, MAXCAPTION, FRAMELEFT/RIGHT/BOTTOM, the caption buttons with their glyphs) are
   drawn at paint time with ut_draw_background (Wine's DrawThemeBackground: nine-grid, TRUESIZE, transparency, IMAGEGLYPH selection),
   the metrics, caption font and colours come from [SysMetrics]; text, shadows, geometry and hit testing stay in frame.c.
   DWM mode: the CompositedWindow PNGSTREAMs replace the theme directory's PNG files (theme.ini keeps insets and tuning keys).
   Built without the library (no HAVE_UXTHEME) this file only has stubs. */
#define _GNU_SOURCE 1   /* strcasestr */
#include "frame_ut.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#ifndef HAVE_UXTHEME
int  fut_available(void) { return 0; }
int  fut_open(struct theme *t, const char *path, const char *color, const char *size) { (void)t; (void)path; (void)color; (void)size; return 0; }
void fut_close(struct theme *t) { (void)t; }
int  fut_ms_load(struct theme *t, int aa_set, int hint_set) { (void)t; (void)aa_set; (void)hint_set; return 0; }
void fut_ms_draw(cairo_t *cr, const struct theme *t, int p, int state, int x, int y, int w, int h) {
    (void)cr; (void)t; (void)p; (void)state; (void)x; (void)y; (void)w; (void)h; }
cairo_surface_t *fut_png(const struct theme *t, const char *name) { (void)t; (void)name; return NULL; }
void fut_add_font_dir(const char *dir) { (void)dir; }
#else
#include <wuxtheme/ut.h>

int fut_available(void) { return 1; }

int fut_open(struct theme *t, const char *path, const char *color, const char *size) {
    fut_close(t);
    ut_theme *u = ut_open(path, color && *color ? color : NULL, size && *size ? size : NULL);
    if (!u) { fprintf(stderr, "luxwm: libuxtheme cannot open %s\n", path); return 0; }
    /* ut_open takes an unknown scheme/size as the style's first: say so, a typo in themes.ini should not go unnoticed */
    if (color && *color && strcasecmp(color, ut_color(u))) fprintf(stderr, "luxwm: %s has no colour %s, using %s\n", path, color, ut_color(u));
    if (size && *size && strcasecmp(size, ut_size(u))) fprintf(stderr, "luxwm: %s has no size %s, using %s\n", path, size, ut_size(u));
    t->ut = u; t->ut_fmt = ut_format(u);
    snprintf(t->ut_src, sizeof t->ut_src, "%s (%s, %s, v%d)", path, ut_color(u), ut_size(u), t->ut_fmt);
    return 1;
}

void fut_close(struct theme *t) {
    if (t->ut) ut_close((ut_theme *)t->ut);
    t->ut = NULL;
}

void fut_add_font_dir(const char *dir) { ut_add_font_dir(dir); }

static const char *ms_part_names[MP_LAST] = { "CAPTION", "MAXCAPTION", "SMALLCAPTION", "FRAMELEFT", "FRAMERIGHT", "FRAMEBOTTOM",
                                              "CLOSEBUTTON", "MAXBUTTON", "MINBUTTON", "RESTOREBUTTON", "HELPBUTTON", "SMALLCLOSEBUTTON" };
#define P(name) ut_prop(u, name)

/* CAPTIONFONT as a pango description, as tools/msstyles2theme.py writes caption_font: "FACE[,Tahoma][ Bold][ Italic] PXpx"
   (Windows: lfHeight = -MulDiv(pt, 96, 72); Tahoma is the fallback family for faces that are not installed) */
static void font_desc(ut_theme *u, int prop, char *out, size_t n) {
    ut_font f; char s[256] = "";
    if (!ut_get_font(u, "sysmetrics", 0, 0, prop, &f)) { snprintf(out, n, "Tahoma Bold 11px"); return; }
    ut_get_string(u, "sysmetrics", 0, 0, prop, s, sizeof s);
    /* XP (v3) styles: ut_get_font reports weight 400 for "Trebuchet MS, 10, bold" (flxp report); the string has the flag */
    int bold = f.weight >= 600, italic = f.italic;
    for (char *save = NULL, *tok = strtok_r(s, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ') tok++;
        if (!strcasecmp(tok, "bold")) bold = 1;
        if (!strcasecmp(tok, "italic")) italic = 1;
    }
    int px = f.height < 0 ? -f.height : (f.height * 96 + 36) / 72;
    snprintf(out, n, "%.60s%s%s%s %dpx", f.face, strcmp(f.face, "Tahoma") ? ",Tahoma" : "", bold ? " Bold" : "", italic ? " Italic" : "", px);
}

static void rgb_of(uint32_t c, double *rgb) { rgb[0] = ((c >> 16) & 255) / 255.0; rgb[1] = ((c >> 8) & 255) / 255.0; rgb[2] = (c & 255) / 255.0; }

/* one part state drawn into a fresh w x h image */
static cairo_surface_t *part_image(ut_theme *u, int part, int state, int w, int h) {
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    ut_rect r = { 0, 0, w, h };
    cairo_surface_flush(s);
    ut_draw_background(u, "Window", part, state, (uint32_t *)cairo_image_surface_get_data(s), w, h, cairo_image_surface_get_stride(s), &r, NULL);
    cairo_surface_mark_dirty(s);
    return s;
}

/* an IMAGEFILE / IMAGEFILEn strip (uxtheme's alpha rules: ut_get_bitmap) split into its IMAGECOUNT images as m->img[k][...]; an
   image without alpha has TRANSPARENTCOLOR keyed out when the part is TRANSPARENT (TransparentBlt) */
static void load_strip(ut_theme *u, struct mspart *m, int id, int k, int prop, int vertical, int transparent, uint32_t tcol) {
    int w, h; uint32_t *px = prop < 0 ? NULL : ut_get_bitmap(u, "Window", id, 0, prop, &w, &h);
    if (!px) return;
    int opaque = 1;
    for (long i = 0; i < (long)w * h && opaque; i++) if ((px[i] >> 24) != 255) opaque = 0;
    if (opaque && transparent) for (long i = 0; i < (long)w * h; i++) if ((px[i] & 0xffffff) == tcol) px[i] = 0;
    int n = m->count, sw = vertical ? w : w / n, sh = vertical ? h / n : h;
    if (sw <= 0 || sh <= 0) { ut_free(px); return; }
    for (int i = 0; i < n; i++) {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
        unsigned char *d = cairo_image_surface_get_data(s); int ds = cairo_image_surface_get_stride(s);
        int x0 = vertical ? 0 : i * sw, y0 = vertical ? i * sh : 0;
        for (int y = 0; y < sh; y++) memcpy(d + y * ds, px + (long)(y0 + y) * w + x0, (size_t)sw * 4);
        cairo_surface_mark_dirty(s); m->img[k][i] = s;
    }
    m->vw[k] = sw; m->vh[k] = sh;
    if (k + 1 > m->nvar) m->nvar = k + 1;
    ut_free(px);
}

int fut_ms_load(struct theme *t, int aa_set, int hint_set) {
    ut_theme *u = t->ut; if (!u) return 0;
    int v; uint32_t c; char fs[256] = "";
    t->ms_border = ut_get_int(u, "sysmetrics", 0, 0, P("SIZINGBORDERWIDTH"), &v) ? v : 1;   /* system default 1 when absent */
    t->ms_cap_h = ut_get_int(u, "sysmetrics", 0, 0, P("CAPTIONBARHEIGHT"), &v) ? v : 18;
    font_desc(u, P("CAPTIONFONT"), t->ms_font, sizeof t->ms_font);
    rgb_of(ut_get_color(u, "sysmetrics", 0, 0, P("CAPTIONTEXT"), &c) ? c : 0xffffff, t->text_a);
    rgb_of(ut_get_color(u, "sysmetrics", 0, 0, P("INACTIVECAPTIONTEXT"), &c) ? c : 0x000000, t->text_i);
    /* text defaults the converter derives (theme.ini wins): Quality:ClearType-Natural has no grid-fitting; XP's "Standard" font
       smoothing leaves captions bi-level */
    ut_get_string(u, "sysmetrics", 0, 0, P("CAPTIONFONT"), fs, sizeof fs);
    if (!hint_set && strcasestr(fs, "natural")) t->ms_hint = CAIRO_HINT_STYLE_NONE;
    if (!aa_set && t->ut_fmt == 3) t->ms_font_aa = CAIRO_ANTIALIAS_NONE;
    int have[64] = { 0 }, ids[64], n = ut_list_parts(u, "Window", ids, 64);
    for (int i = 0; i < n; i++) if (ids[i] > 0 && ids[i] < 64) have[ids[i]] = 1;
    int pim = P("IMAGECOUNT"), psm = P("SIZINGMARGINS"), pcm = P("CONTENTMARGINS"), pcap = P("CAPTIONMARGINS"), pst = P("SIZINGTYPE"),
        pso = P("TEXTSHADOWOFFSET"), psy = P("TEXTSHADOWTYPE"), psc = P("TEXTSHADOWCOLOR");
    for (int p = 0; p < MP_LAST; p++) {
        struct mspart *m = &t->ms[p];
        for (int k = 0; k < 6; k++) for (int i = 0; i < 10; i++) if (m->img[k][i]) cairo_surface_destroy(m->img[k][i]);
        memset(m, 0, sizeof *m);
        int id = ut_part(u, "Window", ms_part_names[p]), cx, cy;
        m->ut_part = id;
        if (id <= 0 || id >= 64 || !have[id] || !ut_part_size(u, "Window", id, 1, 1, NULL, &cx, &cy) || cx <= 0 || cy <= 0) continue;
        m->count = ut_get_int(u, "Window", id, 0, pim, &v) && v > 0 ? v : 1;
        if (m->count > 10) m->count = 10;
        m->nvar = 1; m->vw[0] = cx; m->vh[0] = cy;
        ut_margins mg;
        if (ut_get_margins(u, "Window", id, 0, psm, &mg)) { m->sm[0] = mg.left; m->sm[1] = mg.right; m->sm[2] = mg.top; m->sm[3] = mg.bottom; }
        if (ut_get_margins(u, "Window", id, 0, pcm, &mg)) { m->cm[0] = mg.left; m->cm[1] = mg.right; m->cm[2] = mg.top; m->cm[3] = mg.bottom; }
        if (ut_get_margins(u, "Window", id, 0, pcap, &mg)) { m->capm[0] = mg.left; m->capm[1] = mg.right; m->capm[2] = mg.top; m->capm[3] = mg.bottom; m->has_capm = 1; }
        m->tile = ut_get_enum(u, "Window", id, 0, pst, &v) && v == 2;   /* ST_TILE */
        m->ut_live = 1;
        /* IMAGEGLYPH with CONTENTMARGINS (the caption buttons): XP selects the glyph (IMAGEFILE1..5) for and centres it in the
           content rect (imagefile.cpp DrawBackground: GetBackgroundContentRect, SelectCorrectImageFile(&rc)); the library's Wine
           draw.c uses the whole rect, a size larger and off by the margins. Its bitmaps then go through frame.c's nine-grid and glyph
           rule instead (the same pixels the PNG cache has) */
        if (p >= MP_CLOSE && ut_get_enum(u, "Window", id, 0, P("GLYPHTYPE"), &v) && v == 1 && (m->cm[0] || m->cm[1] || m->cm[2] || m->cm[3])) {
            int vertical = ut_get_enum(u, "Window", id, 0, P("IMAGELAYOUT"), &v) && v == 0;   /* IL_VERTICAL */
            int tr = ut_get_bool(u, "Window", id, 0, P("TRANSPARENT"), &v) && v;
            uint32_t tc = 0xff00ff; ut_get_color(u, "Window", id, 0, P("TRANSPARENTCOLOR"), &tc);
            static const char *files[6] = { "IMAGEFILE", "IMAGEFILE1", "IMAGEFILE2", "IMAGEFILE3", "IMAGEFILE4", "IMAGEFILE5" };
            m->nvar = 0;
            for (int k = 0; k < 6; k++) load_strip(u, m, id, k, P(files[k]), vertical, tr, tc);
            if (m->img[0][0]) m->ut_live = 0;
            else m->nvar = 1;
        }
        if (p <= MP_SMALLCAPTION) {
            for (int s = 0; s < 2; s++) {   /* CS_ACTIVE, CS_INACTIVE */
                int dx = 0, dy = 0, ty = 0; uint32_t col = 0;
                int off = ut_get_position(u, "Window", id, s + 1, pso, &dx, &dy);
                if (off && ut_get_enum(u, "Window", id, s + 1, psy, &v)) ty = v == 2 ? 2 : v == 1 ? 1 : 0;
                ut_get_color(u, "Window", id, s + 1, psc, &col);
                m->sh_type[s] = ty; m->sh_dx[s] = dx; m->sh_dy[s] = dy; rgb_of(col, m->sh_rgb[s]);
            }
            /* the active image at its own size: the part's presence for ms_paint and the window shape's corner rows
               (frame_shape_rects takes the caption's top rows 1:1 from it) */
            m->img[0][0] = part_image(u, id, 1, cx, cy);
        }
    }
    for (int p = MP_MAXCAPTION; p <= MP_SMALLCAPTION; p++) if (!t->ms[p].has_capm) memcpy(t->ms[p].capm, t->ms[MP_CAPTION].capm, sizeof t->ms[p].capm);
    return t->ms[MP_CAPTION].img[0][0] != NULL;
}

void fut_ms_draw(cairo_t *cr, const struct theme *t, int p, int state, int x, int y, int w, int h) {
    const struct mspart *m = &t->ms[p];
    if (!t->ut || w <= 0 || h <= 0 || m->ut_part <= 0) return;
    int sh = m->vh[0], top = m->sm[2], bot = m->sm[3];
    if (h > sh && sh > 0 && top + bot >= sh && top < sh) {
        /* taller than the bitmap, but its SIZINGMARGINS cover every row (Luna Silver's MAXCAPTION: 26 rows, 9 + 17): only lhwm's
           header continuation of a maximised caption asks for that (XP draws MAXCAPTION exactly CAPTIONBARHEIGHT + 1 high; its
           ninegrid2.cpp has no middle to stretch, Wine leaves the rows empty). As frame.c's nine-grid: the part at its own height,
           the rows between the margins repeat source row `top` */
        cairo_surface_t *a = part_image(t->ut, m->ut_part, state, w, sh), *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        unsigned char *sp = cairo_image_surface_get_data(a), *dp = cairo_image_surface_get_data(s);
        int ss = cairo_image_surface_get_stride(a), ds = cairo_image_surface_get_stride(s);
        for (int i = 0; i < h; i++) {
            int v = i < top ? i : i >= h - bot ? sh - bot + (i - (h - bot)) : top;
            if (v < 0) v = 0;
            if (v >= sh) v = sh - 1;
            memcpy(dp + i * ds, sp + v * ss, (size_t)w * 4);
        }
        cairo_surface_mark_dirty(s); cairo_surface_destroy(a);
        cairo_set_source_surface(cr, s, x, y); cairo_paint(cr); cairo_surface_destroy(s);
        return;
    }
    cairo_surface_t *s = part_image(t->ut, m->ut_part, state, w, h);
    cairo_set_source_surface(cr, s, x, y); cairo_paint(cr); cairo_surface_destroy(s);
}

/* ---- DWM: CompositedWindow PNGSTREAMs for the theme directory's file names (COMPOSITION.md 3; ids of the 4074 styles) ---- */
static const struct { const char *file, *part; int half, gray; } pngmap[] = {
    { "plate_active.png", "LGFRAME", 0, 0 },                     /* 483: active top half, inactive bottom half */
    { "plate_inactive.png", "LGFRAME", 1, 0 },
    { "plate_explorer_active.png", "LGFRAMEEXPLORER", 0, 0 },    /* 484 */
    { "plate_explorer_inactive.png", "LGFRAMEEXPLORER", 1, 0 },
    { "plate_max_active.png", "LGFRAMEMAX", 0, 0 },              /* 493 */
    { "plate_max_inactive.png", "LGFRAMEMAX", 1, 0 },
    { "plate_max_explorer_active.png", "LGFRAMEMAXEXPLORER", 0, 0 },   /* 494 */
    { "plate_max_explorer_inactive.png", "LGFRAMEMAXEXPLORER", 1, 0 },
    { "overlay.png", "LGFRAMEOVERLAY", -1, 1 },                  /* 486: grey RGB, an additive highlight = white at alpha = grey */
    { "shadow.png", "LGFRAMESHADOW", -1, 0 },                    /* 485 */
    { "close.png", "LGCLOSEBTN", -1, 0 },                        /* 487 */
    { "glow_close.png", "LGCLOSEBTNOVERLAY", -1, 0 },            /* 488 */
    { "max.png", "LGMAXBTN", -1, 0 },                            /* 489 */
    { "min.png", "LGMINBTN", -1, 0 },                            /* 490 */
    { "restore.png", "LGRESTOREBTN", -1, 0 },                    /* 491 */
    { "glow_btn.png", "LGBTNOVERLAY", -1, 0 },                   /* 492 */
    { "captionfog.png", "CAPTIONFOG", -1, 0 },                   /* 502 */
    { "default_icon.png", "DEFAULTICON", -1, 0 },                /* 506 */
};
struct membuf { const unsigned char *p; size_t n, off; };
static cairo_status_t mem_read(void *closure, unsigned char *data, unsigned int len) {
    struct membuf *b = closure;
    if (b->off + len > b->n) return CAIRO_STATUS_READ_ERROR;
    memcpy(data, b->p + b->off, len); b->off += len;
    return CAIRO_STATUS_SUCCESS;
}

cairo_surface_t *fut_png(const struct theme *t, const char *name) {
    ut_theme *u = t->ut; if (!u || !name) return NULL;
    size_t i; for (i = 0; i < sizeof pngmap / sizeof pngmap[0]; i++) if (!strcmp(name, pngmap[i].file)) break;
    if (i == sizeof pngmap / sizeof pngmap[0]) return NULL;
    int part = ut_part(u, "CompositedWindow", pngmap[i].part), prop = P("PNGSTREAM");
    const void *data; size_t n;
    if (part <= 0 || prop < 0 || !ut_get_stream(u, "CompositedWindow", part, 0, prop, &data, &n)) return NULL;
    struct membuf b = { data, n, 0 };
    cairo_surface_t *src = cairo_image_surface_create_from_png_stream(mem_read, &b);
    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(src); fprintf(stderr, "luxwm: %s: bad PNG stream\n", pngmap[i].part); return NULL; }
    /* always ARGB32 (cairo gives RGB24 for an alpha-less PNG): the half the theme uses, as the extracted file would load */
    int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src), y0 = 0, oh = h;
    if (pngmap[i].half >= 0) { oh = h / 2; y0 = pngmap[i].half * oh; }
    int rgb = cairo_image_surface_get_format(src) == CAIRO_FORMAT_RGB24;
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, oh);
    cairo_surface_flush(src); cairo_surface_flush(out);
    const unsigned char *sp = cairo_image_surface_get_data(src); unsigned char *dp = cairo_image_surface_get_data(out);
    int ss = cairo_image_surface_get_stride(src), ds = cairo_image_surface_get_stride(out);
    for (int y = 0; y < oh; y++) {
        const uint32_t *s = (const uint32_t *)(sp + (y0 + y) * ss); uint32_t *d = (uint32_t *)(dp + y * ds);
        for (int x = 0; x < w; x++) {
            uint32_t px = s[x];
            if (pngmap[i].gray && rgb) { uint32_t g = (px >> 16) & 255; px = g << 24 | g << 16 | g << 8 | g; }   /* premultiplied white */
            else if (rgb) px |= 0xff000000u;
            d[x] = px;
        }
    }
    cairo_surface_mark_dirty(out); cairo_surface_destroy(src);
    return out;
}
#endif
