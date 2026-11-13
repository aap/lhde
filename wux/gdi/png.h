// PNG decoding (libpng's simplified API) into 32bpp 0xAARRGGBB bitmaps,
// straight (not premultiplied) alpha, like decode_bmp; writing with text
// chunks (the freedesktop thumbnails' Thumb::* keys).
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <string>
#include <utility>
#include <vector>
#include "bitmap.h"

namespace gdi {

typedef std::vector<std::pair<std::string, std::string>> PngText;

Bitmap* decode_png(const uint8_t* data, size_t size);   // null on bad input
// Reduced while reading, row by row, so the full image is never in memory: each pixel the average of a factor x factor
// block (premultiplied), the largest power of two that keeps both sides at least max_size. Reads from f when it is not
// null, else from data. w, h: the image's size; an interlaced PNG is not read (null, *interlaced set).
Bitmap* decode_png_reduced(FILE* f, const uint8_t* data, size_t size, int max_size, int* w, int* h, bool* interlaced);
// The tEXt (and uncompressed iTXt) chunks before the image data.
PngText read_png_text(const uint8_t* data, size_t size);
// 8-bit RGBA, the text as tEXt chunks; false on any error (nothing is removed).
bool write_png(const Bitmap* b, const std::string& path, const PngText& text);
// The same file's bytes in memory (the clipboard's image/png)
bool encode_png(const Bitmap* b, std::string& out, const PngText& text);

} // namespace gdi
