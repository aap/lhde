// PNG decoding (libpng's simplified API) into 32bpp 0xAARRGGBB bitmaps,
// straight (not premultiplied) alpha, like decode_bmp; writing with text
// chunks (the freedesktop thumbnails' Thumb::* keys).
#pragma once
#include <stddef.h>
#include <string>
#include <utility>
#include <vector>
#include "bitmap.h"

namespace gdi {

typedef std::vector<std::pair<std::string, std::string>> PngText;

Bitmap* decode_png(const uint8_t* data, size_t size);   // null on bad input
// The tEXt (and uncompressed iTXt) chunks before the image data.
PngText read_png_text(const uint8_t* data, size_t size);
// 8-bit RGBA, the text as tEXt chunks; false on any error (nothing is removed).
bool write_png(const Bitmap* b, const std::string& path, const PngText& text);

} // namespace gdi
