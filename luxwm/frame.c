/* luxwm frame painting: the DWM's plate/overlay/button textures, 9-sliced with cairo */
#include "frame.h"
#include "lhde_data.h"
#include "frame_ut.h"
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stddef.h>
#include <X11/extensions/shape.h>
#include <X11/Xutil.h>
#include <fontconfig/fontconfig.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

int frame_theme_source = FRAME_SRC_AUTO;
int frame_uxtheme_available(void) { return fut_available(); }

static cairo_surface_t *load_png(const char *dir, const char *name) {
    /* the frame set's text (theme.ini) and its pixels may lie in different data directories (lhde_data.h): the file beside
       theme.ini, else the same frames/NAME/ in the user's imported data */
    char p[4200]; lhde_data_find_in(dir, name, p, sizeof p);
    cairo_surface_t *s = cairo_image_surface_create_from_png(p);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); fprintf(stderr, "luxwm: missing %s\n", p); return NULL; }
    return s;
}
/* a theme texture: from the open style's CompositedWindow streams (frame_ut.c fut_png), else the directory's file */
static cairo_surface_t *theme_png(const struct theme *t, const char *dir, const char *name) {
    cairo_surface_t *s = fut_png(t, name);
    if (s) return s;
    if (!dir[0]) { fprintf(stderr, "luxwm: %s: not in the style and no theme directory\n", name); return NULL; }
    return load_png(dir, name);
}
static void parse_color(const char *s, double *rgb) {
    unsigned v = 0; if (s[0] == '#') sscanf(s + 1, "%x", &v);
    rgb[0] = ((v >> 16) & 255) / 255.0; rgb[1] = ((v >> 8) & 255) / 255.0; rgb[2] = (v & 255) / 255.0;
}

/* ---- classic msstyles mode: theme.ini keys written by tools/msstyles2theme.py ---- */
static const char *ms_names[MP_LAST] = { "caption", "maxcaption", "smallcaption", "frameleft", "frameright", "framebottom",
                                         "close", "max", "min", "restore", "help", "smallclose" };
static char ms_vars[MP_LAST][160];   /* variant lists, resolved once the directory is known */
static int ms_aa_set, ms_hint_set;   /* theme.ini set text_antialias / text_hinting (the library source derives defaults otherwise) */

static void ms_key(struct theme *t, const char *key, const char *val) {
    if (!strcmp(key, "border_width")) { t->ms_border = atoi(val); return; }
    if (!strcmp(key, "caption_height")) { t->ms_cap_h = atoi(val); return; }
    if (!strcmp(key, "caption_font")) { snprintf(t->ms_font, sizeof t->ms_font, "%.95s", val); return; }
    if (!strcmp(key, "caption_text")) { parse_color(val, t->text_a); return; }
    if (!strcmp(key, "caption_text_inactive")) { parse_color(val, t->text_i); return; }
    if (!strcmp(key, "text_dy")) { t->ms_text_dy = atoi(val); return; }
    if (!strcmp(key, "text_hinting")) { ms_hint_set = 1; t->ms_hint = !strcmp(val, "none") ? CAIRO_HINT_STYLE_NONE : !strcmp(val, "slight") ? CAIRO_HINT_STYLE_SLIGHT : !strcmp(val, "medium") ? CAIRO_HINT_STYLE_MEDIUM : CAIRO_HINT_STYLE_FULL; return; }
    if (!strcmp(key, "text_antialias")) { ms_aa_set = 1;
        t->ms_font_aa = !strcmp(val, "none") ? CAIRO_ANTIALIAS_NONE : !strcmp(val, "gray") ? CAIRO_ANTIALIAS_GRAY : CAIRO_ANTIALIAS_SUBPIXEL; return; }
    const char *dot = strchr(key, '.'); if (!dot) return;
    int p; for (p = 0; p < MP_LAST; p++) if ((size_t)(dot - key) == strlen(ms_names[p]) && !strncmp(key, ms_names[p], dot - key)) break;
    if (p == MP_LAST) return;
    struct mspart *m = &t->ms[p]; const char *k = dot + 1;
    if (!strcmp(k, "images")) m->count = atoi(val);
    else if (!strcmp(k, "variants")) snprintf(ms_vars[p], sizeof ms_vars[p], "%.159s", val);
    else if (!strcmp(k, "sizing_margins")) sscanf(val, "%d %d %d %d", &m->sm[0], &m->sm[1], &m->sm[2], &m->sm[3]);
    else if (!strcmp(k, "content_margins")) sscanf(val, "%d %d %d %d", &m->cm[0], &m->cm[1], &m->cm[2], &m->cm[3]);
    else if (!strcmp(k, "caption_margins")) m->has_capm = sscanf(val, "%d %d %d %d", &m->capm[0], &m->capm[1], &m->capm[2], &m->capm[3]) == 4;
    else if (!strcmp(k, "sizing")) m->tile = !strcmp(val, "tile");
    else if (!strncmp(k, "shadow.", 7)) {
        int s = atoi(k + 7) - 1; char type[32], col[32]; if (s < 0 || s > 1) return;
        if (sscanf(val, "%31s %d %d %31s", type, &m->sh_dx[s], &m->sh_dy[s], col) == 4) {
            m->sh_type[s] = !strcmp(type, "continuous") ? 2 : !strcmp(type, "single") ? 1 : 0; parse_color(col, m->sh_rgb[s]); }
    }
}

static int ms_load(struct theme *t, const char *dir) {
    for (int p = 0; p < MP_LAST; p++) {
        struct mspart *m = &t->ms[p]; char *s = ms_vars[p], *tok, *save = NULL;
        if (m->count > 10) m->count = 10;
        while ((tok = strtok_r(s, " ", &save))) { s = NULL;
            int k, w, h; if (sscanf(tok, "@%d:%dx%d", &k, &w, &h) != 3 || k < 0 || k > 5) continue;
            m->vw[k] = w; m->vh[k] = h; if (k + 1 > m->nvar) m->nvar = k + 1;
            for (int i = 0; i < m->count; i++) {
                char n[128]; if (k) snprintf(n, sizeof n, "%s@%d_%d.png", ms_names[p], k, i + 1); else snprintf(n, sizeof n, "%s_%d.png", ms_names[p], i + 1);
                m->img[k][i] = load_png(dir, n);
            }
        }
    }
    /* parts without own CAPTIONMARGINS use CAPTION's (uxtheme: marCaptionText defaults) */
    for (int p = MP_MAXCAPTION; p <= MP_SMALLCAPTION; p++) if (!t->ms[p].has_capm) memcpy(t->ms[p].capm, t->ms[MP_CAPTION].capm, sizeof t->ms[p].capm);
    return t->ms[MP_CAPTION].img[0][0] != NULL;
}

/* uxtheme's nine-grid source index for destination pixel i (ninegrid2.cpp): 32.32 fixed-point nearest neighbour starting half a step
   in; corners copied 1:1 while a middle remains, otherwise the corners share the destination in proportion to their margins */
static int ng_stretch(int i, int D, int S) {
    if (D <= 0 || S <= 0) return 0;
    uint64_t dx = ((((uint64_t)S << 32) - 1) / (uint64_t)D) + 1, x0 = (((uint64_t)S << 32) / (uint64_t)D) >> 1;
    return (int)((x0 + (uint64_t)i * dx) >> 32);
}
static int ng_map(int i, int D, int S, int l, int r, int tile) {
    int show = D - l - r > 0, nl = l, nr = r;
    if (!show) { nl = (l + r) ? l * D / (l + r) : 0; nr = D - nl; }
    int v;
    if (i < nl) v = show ? i : ng_stretch(i, nl, l);
    else if (i >= D - nr) { int j = i - (D - nr); v = S - r + (show ? j : ng_stretch(j, nr, r)); }
    else { int mid = D - nl - nr, smid = S - l - r; if (smid <= 0) v = l; else v = l + (tile ? (i - nl) % smid : ng_stretch(i - nl, mid, smid)); }
    return v < 0 ? 0 : v >= S ? S - 1 : v;
}
/* draw src into (x,y,w,h) with SIZINGMARGINS m = left right top bottom */
static void ms_draw(cairo_t *cr, cairo_surface_t *src, int x, int y, int w, int h, const int *m, int tile) {
    if (!src || w <= 0 || h <= 0) return;
    cairo_surface_flush(src);
    int sw = cairo_image_surface_get_width(src), sh = cairo_image_surface_get_height(src), ss = cairo_image_surface_get_stride(src);
    const unsigned char *sp = cairo_image_surface_get_data(src);
    cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    unsigned char *dp = cairo_image_surface_get_data(tmp); int ds = cairo_image_surface_get_stride(tmp);
    int *mx = malloc(w * sizeof *mx);
    for (int i = 0; i < w; i++) mx[i] = ng_map(i, w, sw, m[0], m[1], tile);
    for (int j = 0; j < h; j++) {
        const uint32_t *srow = (const uint32_t *)(sp + ng_map(j, h, sh, m[2], m[3], tile) * ss); uint32_t *drow = (uint32_t *)(dp + j * ds);
        for (int i = 0; i < w; i++) drow[i] = srow[mx[i]];
    }
    free(mx); cairo_surface_mark_dirty(tmp);
    cairo_set_source_surface(cr, tmp, x, y); cairo_paint(cr); cairo_surface_destroy(tmp);
}

/* caption buttons, uxtheme _GetNcBtnMetrics: all sized from CLOSEBUTTON's aspect, (MulDiv(CAPTIONBARHEIGHT, cx, cy) - 4) x
   (CAPTIONBARHEIGHT - 4), 2 px apart, the close button 2 px left of the right border, 2 px below the top border */
static void ms_btn_rect(const struct theme *t, int w, int slot, int maximized, int *bx, int *by, int *bw, int *bh) {
    int cnb = 3 + t->ms_border, off = maximized ? -cnb : 0, right = (maximized ? w + cnb : w) - cnb;
    const struct mspart *c = &t->ms[MP_CLOSE]; int cw = c->vw[0] ? c->vw[0] : 1, ch = c->vh[0] ? c->vh[0] : 1;
    *bw = (t->ms_cap_h * cw + ch / 2) / ch - 4; *bh = t->ms_cap_h - 4;
    *by = off + cnb + 2; *bx = right - 2 - *bw - slot * (*bw + 2);
}

static void ms_shadow_text(cairo_t *cr, PangoLayout *lay, int x, int y, int tw, int th, int dx, int dy, const double *rgb, cairo_antialias_t aa) {
    /* comctl32 DrawShadowText (TST_CONTINUOUS): text into a (W+10)x(H+10) buffer at (5,5); every touched pixel counts 255;
       horizontal box of the next four pixels / 4, then (bottom-up DIB) the row and the four rows above / 4, clamped;
       blended at (left - 4 + dx, top - 8 + dy) */
    int cx = tw + 10, cy = th + 10;
    cairo_surface_t *a = cairo_image_surface_create(CAIRO_FORMAT_A8, cx, cy);
    cairo_t *c2 = cairo_create(a); cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, aa == CAIRO_ANTIALIAS_NONE ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_GRAY);
    pango_cairo_context_set_font_options(pango_layout_get_context(lay), fo); pango_layout_context_changed(lay);
    cairo_move_to(c2, 5, 5); pango_cairo_show_layout(c2, lay); cairo_destroy(c2); cairo_surface_flush(a);
    unsigned char *d = cairo_image_surface_get_data(a); int st = cairo_image_surface_get_stride(a);
    int *hb = calloc(cx * cy, sizeof *hb);
    for (int r = 0; r < cy; r++) for (int i = 0; i < cx; i++) {
        if (i < cx - 5) { int s = 0; for (int k = 1; k <= 4; k++) s += d[r * st + i + k] ? 255 : 0; hb[r * cx + i] = s >> 2; }
        else hb[r * cx + i] = d[r * st + i] ? 255 : 0;
    }
    for (int r = 0; r < cy; r++) for (int i = 0; i < cx; i++) {
        int v = hb[r * cx + i];
        if (r > 4) { v = 0; for (int k = 0; k <= 4; k++) v += hb[(r - k) * cx + i]; v >>= 2; if (v > 255) v = 255; }
        d[r * st + i] = v;
    }
    free(hb); cairo_surface_mark_dirty(a);
    cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]); cairo_mask_surface(cr, a, x - 4 + dx, y - 8 + dy);
    cairo_surface_destroy(a);
    cairo_font_options_set_antialias(fo, aa); pango_cairo_context_set_font_options(pango_layout_get_context(lay), fo);
    pango_layout_context_changed(lay); cairo_font_options_destroy(fo);
}

static int ms_slot_hit(int slot) { return slot == 0 ? HIT_CLOSE : slot == 1 ? HIT_MAX : HIT_MIN; }

/* part p, strip image idx (0-based, clamped to IMAGECOUNT like uxtheme), into (x, y, w, h): through libuxtheme (DrawThemeBackground
   with state idx + 1: background and IMAGEGLYPH glyph) or from the PNG cache with frame.c's nine-grid (the glyph is the caller's) */
static void ms_part(cairo_t *cr, const struct theme *t, int p, int idx, int x, int y, int w, int h) {
    const struct mspart *m = &t->ms[p];
    if (idx >= m->count) idx = m->count - 1;
    if (idx < 0) idx = 0;
    if (t->ut && m->ut_live) fut_ms_draw(cr, t, p, idx + 1, x, y, w, h);
    else ms_draw(cr, m->img[0][idx], x, y, w, h, m->sm, m->tile);
}

/* ext (may be NULL): flags NO_TITLE / NO_ICON (honoured in every mode), and ext->t > 0 continues the caption band that many rows
   further down (the header pixmap, CLIENT-PROTOCOL.md "Additions 2026-10-24"): the caption bitmap is nine-gridded into the taller
   rect with its own sizing margins, the side frames start below it */
