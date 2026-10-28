/* lhnav: the Longhorn navigation bar as a component. API and geometry: lhnav.h, ../NAVBAR.md.
   Dependencies: libuxtheme only (art, and all text through its DrawThemeText: the GDI text of flxp), libpng (art_dir strips);
   the pixels are lhnav_px.c's, not cairo's. */
#include "lhnav.h"
#include "lhnav_res.h"
#include "lhnav_px.h"
#include "lhde_data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef HAVE_UXTHEME
#include <wuxtheme/ut.h>
#endif

/* ---- metrics ---- */

static void place_auto(lhnav_place *p) { p->x = p->y = p->w = p->h = LHNAV_AUTO; p->dx = p->dy = 0; }

void lhnav_metrics_default(lhnav_metrics *m)
{
    memset(m, 0, sizeof *m);
    m->border_t = 0; m->border_l = 0;
    m->deco_h = 15; m->orb_h = 30; m->gap = 2; m->box_h = 23; m->lines = 2; m->resize_rows = 3;
    m->pad_l = 0; m->pad_r = 4; m->title_x = 6; m->box_gap = 8; m->search_w = 180; m->search_min = 90;
    m->crumb_pad = 6; m->sep_w = 15; m->crumb_x = 2; m->title_dy = 0; m->crumb_dy = 0;
    m->title_rgb = 0x000000; m->crumb_rgb = 0xFFFFFFFFu; m->glow_rgb = 0xFFFFFFFFu; m->plain_rgb = 0x000000;
    m->glow_size = -1; m->glow_strength = 2.5;
    m->address_field = 1; m->search_field = 1; m->field_alpha = 0.30;
    m->variant = LHNAV_SMALL; m->header_h = LHNAV_AUTO;
    place_auto(&m->orb); place_auto(&m->address); place_auto(&m->search);
    m->title_dx = 0; m->title_baseline = LHNAV_AUTO; m->crumb_text_dx = m->crumb_text_dy = 0;
    snprintf(m->tools, sizeof m->tools, "%s", "search_field");
    m->tool_size = 0; m->tool_icon = 16; m->tool_gap = 4; m->tools_dx = m->tools_dy = 0; m->sep_last = 2;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

static uint32_t parse_rgb(const char *v)
{
    if (!strcasecmp(v, "style") || !strcasecmp(v, "default")) return 0xFFFFFFFFu;
    if (*v == '#') v++;
    return (uint32_t)strtoul(v, NULL, 16) & 0xFFFFFF;
}

/* "[preset NAME]" -> NAME (into out), 1; another section header: 0 with out = "" */
static int section_name(const char *s, char *out, size_t n)
{
    out[0] = 0;
    if (*s != '[') return 0;
    const char *e = strrchr(s, ']');
    if (!e) return 0;
    char buf[128]; snprintf(buf, sizeof buf, "%.*s", (int)(e - s - 1), s + 1);
    char *b = trim(buf);
    if (strncasecmp(b, "preset", 6) || (b[6] != ' ' && b[6] != '\t')) return 0;
    snprintf(out, n, "%s", trim(b + 7));
    return out[0] != 0;
}

/* one key = value into m */
static void set_key(lhnav_metrics *m, const char *path, char *k, char *v)
{
    struct { const char *k; int *v; int autok; } ints[] = {
        { "deco_h", &m->deco_h, 0 }, { "orb_h", &m->orb_h, 0 }, { "gap", &m->gap, 0 }, { "box_h", &m->box_h, 0 }, { "lines", &m->lines, 0 },
        { "resize_rows", &m->resize_rows, 0 }, { "pad_l", &m->pad_l, 0 }, { "pad_r", &m->pad_r, 0 }, { "title_x", &m->title_x, 0 },
        { "box_gap", &m->box_gap, 0 }, { "search_w", &m->search_w, 0 }, { "search_min", &m->search_min, 0 }, { "crumb_pad", &m->crumb_pad, 0 },
        { "sep_w", &m->sep_w, 0 }, { "crumb_x", &m->crumb_x, 0 }, { "title_dy", &m->title_dy, 0 }, { "crumb_dy", &m->crumb_dy, 0 },
        { "glow_size", &m->glow_size, 0 }, { "address_field", &m->address_field, 0 }, { "search_field", &m->search_field, 0 },
        { "header_h", &m->header_h, 1 }, { "title_dx", &m->title_dx, 0 }, { "title_baseline", &m->title_baseline, 1 },
        { "crumb_text_dx", &m->crumb_text_dx, 0 }, { "crumb_text_dy", &m->crumb_text_dy, 0 },
        { "orb_x", &m->orb.x, 1 }, { "orb_y", &m->orb.y, 1 }, { "orb_dx", &m->orb.dx, 0 }, { "orb_dy", &m->orb.dy, 0 },
        { "address_x", &m->address.x, 1 }, { "address_y", &m->address.y, 1 }, { "address_dx", &m->address.dx, 0 },
        { "address_dy", &m->address.dy, 0 }, { "address_w", &m->address.w, 1 }, { "address_h", &m->address.h, 1 },
        { "search_x", &m->search.x, 1 }, { "search_y", &m->search.y, 1 }, { "search_dx", &m->search.dx, 0 },
        { "search_dy", &m->search.dy, 0 }, { "search_h", &m->search.h, 1 },
        { "tool_size", &m->tool_size, 0 }, { "tool_icon", &m->tool_icon, 0 }, { "tool_gap", &m->tool_gap, 0 },
        { "tools_dx", &m->tools_dx, 0 }, { "tools_dy", &m->tools_dy, 0 }, { "sep_last", &m->sep_last, 0 },
    };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) if (!strcmp(k, ints[i].k)) {
        if (!*v || !strcasecmp(v, "auto")) {   /* empty: automatic (placement keys) / the built-in default (the rest) */
            if (ints[i].autok) *ints[i].v = LHNAV_AUTO;
            else { lhnav_metrics d; lhnav_metrics_default(&d); *ints[i].v = *(int *)((char *)&d + ((char *)ints[i].v - (char *)m)); }
        } else *ints[i].v = atoi(v);
        return;
    }
    if (!strcmp(k, "variant")) m->variant = !strcasecmp(v, "big") || !strcasecmp(v, "medium") || !strcmp(v, "1") ? LHNAV_BIG : LHNAV_SMALL;
    else if (!strcmp(k, "title_font")) snprintf(m->title_font, sizeof m->title_font, "%s", v);
    else if (!strcmp(k, "crumb_font")) snprintf(m->crumb_font, sizeof m->crumb_font, "%s", v);
    else if (!strcmp(k, "title_rgb")) m->title_rgb = parse_rgb(v);
    else if (!strcmp(k, "crumb_rgb")) m->crumb_rgb = parse_rgb(v);
    else if (!strcmp(k, "glow_rgb")) m->glow_rgb = parse_rgb(v);
    else if (!strcmp(k, "plain_rgb")) m->plain_rgb = parse_rgb(v);
    else if (!strcmp(k, "glow_strength")) m->glow_strength = atof(v);
    else if (!strcmp(k, "field_alpha")) m->field_alpha = atof(v);
    else if (!strcmp(k, "tools")) snprintf(m->tools, sizeof m->tools, "%s", *v ? v : "search_field");
    else if (!strcmp(k, "art_dir") || !strcmp(k, "icon_dir")) {
        char *d = k[0] == 'a' ? m->art_dir : m->icon_dir; size_t dn = sizeof m->art_dir;
        /* lhde: icon_dir is relative to the data directories (resources/4074, lhde_data.h); art_dir to the frame set (nav.ini's
           directory, or the same frames/NAME/ in another data directory) */
        if (!*v) d[0] = 0;
        else if (v[0] == '/' || v[0] == '~' || k[0] == 'i') lhde_data_resolve(v, d, dn);
        else if (!strrchr(path, '/')) snprintf(d, dn, "%s", v);
        else { char nd[1024]; snprintf(nd, sizeof nd, "%.*s", (int)(strrchr(path, '/') - path), path); lhde_data_find_in(nd, v, d, dn); }
    }
}

/* split a line into key and value (a trailing " # comment" / " ; comment" is dropped, except #RRGGBB in colour keys and art_dir);
   0 for blank, comment and section lines */
static int key_value(char *line, char **k, char **v)
{
    char *s = trim(line);
    if (!*s || *s == '#' || *s == ';' || *s == '[') return 0;
    char *eq = strchr(s, '=');
    if (!eq) return 0;
    *eq = 0;
    *k = trim(s); *v = trim(eq + 1);
    for (char *c = *v; *c; c++) if ((*c == '#' || *c == ';') && (c == *v || c[-1] == ' ' || c[-1] == '\t') && strcmp(*k, "art_dir") && strcmp(*k, "icon_dir") && strcmp(*k, "tools")) {
        if (*c == '#' && strstr(*k, "rgb")) continue;   /* #RRGGBB is a value, not a comment */
        *c = 0; *v = trim(*v); break; }
    return 1;
}

int lhnav_presets(const char *path, char (*names)[48], int max)
{
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return 0;
    char line[700], name[48]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (section_name(s, name, sizeof name) && n < max) snprintf(names[n++], 48, "%s", name);
    }
    fclose(f);
    return n;
}

int lhnav_metrics_load_preset(lhnav_metrics *m, const char *path, const char *preset)
{
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return 0;
    char line[700], want[48] = "", first[48] = "", sec[48] = "";
    /* pass 1: the keys outside the sections (and the file's `preset`) */
    int in_sec = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line), *k, *v;
        if (*s == '[') { in_sec = 1; if (section_name(s, sec, sizeof sec) && !first[0]) snprintf(first, sizeof first, "%s", sec); continue; }
        if (in_sec || !key_value(s, &k, &v)) continue;
        if (!strcmp(k, "preset")) snprintf(want, sizeof want, "%s", v);
        else set_key(m, path, k, v);
    }
    if (preset && *preset) {   /* the caller's choice, if the file has it */
        char names[64][48]; int n = lhnav_presets(path, names, 64);
        for (int i = 0; i < n; i++) if (!strcmp(names[i], preset)) snprintf(want, sizeof want, "%s", preset);
    }
    if (!want[0]) snprintf(want, sizeof want, "%s", first);
    m->preset[0] = 0;
    /* pass 2: the chosen section */
    if (want[0]) {
        rewind(f);
        int on = 0;
        while (fgets(line, sizeof line, f)) {
            char *s = trim(line), *k, *v;
            if (*s == '[') { on = section_name(s, sec, sizeof sec) && !strcmp(sec, want); if (on) snprintf(m->preset, sizeof m->preset, "%s", want); continue; }
            if (on && key_value(s, &k, &v) && strcmp(k, "preset")) set_key(m, path, k, v);
        }
    }
    fclose(f);
    return 1;
}

int lhnav_metrics_load(lhnav_metrics *m, const char *path) { return lhnav_metrics_load_preset(m, path, NULL); }

static unsigned font_gen = 1;   /* bumped by lhnav_add_font_dir: measured metrics and widths are stale */

void lhnav_add_font_dir(const char *dir)
{
    if (!dir || !*dir) return;
#ifdef HAVE_UXTHEME
    ut_add_font_dir(dir);
#endif
    font_gen++;
}

/* ---- state ---- */

enum { ST_DISABLED, ST_NORMAL, ST_HOT, ST_PRESSED, ST_SPLITPRESSED, NSTATES };
/* st: orb-only images (rows [0, split) of the travel art, hot/pressed synthesized; the small variant), full: the whole art with the
   drop-down arrow part (the big variant; SPLITPRESSED = the style's state for a pressed drop-down, else PRESSED); source resolution */
struct art { px_img *st[NSTATES], *full[NSTATES]; };
struct rect { int x, y, w, h; };
/* a toolbar item (nav.ini `tools`) */
struct tool { int kind; char *arg, *label, *tip; struct rect r; };
enum { ICN_SEARCH, ICN_FOLDER, ICN_VIEWS, ICN_NEWFOLDER, NICONS };
static const char *const icon_names[NICONS] = { "search", "folder", "views", "newfolder" };

/* a text font: the theme part whose FONT libuxtheme's DrawThemeText uses (cls "" = none: no text is drawn) */
enum { F_CRUMB, F_TITLE, F_HINT, F_BOLD, NFONTS };   /* breadcrumb and fields; title; the search placeholder (crumb, slanted);
                                                     the client's bold text (lhnav_text) */
struct tfont {
    char cls[96]; int part, state;
    int explicit_font;                 /* uf instead of the part's FONT (nav.ini fonts, bold, italic; ut_draw_text_font & co.) */
#ifdef HAVE_UXTHEME
    ut_font uf;
#else
    struct { int italic; } uf;
#endif
    int italic;                        /* slanted by lhnav (older libuxtheme: the theme font being upright) */
    int asc, height;                   /* baseline below the cell top and the cell height (tmAscent, tmHeight) */
    int h_right;                       /* older libuxtheme: where an "H" drawn at x 0 ends (measuring by drawing) */
    unsigned gen;                      /* font_gen when measured */
};
/* measured widths, per (font, text) (lhnav_text_width) */
enum { MCACHE = 512 };
struct mc { unsigned hash; int font; char *s; int w; };

