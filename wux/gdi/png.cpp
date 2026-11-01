#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <png.h>
#include "png.h"

namespace gdi {

Bitmap* decode_png(const uint8_t* data, size_t size)
{
    png_image im;
    memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&im, data, size)) return nullptr;
    im.format = PNG_FORMAT_BGRA;    // = 0xAARRGGBB in memory on little endian
    if (im.width == 0 || im.height == 0 || im.width > 16384 || im.height > 16384) { png_image_free(&im); return nullptr; }
    Bitmap* b = create_bitmap((int32_t)im.width, (int32_t)im.height);
    if (!b) { png_image_free(&im); return nullptr; }
    if (!png_image_finish_read(&im, nullptr, b->pixels, (png_int_32)(b->stride * 4), nullptr)) {
        png_image_free(&im);
        destroy_bitmap(b);
        return nullptr;
    }
    return b;
}

} // namespace gdi

namespace gdi {

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

PngText read_png_text(const uint8_t* d, size_t n)
{
    PngText out;
    if (n < 8 || memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return out;
    size_t o = 8;
    while (o + 12 <= n) {
        uint32_t len = be32(d + o);
        const uint8_t* type = d + o + 4;
        const uint8_t* body = d + o + 8;
        if (len > n - o - 12) break;
        if (!memcmp(type, "IDAT", 4) || !memcmp(type, "IEND", 4)) break;
        if (!memcmp(type, "tEXt", 4)) {
            const uint8_t* z = (const uint8_t*)memchr(body, 0, len);
            if (z) out.emplace_back(std::string((const char*)body, (size_t)(z - body)), std::string((const char*)z + 1, (size_t)(body + len - z - 1)));
        } else if (!memcmp(type, "iTXt", 4)) {
            // keyword\0 compression-flag compression-method language\0 translated\0 text
            const uint8_t* end = body + len;
            const uint8_t* z = (const uint8_t*)memchr(body, 0, len);
            if (z && z + 3 <= end && z[1] == 0) {
                const uint8_t* lang = z + 3;
                const uint8_t* z2 = (const uint8_t*)memchr(lang, 0, (size_t)(end - lang));
                const uint8_t* z3 = z2 ? (const uint8_t*)memchr(z2 + 1, 0, (size_t)(end - z2 - 1)) : nullptr;
                if (z3) out.emplace_back(std::string((const char*)body, (size_t)(z - body)), std::string((const char*)z3 + 1, (size_t)(end - z3 - 1)));
            }
        }
        o += 12 + len;
    }
    return out;
}

// the libpng part, apart so that nothing with a destructor lives across its setjmp
static bool write_rows(FILE* f, const Bitmap* b, png_byte* row, png_text* chunks, int nchunks)
{
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, (png_uint_32)b->width, (png_uint_32)b->height, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (nchunks) png_set_text(png, info, chunks, nchunks);
    png_write_info(png, info);
    for (int y = 0; y < b->height; y++) {
        const uint32_t* s = b->row(y);
        for (int x = 0; x < b->width; x++) {
            uint32_t p = s[x];
            row[x * 4] = (png_byte)(p >> 16); row[x * 4 + 1] = (png_byte)(p >> 8); row[x * 4 + 2] = (png_byte)p; row[x * 4 + 3] = (png_byte)(p >> 24);
        }
        png_write_row(png, row);
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return true;
}

// fopen(path) or open_memstream: the rows into f
static bool write_to(FILE* f, const Bitmap* b, const PngText& text)
{
    std::vector<png_byte> row((size_t)b->width * 4);
    std::vector<png_text> chunks(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        memset(&chunks[i], 0, sizeof chunks[i]);
        chunks[i].compression = PNG_TEXT_COMPRESSION_NONE;
        chunks[i].key = (png_charp)text[i].first.c_str();
        chunks[i].text = (png_charp)text[i].second.c_str();
        chunks[i].text_length = text[i].second.size();
    }
    return write_rows(f, b, row.data(), chunks.data(), (int)chunks.size());
}

bool write_png(const Bitmap* b, const std::string& path, const PngText& text)
{
    if (!b || b->width <= 0 || b->height <= 0) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = write_to(f, b, text);
    return fclose(f) == 0 && ok;
}

bool encode_png(const Bitmap* b, std::string& out, const PngText& text)
{
    out.clear();
    if (!b || b->width <= 0 || b->height <= 0) return false;
    char* buf = nullptr;
    size_t len = 0;
    FILE* f = open_memstream(&buf, &len);
    if (!f) return false;
    bool ok = write_to(f, b, text);
    ok = fclose(f) == 0 && ok;
    if (ok) out.assign(buf, len);
    free(buf);
    return ok;
}

} // namespace gdi
