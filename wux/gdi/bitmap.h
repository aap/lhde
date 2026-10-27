// 32bpp BGRA pixel surfaces.
#pragma once
#include <stdint.h>
#include "geom.h"
#include "color.h"

namespace gdi {

struct Bitmap {
    int32_t width, height;
    int32_t stride;      // in pixels
    uint32_t* pixels;
    bool owned;
    // Alpha-tracking surface (an ARGB toplevel's backbuffer): pixels are
    // premultiplied, and the blending paths (blit_alpha, blend_mask, text)
    // do src-over that also updates destination alpha. Opaque surfaces
    // (false) keep the plain paths, which write alpha 255.
    bool has_alpha;

    Rect rect() const { return Rect{0, 0, width, height}; }
    uint32_t* row(int32_t y) { return pixels + (intptr_t)y * stride; }
    const uint32_t* row(int32_t y) const { return pixels + (intptr_t)y * stride; }
};

Bitmap* create_bitmap(int32_t w, int32_t h);
Bitmap* wrap_bitmap(int32_t w, int32_t h, int32_t stride, uint32_t* pixels);
void destroy_bitmap(Bitmap* b);

} // namespace gdi