static void ms_paint(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover, int pressed, int maximized,
                     const struct frame_ext *ext) {
    int cnb = 3 + t->ms_border, fs = focused ? 0 : 1;
    int ox = maximized ? -cnb : 0, oy = ox, W = maximized ? w + 2 * cnb : w, ch = cnb + t->ms_cap_h + 1;
    int et = ext && !ext->sheet && ext->t > 0 ? ext->t : 0;
    if (ext && (ext->flags & FRAME_NO_ICON)) icon = NULL;
    if (ext && (ext->flags & FRAME_NO_TITLE)) title = NULL;
    int capp = maximized && t->ms[MP_MAXCAPTION].img[0][0] ? MP_MAXCAPTION : MP_CAPTION;
    const struct mspart *cap = &t->ms[capp];
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    #define MSIMG(m, v, s) ((m)->img[v][((s) < (m)->count ? (s) : (m)->count - 1)])
    if (!maximized) {   /* full-screen maximised windows have no side/bottom frames */
        const struct mspart *fl = &t->ms[MP_FRAMELEFT], *fr = &t->ms[MP_FRAMERIGHT], *fb = &t->ms[MP_FRAMEBOTTOM];
        if (fl->count) ms_part(cr, t, MP_FRAMELEFT, fs, 0, ch + et, cnb, h - ch - et - cnb);
        if (fr->count) ms_part(cr, t, MP_FRAMERIGHT, fs, w - cnb, ch + et, cnb, h - ch - et - cnb);
        if (fb->count) ms_part(cr, t, MP_FRAMEBOTTOM, fs, 0, h - cnb, w, cnb);
    }
    /* full-screen maximised (nctheme.cpp NcPaintCaption, fFullMaxed): the MAXCAPTION background rect is the caption rect inset by
       cnBorders on top, left and right, i.e. exactly the visible w x (CAPTIONBARHEIGHT + 1) band; only the buttons and the text keep
       their window-relative offsets (ox, oy). Drawing the bitmap over the whole off-screen rect put its edge rows/columns inside. */
    if (cap->count) {
        if (maximized) ms_part(cr, t, capp, fs, 0, 0, w, t->ms_cap_h + 1 + et);
        else ms_part(cr, t, capp, fs, ox, oy, W, ch + et);
    }
    /* buttons: close, max/restore, min; strip index = framestate * (count / 2) + {normal 0, hot 1, pushed 2, disabled 3}
       (8-image XP strips: 4 per frame state; the Longhorn 10-image strips: 5 per frame state, the 5th unused here) */
    int leftmost = W + ox;
    for (int slot = 0; slot < 3; slot++) {
        int part = slot == 0 ? MP_CLOSE : slot == 2 ? MP_MIN : maximized ? MP_RESTORE : MP_MAX;
        const struct mspart *b = &t->ms[part]; if (!b->count) continue;
        int bx, by, bw, bh; ms_btn_rect(t, w, slot, maximized, &bx, &by, &bw, &bh); leftmost = bx;
        int hit = ms_slot_hit(slot), st = pressed == hit ? 2 : hover == hit ? 1 : 0;
        int idx = (b->count >= 8 ? fs * (b->count / 2) : 0) + st;
        ms_part(cr, t, part, idx, bx, by, bw, bh);
        if (t->ut && b->ut_live) continue;   /* DrawThemeBackground drew the glyph too */
        /* IMAGEGLYPH: the largest IMAGEFILEn that fits the content rect, true size, centred */
        int cw = bw - b->cm[0] - b->cm[1], chh = bh - b->cm[2] - b->cm[3], k = 0;
        for (int v = 1; v < b->nvar; v++) { if (!b->img[v][0]) continue; if (!k) k = v; if (b->vw[v] <= cw && b->vh[v] <= chh) k = v; }
        if (k) { cairo_surface_t *g = MSIMG(b, k, idx);
            cairo_set_source_surface(cr, g, bx + b->cm[0] + (cw - b->vw[k]) / 2, by + b->cm[2] + (chh - b->vh[k]) / 2); cairo_paint(cr); }
    }
    /* icon: SM_CXSMICON square at cnBorders + cxEdge, vertically centred in CAPTIONBARHEIGHT */
    int ix = ox + cnb + 2, iy = oy + cnb + (t->ms_cap_h - 16) / 2;
    if (icon) {
        double iw = cairo_image_surface_get_width(icon), ih = cairo_image_surface_get_height(icon), sc = 16 / (iw > ih ? iw : ih);
        if (iw <= 20 && ih <= 20 && (iw > 16 || ih > 16)) sc = 1;   /* as the DWM path: slightly larger icons 1:1, centred */
        cairo_save(cr); cairo_translate(cr, ix + floor((16 - iw * sc) / 2), iy + floor((16 - ih * sc) / 2)); cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, icon, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST); cairo_paint(cr); cairo_restore(cr);
    }
    if (title && *title) {   /* _GetNcCaptionTextRect: left = icon.right + CAPTIONMARGINS.left, right = first button - margin.right,
                                top = cnBorders + (CAPTIONBARHEIGHT + 1 - text height) / 2; DT_SINGLELINE | DT_END_ELLIPSIS */
        PangoLayout *lay = pango_cairo_create_layout(cr); PangoFontDescription *fd = pango_font_description_from_string(t->ms_font);
        cairo_font_options_t *fo = cairo_font_options_create(); cairo_font_options_set_antialias(fo, t->ms_font_aa);
        cairo_font_options_set_subpixel_order(fo, CAIRO_SUBPIXEL_ORDER_RGB); cairo_font_options_set_hint_style(fo, t->ms_hint ? (cairo_hint_style_t)t->ms_hint : CAIRO_HINT_STYLE_FULL);
        cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_ON);
        pango_cairo_context_set_font_options(pango_layout_get_context(lay), fo); cairo_font_options_destroy(fo); pango_layout_context_changed(lay);
        pango_layout_set_font_description(lay, fd); pango_layout_set_text(lay, title, -1);
        int left = (icon ? ix + 16 : ox + cnb) + cap->capm[0], right = leftmost - cap->capm[1];   /* no icon: like a window without WS_SYSMENU */
        if (right < left) right = left;
        pango_layout_set_width(lay, (right - left) * PANGO_SCALE); pango_layout_set_ellipsize(lay, PANGO_ELLIPSIZE_END);
        /* GDI text metrics: tmAscent/tmDescent = the unhinted ascent/descent rounded; DrawText puts the
           baseline tmAscent below the rect top */
        PangoContext *mctx = pango_font_map_create_context(pango_cairo_font_map_get_default());
        cairo_font_options_t *mfo = cairo_font_options_create(); cairo_font_options_set_hint_metrics(mfo, CAIRO_HINT_METRICS_OFF);
        pango_cairo_context_set_font_options(mctx, mfo); cairo_font_options_destroy(mfo);
        PangoFontMetrics *fm = pango_context_get_metrics(mctx, fd, NULL);
        int asc = (pango_font_metrics_get_ascent(fm) + PANGO_SCALE / 2) / PANGO_SCALE, desc = (pango_font_metrics_get_descent(fm) + PANGO_SCALE / 2) / PANGO_SCALE;
        pango_font_metrics_unref(fm); g_object_unref(mctx);
        int th = asc + desc, top = oy + cnb + (t->ms_cap_h + 1 - th) / 2 + t->ms_text_dy;
        top += asc - pango_layout_get_baseline(lay) / PANGO_SCALE;   /* where the layout's top must go for that baseline */
        int lw, lh; pango_layout_get_pixel_size(lay, &lw, &lh); (void)th;
        if (cap->sh_type[fs] == 2) ms_shadow_text(cr, lay, left, top, right - left, lh, cap->sh_dx[fs], cap->sh_dy[fs], cap->sh_rgb[fs], t->ms_font_aa);
        else if (cap->sh_type[fs] == 1) { cairo_set_source_rgb(cr, cap->sh_rgb[fs][0], cap->sh_rgb[fs][1], cap->sh_rgb[fs][2]);
            cairo_move_to(cr, left + cap->sh_dx[fs], top + cap->sh_dy[fs]); pango_cairo_show_layout(cr, lay); }
        const double *tc = maximized ? (focused ? t->text_ma : t->text_mi) : (focused ? t->text_a : t->text_i); cairo_set_source_rgb(cr, tc[0], tc[1], tc[2]);
        cairo_move_to(cr, left, top); pango_cairo_show_layout(cr, lay);
        pango_font_description_free(fd); g_object_unref(lay);
    }
    #undef MSIMG
}

/* fonts: the registry's `fonts` (frame_fonts_set: directories or files, ';' separated) plus the theme.ini `font_files` (files,
   space separated; Luna's Trebuchet MS) are the process's fontconfig application fonts. The set is replaced whenever it changes
   (FcConfigAppFontClear, then add each), so switching to a theme without the 4074 Fonts directory gives the stock fallback again;
   afterwards pango's default font map is dropped so the next layout sees the new set. */
static char fonts_reg[2048], fonts_applied[4600];
void frame_fonts_set(const char *list) { snprintf(fonts_reg, sizeof fonts_reg, "%s", list ? list : ""); }
static void font_add(const char *f0, const char *what) {
    struct stat st; int ok; char fb[4200]; const char *f = lhde_data_resolve(f0, fb, sizeof fb);   /* fonts/4074/trebuc.ttf: the data directories */
    if (stat(f, &st) != 0) { fprintf(stderr, "luxwm: %s: %s does not exist\n", what, f); return; }
    ok = S_ISDIR(st.st_mode) ? FcConfigAppFontAddDir(NULL, (const FcChar8 *)f) : FcConfigAppFontAddFile(NULL, (const FcChar8 *)f);
    if (!ok) fprintf(stderr, "luxwm: %s: cannot load %s\n", what, f);
}
static void fonts_apply(const char *files) {
    char want[sizeof fonts_applied]; snprintf(want, sizeof want, "%s|%s", fonts_reg, files ? files : "");
    if (!strcmp(want, fonts_applied)) return;
    snprintf(fonts_applied, sizeof fonts_applied, "%s", want);
    FcInit(); FcConfigAppFontClear(NULL);
    char buf[sizeof fonts_reg]; snprintf(buf, sizeof buf, "%s", fonts_reg);
    for (char *save = NULL, *f = strtok_r(buf, ";", &save); f; f = strtok_r(NULL, ";", &save)) {
        while (*f == ' ' || *f == '\t') f++;
        size_t n = strlen(f); while (n && (f[n - 1] == ' ' || f[n - 1] == '\t')) f[--n] = 0;
        if (*f) { font_add(f, "fonts"); fut_add_font_dir(f); }   /* libuxtheme's own text (ut_draw_text) sees them too */
    }
    char fb[400]; snprintf(fb, sizeof fb, "%s", files ? files : "");
    for (char *save = NULL, *f = strtok_r(fb, " ", &save); f; f = strtok_r(NULL, " ", &save)) font_add(f, "font_files");
    pango_cairo_font_map_set_default(NULL);
}
void frame_fonts_report(const struct theme *t) {
    const char *name = t->mode == THEME_MSSTYLES ? t->ms_font : t->font;
    PangoFontMap *fm = pango_cairo_font_map_get_default(); PangoContext *ctx = pango_font_map_create_context(fm);
    PangoFontDescription *fd = pango_font_description_from_string(name);
    PangoFont *f = pango_font_map_load_font(fm, ctx, fd);
    char file[512] = "?";
    if (f) {   /* the file behind the first family pango settles on (pangocairo-fc fonts are PangoFcFonts) */
        PangoFontDescription *got = pango_font_describe(f);
        FcPattern *pat = FcPatternCreate(), *m; FcResult r;
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)pango_font_description_get_family(got));
        if (pango_font_description_get_weight(got) >= PANGO_WEIGHT_BOLD) FcPatternAddInteger(pat, FC_WEIGHT, FC_WEIGHT_BOLD);
        FcConfigSubstitute(NULL, pat, FcMatchPattern); FcDefaultSubstitute(pat);
        if ((m = FcFontMatch(NULL, pat, &r))) { FcChar8 *fl; if (FcPatternGetString(m, FC_FILE, 0, &fl) == FcResultMatch) snprintf(file, sizeof file, "%s", (char *)fl); FcPatternDestroy(m); }
        FcPatternDestroy(pat);
        char *gs = pango_font_description_to_string(got);
        fprintf(stderr, "luxwm: caption font \"%s\" -> \"%s\" (%s); fonts = %s; font_files: %s\n", name, gs, file,
                fonts_reg[0] ? fonts_reg : "(none)", strchr(fonts_applied, '|') && strchr(fonts_applied, '|')[1] ? strchr(fonts_applied, '|') + 1 : "(none)");
        g_free(gs); pango_font_description_free(got); g_object_unref(f);
    }
    pango_font_description_free(fd); g_object_unref(ctx);
}

static unsigned theme_gen_next;   /* theme_load count: identifies a loaded theme for the crossfade cache */
/* "DIR|STYLE|COLOR|SIZE" -> its four fields (missing ones empty) */
static void spec_split(const char *spec, char *dir, char *style, char *color, char *size) {
    char *out[4] = { dir, style, color, size }; size_t cap[4] = { 1024, 1024, 64, 64 };
    for (int i = 0; i < 4; i++) out[i][0] = 0;
    for (int i = 0; i < 4 && spec; i++) {
        const char *bar = strchr(spec, '|'); size_t n = bar ? (size_t)(bar - spec) : strlen(spec);
        snprintf(out[i], cap[i], "%.*s", (int)n, spec);
        spec = bar ? bar + 1 : NULL;
    }
}

