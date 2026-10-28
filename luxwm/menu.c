/* lhwm: popup menus drawn by the window manager (menu.h; README "The window menu"). */
#include "config.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <poll.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>
#ifdef HAVE_UXTHEME
#include <wuxtheme/ut.h>
#endif

#define SHADOW 4   /* XP's menu drop shadow: right and bottom, 4 px (measured on the 4074 Slate window menu) */

struct look {
    void *ut;                           /* ut_theme with a Menu class (NULL: XP's COLOR_MENU drawing) */
    int p_borders, p_bg, p_item, p_sep, p_gutter, p_sys[5];   /* Menu part ids, 0 = the style has none */
    int flat;                           /* FLATMENUS */
    uint32_t menu, text, gray, hltext, hl, hlframe, shadow, hilight, face, dkshadow, light;
    uint32_t t_norm, t_hot, t_dis, t_dishot;
    char font[160];
    int aa, hint;
    int pad, item_h, sep_h, sep_top, sep_ch, text_x, right_pad, gap, gutter_w, glyph_x, glyph_w;
    int asc, th;
};

static void set_rgb(cairo_t *cr, uint32_t c) { cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0); }
static void fill(cairo_t *cr, uint32_t c, int x, int y, int w, int h) { if (w <= 0 || h <= 0) return; set_rgb(cr, c); cairo_rectangle(cr, x, y, w, h); cairo_fill(cr); }

#ifdef HAVE_UXTHEME
static int menu_part(ut_theme *u, const char *name) {
    int id = ut_part(u, "Menu", name), ids[64], n = ut_list_parts(u, "Menu", ids, 64);
    if (id <= 0) return 0;
    for (int i = 0; i < n; i++) if (ids[i] == id) return id;
    return 0;
}
static void ut_paint(cairo_t *cr, ut_theme *u, int part, int state, int x, int y, int w, int h) {
    if (!part || w <= 0 || h <= 0) return;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    ut_rect r = { 0, 0, w, h };
    cairo_surface_flush(s);
    ut_draw_background(u, "Menu", part, state, (uint32_t *)cairo_image_surface_get_data(s), w, h, cairo_image_surface_get_stride(s), &r, NULL);
    cairo_surface_mark_dirty(s);
    cairo_set_source_surface(cr, s, x, y); cairo_paint(cr); cairo_surface_destroy(s);
}
#endif

