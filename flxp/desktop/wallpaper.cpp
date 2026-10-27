#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <png.h>
#include <jpeglib.h>
#include <algorithm>
#include "wallpaper.h"
#include "gdi/bmp.h"
#include "gdi/image.h"
#include "shell/config.h"

namespace wallpaper {

static bool load_png(const std::string& data, Image& out)
{
    png_image im;
    memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&im, data.data(), data.size())) return false;
    im.format = PNG_FORMAT_BGRA;    // = 0xAARRGGBB in memory on little endian
    out.width = (int)im.width; out.height = (int)im.height;
    out.px.assign((size_t)out.width * out.height, 0);
    if (!png_image_finish_read(&im, nullptr, out.px.data(), 0, nullptr)) { png_image_free(&im); out = Image(); return false; }
    return true;
}

struct JpegErr { jpeg_error_mgr mgr; jmp_buf jb; };
static void jpeg_fail(j_common_ptr c) { longjmp(((JpegErr*)c->err)->jb, 1); }

static bool load_jpeg(const std::string& data, Image& out)
{
    jpeg_decompress_struct c;
    JpegErr e;
    c.err = jpeg_std_error(&e.mgr);
    e.mgr.error_exit = jpeg_fail;
    std::vector<uint8_t> row;
    if (setjmp(e.jb)) { jpeg_destroy_decompress(&c); out = Image(); return false; }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, (const unsigned char*)data.data(), (unsigned long)data.size());
    if (jpeg_read_header(&c, TRUE) != JPEG_HEADER_OK) { jpeg_destroy_decompress(&c); return false; }
    c.out_color_space = JCS_RGB;
    jpeg_start_decompress(&c);
    out.width = (int)c.output_width; out.height = (int)c.output_height;
    out.px.assign((size_t)out.width * out.height, 0);
    row.resize((size_t)out.width * 3);
    while (c.output_scanline < c.output_height) {
        JSAMPROW r = row.data();
        int y = (int)c.output_scanline;
        jpeg_read_scanlines(&c, &r, 1);
        uint32_t* d = out.px.data() + (size_t)y * out.width;
        for (int x = 0; x < out.width; x++) d[x] = 0xff000000u | (uint32_t)row[x * 3] << 16 | (uint32_t)row[x * 3 + 1] << 8 | row[x * 3 + 2];
    }
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    return true;
}

bool load(const std::string& path, Image& out, std::string* err)
{
    out = Image();
    std::string data;
    if (!shell::read_file(path, data) || data.size() < 8) { if (err) *err = "cannot read " + path; return false; }
    const unsigned char* d = (const unsigned char*)data.data();
    bool ok = false;
    if (!memcmp(d, "\x89PNG", 4)) ok = load_png(data, out);
    else if (d[0] == 0xff && d[1] == 0xd8) ok = load_jpeg(data, out);
    else if ((d[0] == 'B' && d[1] == 'M') || (data.size() >= 16 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4))) {
        gdi::Bitmap* b = d[0] == 'B' ? gdi::decode_bmp(d, data.size()) : gdi::decode_image(d, data.size());
        if (b) {
            out.width = b->width; out.height = b->height;
            out.px.resize((size_t)b->width * b->height);
            for (int y = 0; y < b->height; y++) memcpy(&out.px[(size_t)y * b->width], b->row(y), (size_t)b->width * 4);
            gdi::destroy_bitmap(b);
            ok = true;
        }
    }
    if (!ok && err) *err = path + ": not a BMP, PNG or JPEG image this can read";
    return ok;
}

Mode mode_from(const std::string& n)
{
    if (n == "center" || n == "centre") return Mode::Center;
    if (n == "tile") return Mode::Tile;
    if (n == "fill") return Mode::Fill;
    if (n == "fit") return Mode::Fit;
    return Mode::Stretch;
}