int theme_load(struct theme *t, const char *spec) {
    char dir[1024], style[1024], color[64], size[64], ini_style[1024] = "", ini_color[64] = "", ini_size[64] = "";
    spec_split(spec, dir, style, color, size);
    { size_t n = strlen(dir); while (n > 1 && dir[n - 1] == '/') dir[--n] = 0; }
    memset(t, 0, sizeof *t); memset(ms_vars, 0, sizeof ms_vars);   /* runtime theme switches load again */
    ms_aa_set = ms_hint_set = 0;
    t->gen = ++theme_gen_next;
    t->pi_t = 27; t->pi_r = 9; t->pi_b = 9; t->pi_l = 9; t->m_t = 27; t->m_r = 9; t->m_b = 9; t->m_l = 9; t->oi_t = t->oi_r = t->oi_b = t->oi_l = 30;
    t->oo_t = t->oo_b = 3; t->oo_r = t->oo_l = 4; t->btn_h = 14; t->btn_top = 2; t->btn_right = 5; t->btn_gap = 0; t->title_x = 28; t->icon_x = 8; t->icon_size = 16;
    strcpy(t->font, "Sans 9"); t->text_i[0] = t->text_i[1] = t->text_i[2] = 0.27; t->glow[0] = t->glow[1] = t->glow[2] = 1;
    t->order[0] = BTN_CLOSE; t->order[1] = BTN_MAX; t->order[2] = BTN_MIN; t->norder = 3;
    t->shape_alpha = -1; t->ms_border = 1; t->ms_cap_h = 18; t->ms_font_aa = CAIRO_ANTIALIAS_SUBPIXEL; strcpy(t->ms_font, "Tahoma Bold 11px");
    t->ms = calloc(MP_LAST, sizeof *t->ms); t->ext_r0 = 2; t->ext_r1 = -1;
    t->xi_t = -1; t->x_r0 = t->x_r1 = t->x_line = -1; t->glass_alpha = 1;
    t->mi_t = t->mxi_t = -1; t->mx_r0 = t->mx_r1 = t->mx_line = -1; t->mbtn_right = t->mbtn_top = -1000; t->overlay_on = 1;
    t->cap_border = -1;
    char p[4200], fonts[400] = "", xplate[400] = "", mplate[400] = "", mxplate[400] = "", defic[400] = "", bfiles[400] = ""; lhde_data_find_in(dir, "theme.ini", p, sizeof p);
    int set_ma = 0, set_mi = 0, set_gm = 0;
    FILE *f = dir[0] ? fopen(p, "r") : NULL;
    if (!dir[0]) t->mode = THEME_MSSTYLES;   /* a style without a directory: classic frames, tuning keys at their defaults */
    else if (!f) fprintf(stderr, "luxwm: no %s\n", p);
    if (f) {
        char line[512], key[64], val[400];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || sscanf(line, " %63[^= ] = %399[^\n]", key, val) != 2) continue;
            if (!strcmp(key, "plate_insets")) sscanf(val, "%d %d %d %d", &t->pi_t, &t->pi_r, &t->pi_b, &t->pi_l);
            else if (!strcmp(key, "margins")) sscanf(val, "%d %d %d %d", &t->m_t, &t->m_r, &t->m_b, &t->m_l);
            else if (!strcmp(key, "overlay_insets")) sscanf(val, "%d %d %d %d", &t->oi_t, &t->oi_r, &t->oi_b, &t->oi_l);
            else if (!strcmp(key, "overlay_outset")) { int n = sscanf(val, "%d %d %d %d", &t->oo_t, &t->oo_r, &t->oo_b, &t->oo_l); if (n == 1) t->oo_r = t->oo_b = t->oo_l = t->oo_t; }
            else if (!strcmp(key, "button_frame_h")) t->btn_h = atoi(val);
            else if (!strcmp(key, "button_top")) t->btn_top = atoi(val);
            else if (!strcmp(key, "button_right")) t->btn_right = atoi(val);
            else if (!strcmp(key, "button_gap")) t->btn_gap = atoi(val);
            else if (!strcmp(key, "title_x")) t->title_x = atoi(val);
            else if (!strcmp(key, "icon_x")) t->icon_x = atoi(val);
            else if (!strcmp(key, "icon_size")) t->icon_size = atoi(val);
            else if (!strcmp(key, "font")) { strncpy(t->font, val, 63); }
            else if (!strcmp(key, "text_active")) parse_color(val, t->text_a);
            else if (!strcmp(key, "text_inactive")) parse_color(val, t->text_i);
            else if (!strcmp(key, "glow")) parse_color(val, t->glow);
            else if (!strcmp(key, "mode")) t->mode = !strcmp(val, "msstyles") ? THEME_MSSTYLES : THEME_DWM;
            else if (!strcmp(key, "shape_alpha")) t->shape_alpha = atoi(val);
            else if (!strcmp(key, "extend_rows")) sscanf(val, "%d %d", &t->ext_r0, &t->ext_r1);
            else if (!strcmp(key, "plate_explorer")) snprintf(xplate, sizeof xplate, "%s", val);
            else if (!strcmp(key, "explorer_insets")) sscanf(val, "%d %d %d %d", &t->xi_t, &t->xi_r, &t->xi_b, &t->xi_l);
            else if (!strcmp(key, "explorer_rows")) sscanf(val, "%d %d %d", &t->x_r0, &t->x_r1, &t->x_line);
            else if (!strcmp(key, "header_mode")) t->x_crop = !strcmp(val, "crop");
            else if (!strcmp(key, "deco_h")) t->deco_h = atoi(val);
            else if (!strcmp(key, "caption_border")) t->cap_border = atoi(val);
            else if (!strcmp(key, "button_files")) snprintf(bfiles, sizeof bfiles, "%s", val);
            else if (!strcmp(key, "fog")) t->fog_on = atoi(val) != 0;
            else if (!strcmp(key, "overlay")) t->overlay_on = atoi(val) != 0;
            else if (!strcmp(key, "plate_max")) snprintf(mplate, sizeof mplate, "%s", val);
            else if (!strcmp(key, "plate_max_explorer")) snprintf(mxplate, sizeof mxplate, "%s", val);
            else if (!strcmp(key, "max_insets")) sscanf(val, "%d %d %d %d", &t->mi_t, &t->mi_r, &t->mi_b, &t->mi_l);
            else if (!strcmp(key, "max_explorer_insets")) sscanf(val, "%d %d %d %d", &t->mxi_t, &t->mxi_r, &t->mxi_b, &t->mxi_l);
            else if (!strcmp(key, "max_explorer_rows")) sscanf(val, "%d %d %d", &t->mx_r0, &t->mx_r1, &t->mx_line);
            else if (!strcmp(key, "max_button_right")) t->mbtn_right = atoi(val);
            else if (!strcmp(key, "max_button_top")) t->mbtn_top = atoi(val);
            else if (!strcmp(key, "glass_alpha")) { t->glass_alpha = atof(val); if (t->glass_alpha < 0) t->glass_alpha = 0; if (t->glass_alpha > 1) t->glass_alpha = 1; }
            else if (!strcmp(key, "glass_alpha_mode")) t->glass_square = !strcmp(val, "square");
            else if (!strcmp(key, "fallback_theme")) snprintf(t->fallback, sizeof t->fallback, "%.255s", val);
            else if (!strcmp(key, "font_files")) snprintf(fonts, sizeof fonts, "%s", val);
            else if (!strcmp(key, "text_max_active")) { parse_color(val, t->text_ma); set_ma = 1; }
            else if (!strcmp(key, "text_max_inactive")) { parse_color(val, t->text_mi); set_mi = 1; }
            else if (!strcmp(key, "glow_max")) { t->glow_m_none = !strcmp(val, "none"); if (!t->glow_m_none) parse_color(val, t->glow_m); set_gm = 1; }
            else if (!strcmp(key, "default_icon")) snprintf(defic, sizeof defic, "%s", val);
            /* the visual style this directory caches (README "libuxtheme source"); relative to the data directories (styles/NAME) */
            else if (!strcmp(key, "msstyles")) lhde_data_resolve(val, ini_style, sizeof ini_style);
            else if (!strcmp(key, "color")) snprintf(ini_color, sizeof ini_color, "%.63s", val);
            else if (!strcmp(key, "size")) snprintf(ini_size, sizeof ini_size, "%.63s", val);
            else if (strchr(key, '.') || !strncmp(key, "caption", 7) || !strcmp(key, "border_width") || !strncmp(key, "text_", 5)) ms_key(t, key, val);
            else if (!strcmp(key, "buttons")) {
                t->norder = 0; char *s = val, *tok;
                while ((tok = strtok(s, " ")) && t->norder < 3) { s = NULL;
                    t->order[t->norder++] = !strcmp(tok, "close") ? BTN_CLOSE : !strcmp(tok, "max") ? BTN_MAX : BTN_MIN; }
            }
        }
        fclose(f);
    }
    fonts_apply(fonts);
    /* the source: the style (the spec's, else theme.ini's) through libuxtheme, else the directory's PNG cache */
    if (!style[0]) { snprintf(style, sizeof style, "%s", ini_style); snprintf(color, sizeof color, "%s", ini_color); snprintf(size, sizeof size, "%s", ini_size); }
    snprintf(t->style, sizeof t->style, "%s", style); snprintf(t->style_color, sizeof t->style_color, "%s", color);
    snprintf(t->style_size, sizeof t->style_size, "%s", size);
    if (style[0] && frame_theme_source != FRAME_SRC_DIR && fut_available()) {
        if (!fut_open(t, style, color, size) && (frame_theme_source == FRAME_SRC_LIB || !dir[0])) return 0;
        if (!t->ut) fprintf(stderr, "luxwm: %s: using the PNG cache in %s\n", style, dir);
    } else if (frame_theme_source == FRAME_SRC_LIB || !dir[0]) {
        fprintf(stderr, "luxwm: %s: %s\n", spec, !style[0] ? "no msstyles named" : !fut_available() ? "lhwm was built without libuxtheme" : "library source disabled");
        return 0;
    }
    if (t->ut && t->mode == THEME_MSSTYLES && !fut_ms_load(t, ms_aa_set, ms_hint_set)) {
        fprintf(stderr, "luxwm: %s has no Window.CAPTION\n", t->ut_src); return 0; }
    if (!set_ma) memcpy(t->text_ma, t->text_a, sizeof t->text_ma);
    if (!set_mi) memcpy(t->text_mi, t->text_i, sizeof t->text_mi);
    if (!set_gm) memcpy(t->glow_m, t->glow, sizeof t->glow_m);
    {   /* default_icon = FILE [FILE...] (a bare name: in the frame set, beside theme.ini or in its imported pixels; a path with a
           '/': relative to the data directories, e.g. packs/4074/shell32/16/3.png), else default_icon.png when it exists */
        char buf[400]; snprintf(buf, sizeof buf, "%s", defic[0] ? defic : "default_icon.png");
        for (char *save = NULL, *fn = strtok_r(buf, " ", &save); fn && t->ndefic < 4; fn = strtok_r(NULL, " ", &save)) {
            char q[4200]; if (strchr(fn, '/')) lhde_data_resolve(fn, q, sizeof q); else lhde_data_find_in(dir, fn, q, sizeof q);
            cairo_surface_t *ic = !defic[0] ? fut_png(t, fn) : NULL;   /* DEFAULTICON from the style */
            if (ic) { t->defic[t->ndefic++] = ic; continue; }
            if (!dir[0] || (!defic[0] && access(q, R_OK) != 0)) break;
            ic = cairo_image_surface_create_from_png(q);
            if (cairo_surface_status(ic) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(ic); fprintf(stderr, "luxwm: default_icon: cannot load %s\n", q); continue; }
            t->defic[t->ndefic++] = ic;
        }
    }
    if (t->ext_r1 < 0) t->ext_r1 = t->m_t - 2;
    if (t->xi_t < 0) { t->xi_t = t->pi_t; t->xi_r = t->pi_r; t->xi_b = t->pi_b; t->xi_l = t->pi_l; }
    if (t->x_r0 < 0) t->x_r0 = t->ext_r0;
    if (t->x_r1 < 0) t->x_r1 = t->xi_t - 2;
    if (t->x_line < 0) t->x_line = t->x_r1;
    if (t->mi_t < 0) { t->mi_t = t->pi_t; t->mi_r = t->pi_r; t->mi_b = t->pi_b; t->mi_l = t->pi_l; }
    if (t->mxi_t < 0) { t->mxi_t = t->mi_t; t->mxi_r = t->mi_r; t->mxi_b = t->mi_b; t->mxi_l = t->mi_l; }
    if (t->mx_r0 < 0) t->mx_r0 = 2;
    if (t->mx_r1 < 0) t->mx_r1 = t->mxi_t - 2;
    if (t->mx_line < 0) t->mx_line = t->mx_r1;
    if (t->mbtn_right == -1000) t->mbtn_right = t->btn_right;
    if (t->mbtn_top == -1000) t->mbtn_top = t->btn_top;
    if (t->shape_alpha < 0) t->shape_alpha = t->mode == THEME_MSSTYLES ? 1 : 32;
    if (t->mode == THEME_MSSTYLES) {
        int ok = t->ut ? 1 : ms_load(t, dir);   /* the library source filled t->ms in fut_ms_load */
        t->m_t = 3 + t->ms_border + t->ms_cap_h + 1; t->m_l = t->m_r = t->m_b = 3 + t->ms_border;
        return ok;
    }
    t->plate[0] = theme_png(t, dir, "plate_active.png"); t->plate[1] = theme_png(t, dir, "plate_inactive.png");
    t->overlay = theme_png(t, dir, "overlay.png"); t->fog = theme_png(t, dir, "captionfog.png");
    t->glow_btn = theme_png(t, dir, "glow_btn.png"); t->glow_close = theme_png(t, dir, "glow_close.png");
    /* button_files = CLOSE MAX MIN RESTORE: sprite files of the theme's own (names other than close.png etc., which the style's
       streams replace); a missing entry keeps the default name */
    { char bf[4][100] = { "close.png", "max.png", "min.png", "restore.png" };
      if (bfiles[0]) sscanf(bfiles, "%99s %99s %99s %99s", bf[0], bf[1], bf[2], bf[3]);
      t->btn[BTN_CLOSE] = theme_png(t, dir, bf[0]); t->btn[BTN_MAX] = theme_png(t, dir, bf[1]);
      t->btn[BTN_MIN] = theme_png(t, dir, bf[2]); t->btn[BTN_RESTORE] = theme_png(t, dir, bf[3]); }
    /* plate_explorer / plate_max / plate_max_explorer = ACTIVE.png [INACTIVE.png]: without a second file the active one serves both */
    const char *lists[3] = { xplate, mplate, mxplate }; cairo_surface_t **dst[3] = { t->plate_x, t->plate_m, t->plate_mx };
    for (int i = 0; i < 3; i++) {
        if (!lists[i][0]) continue;
        char a[200] = "", b[200] = ""; sscanf(lists[i], "%199s %199s", a, b);
        dst[i][0] = theme_png(t, dir, a); dst[i][1] = b[0] ? theme_png(t, dir, b) : NULL;
        if (dst[i][0] && !dst[i][1]) dst[i][1] = cairo_surface_reference(dst[i][0]);
    }
    if (!t->overlay_on && t->overlay) { cairo_surface_destroy(t->overlay); t->overlay = NULL; }
    return t->plate[0] != NULL;
}

