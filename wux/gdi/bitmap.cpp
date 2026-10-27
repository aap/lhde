#include <stdlib.h>
#include <string.h>
#include "bitmap.h"

namespace gdi {

Bitmap* create_bitmap(int32_t w, int32_t h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    Bitmap* b = new Bitmap;
    b->width = w; b->height = h; b->stride = w;
    b->pixels = (uint32_t*)calloc((size_t)w * h, sizeof(uint32_t));
    b->owned = true;
    b->has_alpha = false;
    return b;
}

Bitmap* wrap_bitmap(int32_t w, int32_t h, int32_t stride, uint32_t* pixels)
{
    Bitmap* b = new Bitmap;
    b->width = w; b->height = h; b->stride = stride;
    b->pixels = pixels;
    b->owned = false;
    b->has_alpha = false;
    return b;
}

void destroy_bitmap(Bitmap* b)
{
    if (!b) return;
    if (b->owned) free(b->pixels);
    delete b;
}

} // namespace gdi