/* the font: [SysMetrics] MENUFONT as a pango description ("FACE[,Tahoma][ Bold] PXpx", lfHeight = -MulDiv(pt, 96, 72)) */
static void look_build(const struct theme *t, struct look *L) {
    memset(L, 0, sizeof *L);
    int dwm = t->mode == THEME_DWM;
    if (dwm) {   /* the 4074 Jade/Aero menu (jade.msstyles [SysMetrics] / Menu.POPUP*), for a DWM theme without the library */
        L->menu = L->face = 0xf4f4f4; L->text = 0x00025c; L->gray = 0x848589; L->hl = L->hlframe = 0xc5e7f6; L->hltext = 0x00025c;
        L->shadow = 0x4e5358; L->hilight = 0xffffff; L->dkshadow = 0x404040; L->light = 0xf4f4f4; L->flat = 1;
        snprintf(L->font, sizeof L->font, "%s", t->font[0] ? t->font : "Segoe UI,Tahoma 12px");
    } else {     /* XP's Windows Standard scheme, then whatever the style's [SysMetrics] sets */
        L->menu = L->face = L->light = 0xd4d0c8; L->text = 0x000000; L->gray = 0x808080; L->hl = L->hlframe = 0x0a246a; L->hltext = 0xffffff;
        L->shadow = 0x808080; L->hilight = 0xffffff; L->dkshadow = 0x404040; L->flat = 0;
        snprintf(L->font, sizeof L->font, "Tahoma 11px");
    }
    if (t->classic) {   /* Windows Classic: the scheme's colours (COLOR_ indices) and Metrics.menufont */
        const unsigned *C = t->cl;
        L->menu = C[4]; L->text = C[7]; L->gray = C[17]; L->hl = L->hlframe = C[13]; L->hltext = C[14];
        L->face = C[15]; L->shadow = C[16]; L->hilight = C[20]; L->dkshadow = C[21]; L->light = C[22]; L->flat = 0;
        snprintf(L->font, sizeof L->font, "%s", t->cl_menufont);
    }
    L->aa = t->mode == THEME_MSSTYLES ? t->ms_font_aa : CAIRO_ANTIALIAS_SUBPIXEL;
    L->hint = t->mode == THEME_MSSTYLES && t->ms_hint ? t->ms_hint : CAIRO_HINT_STYLE_FULL;
    int item_t = 1, item_b = 1, sep_cy = 2, sep_t = -1, sep_b = -1;   /* XP: text height + CYEDGE + 2 */
#ifdef HAVE_UXTHEME
    ut_theme *u = t->ut;
    if (u) {
        uint32_t c; int v;
        #define SM(name, field) if (ut_get_color(u, "sysmetrics", 0, 0, ut_prop(u, name), &c)) L->field = c
        SM("MENU", menu); SM("MENUTEXT", text); SM("GRAYTEXT", gray); SM("HIGHLIGHT", hlframe); SM("HIGHLIGHTTEXT", hltext);
        SM("BTNSHADOW", shadow); SM("BTNHIGHLIGHT", hilight); SM("BTNFACE", face); SM("DKSHADOW3D", dkshadow); SM("LIGHT3D", light);
        L->hl = L->hlframe; SM("MENUHILIGHT", hl);
        #undef SM
        if (ut_get_bool(u, "sysmetrics", 0, 0, ut_prop(u, "FLATMENUS"), &v)) L->flat = v != 0;
        ut_font f; char s[256] = "";
        if (ut_get_font(u, "sysmetrics", 0, 0, ut_prop(u, "MENUFONT"), &f) && f.face[0]) {
            ut_get_string(u, "sysmetrics", 0, 0, ut_prop(u, "MENUFONT"), s, sizeof s);
            int bold = f.weight >= 600 || strcasestr(s, "bold"), px = f.height < 0 ? -f.height : (f.height * 96 + 36) / 72;
            snprintf(L->font, sizeof L->font, "%.90s%s%s %dpx", f.face, strcmp(f.face, "Tahoma") ? ",Tahoma" : "", bold ? " Bold" : "", px);
            if (strcasestr(s, "natural")) L->hint = CAIRO_HINT_STYLE_NONE;   /* Quality:ClearType-Natural: no grid fitting */
        }
        if ((L->p_item = menu_part(u, "POPUPITEM"))) {   /* the Longhorn / Vista Menu class */
            L->ut = u;
            L->p_borders = menu_part(u, "POPUPBORDERS"); L->p_bg = menu_part(u, "POPUPBACKGROUND"); L->p_gutter = menu_part(u, "POPUPGUTTER");
            if (!(L->p_sep = menu_part(u, "POPUPSEPARATOR"))) L->p_sep = menu_part(u, "SEPARATOR");
            static const char *sys[5] = { NULL, "SYSTEMRESTORE", "SYSTEMMINIMIZE", "SYSTEMMAXIMIZE", "SYSTEMCLOSE" };
            for (int i = 1; i < 5; i++) L->p_sys[i] = menu_part(u, sys[i]);
            int pt = ut_prop(u, "TEXTCOLOR");
            L->t_norm = ut_get_color(u, "Menu", L->p_item, 1, pt, &c) ? c : L->text;
            L->t_hot = ut_get_color(u, "Menu", L->p_item, 2, pt, &c) ? c : L->t_norm;
            L->t_dis = ut_get_color(u, "Menu", L->p_item, 3, pt, &c) ? c : L->gray;
            L->t_dishot = ut_get_color(u, "Menu", L->p_item, 4, pt, &c) ? c : L->t_dis;
            ut_margins m;
            if (ut_get_margins(u, "Menu", L->p_item, 0, ut_prop(u, "CONTENTMARGINS"), &m)) { item_t = m.top; item_b = m.bottom; }
            else item_t = item_b = 3;
            if (L->p_sep) {
                int cx, cy; if (ut_part_size(u, "Menu", L->p_sep, 0, 1, NULL, &cx, &cy) && cy > 0) sep_cy = cy;
                if (ut_get_margins(u, "Menu", L->p_sep, 0, ut_prop(u, "CONTENTMARGINS"), &m) && (m.top || m.bottom)) { sep_t = m.top; sep_b = m.bottom; }
            }
        }
    }
#endif
    if (!L->ut) {   /* XP mndraw.c colours */
        L->t_norm = L->text; L->t_hot = L->flat ? L->hltext : L->hltext;
        L->t_dis = L->gray; L->t_dishot = L->gray == (L->flat ? L->hl : L->hlframe) ? L->hltext : L->gray;
        if (!L->flat) L->hl = L->hlframe;
        if (dwm) item_t = item_b = 3;   /* the 4074 Menu.POPUPITEM content margins */
    }
    /* text metrics: GDI's tmAscent / tmHeight (rounded unhinted pango metrics) */
    PangoFontMap *fm = pango_cairo_font_map_get_default(); PangoContext *ctx = pango_font_map_create_context(fm);
    { cairo_font_options_t *mfo = cairo_font_options_create(); cairo_font_options_set_hint_metrics(mfo, CAIRO_HINT_METRICS_OFF);
      pango_cairo_context_set_font_options(ctx, mfo); cairo_font_options_destroy(mfo); }
    PangoFontDescription *fd = pango_font_description_from_string(L->font);
    PangoFontMetrics *met = pango_context_get_metrics(ctx, fd, NULL);
    L->asc = (pango_font_metrics_get_ascent(met) + PANGO_SCALE / 2) / PANGO_SCALE;
    L->th = L->asc + (pango_font_metrics_get_descent(met) + PANGO_SCALE / 2) / PANGO_SCALE;
    int cw = (pango_font_metrics_get_approximate_char_width(met) + PANGO_SCALE / 2) / PANGO_SCALE;
    pango_font_metrics_unref(met); pango_font_description_free(fd); g_object_unref(ctx);
    /* geometry (XP: 1 px outline + CXEDGE inside; item = text + CYEDGE + 2; separator CYMENUSIZE / 2 with the etch at its middle - 1;
       text after MNLEFTMARGIN + the 13 px check column + MNXSPACE; the 4074 menus measure the same: border 1 + 2, text 22 px in) */
    L->pad = 3;
    L->item_h = L->th + item_t + item_b + (L->ut ? 0 : 2);
    if (L->p_sep) { L->sep_ch = sep_cy; L->sep_top = sep_t >= 0 ? sep_t : 3; L->sep_h = sep_cy + L->sep_top + (sep_b >= 0 ? sep_b : 3); }
    else { L->sep_h = 9; L->sep_top = 9 / 2 - 1; L->sep_ch = 2; }
    L->glyph_x = 2; L->glyph_w = 13;
    L->text_x = 19;
    if (L->p_gutter) { L->gutter_w = 26; L->glyph_x = 4; L->glyph_w = 18; L->text_x = 31; }   /* Vista: icons/checks in a gutter */
    L->gap = 2 * (cw > 0 ? cw : 6);
    L->right_pad = 17;
}