void theme_free(struct theme *t) {
    cairo_surface_t **all[] = { &t->plate[0], &t->plate[1], &t->plate_x[0], &t->plate_x[1], &t->plate_m[0], &t->plate_m[1],
                                &t->plate_mx[0], &t->plate_mx[1], &t->overlay, &t->fog, &t->glow_btn, &t->glow_close,
                                &t->btn[0], &t->btn[1], &t->btn[2], &t->btn[3] };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) if (*all[i]) { cairo_surface_destroy(*all[i]); *all[i] = NULL; }
    for (int i = 0; i < t->ndefic; i++) cairo_surface_destroy(t->defic[i]);
    t->ndefic = 0;
    if (t->ms) {
        for (int p = 0; p < MP_LAST; p++) for (int v = 0; v < 6; v++) for (int k = 0; k < 10; k++) if (t->ms[p].img[v][k]) cairo_surface_destroy(t->ms[p].img[v][k]);
        free(t->ms); t->ms = NULL;
    }
    fut_close(t);
}

void theme_margins(const struct theme *t, int *l, int *r, int *top, int *b) { theme_margins_ex(t, 0, l, r, top, b); }
void theme_margins_ex(const struct theme *t, int maximized, int *l, int *r, int *top, int *b) {
    if (t->mode == THEME_MSSTYLES) {   /* cnBorders = 3 + SIZINGBORDERWIDTH on all sides, caption = cnBorders + CAPTIONBARHEIGHT + 1;
                                          full-screen maximised: the borders lie off-screen, only CAPTIONBARHEIGHT + 1 remains */
        int cnb = 3 + t->ms_border;
        if (maximized) { *l = *r = *b = 0; *top = t->ms_cap_h + 1; }
        else { *l = *r = *b = cnb; *top = cnb + t->ms_cap_h + 1; }
        return;
    }
    if (maximized) { *l = *r = *b = 0; *top = t->m_t; return; }   /* DWM maximised: the caption stays, side/bottom borders go */
    *l = t->m_l; *r = t->m_r; *top = t->m_t; *b = t->m_b;
}

/* ---- theme registry (themes.ini): plain ini, one section per theme name (THEMES.md) ---- */
static char *reg_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}
/* lhde: a registry value is absolute (a resolved copy, lhde_data.h) or relative to the data directories (styles/..., fonts/...);
   a frame directory is the one with the frame set's theme.ini (its pixels are found from there, load_png) */
static void reg_path(char *out, size_t n, const char *ini, const char *v) { (void)ini; lhde_data_resolve(v, out, n); }
static void reg_dir(char *out, size_t n, const char *v) { lhde_data_resolve_dir(v, "theme.ini", out, n); }
int theme_registry_lookup(const char *ini, const char *name, struct theme_reg *r) {
    FILE *f = ini && ini[0] ? fopen(ini, "r") : NULL;
    if (!f || !name || !name[0]) { if (f) fclose(f); return 0; }
    char line[1280], sec[64] = "", frame[1024] = "", classic[1024] = "", aliases[256] = "", fonts[1024] = "", ms[1024] = "", color[64] = "",
         size[64] = ""; int off = 0, found = 0;
    for (int eof = 0; !found && !eof; ) {
        char *s = NULL;
        if (fgets(line, sizeof line, f)) s = reg_trim(line); else eof = 1;
        if (eof || s[0] == '[') {   /* end of a section: does it match? */
            if (sec[0]) {
                int hit = !strcmp(sec, name);
                for (char *a = aliases, *tok; !hit && (tok = strtok(a, " \t,")); a = NULL) hit = !strcmp(tok, name);
                if (hit) {
                    memset(r, 0, sizeof *r); snprintf(r->name, sizeof r->name, "%s", sec); r->comp_off = off;
                    if (frame[0]) reg_dir(r->frame, sizeof r->frame, frame);
                    /* frame_classic = style (or empty): the theme's own msstyles in msstyles mode; kept as the word (wm.c) */
                    if (!strcmp(classic, "style")) snprintf(r->frame_classic, sizeof r->frame_classic, "style");
                    else if (classic[0]) reg_dir(r->frame_classic, sizeof r->frame_classic, classic);
                    if (ms[0]) reg_path(r->msstyles, sizeof r->msstyles, ini, ms);
                    snprintf(r->color, sizeof r->color, "%s", color); snprintf(r->size, sizeof r->size, "%s", size);
                    /* fonts = DIR[;DIR...]: each resolved against the registry's directory, ';' separated again */
                    for (char *save = NULL, *tok = strtok_r(fonts, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
                        char *tk = reg_trim(tok), one[1024]; if (!*tk) continue;
                        reg_path(one, sizeof one, ini, tk); size_t l = strlen(r->fonts);
                        snprintf(r->fonts + l, sizeof r->fonts - l, "%s%s", l ? ";" : "", one);
                    }
                    found = 1; break;
                }
            }
            if (eof) break;
            char *e = strchr(s, ']'); if (e) *e = 0;
            snprintf(sec, sizeof sec, "%s", reg_trim(s + 1)); frame[0] = classic[0] = aliases[0] = fonts[0] = ms[0] = color[0] = size[0] = 0; off = 0;
            continue;
        }
        if (!s[0] || s[0] == ';' || s[0] == '#') continue;
        char *eq = strchr(s, '='); if (!eq) continue;
        *eq = 0; char *k = reg_trim(s), *v = reg_trim(eq + 1);
        if (*v) for (char *c = v + 1; *c; c++) if ((*c == ';' || *c == '#') && (c[-1] == ' ' || c[-1] == '\t')) { *c = 0; v = reg_trim(v); break; }
        if (!strcmp(k, "frame")) snprintf(frame, sizeof frame, "%s", v);
        else if (!strcmp(k, "frame_classic")) snprintf(classic, sizeof classic, "%s", v);
        else if (!strcmp(k, "aliases")) snprintf(aliases, sizeof aliases, "%s", v);
        else if (!strcmp(k, "fonts")) snprintf(fonts, sizeof fonts, "%s", v);
        else if (!strcmp(k, "msstyles")) snprintf(ms, sizeof ms, "%s", v);
        else if (!strcmp(k, "color")) snprintf(color, sizeof color, "%s", v);
        else if (!strcmp(k, "size")) snprintf(size, sizeof size, "%s", v);
        else if (!strcmp(k, "composition")) off = !strcmp(v, "off");
    }
    fclose(f);
    return found;
}

/* ---- user presets (themes/NAME.theme, ../SETTINGS.md) ---- */
/* a preset value as a path: absolute, ~/..., else relative to the preset file's directory */
static void preset_path_of(char *out, size_t n, const char *pdir, const char *v) {
    const char *home = getenv("HOME");
    if (v[0] == '/') snprintf(out, n, "%s", v);
    else if (v[0] == '~' && v[1] == '/') snprintf(out, n, "%s%s", home ? home : "", v + 1);
    else snprintf(out, n, "%s/%s", pdir, v);
}
/* the overrides lhwm uses, from either source; kv: key -> value (paths absolute already) */
static void preset_apply(struct theme_reg *r, const char *k, const char *v) {
    if (!v[0]) return;   /* empty = not set: the base's value */
    if (!strcmp(k, "msstyles")) snprintf(r->msstyles, sizeof r->msstyles, "%s", v);
    else if (!strcmp(k, "color")) snprintf(r->color, sizeof r->color, "%s", v);
    else if (!strcmp(k, "size")) snprintf(r->size, sizeof r->size, "%s", v);
    else if (!strcmp(k, "frame")) snprintf(r->frame, sizeof r->frame, "%s", v);
    else if (!strcmp(k, "frame_classic")) snprintf(r->frame_classic, sizeof r->frame_classic, "%s", v);
    else if (!strcmp(k, "fonts")) snprintf(r->fonts, sizeof r->fonts, "%s", v);
    else if (!strcmp(k, "composition")) r->comp_off = !strcmp(v, "off") || !strcmp(v, "0");   /* on / auto: glass while composited */
}
int theme_preset_lookup(const char *ini, const char *name, const char *resolved, struct theme_reg *r, char *src, size_t nsrc,
                        char *anim, size_t nanim) {
    if (anim && nanim) anim[0] = 0;
    if (src && nsrc) src[0] = 0;
    if (!name || !name[0] || name[0] == '.' || strchr(name, '/') || strlen(name) > 200) return 0;
    char path[1400], pdir[1200];
    const char *x = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (x && *x) snprintf(pdir, sizeof pdir, "%.1100s/longhorn/themes", x);
    else snprintf(pdir, sizeof pdir, "%.1100s/.config/longhorn/themes", home ? home : ".");
    snprintf(path, sizeof path, "%s/%s.theme", pdir, name);
    /* the preset file: [Theme] base and the overrides (sections and keys case-insensitive, as lux and flxp read them) */
    char base[256] = "", sec[64] = "", line[2400];
    char vals[7][2048] = { "", "", "", "", "", "", "" };
    static const char *const keys[7] = { "msstyles", "color", "size", "frame", "frame_classic", "fonts", "composition" };
    static const char *const secs[7] = { "visualstyle", "visualstyle", "visualstyle", "frames", "frames", "fonts", "composition" };
    FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char *s = reg_trim(line);
            if (s[0] == '[') { char *e = strchr(s, ']'); if (e) *e = 0; snprintf(sec, sizeof sec, "%s", reg_trim(s + 1)); continue; }
            if (!s[0] || s[0] == ';' || s[0] == '#') continue;
            char *eq = strchr(s, '='); if (!eq) continue;
            *eq = 0; char *k = reg_trim(s), *v = reg_trim(eq + 1);
            if (*v) for (char *c = v + 1; *c; c++) if ((*c == ';' || *c == '#') && (c[-1] == ' ' || c[-1] == '\t')) { *c = 0; v = reg_trim(v); break; }
            if (!strcasecmp(sec, "theme") && !strcasecmp(k, "base")) snprintf(base, sizeof base, "%s", v);
            else if (!strcasecmp(sec, "composition") && !strcasecmp(k, "animations")) { if (anim && nanim) snprintf(anim, nanim, "%s", v); }
            else for (int i = 0; i < 7; i++)
                if (!strcasecmp(sec, secs[i]) && !strcasecmp(k, keys[i])) { snprintf(vals[i], sizeof vals[i], "%s", v); break; }
        }
        fclose(f);
    }
    /* lux's resolved view of the same name wins (its values are the effective ones, paths absolute) */
    if (resolved && resolved[0]) {
        char items[17][2048]; char ik[17][32]; int ni = 0; size_t o = 0; int esc = 0;
        char cur[2048];
        for (const char *p = resolved; ; p++) {
            if (!*p || (*p == ';' && !esc)) {
                cur[o] = 0; char *eq = strchr(cur, '=');
                if (eq && ni < 17) { *eq = 0; snprintf(ik[ni], sizeof ik[ni], "%.31s", cur); snprintf(items[ni], sizeof items[ni], "%s", eq + 1); ni++; }
                o = 0; esc = 0;
                if (!*p) break;
                continue;
            }
            if (*p == '\\' && !esc) { esc = 1; continue; }
            esc = 0;
            if (o < sizeof cur - 1) cur[o++] = *p;
        }
        const char *rn = NULL, *rb = NULL;
        for (int i = 0; i < ni; i++) { if (!strcmp(ik[i], "name")) rn = items[i]; else if (!strcmp(ik[i], "base")) rb = items[i]; }
        if (rn && rb && rb[0] && !strcmp(rn, name) && theme_registry_lookup(ini, rb, r)) {
            for (int i = 0; i < ni; i++) preset_apply(r, ik[i], items[i]);
            if (src && nsrc) snprintf(src, nsrc, "_LH_THEME_RESOLVED");
            return 1;
        }
    }
    if (!f) return 0;
    if (!base[0] || !theme_registry_lookup(ini, base, r)) {
        fprintf(stderr, "luxwm: preset %s: base '%s' is not in %s\n", path, base, ini ? ini : "(no registry)");
        return 0;
    }
    for (int i = 0; i < 7; i++) {
        if (!vals[i][0]) continue;
        char abs[2048];
        if (i == 4 && !strcmp(vals[i], "style")) preset_apply(r, keys[i], vals[i]);   /* frame_classic = style: the theme's own msstyles */
        else if (i == 0 || i == 3 || i == 4) { preset_path_of(abs, sizeof abs, pdir, vals[i]); preset_apply(r, keys[i], abs); }
        else if (i == 5) {   /* fonts = DIR[;DIR...], each resolved against the preset's directory */
            char all[2048] = "";
            for (char *save = NULL, *tok = strtok_r(vals[i], ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
                char *tk = reg_trim(tok), one[1024]; if (!*tk) continue;
                preset_path_of(one, sizeof one, pdir, tk); size_t l = strlen(all);
                snprintf(all + l, sizeof all - l, "%s%s", l ? ";" : "", one);
            }
            preset_apply(r, "fonts", all);
        } else preset_apply(r, keys[i], vals[i]);
    }
    if (src && nsrc) snprintf(src, nsrc, "%s", path);
    return 1;
}

/* one grid cell: the source rectangle (sx,sy,sw,sh) of img onto the destination (dx,dy,dw,dh), bilinear. The cell samples only
   its own source rectangle (a sub-surface, PAD at its edges): before 2026-10-27 the whole texture was the source, so a stretched
   cell blended its neighbour's first row or column into its own edge (aero at w = 1280: the column left of the 1:1 right 100 px
   carried up to 19 levels of the pill's left edge, the pill looked a pixel wider; lhshell's nine() had the same fix for its strips).
   1:1 cells are unchanged (integer offsets) */
static void cell_paint(cairo_t *cr, cairo_surface_t *img, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    cairo_surface_t *piece = cairo_surface_create_for_rectangle(img, sx, sy, sw, sh);
    cairo_save(cr); cairo_rectangle(cr, dx, dy, dw, dh); cairo_clip(cr);
    cairo_translate(cr, dx, dy); cairo_scale(cr, dw / sw, dh / sh);
    cairo_set_source_surface(cr, piece, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_paint(cr); cairo_restore(cr);
    cairo_surface_destroy(piece);   /* the pattern held its own reference */
}

/* 9-slice img over (x,y,w,h) with insets t,r,b,l taken from the image edges */
static void nine(cairo_t *cr, cairo_surface_t *img, double x, double y, double w, double h, int it, int ir, int ib, int il) {
    if (!img) return;
    double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
    double sx[4] = {0, il, iw - ir, iw}, sy[4] = {0, it, ih - ib, ih};
    double dx[4] = {x, x + il, x + w - ir, x + w}, dy[4] = {y, y + it, y + h - ib, y + h};
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++)
        cell_paint(cr, img, sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j], dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]);
}

