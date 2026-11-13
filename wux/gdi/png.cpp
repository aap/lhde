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


// ---- reduced while reading -------------------------------------------------------

struct PngSource { FILE* f; const uint8_t* d; size_t n, at; };

static void png_source_read(png_structp png, png_bytep out, png_size_t len)
{
    PngSource* s = (PngSource*)png_get_io_ptr(png);
    if (s->f) {
        if (fread(out, 1, len, s->f) != len) png_error(png, "short read");
        return;
    }
    if (len > s->n - s->at) png_error(png, "short read");
    memcpy(out, s->d + s->at, len);
    s->at += len;
}

// the libpng part, apart so that nothing with a destructor lives across its setjmp; row and sums are the caller's, sized
// by the first call (rows null: only the header is read, into *w, *h, *interlaced)
static bool reduced_rows(PngSource* src, int max_size, int* w, int* h, bool* interlaced, std::vector<png_byte>* row,
                         std::vector<uint32_t>* sums, std::vector<uint32_t>* counts, Bitmap** out)
{
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    Bitmap* volatile b = nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        if (b) destroy_bitmap(b);
        return false;
    }
    png_set_read_fn(png, src, png_source_read);
    png_set_user_limits(png, 0x7fffffff, 0x7fffffff);
    png_read_info(png, info);
    png_uint_32 iw = 0, ih = 0;
    int depth = 0, type = 0, il = 0;
    png_get_IHDR(png, info, &iw, &ih, &depth, &type, &il, nullptr, nullptr);
    *w = (int)iw; *h = (int)ih;
    *interlaced = il != PNG_INTERLACE_NONE;
    if (*interlaced || !out || iw == 0 || ih == 0 || iw > 0x7fffff || ih > 0x7fffff) {
        png_destroy_read_struct(&png, &info, nullptr);
        return !*interlaced && iw && ih;
    }
    // 8-bit BGRA rows whatever the file has (palette, grey, tRNS, 16-bit)
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_bgr(png);
    png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    // the largest factor that keeps both sides at least max_size (as the JPEG's DCT scaling)
    int f = 1;
    int shorter = (int)(iw < ih ? iw : ih);
    while (f < 64 && shorter / (f * 2) >= max_size) f *= 2;   // 64: the sums stay within 32 bits
    int ow = (int)((iw + f - 1) / f), oh = (int)((ih + f - 1) / f);
    b = create_bitmap(ow, oh);
    if (!b) { png_destroy_read_struct(&png, &info, nullptr); return false; }
    row->assign((size_t)iw * 4, 0);
    sums->assign((size_t)ow * 4, 0);
    counts->assign((size_t)ow, 0);
    for (png_uint_32 y = 0; y < ih; y++) {
        png_read_row(png, row->data(), nullptr);
        const png_byte* r = row->data();
        uint32_t* s = sums->data();
        for (png_uint_32 x = 0; x < iw; x++, r += 4) {   // premultiplied sums: transparent pixels do not darken
            uint32_t a = r[3], *t = s + (x / f) * 4;
            t[0] += r[0] * a; t[1] += r[1] * a; t[2] += r[2] * a; t[3] += a;
            (*counts)[x / f]++;
        }
        if ((y + 1) % (png_uint_32)f && y + 1 != ih) continue;
        uint32_t* d = ((Bitmap*)b)->row((int)(y / f));
        for (int x = 0; x < ow; x++) {
            uint32_t* t = s + x * 4, n = (*counts)[(size_t)x], a = t[3];
            d[x] = !a ? 0 : (a + n / 2) / n << 24 | (t[2] + a / 2) / a << 16 | (t[1] + a / 2) / a << 8 | (t[0] + a / 2) / a;
            t[0] = t[1] = t[2] = t[3] = 0;
            (*counts)[(size_t)x] = 0;
        }
    }
    png_destroy_read_struct(&png, &info, nullptr);
    *out = b;
    return true;
}

Bitmap* decode_png_reduced(FILE* f, const uint8_t* data, size_t size, int max_size, int* w, int* h, bool* interlaced)
{
    PngSource src = { f, data, size, 0 };
    std::vector<png_byte> row;
    std::vector<uint32_t> sums, counts;
    Bitmap* out = nullptr;
    int iw = 0, ih = 0;
    bool il = false;
    bool ok = reduced_rows(&src, max_size < 1 ? 1 : max_size, &iw, &ih, &il, &row, &sums, &counts, &out);
    if (w) *w = iw;
    if (h) *h = ih;
    if (interlaced) *interlaced = il;
    return ok ? out : nullptr;
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