/* "&Move" -> "Move" and the byte offset of the mnemonic (-1: none) */
static void strip_amp(const char *s, char *out, size_t n, int *mn) {
    size_t o = 0; *mn = -1;
    for (const char *p = s ? s : ""; *p && o + 1 < n; p++) {
        if (*p == '&' && p[1] == '&') { out[o++] = '&'; p++; continue; }
        if (*p == '&' && p[1]) { if (*mn < 0) *mn = (int)o; continue; }
        out[o++] = *p;
    }
    out[o] = 0;
}

static PangoLayout *layout(cairo_t *cr, const struct look *L, const char *text, int bold, int cues) {
    char buf[256]; int mn; strip_amp(text, buf, sizeof buf, &mn);
    PangoLayout *lay = pango_cairo_create_layout(cr);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, (cairo_antialias_t)L->aa); cairo_font_options_set_subpixel_order(fo, CAIRO_SUBPIXEL_ORDER_RGB);
    cairo_font_options_set_hint_style(fo, (cairo_hint_style_t)L->hint); cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_ON);
    pango_cairo_context_set_font_options(pango_layout_get_context(lay), fo); cairo_font_options_destroy(fo);
    pango_layout_context_changed(lay);
    PangoFontDescription *fd = pango_font_description_from_string(L->font);
    if (bold) pango_font_description_set_weight(fd, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(lay, fd); pango_font_description_free(fd);
    pango_layout_set_text(lay, buf, -1);
    if (cues && mn >= 0) {
        PangoAttrList *al = pango_attr_list_new(); PangoAttribute *a = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        a->start_index = mn; a->end_index = mn + g_utf8_skip[(unsigned char)buf[mn]];
        pango_attr_list_insert(al, a); pango_layout_set_attributes(lay, al); pango_attr_list_unref(al);
    }
    return lay;
}

struct geom { int w, h, y[64], ih[64]; };