/* button rectangles, laid out from the right edge in theme order; which = enum btn slot index into order[] */
static int button_rect(const struct theme *t, int w, int idx, int maximized, int *bx, int *by, int *bw, int *bh) {
    int x = w - (maximized ? t->mbtn_right : t->btn_right);
    for (int i = 0; i < t->norder; i++) {
        int b = t->order[i]; if (b == BTN_MAX && maximized) b = BTN_RESTORE;
        cairo_surface_t *s = t->btn[b]; int sw = s ? cairo_image_surface_get_width(s) : 25;
        x -= sw + t->btn_gap;   /* cell = sprite + button_gap extra columns (see paint) */
        if (i == idx) { *bx = x; *by = maximized ? t->mbtn_top : t->btn_top; *bw = sw + t->btn_gap; *bh = t->btn_h; return b; }
    }
    return -1;
}
static int hit_of_slot(const struct theme *t, int i) { int b = t->order[i]; return b == BTN_CLOSE ? HIT_CLOSE : b == BTN_MAX ? HIT_MAX : HIT_MIN; }

/* the shape of the decoration window: rows near the top and bottom lose their leading/trailing pixels whose alpha is below
   shape_alpha (the plate's / caption's rounded corners). Without this a compositor blurs and shadows the full rectangle, so the
   square corner shows through around the rounded plate, and without a compositor those pixels are not transparent at all. */
int frame_shape_rects(const struct theme *t, cairo_surface_t *img, int maximized, XRectangle *r, int max) {
    cairo_surface_flush(img);
    int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img), st = cairo_image_surface_get_stride(img);
    const unsigned char *d = cairo_image_surface_get_data(img); int n = 0, zone = 24, lim = 40, prev_l = -1, prev_r = -1;
    /* the plate is painted with glass_alpha (and squared): compare against the threshold mapped the same way, so the cut follows the
       texture's own corners whatever the glass weight */
    int sa = t->shape_alpha;
    if (t->mode == THEME_DWM && (t->glass_alpha < 1 || t->glass_square)) {
        double a = sa / 255.0; if (t->glass_square) a *= a;
        sa = (int)(a * t->glass_alpha * 255 + 0.5);
    }
    if (t->mode == THEME_DWM && maximized) {   /* square corners: the full rectangle */
        if (max < 1) return -1;
        r[0].x = 0; r[0].y = 0; r[0].width = w; r[0].height = h; return 1;
    }
    /* msstyles: uxtheme builds the window region from the TRANSPARENT caption bitmap with its margin areas kept at their
       source positions (the rounded corners are not stretched with the image), so take the top rows 1:1 from the source */
    const unsigned char *sd = NULL; int sw = 0, sh = 0, sst = 0, srows = 0;
    if (t->mode == THEME_MSSTYLES) {
        zone = 0;
        const struct mspart *c = &t->ms[MP_CAPTION];
        if (!maximized && c->img[0][0]) {
            cairo_surface_t *s = c->img[0][0]; cairo_surface_flush(s);
            sd = cairo_image_surface_get_data(s); sw = cairo_image_surface_get_width(s); sh = cairo_image_surface_get_height(s);
            sst = cairo_image_surface_get_stride(s); srows = c->sm[2] < sh ? c->sm[2] : sh;
            if (srows > 3 + t->ms_border + t->ms_cap_h + 1) srows = 3 + t->ms_border + t->ms_cap_h + 1;
        }
    }
    for (int y = 0; y < h; y++) {
        int l = 0, rr = 0;
        const uint32_t *row = NULL; int rw = w;
        if (y < srows) { row = (const uint32_t *)(sd + y * sst); rw = sw; }
        else if (y < zone || y >= h - zone) row = (const uint32_t *)(d + y * st);
        if (row) {
            while (l < lim && l < rw && (int)(row[l] >> 24) < sa) l++;
            while (rr < lim && rr < rw - l && (int)(row[rw - 1 - rr] >> 24) < sa) rr++;
            if (l == lim) l = 0;
            if (rr == lim) rr = 0;
        }
        if (n && l == prev_l && rr == prev_r) { r[n - 1].height++; continue; }
        if (n == max) return -1;
        r[n].x = l; r[n].y = y; r[n].width = w - l - rr; r[n].height = 1; n++; prev_l = l; prev_r = rr;
    }
    return n;
}

/* square the two top corners of a painted DWM plate/overlay (maximised frames): inside the k x k corner square every pixel is
   taken from the straight edge next to it, mitred along the diagonal - nearer the side: the side edge's row k, otherwise the
   top edge's column k. k = 6 covers the plate's ~4 px corner radius and the overlay highlight's arc, and stays clear of the
   aero caption-button pill (it ends 7 px from the right edge). */
static void square_corners(cairo_surface_t *img, int k) {
    cairo_surface_flush(img);
    int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img), st = cairo_image_surface_get_stride(img);
    unsigned char *d = cairo_image_surface_get_data(img);
    if (w <= 2 * k + 1 || h <= k) return;
    #define PX(x, y) (((uint32_t *)(d + (y) * st))[x])
    for (int y = 0; y < k; y++) for (int i = 0; i < k; i++) {
        PX(i, y) = i < y ? PX(i, k) : PX(k, y);
        PX(w - 1 - i, y) = i < y ? PX(w - 1 - i, k) : PX(w - 1 - k, y);
    }
    #undef PX
    cairo_surface_mark_dirty(img);
}

void frame_paint_cr2(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover, int pressed, int maximized, const struct frame_ext *ext);
static void plate_ext(cairo_t *cr, const struct theme *t, cairo_surface_t *img, double w, double h, const struct frame_ext *e);
static void plate_explorer(cairo_t *cr, const struct theme *t, cairo_surface_t *img, double w, double h, const struct frame_ext *e);
static void plate_max(cairo_t *cr, const struct theme *t, cairo_surface_t *img, double w, double h, const struct frame_ext *e, int explorer);

/* M5b crossfade: the three layers that only change in weight between the frames of a fade (normal plate, maximised plate, overlay
   masked by the normal plate) are rendered once into w x h images and kept here; freed by the next paint outside a fade */
static struct fcache { unsigned gen; int w, h, foc, max, l, r, tp, b, sheet; cairo_pattern_t *p; } fade_cache[3];
static void fade_cache_clear(void) {
    for (int i = 0; i < 3; i++) if (fade_cache[i].p) { cairo_pattern_destroy(fade_cache[i].p); fade_cache[i].p = NULL; }
}
/* which: 0 normal plate (maximised: plate over w x (h + m_b), corners squared), 1 maximised plate, 2 overlay (needs 0) */
static cairo_pattern_t *fade_layer(const struct theme *t, int which, int w, int h, int focused, int maximized, const struct frame_ext *ext) {
    int extended = ext && (ext->l > 0 || ext->r > 0 || ext->t > 0 || ext->b > 0 || ext->sheet);
    struct fcache k = { t->gen, w, h, focused, maximized, ext ? ext->l : 0, ext ? ext->r : 0, ext ? ext->t : 0, ext ? ext->b : 0,
                        ext ? ext->sheet : 0, NULL };
    struct fcache *c = &fade_cache[which];
    if (c->p && !memcmp(c, &k, offsetof(struct fcache, p))) return c->p;
    cairo_pattern_t *np = which == 2 ? fade_layer(t, 0, w, h, focused, maximized, ext) : NULL;
    if (c->p) cairo_pattern_destroy(c->p);
    cairo_surface_t *ps = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); cairo_t *c2 = cairo_create(ps);
    int hh = maximized ? h + t->m_b : h;
    if (which == 0) {
        int explorer = extended && !ext->sheet && ext->t > 0 && t->plate_x[0];
        if (explorer) plate_explorer(c2, t, t->plate_x[focused ? 0 : 1], w, hh, ext);
        else if (extended) plate_ext(c2, t, t->plate[focused ? 0 : 1], w, hh, ext);
        else nine(c2, t->plate[focused ? 0 : 1], 0, 0, w, hh, t->pi_t, t->pi_r, t->pi_b, t->pi_l);
    } else if (which == 1) {
        int mexp = extended && !ext->sheet && ext->t > 0 && t->plate_mx[0];
        plate_max(c2, t, mexp ? t->plate_mx[focused ? 0 : 1] : t->plate_m[focused ? 0 : 1], w, h, ext, mexp);
    } else {
        nine(c2, t->overlay, -t->oo_l, -t->oo_t, w + t->oo_l + t->oo_r, hh + t->oo_t + t->oo_b, t->oi_t, t->oi_r, t->oi_b, t->oi_l);
        cairo_set_operator(c2, CAIRO_OPERATOR_DEST_IN); cairo_set_source(c2, np); cairo_paint(c2);
    }
    cairo_destroy(c2);
    if (maximized) square_corners(ps, 6);
    k.p = cairo_pattern_create_for_surface(ps); cairo_surface_destroy(ps);
    *c = k;
    return c->p;
}

/* ---- extended frames (CLIENT-PROTOCOL.md) ----
   One axis of the extended plate as 9 segments: outer | band | inner lines | inset | middle | inset | inner lines | band | outer.
   The near side's outer [0,o0), inner lines [a0,l0) and inset [l0,p0) stay 1:1, its band [o0,a0) is stretched by e0 extra pixels
   (lines and inset move in with it); the far side is mirrored (o1, a1, l1, p1 measured from the far edge). The lines are the frame
   margin's last two pixels (a = margin - 2, l = margin); the inset is the rest of the 9-slice inset, which may be wider than the
   margin: aero's right 100 px (the pill) against a 9 px margin. Until 2026-10-27 the inset counted as "inner lines", so a sheet of
   glass dropped the pill's 91 columns and stretched the 5 px band over the right 100 px. sheet: no inner lines, the bands absorb
   them (the whole client is glass). s and d hold 10 boundaries. */
static void ext_axis(double *s, double *d, double S, double D, int o0, int a0, int l0, int p0, int e0, int o1, int a1, int l1, int p1, int e1, int sheet) {
    if (l0 > p0) l0 = p0;
    if (a0 > l0) a0 = l0;
    if (o0 > a0) o0 = a0;
    if (l1 > p1) l1 = p1;
    if (a1 > l1) a1 = l1;
    if (o1 > a1) o1 = a1;
    s[0] = 0; s[1] = o0; s[2] = a0; s[3] = l0; s[4] = p0; s[5] = S - p1; s[6] = S - l1; s[7] = S - a1; s[8] = S - o1; s[9] = S;
    d[0] = 0; d[1] = o0; d[2] = a0 + e0; d[3] = l0 + e0; d[4] = p0 + e0; d[5] = D - p1 - e1; d[6] = D - l1 - e1; d[7] = D - a1 - e1; d[8] = D - o1; d[9] = D;
    if (sheet) { d[2] = d[3]; d[7] = d[6]; }
}

/* the columns of an extended plate: x (the side extensions, the rows below the caption band) and x0 (no side extension: the caption
   band's rows). The caption band keeps the layout of a normal frame, so its right inset (aero: the pill) stays 1:1 under the caption
   buttons, which do not move with an extension; the side bands, their inner lines and the inset move in only below it (the
   vertical inner lines of the side bands start below the header, as on the reference). Before 2026-10-27 a right extension of e px
   moved the pill e px left of the buttons and stretched the 5 px band beside it by e. */
static void ext_columns(const struct theme *t, double iw, double w, int pl, int pr, const struct frame_ext *e, double *sx, double *dx, double *sx0, double *dx0) {
    ext_axis(sx, dx, iw, w, 2, t->m_l - 2, t->m_l, pl, e->l, 2, t->m_r - 2, t->m_r, pr, e->r, e->sheet);
    ext_axis(sx0, dx0, iw, w, 2, t->m_l - 2, t->m_l, pl, 0, 2, t->m_r - 2, t->m_r, pr, 0, 0);
}

/* the plate with its bands stretched into the client: cells as nine() (cell_paint) on a 9x9 grid; the top's outer rows and band
   (y segments 0 and 1, the caption band) take the columns without side extension */
static void plate_ext(cairo_t *cr, const struct theme *t, cairo_surface_t *img, double w, double h, const struct frame_ext *e) {
    if (!img) return;
    double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img), sx[10], dx[10], sx0[10], dx0[10], sy[10], dy[10];
    ext_columns(t, iw, w, t->pi_l, t->pi_r, e, sx, dx, sx0, dx0);
    ext_axis(sy, dy, ih, h, t->ext_r0, t->ext_r1, t->pi_t, t->pi_t, e->t, 2, t->m_b - 2, t->m_b, t->pi_b, e->b, e->sheet);
    for (int j = 0; j < 9; j++) {
        const double *cs = j < 2 ? sx0 : sx, *cd = j < 2 ? dx0 : dx;
        for (int i = 0; i < 9; i++)
            cell_paint(cr, img, cs[i], sy[j], cs[i + 1] - cs[i], sy[j + 1] - sy[j], cd[i], dy[j], cd[i + 1] - cd[i], dy[j + 1] - dy[j]);
    }
}

/* header_mode = crop (README "Aero vision"): the first two row segments of an Explorer / MAXEXPLORER grid (the 1:1 top rows and the
   stretched band) become the texture's rows [0, c) copied 1:1 onto the header's rows [0, c), c = min(H - 2, ln), and, only when the
   header is taller than the band (H - 2 > ln), the band's last row repeated over [c, H - 2). The inner lines [ln, ln + 2) stay on the
   header's last two rows H - 2, H - 1 (as in stretch mode), so the client's body starts on row H either way. */
static void crop_rows(double *sy0, double *sy1, double *dy0, double *dy1, double H, double ln) {
    double c = H - 2 < ln ? H - 2 : ln;
    if (c < 0) c = 0;
    sy0[0] = 0; sy1[0] = c; dy0[0] = 0; dy1[0] = c;
    sy0[1] = ln - 1; sy1[1] = ln; dy0[1] = c; dy1[1] = H - 2;   /* empty unless the header outgrows the band */
}

