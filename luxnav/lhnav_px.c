/* lhnav_px: pixel operations on premultiplied ARGB for lhnav (lhnav_px.h). Plain C, libpng for PNG files. */
#include "lhnav_px.h"
#include <math.h>
#include <png.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static inline uint32_t mul8(uint32_t a, uint32_t b) { uint32_t t = a * b + 0x80; return ((t >> 8) + t) >> 8; }

/* ---- images ---- */

px_img *px_img_new(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    px_img *im = malloc(sizeof *im);
    if (!im) return NULL;
    im->w = w; im->h = h; im->px = calloc((size_t)w * h, 4);
    if (!im->px) { free(im); return NULL; }
    return im;
}

px_img *px_img_from(const uint32_t *px, int w, int h, int rows)
{
    if (rows > h) rows = h;
    px_img *im = px_img_new(w, rows);
    if (im) memcpy(im->px, px, (size_t)w * rows * 4);
    return im;
}

void px_img_free(px_img **im)
{
    if (!*im) return;
    free((*im)->px); free(*im); *im = NULL;
}

uint32_t px_img_at(const px_img *im, int x, int y)
{
    return im && x >= 0 && y >= 0 && x < im->w && y < im->h ? im->px[(size_t)y * im->w + x] : 0;
}

/* PNG through libpng's classic API, transformed as cairo's PNG loader does (palette/grey/tRNS expanded, 16 bits stripped, no
   gamma) and premultiplied with its rounding */
