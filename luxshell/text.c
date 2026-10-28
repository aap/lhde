/* lhshell text: libuxtheme's GDI-faithful text (flxp's renderer, the same glyphs lhnav and flxp draw) for every N_TEXT node.
   SHELL-DESIGN 4.2 "Text"; the approach is lhnav's (NAVBAR.md "Text and pixels without pango and cairo"):
   - a node's font is its pango-style description ("Segoe UI 9", "Segoe UI,Tahoma Bold 14px", "ITC Franklin Gothic Std Medium
     15px"): the first family that exists (the theme's font directories first, as libuxtheme looks them up, then the system), the
     weight and slant words, the size in points at 96 dpi or in px; it becomes a ut_font (lfHeight = -px rounded, as GDI does);
   - glyphs and advances are GDI's: each line is drawn once into an 8-bit coverage mask (ut_draw_text_mask, DT_SINGLELINE |
     DT_NOPREFIX, DT_END_ELLIPSIS for `ellipsis`), the mask is painted with cairo in the node's colour on whole device pixels;
   - the line box is the font's typographic one (hhea ascent / descent at the size, rounded up: what pango laid out with, and
     Avalon's line box), so every node keeps its size and its baseline; the GDI cell is placed on that baseline;
   - wrapping (`wrap`) is greedy at spaces with GDI widths, a word wider than the line broken between characters (pango's
     WORD_CHAR); `markup` is the menus' mnemonic markup (<u>x</u>, &amp; ...): the tags are removed and the underline is drawn
     under the prefix's extent at GDI's underline rows (those of the font with lfUnderline);
   - `glow` = the mask blurred (two box passes, glow_size) times glow_strength in the glow colour under the text (lhnav's text
     on glass), `shadow` = the mask offset in the shadow colour;
   - a string with characters the chosen face lacks is drawn in a face that has them (fontconfig's choice; GDI has no per-glyph
     fallback). */
#include "scene.h"
#include <wuxtheme/ut.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdint.h>

enum { DT_SINGLELINE_ = 0x20, DT_NOCLIP_ = 0x100, DT_NOPREFIX_ = 0x800, DT_END_ELLIPSIS_ = 0x8000 };
#define LINE_FLAGS (DT_SINGLELINE_ | DT_NOPREFIX_ | DT_NOCLIP_)

static unsigned gen = 1;            /* bumped when the font directories change: every cache below is stale */
static FcConfig *app_config;        /* the theme font directories only (libuxtheme's lookup order: these first, then the system) */
static FT_Library ftlib;

/* ---- fonts ---- */
struct tfont {
    char key[200]; unsigned gen, id;
    char face[96]; char file[512]; int index;
    double px; int weight, italic;
    ut_font uf;
    int asc, desc, lineh;               /* the line box (typographic ascent / descent, rounded up) */
    int gasc, gdesc, gh;                /* GDI's cell (TEXTMETRIC) */
    int ul_y, ul_h;                     /* GDI's underline rows, from the cell's top */
    FcCharSet *cs;
    int ok;
};
#define NFONT 64
static struct tfont fonts[NFONT]; static int font_next; static unsigned font_serial;

static int family_matches(FcPattern *p, const char *family)
{
    FcChar8 *s;
    for (int i = 0; FcPatternGetString(p, FC_FAMILY, i, &s) == FcResultMatch; i++) if (!strcasecmp((const char *)s, family)) return 1;
    return 0;
}
/* family/weight/slant -> file (libuxtheme's find_face: the theme directories when they have the family, else the system's
   match, which may be another family); exact: whether the family itself was found */