/* the Explorer plate (LgFrameExplorer, README "Explorer plate") under an extended frame whose top reaches into the client. x as in
   plate_ext. y: the outer rows [0, x_r0) 1:1 (aero: the caption-button pill), the band [x_r0, x_r1) stretched into the header down to
   its last two rows, which get the texture's inner light/dark lines [x_line, x_line + 2); below that the body from x_line + 2 and the
   bottom edge exactly as plate_ext. Header H = m_t + t: for t = xi_t - m_t (aero: 73 - 27 = 46) the band is copied 1:1. */
static void plate_explorer(cairo_t *cr, const struct theme *t, cairo_surface_t *img, double w, double h, const struct frame_ext *e) {
    if (!img) return;
    double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img), sx[10], dx[10], sx0[10], dx0[10];
    struct frame_ext ns = *e; ns.sheet = 0;
    ext_columns(t, iw, w, t->xi_l, t->xi_r, &ns, sx, dx, sx0, dx0);
    double H = t->m_t + e->t, r0 = t->x_r0 < H - 2 ? t->x_r0 : H - 2, ln = t->x_line, ob = 2, ab = t->m_b - 2, ib = t->xi_b, lb = t->m_b < ib ? t->m_b : ib;
    if (r0 < 0) r0 = 0;
    double sy0[8] = { 0, r0, ln, ln + 2, ih - ib, ih - lb, ih - ab, ih - ob }, sy1[8] = { r0, t->x_r1, ln + 2, ih - ib, ih - lb, ih - ab, ih - ob, ih },
           dy0[8] = { 0, r0, H - 2, H, h - ib - e->b, h - lb - e->b, h - ab - e->b, h - ob }, dy1[8] = { r0, H - 2, H, h - ib - e->b, h - lb - e->b, h - ab - e->b, h - ob, h };
    if (t->x_crop) crop_rows(sy0, sy1, dy0, dy1, H, ln);
    for (int j = 0; j < 8; j++) {
        const double *cs = j < 2 ? sx0 : sx, *cd = j < 2 ? dx0 : dx;
        for (int i = 0; i < 9; i++)
            cell_paint(cr, img, cs[i], sy0[j], cs[i + 1] - cs[i], sy1[j] - sy0[j], cd[i], dy0[j], cd[i + 1] - cd[i], dy1[j] - dy0[j]);
    }
}

/* the plate painted (shape: its pattern, raw texture alpha) with glass_alpha / glass_alpha_mode (README "Glass accuracy"). The
   factor is applied here and not to the textures because the raw alpha is also the overlay's mask: the overlay, buttons and fog
   keep their own alpha. Square: source masked by its own alpha, (a c, a) -> (a^2 c, a^2), i.e. straight alpha a -> a^2. */
static void plate_draw(cairo_t *cr, const struct theme *t, cairo_pattern_t *shape, double k) {
    double a = t->glass_alpha * k;   /* k: the crossfade weight (1 outside a maximise/restore fade) */
    if (t->glass_square) {
        cairo_push_group(cr); cairo_set_source(cr, shape); cairo_mask(cr, shape); cairo_pop_group_to_source(cr);
        if (a < 1) cairo_paint_with_alpha(cr, a); else cairo_paint(cr);
        return;
    }
    cairo_set_source(cr, shape);
    if (a < 1) cairo_paint_with_alpha(cr, a); else cairo_paint(cr);
}

/* one cell grid: source rows [sy0[j], sy1[j]) into destination rows [dy0[j], dy1[j]), columns likewise (bilinear, as nine()) */
static void grid_paint(cairo_t *cr, cairo_surface_t *img, int nx, const double *sx0, const double *sx1, const double *dx0, const double *dx1,
                       int ny, const double *sy0, const double *sy1, const double *dy0, const double *dy1) {
    for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++)
        cell_paint(cr, img, sx0[i], sy0[j], sx1[i] - sx0[i], sy1[j] - sy0[j], dx0[i], dy0[j], dx1[i] - dx0[i], dy1[j] - dy0[j]);
}

/* the maximised plate (LGFRAMEMAX, or MAXEXPLORER under an Explorer-style header of H = m_t + ext->t rows) over w x h. Columns: a
   9-grid with max_insets left/right (aero: 0 0, desksrv's LgFrameMax margins: the whole width stretches). Rows: plain 9-grid with
   max_insets top/bottom; MAXEXPLORER: [0, mx_r0) 1:1, the band [mx_r0, mx_r1) stretched to H - 2, the inner lines [mx_line, +2) on the
   header's last two rows, the body from mx_line + 2 down to the bottom inset, then the bottom edge 1:1 */
static void plate_max(cairo_t *cr, const struct theme *t, cairo_surface_t *img, double w, double h, const struct frame_ext *e, int explorer) {
    if (!img) return;
    double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
    int il = explorer ? t->mxi_l : t->mi_l, ir = explorer ? t->mxi_r : t->mi_r, ib = explorer ? t->mxi_b : t->mi_b;
    double sx0[3] = { 0, il, iw - ir }, sx1[3] = { il, iw - ir, iw }, dx0[3] = { 0, il, w - ir }, dx1[3] = { il, w - ir, w };
    if (explorer) {
        double H = t->m_t + e->t, r0 = t->mx_r0 < H - 2 ? t->mx_r0 : H - 2, ln = t->mx_line;
        if (r0 < 0) r0 = 0;
        double sy0[5] = { 0, r0, ln, ln + 2, ih - ib }, sy1[5] = { r0, t->mx_r1, ln + 2, ih - ib, ih },
               dy0[5] = { 0, r0, H - 2, H, h - ib }, dy1[5] = { r0, H - 2, H, h - ib, h };
        if (t->x_crop) crop_rows(sy0, sy1, dy0, dy1, H, ln);
        grid_paint(cr, img, 3, sx0, sx1, dx0, dx1, 5, sy0, sy1, dy0, dy1);
    } else {
        int it = t->mi_t;
        double sy0[3] = { 0, it, ih - ib }, sy1[3] = { it, ih - ib, ih }, dy0[3] = { 0, it, h - ib }, dy1[3] = { it, h - ib, h };
        grid_paint(cr, img, 3, sx0, sx1, dx0, dx1, 3, sy0, sy1, dy0, dy1);
    }
}

/* the caption fog (CAPTIONFOG, captionfog.png 99x26: white, alpha up to 0x47, 10 px fades at both ends) as one band over the caption
   row and the extended top, between the side borders: ends 1:1, middle stretched, the 26 rows stretched to the band height */
static void fog_band(cairo_t *cr, cairo_surface_t *fog, double x, double y, double w, double h) {
    if (!fog || w <= 0 || h <= 0) return;
    double fw = cairo_image_surface_get_width(fog), fh = cairo_image_surface_get_height(fog), end = fw > 30 ? 10 : fw / 3;
    if (w < 2 * end) end = w / 2;
    double sx[4] = {0, end, fw - end, fw}, dx[4] = {x, x + end, x + w - end, x + w};
    for (int i = 0; i < 3; i++) {
        double sw = sx[i + 1] - sx[i], dw = dx[i + 1] - dx[i];
        if (sw <= 0 || dw <= 0) continue;
        cairo_save(cr); cairo_rectangle(cr, dx[i], y, dw, h); cairo_clip(cr);
        cairo_translate(cr, dx[i], y); cairo_scale(cr, dw / sw, h / fh); cairo_translate(cr, -sx[i], 0);
        cairo_set_source_surface(cr, fog, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD); cairo_paint(cr); cairo_restore(cr);
    }
}

static struct { Window win; int w, h, n; unsigned long sum; } shape_cache[256];

/* profiling (LHWM_PROF): accumulated milliseconds of the last frame_paint_ex calls: [0] cairo paint, [1] shape, [2] upload */
unsigned long frame_upload_serial;
void (*frame_pre_upload)(void *); void *frame_pre_upload_arg;
double frame_prof_ms[4];   /* [3]: number of paints */
#include <time.h>
static double prof_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }

void frame_paint(Display *dpy, Visual *vis, const struct theme *t, Window dec, int w, int h,
                 const char *title, cairo_surface_t *icon, int focused, int hover, int pressed, int maximized) {
    frame_paint_ex(dpy, vis, t, dec, w, h, title, icon, focused, hover, pressed, maximized, NULL);
}

void frame_paint_ex(Display *dpy, Visual *vis, const struct theme *t, Window dec, int w, int h, const char *title, cairo_surface_t *icon,
                    int focused, int hover, int pressed, int maximized, const struct frame_ext *ext) {
    frame_paint_ex2(dpy, vis, t, dec, w, h, title, icon, focused, hover, pressed, maximized, ext, NULL);
}

/* upload rows/columns of a painted ARGB32 image to the window with XPutImage (the image data wrapped, not copied) */
static void put_rect(Display *dpy, Drawable win, GC gc, XImage *xi, int x, int y, int w, int h) {
    if (w > 0 && h > 0) XPutImage(dpy, win, gc, xi, x, y, x, y, (unsigned)w, (unsigned)h);
}

void frame_paint_ex2(Display *dpy, Visual *vis, const struct theme *t, Window dec, int w, int h, const char *title, cairo_surface_t *icon,
                     int focused, int hover, int pressed, int maximized, const struct frame_ext *ext, const XRectangle *hole) {
    frame_paint_ex3(dpy, vis, t, dec, w, h, title, icon, focused, hover, pressed, maximized, ext, hole, -1);
}

void frame_paint_ex3(Display *dpy, Visual *vis, const struct theme *t, Window dec, int w, int h, const char *title, cairo_surface_t *icon,
                     int focused, int hover, int pressed, int maximized, const struct frame_ext *ext, const XRectangle *hole, double maxfade) {
    frame_paint_ex4(dpy, vis, t, dec, w, h, title, icon, focused, hover, pressed, maximized, ext, hole, maxfade, NULL);
}

/* the part of the frame's client area that the client leaves free (size hints: a maximised terminal is a whole number of character
   cells, smaller than the work area by the remainder; README "Size hints"). DWM plates are painted under the client anyway, so the
   remainder shows the plate's own client-area texture. msstyles frames leave the client area transparent: there it is filled with
   the colour of the frame border (FRAMELEFT drawn cnBorders x 64 in its frame state, sampled in the middle; button face grey without
   one), as if the border went on around the client */
static void ms_gap_fill(cairo_t *cr, const struct theme *t, int w, int h, int focused, int maximized, const XRectangle *cl) {
    int l, r, tp, b; theme_margins_ex(t, maximized, &l, &r, &tp, &b);
    int x0 = cl->x, y0 = cl->y, x1 = w - r, y1 = h - b;
    if (x1 <= x0 || y1 <= y0) return;
    if (cl->x + cl->width >= x1 && cl->y + cl->height >= y1) return;   /* nothing left free */
    double rgb[3] = { 0xd4 / 255.0, 0xd0 / 255.0, 0xc8 / 255.0 };
    const struct mspart *fl = &t->ms[MP_FRAMELEFT];
    int cnb = 3 + t->ms_border;
    if (fl->count && cnb > 0) {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cnb, 64); cairo_t *c2 = cairo_create(s);
        cairo_set_operator(c2, CAIRO_OPERATOR_OVER); ms_part(c2, t, MP_FRAMELEFT, focused ? 0 : 1, 0, 0, cnb, 64); cairo_destroy(c2);
        cairo_surface_flush(s);
        uint32_t px = *(uint32_t *)(cairo_image_surface_get_data(s) + 32 * cairo_image_surface_get_stride(s) + 4 * (cnb / 2));
        unsigned a = px >> 24;
        if (a) { rgb[0] = ((px >> 16) & 255) / (double)a; rgb[1] = ((px >> 8) & 255) / (double)a; rgb[2] = (px & 255) / (double)a; }
        cairo_surface_destroy(s);
    }
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0); cairo_rectangle(cr, cl->x, cl->y, cl->width, cl->height); cairo_fill(cr);
    cairo_restore(cr);
}