static void measure(const struct look *L, const struct wmmenu_item *items, int n, struct geom *g) {
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *cr = cairo_create(s);
    int tw = 0, aw = 0, y = L->pad;
    for (int i = 0; i < n && i < 64; i++) {
        g->y[i] = y;
        if (items[i].flags & WMMENU_SEPARATOR) { g->ih[i] = L->sep_h; y += L->sep_h; continue; }
        g->ih[i] = L->item_h; y += L->item_h;
        int w, h; PangoLayout *lay = layout(cr, L, items[i].text, items[i].flags & WMMENU_DEFAULT, 0);
        pango_layout_get_pixel_size(lay, &w, &h); g_object_unref(lay);
        if (w > tw) tw = w;
        if (items[i].accel) { lay = layout(cr, L, items[i].accel, items[i].flags & WMMENU_DEFAULT, 0);
            pango_layout_get_pixel_size(lay, &w, &h); g_object_unref(lay); if (w > aw) aw = w; }
    }
    g->w = 2 * L->pad + L->text_x + tw + (aw ? L->gap + aw : 0) + L->right_pad;
    g->h = y + L->pad;
    cairo_destroy(cr); cairo_surface_destroy(s);
}

/* the system menu bitmaps in a 13 x 13 cell: XP draws them from Marlett ('0' minimise, '1' maximise, '2' restore, 'r' close) */
static void glyph(cairo_t *cr, int g, int x, int y) {
    #define R(a, b, c, d) cairo_rectangle(cr, x + (a), y + (b), c, d)
    switch (g) {
    case WMMENU_GLYPH_MINIMIZE: R(3, 8, 6, 2); break;
    case WMMENU_GLYPH_MAXIMIZE: R(2, 2, 9, 2); R(2, 4, 1, 6); R(10, 4, 1, 6); R(2, 10, 9, 1); break;
    case WMMENU_GLYPH_RESTORE:  R(4, 1, 6, 2); R(9, 3, 1, 5); R(8, 7, 1, 1);
                                R(1, 5, 6, 2); R(1, 7, 1, 4); R(6, 7, 1, 4); R(1, 10, 6, 1); break;
    case WMMENU_GLYPH_CLOSE:    for (int i = 0; i < 6; i++) { R(3 + i, 3 + i, 2, 1); R(8 - i, 3 + i, 2, 1); } break;
    }
    #undef R
    cairo_fill(cr);
}

