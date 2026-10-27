// A device context: a target surface, an origin, and a clip region in device
// coordinates. All drawing calls take logical coordinates, which are device
// coordinates minus the origin.
#pragma once
#include "bitmap.h"
#include "region.h"

namespace gdi {

struct DC {
    Bitmap* target;
    Point origin;          // device = logical + origin
    Region clip;           // device coords; empty region means nothing is drawn
    Rect clip_box;         // bounds of clip, device coords
};

DC* create_dc(Bitmap* target);
void destroy_dc(DC* dc);

void set_clip(DC* dc, const Region& device_region);
void set_origin(DC* dc, Point origin);
Rect clip_box(const DC* dc);                 // logical coords
bool rect_visible(const DC* dc, const Rect& r);

void fill_rect(DC* dc, const Rect& r, Color c);
// Set r to transparent black (premultiplied 0,0,0,0): on an alpha-tracking
// surface (Bitmap::has_alpha) the window manager's frame shows through there.
void clear_rect(DC* dc, const Rect& r);
void frame_rect(DC* dc, const Rect& r, Color c);          // 1px inside r
void hline(DC* dc, int32_t x0, int32_t x1, int32_t y, Color c);   // [x0,x1)
void vline(DC* dc, int32_t x, int32_t y0, int32_t y1, Color c);   // [y0,y1)
void line(DC* dc, Point a, Point b, Color c);               // excludes b, like GDI
void set_pixel(DC* dc, Point p, Color c);
Color get_pixel(const DC* dc, Point p);

// Copy src_rect of src to dst (top-left) in dc.
void blit(DC* dc, Point dst, const Bitmap* src, const Rect& src_rect);
// Per-pixel alpha composite, with an additional constant alpha 0..255.
void blit_alpha(DC* dc, Point dst, const Bitmap* src, const Rect& src_rect, uint8_t alpha = 255);
// Nearest-neighbour stretch.
void stretch_blit(DC* dc, const Rect& dst, const Bitmap* src, const Rect& src_rect);
// Composite fg through an 8-bit coverage mask (0 = transparent, 255 = solid)
// whose top-left lands at pos.
void blend_mask(DC* dc, Point pos, const uint8_t* mask, int32_t w, int32_t h, int32_t stride, Color fg);
// A soft halo (Longhorn's TextGlow): the coverage mask (top-left at pos) is
// widened and blurred by about `radius` pixels and composited in `glow`
// (glow.a scales it). Draw the text itself afterwards.
void glow_mask(DC* dc, Point pos, const uint8_t* mask, int32_t w, int32_t h, int32_t stride, Color glow, int32_t radius);

} // namespace gdi