struct png_mem { const unsigned char *p; size_t n; };
static void png_mem_read(png_structp pp, png_bytep d, png_size_t len)
{
    struct png_mem *m = png_get_io_ptr(pp);
    if (len > m->n) png_error(pp, "short");
    memcpy(d, m->p, len); m->p += len; m->n -= len;
}
static uint32_t *png_decode(FILE *f, struct png_mem *mem, int *ow, int *oh)
{
    png_structp pp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = pp ? png_create_info_struct(pp) : NULL;
    uint32_t *volatile o = NULL;
    png_bytep *volatile rows = NULL;
    if (!info) { png_destroy_read_struct(&pp, NULL, NULL); return NULL; }
    if (setjmp(png_jmpbuf(pp))) { free(o); free(rows); png_destroy_read_struct(&pp, &info, NULL); return NULL; }
    if (f) png_init_io(pp, f); else png_set_read_fn(pp, mem, png_mem_read);
    png_read_info(pp, info);
    png_uint_32 w, h; int depth, ctype, il;
    png_get_IHDR(pp, info, &w, &h, &depth, &ctype, &il, NULL, NULL);
    if (ctype == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(pp);
    if (ctype == PNG_COLOR_TYPE_GRAY) png_set_expand_gray_1_2_4_to_8(pp);
    if (png_get_valid(pp, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(pp);
    if (depth == 16) png_set_strip_16(pp);
    if (depth < 8) png_set_packing(pp);
    if (ctype == PNG_COLOR_TYPE_GRAY || ctype == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(pp);
    if (il != PNG_INTERLACE_NONE) png_set_interlace_handling(pp);
    png_set_filler(pp, 0xff, PNG_FILLER_AFTER);   /* RGBA bytes */
    png_read_update_info(pp, info);
    o = malloc((size_t)w * h * 4);
    rows = malloc(h * sizeof *rows);
    if (!o || !rows) png_error(pp, "memory");
    for (png_uint_32 y = 0; y < h; y++) rows[y] = (png_bytep)(o + (size_t)y * w);
    png_read_image(pp, rows);
    png_read_end(pp, NULL);
    png_destroy_read_struct(&pp, &info, NULL);
    free(rows);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t *b = (const uint8_t *)&o[i];
        uint32_t r = b[0], g = b[1], bl = b[2], a = b[3];
        o[i] = a << 24 | mul8(r, a) << 16 | mul8(g, a) << 8 | mul8(bl, a);
    }
    *ow = (int)w; *oh = (int)h;
    return o;
}

uint32_t *px_png_decode(const void *data, size_t size, int *w, int *h)
{
    struct png_mem m = { data, size };
    return size > 8 && !memcmp(data, "\x89PNG", 4) ? png_decode(NULL, &m, w, h) : NULL;
}

px_img *px_img_png(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int w, h;
    uint32_t *px = png_decode(f, NULL, &w, &h);
    fclose(f);
    if (!px) return NULL;
    px_img *im = malloc(sizeof *im);
    if (!im) { free(px); return NULL; }
    im->w = w; im->h = h; im->px = px;
    return im;
}

/* ---- destinations ---- */

px_cv px_cv_make(uint32_t *px, int w, int h, int stride)
{
    px_cv c = { px, w, h, stride, 0, 0, w, h };
    return c;
}
px_cv px_cv_img(px_img *im) { return px_cv_make(im->px, im->w, im->h, im->w * 4); }

void px_clip(px_cv *c, int x, int y, int w, int h)
{
    if (x > c->x0) c->x0 = x;
    if (y > c->y0) c->y0 = y;
    if (x + w < c->x1) c->x1 = x + w;
    if (y + h < c->y1) c->y1 = y + h;
    if (c->x1 < c->x0) c->x1 = c->x0;
    if (c->y1 < c->y0) c->y1 = c->y0;
}

static inline uint32_t *at(px_cv *c, int x, int y) { return (uint32_t *)((char *)c->px + (size_t)y * c->stride) + x; }

/* ---- compositing: pixman's OVER (UN8x4_MUL_UN8_ADD_UN8x4) ---- */

static inline uint32_t px_mul(uint32_t p, uint32_t a)
{
    if (a == 255) return p;
    return mul8(p >> 24, a) << 24 | mul8(p >> 16 & 255, a) << 16 | mul8(p >> 8 & 255, a) << 8 | mul8(p & 255, a);
}
void px_over(uint32_t *d, uint32_t s)
{
    uint32_t ia = 255 - (s >> 24);
    if (!s) return;
    if (!ia) { *d = s; return; }
    uint32_t o = 0, dv = *d;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t v = (s >> sh & 255) + mul8(dv >> sh & 255, ia);
        o |= (v > 255 ? 255 : v) << sh;
    }
    *d = o;
}
/* cairo's colour -> 8 bits (double -> 16 bit short -> pixman's >> 8) */
static inline uint32_t to8(double v) { if (v <= 0) return 0; if (v >= 1) return 255; return (uint32_t)(v * 65535.0 + 0.5) >> 8; }
static inline uint32_t alpha8(double a) { return to8(a); }

static void blit_part_a(px_cv *c, const px_img *im, int sx, int sy, int w, int h, int x, int y, uint32_t a);
void px_blit(px_cv *c, const px_img *im, int x, int y, double alpha)
{
    if (im) blit_part_a(c, im, 0, 0, im->w, im->h, x, y, alpha >= 1 ? 255 : alpha8(alpha));
}

static void blit_part_a(px_cv *c, const px_img *im, int sx, int sy, int w, int h, int x, int y, uint32_t a)
{
    for (int j = 0; j < h; j++) {
        int dy = y + j, yy = sy + j;
        if (dy < c->y0 || dy >= c->y1 || yy < 0 || yy >= im->h) continue;
        for (int i = 0; i < w; i++) {
            int dx = x + i, xx = sx + i;
            if (dx < c->x0 || dx >= c->x1 || xx < 0 || xx >= im->w) continue;
            uint32_t s = im->px[(size_t)yy * im->w + xx];
            if (a != 255) s = px_mul(s, a);
            px_over(at(c, dx, dy), s);
        }
    }
}

void px_blit_part(px_cv *c, const px_img *im, int sx, int sy, int w, int h, int x, int y)
{
    if (im) blit_part_a(c, im, sx, sy, w, h, x, y, 255);
}

/* weights of the source pixels for destination pixel i (box: the overlap of [i, i+1) mapped into the source, over its length;
   bilinear: the two neighbours of the mapped centre); returns the first source index and fills wt[0..*n) */
static int weights(int i, double x, double k, int box, double *wt, int *n, int max)
{
    if (box) {
        double a = (i - x) / k, b = (i + 1 - x) / k, len = b - a;
        int j0 = (int)floor(a), j1 = (int)ceil(b);
        *n = 0;
        for (int j = j0; j < j1 && *n < max; j++) {
            double o = fmin(b, j + 1.0) - fmax(a, (double)j);
            wt[(*n)++] = o > 0 ? o / len : 0;
        }
        return j0;
    }
    double u = (i + 0.5 - x) / k - 0.5;
    int j0 = (int)floor(u);
    double f = u - j0;
    wt[0] = 1 - f; wt[1] = f; *n = 2;
    return j0;
}

void px_blit_scaled(px_cv *c, const px_img *im, double x, double y, double k, double alpha)
{
    if (!im || k <= 0) return;
    uint32_t a = alpha >= 1 ? 255 : alpha8(alpha);
    if (fabs(k - 1) < 1e-9 && x == floor(x) && y == floor(y)) { blit_part_a(c, im, 0, 0, im->w, im->h, (int)x, (int)y, a); return; }
    int box = k < 0.75;
    int X0 = (int)floor(x) - 1, X1 = (int)ceil(x + im->w * k) + 1, Y0 = (int)floor(y) - 1, Y1 = (int)ceil(y + im->h * k) + 1;
    if (X0 < c->x0) X0 = c->x0;
    if (Y0 < c->y0) Y0 = c->y0;
    if (X1 > c->x1) X1 = c->x1;
    if (Y1 > c->y1) Y1 = c->y1;
    enum { MAXW = 64 };
    double wx[MAXW], wy[MAXW];
    for (int dy = Y0; dy < Y1; dy++) {
        int ny, jy = weights(dy, y, k, box, wy, &ny, MAXW);
        for (int dx = X0; dx < X1; dx++) {
            int nx, jx = weights(dx, x, k, box, wx, &nx, MAXW);
            double acc[4] = { 0, 0, 0, 0 };
            for (int q = 0; q < ny; q++) {
                int sy = jy + q;
                if (sy < 0 || sy >= im->h || wy[q] == 0) continue;
                const uint32_t *row = im->px + (size_t)sy * im->w;
                for (int p = 0; p < nx; p++) {
                    int sx = jx + p;
                    if (sx < 0 || sx >= im->w || wx[p] == 0) continue;
                    double w = wx[p] * wy[q];
                    uint32_t s = row[sx];
                    acc[0] += (s & 255) * w; acc[1] += (s >> 8 & 255) * w; acc[2] += (s >> 16 & 255) * w; acc[3] += (s >> 24) * w;
                }
            }
            uint32_t s = 0, al = (uint32_t)fmin(255, acc[3] + 0.5);
            for (int ch = 0; ch < 3; ch++) { uint32_t v = (uint32_t)fmin(255, acc[ch] + 0.5); if (v > al) v = al; s |= v << (8 * ch); }
            s |= al << 24;
            if (a != 255) s = px_mul(s, a);
            px_over(at(c, dx, dy), s);
        }
    }
}

px_img *px_img_scaled(const px_img *im, int w, int h)
{
    px_img *d = px_img_new(w > 0 ? w : 1, h > 0 ? h : 1);
    if (!d || !im) return d;
    px_cv c = px_cv_img(d);
    px_blit_scaled(&c, im, 0, 0, (double)w / im->w, 1);   /* the aspect is kept by the callers */
    return d;
}

void px_mask(px_cv *c, const unsigned char *m, int mw, int mh, int x, int y, uint32_t rgb, double alpha)
{
    uint32_t a = alpha8(alpha),
             s = a << 24 | to8((rgb >> 16 & 255) / 255.0 * alpha) << 16 | to8((rgb >> 8 & 255) / 255.0 * alpha) << 8 | to8((rgb & 255) / 255.0 * alpha);
    for (int j = 0; j < mh; j++) {
        int dy = y + j;
        if (dy < c->y0 || dy >= c->y1) continue;
        for (int i = 0; i < mw; i++) {
            int dx = x + i;
            unsigned v = m[(size_t)j * mw + i];
            if (!v || dx < c->x0 || dx >= c->x1) continue;
            px_over(at(c, dx, dy), px_mul(s, v));
        }
    }
}

/* ---- paints ---- */

px_paint px_solid(double r, double g, double b, double a)
{
    px_paint p;
    memset(&p, 0, sizeof p);
    p.type = PX_SOLID; p.nstops = 1;
    p.rgba[0][0] = r; p.rgba[0][1] = g; p.rgba[0][2] = b; p.rgba[0][3] = a;
    return p;
}
px_paint px_rgb(uint32_t c, double a) { return px_solid((c >> 16 & 255) / 255.0, (c >> 8 & 255) / 255.0, (c & 255) / 255.0, a); }
px_paint px_linear(double x0, double y0, double x1, double y1)
{
    px_paint p;
    memset(&p, 0, sizeof p);
    p.type = PX_LINEAR; p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y1;
    return p;
}
px_paint px_radial(double cx0, double cy0, double r0, double cx1, double cy1, double r1)
{
    px_paint p;
    memset(&p, 0, sizeof p);
    p.type = PX_RADIAL; p.x0 = cx0; p.y0 = cy0; p.r0 = r0; p.x1 = cx1; p.y1 = cy1; p.r1 = r1;
    return p;
}
void px_stop(px_paint *p, double off, double r, double g, double b, double a)
{
    if (p->nstops >= 4) return;
    int i = p->nstops++;
    p->off[i] = off; p->rgba[i][0] = r; p->rgba[i][1] = g; p->rgba[i][2] = b; p->rgba[i][3] = a;
}

static uint32_t solid_px(const double *c)
{
    return alpha8(c[3]) << 24 | to8(c[0] * c[3]) << 16 | to8(c[1] * c[3]) << 8 | to8(c[2] * c[3]);
}

/* the paint's premultiplied colour at the pixel centre (px, py) (pixman: unpremultiplied colour and alpha interpolated, PAD) */
static uint32_t paint_at(const px_paint *p, double px, double py)
{
    if (p->type == PX_SOLID || p->nstops < 2) return solid_px(p->rgba[0]);
    double t;
    if (p->type == PX_LINEAR) {
        double dx = p->x1 - p->x0, dy = p->y1 - p->y0, l2 = dx * dx + dy * dy;
        t = l2 > 0 ? ((px - p->x0) * dx + (py - p->y0) * dy) / l2 : 0;
    } else {   /* the largest t with |p - c(t)| = r(t), r(t) >= 0 */
        double cdx = p->x1 - p->x0, cdy = p->y1 - p->y0, dr = p->r1 - p->r0;
        double pdx = px - p->x0, pdy = py - p->y0;
        double A = cdx * cdx + cdy * cdy - dr * dr, B = pdx * cdx + pdy * cdy + p->r0 * dr, C = pdx * pdx + pdy * pdy - p->r0 * p->r0;
        if (fabs(A) < 1e-9) {
            if (fabs(B) < 1e-12) return 0;
            t = C / (2 * B);
            if (p->r0 + t * dr < 0) return 0;
        } else {
            double disc = B * B - A * C;
            if (disc < 0) return 0;
            double s = sqrt(disc), t1 = (B + s) / A, t2 = (B - s) / A;
            if (t1 < t2) { double x = t1; t1 = t2; t2 = x; }
            if (p->r0 + t1 * dr >= 0) t = t1;
            else if (p->r0 + t2 * dr >= 0) t = t2;
            else return 0;
        }
    }
    int n = p->nstops;
    if (t <= p->off[0]) return solid_px(p->rgba[0]);
    if (t >= p->off[n - 1]) return solid_px(p->rgba[n - 1]);
    int i = 0;
    while (i + 2 < n && t > p->off[i + 1]) i++;
    double f = p->off[i + 1] > p->off[i] ? (t - p->off[i]) / (p->off[i + 1] - p->off[i]) : 0;
    const double *a = p->rgba[i], *b = p->rgba[i + 1];
    double al = (a[3] + (b[3] - a[3]) * f) * 255;
    uint32_t r = (uint32_t)(al * (a[0] + (b[0] - a[0]) * f) + 0.5f), g = (uint32_t)(al * (a[1] + (b[1] - a[1]) * f) + 0.5f),
             bl = (uint32_t)(al * (a[2] + (b[2] - a[2]) * f) + 0.5f), a8 = (uint32_t)(al + 0.5f);
    return a8 << 24 | (r > a8 ? a8 : r) << 16 | (g > a8 ? a8 : g) << 8 | (bl > a8 ? a8 : bl);
}

void px_fill_rect(px_cv *c, int x, int y, int w, int h, const px_paint *p)
{
    int x0 = x > c->x0 ? x : c->x0, y0 = y > c->y0 ? y : c->y0, x1 = x + w < c->x1 ? x + w : c->x1, y1 = y + h < c->y1 ? y + h : c->y1;
    uint32_t s = p->type == PX_SOLID ? paint_at(p, 0, 0) : 0;
    for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) px_over(at(c, i, j), p->type == PX_SOLID ? s : paint_at(p, i + 0.5, j + 0.5));
}

void px_paint_all(px_cv *c, const px_paint *p) { px_fill_rect(c, c->x0, c->y0, c->x1 - c->x0, c->y1 - c->y0, p); }

/* ---- shapes from a signed distance (< 0 inside): pixels farther than 0.75 from the edge are whole, the others are sampled
   16 x 16 ---- */

typedef double (*sdf_fn)(const void *ctx, double x, double y);

static void fill_sdf(px_cv *c, sdf_fn f, const void *ctx, double bx0, double by0, double bx1, double by1, const px_paint *p)
{
    int x0 = (int)floor(bx0) - 1, y0 = (int)floor(by0) - 1, x1 = (int)ceil(bx1) + 1, y1 = (int)ceil(by1) + 1;
    if (x0 < c->x0) x0 = c->x0;
    if (y0 < c->y0) y0 = c->y0;
    if (x1 > c->x1) x1 = c->x1;
    if (y1 > c->y1) y1 = c->y1;
    enum { SX = 32, SY = 15 };   /* cairo's scan converter: 15 sample rows (at their tops) per pixel row, x nearly exact */
    for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) {
        double d = f(ctx, i + 0.5, j + 0.5);
        unsigned cov;
        if (d <= -0.75) cov = 255;
        else if (d >= 0.75) continue;
        else {
            int in = 0;
            for (int sy = 0; sy < SY; sy++) for (int sx = 0; sx < SX; sx++)
                in += f(ctx, i + (sx + 0.5) / SX, j + (double)sy / SY + 1e-7) < 0;
            if (!in) continue;
            cov = (unsigned)((in * 255 + SX * SY / 2) / (SX * SY));
        }
        uint32_t s = paint_at(p, i + 0.5, j + 0.5);
        px_over(at(c, i, j), px_mul(s, cov));
    }
}