void frame_paint_ex4(Display *dpy, Visual *vis, const struct theme *t, Window dec, int w, int h, const char *title, cairo_surface_t *icon,
                     int focused, int hover, int pressed, int maximized, const struct frame_ext *ext, const XRectangle *hole, double maxfade,
                     const XRectangle *client) {
    /* paint offscreen, then copy: no flicker, and the alpha gives the window shape */
    double t0 = prof_now();
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(img);
    /* hole: the part covered by an opaque client. It is neither painted nor uploaded (README "Performance"); it keeps clear of
       the 24 top/bottom rows that frame_shape_rects reads, and the clip is pixel-aligned, so the painted pixels are the same */
    int hx = 0, hy = 0, hw = 0, hh = 0;
    if (hole) {
        hx = hole->x; hy = hole->y < 24 ? 24 : hole->y; hw = hole->width;
        int hb = hole->y + hole->height; if (hb > h - 24) hb = h - 24;
        hh = hb - hy;
        if (hx < 0 || hx + hw > w || hw <= 0 || hh <= 0) hw = hh = 0;
    }
    if (hw > 0) {
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_rectangle(cr, 0, 0, w, h); cairo_rectangle(cr, hx, hy, hw, hh); cairo_clip(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    }
    frame_paint_cr3(cr, t, w, h, title, icon, focused, hover, pressed, maximized, ext, maxfade);
    if (client && t->mode == THEME_MSSTYLES) ms_gap_fill(cr, t, w, h, focused, maximized, client);
    cairo_destroy(cr);
    double t1 = prof_now();
    XRectangle rects[128]; int n = frame_shape_rects(t, img, maximized, rects, 128);
    unsigned long sum = (unsigned long)n;
    for (int i = 0; i < n; i++) sum = sum * 31 + rects[i].x * 7 + rects[i].y * 13 + rects[i].width * 17 + rects[i].height;
    int slot = (int)(dec % 256);
    /* frame_pre_upload (README "Resize without flicker"): the pixels go to an offscreen pixmap first; then the hook resizes the
       windows, and the shape and a copy into the window follow at once, so the server gets the new geometry and the new pixels as
       one short batch (a compositor never sees the window at the new size with the old or half-uploaded pixels) */
    void (*pre)(void *) = frame_pre_upload; frame_pre_upload = NULL;
    Pixmap stage = pre ? XCreatePixmap(dpy, dec, (unsigned)w, (unsigned)h, 32) : None;
    Drawable dst = stage != None ? stage : dec;
    static GC gc = None;
    if (gc == None) gc = XCreateGC(dpy, dec, 0, NULL);   /* any depth-32 drawable on this screen will do */
    #define SEND_SHAPE() do { \
        if (n > 0 && (shape_cache[slot].win != dec || shape_cache[slot].w != w || shape_cache[slot].h != h || shape_cache[slot].sum != sum)) { \
            XShapeCombineRectangles(dpy, dec, ShapeBounding, 0, 0, rects, n, ShapeSet, YXBanded); \
            shape_cache[slot].win = dec; shape_cache[slot].w = w; shape_cache[slot].h = h; shape_cache[slot].sum = sum; } } while (0)
    if (stage == None) SEND_SHAPE();
    double t2 = prof_now();
    frame_upload_serial = NextRequest(dpy);   /* the pixels go out from here on: Expose events older than this are covered */
    if (hw > 0) {   /* only the four bands around the hole, straight XPutImage of the painted rows */
        cairo_surface_flush(img);
        XImage *xi = XCreateImage(dpy, vis, 32, ZPixmap, 0, (char *)cairo_image_surface_get_data(img), (unsigned)w, (unsigned)h, 32,
                                  cairo_image_surface_get_stride(img));
        if (xi) {
            union { uint32_t u; unsigned char b[4]; } bo = { 1 };
            xi->byte_order = bo.b[0] ? LSBFirst : MSBFirst;   /* cairo's ARGB32 is native-endian; Xlib swaps if the server differs */
            put_rect(dpy, dst, gc, xi, 0, 0, w, hy);
            put_rect(dpy, dst, gc, xi, 0, hy + hh, w, h - hy - hh);
            put_rect(dpy, dst, gc, xi, 0, hy, hx, hh);
            put_rect(dpy, dst, gc, xi, hx + hw, hy, w - hx - hw, hh);
            xi->data = NULL; XDestroyImage(xi);
        }
    } else {   /* the whole image (cairo-xlib uses MIT-SHM for large uploads: faster than a plain XPutImage here) */
        cairo_surface_t *xs = cairo_xlib_surface_create(dpy, dst, vis, w, h);
        cr = cairo_create(xs); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_surface(cr, img, 0, 0); cairo_paint(cr);
        cairo_destroy(cr); cairo_surface_flush(xs); cairo_surface_destroy(xs);
    }
    if (stage != None) {
        /* the upload is through first; then resize, shape and copy go out as one short batch under a server grab, so no other
           client's request (the compositor's) runs between them: without the grab picom on the lab still caught, in about one
           drag of two, the client resized but the frame's right band not yet copied */
        XSync(dpy, False);
        XGrabServer(dpy);
        pre(frame_pre_upload_arg);
        SEND_SHAPE();
        frame_upload_serial = NextRequest(dpy);   /* the Expose events of the resize are older: covered */
        if (hw > 0) {
            XCopyArea(dpy, stage, dec, gc, 0, 0, (unsigned)w, (unsigned)hy, 0, 0);
            if (h - hy - hh > 0) XCopyArea(dpy, stage, dec, gc, 0, hy + hh, (unsigned)w, (unsigned)(h - hy - hh), 0, hy + hh);
            if (hx > 0) XCopyArea(dpy, stage, dec, gc, 0, hy, (unsigned)hx, (unsigned)hh, 0, hy);
            if (w - hx - hw > 0) XCopyArea(dpy, stage, dec, gc, hx + hw, hy, (unsigned)(w - hx - hw), (unsigned)hh, hx + hw, hy);
        } else XCopyArea(dpy, stage, dec, gc, 0, 0, (unsigned)w, (unsigned)h, 0, 0);
        XUngrabServer(dpy);
        XFreePixmap(dpy, stage);
        XFlush(dpy);   /* the grab lasts only for this batch */
    }
    #undef SEND_SHAPE
    cairo_surface_destroy(img);
    frame_prof_ms[0] += t1 - t0; frame_prof_ms[1] += t2 - t1; frame_prof_ms[2] += prof_now() - t2; frame_prof_ms[3] += 1;
}

void frame_paint_cr(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover, int pressed, int maximized) { frame_paint_cr2(cr, t, w, h, title, icon, focused, hover, pressed, maximized, NULL); }
void frame_paint_cr_ex(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover,
                       int pressed, int maximized, const struct frame_ext *ext) { frame_paint_cr2(cr, t, w, h, title, icon, focused, hover, pressed, maximized, ext); }

void frame_paint_cr2(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover, int pressed, int maximized, const struct frame_ext *ext) {
    frame_paint_cr3(cr, t, w, h, title, icon, focused, hover, pressed, maximized, ext, -1);
}

void frame_paint_cr3(cairo_t *cr, const struct theme *t, int w, int h, const char *title, cairo_surface_t *icon, int focused, int hover,
                     int pressed, int maximized, const struct frame_ext *ext, double maxfade) {
    /* classic msstyles frames have no glass: NO_TITLE / NO_ICON apply, an extension only continues the caption band (header pixmap) */
    if (t->mode == THEME_MSSTYLES) { ms_paint(cr, t, w, h, title, icon, focused, hover, pressed, maximized, ext); return; }
    int extended = ext && (ext->l > 0 || ext->r > 0 || ext->t > 0 || ext->b > 0 || ext->sheet);
    int explorer = extended && !ext->sheet && ext->t > 0 && t->plate_x[0];   /* Explorer-style tall header: LgFrameExplorer */
    unsigned flags = ext ? ext->flags : 0;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    /* 1. plate (LGFRAME active / inactive half); kept as a pattern: its alpha is the frame's shape and masks the overlay below.
       Maximised (the frame fills the work area): the bottom border lies below the window, the side borders under the client
       (margins 0), the caption keeps its height and button layout, and the two top corners are squared (square_corners) */
    /* M5b: the maximised plate (plate_max) at weight mf, cross-dissolved with the normal plate while a maximise/restore fades */
    double mf = !t->plate_m[0] ? 0 : maxfade < 0 ? (maximized ? 1 : 0) : maxfade > 1 ? 1 : maxfade;
    int fading = mf > 0 && mf < 1;
    cairo_pattern_t *shape = NULL, *opat = NULL;
    if (fading) {   /* cross-dissolve (1 - mf) normal + mf maximised, premultiplied, then over the background: a translucent maximised
                       plate (Jade) ends exactly at its own alpha instead of max-over-normal. Layers cached for the whole fade */
        cairo_pattern_t *np = fade_layer(t, 0, w, h, focused, maximized, ext), *mp = fade_layer(t, 1, w, h, focused, maximized, ext);
        if (t->overlay) opat = fade_layer(t, 2, w, h, focused, maximized, ext);
        cairo_push_group(cr); plate_draw(cr, t, np, 1 - mf);
        cairo_set_operator(cr, CAIRO_OPERATOR_ADD); plate_draw(cr, t, mp, mf);
        cairo_pop_group_to_source(cr); cairo_paint(cr);
    } else {
        fade_cache_clear();
        if (mf >= 1) {   /* maximised plate; the normal plate is not needed (nor is the overlay it masks) */
            int mexp = extended && !ext->sheet && ext->t > 0 && t->plate_mx[0];   /* Explorer-style header: MAXEXPLORER */
            cairo_surface_t *ps = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); cairo_t *c2 = cairo_create(ps);
            plate_max(c2, t, mexp ? t->plate_mx[focused ? 0 : 1] : t->plate_m[focused ? 0 : 1], w, h, ext, mexp);
            cairo_destroy(c2);
            if (maximized) square_corners(ps, 6);   /* Jade's LGFRAMEMAX is the normal plate again: square its corners as in M5a */
            cairo_pattern_t *mshape = cairo_pattern_create_for_surface(ps); cairo_surface_destroy(ps);
            plate_draw(cr, t, mshape, 1); cairo_pattern_destroy(mshape);
        } else {
            if (maximized) {
                cairo_surface_t *ps = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); cairo_t *c2 = cairo_create(ps);
                if (explorer) plate_explorer(c2, t, t->plate_x[focused ? 0 : 1], w, h + t->m_b, ext);
                else if (extended) plate_ext(c2, t, t->plate[focused ? 0 : 1], w, h + t->m_b, ext);
                else nine(c2, t->plate[focused ? 0 : 1], 0, 0, w, h + t->m_b, t->pi_t, t->pi_r, t->pi_b, t->pi_l);
                cairo_destroy(c2);
                square_corners(ps, 6); shape = cairo_pattern_create_for_surface(ps); cairo_surface_destroy(ps);
            } else if (explorer) { cairo_push_group(cr); plate_explorer(cr, t, t->plate_x[focused ? 0 : 1], w, h, ext); shape = cairo_pop_group(cr); }
            else if (extended) { cairo_push_group(cr); plate_ext(cr, t, t->plate[focused ? 0 : 1], w, h, ext); shape = cairo_pop_group(cr); }
            else { cairo_push_group(cr); nine(cr, t->plate[focused ? 0 : 1], 0, 0, w, h, t->pi_t, t->pi_r, t->pi_b, t->pi_l); shape = cairo_pop_group(cr); }
            plate_draw(cr, t, shape, 1);
        }
    }
    /* 1b. extended top: the caption fog as a soft light band behind the caption row and the client's header (NO_FOG: none) */
    if (extended && !ext->sheet && ext->t > 0 && !(flags & FRAME_NO_FOG) && t->fog_on) {
        int ml, mr, mt, mb; theme_margins_ex(t, maximized, &ml, &mr, &mt, &mb);
        fog_band(cr, t->fog, ml + ext->l, 0, w - ml - mr - ext->l - ext->r, mt + ext->t);
    }
    /* 2. caption buttons with glow plates for hot/pressed */
    for (int i = 0; i < t->norder; i++) {
        int bx, by, bw, bh; int b = button_rect(t, w, i, maximized, &bx, &by, &bw, &bh);
        int hit = hit_of_slot(t, i); int state = !focused ? 3 : 0;
        if (pressed == hit) state = 2; else if (hover == hit) state = 1;   /* hot/pressed also on inactive frames */
        if (state == 1 || state == 2) {
            cairo_surface_t *g = (b == BTN_CLOSE) ? t->glow_close : t->glow_btn;
            if (g) { double gw = cairo_image_surface_get_width(g), gh = cairo_image_surface_get_height(g);
                cairo_set_source_surface(cr, g, bx + bw / 2.0 - gw / 2.0, by + bh / 2.0 - gh / 2.0); cairo_paint(cr); }
        }
        if (t->btn[b]) {
            /* the cell is button_gap columns wider than the sprite: the sprite's left divider part (cols 0..3) stays at the cell's
               left, the rest sits at the right end, and the gap is filled by repeating sprite column 4 (flat well interior) */
            int g = t->btn_gap, c = 4;
            if (g <= 0) { cairo_save(cr); cairo_rectangle(cr, bx, by, bw, bh); cairo_clip(cr);
                cairo_set_source_surface(cr, t->btn[b], bx, by - state * t->btn_h); cairo_paint(cr); cairo_restore(cr); }
            else {
                cairo_save(cr); cairo_rectangle(cr, bx, by, c, bh); cairo_clip(cr);
                cairo_set_source_surface(cr, t->btn[b], bx, by - state * t->btn_h); cairo_paint(cr); cairo_restore(cr);
                cairo_save(cr); cairo_rectangle(cr, bx + c, by, g, bh); cairo_clip(cr);
                cairo_translate(cr, bx + c, by - state * t->btn_h); cairo_scale(cr, g, 1); cairo_translate(cr, -c, 0);
                cairo_set_source_surface(cr, t->btn[b], 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
                cairo_paint(cr); cairo_restore(cr);
                cairo_save(cr); cairo_rectangle(cr, bx + c + g, by, bw - c - g, bh); cairo_clip(cr);
                cairo_set_source_surface(cr, t->btn[b], bx + g, by - state * t->btn_h); cairo_paint(cr); cairo_restore(cr);
            }
        }
    }
    /* 3. window icon (from _NET_WM_ICON) and caption text with TextGlow */
    if (flags & FRAME_NO_ICON) icon = NULL;
    if (flags & FRAME_NO_TITLE) title = NULL;
    if (icon) {   /* scaled to icon_size; an icon at most 4 px larger (DWM DEFAULTICON is 20x17) is drawn 1:1, centred on the cell */
        double iw = cairo_image_surface_get_width(icon), ih = cairo_image_surface_get_height(icon), sc = t->icon_size / (iw > ih ? iw : ih);
        if (iw <= t->icon_size + 4 && ih <= t->icon_size + 4 && (iw > t->icon_size || ih > t->icon_size)) sc = 1;
        cairo_save(cr); cairo_translate(cr, t->icon_x + floor((t->icon_size - iw * sc) / 2.0), (t->m_t - ih * sc) / 2.0); cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, icon, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST); cairo_paint(cr); cairo_restore(cr);
    }
    if (title && *title) {
        PangoLayout *lay = pango_cairo_create_layout(cr); PangoFontDescription *fd = pango_font_description_from_string(t->font);
        pango_layout_set_font_description(lay, fd); pango_layout_set_text(lay, title, -1);
        int bx, by, bw, bh; button_rect(t, w, t->norder - 1, maximized, &bx, &by, &bw, &bh);
        pango_layout_set_width(lay, (bx - t->title_x - 8) * PANGO_SCALE); pango_layout_set_ellipsize(lay, PANGO_ELLIPSIZE_END);
        int tw, th; pango_layout_get_pixel_size(lay, &tw, &th);
        double tx = icon ? t->title_x : t->icon_x + 2, ty = (t->m_t - th) / 2.0;
        /* maximised text colours (text_max_*, glow_max), weighted like the plate: the M5b crossfade fades them with it */
        double xw = maxfade < 0 ? (maximized ? 1 : 0) : maxfade > 1 ? 1 : maxfade, ga = t->glow_m_none ? 0.22 * (1 - xw) : 0.22, gc[3], tc[3];
        const double *tn = focused ? t->text_a : t->text_i, *tm = focused ? t->text_ma : t->text_mi, *gm = t->glow_m_none ? t->glow : t->glow_m;
        for (int i = 0; i < 3; i++) { tc[i] = tn[i] + (tm[i] - tn[i]) * xw; gc[i] = t->glow[i] + (gm[i] - t->glow[i]) * xw; }
        if (ga > 0) for (int r = 3; r >= 1; r--) for (int k = 0; k < 8; k++) {
            double a = k * 0.7854; cairo_set_source_rgba(cr, gc[0], gc[1], gc[2], ga);
            cairo_move_to(cr, tx + r * 0.9 * __builtin_cos(a), ty + r * 0.9 * __builtin_sin(a)); pango_cairo_show_layout(cr, lay); }
        cairo_set_source_rgb(cr, tc[0], tc[1], tc[2]);
        cairo_move_to(cr, tx, ty); pango_cairo_show_layout(cr, lay);
        pango_font_description_free(fd); g_object_unref(lay);
    }
    /* 4. edge overlay: its texture reaches overlay_outset px past the plate on each side (811x615 vs 801x607: 4 left/right,
       3 top/bottom once its highlight line is put on the plate's inset-1 line, as in the 4074 screenshots). Outside the plate
       only its soft halo would remain, which the window then clips to a square at the rounded corners, so mask it with the
       plate's own alpha: nothing of the frame sticks out past the plate's corner radius. */
    if (fading) { if (opat) { cairo_set_source(cr, opat); cairo_paint_with_alpha(cr, 1 - mf); } return; }   /* fades out with the plate */
    if (!t->overlay || !shape) { if (shape) cairo_pattern_destroy(shape); return; }   /* overlay = 0, or the maximised plate (it has none) */
    if (maximized) {   /* same rule on the squared plate, then the overlay's own rounded highlight squared too */
        cairo_surface_t *os = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); cairo_t *c2 = cairo_create(os);
        nine(c2, t->overlay, -t->oo_l, -t->oo_t, w + t->oo_l + t->oo_r, h + t->m_b + t->oo_t + t->oo_b, t->oi_t, t->oi_r, t->oi_b, t->oi_l);
        cairo_set_operator(c2, CAIRO_OPERATOR_DEST_IN); cairo_set_source(c2, shape); cairo_paint(c2); cairo_destroy(c2);
        square_corners(os, 6); cairo_set_source_surface(cr, os, 0, 0); cairo_paint(cr); cairo_surface_destroy(os); cairo_pattern_destroy(shape);
        return;
    }
    cairo_push_group(cr);
    nine(cr, t->overlay, -t->oo_l, -t->oo_t, w + t->oo_l + t->oo_r, h + t->oo_t + t->oo_b, t->oi_t, t->oi_r, t->oi_b, t->oi_l);
    cairo_pop_group_to_source(cr); cairo_mask(cr, shape); cairo_pattern_destroy(shape);
}

