/* lhnav_px: lhnav's private pixel operations on premultiplied ARGB (lhnav_px.c), in place of cairo: images, blits (1:1, scaled
   box-filtered or bilinear, with opacity), solid and gradient fills of rectangles and of simple shapes rasterised from a signed
   distance (antialiased by supersampling the pixels on the edge), PNG files through libpng. Compositing is OVER with pixman's
   rounding, so a 1:1 blit of an image gives the same pixels cairo gave. */
#ifndef LHNAV_PX_H
#define LHNAV_PX_H
#include <stddef.h>
#include <stdint.h>

/* an image: premultiplied ARGB, stride 4 w, owned */
typedef struct px_img { int w, h; uint32_t *px; } px_img;
px_img *px_img_new(int w, int h);                                          /* transparent */
px_img *px_img_from(const uint32_t *px, int w, int h, int rows);           /* a copy of rows [0, rows) of a stride-4w buffer */
void    px_img_free(px_img **im);
px_img *px_img_png(const char *path);                                      /* NULL when unreadable */
uint32_t *px_png_decode(const void *data, size_t size, int *w, int *h);    /* PNG bytes -> premultiplied, stride 4 w (free) */
uint32_t px_img_at(const px_img *im, int x, int y);                        /* 0 outside */

/* a destination: a buffer with a clip rectangle [x0, x1) x [y0, y1); copy the struct to save / restore the clip */
typedef struct px_cv { uint32_t *px; int w, h, stride; int x0, y0, x1, y1; } px_cv;
px_cv px_cv_make(uint32_t *px, int w, int h, int stride);
px_cv px_cv_img(px_img *im);
void  px_clip(px_cv *c, int x, int y, int w, int h);                       /* intersect */

/* a paint: a solid colour or a gradient (cairo's: colours unpremultiplied with alpha, interpolated, then premultiplied; PAD) */
enum { PX_SOLID, PX_LINEAR, PX_RADIAL };
typedef struct px_paint {
    int type, nstops;
    double x0, y0, r0, x1, y1, r1;          /* linear: (x0, y0) -> (x1, y1); radial: circle 0 -> circle 1 */
    double off[4], rgba[4][4];              /* stops */
} px_paint;
px_paint px_solid(double r, double g, double b, double a);
px_paint px_rgb(uint32_t rgb, double a);                                   /* 0xRRGGBB */
px_paint px_linear(double x0, double y0, double x1, double y1);
px_paint px_radial(double cx0, double cy0, double r0, double cx1, double cy1, double r1);
void     px_stop(px_paint *p, double off, double r, double g, double b, double a);

/* compositing (OVER) */
void px_over(uint32_t *d, uint32_t s);
void px_blit(px_cv *c, const px_img *im, int x, int y, double alpha);                 /* 1:1 at (x, y) */
void px_blit_part(px_cv *c, const px_img *im, int sx, int sy, int w, int h, int x, int y);   /* source rect */
/* scaled by k with its top-left at (x, y): k < 0.75 box-filtered (the area each pixel covers), else bilinear (cairo's GOOD
   filter makes the same choice); k = 1 at integer x, y: px_blit */
void px_blit_scaled(px_cv *c, const px_img *im, double x, double y, double k, double alpha);
px_img *px_img_scaled(const px_img *im, int w, int h);                      /* a new image of im drawn into w x h */
/* an A8 coverage mask (stride mw) at (x, y) in a colour (0xRRGGBB) at opacity alpha */
void px_mask(px_cv *c, const unsigned char *m, int mw, int mh, int x, int y, uint32_t rgb, double alpha);

/* filling */
void px_fill_rect(px_cv *c, int x, int y, int w, int h, const px_paint *p);    /* whole pixels */
/* shapes in pixel coordinates (pixel (i, j) covers [i, i + 1) x [j, j + 1); a 1 px line on x + 0.5 fills column x) */
void px_fill_rrect(px_cv *c, double x, double y, double w, double h, double r, const px_paint *p);
void px_stroke_rrect(px_cv *c, double x, double y, double w, double h, double r, double lw, const px_paint *p);
void px_fill_circle(px_cv *c, double cx, double cy, double r, const px_paint *p);
void px_stroke_circle(px_cv *c, double cx, double cy, double r, double lw, const px_paint *p);
void px_fill_poly(px_cv *c, const double *xy, int n, const px_paint *p);          /* n points, nonzero winding */
/* a polyline (closed: back to the first point) of width lw; round: round caps and joins, else butt caps (joins rounded) */
void px_stroke_poly(px_cv *c, const double *xy, int n, int closed, double lw, int round, const px_paint *p);
/* a whole area painted (cairo_paint through a clip): the paint over the clip rectangle */
void px_paint_all(px_cv *c, const px_paint *p);

/* box blur of an A8 image, radius r, in place (one pass horizontally, one vertically) */
void px_blur_a8(unsigned char *d, int w, int h, int stride, int r);
#endif