struct rr { double cx, cy, hw, hh, r, lw; };
static double rr_sdf(const void *v, double x, double y)
{
    const struct rr *q = v;
    double dx = fabs(x - q->cx) - (q->hw - q->r), dy = fabs(y - q->cy) - (q->hh - q->r);
    double ox = dx > 0 ? dx : 0, oy = dy > 0 ? dy : 0;
    double d = sqrt(ox * ox + oy * oy) + fmin(fmax(dx, dy), 0) - q->r;
    return q->lw > 0 ? fabs(d) - q->lw / 2 : d;
}
void px_fill_rrect(px_cv *c, double x, double y, double w, double h, double r, const px_paint *p)
{
    struct rr q = { x + w / 2, y + h / 2, w / 2, h / 2, fmin(r, fmin(w, h) / 2), 0 };
    fill_sdf(c, rr_sdf, &q, x, y, x + w, y + h, p);
}
void px_stroke_rrect(px_cv *c, double x, double y, double w, double h, double r, double lw, const px_paint *p)
{
    struct rr q = { x + w / 2, y + h / 2, w / 2, h / 2, fmin(r, fmin(w, h) / 2), lw };
    fill_sdf(c, rr_sdf, &q, x - lw, y - lw, x + w + lw, y + h + lw, p);
}