/* ---- CLIENT_CAPTION and the header pixmap (CLIENT-PROTOCOL.md "Additions 2026-10-24") ---- */
int theme_caption_border(const struct theme *t, int maximized) {
    if (maximized) return 0;   /* the borders of a maximised frame lie off-screen (msstyles) or are dropped (DWM) */
    if (t->cap_border >= 0) return t->cap_border;   /* caption_border (vision: the plate's 2 outer rows) */
    return t->mode == THEME_MSSTYLES ? 3 + t->ms_border : t->m_l;
}

void frame_buttons_rect(const struct theme *t, int w, int maximized, int *x, int *y, int *bw, int *bh) {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30), n = t->mode == THEME_MSSTYLES ? 3 : t->norder;
    for (int i = 0; i < n; i++) {
        int bx, by, w1, h1;
        if (t->mode == THEME_MSSTYLES) {
            const int part = i == 0 ? MP_CLOSE : i == 2 ? MP_MIN : maximized ? MP_RESTORE : MP_MAX;
            if (!t->ms[part].count) continue;
            ms_btn_rect(t, w, i, maximized, &bx, &by, &w1, &h1);
        }
        else { button_rect(t, w, i, maximized, &bx, &by, &w1, &h1); bx -= 2; by -= 3; w1 += 4; h1 += 6; }   /* frame_hit_ex's slop */
        if (bx < x0) x0 = bx;
        if (by < y0) y0 = by;
        if (bx + w1 > x1) x1 = bx + w1;
        if (by + h1 > y1) y1 = by + h1;
    }
    if (x1 < x0) { *x = *y = *bw = *bh = 0; return; }
    if (t->deco_h > 0 && y1 > t->deco_h) y1 = t->deco_h;   /* vision: the frame's decoration ends at deco_h, the hit slop too */
    *x = x0; *y = y0 < 0 ? 0 : y0; *bw = x1 - x0; *bh = y1 - *y;
}

cairo_surface_t *frame_header_image(const struct theme *t, int w, int h, int x0, int cw, int band_h, int focused, int hover, int pressed,
                                    int maximized, const struct frame_ext *ext) {
    if (cw <= 0 || band_h <= 0) return NULL;
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, band_h);
    cairo_t *cr = cairo_create(img);
    struct frame_ext e = { 0 }; if (ext) e = *ext;
    e.flags |= FRAME_NO_TITLE | FRAME_NO_ICON;   /* an opaque header carries its own title and icon */
    cairo_translate(cr, -x0, 0);
    frame_paint_cr2(cr, t, w, h, NULL, NULL, focused, hover, pressed, maximized, &e);
    cairo_destroy(cr); cairo_surface_flush(img);
    return img;
}

/* the caption icon's cell (the window menu's click target; README "The window menu"), frame coordinates; the painters place the icon
   there (ms_paint: SM_CXSMICON at cnBorders + 2; DWM: icon_x, centred in the caption) */
void frame_icon_rect(const struct theme *t, int w, int maximized, int *x, int *y, int *iw, int *ih) {
    (void)w;
    if (t->mode == THEME_MSSTYLES) {
        int cnb = 3 + t->ms_border, o = maximized ? -cnb : 0;
        *x = o + cnb + 2; *y = o + cnb + (t->ms_cap_h - 16) / 2; *iw = *ih = 16;
        if (*y < 0) { *ih += *y; *y = 0; }
        return;
    }
    *x = t->icon_x; *y = (t->m_t - t->icon_size) / 2; *iw = *ih = t->icon_size;
}

int frame_hit(const struct theme *t, int w, int h, int x, int y) { return frame_hit_ex(t, w, h, x, y, 0); }
int frame_hit_ex(const struct theme *t, int w, int h, int x, int y, int maximized) {
    if (t->mode == THEME_MSSTYLES) {
        for (int slot = 0; slot < 3; slot++) {
            int bx, by, bw, bh; ms_btn_rect(t, w, slot, maximized, &bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) return ms_slot_hit(slot);
        }
        int l, r, tp, b; theme_margins_ex(t, maximized, &l, &r, &tp, &b);
        if (maximized) return y < tp ? HIT_CAPTION : HIT_NONE;
        int cnb = l, corner = tp, L = x < cnb, R = x >= w - cnb, T = y < cnb, B = y >= h - cnb;   /* corners: one caption height along each edge */
        if ((T && x < corner) || (L && y < corner)) return HIT_TL;
        if ((T && x >= w - corner) || (R && y < corner)) return HIT_TR;
        if ((B && x < corner) || (L && y >= h - corner)) return HIT_BL;
        if ((B && x >= w - corner) || (R && y >= h - corner)) return HIT_BR;
        if (L) return HIT_L;
        if (R) return HIT_R;
        if (T) return HIT_T;
        if (B) return HIT_B;
        return y < tp ? HIT_CAPTION : HIT_NONE;
    }
    for (int i = 0; i < t->norder; i++) {
        int bx, by, bw, bh; button_rect(t, w, i, maximized, &bx, &by, &bw, &bh);
        if (x >= bx - 2 && x < bx + bw + 2 && y >= by - 3 && y < by + bh + 3 && (t->deco_h <= 0 || y < t->deco_h)) return hit_of_slot(t, i);
    }
    if (maximized) return y < t->m_t ? HIT_CAPTION : HIT_NONE;   /* no resize edges on a maximised frame */
    int corner = 18, L = x < t->m_l, R = x >= w - t->m_r, T = y < 6, B = y >= h - t->m_b;
    if ((L || R) && y < corner) return L ? HIT_TL : HIT_TR;
    if ((L || R) && y >= h - corner) return L ? HIT_BL : HIT_BR;
    if (B && x < corner) return HIT_BL;
    if (B && x >= w - corner) return HIT_BR;
    if (L) return HIT_L;
    if (R) return HIT_R;
    if (B) return HIT_B;
    if (T) return HIT_T;
    if (y < t->m_t) return HIT_CAPTION;
    return HIT_NONE;
}

/* src scaled into a want x want square (aspect kept, centred) with CAIRO_FILTER_BEST; takes src */
static cairo_surface_t *icon_fit(cairo_surface_t *src, int want) {
    int iw = cairo_image_surface_get_width(src), ih = cairo_image_surface_get_height(src);
    if (want <= 0 || (iw == want && ih == want)) return src;
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, want, want); cairo_t *cr = cairo_create(out);
    double k = (double)want / (iw > ih ? iw : ih);
    cairo_translate(cr, (want - iw * k) / 2, (want - ih * k) / 2); cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, src, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST); cairo_paint(cr);
    cairo_destroy(cr); cairo_surface_destroy(src); return out;
}

cairo_surface_t *frame_icon_from_netwm(const unsigned long *data, unsigned long n, int want) {
    /* the smallest image >= want (downscaling a little loses less than upscaling), else the largest */
    unsigned long i = 0; const unsigned long *best = NULL; unsigned long bw = 0, bh = 0;
    while (i + 2 <= n) {
        unsigned long w = data[i], h = data[i + 1]; if (!w || !h || w > 1024 || h > 1024 || i + 2 + w * h > n) break;
        unsigned long sz = w > h ? w : h, bsz = bw > bh ? bw : bh;
        int better = !best || (sz >= (unsigned long)want ? (bsz < (unsigned long)want || sz < bsz) : (bsz < (unsigned long)want && sz > bsz));
        if (better) { best = data + i + 2; bw = w; bh = h; }
        i += 2 + w * h;
    }
    if (!best) return NULL;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh);
    unsigned char *px = cairo_image_surface_get_data(s); int stride = cairo_image_surface_get_stride(s);
    for (unsigned long y = 0; y < bh; y++) for (unsigned long x = 0; x < bw; x++) {
        unsigned long v = best[y * bw + x]; unsigned a = (v >> 24) & 255, r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
        unsigned *q = (unsigned *)(px + y * stride + x * 4); *q = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);  /* premultiply */
    }
    cairo_surface_mark_dirty(s); return icon_fit(s, want);
}

static int mshift(unsigned long m) { int k = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; k++; } return k; }
static unsigned mchan(unsigned long p, unsigned long m) { if (!m) return 0; unsigned long mx = m >> mshift(m); return (unsigned)(((p & m) >> mshift(m)) * 255 / (mx ? mx : 1)); }
static int quiet_err(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }

cairo_surface_t *frame_icon_from_pixmap(Display *dpy, Pixmap icon, Pixmap mask, int want) {
    if (!icon) return NULL;
    /* the pixmaps belong to the client and may be freed under us: errors are swallowed for these few requests */
    XSync(dpy, False); int (*old)(Display *, XErrorEvent *) = XSetErrorHandler(quiet_err);
    Window r; int gx, gy; unsigned pw = 0, ph = 0, bw, pd = 0, mw = 0, mh = 0, md = 0;
    XImage *img = NULL, *mimg = NULL;
    if (XGetGeometry(dpy, icon, &r, &gx, &gy, &pw, &ph, &bw, &pd) && pw && ph && pw <= 1024 && ph <= 1024) {
        img = XGetImage(dpy, icon, 0, 0, pw, ph, AllPlanes, ZPixmap);
        if (mask && XGetGeometry(dpy, mask, &r, &gx, &gy, &mw, &mh, &bw, &md) && md == 1 && mw >= pw && mh >= ph)
            mimg = XGetImage(dpy, mask, 0, 0, pw, ph, AllPlanes, ZPixmap);
    }
    XSync(dpy, False); XSetErrorHandler(old);
    if (!img) { if (mimg) XDestroyImage(mimg); return NULL; }
    Visual *v = DefaultVisual(dpy, DefaultScreen(dpy));
    unsigned long rm = img->red_mask ? img->red_mask : v->red_mask, gm = img->green_mask ? img->green_mask : v->green_mask,
                  bm = img->blue_mask ? img->blue_mask : v->blue_mask;
    if (pd == 32) { rm = 0xff0000; gm = 0xff00; bm = 0xff; }
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char *px = cairo_image_surface_get_data(s); int stride = cairo_image_surface_get_stride(s);
    for (unsigned y = 0; y < ph; y++) for (unsigned x = 0; x < pw; x++) {
        unsigned long p = XGetPixel(img, x, y); unsigned rr, gg, bb, a = 255;
        if (pd == 1) rr = gg = bb = p ? 0 : 255;   /* bitmap icons: 1 = foreground (black) on white, the classic X icon box */
        else { rr = mchan(p, rm); gg = mchan(p, gm); bb = mchan(p, bm); if (pd == 32) a = (p >> 24) & 255; }
        if (mimg && !XGetPixel(mimg, x, y)) a = rr = gg = bb = 0;
        else if (pd != 32) { rr = rr * a / 255; gg = gg * a / 255; bb = bb * a / 255; }   /* depth 32 is premultiplied already (Render) */
        *(unsigned *)(px + y * stride + x * 4) = (a << 24) | (rr << 16) | (gg << 8) | bb;
    }
    cairo_surface_mark_dirty(s); XDestroyImage(img); if (mimg) XDestroyImage(mimg);
    return icon_fit(s, want);
}

cairo_surface_t *theme_default_icon(const struct theme *t, int want) {
    cairo_surface_t *best = NULL; int bsz = 0;
    for (int i = 0; i < t->ndefic; i++) {
        int w = cairo_image_surface_get_width(t->defic[i]), h = cairo_image_surface_get_height(t->defic[i]), sz = w > h ? w : h;
        if (!best || (sz >= want ? (bsz < want || sz < bsz) : (bsz < want && sz > bsz))) { best = t->defic[i]; bsz = sz; }
    }
    return best;
}