struct lhnav {
    struct ut_theme *ut;
    lhnav_metrics m;
    /* art */
    struct art back[4], fwd[4];        /* variants: active, inactive, maximised, maximised inactive (missing ones = active) */
    int fwd_x, split, art_w, art_h;    /* forward image's x relative to back (ANIMATIONBUTTONRECT), source rows used, size */
    int full_h;                        /* the full art's height (aero-patch 54) */
    int menu_x[2][2];                  /* the drop-down boxes' columns [back, fwd][first, end) in source pixels (hot art, rows >= split) */
    px_img *sep;                       /* the breadcrumb separator glyph (SPLITBUTTONHORZARROW, normal) */
    px_img *sep_down;                  /* its open look (the glyph's second image: a down arrow), NULL = drawn */
    int dropbtn_part;                  /* SPLITBUTTONDROPDOWN of crumb_cls: the chevron's hot / pressed box, 0 = drawn */
    px_img *fav[2][4][4];              /* the star: [normal, small][variant][disabled, normal, hot, pressed] (Favorites::AnimationButton) */
    px_img *icon[NICONS][2];           /* the toolbar's icons [kind][normal, hot]: shell32 of icon_dir, else drawn */
    px_img *cicon[NICONS][2];          /* the client's (lhnav_set_icon) */
    char crumb_cls[64];                /* class with the SPLITBUTTON hover/pressed art, "" = drawn */
    int splitbtn_part;
    struct tfont font[NFONTS];
    struct mc mc[MCACHE];
    uint32_t crumb_rgb, glow_rgb;
    int glow;
    /* content */
    char *title, **seg, *field[2], *placeholder;
    int nseg, can_back, can_fwd, focused, maximized, caret[2], editing[2];
    struct tool *tools; int ntools;
    int search_open, open;
    char tip[320];
    /* layout */
    int width;
    double k;                          /* orb scale: orb_h / split */
    struct rect r_title, r_back, r_fwd, r_addr, r_search, r_over, *r_seg, *r_sep, r_bmenu, r_fmenu;
    int title_base;                    /* the title's baseline, client row */
    int hidden;                        /* leading segments collapsed into "«" */
    /* pointer */
    int hover, pressed, down;
};

static char *dupstr(const char *s) { return s ? strdup(s) : NULL; }

static void art_clear(struct art *a)
{
    for (int s = 0; s < NSTATES; s++) { px_img_free(&a->st[s]); px_img_free(&a->full[s]); }
}

static void art_free(lhnav *n)
{
    for (int v = 0; v < 4; v++) { art_clear(&n->back[v]); art_clear(&n->fwd[v]); }
    px_img_free(&n->sep); px_img_free(&n->sep_down);
    for (int a = 0; a < 2; a++) for (int v = 0; v < 4; v++) for (int k = 0; k < 4; k++) px_img_free(&n->fav[a][v][k]);
    for (int i = 0; i < NICONS; i++) { px_img_free(&n->icon[i][0]); px_img_free(&n->icon[i][1]); }
}

static void mc_clear(lhnav *n)
{
    for (int i = 0; i < MCACHE; i++) { free(n->mc[i].s); n->mc[i].s = NULL; }
}
/* the orb-only hot/pressed image (aap's todo: "we have no hovered/clicked icons without the menu arrow ... synthesized from the
   regular buttons with just the inside orb copied over"): the NORMAL image, and inside the orb the HOT/PRESSED one. The orb is
   where NORMAL is (nearly) opaque: the well and halo around it are translucent and the drop-down box below is absent from NORMAL
   (alpha 0), so it stays out. Between alpha 160 and 240 the two are mixed (the orb's antialiased rim). Premultiplied lerp. */
static __attribute__((unused)) uint32_t *synth_orb(const uint32_t *nrm, const uint32_t *act, int w, int h)
{
    uint32_t *o = malloc((size_t)w * h * 4);
    if (!o) return NULL;
    for (int i = 0; i < w * h; i++) {
        unsigned a = nrm[i] >> 24;
        double k = a <= 160 ? 0 : a >= 240 ? 1 : (a - 160) / 80.0;
        uint32_t p = 0;
        for (int c = 0; c < 32; c += 8) {
            double v = (nrm[i] >> c & 255) * (1 - k) + (act[i] >> c & 255) * k;
            p |= (uint32_t)(v + 0.5 > 255 ? 255 : v + 0.5) << c;
        }
        o[i] = p;
    }
    return o;
}

#ifdef HAVE_UXTHEME
static int has_class(struct ut_theme *t, const char *cls)
{
    int n = ut_list_classes(t, NULL, 0);
    if (n <= 0) return 0;
    const char **names = calloc((size_t)n, sizeof *names);
    int hit = 0;
    if (names) {
        ut_list_classes(t, names, n);
        for (int i = 0; i < n && !hit; i++) hit = !strcasecmp(names[i], cls);
        free(names);
    }
    return hit;
}

/* one travel button variant from the style: the four states, orb only */
static int load_travel(lhnav *n, struct art *a, const char *cls)
{
    struct ut_theme *t = n->ut;
    if (!has_class(t, cls)) return 0;
    int part = ut_part(t, "AnimationButton", "ABBACKGROUND"), img = ut_prop(t, "IMAGEFILE");
    static const char *const names[NSTATES] = { "DISABLED", "NORMAL", "HOT", "PRESSED", "SPLITPRESSED" };
    uint32_t *px[NSTATES] = { 0 }; int w[NSTATES] = { 0 }, h[NSTATES] = { 0 }, ok = 1;
    for (int s = 0; s < NSTATES; s++) {
        int st = ut_state(t, "AnimationButton", part, names[s]);
        px[s] = st > 0 || s < ST_SPLITPRESSED ? ut_get_bitmap(t, cls, part, st, img, &w[s], &h[s]) : NULL;
        if (s == ST_SPLITPRESSED) { if (px[s] && (w[s] != w[0] || h[s] != h[0])) { ut_free(px[s]); px[s] = NULL; } continue; }   /* optional */
        if (!px[s] || (s && (w[s] != w[0] || h[s] != h[0]))) ok = 0;
    }
    if (ok) {
        int rows = n->split > 0 && n->split < h[0] ? n->split : h[0];
        uint32_t *hot = synth_orb(px[ST_NORMAL], px[ST_HOT], w[0], h[0]), *prs = synth_orb(px[ST_NORMAL], px[ST_PRESSED], w[0], h[0]);
        a->st[ST_DISABLED] = px_img_from(px[ST_DISABLED], w[0], h[0], rows);
        a->st[ST_NORMAL] = px_img_from(px[ST_NORMAL], w[0], h[0], rows);
        a->st[ST_HOT] = px_img_from(hot ? hot : px[ST_HOT], w[0], h[0], rows);
        a->st[ST_PRESSED] = px_img_from(prs ? prs : px[ST_PRESSED], w[0], h[0], rows);
        free(hot); free(prs);
        for (int s = 0; s < NSTATES; s++) a->full[s] = px[s] ? px_img_from(px[s], w[0], h[0], h[0]) : NULL;
    }
    for (int s = 0; s < NSTATES; s++) if (px[s]) ut_free(px[s]);
    return ok;
}
#endif

/* nav_back.png / nav_fwd.png in the art directory: 4 frames stacked (disabled, normal, hot, pressed), orb only */
static int load_strip(struct art *a, const char *dir, const char *name)
{
    enum { NSTRIP = ST_SPLITPRESSED };   /* the strips have no split-pressed frame */
    char p[700];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    px_img *s = px_img_png(p);
    if (!s) return 0;
    int w = s->w, h = s->h / NSTRIP;
    for (int k = 0; k < NSTRIP; k++) {
        a->st[k] = px_img_from(s->px + (size_t)k * h * w, w, h, h);
        a->full[k] = px_img_from(s->px + (size_t)k * h * w, w, h, h);
    }
    px_img_free(&s);
    return 1;
}

/* no style art at all: a drawn orb (blue glass sphere in a light well, white arrow; hot green, pressed darker green) */
static void drawn_travel(struct art *a, int fwd)
{
    int w = fwd ? 27 : 38, h = 44;
    double cx = fwd ? 11.5 : 19, cy = fwd ? 21 : 20.5, r = fwd ? 10 : 14.5;
    for (int s = 0; s < ST_SPLITPRESSED; s++) {
        px_img *img = px_img_new(w, h);
        if (!img) continue;
        px_cv c = px_cv_img(img);
        px_paint well = px_solid(0.85, 0.88, 0.92, 0.45);
        px_fill_circle(&c, cx, cy, r + 4, &well);
        if (s != ST_DISABLED || !fwd) {
            double c0[3] = { 0.55, 0.75, 0.95 }, c1[3] = { 0.10, 0.35, 0.65 };
            if (s == ST_HOT) { c0[0] = 0.55; c0[1] = 0.95; c0[2] = 0.50; c1[0] = 0.05; c1[1] = 0.55; c1[2] = 0.10; }
            if (s == ST_PRESSED) { c0[0] = 0.35; c0[1] = 0.75; c0[2] = 0.30; c1[0] = 0.02; c1[1] = 0.40; c1[2] = 0.05; }
            px_paint g = px_radial(cx - r * 0.3, cy - r * 0.4, r * 0.1, cx, cy, r);
            px_stop(&g, 0, c0[0], c0[1], c0[2], 1); px_stop(&g, 1, c1[0], c1[1], c1[2], 1);
            px_fill_circle(&c, cx, cy, r, &g);
            px_paint rim = px_solid(0.05, 0.15, 0.35, 0.8);
            px_stroke_circle(&c, cx, cy, r, 1, &rim);
            double d = fwd ? 1 : -1, ar = r * 0.55;
            double pts[] = { cx - d * ar, cy, cx + d * ar * 0.1, cy - ar * 0.8, cx + d * ar * 0.1, cy - ar * 0.3, cx + d * ar, cy - ar * 0.3,
                             cx + d * ar, cy + ar * 0.3, cx + d * ar * 0.1, cy + ar * 0.3, cx + d * ar * 0.1, cy + ar * 0.8 };
            px_paint arrow = px_solid(1, 1, 1, s == ST_DISABLED ? 0.5 : 0.95);
            px_fill_poly(&c, pts, 7, &arrow);
        }
        a->st[s] = img; a->full[s] = px_img_from(img->px, w, h, h);
    }
}

/* premultiplied pixels (stride 4 w) as an image scaled to size x size (box-filtered), keeping the aspect */
static px_img *img_scaled(const uint32_t *px, int w, int h, int size)
{
    px_img *src = px_img_from(px, w, h, h);
    if (!src || size <= 0 || (w == size && h == size)) return src;
    double k = (double)size / (w > h ? w : h);
    int dw = (int)lround(w * k), dh = (int)lround(h * k);
    px_img *d = px_img_new(dw > 0 ? dw : 1, dh > 0 ? dh : 1);
    if (d) { px_cv c = px_cv_img(d); px_blit_scaled(&c, src, 0, 0, k, 1); }
    px_img_free(&src);
    return d;
}

/* the toolbar's icons from icon_dir's shell32.dll: Search = icon group 23 (32 px, as flxp's header), folder = group 4, Views and
   New Folder = cells 32 and 31 of the toolbar strips (XP's tb_sh_def/tb_sh_hot layout: bitmaps 216/217 at 16 px, 214/215 at 24) */
static void load_icons(lhnav *n)
{
    char f[600];
    if (!n->m.icon_dir[0]) return;
    snprintf(f, sizeof f, "%s/shell32.dll", n->m.icon_dir);
    int sz = n->m.tool_icon > 0 ? n->m.tool_icon : 16, w, h;
    uint32_t *px;
    if ((px = lhnav_res_icon(f, 23, 32, &w, &h))) { n->icon[ICN_SEARCH][0] = px_img_from(px, w, h, h); free(px); }
    if ((px = lhnav_res_icon(f, 4, sz, &w, &h))) { n->icon[ICN_FOLDER][0] = img_scaled(px, w, h, sz); free(px); }
    int cell = sz > 16 ? 24 : 16;
    for (int hot = 0; hot < 2; hot++) {
        if (!(px = lhnav_res_bitmap(f, (cell == 16 ? 216 : 214) + hot, &w, &h))) continue;
        if (h == cell && w >= 33 * cell) {
            uint32_t *c = malloc((size_t)cell * cell * 4);
            for (int k = 0; k < 2 && c; k++) {
                int idx = k ? 31 : 32;
                for (int y = 0; y < cell; y++) memcpy(c + (size_t)y * cell, px + (size_t)y * w + idx * cell, (size_t)cell * 4);
                n->icon[k ? ICN_NEWFOLDER : ICN_VIEWS][hot] = img_scaled(c, cell, cell, sz);
            }
            free(c);
        }
        free(px);
    }
}

static void fonts_load(lhnav *n);