struct circ { double cx, cy, r, lw; };
static double circ_sdf(const void *v, double x, double y)
{
    const struct circ *q = v;
    double d = hypot(x - q->cx, y - q->cy) - q->r;
    return q->lw > 0 ? fabs(d) - q->lw / 2 : d;
}
void px_fill_circle(px_cv *c, double cx, double cy, double r, const px_paint *p)
{
    struct circ q = { cx, cy, r, 0 };
    fill_sdf(c, circ_sdf, &q, cx - r, cy - r, cx + r, cy + r, p);
}
void px_stroke_circle(px_cv *c, double cx, double cy, double r, double lw, const px_paint *p)
{
    struct circ q = { cx, cy, r, lw };
    fill_sdf(c, circ_sdf, &q, cx - r - lw, cy - r - lw, cx + r + lw, cy + r + lw, p);
}

struct poly { const double *xy; int n, closed, round, stroke; double lw; };
static double seg_dist(double px, double py, double ax, double ay, double bx, double by, int clamp)
{
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    if (t < 0 || t > 1) { if (!clamp) return 1e9; t = t < 0 ? 0 : 1; }
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}
static double poly_sdf(const void *v, double x, double y)
{
    const struct poly *q = v;
    int n = q->n, segs = q->closed || !q->stroke ? n : n - 1, wind = 0;
    double d = 1e9;
    for (int i = 0; i < segs; i++) {
        const double *a = q->xy + 2 * i, *b = q->xy + 2 * ((i + 1) % n);
        d = fmin(d, seg_dist(x, y, a[0], a[1], b[0], b[1], !q->stroke || q->round));
        if (!q->stroke) {   /* nonzero winding */
            if (a[1] <= y) { if (b[1] > y && (b[0] - a[0]) * (y - a[1]) - (x - a[0]) * (b[1] - a[1]) > 0) wind++; }
            else if (b[1] <= y && (b[0] - a[0]) * (y - a[1]) - (x - a[0]) * (b[1] - a[1]) < 0) wind--;
        }
    }
    if (q->stroke) {
        if (!q->round)   /* joins: round (the inner vertices; all of them on a closed path) */
            for (int i = q->closed ? 0 : 1; i < (q->closed ? n : n - 1); i++) d = fmin(d, hypot(x - q->xy[2 * i], y - q->xy[2 * i + 1]));
        return d - q->lw / 2;
    }
    return wind ? -d : d;
}
static void poly_box(const double *xy, int n, double m, double *b)
{
    b[0] = b[1] = 1e9; b[2] = b[3] = -1e9;
    for (int i = 0; i < n; i++) {
        b[0] = fmin(b[0], xy[2 * i] - m); b[2] = fmax(b[2], xy[2 * i] + m);
        b[1] = fmin(b[1], xy[2 * i + 1] - m); b[3] = fmax(b[3], xy[2 * i + 1] + m);
    }
}
void px_fill_poly(px_cv *c, const double *xy, int n, const px_paint *p)
{
    if (n < 3) return;
    struct poly q = { xy, n, 1, 0, 0, 0 };
    double b[4]; poly_box(xy, n, 0, b);
    fill_sdf(c, poly_sdf, &q, b[0], b[1], b[2], b[3], p);
}
void px_stroke_poly(px_cv *c, const double *xy, int n, int closed, double lw, int round, const px_paint *p)
{
    if (n < 2) return;
    struct poly q = { xy, n, closed, round, 1, lw };
    double b[4]; poly_box(xy, n, lw, b);
    fill_sdf(c, poly_sdf, &q, b[0], b[1], b[2], b[3], p);
}