static FcPattern *match(const char *family, int weight, int italic, const FcCharSet *need, int *exact)
{
    FcPattern *pat = FcPatternCreate();
    if (family) FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddInteger(pat, FC_WEIGHT, FcWeightFromOpenType(weight));
    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
    if (need) FcPatternAddCharSet(pat, FC_CHARSET, need);
    FcPattern *got = NULL;
    FcConfig *cf[2] = { app_config, NULL };
    for (int c = 0; c < 2 && !got; c++) {
        if (c == 0 && (!app_config || need)) continue;
        FcPattern *p = FcPatternDuplicate(pat); FcResult r;
        FcConfigSubstitute(cf[c], p, FcMatchPattern); FcDefaultSubstitute(p);
        FcPattern *m = FcFontMatch(cf[c], p, &r);
        FcPatternDestroy(p);
        if (m && (c == 1 || (family && family_matches(m, family)))) got = m; else if (m) FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    if (exact) *exact = got && family && family_matches(got, family);
    return got;
}

/* pango's description: "Fam[,Fam2...] [weight/style words] [SIZE[px]]" */
static int parse_desc(const char *s, char fams[][96], int maxf, double *px, int *weight, int *italic)
{
    char buf[200], *tok[40]; int k = 0;
    snprintf(buf, sizeof buf, "%s", s);
    for (char *save = NULL, *t = strtok_r(buf, " \t", &save); t && k < 40; t = strtok_r(NULL, " \t", &save)) tok[k++] = t;
    *px = 12; *weight = 400; *italic = 0;
    if (k && ((tok[k - 1][0] >= '0' && tok[k - 1][0] <= '9') || tok[k - 1][0] == '.')) {
        char *e; double v = strtod(tok[k - 1], &e);
        if (v > 0) *px = !strcasecmp(e, "px") ? v : v * 96.0 / 72.0;
        k--;
    }
    static const struct { const char *w; int v; } words[] = {
        { "thin", 100 }, { "ultralight", 200 }, { "ultra-light", 200 }, { "extralight", 200 }, { "extra-light", 200 }, { "light", 300 },
        { "semilight", 350 }, { "semi-light", 350 }, { "demilight", 350 }, { "book", 380 }, { "regular", 400 }, { "normal", 400 },
        { "medium", 500 }, { "semibold", 600 }, { "semi-bold", 600 }, { "demibold", 600 }, { "demi-bold", 600 }, { "bold", 700 },
        { "ultrabold", 800 }, { "ultra-bold", 800 }, { "extrabold", 800 }, { "extra-bold", 800 }, { "heavy", 900 }, { "black", 900 },
        { "ultraheavy", 1000 }, { "ultra-heavy", 1000 } };
    while (k > 1) {
        const char *w = tok[k - 1]; int hit = 0;
        for (size_t i = 0; i < sizeof words / sizeof *words; i++) if (!strcasecmp(w, words[i].w)) { *weight = words[i].v; hit = 1; break; }
        if (!hit && (!strcasecmp(w, "italic") || !strcasecmp(w, "oblique"))) { *italic = 1; hit = 1; }
        if (!hit && (!strcasecmp(w, "small-caps") || !strcasecmp(w, "condensed") || !strcasecmp(w, "expanded"))) hit = 1;
        if (!hit) break;
        k--;
    }
    char all[200]; size_t o = 0; all[0] = 0;
    for (int i = 0; i < k && o + 1 < sizeof all; i++) o += (size_t)snprintf(all + o, sizeof all - o, "%s%s", i ? " " : "", tok[i]);
    int n = 0;
    for (char *save = NULL, *f = strtok_r(all, ",", &save); f && n < maxf; f = strtok_r(NULL, ",", &save)) {
        while (*f == ' ') f++;
        size_t l = strlen(f); while (l && f[l - 1] == ' ') f[--l] = 0;
        if (*f) snprintf(fams[n++], 96, "%s", f);
    }
    return n;
}

/* the line box from the font file: hhea ascent / descent at px, rounded up (pango's metrics with hinted metrics) */
static void typo_box(struct tfont *f)
{
    f->asc = f->gasc; f->desc = f->gdesc;
    if (!f->file[0]) return;
    if (!ftlib && FT_Init_FreeType(&ftlib)) return;
    FT_Face face;
    if (FT_New_Face(ftlib, f->file, f->index, &face)) return;
    if (face->units_per_EM > 0 && face->ascender > 0) {
        double s = f->px / face->units_per_EM;
        f->asc = (int)ceil(face->ascender * s - 1e-3);
        f->desc = (int)ceil(-face->descender * s - 1e-3);
    }
    FT_Done_Face(face);
}

static void cell_and_underline(struct tfont *f)
{
    ut_text_metrics tm;
    if (ut_get_text_metrics(NULL, NULL, 0, 0, &f->uf, &tm) && tm.height > 0) { f->gasc = tm.ascent; f->gdesc = tm.descent; f->gh = tm.height; f->ok = 1; }
    else { f->gasc = (int)ceil(f->px); f->gdesc = (int)ceil(f->px / 4); f->gh = f->gasc + f->gdesc; }
    /* GDI's underline: the rows lfUnderline draws under two blanks */
    f->ul_y = f->gasc + 1; f->ul_h = 1;
    int W = 40, H = f->gh + 8; unsigned char *m = calloc((size_t)W * H, 1);
    if (m) {
        ut_font u = f->uf; u.underline = 1; ut_rect r = { 2, 2, W, H };
        ut_draw_text_mask(NULL, NULL, 0, 0, &u, m, W, H, W, "  ", LINE_FLAGS, &r);
        int top = -1, bot = -1;
        for (int y = 0; y < H; y++) { int s = 0; for (int x = 0; x < W; x++) s += m[y * W + x]; if (s) { if (top < 0) top = y; bot = y; } }
        if (top >= 0) { f->ul_y = top - 2; f->ul_h = bot - top + 1; }
        free(m);
    }
}

static struct tfont *font_load(const char *desc, const char *face_override)
{
    char key[200]; snprintf(key, sizeof key, "%s|%s", desc, face_override ? face_override : "");
    for (int i = 0; i < NFONT; i++) if (fonts[i].gen == gen && !strcmp(fonts[i].key, key)) return &fonts[i];
    struct tfont *f = &fonts[font_next]; font_next = (font_next + 1) % NFONT;
    if (f->cs) FcCharSetDestroy(f->cs);
    memset(f, 0, sizeof *f); snprintf(f->key, sizeof f->key, "%s", key); f->gen = gen; f->id = ++font_serial;
    char fams[8][96]; int nf = parse_desc(desc, fams, 8, &f->px, &f->weight, &f->italic);
    FcPattern *m = NULL; int exact = 0;
    if (face_override) { snprintf(f->face, sizeof f->face, "%s", face_override); m = match(face_override, f->weight, f->italic, NULL, &exact); }
    else {
        for (int i = 0; i < nf && !exact; i++) { if (m) FcPatternDestroy(m); m = match(fams[i], f->weight, f->italic, NULL, &exact); if (exact) snprintf(f->face, sizeof f->face, "%s", fams[i]); }
        if (!exact) {   /* an alias ("Sans") or nothing installed: fontconfig's choice for the first family, by its own name */
            if (m) FcPatternDestroy(m);
            m = match(nf ? fams[0] : "Sans", f->weight, f->italic, NULL, NULL);
            FcChar8 *s; if (m && FcPatternGetString(m, FC_FAMILY, 0, &s) == FcResultMatch) snprintf(f->face, sizeof f->face, "%s", (const char *)s);
            else snprintf(f->face, sizeof f->face, "%s", nf ? fams[0] : "Tahoma");
        }
    }
    if (m) {
        FcChar8 *s; if (FcPatternGetString(m, FC_FILE, 0, &s) == FcResultMatch) snprintf(f->file, sizeof f->file, "%s", (const char *)s);
        if (FcPatternGetInteger(m, FC_INDEX, 0, &f->index) != FcResultMatch) f->index = 0;
        FcCharSet *cs; if (FcPatternGetCharSet(m, FC_CHARSET, 0, &cs) == FcResultMatch) f->cs = FcCharSetCopy(cs);
        FcPatternDestroy(m);
    }
    memcpy(f->uf.face, f->face, sizeof f->uf.face); f->uf.face[sizeof f->uf.face - 1] = 0;
    f->uf.height = -(int)lround(f->px); f->uf.weight = f->weight; f->uf.italic = f->italic;
    cell_and_underline(f);
    typo_box(f);
    f->lineh = f->asc + f->desc;
    return f;
}

static uint32_t utf8_next(const unsigned char **p)
{
    const unsigned char *s = *p; uint32_t c = *s++;
    if (c >= 0xf0 && s[0] && s[1] && s[2]) { c = (c & 7) << 18 | (s[0] & 63) << 12 | (s[1] & 63) << 6 | (s[2] & 63); s += 3; }
    else if (c >= 0xe0 && s[0] && s[1]) { c = (c & 15) << 12 | (s[0] & 63) << 6 | (s[1] & 63); s += 2; }
    else if (c >= 0xc0 && s[0]) { c = (c & 31) << 6 | (s[0] & 63); s += 1; }
    *p = s; return c;
}
static int utf8_len(const char *s) { unsigned char c = (unsigned char)*s; return c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1; }

/* the font for desc and this text: the description's face, or a face that has all of the text's characters */
static struct tfont *font_for(const char *desc, const char *text)
{
    struct tfont *f = font_load(desc, NULL);
    if (!f->cs || !text) return f;
    FcCharSet *need = NULL;
    for (const unsigned char *p = (const unsigned char *)text; *p; ) {
        uint32_t c = utf8_next(&p);
        if (c < 0x20 || c == 0xa0 || FcCharSetHasChar(f->cs, c)) continue;
        if (!need) need = FcCharSetCreate();
        FcCharSetAddChar(need, c);
    }
    if (!need) return f;
    char fams[8][96]; double px; int w, it; int nf = parse_desc(desc, fams, 8, &px, &w, &it);
    FcPattern *m = match(nf ? fams[0] : "Sans", f->weight, f->italic, need, NULL);
    FcCharSetDestroy(need);
    FcChar8 *s; char face[96] = "";
    if (m && FcPatternGetString(m, FC_FAMILY, 0, &s) == FcResultMatch) snprintf(face, sizeof face, "%s", (const char *)s);
    if (m) FcPatternDestroy(m);
    if (!face[0] || !strcasecmp(face, f->face)) return f;
    return font_load(desc, face);
}

/* ---- widths (GDI advances), cached ---- */
struct wcache { unsigned gen, font; uint32_t h; char *s; int w; };
#define NW 2048
static struct wcache wc[NW];
static uint32_t hash(const char *s, size_t n, uint32_t h) { for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u; return h; }
static int width_n(const struct tfont *f, const char *s, size_t n)
{
    if (!n) return 0;
    uint32_t h = hash(s, n, 2166136261u ^ f->id * 2654435761u);
    struct wcache *e = &wc[h % NW];
    if (e->gen == gen && e->font == f->id && e->h == h && e->s && strlen(e->s) == n && !memcmp(e->s, s, n)) return e->w;
    char *t = strndup(s, n); ut_rect r; int w = 0;
    if (ut_text_extent_font(&f->uf, t, DT_SINGLELINE_ | DT_NOPREFIX_, NULL, &r)) w = r.right - r.left;
    free(e->s); e->s = t; e->gen = gen; e->font = f->id; e->h = h; e->w = w;
    return w;
}

/* ---- markup: the mnemonic subset (menu.lua: <u>x</u>, &amp; &lt; &gt; &quot; &apos;; other tags are dropped) ---- */
struct span { int a, b; };
static char *unmarkup(const char *in, struct span *ul, int *nul, int maxul)
{
    size_t n = strlen(in); char *o = malloc(n + 1); size_t k = 0; int under = -1; *nul = 0;
    for (size_t i = 0; i < n; ) {
        if (in[i] == '<') {
            const char *e = strchr(in + i, '>'); if (!e) { o[k++] = in[i++]; continue; }
            if (!strncmp(in + i, "<u>", 3)) under = (int)k;
            else if (!strncmp(in + i, "</u>", 4) && under >= 0) { if (*nul < maxul) { ul[*nul].a = under; ul[*nul].b = (int)k; (*nul)++; } under = -1; }
            i = (size_t)(e - in) + 1; continue;
        }
        if (in[i] == '&') {
            static const struct { const char *e; char c; } ents[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
            int hit = 0;
            for (size_t j = 0; j < 5; j++) { size_t l = strlen(ents[j].e); if (!strncmp(in + i, ents[j].e, l)) { o[k++] = ents[j].c; i += l; hit = 1; break; } }
            if (hit) continue;
        }
        o[k++] = in[i++];
    }
    o[k] = 0; return o;
}

/* ---- lines ---- */
struct line { int a, b; int w; };
/* split s into lines: '\n' always; wrap_w > 0: greedy at blanks, a word wider than the line between characters */
static int break_lines(const struct tfont *f, const char *s, int wrap_w, struct line **out)
{
    int cap = 8, n = 0; struct line *L = malloc(cap * sizeof *L);
    int len = (int)strlen(s), p = 0;
    for (;;) {
        int e = p; while (e < len && s[e] != '\n') e++;
        /* the paragraph [p, e) */
        int a = p;
        if (wrap_w <= 0) { if (n == cap) L = realloc(L, (cap *= 2) * sizeof *L); L[n].a = a; L[n].b = e; L[n].w = width_n(f, s + a, e - a); n++; }
        else do {
            int fit = a, i = a;
            while (i < e) {   /* the longest run of whole words that fits */
                int j = i; while (j < e && s[j] == ' ') j++;
                int k = j; while (k < e && s[k] != ' ') k++;
                if (width_n(f, s + a, k - a) > wrap_w) break;
                fit = i = k;
            }
            if (fit == a) {   /* not even one word: as many characters as fit (at least one) */
                int k = a + utf8_len(s + a);
                while (k < e) { int k2 = k + utf8_len(s + k); if (width_n(f, s + a, k2 - a) > wrap_w) break; k = k2; }
                fit = k > e ? e : k;
            }
            int b = fit; while (b > a && s[b - 1] == ' ') b--;
            if (n == cap) L = realloc(L, (cap *= 2) * sizeof *L);
            L[n].a = a; L[n].b = b; L[n].w = width_n(f, s + a, b - a); n++;
            a = fit; while (a < e && s[a] == ' ') a++;
        } while (a < e);
        if (e >= len) break;
        p = e + 1;
    }
    *out = L; return n;
}

/* ---- the rendered text of a node, cached ---- */
struct tmask {
    unsigned gen; uint32_t hv; char *key;
    cairo_surface_t *a8, *glow; int glow_r; double glow_k;
    int w, h, px, py;               /* the logical block (w x lines * lineh) and the mask's origin at (-px, -py) of it */
};
#define NM 512
static struct tmask mc[NM];

static void blur_a8(unsigned char *d, int w, int h, int st, int r)   /* lhnav_px.c px_blur_a8: a box blur, rows then columns */
{
    if (r <= 0) return;
    unsigned char *tmp = malloc((size_t)(w > h ? w : h));
    if (!tmp) return;
    for (int y = 0; y < h; y++) {
        unsigned char *row = d + y * st; int sum = 0;
        for (int x = -r; x <= r; x++) sum += x >= 0 && x < w ? row[x] : 0;
        for (int x = 0; x < w; x++) { tmp[x] = (unsigned char)(sum / (2 * r + 1)); int a = x - r, b = x + r + 1; sum += (b < w ? row[b] : 0) - (a >= 0 ? row[a] : 0); }
        memcpy(row, tmp, (size_t)w);
    }
    for (int x = 0; x < w; x++) {
        int sum = 0;
        for (int y = -r; y <= r; y++) sum += y >= 0 && y < h ? d[y * st + x] : 0;
        for (int y = 0; y < h; y++) { tmp[y] = (unsigned char)(sum / (2 * r + 1)); int a = y - r, b = y + r + 1; sum += (b < h ? d[b * st + x] : 0) - (a >= 0 ? d[a * st + x] : 0); }
        for (int y = 0; y < h; y++) d[y * st + x] = tmp[y];
    }
    free(tmp);
}

#define GLOW_PAD 10
/* lay out and render: ell_w > 0 ellipsizes each line to that width, wrap_w > 0 wraps */
static struct tmask *render(const struct node *n, int ell_w, int wrap_w)
{
    const char *desc = n->font ? n->font : default_font, *text = n->text ? n->text : "";
    char hk[64]; snprintf(hk, sizeof hk, "|%d|%d|%d|", ell_w, wrap_w, n->markup);
    uint32_t h = hash(text, strlen(text), hash(hk, strlen(hk), hash(desc, strlen(desc), 2166136261u)));
    struct tmask *e = &mc[h % NM];
    size_t kl = strlen(desc) + strlen(hk) + strlen(text) + 1;
    if (e->gen == gen && e->hv == h && e->key) {
        char *k = malloc(kl); snprintf(k, kl, "%s%s%s", desc, hk, text);
        int same = !strcmp(k, e->key); free(k);
        if (same) return e;
    }
    if (e->a8) cairo_surface_destroy(e->a8);
    if (e->glow) cairo_surface_destroy(e->glow);
    free(e->key); memset(e, 0, sizeof *e);
    e->key = malloc(kl); snprintf(e->key, kl, "%s%s%s", desc, hk, text); e->hv = h; e->gen = gen;

    struct span ul[8]; int nul = 0;
    char *plain = n->markup ? unmarkup(text, ul, &nul, 8) : strdup(text);
    struct tfont *f = font_for(desc, plain);
    struct line *L; int nl = break_lines(f, plain, wrap_w, &L);
    int bw = 0;
    for (int i = 0; i < nl; i++) {
        if (ell_w > 0 && L[i].w > ell_w) {   /* GDI's ellipsized width */
            char *t = strndup(plain + L[i].a, L[i].b - L[i].a); ut_rect b = { 0, 0, ell_w, 0x7fff }, r;
            if (ut_text_extent_font(&f->uf, t, DT_SINGLELINE_ | DT_NOPREFIX_ | DT_END_ELLIPSIS_, &b, &r)) L[i].w = r.right - r.left;
            if (L[i].w > ell_w) L[i].w = ell_w;
            free(t);
        }
        if (L[i].w > bw) bw = L[i].w;
    }
    e->w = bw; e->h = nl * f->lineh;
    int above = f->gasc > f->asc ? f->gasc - f->asc : 0, below = f->gdesc > f->desc ? f->gdesc - f->desc : 0;
    e->px = GLOW_PAD + 2; e->py = GLOW_PAD + above + 1;
    int W = e->px + bw + f->gh / 2 + (f->italic ? f->gh / 3 : 0) + 2 + GLOW_PAD, H = e->py + e->h + below + 1 + GLOW_PAD;
    e->a8 = cairo_image_surface_create(CAIRO_FORMAT_A8, W, H);
    unsigned char *m = cairo_image_surface_get_data(e->a8); int st = cairo_image_surface_get_stride(e->a8);
    for (int i = 0; i < nl; i++) {
        if (L[i].b <= L[i].a) continue;
        char *t = strndup(plain + L[i].a, L[i].b - L[i].a);
        int top = e->py + i * f->lineh + f->asc - f->gasc;   /* the GDI cell on the line's baseline */
        ut_rect r = { e->px, top, ell_w > 0 ? e->px + ell_w : W, top + f->gh };
        ut_draw_text_mask(NULL, NULL, 0, 0, &f->uf, m, W, H, st, t, LINE_FLAGS | (ell_w > 0 ? DT_END_ELLIPSIS_ : 0), &r);
        for (int u = 0; u < nul; u++) {   /* mnemonic underlines inside this line */
            int a = ul[u].a > L[i].a ? ul[u].a : L[i].a, b = ul[u].b < L[i].b ? ul[u].b : L[i].b;
            if (b <= a) continue;
            int x0 = width_n(f, plain + L[i].a, a - L[i].a), x1 = width_n(f, plain + L[i].a, b - L[i].a);
            if (x1 > L[i].w) x1 = L[i].w;
            for (int y = top + f->ul_y; y < top + f->ul_y + f->ul_h; y++) if (y >= 0 && y < H)
                for (int x = e->px + x0; x < e->px + x1 && x < W; x++) m[y * st + x] = 255;
        }
        free(t);
    }
    cairo_surface_mark_dirty(e->a8);
    free(L); free(plain);
    return e;
}

/* ---- the scene's calls ---- */
void text_measure_w(const struct node *n, double wrap_w, double *w, double *h)
{
    struct tmask *e = render(n, 0, wrap_w > 0 ? (int)floor(wrap_w) : 0);
    *w = e->w; *h = e->h;
}
void text_measure(const struct node *n, double *w, double *h) { text_measure_w(n, -1, w, h); }

/* the block's top-left on a whole device pixel */
static void snap(cairo_t *cr, double *x, double *y)
{
    double dx = *x, dy = *y; cairo_user_to_device(cr, &dx, &dy);
    dx = floor(dx + 0.5); dy = floor(dy + 0.5);
    cairo_device_to_user(cr, &dx, &dy); *x = dx; *y = dy;
}

void text_paint(cairo_t *cr, struct node *n)
{
    double aw = n->cw - n->ml - n->mr, ah = n->ch - n->mt - n->mb;
    int ell = 0, wrap = 0;
    if (n->wrap) wrap = (int)floor(aw); else if (n->ellipsis) ell = (int)floor(aw) > 0 ? (int)floor(aw) : 1;
    struct tmask *e = render(n, ell, wrap);
    int tw = e->w, th = e->h;
    double tx = n->cx + n->ml + (n->halign == 1 ? (aw - tw) / 2 : n->halign == 2 ? aw - tw : 0);
    double ty = n->cy + n->mt + (n->valign == 1 ? (ah - th) / 2 : n->valign == 2 ? ah - th : 0);
    snap(cr, &tx, &ty);
    {   /* LHSHELL_TEXT_TRACE=FILE: every painted text node, its rect, the block's top-left and baseline (before/after checks) */
        static FILE *tr; static int init;
        if (!init) { init = 1; const char *p = getenv("LHSHELL_TEXT_TRACE"); if (p) tr = fopen(p, "a"); }
        if (tr) {
            const struct tfont *f = font_for(n->font ? n->font : default_font, n->text);
            double ux = tx, uy = ty; cairo_user_to_device(cr, &ux, &uy);
            fprintf(tr, "%s\t%g %g %g %g\t%g %g\t%g\t%d %d\n", n->text ? n->text : "", n->cx, n->cy, n->cw, n->ch, ux, uy, uy + f->asc, tw, th); fflush(tr);
        }
    }
    double x0 = tx - e->px, y0 = ty - e->py;
    if (n->has_glow) {
        int R = n->glow_size > 0 ? n->glow_size : 4; double k = n->glow_strength > 0 ? n->glow_strength : 2.5;
        if (R > GLOW_PAD - 1) R = GLOW_PAD - 1;
        if (!e->glow || e->glow_r != R || e->glow_k != k) {
            if (e->glow) cairo_surface_destroy(e->glow);
            int W = cairo_image_surface_get_width(e->a8), H = cairo_image_surface_get_height(e->a8), st = cairo_image_surface_get_stride(e->a8);
            e->glow = cairo_image_surface_create(CAIRO_FORMAT_A8, W, H);
            unsigned char *g = cairo_image_surface_get_data(e->glow); cairo_surface_flush(e->a8);
            memcpy(g, cairo_image_surface_get_data(e->a8), (size_t)st * H);
            int r1 = (R + 1) / 2; blur_a8(g, W, H, st, r1); blur_a8(g, W, H, st, R - r1 > 0 ? R - r1 : 1);
            for (size_t i = 0; i < (size_t)st * H; i++) { double v = g[i] * k; g[i] = (unsigned char)(v > 255 ? 255 : v); }
            cairo_surface_mark_dirty(e->glow); e->glow_r = R; e->glow_k = k;
        }
        cairo_set_source_rgba(cr, n->glow[0], n->glow[1], n->glow[2], n->glow[3]); cairo_mask_surface(cr, e->glow, x0, y0);
    }
    if (n->has_shadow) { cairo_set_source_rgba(cr, n->shadow[0], n->shadow[1], n->shadow[2], n->shadow[3]); cairo_mask_surface(cr, e->a8, x0 + n->shadow_dx, y0 + n->shadow_dy); }
    cairo_set_source_rgba(cr, n->color[0], n->color[1], n->color[2], n->color[3]); cairo_mask_surface(cr, e->a8, x0, y0);
}

/* the theme's font directories (lh.fonts): libuxtheme's and our lookup's; every cached font, width and mask is stale */
void text_add_font_dir(const char *dir)
{
    if (!dir || !*dir) return;
    FcInit();
    if (!app_config) {
        app_config = FcConfigCreate();
        FcConfigParseAndLoadFromMemory(app_config, (const FcChar8 *)"<fontconfig><cachedir prefix=\"xdg\">fontconfig</cachedir></fontconfig>", FcFalse);
    }
    FcConfigAppFontAddDir(app_config, (const FcChar8 *)dir);
    ut_add_font_dir(dir);
    gen++;
}
void text_fonts_changed(void) { gen++; }

/* what desc resolves to for plain text: the face libuxtheme draws with and its file */
void text_resolve(const char *desc, char *face, size_t fn, char *file, size_t fl)
{
    struct tfont *f = font_load(desc ? desc : default_font, NULL);
    snprintf(face, fn, "%s", f->face); snprintf(file, fl, "%s", f->file[0] ? f->file : "?");
}