static void paint(cairo_t *cr, const struct look *L, const struct wmmenu_item *items, int n, const struct geom *g, int sel, int cues) {
    int W = g->w, H = g->h, iw = W - 2 * L->pad;
    /* the plate */
#ifdef HAVE_UXTHEME
    if (L->ut) {
        fill(cr, L->menu, 0, 0, W, H);
        if (L->p_borders) ut_paint(cr, L->ut, L->p_borders, 0, 0, 0, W, H);
        else { if (L->p_bg) ut_paint(cr, L->ut, L->p_bg, 0, 0, 0, W, H);
               set_rgb(cr, L->shadow); cairo_rectangle(cr, 0.5, 0.5, W - 1, H - 1); cairo_set_line_width(cr, 1); cairo_stroke(cr); }
        if (L->p_gutter) ut_paint(cr, L->ut, L->p_gutter, 0, L->pad, L->pad, L->gutter_w, H - 2 * L->pad);
    } else
#endif
    if (L->flat) {   /* mndraw.c MNDrawFlatMenu...: 1 px BTNSHADOW outline, MENU inside */
        fill(cr, L->shadow, 0, 0, W, H); fill(cr, L->menu, 1, 1, W - 2, H - 2);
    } else {         /* classic popup: EDGE_RAISED (LIGHT3D / DKSHADOW, BTNHIGHLIGHT / BTNSHADOW), then MENU */
        fill(cr, L->dkshadow, 0, 0, W, H); fill(cr, L->light, 0, 0, W - 1, H - 1);
        fill(cr, L->shadow, 1, 1, W - 2, H - 2); fill(cr, L->hilight, 1, 1, W - 3, H - 3);
        fill(cr, L->menu, 2, 2, W - 4, H - 4);
    }
    for (int i = 0; i < n && i < 64; i++) {
        const struct wmmenu_item *it = &items[i];
        int y = g->y[i], h = g->ih[i], x = L->pad;
        if (it->flags & WMMENU_SEPARATOR) {
#ifdef HAVE_UXTHEME
            if (L->ut && L->p_sep) { ut_paint(cr, L->ut, L->p_sep, 0, x, y + L->sep_top, iw, L->sep_ch); continue; }
#endif
            fill(cr, L->shadow, x + 1, y + L->sep_top, iw - 2, 1); fill(cr, L->hilight, x + 1, y + L->sep_top + 1, iw - 2, 1);   /* BDR_SUNKENOUTER */
            continue;
        }
        int hot = i == sel, dis = (it->flags & WMMENU_DISABLED) != 0;
        uint32_t tc = dis ? (hot ? L->t_dishot : L->t_dis) : hot ? L->t_hot : L->t_norm;
        if (hot) {
#ifdef HAVE_UXTHEME
            if (L->ut) ut_paint(cr, L->ut, L->p_item, dis ? 4 : 2, x, y, iw, h);
            else
#endif
            { fill(cr, L->hl, x, y, iw, h);
              if (L->flat) { set_rgb(cr, L->hlframe); cairo_rectangle(cr, x + 0.5, y + 0.5, iw - 1, h - 1); cairo_set_line_width(cr, 1); cairo_stroke(cr); } }
        }
        /* XP greys by embossing when the menu is the 3D face colour (classic), else with GRAYTEXT */
        int emboss = dis && !hot && !L->ut && !L->flat && L->menu == L->face;
        if (it->glyph) {
#ifdef HAVE_UXTHEME
            if (L->ut && L->p_sys[it->glyph]) {
                int cx = 16, cy = 16; ut_part_size(L->ut, "Menu", L->p_sys[it->glyph], dis ? 2 : 1, 1, NULL, &cx, &cy);
                ut_paint(cr, L->ut, L->p_sys[it->glyph], dis ? 2 : 1, x + L->glyph_x + (L->glyph_w - cx) / 2, y + (h - cy) / 2, cx, cy);
            } else if (!L->ut)
#else
            if (1)
#endif
            {
                int gx = x + L->glyph_x, gy = y + (h - 13) / 2;
                if (emboss) { set_rgb(cr, L->hilight); glyph(cr, it->glyph, gx + 1, gy + 1); set_rgb(cr, L->shadow); }
                else set_rgb(cr, tc);
                glyph(cr, it->glyph, gx, gy);
            }
        }
        int ty = y + (h - L->th) / 2;
        for (int k = 0; k < 2; k++) {
            const char *s = k ? it->accel : it->text;
            if (!s) continue;
            PangoLayout *lay = layout(cr, L, s, it->flags & WMMENU_DEFAULT, cues && !k);
            int tw, th2; pango_layout_get_pixel_size(lay, &tw, &th2);
            int base = pango_layout_get_baseline(lay) / PANGO_SCALE;
            int tx = k ? W - L->pad - L->right_pad - tw : x + L->text_x;
            int yy = ty + L->asc - base;
            if (emboss) { set_rgb(cr, L->hilight); cairo_move_to(cr, tx + 1, yy + 1); pango_cairo_show_layout(cr, lay); set_rgb(cr, L->shadow); }
            else set_rgb(cr, tc);
            cairo_move_to(cr, tx, yy); pango_cairo_show_layout(cr, lay);
            g_object_unref(lay);
        }
        if (it->flags & WMMENU_CHECKED) { set_rgb(cr, tc); cairo_arc(cr, x + L->glyph_x + 6.5, y + h / 2.0, 2.5, 0, 6.2832); cairo_fill(cr); }
    }
}

/* the drop shadow right of and below the W x H menu, alpha per pixel (4074 Slate measurement: 0.44 0.33 0.17 0.055 outwards, starting
   5 px in from the far corner with a 4 px ramp) */
static double shadow_alpha(int x, int y, int W, int H) {
    static const double p[5] = { 0, 0.44, 0.33, 0.17, 0.055 }, q[5] = { 0.12, 0.33, 0.65, 0.88, 1 };
    int dx = x - W + 1, dy = y - H + 1;   /* 1..SHADOW outside the right / bottom edge */
    if (dx >= 1 && dy >= 1) return dx <= SHADOW && dy <= SHADOW ? p[dx] * p[dy] / p[1] : 0;
    if (dx >= 1 && dx <= SHADOW) { int k = y - 5; return k < 0 ? 0 : p[dx] * (k < 5 ? q[k] : 1); }
    if (dy >= 1 && dy <= SHADOW) { int k = x - 5; return k < 0 ? 0 : p[dy] * (k < 5 ? q[k] : 1); }
    return 0;
}