/* ---- blur ---- */

void px_blur_a8(unsigned char *d, int w, int h, int st, int r)
{
    if (r <= 0) return;
    unsigned char *tmp = malloc((size_t)(w > h ? w : h));
    if (!tmp) return;
    for (int y = 0; y < h; y++) {
        unsigned char *row = d + y * st; int sum = 0;
        for (int x = -r; x <= r; x++) sum += x >= 0 && x < w ? row[x] : 0;
        for (int x = 0; x < w; x++) {
            tmp[x] = (unsigned char)(sum / (2 * r + 1));
            int a = x - r, b = x + r + 1;
            sum += (b < w ? row[b] : 0) - (a >= 0 ? row[a] : 0);
        }
        memcpy(row, tmp, (size_t)w);
    }
    for (int x = 0; x < w; x++) {
        int sum = 0;
        for (int y = -r; y <= r; y++) sum += y >= 0 && y < h ? d[y * st + x] : 0;
        for (int y = 0; y < h; y++) {
            tmp[y] = (unsigned char)(sum / (2 * r + 1));
            int a = y - r, b = y + r + 1;
            sum += (b < h ? d[b * st + x] : 0) - (a >= 0 ? d[a * st + x] : 0);
        }
        for (int y = 0; y < h; y++) d[y * st + x] = tmp[y];
    }
    free(tmp);
}