// Resample the source region (sx, sy, sw, sh) of img to dw x dh: box filter
// (area average) along an axis that shrinks, bilinear along one that grows.
static void resample(const Image& img, double sx, double sy, double sw, double sh, std::vector<uint32_t>& out, int dw, int dh)
{
    out.assign((size_t)dw * dh, 0);
    // per axis: for each destination index a list of (source index, weight)
    struct Tap { int i; float w; };
    auto taps = [](int dn, double s0, double sn, int limit) {
        std::vector<std::vector<Tap>> t((size_t)dn);
        double scale = sn / dn;
        for (int d = 0; d < dn; d++) {
            if (scale > 1.0) {
                double a = s0 + d * scale, b = a + scale;
                for (int i = (int)a; i < (int)b + 1 && i < limit; i++) {
                    double lo = i > a ? i : a, hi = i + 1 < b ? i + 1 : b;
                    if (hi > lo && i >= 0) t[(size_t)d].push_back({ i, (float)((hi - lo) / scale) });
                }
            } else {
                double c = s0 + (d + 0.5) * scale - 0.5;
                int i0 = (int)(c < 0 ? -1 : c); double f = c - i0;
                if (c < 0) { i0 = 0; f = 0; }
                int i1 = i0 + 1 < limit ? i0 + 1 : i0;
                t[(size_t)d].push_back({ i0, (float)(1 - f) });
                if (f > 0) t[(size_t)d].push_back({ i1, (float)f });
            }
        }
        return t;
    };
    auto tx = taps(dw, sx, sw, img.width), ty = taps(dh, sy, sh, img.height);
    std::vector<float> rowbuf((size_t)dw * 3);
    for (int y = 0; y < dh; y++) {
        std::fill(rowbuf.begin(), rowbuf.end(), 0.f);
        for (const Tap& a : ty[(size_t)y]) {
            const uint32_t* src = img.px.data() + (size_t)a.i * img.width;
            for (int x = 0; x < dw; x++) {
                float r = 0, g = 0, b = 0;
                for (const Tap& t : tx[(size_t)x]) {
                    uint32_t p = src[t.i];
                    r += t.w * (float)((p >> 16) & 0xff); g += t.w * (float)((p >> 8) & 0xff); b += t.w * (float)(p & 0xff);
                }
                rowbuf[(size_t)x * 3] += a.w * r; rowbuf[(size_t)x * 3 + 1] += a.w * g; rowbuf[(size_t)x * 3 + 2] += a.w * b;
            }
        }
        uint32_t* d = out.data() + (size_t)y * dw;
        for (int x = 0; x < dw; x++) {
            auto c = [](float v) { int i = (int)(v + 0.5f); return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i); };
            d[x] = 0xff000000u | c(rowbuf[(size_t)x * 3]) << 16 | c(rowbuf[(size_t)x * 3 + 1]) << 8 | c(rowbuf[(size_t)x * 3 + 2]);
        }
    }
}

// an image pixel over the background (images may have alpha)
static inline uint32_t over(uint32_t p, uint32_t bg)
{
    uint32_t a = p >> 24;
    if (a == 255) return p;
    uint32_t r = (((p >> 16) & 0xff) * a + ((bg >> 16) & 0xff) * (255 - a)) / 255;
    uint32_t g = (((p >> 8) & 0xff) * a + ((bg >> 8) & 0xff) * (255 - a)) / 255;
    uint32_t b = ((p & 0xff) * a + (bg & 0xff) * (255 - a)) / 255;
    return 0xff000000u | r << 16 | g << 8 | b;
}

void compose(const Image& img, Mode mode, uint32_t background, uint32_t* out, int width, int height, int x0, int y0, int w, int h)
{
    uint32_t bg = 0xff000000u | (background & 0xffffff);
    for (size_t i = 0; i < (size_t)width * height; i++) out[i] = bg;
    if (img.empty() || w <= 0 || h <= 0) return;
    auto put = [&](const uint32_t* src, int sw, int sh, int dx, int dy) {
        for (int y = 0; y < sh; y++) {
            int ty = dy + y;
            if (ty < 0 || ty >= height) continue;
            for (int x = 0; x < sw; x++) {
                int tx = dx + x;
                if (tx >= 0 && tx < width) out[(size_t)ty * width + tx] = over(src[(size_t)y * sw + x], bg);
            }
        }
    };
    std::vector<uint32_t> tmp;
    switch (mode) {
    case Mode::Center:
        put(img.px.data(), img.width, img.height, x0 + (w - img.width) / 2, y0 + (h - img.height) / 2);
        break;
    case Mode::Tile:
        for (int y = y0; y < y0 + h; y += img.height)
            for (int x = x0; x < x0 + w; x += img.width) put(img.px.data(), img.width, img.height, x, y);
        break;
    case Mode::Stretch:
        resample(img, 0, 0, img.width, img.height, tmp, w, h);
        put(tmp.data(), w, h, x0, y0);
        break;
    case Mode::Fill: {
        // cover the rect keeping the aspect ratio; crop the overhang equally on both sides
        double sw = img.width, sh = img.height;
        if ((double)w / h > sw / sh) sh = sw * h / w; else sw = sh * w / h;
        resample(img, (img.width - sw) / 2, (img.height - sh) / 2, sw, sh, tmp, w, h);
        put(tmp.data(), w, h, x0, y0);
        break;
    }
    case Mode::Fit: {
        // inside the rect keeping the aspect ratio, the background around it (Windows 7's Fit)
        int fw = w, fh = h;
        if ((double)w / h > (double)img.width / img.height) fw = std::max(1, (int)((double)img.width * h / img.height + 0.5));
        else fh = std::max(1, (int)((double)img.height * w / img.width + 0.5));
        resample(img, 0, 0, img.width, img.height, tmp, fw, fh);
        put(tmp.data(), fw, fh, x0 + (w - fw) / 2, y0 + (h - fh) / 2);
        break;
    }
    }
}

} // namespace wallpaper