cairo_surface_t *wmmenu_render(const struct theme *t, const struct wmmenu_item *items, int n, int sel, unsigned flags, int *w, int *h) {
    struct look L; struct geom g; look_build(t, &L); measure(&L, items, n, &g);
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.w + SHADOW, g.h + SHADOW);
    cairo_t *cr = cairo_create(img);
    paint(cr, &L, items, n, &g, sel, flags & WMMENU_KEYBOARD);
    cairo_destroy(cr); cairo_surface_flush(img);
    unsigned char *d = cairo_image_surface_get_data(img); int st = cairo_image_surface_get_stride(img);
    for (int y = 0; y < g.h + SHADOW; y++) for (int x = 0; x < g.w + SHADOW; x++) {
        if (x < g.w && y < g.h) continue;
        double a = shadow_alpha(x, y, g.w, g.h); ((uint32_t *)(d + y * st))[x] = (uint32_t)(a * 255 + 0.5) << 24;   /* black, premultiplied */
    }
    cairo_surface_mark_dirty(img);
    if (w) *w = g.w;
    if (h) *h = g.h;
    return img;
}

/* ---- the popup ---- */
struct run {
    const struct wmmenu_env *env; const struct wmmenu_item *items; int n;
    struct look L; struct geom g; Window win; int x, y, W, H; int sel, cues;
    cairo_surface_t *under;             /* no compositor: the screen under the menu + shadow, for the blended shadow */
};

static void run_paint(struct run *r) {
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r->W, r->H); cairo_t *cr = cairo_create(img);
    if (r->under) { cairo_set_source_surface(cr, r->under, 0, 0); cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_paint(cr); cairo_set_operator(cr, CAIRO_OPERATOR_OVER); }
    /* shadow */
    cairo_surface_t *sh = cairo_image_surface_create(CAIRO_FORMAT_A8, r->W, r->H);
    unsigned char *d = cairo_image_surface_get_data(sh); int st = cairo_image_surface_get_stride(sh);
    cairo_surface_flush(sh);
    for (int y = 0; y < r->H; y++) for (int x = 0; x < r->W; x++) d[y * st + x] = (unsigned char)(shadow_alpha(x, y, r->g.w, r->g.h) * 255 + 0.5);
    cairo_surface_mark_dirty(sh);
    cairo_set_source_rgb(cr, 0, 0, 0); cairo_mask_surface(cr, sh, 0, 0); cairo_surface_destroy(sh);
    paint(cr, &r->L, r->items, r->n, &r->g, r->sel, r->cues);
    cairo_destroy(cr);
    const struct wmmenu_env *e = r->env;
    cairo_surface_t *xs = cairo_xlib_surface_create(e->dpy, r->win, e->visual, r->W, r->H); cairo_t *xc = cairo_create(xs);
    cairo_set_operator(xc, CAIRO_OPERATOR_SOURCE); cairo_set_source_surface(xc, img, 0, 0); cairo_paint(xc);
    cairo_destroy(xc); cairo_surface_flush(xs); cairo_surface_destroy(xs); cairo_surface_destroy(img);
    XFlush(e->dpy);
}

static int item_at(const struct run *r, int rx, int ry) {
    int x = rx - r->x, y = ry - r->y;
    if (x < r->L.pad || x >= r->g.w - r->L.pad || y < r->L.pad || y >= r->g.h - r->L.pad) return -1;
    for (int i = 0; i < r->n && i < 64; i++)
        if (y >= r->g.y[i] && y < r->g.y[i] + r->g.ih[i]) return (r->items[i].flags & WMMENU_SEPARATOR) ? -1 : i;
    return -1;
}
static int inside(const struct run *r, int rx, int ry) { return rx >= r->x && rx < r->x + r->g.w && ry >= r->y && ry < r->y + r->g.h; }

static int next_item(const struct run *r, int from, int dir) {
    for (int k = 0, i = from; k < r->n; k++) {
        i = (i + dir + r->n) % r->n;
        if (!(r->items[i].flags & WMMENU_SEPARATOR)) return i;
    }
    return -1;
}

static Bool menu_pred(Display *d, XEvent *ev, XPointer arg) {
    (void)d; (void)arg;
    switch (ev->type) {
    case MotionNotify: case ButtonPress: case ButtonRelease: case KeyPress:
    case Expose: case MapRequest: case ConfigureRequest:
        return True;
    }
    return False;
}