static void load_art(lhnav *n)
{
    art_free(n);
    n->fwd_x = 38; n->split = 44; n->crumb_cls[0] = 0; n->splitbtn_part = 0; n->dropbtn_part = 0;
    n->crumb_rgb = 0x00025c; n->glow_rgb = 0xffffff; n->glow = 4;
    int have = 0;
#ifdef HAVE_UXTHEME
    struct ut_theme *t = n->ut;
    if (t) {
        int part = ut_part(t, "AnimationButton", "ABBACKGROUND");
        ut_rect r;
        if (part > 0 && ut_get_rect(t, "TravelBack::AnimationButton", part, 0, ut_prop(t, "CUSTOMSPLITRECT"), &r) && r.top > 0) n->split = r.top;
        if (part > 0 && ut_get_rect(t, "TravelFwd::AnimationButton", part, 0, ut_prop(t, "ANIMATIONBUTTONRECT"), &r) && r.left > 0) n->fwd_x = r.left;
        static const char *const pre[4] = { "", "Inactive", "Max", "MaxInactive" };
        if (load_travel(n, &n->back[0], "TravelBack::AnimationButton") && load_travel(n, &n->fwd[0], "TravelFwd::AnimationButton")) {
            have = 1;
            for (int v = 1; v < 4; v++) {
                char cb[64], cf[64];
                snprintf(cb, sizeof cb, "%sTravelBack::AnimationButton", pre[v]); snprintf(cf, sizeof cf, "%sTravelFwd::AnimationButton", pre[v]);
                if (!load_travel(n, &n->back[v], cb) || !load_travel(n, &n->fwd[v], cf)) { art_clear(&n->back[v]); art_clear(&n->fwd[v]); }
            }
        }
        /* the breadcrumb: the composited toolbar's colours and glow (aero: (0,2,92), white, 4; its font: fonts_load), the split
           button art for hover/pressed, the separator glyph */
        const char *fc = has_class(t, "CompositedBreadcrumbBar::Toolbar") ? "CompositedBreadcrumbBar::Toolbar"
                       : has_class(t, "BreadcrumbBarToolbar::Toolbar") ? "BreadcrumbBarToolbar::Toolbar" : has_class(t, "BB::Toolbar") ? "BB::Toolbar" : NULL;
        if (fc) {
            uint32_t c; int g;
            if (ut_get_color(t, fc, 0, 0, ut_prop(t, "TEXTCOLOR"), &c)) n->crumb_rgb = c;
            if (ut_get_color(t, fc, 0, 0, ut_prop(t, "GLOWCOLOR"), &c)) n->glow_rgb = c;
            if (ut_get_int(t, fc, 0, 0, ut_prop(t, "TEXTGLOWSIZE"), &g) && g > 0) n->glow = g;
        }
        static const char *const tb[3] = { "BreadcrumbBarToolbar::Toolbar", "BB::Toolbar", "Toolbar" };
        for (int i = 0; i < 3 && !n->crumb_cls[0]; i++) {
            if (i < 2 && !has_class(t, tb[i])) continue;
            int p = ut_part(t, "Toolbar", "SPLITBUTTON"), w, h;
            uint32_t *px = p > 0 ? ut_get_bitmap(t, tb[i], p, 0, ut_prop(t, "IMAGEFILE"), &w, &h) : NULL;
            if (px) { snprintf(n->crumb_cls, sizeof n->crumb_cls, "%s", tb[i]); n->splitbtn_part = p; ut_free(px); }
        }
        for (int i = 0; i < 3 && !n->sep; i++) {
            if (i < 2 && !has_class(t, tb[i])) continue;
            int p = ut_part(t, "Toolbar", "SPLITBUTTONHORZARROW"), w, h;
            uint32_t *px = p > 0 ? ut_get_bitmap(t, tb[i], p, 0, ut_prop(t, "GLYPHIMAGEFILE"), &w, &h) : NULL;
            if (px) {   /* states stacked: the first square; the second (aero-patch: a down arrow) is the open look */
                n->sep = px_img_from(px, w, h, h % w == 0 ? w : h);
                if (h % w == 0 && h >= 2 * w) n->sep_down = px_img_from(px + (size_t)w * w, w, h - w, w);
                ut_free(px);
            }
        }
        if (n->crumb_cls[0]) {
            int p = ut_part(t, "Toolbar", "SPLITBUTTONDROPDOWN"), w, h;
            uint32_t *px = p > 0 ? ut_get_bitmap(t, n->crumb_cls, p, 0, ut_prop(t, "IMAGEFILE"), &w, &h) : NULL;
            if (px) { n->dropbtn_part = p; ut_free(px); }
        }
        /* the star orb: Favorites::AnimationButton and its variants, SmallFavorites (as flxp's navbar.cpp finds them) */
        {
            static const char *const pre[4] = { "", "Inactive", "Max", "MaxInactive" }, *const sn[4] = { "DISABLED", "NORMAL", "HOT", "PRESSED" };
            int part = ut_part(t, "AnimationButton", "ABBACKGROUND"), img = ut_prop(t, "IMAGEFILE");
            for (int a = 0; a < 2 && part > 0; a++) for (int v = 0; v < 4; v++) {
                char cls[64]; snprintf(cls, sizeof cls, "%s%sFavorites::AnimationButton", a ? "Small" : "", pre[v]);
                if (!has_class(t, cls)) continue;
                for (int k = 0; k < 4; k++) {
                    int w, h, st = ut_state(t, "AnimationButton", part, sn[k]);
                    uint32_t *px = ut_get_bitmap(t, cls, part, st, img, &w, &h);
                    if (px) { n->fav[a][v][k] = px_img_from(px, w, h, h); ut_free(px); }
                }
            }
        }
    }
#endif
    if (n->m.art_dir[0]) {   /* tuning override (lhwm/tools/build_aerovision.py writes these) */
        struct art b = { { 0 }, { 0 } }, f = { { 0 }, { 0 } };
        if (load_strip(&b, n->m.art_dir, "nav_back.png") && load_strip(&f, n->m.art_dir, "nav_fwd.png")) {
            for (int v = 0; v < 4; v++) { art_clear(&n->back[v]); art_clear(&n->fwd[v]); }
            n->back[0] = b; n->fwd[0] = f;
            n->split = b.st[0]->h;
            if (!have) n->fwd_x = b.st[0]->w;
            have = 1;
        } else { art_clear(&b); art_clear(&f); }
    }
    if (!have) { drawn_travel(&n->back[0], 0); drawn_travel(&n->fwd[0], 1); n->fwd_x = 38; n->split = 44; }
    load_icons(n);
    n->art_w = n->fwd_x + n->fwd[0].st[ST_NORMAL]->w;
    n->art_h = n->back[0].st[ST_NORMAL]->h;
    n->full_h = n->back[0].full[ST_NORMAL]->h;
    if (n->split > n->art_h) n->split = n->art_h;
    /* the drop-down boxes: the columns where the hot art is (nearly) opaque below the split; none (orb-only art): flxp's margins */
    for (int i = 0; i < 2; i++) {
        const px_img *hs = (i ? n->fwd : n->back)[0].full[ST_HOT];
        int w = hs->w, h = hs->h, x0 = w, x1 = 0;
        for (int y = n->split; y < h; y++) {
            const uint32_t *row = hs->px + (size_t)y * w;
            for (int x = 0; x < w; x++) if ((row[x] >> 24) > 96) { if (x < x0) x0 = x; if (x + 1 > x1) x1 = x + 1; }
        }
        if (x1 <= x0) { x0 = i ? 0 : 4; x1 = i ? w - 2 : n->fwd_x - 4; }
        if (!i && x1 > n->fwd_x) x1 = n->fwd_x;   /* back's box ends where forward's starts */
        n->menu_x[i][0] = x0; n->menu_x[i][1] = x1;
    }
}

static void tools_free(lhnav *n)
{
    for (int i = 0; i < n->ntools; i++) { free(n->tools[i].arg); free(n->tools[i].label); free(n->tools[i].tip); }
    free(n->tools); n->tools = NULL; n->ntools = 0;
}

int lhnav_set_tools(lhnav *n, const char *spec)
{
    tools_free(n);
    if (!spec) return 0;
    const char *p = spec;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char item[600]; size_t k = 0;
        if (*p == '"') { p++; while (*p && *p != '"') { if (k + 1 < sizeof item) item[k++] = *p; p++; } if (*p) p++; }
        else while (*p && *p != ' ' && *p != '\t' && *p != ',') { if (k + 1 < sizeof item) item[k++] = *p; p++; }
        item[k] = 0;
        struct tool t = { 0, NULL, NULL, NULL, { 0, 0, 0, 0 } };
        if (!strcmp(item, "search")) { t.kind = LHNAV_TOOL_SEARCH; t.tip = strdup("Search"); }
        else if (!strcmp(item, "search_field")) { t.kind = LHNAV_TOOL_SEARCH_FIELD; t.tip = strdup("Search"); }
        else if (!strcmp(item, "favorites")) { t.kind = LHNAV_TOOL_FAVORITES; t.tip = strdup("Favorites"); }
        else if (!strcmp(item, "views")) { t.kind = LHNAV_TOOL_VIEWS; t.tip = strdup("Views"); }
        else if (!strcmp(item, "newfolder")) { t.kind = LHNAV_TOOL_NEWFOLDER; t.tip = strdup("New Folder"); }
        else if (!strcmp(item, "separator")) t.kind = LHNAV_TOOL_SEPARATOR;
        else if (!strncmp(item, "bookmark:", 9) && item[9]) {
            /* bookmark:PATH[:LABEL] (PATH without ':') */
            char *path = item + 9, *c = strchr(path, ':');
            if (c) *c = 0;
            t.kind = LHNAV_TOOL_BOOKMARK; t.arg = strdup(path); t.label = strdup(c ? c + 1 : "");
            const char *b = strrchr(path, '/');
            t.tip = strdup(c && c[1] ? c + 1 : b && b[1] ? b + 1 : path);
        } else continue;   /* unknown: ignored */
        struct tool *nt = realloc(n->tools, (size_t)(n->ntools + 1) * sizeof *nt);
        if (!nt) { free(t.arg); free(t.label); free(t.tip); break; }
        n->tools = nt; n->tools[n->ntools++] = t;
    }
    return n->ntools;
}

int lhnav_tool_count(lhnav *n) { return n->ntools; }
int lhnav_tool(lhnav *n, int i, const char **arg, const char **label)
{
    if (i < 0 || i >= n->ntools) { if (arg) *arg = NULL; if (label) *label = NULL; return LHNAV_TOOL_NONE; }
    if (arg) *arg = n->tools[i].arg;
    if (label) *label = n->tools[i].label ? n->tools[i].label : "";
    return n->tools[i].kind;
}
const char *lhnav_tip(lhnav *n, int part)
{
    if (part == LHNAV_BACK || part == LHNAV_BACK_MENU) return part == LHNAV_BACK ? "Back" : "Recent pages";
    if (part == LHNAV_FWD || part == LHNAV_FWD_MENU) return part == LHNAV_FWD ? "Forward" : "Recent pages";
    if (part == LHNAV_SEARCH) return "Search";
    if (part == LHNAV_OVERFLOW) return "Folders above";
    if (LHNAV_IS_TOOL(part)) { int i = part - LHNAV_TOOL; return i < n->ntools ? n->tools[i].tip : NULL; }
    if (LHNAV_IS_SEP(part) && part - LHNAV_SEP < n->nseg) {
        snprintf(n->tip, sizeof n->tip, "Folders in %s", n->seg[part - LHNAV_SEP]);
        return n->tip;
    }
    return NULL;
}
void lhnav_set_search_open(lhnav *n, int open) { n->search_open = open != 0; }
int  lhnav_search_open(lhnav *n) { return n->search_open; }
void lhnav_set_open(lhnav *n, int part) { n->open = part; }
int  lhnav_open(lhnav *n) { return n->open; }
void lhnav_set_icon(lhnav *n, const char *name, const uint32_t *px, int w, int h)
{
    for (int i = 0; i < NICONS; i++) for (int hot = 0; hot < 2; hot++) {
        char nm[32]; snprintf(nm, sizeof nm, "%s%s", icon_names[i], hot ? "_hot" : "");
        if (strcmp(nm, name)) continue;
        px_img_free(&n->cicon[i][hot]);
        if (px && w > 0 && h > 0) n->cicon[i][hot] = px_img_from(px, w, h, h);
    }
}

static void apply_metrics(lhnav *n)
{
    if (n->m.glow_size >= 0) n->glow = n->m.glow_size;
    fonts_load(n);   /* crumb_font / title_font */
}

lhnav *lhnav_new(struct ut_theme *style, const lhnav_metrics *m)
{
    lhnav *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->ut = style;
    if (m) n->m = *m; else lhnav_metrics_default(&n->m);
    n->focused = 1; n->can_back = n->can_fwd = 0;
    n->caret[0] = n->caret[1] = -1;
    n->placeholder = strdup("Search");
    load_art(n); apply_metrics(n);
    lhnav_set_tools(n, n->m.tools);
    return n;
}

void lhnav_free(lhnav *n)
{
    if (!n) return;
    art_free(n); tools_free(n); mc_clear(n);
    for (int i = 0; i < NICONS; i++) { px_img_free(&n->cicon[i][0]); px_img_free(&n->cicon[i][1]); }
    for (int i = 0; i < n->nseg; i++) free(n->seg[i]);
    free(n->seg); free(n->r_seg); free(n->r_sep); free(n->title); free(n->field[0]); free(n->field[1]); free(n->placeholder);
    free(n);
}

void lhnav_set_metrics(lhnav *n, const lhnav_metrics *m)
{
    n->m = *m;
    load_art(n);   /* the art directory, fonts and colours may have changed with it */
    apply_metrics(n);
    lhnav_set_tools(n, n->m.tools);
}

