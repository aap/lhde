// BMP/DIB decoding into 32bpp 0xAARRGGBB bitmaps.
#pragma once
#include <stddef.h>
#include "bitmap.h"

namespace gdi {

// Decode a BMP. Accepts both a full file (BITMAPFILEHEADER first) and a bare
// DIB as stored in RT_BITMAP resources (BITMAPINFOHEADER first). Handles
// core/info/V4/V5 headers, 1/4/8bpp paletted, 16/24/32bpp, BI_BITFIELDS.
// 32bpp BI_RGB passes the fourth byte through as alpha (themes rely on it);
// formats without alpha get 255; RLE4/RLE8 too (pixels the runs skip are
// transparent). JPEG/PNG-in-BMP and bad input return null.
Bitmap* decode_bmp(const uint8_t* data, size_t size);

bool write_ppm(const Bitmap*, const char* path);   // P6, alpha ignored; for tests
bool write_bmp(const Bitmap*, const char* path);   // 32bpp BI_RGB, bottom-up, alpha kept

} // namespace gdi