static double ms_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }

int wmmenu_popup(const struct wmmenu_env *e, int x, int y, const struct wmmenu_item *items, int n, unsigned flags, unsigned button) {
    struct run r; memset(&r, 0, sizeof r);
    if (n <= 0) return WMMENU_CANCEL;
    if (n > 64) n = 64;
    r.env = e; r.items = items; r.n = n; r.cues = (flags & WMMENU_KEYBOARD) != 0;
    look_build(e->theme, &r.L); measure(&r.L, items, n, &r.g);
    r.W = r.g.w + SHADOW; r.H = r.g.h + SHADOW;
    /* fit into the monitor: a context menu flips to the left of / above the point, the window menu is shifted (TrackPopupMenu) */
    int bx1 = e->bx + e->bw, by1 = e->by + e->bh;
    if (x + r.g.w > bx1) x = (flags & WMMENU_FLIP) && x - r.g.w >= e->bx ? x - r.g.w : bx1 - r.g.w;
    if (y + r.g.h > by1) y = (flags & WMMENU_FLIP) && y - r.g.h >= e->by ? y - r.g.h : by1 - r.g.h;
    if (x < e->bx) x = e->bx;
    if (y < e->by) y = e->by;
    r.x = x; r.y = y;
    r.sel = (flags & WMMENU_KEYBOARD) ? next_item(&r, -1, 1) : -1;   /* XP: the first item, greyed or not, when opened from the keyboard */
    Display *dpy = e->dpy;
    if (!e->composited) {   /* XP's drop shadow is a copy of what lies under it, darkened */
        int sw = DisplayWidth(dpy, e->screen), sh = DisplayHeight(dpy, e->screen);
        cairo_surface_t *rs = cairo_xlib_surface_create(dpy, e->root, DefaultVisual(dpy, e->screen), sw, sh);
        r.under = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.W, r.H); cairo_t *cr = cairo_create(r.under);
        cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr);
        cairo_set_source_surface(cr, rs, -x, -y); cairo_paint(cr); cairo_destroy(cr); cairo_surface_destroy(rs);
    }
    XSetWindowAttributes a = { .override_redirect = True, .colormap = e->cmap, .border_pixel = 0, .background_pixmap = None, .save_under = True,
                               .event_mask = ExposureMask };
    r.win = XCreateWindow(dpy, e->root, x, y, r.W, r.H, 0, e->depth, InputOutput, e->visual,
                          CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixmap | CWSaveUnder | CWEventMask, &a);
    Atom wt = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False), pm = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    XChangeProperty(dpy, r.win, wt, XA_ATOM, 32, PropModeReplace, (unsigned char *)&pm, 1);
    long one = 1; XChangeProperty(dpy, r.win, XInternAtom(dpy, "_LHWM_MENU", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
    XClassHint ch = { (char *)"lhwm-menu", (char *)"lhwm" }; XSetClassHint(dpy, r.win, &ch);
    XStoreName(dpy, r.win, "lhwm window menu");
    run_paint(&r);   /* before the map: the first frame the compositor sees is the finished menu */
    XMapRaised(dpy, r.win);
    int gp = GrabNotViewable, gk = GrabNotViewable;
    for (int i = 0; i < 50; i++) {   /* a client's own grab (or lhwm's passive key grab) may be ending */
        if (gp != GrabSuccess) gp = XGrabPointer(dpy, r.win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        if (gk != GrabSuccess) gk = XGrabKeyboard(dpy, e->root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        if (gp == GrabSuccess && gk == GrabSuccess) break;
        struct timespec ts = { 0, 5000000 }; nanosleep(&ts, NULL);
    }
    int result = WMMENU_CANCEL;
    if (gp != GrabSuccess || gk != GrabSuccess) { fprintf(stderr, "luxwm: menu: grab failed (pointer %d, keyboard %d)\n", gp, gk); goto out; }
    double t0 = ms_now(); bool down = button != 0, mouse_sel = false; int px0 = -1, py0 = -1;
    { Window rw, cw; int wx, wy; unsigned m; XQueryPointer(dpy, e->root, &rw, &cw, &px0, &py0, &wx, &wy, &m); }
    for (bool done = false; !done; ) {
        XEvent ev; XIfEvent(dpy, &ev, menu_pred, NULL);
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.window == r.win) { if (ev.xexpose.count == 0) run_paint(&r); }
            else if (e->dispatch) e->dispatch(&ev, e->dispatch_arg);
            break;
        case MapRequest: case ConfigureRequest:
            if (e->dispatch) e->dispatch(&ev, e->dispatch_arg);
            break;
        case MotionNotify: {
            while (XCheckTypedEvent(dpy, MotionNotify, &ev)) ;
            int i = item_at(&r, ev.xmotion.x_root, ev.xmotion.y_root);
            if (i >= 0 || mouse_sel || inside(&r, ev.xmotion.x_root, ev.xmotion.y_root)) {
                if (i != r.sel) { r.sel = i; run_paint(&r); }
                mouse_sel = i >= 0;
            }
            break;
        }
        case ButtonPress: {
            int rx = ev.xbutton.x_root, ry = ev.xbutton.y_root;
            if (!inside(&r, rx, ry)) {
                if (e->dbl_w > 0 && rx >= e->dbl_x && rx < e->dbl_x + e->dbl_w && ry >= e->dbl_y && ry < e->dbl_y + e->dbl_h && ms_now() - t0 < e->dbl_ms)
                    result = WMMENU_DBLCLICK;
                done = true;   /* a click outside dismisses the menu (and is not passed on, as in XP) */
                break;
            }
            down = true; int i = item_at(&r, rx, ry);
            if (i != r.sel) { r.sel = i; run_paint(&r); }
            break;
        }
        case ButtonRelease: {
            int rx = ev.xbutton.x_root, ry = ev.xbutton.y_root, i = item_at(&r, rx, ry);
            if (!down) break;
            down = false;
            if (i >= 0 && !(items[i].flags & WMMENU_DISABLED)) { result = i; done = true; }
            /* the release of the press that opened the menu, where it was pressed or off the items: the menu stays */
            (void)px0; (void)py0;
            break;
        }
        case KeyPress: {
            KeySym k = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            r.cues = 1;   /* a key shows the keyboard cues from now on (XP) */
            if (k == XK_Escape || k == XK_Alt_L || k == XK_Alt_R || k == XK_F10 || k == XK_Meta_L || k == XK_Meta_R) { done = true; break; }
            if (k == XK_Down || k == XK_KP_Down || k == XK_Tab) r.sel = next_item(&r, r.sel < 0 ? -1 : r.sel, 1);
            else if (k == XK_Up || k == XK_KP_Up) r.sel = next_item(&r, r.sel < 0 ? 0 : r.sel, -1);
            else if (k == XK_Home || k == XK_KP_Home) r.sel = next_item(&r, -1, 1);
            else if (k == XK_End || k == XK_KP_End) r.sel = next_item(&r, 0, -1);
            else if (k == XK_Return || k == XK_KP_Enter || k == XK_space) {
                if (r.sel >= 0 && !(items[r.sel].flags & WMMENU_DISABLED)) { result = r.sel; done = true; break; }
            } else {
                /* a mnemonic: the next item with it after the selection; the only one with it is chosen at once (greyed: selected) */
                char buf[256]; int mn, first = -1, count = 0, after = -1;
                gunichar kc = k < 0x100 ? (gunichar)k : (k & 0xff000000) == 0x01000000 ? (gunichar)(k & 0xffffff) : 0;   /* Latin-1 / Unicode keysyms */
                for (int i = 0; i < n; i++) {
                    if (items[i].flags & WMMENU_SEPARATOR) continue;
                    strip_amp(items[i].text, buf, sizeof buf, &mn);
                    if (mn < 0 || !kc || g_unichar_tolower(g_utf8_get_char(buf + mn)) != g_unichar_tolower(kc))
                        continue;
                    count++;
                    if (first < 0) first = i;
                    if (after < 0 && i > r.sel) after = i;
                }
                if (count == 1) {
                    r.sel = first;
                    if (!(items[first].flags & WMMENU_DISABLED)) { result = first; done = true; break; }
                } else if (count > 1) r.sel = after >= 0 ? after : first;
            }
            mouse_sel = false;
            run_paint(&r);
            break;
        }
        }
    }
out:
    XUngrabPointer(dpy, CurrentTime); XUngrabKeyboard(dpy, CurrentTime);
    XDestroyWindow(dpy, r.win); XFlush(dpy);
    if (r.under) cairo_surface_destroy(r.under);
    if (result >= 0 && items[result].cb) items[result].cb(items[result].arg, items[result].id);
    return result;
}