void lhnav_set_style(lhnav *n, struct ut_theme *style) { n->ut = style; load_art(n); apply_metrics(n); }
void lhnav_set_title(lhnav *n, const char *s) { free(n->title); n->title = dupstr(s); }
void lhnav_set_history(lhnav *n, int b, int f) { n->can_back = b != 0; n->can_fwd = f != 0; }
void lhnav_set_frame_state(lhnav *n, int focused, int maximized) { n->focused = focused != 0; n->maximized = maximized != 0; }
void lhnav_set_placeholder(lhnav *n, const char *s) { free(n->placeholder); n->placeholder = dupstr(s ? s : ""); }

void lhnav_set_path(lhnav *n, const char *const *seg, int count)
{
    for (int i = 0; i < n->nseg; i++) free(n->seg[i]);
    free(n->seg); free(n->r_seg); free(n->r_sep);
    n->seg = count > 0 ? calloc((size_t)count, sizeof *n->seg) : NULL;
    n->r_seg = count > 0 ? calloc((size_t)count, sizeof *n->r_seg) : NULL;
    n->r_sep = count > 0 ? calloc((size_t)count, sizeof *n->r_sep) : NULL;
    n->nseg = n->seg && n->r_seg && n->r_sep ? count : 0;
    for (int i = 0; i < n->nseg; i++) n->seg[i] = strdup(seg[i] ? seg[i] : "");
    n->hidden = 0;
}

void lhnav_set_field(lhnav *n, int part, const char *s, int caret, int editing)
{
    int i = part == LHNAV_SEARCH ? 1 : 0;
    free(n->field[i]); n->field[i] = dupstr(s);
    n->caret[i] = caret; n->editing[i] = editing != 0;
}

/* ---- text: libuxtheme's GDI text (FreeType under flxp's GDI: what flxp draws) ----
   Every string is rendered once as an 8-bit coverage mask (ut_draw_text_mask: the glyphs DrawThemeText / the font calls draw);
   lhnav composites it in its own colour (nav.ini, the style's breadcrumb colour) and builds the white halo on glass from the same
   mask. Fonts: the breadcrumb's is the style's part font (CompositedBreadcrumbBar::Toolbar), nav.ini's crumb_font / title_font
   are explicit fonts (ut_font). Widths: ut_text_extent (GetThemeTextExtent), cached per (font, text); cell: ut_get_text_metrics.
   UT_HAVE_TEXT_EXTENT (the Makefile defines it when ut.h has these calls, flxp 2026-10-27): without it (an older libuxtheme)
   lhnav falls back to ut_draw_text into a scratch buffer, measuring by drawing, and the closest theme part for nav.ini fonts. */

enum { LH_DT_BOTTOM = 0x8, LH_DT_SINGLELINE = 0x20, LH_DT_NOCLIP = 0x100, LH_DT_NOPREFIX = 0x800, LH_DT_END_ELLIPSIS = 0x8000 };

static void note(const char *msg)   /* a log line, once per distinct message */
{
    static char seen[16][320];
    static int k;
    for (int i = 0; i < 16; i++) if (!strcmp(seen[i], msg)) return;
    snprintf(seen[k], sizeof seen[k], "%s", msg); k = (k + 1) % 16;
    fprintf(stderr, "lhnav: %s\n", msg);
}

#ifdef HAVE_UXTHEME
static __attribute__((unused)) int font_px(const ut_font *f) { return f->height < 0 ? -f->height : (f->height * 4 + 2) / 5; }   /* + = cell height */
static void face_of(const char *in, char *out, size_t n, int *bold)   /* "Segoe UI Bold" -> "Segoe UI", bold */
{
    snprintf(out, n, "%s", in);
    size_t l = strlen(out);
    if (l > 5 && !strcasecmp(out + l - 5, " Bold")) { out[l - 5] = 0; if (bold) *bold = 1; }
}

/* nav.ini's font description, pango's form: "Family[, Family2 ...] [Bold] [Italic] SIZE[px]" (SIZE without px: points at 96 dpi);
   the first family. 0 when there is no size */
static int parse_font(const char *s, char *face, size_t fn, int *px, int *bold, int *italic)
{
    char buf[128], *tok[32]; int k = 0;
    snprintf(buf, sizeof buf, "%s", s);
    for (char *save = NULL, *t = strtok_r(buf, " \t", &save); t && k < 32; t = strtok_r(NULL, " \t", &save)) tok[k++] = t;
    *px = 0; *bold = 0; *italic = 0; face[0] = 0;
    if (k && (tok[k - 1][0] >= '0' && tok[k - 1][0] <= '9')) {
        char *e; double v = strtod(tok[k - 1], &e);
        *px = (int)lround(!strcasecmp(e, "px") ? v : v * 96 / 72);
        k--;
    }
    while (k > 1) {   /* style words at the end */
        const char *w = tok[k - 1];
        if (!strcasecmp(w, "bold") || !strcasecmp(w, "semi-bold") || !strcasecmp(w, "semibold") || !strcasecmp(w, "heavy")) *bold = 1;
        else if (!strcasecmp(w, "italic") || !strcasecmp(w, "oblique")) *italic = 1;
        else if (strcasecmp(w, "normal") && strcasecmp(w, "regular") && strcasecmp(w, "light") && strcasecmp(w, "medium") && strcasecmp(w, "book")) break;
        k--;
    }
    size_t o = 0;
    for (int i = 0; i < k && o + 1 < fn; i++) o += (size_t)snprintf(face + o, fn - o, "%s%s", i ? " " : "", tok[i]);
    char *comma = strchr(face, ',');
    if (comma) *comma = 0;
    return *px > 0 && face[0];
}

/* the font a tfont draws with: its explicit ut_font, else its part's FONT (0: none, the DC's) */
static int font_of(lhnav *n, const struct tfont *f, ut_font *out)
{
    if (f->explicit_font) { *out = f->uf; return 1; }
    return n->ut && f->cls[0] && ut_get_font(n->ut, f->cls, f->part, f->state, ut_prop(n->ut, "FONT"), out) && out->face[0];
}
#endif

#if defined HAVE_UXTHEME && defined UT_HAVE_TEXT_EXTENT
static const ut_font *xfont(const struct tfont *f) { return f->explicit_font ? &f->uf : NULL; }

/* s's coverage into a new W x H mask, the text cell's top-left at (ox, oy), single line; maxw > 0: DT_END_ELLIPSIS within it */
static unsigned char *text_mask(lhnav *n, const struct tfont *f, const char *s, int ox, int oy, int maxw, int W, int H)
{
    unsigned char *m = calloc((size_t)W * H, 1);
    if (!m) return NULL;
    ut_rect r = { ox, oy, maxw > 0 ? ox + maxw : W, H };
    ut_draw_text_mask(n->ut, f->cls, f->part, f->state, xfont(f), m, W, H, W, s,
                      LH_DT_SINGLELINE | LH_DT_NOPREFIX | LH_DT_NOCLIP | (maxw > 0 ? LH_DT_END_ELLIPSIS : 0), &r);
    return m;
}

static void font_measure(lhnav *n, struct tfont *f)
{
    ut_text_metrics tm;
    f->asc = f->height = f->h_right = 0;
    f->gen = font_gen;
    if (!f->cls[0] && !f->explicit_font) return;
    if (!ut_get_text_metrics(n->ut, f->cls, f->part, f->state, xfont(f), &tm) || tm.height <= 0) {
        note("libuxtheme has no metrics for a font (fonts missing? lhnav_add_font_dir / the theme's `fonts`)"); return; }
    f->asc = tm.ascent; f->height = tm.height;
}

static int measure(lhnav *n, const struct tfont *f, const char *s)
{
    ut_rect e;
    int ok = f->explicit_font ? ut_text_extent_font(&f->uf, s, LH_DT_SINGLELINE | LH_DT_NOPREFIX, NULL, &e)
                              : ut_text_extent(n->ut, f->cls, f->part, f->state, s, LH_DT_SINGLELINE | LH_DT_NOPREFIX, NULL, &e);
    return ok ? e.right - e.left : 0;
}

/* nav.ini's `key = desc`: that font (lhnav's own, no theme part needed) */
static struct tfont pick_font(lhnav *n, const char *key, const char *desc, const struct tfont *fb)
{
    (void)n;
    struct tfont r = *fb;
    char face[96], msg[320]; int px, bold, italic;
    if (!parse_font(desc, face, sizeof face, &px, &bold, &italic)) {
        snprintf(msg, sizeof msg, "%s \"%s\": no family or size; using the style's breadcrumb font", key, desc); note(msg);
        return r;
    }
    memset(&r.uf, 0, sizeof r.uf);
    snprintf(r.uf.face, sizeof r.uf.face, "%s", face);
    r.uf.height = -px; r.uf.weight = bold ? 700 : 400; r.uf.italic = italic;
    r.explicit_font = 1; r.italic = 0;
    return r;
}

/* the font f with another weight / italic, as an explicit font */
static struct tfont font_variant(lhnav *n, const struct tfont *f, int bold, int italic)
{
    struct tfont r = *f;
    ut_font u;
    if (!font_of(n, f, &u)) { r.italic = italic; return r; }
    if (bold) { char face[96]; face_of(u.face, face, sizeof face, NULL); snprintf(u.face, sizeof u.face, "%s", face); u.weight = 700; }
    if (italic) u.italic = 1;
    r.uf = u; r.explicit_font = 1; r.italic = 0;
    return r;
}
#elif defined HAVE_UXTHEME
/* ---- the fallback for a libuxtheme without the text calls: ut_draw_text into a scratch buffer (its alpha = the coverage), widths
   by drawing, nav.ini fonts as the closest theme part ---- */

/* s in font f into a new transparent W x H buffer, the text cell's top-left at (ox, oy), single line, not clipped; maxw > 0:
   DT_END_ELLIPSIS within maxw */
static uint32_t *text_raster(lhnav *n, const struct tfont *f, const char *s, unsigned flags, int ox, int oy, int maxw, int W, int H)
{
    uint32_t *px = calloc((size_t)W * H, 4);
    if (!px) return NULL;
    ut_rect r = { ox, oy, maxw > 0 ? ox + maxw : W, H };
    ut_draw_text(n->ut, f->cls, f->part, f->state, px, W, H, W * 4, s,
                 LH_DT_SINGLELINE | LH_DT_NOPREFIX | LH_DT_NOCLIP | flags | (maxw > 0 ? LH_DT_END_ELLIPSIS : 0), &r);
    return px;
}

/* the last column and row with alpha >= thr (-1: none) */
static void ink_end(const uint32_t *px, int W, int H, unsigned thr, int *xr, int *yb)
{
    int x1 = -1, y1 = -1;
    for (int y = 0; px && y < H; y++) for (int x = 0; x < W; x++)
        if ((px[(size_t)y * W + x] >> 24) >= thr) { if (x > x1) x1 = x; y1 = y; }
    if (xr) *xr = x1;
    if (yb) *yb = y1;
}

/* GDI's synthesized oblique (FreeType's slant 0.2126) of a coverage mask, about row `base` */
static void slant(unsigned char *m, int W, int H, int base)
{
    unsigned char *row = malloc((size_t)W);
    if (!row) return;
    for (int y = 0; y < H; y++) {
        double s = (base - 0.5 - y) * 0.2126;
        unsigned char *r = m + (size_t)y * W;
        memcpy(row, r, (size_t)W);
        for (int x = 0; x < W; x++) {
            double u = x - s; int i0 = (int)floor(u); double f = u - i0;
            double a = i0 >= 0 && i0 < W ? row[i0] : 0, b = i0 + 1 >= 0 && i0 + 1 < W ? row[i0 + 1] : 0;
            r[x] = (unsigned char)(a * (1 - f) + b * f + 0.5);
        }
    }
    free(row);
}

static unsigned char *text_mask(lhnav *n, const struct tfont *f, const char *s, int ox, int oy, int maxw, int W, int H)
{
    uint32_t *px = text_raster(n, f, s, 0, ox, oy, maxw, W, H);
    unsigned char *m = px ? malloc((size_t)W * H) : NULL;
    if (m) for (size_t i = 0; i < (size_t)W * H; i++) m[i] = (unsigned char)(px[i] >> 24);
    free(px);
    if (m && f->italic) slant(m, W, H, oy + f->asc);
    return m;
}

/* the font's cell: "H" drawn at the top (its last row is the baseline's) and at the bottom (DT_BOTTOM: the cell ends there) */
static void font_measure(lhnav *n, struct tfont *f)
{
    f->asc = f->height = f->h_right = 0;
    f->gen = font_gen;
    if (!f->cls[0] || !n->ut) return;
    int W = 200, H = 200, xr, yb, yb2;
    uint32_t *a = text_raster(n, f, "H", 0, 0, 0, 0, W, H), *b = text_raster(n, f, "H", LH_DT_BOTTOM, 0, 0, 0, W, H);
    ink_end(a, W, H, 128, NULL, &yb); ink_end(b, W, H, 128, NULL, &yb2);
    ink_end(a, W, H, 1, &xr, NULL);
    free(a); free(b);
    if (yb < 0 || yb2 < 0) { note("libuxtheme drew no text (fonts missing? lhnav_add_font_dir / the theme's `fonts`)"); return; }
    f->asc = yb + 1; f->height = H - (yb2 - yb); f->h_right = xr + 1;
}

