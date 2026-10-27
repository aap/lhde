// Image files into 32bpp 0xAARRGGBB bitmaps (straight alpha, top-down), the
// shared decoder of the wallpaper, the thumbnails and the icon loader:
// PNG (libpng), JPEG (libjpeg, EXIF orientation applied), BMP, ICO and CUR
// (the largest, deepest frame), WebP (libwebp if built with it: alpha, an
// animation's first frame). Plus resampling for thumbnails.
#pragma once
#include <stddef.h>
#include <string>
#include "bitmap.h"

namespace gdi {

// max_size > 0 is a hint that the caller will shrink the result to fit
// max_size x max_size: JPEG then decodes with DCT scaling (1/2, 1/4, 1/8),
// never below that size. orig_w/orig_h get the full image size (after the
// EXIF rotation). Null on anything unreadable.
Bitmap* decode_image(const uint8_t* data, size_t size, int max_size = 0, int* orig_w = nullptr, int* orig_h = nullptr);
Bitmap* load_image(const std::string& path, int max_size = 0, int* orig_w = nullptr, int* orig_h = nullptr);
bool is_image_data(const uint8_t* data, size_t size);       // a signature decode_image knows
bool webp_supported();                                      // built with libwebp

Bitmap* decode_jpeg(const uint8_t* data, size_t size, int max_size = 0, int* orig_w = nullptr, int* orig_h = nullptr);
// One icon/cursor directory image: a PNG, or a DIB with doubled height and
// the 1bpp AND mask after the colour bits (cursor: mask=1 over colour != 0
// stays black, the screen inversion we cannot do).
Bitmap* decode_icon_image(const uint8_t* data, size_t size, bool cursor = false);
Bitmap* decode_ico(const uint8_t* data, size_t size);       // .ico/.cur file: the best frame

// Resample to exactly dw x dh: area average where an axis shrinks, bilinear
// where it grows; alpha-weighted so transparent pixels do not darken edges.
Bitmap* resample(const Bitmap* src, int dw, int dh);
// The size that fits w x h into max_w x max_h keeping the aspect ratio
// (at least 1 px); never larger than the image unless grow.
void fit_size(int w, int h, int max_w, int max_h, bool grow, int* out_w, int* out_h);

} // namespace gdi
