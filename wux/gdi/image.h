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

// max_size > 0: the caller will shrink the result to fit max_size x max_size
// (a thumbnail), so the result may be smaller than the image, never below that
// size: JPEG decodes with DCT scaling (1/2, 1/4, 1/8), PNG is reduced row by row
// (by up to 64), WebP rescaled by libwebp; what can only decode at full size is
// refused above 25 megapixels, a file read whole above 64 MB, a JPEG whose
// decoder needs more than 160 MB. orig_w/orig_h get the full image size (after
// the EXIF rotation). Null on anything unreadable. load_image reads JPEG and PNG
// from the file as they decode.
Bitmap* decode_image(const uint8_t* data, size_t size, int max_size = 0, int* orig_w = nullptr, int* orig_h = nullptr);
Bitmap* load_image(const std::string& path, int max_size = 0, int* orig_w = nullptr, int* orig_h = nullptr);
bool webp_supported();                                      // built with libwebp

Bitmap* decode_jpeg(const uint8_t* data, size_t size, int max_size = 0, int* orig_w = nullptr, int* orig_h = nullptr);
// One icon/cursor directory image: a PNG, or a DIB with doubled height and
// the 1bpp AND mask after the colour bits (cursor: mask=1 over colour != 0
// stays black, the screen inversion we cannot do).
Bitmap* decode_icon_image(const uint8_t* data, size_t size, bool cursor = false);
Bitmap* decode_ico(const uint8_t* data, size_t size);       // .ico/.cur file: the best frame

// b turned as an EXIF Orientation (1..8) says, 1 or another value: as it is; b is taken (the result replaces it)
Bitmap* orient(Bitmap* b, int orientation);

// Resample to exactly dw x dh: area average where an axis shrinks, bilinear
// where it grows; alpha-weighted so transparent pixels do not darken edges.
Bitmap* resample(const Bitmap* src, int dw, int dh);
// The size that fits w x h into max_w x max_h keeping the aspect ratio
// (at least 1 px); never larger than the image unless grow.
void fit_size(int w, int h, int max_w, int max_h, bool grow, int* out_w, int* out_h);

} // namespace gdi