/* the advance of s, measured: "sH" and "H" drawn from x 0, the H's right edges differ by the advance */
static int measure(lhnav *n, const struct tfont *f, const char *s)
{
    size_t len = strlen(s);
    char *sh = malloc(len + 2);
    if (!sh) return 0;
    memcpy(sh, s, len); sh[len] = 'H'; sh[len + 1] = 0;
    int chars = 0, w = 0;
    for (const char *p = s; *p; p++) chars += (*p & 0xC0) != 0x80;
    int W = (chars + 2) * (f->height + 4) + 8, H = f->height + 2;
    for (int tries = 0; tries < 4; tries++, W *= 2) {
        uint32_t *px = text_raster(n, f, sh, 0, 0, 0, 0, W, H);
        int xr; ink_end(px, W, H, 1, &xr, NULL); free(px);
        if (xr < W - 2) { w = xr + 1 - f->h_right; break; }
    }
    free(sh);
    return w > 0 ? w : 0;
}

/* every distinct theme font of the style (class, part, state where its FONT is defined) */
struct fcand { char cls[96]; int part, state; ut_font f; };
static int font_cands(struct ut_theme *t, struct fcand *out, int max)
{
    int nc = ut_list_classes(t, NULL, 0), n = 0, fprop = ut_prop(t, "FONT");
    if (nc <= 0 || fprop < 0) return 0;
    const char **names = calloc((size_t)nc, sizeof *names);
    if (!names) return 0;
    ut_list_classes(t, names, nc);
    for (int c = 0; c < nc && n < max; c++) {
        if (!strcasecmp(names[c], "sysmetrics") || !strcasecmp(names[c], "globals")) continue;
        int parts[65], np = ut_list_parts(t, names[c], parts + 1, 64);
        parts[0] = 0; np = (np > 64 ? 64 : np < 0 ? 0 : np) + 1;
        for (int p = 0; p < np && n < max; p++) {
            if (p && !parts[p]) continue;
            int states[65], ns = ut_list_states(t, names[c], parts[p], states + 1, 64);
            states[0] = 0; ns = (ns > 64 ? 64 : ns < 0 ? 0 : ns) + 1;
            for (int s = 0; s < ns && n < max; s++) {
                if (s && !states[s]) continue;
                ut_font f;
                if (!ut_get_font(t, names[c], parts[p], states[s], fprop, &f) || !f.face[0]) continue;
                int dup = 0;
                for (int i = 0; i < n && !dup; i++)
                    dup = !strcasecmp(out[i].f.face, f.face) && out[i].f.height == f.height && out[i].f.weight == f.weight && out[i].f.italic == f.italic;
                if (dup) continue;
                snprintf(out[n].cls, sizeof out[n].cls, "%s", names[c]); out[n].part = parts[p]; out[n].state = states[s]; out[n].f = f;
                n++;
            }
        }
    }
    free(names);
    return n;
}

/* how far theme font f is from the wanted one (0 = the same) */
static int font_dist(const ut_font *f, const char *face, int px, int bold)
{
    char ff[96]; int fb = f->weight >= 600;
    face_of(f->face, ff, sizeof ff, &fb);
    return (strcasecmp(ff, face) ? 1000 : 0) + 10 * abs(font_px(f) - px) + (fb != bold ? 40 : 0);
}

/* nav.ini's `key = desc` as a theme font: fb when that is the font asked for, else the closest of the style's (logged) */
static struct tfont pick_font(lhnav *n, const char *key, const char *desc, const struct tfont *fb)
{
    struct ut_theme *t = n->ut;
    struct tfont r = *fb;
    char face[96], msg[320]; int px, bold, italic;
    if (!parse_font(desc, face, sizeof face, &px, &bold, &italic)) {
        snprintf(msg, sizeof msg, "%s \"%s\": no family or size; using the style's breadcrumb font", key, desc); note(msg);
        return r;
    }
    ut_font f;
    if (font_of(n, fb, &f) && font_dist(&f, face, px, bold) == 0) { r.italic = italic; return r; }
    enum { MAXC = 96 };
    struct fcand *c = calloc(MAXC, sizeof *c);
    int nc = c ? font_cands(t, c, MAXC) : 0, best = -1, bd = 1 << 30;
    for (int i = 0; i < nc; i++) { int d = font_dist(&c[i].f, face, px, bold); if (d < bd) { bd = d; best = i; } }
    if (best >= 0) {
        snprintf(r.cls, sizeof r.cls, "%s", c[best].cls); r.part = c[best].part; r.state = c[best].state;
        r.italic = italic && !c[best].f.italic;
        if (bd) {
            snprintf(msg, sizeof msg, "%s \"%s\" (%s %d px%s): this libuxtheme draws theme fonts only; using %s %d px%s (%s part %d)",
                     key, desc, face, px, bold ? " bold" : "", c[best].f.face, font_px(&c[best].f), c[best].f.weight >= 600 ? " bold" : "",
                     c[best].cls, c[best].part);
            note(msg);
        }
    } else { snprintf(msg, sizeof msg, "%s \"%s\": the style has no theme fonts; using the breadcrumb's", key, desc); note(msg); r.italic = italic; }
    free(c);
    return r;
}

static struct tfont font_variant(lhnav *n, const struct tfont *f, int bold, int italic)
{
    struct tfont r = *f;
    ut_font u;
    if (bold && font_of(n, f, &u) && u.weight < 600) {
        char face[96], desc[128]; face_of(u.face, face, sizeof face, NULL);
        snprintf(desc, sizeof desc, "%s Bold %dpx", face, font_px(&u));
        r = pick_font(n, "bold text", desc, f);
    }
    if (italic) r.italic = 1;
    return r;
}
#endif

static void fonts_load(lhnav *n)
{
    mc_clear(n);
    memset(n->font, 0, sizeof n->font);
#ifdef HAVE_UXTHEME
    struct ut_theme *t = n->ut;
    if (!t) { note("no visual style: no text is drawn (lhnav's text comes from libuxtheme's theme fonts)"); return; }
    /* the breadcrumb's: the composited toolbar's FONT (aero-patch: Segoe UI -12), else Segoe UI 12 px (an explicit font; with an
       older libuxtheme the closest theme font) */
    static const char *const fcs[3] = { "CompositedBreadcrumbBar::Toolbar", "BreadcrumbBarToolbar::Toolbar", "BB::Toolbar" };
    struct tfont base; memset(&base, 0, sizeof base);
    const char *fc = NULL;
    for (int i = 0; i < 3 && !fc; i++) if (has_class(t, fcs[i])) fc = fcs[i];
    ut_font f;
    snprintf(base.cls, sizeof base.cls, "%s", fc ? fc : "Toolbar");
    if (!font_of(n, &base, &f)) base = pick_font(n, "the breadcrumb font", "Segoe UI 12px", &base);
    n->font[F_CRUMB] = n->m.crumb_font[0] ? pick_font(n, "crumb_font", n->m.crumb_font, &base) : base;
    n->font[F_TITLE] = n->m.title_font[0] ? pick_font(n, "title_font", n->m.title_font, &n->font[F_CRUMB]) : n->font[F_CRUMB];
    n->font[F_HINT] = font_variant(n, &n->font[F_CRUMB], 0, 1);
    n->font[F_BOLD] = font_variant(n, &n->font[F_CRUMB], 1, 0);
    for (int i = 0; i < NFONTS; i++) font_measure(n, &n->font[i]);
#else
    note("built without libuxtheme: no text is drawn");
#endif
}

/* font i, re-measured when a font directory was added since */
static struct tfont *font_get(lhnav *n, int i)
{
#ifdef HAVE_UXTHEME
    if (n->font[i].gen != font_gen) { mc_clear(n); for (int k = 0; k < NFONTS; k++) font_measure(n, &n->font[k]); }
#endif
    return &n->font[i];
}

static int font_ok(const struct tfont *f) { return (f->cls[0] || f->explicit_font) && f->height > 0; }

static unsigned str_hash(int font, const char *s)
{
    unsigned h = 2166136261u ^ (unsigned)font;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

/* the advance of s in font fid (GetTextExtentPoint's cx), cached per (font, text) */
static int text_width(lhnav *n, int fid, const char *s)
{
    if (!s || !*s) return 0;
    struct tfont *f = font_get(n, fid);
    if (!font_ok(f)) return 0;
    unsigned h = str_hash(fid, s);
    struct mc *e = &n->mc[h % MCACHE];
    if (e->s && e->hash == h && e->font == fid && !strcmp(e->s, s)) return e->w;
    int w = 0;
#ifdef HAVE_UXTHEME
    w = measure(n, f, s);
#endif
    free(e->s);
    e->s = strdup(s); e->hash = h; e->font = fid; e->w = w;
    return w;
}

/* s in font fid, its cell's top-left at (x, y) (the baseline y + asc), ellipsized to maxw (> 0), in rgb at opacity alpha. glow
   (text on glass, aap's todo: "liberally blur the text to white so it becomes a background, draw the text in black over it"):
   first the glyphs' coverage blurred (two box passes of half the glow size ~ a gaussian of that size), times glow_strength and
   clamped, in the glow colour */
static void draw_text(lhnav *n, px_cv *c, int fid, const char *s, int x, int y, int maxw, uint32_t rgb, double alpha, int glow)
{
#ifdef LHNAV_TEST_HOOKS
    if (getenv("LHNAV_NOTEXT")) return;
#endif
#ifdef HAVE_UXTHEME
    if (!s || !*s) return;
    struct tfont *f = font_get(n, fid);
    if (!font_ok(f)) return;
    int tw = text_width(n, fid, s);
    if (maxw > 0 && tw <= maxw) maxw = 0;
    int R = glow && n->glow > 0 && n->m.glow_strength > 0 ? n->glow : 0, pad = R ? R + 2 : 0;
    int ox = pad + 2 + f->height / 4, oy = pad + 1;
    int W = ox + (maxw > 0 ? maxw : tw) + f->height / 2 + (f->italic || f->uf.italic ? f->height / 3 : 0) + 2 + pad, H = oy + f->height + 2 + pad;
    unsigned char *m = text_mask(n, f, s, ox, oy, maxw, W, H);
    if (!m) return;
    if (R) {
        unsigned char *g = malloc((size_t)W * H);
        if (g) {
            memcpy(g, m, (size_t)W * H);
            int r1 = (R + 1) / 2;
            px_blur_a8(g, W, H, W, r1); px_blur_a8(g, W, H, W, R - r1 > 0 ? R - r1 : 1);
            for (size_t i = 0; i < (size_t)W * H; i++) { double v = g[i] * n->m.glow_strength; g[i] = (unsigned char)(v > 255 ? 255 : v); }
            px_mask(c, g, W, H, x - ox, y - oy, n->m.glow_rgb != 0xFFFFFFFFu ? n->m.glow_rgb : n->glow_rgb, 1);
            free(g);
        }
    }
    px_mask(c, m, W, H, x - ox, y - oy, rgb, alpha);
    free(m);
#else
    (void)n; (void)c; (void)fid; (void)s; (void)x; (void)y; (void)maxw; (void)rgb; (void)alpha; (void)glow;
#endif
}

/* ---- layout ---- */

static int row_top(const lhnav *n) { return n->m.deco_h - n->m.border_t; }   /* the orb row, client rows */
static int big(const lhnav *n) { return n->m.variant == LHNAV_BIG; }
/* the travel art's rows drawn (small: the orb part, big: all) and its height on screen */
static int art_rows(const lhnav *n) { return big(n) ? n->full_h : n->split; }
static int orb_row_h(const lhnav *n) { return n->m.orb_h > 0 ? n->m.orb_h : art_rows(n); }
int lhnav_height(lhnav *n)
{
    int h = n->m.header_h != LHNAV_AUTO ? n->m.header_h : n->m.deco_h + orb_row_h(n) + n->m.gap;
    return h - n->m.border_t;
}
int lhnav_extend_top(lhnav *n) { return lhnav_height(n) + n->m.lines; }
int lhnav_hidden(lhnav *n) { return n->hidden; }
int lhnav_title_baseline(lhnav *n) { return n->title_base; }

/* hand placement (nav.ini, lhnav.h lhnav_place) over an automatic rect; x negative = from the right edge, y in frame rows */
static void place(const lhnav *n, struct rect *r, const lhnav_place *p, int boxes)
{
    if (p->x != LHNAV_AUTO) r->x = p->x < 0 ? n->width + p->x : p->x;
    if (p->y != LHNAV_AUTO) r->y = p->y - n->m.border_t;
    if (boxes && p->w != LHNAV_AUTO && p->w >= 0) r->w = p->w;
    if (boxes && p->h != LHNAV_AUTO && p->h >= 0) r->h = p->h;
    r->x += p->dx; r->y += p->dy;
}

/* the orb items' size (star, magnifier): tool_size, else the orb part's height - 2 */
static int tool_size(const lhnav *n)
{
    if (n->m.tool_size > 0) return n->m.tool_size;
    int oh = orb_row_h(n), part = big(n) ? (int)lround(n->split * (art_rows(n) > 0 ? (double)oh / art_rows(n) : 1)) : oh;
    return part - 2 > 8 ? part - 2 : 8;
}
static int icon_px(const lhnav *n) { return n->m.tool_icon > 0 ? n->m.tool_icon : 16; }
/* an item's width (the field's: 0, computed with search_w) */
static int tool_w(const lhnav *n, int kind, int ts)
{
    switch (kind) {
    case LHNAV_TOOL_SEARCH: return (int)lround(ts * 0.8) + 2;   /* the magnifier: 4/5 of the star, as the concept */
    case LHNAV_TOOL_FAVORITES: return ts;
    case LHNAV_TOOL_BOOKMARK: case LHNAV_TOOL_NEWFOLDER: return icon_px(n) + 8;
    case LHNAV_TOOL_VIEWS: return icon_px(n) + 8 + 11;   /* + XP's drop-down arrow part */
    case LHNAV_TOOL_SEPARATOR: return 7;
    }
    return 0;
}

void lhnav_layout(lhnav *n, int width)
{
    const lhnav_metrics *m = &n->m;
    n->width = width;
    int top = row_top(n), rows = art_rows(n), oh = orb_row_h(n);
    n->k = rows > 0 ? (double)oh / rows : 1;
    /* the orb: back at pad_l, forward ANIMATIONBUTTONRECT.left (scaled) to its right */
    int bw = n->back[0].st[ST_NORMAL]->w, fw = n->fwd[0].st[ST_NORMAL]->w;
    n->r_back = (struct rect){ m->pad_l, top, (int)lround(bw * n->k), oh };
    n->r_fwd = (struct rect){ m->pad_l + (int)lround(n->fwd_x * n->k), top, (int)lround(fw * n->k), oh };
    int orb_r = n->r_fwd.x + n->r_fwd.w;
    /* the boxes, centred against the orb part (small: the whole row) */
    int orb_part = big(n) ? (int)lround(n->split * n->k) : oh;
    int by = top + (orb_part - m->box_h) / 2;
    int ax = orb_r + m->box_gap, sr = width - m->pad_r;
    /* the toolbar (nav.ini `tools`), right-aligned before pad_r; the search field (a search_field item, or an open `search`)
       takes search_w, which narrow windows shrink first, as before the toolbar */
    int field_at = -1;   /* the field goes before item field_at (ntools: at the right end) */
    for (int i = 0; i < n->ntools && field_at < 0; i++) if (n->tools[i].kind == LHNAV_TOOL_SEARCH_FIELD) field_at = i;
    if (field_at < 0 && n->search_open) {
        field_at = n->ntools;
        for (int i = 0; i < n->ntools; i++) if (n->tools[i].kind == LHNAV_TOOL_SEARCH) { field_at = i; break; }
    }
    /* the items' row: the search box's (after its hand placement), whether it is shown or not */
    struct rect ref = { 0, by, 0, m->box_h };
    place(n, &ref, &m->search, 1);
    double cy = ref.y + ref.h / 2.0;
    int ts = tool_size(n), fixed = 0, items = 0;
    if (m->tool_size <= 0) {   /* automatic: no bigger than the header around that row */
        int room_v = 2 * (int)fmin(cy - row_top(n), lhnav_height(n) - cy);
        if (ts > room_v) ts = room_v > 8 ? room_v : 8;
    }
    for (int i = 0; i < n->ntools; i++) {
        struct tool *t = &n->tools[i];
        t->r = (struct rect){ 0, 0, t->kind == LHNAV_TOOL_SEARCH_FIELD ? 0 : tool_w(n, t->kind, ts), 0 };
        if (t->r.w > 0) { fixed += t->r.w; items++; }
    }
    if (field_at >= 0) items++;
    if (items > 1) fixed += (items - 1) * m->tool_gap;
    int sw = 0, room = sr - ax - fixed;
    if (field_at >= 0) {
        sw = m->search_w;
        if (room - sw - m->box_gap < 2 * sw) sw = (room - m->box_gap) / 3;   /* narrow windows: the search box gives way first */
        if (sw < m->search_min) sw = m->search_min;
        if (sw > room - m->box_gap) sw = room - m->box_gap > 0 ? room - m->box_gap : 0;
    }
    /* right to left */
    int x = sr, first = 1;
    n->r_search = (struct rect){ 0, 0, 0, 0 };
    for (int i = n->ntools; i >= 0; i--) {
        if (i < n->ntools && n->tools[i].r.w > 0) {
            if (!first) x -= m->tool_gap;
            x -= n->tools[i].r.w; n->tools[i].r.x = x; first = 0;
        }
        if (i == field_at) {
            if (!first) x -= m->tool_gap;
            x -= sw; n->r_search = (struct rect){ x, by, sw, m->box_h }; first = 0;
        }
    }
    n->r_addr = (struct rect){ ax, by, x - m->box_gap - ax, m->box_h };
    if (first) n->r_addr.w = sr - ax;   /* no toolbar: the breadcrumb to the right edge */
    if (n->r_addr.w < 0) n->r_addr.w = 0;
    for (int i = 0; i < n->ntools; i++) {
        struct tool *t = &n->tools[i];
        if (t->r.w <= 0) continue;
        int h = t->kind == LHNAV_TOOL_SEARCH || t->kind == LHNAV_TOOL_FAVORITES ? ts : ref.h;
        t->r.h = h; t->r.y = (int)floor(cy - h / 2.0) + m->tools_dy; t->r.x += m->tools_dx;
    }
    /* hand placement, each element against its own automatic place */
    int ox = n->r_back.x, oy = n->r_back.y;
    { struct rect o = n->r_back; place(n, &o, &m->orb, 0); ox = o.x - ox; oy = o.y - oy; }
    n->r_back.x += ox; n->r_back.y += oy; n->r_fwd.x += ox; n->r_fwd.y += oy;
    place(n, &n->r_addr, &m->address, 1);
    if (n->r_search.w > 0) place(n, &n->r_search, &m->search, 1);
    /* the big variant's drop-down parts: below the split, the hot art's box columns */
    n->r_bmenu = n->r_fmenu = (struct rect){ 0, 0, 0, 0 };
    if (big(n) && n->full_h > n->split) {
        int my = n->r_back.y + (int)lround(n->split * n->k), mh = n->r_back.y + oh - my;
        n->r_bmenu = (struct rect){ n->r_back.x + (int)lround(n->menu_x[0][0] * n->k), my,
                                    (int)lround((n->menu_x[0][1] - n->menu_x[0][0]) * n->k), mh };
        n->r_fmenu = (struct rect){ n->r_fwd.x + (int)lround(n->menu_x[1][0] * n->k), my,
                                    (int)lround((n->menu_x[1][1] - n->menu_x[1][0]) * n->k), mh };
    }
    /* the title in the deco, left of the caption buttons; baseline on deco_h (or title_baseline) + title_dy */
    int tx = m->title_x + m->title_dx;
    int tr = m->caption_w > 0 ? m->caption_x - 8 : width - m->pad_r;
    n->r_title = (struct rect){ tx, 0, tr - tx > 0 ? tr - tx : 0, top > 0 ? top : 0 };
    n->title_base = (m->title_baseline != LHNAV_AUTO ? m->title_baseline - m->border_t : top) + m->title_dy;
    if (n->title_base > n->r_title.h) n->r_title.h = n->title_base;
    /* the segments: as many of the rightmost as fit, the rest collapsed into "«" */
    by = n->r_addr.y;
    int bh = n->r_addr.h;
    int avail = n->r_addr.w - m->crumb_x - 4, total = 0;
    int *sw_ = n->nseg ? calloc((size_t)n->nseg, sizeof *sw_) : NULL;
    for (int i = 0; i < n->nseg && sw_; i++) { sw_[i] = text_width(n, F_CRUMB, n->seg[i]) + 2 * m->crumb_pad; total += sw_[i] + (i ? m->sep_w : 0); }
    int over_w = text_width(n, F_CRUMB, "«") + 2 * m->crumb_pad;
    n->hidden = 0;
    if (total > avail && n->nseg > 1) {
        int used = over_w + m->sep_w, h = n->nseg - 1;
        used += sw_[h];
        while (h > 0 && used + sw_[h - 1] + m->sep_w <= avail) { h--; used += sw_[h] + m->sep_w; }
        n->hidden = h;
    }
    x = n->r_addr.x + m->crumb_x;
    n->r_over = (struct rect){ 0, 0, 0, 0 };
    for (int i = 0; i < n->nseg; i++) n->r_sep[i] = (struct rect){ 0, 0, 0, 0 };
    if (n->hidden) { n->r_over = (struct rect){ x, by, over_w, bh }; n->r_sep[n->hidden - 1] = (struct rect){ x + over_w, by, m->sep_w, bh };
                     x += over_w + m->sep_w; }
    int end = n->r_addr.x + n->r_addr.w - 2;
    for (int i = 0; i < n->nseg; i++) {
        if (i < n->hidden) { n->r_seg[i] = (struct rect){ 0, 0, 0, 0 }; continue; }
        int w = sw_[i], lim = end - x;
        if (w > lim) w = lim > 0 ? lim : 0;   /* a single long segment is cut (ellipsized when painted) */
        n->r_seg[i] = (struct rect){ x, by, w, bh };
        x += w;
        /* the chevron after it: between segments always; after the last one (sep_last) only where it fits */
        if (i + 1 < n->nseg || (m->sep_last && x + m->sep_w <= end)) n->r_sep[i] = (struct rect){ x, by, m->sep_w, bh };
        x += m->sep_w;
    }
    free(sw_);
}

int lhnav_rect(lhnav *n, int part, int *x, int *y, int *w, int *h)
{
    struct rect r = { 0, 0, 0, 0 };
    switch (part) {
    case LHNAV_TITLE: r = n->r_title; break;
    case LHNAV_BACK: r = n->r_back; break;
    case LHNAV_FWD: r = n->r_fwd; break;
    case LHNAV_ADDRESS: r = n->r_addr; break;
    case LHNAV_SEARCH: r = n->r_search; break;
    case LHNAV_OVERFLOW: r = n->r_over; break;
    case LHNAV_BACK_MENU: r = n->r_bmenu; break;
    case LHNAV_FWD_MENU: r = n->r_fmenu; break;
    case LHNAV_BUTTONS: r = (struct rect){ n->m.caption_x, n->m.caption_y, n->m.caption_w, n->m.caption_h }; break;
    default:
        if (LHNAV_IS_CRUMB(part) && part - LHNAV_CRUMB < n->nseg) r = n->r_seg[part - LHNAV_CRUMB];
        else if (LHNAV_IS_SEP(part) && part - LHNAV_SEP < n->nseg) r = n->r_sep[part - LHNAV_SEP];
        else if (LHNAV_IS_TOOL(part) && part - LHNAV_TOOL < n->ntools) r = n->tools[part - LHNAV_TOOL].r;
    }
    if (x) *x = r.x;
    if (y) *y = r.y;
    if (w) *w = r.w;
    if (h) *h = r.h;
    return r.w > 0 && r.h > 0;
}

static int in(const struct rect *r, int x, int y) { return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h; }

/* alpha of the travel image under (x, y) (client coordinates), 0 outside */
static int orb_alpha(lhnav *n, const px_img *s, const struct rect *r, int x, int y)
{
    if (!s || !in(r, x, y)) return 0;
    return (int)(px_img_at(s, (int)((x - r->x) / n->k), (int)((y - r->y) / n->k)) >> 24);
}

int lhnav_hit(lhnav *n, int x, int y)
{
    const lhnav_metrics *m = &n->m;
    if (x < 0 || y < 0 || x >= n->width || y >= lhnav_height(n)) return LHNAV_NONE;
    struct rect cap = { m->caption_x, m->caption_y, m->caption_w, m->caption_h };
    if (in(&cap, x, y)) return LHNAV_BUTTONS;
    if (y < m->resize_rows) return LHNAV_RESIZE_TOP;
    /* the big variant's drop-down parts (only while their direction is possible, as 4074's browseui) */
    if (n->can_back && in(&n->r_bmenu, x, y)) return LHNAV_BACK_MENU;
    if (n->can_fwd && in(&n->r_fmenu, x, y)) return LHNAV_FWD_MENU;
    /* the orb: back where its orb is solid, then forward, then the rest of back's well */
    const px_img *b = n->back[0].full[ST_NORMAL], *f = n->fwd[0].full[ST_NORMAL];
    if (orb_alpha(n, b, &n->r_back, x, y) >= 160) return LHNAV_BACK;
    if (orb_alpha(n, f, &n->r_fwd, x, y) >= 48) return LHNAV_FWD;
    if (orb_alpha(n, b, &n->r_back, x, y) >= 48) return LHNAV_BACK;
    for (int i = 0; i < n->ntools; i++)
        if (n->tools[i].kind != LHNAV_TOOL_SEPARATOR && n->tools[i].kind != LHNAV_TOOL_SEARCH_FIELD && in(&n->tools[i].r, x, y)) return LHNAV_TOOL + i;
    if (!n->editing[0]) {
        if (in(&n->r_over, x, y)) return LHNAV_OVERFLOW;
        for (int i = n->hidden; i < n->nseg; i++) if (in(&n->r_seg[i], x, y)) return LHNAV_CRUMB + i;
        for (int i = n->hidden > 0 ? n->hidden - 1 : 0; i < n->nseg; i++) if (in(&n->r_sep[i], x, y)) return LHNAV_SEP + i;
    }
    if (in(&n->r_addr, x, y)) return LHNAV_ADDRESS;
    if (in(&n->r_search, x, y)) return LHNAV_SEARCH;
    if (n->title && *n->title && y < n->r_title.h && x >= n->r_title.x && x < n->r_title.x + n->r_title.w) {
        int tw = text_width(n, F_TITLE, n->title);
        if (x < n->r_title.x + tw) return LHNAV_TITLE;
    }
    return LHNAV_CAPTION;
}

static int clickable(int p) { return p == LHNAV_BACK || p == LHNAV_FWD || p == LHNAV_BACK_MENU || p == LHNAV_FWD_MENU || p == LHNAV_OVERFLOW || p >= LHNAV_CRUMB; }
/* the part shows pressed: pressed with the pointer on it, or its list open */
static int down_on(const lhnav *n, int part) { return n->open == part || (n->pressed == part && n->hover == part); }

int lhnav_pointer(lhnav *n, int x, int y, int buttons)
{
    int part = x < 0 ? LHNAV_NONE : lhnav_hit(n, x, y), down = buttons & 1, oh = n->hover, op = n->pressed;
    if (part == LHNAV_BACK && !n->can_back) part = LHNAV_CAPTION;   /* a disabled button neither lights nor presses */
    if (part == LHNAV_FWD && !n->can_fwd) part = LHNAV_CAPTION;
    n->hover = part;
    if (down && !n->down) n->pressed = clickable(part) ? part : LHNAV_NONE;
    if (!down) n->pressed = LHNAV_NONE;
    n->down = down;
    return n->hover != oh || n->pressed != op;
}
int lhnav_hover(lhnav *n) { return n->hover; }
int lhnav_pressed(lhnav *n) { return n->pressed; }
/* ---- fields ---- */

/* the advance of the first `index` bytes of text (the caret's x) */
static int prefix_w(lhnav *n, const char *text, int index)
{
    if (!text || index <= 0) return 0;
    size_t len = strlen(text);
    if ((size_t)index > len) index = (int)len;
    char *p = strndup(text, (size_t)index);
    int w = p ? text_width(n, F_CRUMB, p) : 0;
    free(p);
    return w;
}

/* where field i's text starts (scrolled so the caret stays visible) */
static int field_origin(lhnav *n, int part, const char *text, int caret)
{
    const struct rect *r = part == LHNAV_SEARCH ? &n->r_search : &n->r_addr;
    int x0 = r->x + 5, right = r->x + r->w - (part == LHNAV_SEARCH ? 22 : 6);
    if (caret < 0 || !text) return x0;
    int cx = x0 + prefix_w(n, text, caret);
    return cx > right ? x0 - (cx - right) : x0;
}

int lhnav_field_x(lhnav *n, int part, const char *text, int index)
{
    int i = part == LHNAV_SEARCH ? 1 : 0;
    return field_origin(n, part, n->field[i], n->caret[i]) + prefix_w(n, text, index);
}

int lhnav_field_index(lhnav *n, int part, const char *text, int x)
{
    int i = part == LHNAV_SEARCH ? 1 : 0, best = 0;
    if (!text) return 0;
    int x0 = field_origin(n, part, n->field[i], n->caret[i]), bd = abs(x - x0);
    for (const char *p = text; *p; ) {   /* the character boundary nearest to x */
        p++;
        while ((*p & 0xC0) == 0x80) p++;
        int d = abs(x - (x0 + prefix_w(n, text, (int)(p - text))));
        if (d < bd) { bd = d; best = (int)(p - text); }
    }
    return best;
}

/* ---- painting ---- */

/* a field: translucent white on glass at rest, opaque white while editing; a soft dark border with a light inner line */
static void field_box(px_cv *c, const struct rect *r, double alpha, int editing)
{
    px_paint fill = px_solid(1, 1, 1, editing ? 1 : alpha), line = px_solid(0.12, 0.16, 0.24, editing ? 0.75 : 0.45);
    px_fill_rrect(c, r->x + 0.5, r->y + 0.5, r->w - 1, r->h - 1, 2.5, &fill);
    px_stroke_rrect(c, r->x + 0.5, r->y + 0.5, r->w - 1, r->h - 1, 2.5, 1, &line);
    if (!editing) { px_paint hl = px_solid(1, 1, 1, 0.5); px_fill_rect(c, r->x + 3, r->y + 1, r->w - 6, 1, &hl); }
}

/* the magnifier of the search box (concept art: a light blue lens in a dark rim, a grey handle to the lower right), at scale s */
static void magnifier(px_cv *c, double cx, double cy, double s)
{
    double handle[] = { cx + 2.6 * s, cy + 2.6 * s, cx + 6 * s, cy + 6 * s };
    px_paint hp = px_solid(0.25, 0.27, 0.32, 0.95), rim = px_solid(0.18, 0.24, 0.36, 0.95);
    px_stroke_poly(c, handle, 2, 0, 2.4 * s, 1, &hp);
    px_paint g = px_radial(cx - 1.5 * s, cy - 1.5 * s, 0.5 * s, cx, cy, 4.5 * s);
    px_stop(&g, 0, 0.95, 0.98, 1, 0.95); px_stop(&g, 1, 0.55, 0.72, 0.92, 0.95);
    px_fill_circle(c, cx - 0.5 * s, cy - 0.5 * s, 4.2 * s, &g);
    px_stroke_circle(c, cx - 0.5 * s, cy - 0.5 * s, 4.2 * s, 1.2 * s, &rim);
}

/* a part of the breadcrumb toolbar's art (SPLITBUTTON, SPLITBUTTONDROPDOWN) hot / pressed over r; 0 when the style has none */
static int toolbar_bg(lhnav *n, px_cv *c, const struct rect *r, int part, int pressed)
{
    (void)n; (void)c; (void)r; (void)part; (void)pressed;
#ifdef HAVE_UXTHEME
    if (n->ut && n->crumb_cls[0] && part > 0 && r->w > 0 && r->h > 0) {
        px_img *s = px_img_new(r->w, r->h);
        if (!s) return 0;
        ut_rect rr = { 0, 0, r->w, r->h };
        int st = ut_state(n->ut, "Toolbar", part, pressed ? "PRESSED" : "HOT");
        int ok = ut_draw_background(n->ut, n->crumb_cls, part, st > 0 ? st : pressed ? 3 : 2, s->px, r->w, r->h, r->w * 4, &rr, NULL);
        if (ok) px_blit(c, s, r->x, r->y, 1);
        px_img_free(&s);
        return ok;
    }
#endif
    return 0;
}

/* a breadcrumb segment's hot / pressed background: the style's SPLITBUTTON (nine-gridded by libuxtheme), else a drawn one */
static void crumb_bg(lhnav *n, px_cv *c, const struct rect *r, int pressed)
{
    if (toolbar_bg(n, c, r, n->splitbtn_part, pressed)) return;
    px_paint fill = px_solid(1, 1, 1, pressed ? 0.25 : 0.45), line = px_solid(0.1, 0.15, 0.25, pressed ? 0.6 : 0.4);
    px_fill_rrect(c, r->x + 0.5, r->y + 0.5, r->w - 1, r->h - 1, 2, &fill);
    px_stroke_rrect(c, r->x + 0.5, r->y + 0.5, r->w - 1, r->h - 1, 2, 1, &line);
}

static void separator(lhnav *n, px_cv *c, int x, int y, int h, int composited, int down)
{
    double cx = x + n->m.sep_w / 2.0, cy = y + h / 2.0;
    const px_img *g = down ? n->sep_down : n->sep;
    if (g) { px_blit(c, g, (int)floor(cx - g->w / 2.0), (int)floor(cy - g->h / 2.0), 1); return; }
    double dn[] = { cx - 4, cy - 2, cx, cy + 2.5, cx + 4, cy - 2 }, rt[] = { cx - 2, cy - 4, cx + 2.5, cy, cx - 2, cy + 4 };
    const double *t = down ? dn : rt;
    if (composited) { px_paint w = px_solid(1, 1, 1, 0.8); px_stroke_poly(c, t, 3, 1, 2, 0, &w); }
    px_paint f = px_rgb(n->m.crumb_rgb != 0xFFFFFFFFu ? n->m.crumb_rgb : n->crumb_rgb, 1);
    px_fill_poly(c, t, 3, &f);
}

/* the chevron after segment i: a drop-down button (hot / pressed / open: SPLITBUTTONDROPDOWN, the glyph turned down while
   pressed or open); after the last segment only with sep_last 1, or 2 while the breadcrumb is hovered */
static void paint_sep(lhnav *n, px_cv *c, int i, int composited, int hov_addr)
{
    if (i < 0 || i >= n->nseg) return;
    struct rect *r = &n->r_sep[i];
    if (r->w <= 0) return;
    if (i + 1 == n->nseg && !(n->m.sep_last == 1 || (n->m.sep_last == 2 && hov_addr))) return;
    int part = LHNAV_SEP + i, dn = down_on(n, part);
    if (dn || n->hover == part) {
        if (!toolbar_bg(n, c, r, n->dropbtn_part, dn)) crumb_bg(n, c, r, dn);
    }
    separator(n, c, r->x, r->y, r->h, composited, dn);
}

static void paint_orb(lhnav *n, px_cv *c, struct art *a, struct art *a0, const struct rect *r, int part, int menu, int enabled)
{
    /* the button and its drop-down part are one image: hot when either is hovered, PRESSED / SPLITPRESSED by which is pressed */
    int st = !enabled ? ST_DISABLED : n->pressed == part && n->hover == part ? ST_PRESSED
           : down_on(n, menu) ? ST_SPLITPRESSED : n->hover == part || n->hover == menu ? ST_HOT : ST_NORMAL;
    px_img *const *v = big(n) ? a->full : a->st, *const *v0 = big(n) ? a0->full : a0->st;
    if (st == ST_SPLITPRESSED && !v[st] && !v0[st]) st = ST_PRESSED;
    const px_img *s = v[st] ? v[st] : v0[st];
    if (s) px_blit_scaled(c, s, r->x, r->y, n->k, 1);   /* box-filtered when shrunk */
}

/* image s drawn into the box (x, y, w, h) scaled to fit (aspect kept, centred), at alpha a */
static void draw_fit(px_cv *c, const px_img *s, double x, double y, double w, double h, double a)
{
    double k = fmin(w / s->w, h / s->h);
    if (fabs(k - 1) < 1e-6) k = 1;
    double dx = floor(x + (w - s->w * k) / 2 + 0.5), dy = floor(y + (h - s->h * k) / 2 + 0.5);
    px_blit_scaled(c, s, dx, dy, k, a);
}

/* drawn stand-ins for missing icons, in an s x s box at (x, y) (designed on 16 x 16) */
static void drawn_icon(px_cv *c, int kind, double x, double y, double s)
{
    double k = s / 16.0;
#define X(v) (x + (v) * k)
#define Y(v) (y + (v) * k)
    if (kind == ICN_FOLDER || kind == ICN_NEWFOLDER) {   /* a manila folder (+ a spark for New Folder) */
        px_paint body = px_solid(0.93, 0.78, 0.35, 1), edge = px_solid(0.55, 0.40, 0.10, 0.9);
        double tab[] = { X(1), Y(3), X(7), Y(3), X(7), Y(5), X(1), Y(5) }, box[] = { X(1), Y(4.5), X(15), Y(4.5), X(15), Y(14), X(1), Y(14) };
        px_fill_poly(c, tab, 4, &body); px_fill_poly(c, box, 4, &body);
        px_stroke_poly(c, tab, 4, 1, k, 0, &edge); px_stroke_poly(c, box, 4, 1, k, 0, &edge);
        if (kind == ICN_NEWFOLDER) { px_paint sp = px_solid(1, 0.55, 0.1, 1); px_fill_circle(c, X(12.5), Y(3.5), 2.5 * k, &sp); }
    } else if (kind == ICN_VIEWS) {   /* a window with a grid of tiles */
        px_paint wh = px_solid(1, 1, 1, 1), bl = px_solid(0.15, 0.35, 0.75, 1);
        double win[] = { X(1.5), Y(2.5), X(14.5), Y(2.5), X(14.5), Y(13.5), X(1.5), Y(13.5) };
        px_fill_poly(c, win, 4, &wh); px_stroke_poly(c, win, 4, 1, k, 0, &bl);
        double bar[] = { X(1.5), Y(2.5), X(14.5), Y(2.5), X(14.5), Y(4.5), X(1.5), Y(4.5) };
        px_fill_poly(c, bar, 4, &bl);
        for (int j = 0; j < 2; j++) for (int i = 0; i < 3; i++) {
            double t[] = { X(3.5 + 3.7 * i), Y(6.5 + 3.5 * j), X(5.9 + 3.7 * i), Y(6.5 + 3.5 * j), X(5.9 + 3.7 * i), Y(8.7 + 3.5 * j), X(3.5 + 3.7 * i), Y(8.7 + 3.5 * j) };
            px_fill_poly(c, t, 4, &bl);
        }
    }
#undef X
#undef Y
}

/* the drawn star orb (no Favorites art): a blue glass sphere in a light well, a white star */
static void drawn_star(px_cv *c, double x, double y, double s, int state)
{
    double cx = x + s / 2, cy = y + s / 2, r = s * 0.36;
    px_paint well = px_solid(0.85, 0.88, 0.92, 0.45);
    px_fill_circle(c, cx, cy, s * 0.48, &well);
    px_paint g = px_radial(cx - r * 0.3, cy - r * 0.4, r * 0.1, cx, cy, r);
    double c1 = state == 2 ? 0.55 : state == 3 ? 0.40 : 0.35;
    px_stop(&g, 0, 0.65, 0.85, 0.95, 1); px_stop(&g, 1, 0.05, c1, 0.65, 1);
    px_fill_circle(c, cx, cy, r, &g);
    double pts[20];
    for (int k = 0; k < 10; k++) {
        double a = -M_PI / 2 + k * M_PI / 5, rr = k & 1 ? r * 0.27 : r * 0.62;
        pts[2 * k] = cx + rr * cos(a); pts[2 * k + 1] = cy + rr * sin(a);
    }
    px_paint st = px_solid(1, 1, 1, state == 0 ? 0.5 : 0.95);
    px_fill_poly(c, pts, 10, &st);
}

/* a little down arrow (XP's drop-down part) centred on (cx, cy) */
static void down_arrow(px_cv *c, double cx, double cy, double a)
{
    double t[] = { cx - 2.5, cy - 1.5, cx + 2.5, cy - 1.5, cx, cy + 1.5 };
    px_paint p = px_solid(0, 0, 0, a);
    px_fill_poly(c, t, 3, &p);
}

static const px_img *icon_of(lhnav *n, int k, int hot)
{
    if (hot && n->cicon[k][1]) return n->cicon[k][1];
    if (n->cicon[k][0]) return n->cicon[k][0];
    if (hot && n->icon[k][1]) return n->icon[k][1];
    return n->icon[k][0];
}

/* toolbar item i */
static void paint_tool(lhnav *n, px_cv *c, int i, int composited)
{
    struct tool *t = &n->tools[i];
    struct rect *r = &t->r;
    if (r->w <= 0 || r->h <= 0 || t->kind == LHNAV_TOOL_SEARCH_FIELD) return;
    int part = LHNAV_TOOL + i, dn = down_on(n, part), hot = n->hover == part || dn;
    int v = (n->maximized ? 2 : 0) + (n->focused ? 0 : 1);
    switch (t->kind) {
    case LHNAV_TOOL_SEPARATOR: {   /* glass: a white line fading out at both ends (flxp's divider); opaque: dark with a light line */
        int x = r->x + r->w / 2;
        double g = composited ? 1 : 0.25, a = composited ? 0.85 : 0.5;
        px_paint p = px_linear(0, r->y, 0, r->y + r->h);
        px_stop(&p, 0, g, g, g, 0); px_stop(&p, 0.5, g, g, g, a); px_stop(&p, 1, g, g, g, 0);
        px_fill_rect(c, x, r->y, 1, r->h, &p);
        if (!composited) { px_paint l = px_solid(1, 1, 1, 0.5); px_fill_rect(c, x + 1, r->y + 2, 1, r->h - 4, &l); }
        break; }
    case LHNAV_TOOL_FAVORITES: {   /* the style's star orb at tool_size: the small art up to 35 px, else the normal one */
        int st = dn ? 3 : hot ? 2 : 1, a = r->h <= 35 && n->fav[1][0][1] ? 1 : 0;
        const px_img *s = n->fav[a][v][st] ? n->fav[a][v][st] : n->fav[a][0][st] ? n->fav[a][0][st] : n->fav[!a][0][st];
        if (s) draw_fit(c, s, r->x, r->y, r->w, r->h, 1);
        else drawn_star(c, r->x, r->y, r->h, st);
        break; }
    case LHNAV_TOOL_SEARCH: {   /* the magnifier: shell32's Search icon (or drawn); hot: full strength and a soft light behind */
        double sz = r->w - 2, x = r->x + 1 + (dn ? 1 : 0), y = r->y + (r->h - sz) / 2 + (dn ? 1 : 0);
        if (hot && composited) {
            double cx = r->x + r->w / 2.0, cy = r->y + r->h / 2.0, rad = r->h / 2.0;
            px_paint g = px_radial(cx, cy, 0, cx, cy, rad);
            px_stop(&g, 0, 1, 1, 1, dn ? 0.45 : 0.7); px_stop(&g, 1, 1, 1, 1, 0);
            px_fill_rect(c, (int)floor(cx - rad) - 1, (int)floor(cy - rad) - 1, (int)ceil(2 * rad) + 3, (int)ceil(2 * rad) + 3, &g);
        }
        const px_img *s = icon_of(n, ICN_SEARCH, hot);
        if (s) draw_fit(c, s, x, y, sz, sz, hot ? 1 : 0.9);
        else magnifier(c, x + sz * 0.42, y + sz * 0.42, sz / 14.0);
        break; }
    default: {   /* icon buttons: the breadcrumb toolbar's hot / pressed box, the icon (hot strip when hot) */
        int k = t->kind == LHNAV_TOOL_BOOKMARK ? ICN_FOLDER : t->kind == LHNAV_TOOL_VIEWS ? ICN_VIEWS : ICN_NEWFOLDER;
        if (hot) crumb_bg(n, c, r, dn);
        int ip = icon_px(n), iw = ip + 8;
        double ix = r->x + (iw - ip) / 2 + (dn ? 1 : 0), iy = r->y + (r->h - ip) / 2 + (dn ? 1 : 0);
        const px_img *s = icon_of(n, k, hot);
        if (s) draw_fit(c, s, ix, iy, ip, ip, 1); else drawn_icon(c, k, ix, iy, ip);
        if (t->kind == LHNAV_TOOL_VIEWS) {
            if (hot) { px_paint d = px_solid(0, 0, 0, 0.25); px_fill_rect(c, r->x + iw, r->y + 3, 1, r->h - 6, &d); }
            down_arrow(c, r->x + iw + 5.5 + (dn ? 1 : 0), r->y + r->h / 2.0 + (dn ? 1 : 0), 0.85);
        }
        break; }
    }
}

/* a caret: a 1 px black line in column x, the box's rows 4 .. h - 4 */
static void caret(px_cv *c, int x, const struct rect *r)
{
    px_paint b = px_solid(0, 0, 0, 1);
    px_fill_rect(c, x, r->y + 4, 1, r->h - 8, &b);
}

/* the top of a text cell centred in r (with the old pango layout's rounding: floor) */
static int text_top(lhnav *n, int fid, const struct rect *r, int dy)
{
    return (int)floor(r->y + (r->h - font_get(n, fid)->height) / 2.0) + dy;
}

void lhnav_paint(lhnav *n, uint32_t *px, int w, int h, int stride, int composited)
{
    const lhnav_metrics *m = &n->m;
    px_cv cv = px_cv_make(px, w, h, stride), *c = &cv;
    px_clip(c, 0, 0, w, lhnav_extend_top(n));
    uint32_t tcol = composited ? m->title_rgb : m->plain_rgb,
             ccol = composited ? (m->crumb_rgb != 0xFFFFFFFFu ? m->crumb_rgb : n->crumb_rgb) : m->plain_rgb;
    /* 1. title: baseline on frame row deco_h (+ title_dy), ellipsized left of the caption buttons */
    if (n->title && *n->title && n->r_title.w > 0) {
        struct tfont *f = font_get(n, F_TITLE);
        int y = n->title_base - f->asc;
        /* a tall deco (a theme without a vision deco: deco_h = the whole caption) centres the title in it instead */
        if (m->deco_h > 20 && m->title_baseline == LHNAV_AUTO) y = (int)floor((row_top(n) - f->height) / 2.0 + m->title_dy);
        draw_text(n, c, F_TITLE, n->title, n->r_title.x, y, n->r_title.w, tcol == 0xFFFFFFFFu ? 0 : tcol, 1, composited);
    }
    /* 2. the orb */
    int v = (n->maximized ? 2 : 0) + (n->focused ? 0 : 1);
    paint_orb(n, c, &n->fwd[v], &n->fwd[0], &n->r_fwd, LHNAV_FWD, LHNAV_FWD_MENU, n->can_fwd);
    paint_orb(n, c, &n->back[v], &n->back[0], &n->r_back, LHNAV_BACK, LHNAV_BACK_MENU, n->can_back);
    /* 3. the breadcrumb box: its field while editing (always / on hover by address_field), the segments otherwise */
    int hov_addr = n->hover == LHNAV_ADDRESS || n->hover == LHNAV_OVERFLOW || LHNAV_IS_CRUMB(n->hover) || LHNAV_IS_SEP(n->hover);
    if (n->r_addr.w > 0) {
        if (n->editing[0]) field_box(c, &n->r_addr, 1, 1);
        else if (m->address_field == 2 || (m->address_field == 1 && hov_addr)) field_box(c, &n->r_addr, m->field_alpha, 0);
    }
    if (n->editing[0]) {
        px_cv sv = *c;
        px_clip(c, n->r_addr.x + 2, n->r_addr.y, n->r_addr.w - 4, n->r_addr.h);
        const char *t = n->field[0] ? n->field[0] : "";
        int x0 = field_origin(n, LHNAV_ADDRESS, t, n->caret[0]), y = text_top(n, F_CRUMB, &n->r_addr, m->crumb_dy);
        draw_text(n, c, F_CRUMB, t, x0, y, 0, 0x000000, 1, 0);
        if (n->caret[0] >= 0) caret(c, x0 + prefix_w(n, t, n->caret[0]), &n->r_addr);
        *c = sv;
    } else {
        px_cv sv = *c;
        px_clip(c, n->r_addr.x, n->r_addr.y - 4, n->r_addr.w, n->r_addr.h + 8);
        if (n->hidden) {
            int sp = LHNAV_SEP + n->hidden - 1;
            if (n->hover == LHNAV_OVERFLOW || down_on(n, LHNAV_OVERFLOW) || n->hover == sp || down_on(n, sp))
                crumb_bg(n, c, &n->r_over, down_on(n, LHNAV_OVERFLOW) || down_on(n, sp));
            draw_text(n, c, F_CRUMB, "«", n->r_over.x + m->crumb_pad + m->crumb_text_dx, text_top(n, F_CRUMB, &n->r_over, m->crumb_dy + m->crumb_text_dy),
                      0, ccol, 1, composited);
            paint_sep(n, c, n->hidden - 1, composited, hov_addr);
        }
        for (int i = n->hidden; i < n->nseg; i++) {
            struct rect *r = &n->r_seg[i];
            if (r->w <= 0) continue;
            int part = LHNAV_CRUMB + i;
            int sp = LHNAV_SEP + i;
            if (n->hover == part) crumb_bg(n, c, r, n->pressed == part);
            else if (n->hover == sp || down_on(n, sp)) crumb_bg(n, c, r, down_on(n, sp));   /* the split pair */
            int dy = n->pressed == part && n->hover == part ? 1 : 0;
            draw_text(n, c, F_CRUMB, n->seg[i], r->x + m->crumb_pad + m->crumb_text_dx + dy, text_top(n, F_CRUMB, r, m->crumb_dy + m->crumb_text_dy + dy),
                      r->w - 2 * m->crumb_pad > 1 ? r->w - 2 * m->crumb_pad : 1, ccol, 1, composited);
            paint_sep(n, c, i, composited, hov_addr);
        }
        *c = sv;
    }
    /* 4. the search box: field, placeholder or text, the magnifier at its right end */
    if (n->r_search.w > 0) {
        struct rect *r = &n->r_search;
        if (n->editing[1] || m->search_field == 1 || n->hover == LHNAV_SEARCH) field_box(c, r, m->field_alpha, n->editing[1]);
        px_cv sv = *c;
        px_clip(c, r->x + 2, r->y, r->w - 22, r->h);
        const char *t = n->field[1] && *n->field[1] ? n->field[1] : NULL;
        if (t || !n->editing[1]) {
            int x0 = t ? field_origin(n, LHNAV_SEARCH, t, n->editing[1] ? n->caret[1] : -1) : r->x + 5;
            int y = text_top(n, t ? F_CRUMB : F_HINT, r, m->crumb_dy);
            if (t && (n->editing[1] || !composited)) draw_text(n, c, F_CRUMB, t, x0, y, 0, n->editing[1] ? 0 : ccol, 1, 0);
            else if (t) draw_text(n, c, F_CRUMB, t, x0, y, 0, ccol, 1, composited);
            else draw_text(n, c, F_HINT, n->placeholder, x0, y, 0, ccol, 0.55, 0);   /* placeholder */
            if (n->editing[1] && n->caret[1] >= 0) caret(c, x0 + prefix_w(n, t, n->caret[1]), r);
        } else if (n->caret[1] >= 0) caret(c, r->x + 5, r);
        *c = sv;
        magnifier(c, r->x + r->w - 11, r->y + r->h / 2.0 - 1, 1);
    }
    /* 5. the toolbar */
    for (int i = 0; i < n->ntools; i++) paint_tool(n, c, i, composited);
}

/* ---- text for the client's own popups ---- */

void lhnav_text(lhnav *n, uint32_t *px, int w, int h, int stride, int x, int y, const char *utf8, int bold, uint32_t rgb)
{
    px_cv c = px_cv_make(px, w, h, stride);
    draw_text(n, &c, bold ? F_BOLD : F_CRUMB, utf8, x, y, 0, rgb, 1, 0);
}

int lhnav_text_size(lhnav *n, const char *utf8, int bold, int *height)
{
    int f = bold ? F_BOLD : F_CRUMB;
    if (height) *height = font_get(n, f)->height;
    return text_width(n, f, utf8);
}
