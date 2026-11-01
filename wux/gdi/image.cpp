#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <vector>
#include <jpeglib.h>
#include "image.h"
#include "bmp.h"
#include "png.h"
#ifdef FLXP_WEBP
#include <webp/decode.h>
#include <webp/demux.h>
#endif

namespace gdi {

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// ---- JPEG ---------------------------------------------------------------------

struct JpegErr { jpeg_error_mgr mgr; jmp_buf jb; };
static void jpeg_fail(j_common_ptr c) { longjmp(((JpegErr*)c->err)->jb, 1); }
static void jpeg_quiet(j_common_ptr, int) {}

// EXIF orientation (tag 0x0112 in IFD0 of the APP1 "Exif" block), 1 if none.
static int exif_orientation(const uint8_t* d, size_t n)
{
    if (n < 14 || memcmp(d, "Exif\0\0", 6)) return 1;
    const uint8_t* t = d + 6;
    size_t tn = n - 6;
    bool le = t[0] == 'I';
    if (!(le ? t[0] == 'I' && t[1] == 'I' : t[0] == 'M' && t[1] == 'M')) return 1;
    auto u16 = [&](size_t o) -> uint32_t { return o + 2 > tn ? 0 : le ? (uint32_t)(t[o] | t[o + 1] << 8) : (uint32_t)(t[o] << 8 | t[o + 1]); };
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 > tn ? 0 : le ? (u16(o) | u16(o + 2) << 16) : (u16(o) << 16 | u16(o + 2)); };
    size_t ifd = u32(4);
    if (ifd + 2 > tn) return 1;
    uint32_t count = u16(ifd);
    for (uint32_t i = 0; i < count && ifd + 2 + (i + 1) * 12 <= tn; i++) {
        size_t e = ifd + 2 + i * 12;
        if (u16(e) == 0x0112) { uint32_t v = u16(e + 8); return v >= 1 && v <= 8 ? (int)v : 1; }
    }
    return 1;
}

static Bitmap* orient(Bitmap* s, int o)
{
    if (o <= 1 || !s) return s;
    int w = s->width, h = s->height;
    bool swap = o >= 5;
    Bitmap* d = create_bitmap(swap ? h : w, swap ? w : h);
    if (!d) return s;
    for (int y = 0; y < d->height; y++) {
        uint32_t* out = d->row(y);
        for (int x = 0; x < d->width; x++) {
            int sx, sy;
            switch (o) {
            case 2: sx = w - 1 - x; sy = y; break;
            case 3: sx = w - 1 - x; sy = h - 1 - y; break;
            case 4: sx = x; sy = h - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = h - 1 - x; break;
            case 7: sx = w - 1 - y; sy = h - 1 - x; break;
            default: sx = w - 1 - y; sy = x; break;   // 8
            }
            out[x] = s->row(sy)[sx];
        }
    }
    destroy_bitmap(s);
    return d;
}

Bitmap* decode_jpeg(const uint8_t* data, size_t size, int max_size, int* orig_w, int* orig_h)
{
    jpeg_decompress_struct c;
    JpegErr e;
    c.err = jpeg_std_error(&e.mgr);
    e.mgr.error_exit = jpeg_fail;
    e.mgr.emit_message = jpeg_quiet;
    Bitmap* volatile b = nullptr;
    std::vector<uint8_t> row;
    if (setjmp(e.jb)) {
        jpeg_destroy_decompress(&c);
        if (b) destroy_bitmap(b);
        return nullptr;
    }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, (const unsigned char*)data, (unsigned long)size);
    jpeg_save_markers(&c, JPEG_APP0 + 1, 0xffff);
    if (jpeg_read_header(&c, TRUE) != JPEG_HEADER_OK) { jpeg_destroy_decompress(&c); return nullptr; }
    int o = 1;
    for (jpeg_saved_marker_ptr m = c.marker_list; m; m = m->next)
        if (m->marker == JPEG_APP0 + 1) { o = exif_orientation(m->data, m->data_length); if (o != 1) break; }
    int iw = (int)c.image_width, ih = (int)c.image_height;
    if (orig_w) *orig_w = o >= 5 ? ih : iw;
    if (orig_h) *orig_h = o >= 5 ? iw : ih;
    if (iw <= 0 || ih <= 0 || (int64_t)iw * ih > 200000000) { jpeg_destroy_decompress(&c); return nullptr; }
    if (max_size > 0) {
        // the largest 1/denom that still covers max_size along the shorter fit
        int denom = 1;
        while (denom < 8 && iw / (denom * 2) >= max_size && ih / (denom * 2) >= max_size) denom *= 2;
        c.scale_num = 1; c.scale_denom = (unsigned)denom;
        c.dct_method = JDCT_IFAST;
    }
    c.out_color_space = JCS_RGB;
    jpeg_start_decompress(&c);
    int w = (int)c.output_width, h = (int)c.output_height;
    b = create_bitmap(w, h);
    if (!b) { jpeg_abort_decompress(&c); jpeg_destroy_decompress(&c); return nullptr; }
    row.resize((size_t)w * 3);
    while (c.output_scanline < c.output_height) {
        JSAMPROW r = row.data();
        int y = (int)c.output_scanline;
        jpeg_read_scanlines(&c, &r, 1);
        uint32_t* d = b->row(y);
        for (int x = 0; x < w; x++) d[x] = 0xff000000u | (uint32_t)row[x * 3] << 16 | (uint32_t)row[x * 3 + 1] << 8 | row[x * 3 + 2];
    }
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    return orient(b, o);
}

// ---- ICO / CUR ------------------------------------------------------------------

static bool any_alpha(const Bitmap* b)
{
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++)
            if (b->row(y)[x] >> 24) return true;
    return false;
}

Bitmap* decode_icon_image(const uint8_t* d, size_t n, bool cursor)
{
    if (n >= 8 && !memcmp(d, "\x89PNG", 4)) return decode_png(d, n);
    if (n < 40) return nullptr;
    uint32_t hdr = rd32(d);
    if (hdr < 40) return nullptr;
    int width = (int32_t)rd32(d + 4), height = (int32_t)rd32(d + 8), bpp = rd16(d + 14);
    uint32_t comp = rd32(d + 16), used = rd32(d + 32);
    int h = abs(height) / 2;
    if (width <= 0 || h <= 0) return nullptr;
    int colors = bpp <= 8 ? (used && used <= 256u ? (int)used : 1 << bpp) : (used <= 256u ? (int)used : 0);
    size_t palette = (size_t)colors * 4 + (comp == 3 && hdr == 40 ? 12 : 0);
    std::vector<uint8_t> copy(d, d + n);
    uint32_t hh = (uint32_t)h;
    copy[8] = (uint8_t)hh; copy[9] = (uint8_t)(hh >> 8); copy[10] = (uint8_t)(hh >> 16); copy[11] = (uint8_t)(hh >> 24);
    Bitmap* b = decode_bmp(copy.data(), copy.size());
    if (!b) return nullptr;
    size_t xor_stride = ((size_t)width * bpp + 31) / 32 * 4;
    size_t and_off = hdr + palette + xor_stride * h;
    size_t and_stride = ((size_t)width + 31) / 32 * 4;
    bool has_mask = and_off + and_stride * h <= n;
    if (bpp == 32 && any_alpha(b)) return b;
    for (int y = 0; y < h; y++) {
        uint32_t* p = b->row(y);
        const uint8_t* m = has_mask ? d + and_off + and_stride * (h - 1 - y) : nullptr;
        for (int x = 0; x < width; x++) {
            bool transparent = m && (m[x >> 3] & (0x80 >> (x & 7)));
            uint32_t c = p[x] & 0xffffff;
            if (transparent && c && cursor) p[x] = 0xff000000;
            else p[x] = transparent ? 0 : (c | 0xff000000);
        }
    }
    return b;
}

Bitmap* decode_ico(const uint8_t* d, size_t n)
{
    if (n < 6 || rd16(d) != 0 || (rd16(d + 2) != 1 && rd16(d + 2) != 2)) return nullptr;
    bool cursor = rd16(d + 2) == 2;
    int count = rd16(d + 4);
    int best = -1, best_area = -1, best_bpp = -1;
    for (int i = 0; i < count; i++) {
        const uint8_t* e = d + 6 + i * 16;
        if ((size_t)(e - d) + 16 > n) break;
        int w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256;
        int bpp = cursor ? 0 : rd16(e + 6);
        uint32_t size = rd32(e + 8), off = rd32(e + 12);
        if (off > n || size > n - off) continue;
        if (!bpp && size >= 40) {
            const uint8_t* img = d + off;
            bpp = !memcmp(img, "\x89PNG", 4) ? 32 : rd16(img + 14);
        }
        if (w * h > best_area || (w * h == best_area && bpp > best_bpp)) { best = i; best_area = w * h; best_bpp = bpp; }
    }
    if (best < 0) return nullptr;
    const uint8_t* e = d + 6 + best * 16;
    return decode_icon_image(d + rd32(e + 12), rd32(e + 8), cursor);
}

// ---- WebP (libwebp, when built with it) -------------------------------------------

bool webp_supported()
{
#ifdef FLXP_WEBP
    return true;
#else
    return false;
#endif
}

static bool is_webp(const uint8_t* d, size_t n) { return n >= 16 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4); }

#ifdef FLXP_WEBP
// Lossy, lossless, with alpha (straight, as WebP stores it); an animation gives its first
// frame. max_size > 0: libwebp's own rescaler decodes straight to the size that fits.
static Bitmap* decode_webp(const uint8_t* d, size_t n, int max_size, int* orig_w, int* orig_h)
{
    WebPBitstreamFeatures f;
    if (WebPGetFeatures(d, n, &f) != VP8_STATUS_OK || f.width <= 0 || f.height <= 0) return nullptr;
    if ((int64_t)f.width * f.height > 200000000) return nullptr;
    if (orig_w) *orig_w = f.width;
    if (orig_h) *orig_h = f.height;
    if (f.has_animation) {
        WebPData data = { d, n };
        WebPAnimDecoderOptions o;
        if (!WebPAnimDecoderOptionsInit(&o)) return nullptr;
        o.color_mode = MODE_BGRA;   // 0xAARRGGBB little-endian, straight alpha
        WebPAnimDecoder* ad = WebPAnimDecoderNew(&data, &o);
        if (!ad) return nullptr;
        WebPAnimInfo ai;
        uint8_t* buf = nullptr; int ts = 0;
        Bitmap* b = nullptr;
        if (WebPAnimDecoderGetInfo(ad, &ai) && WebPAnimDecoderGetNext(ad, &buf, &ts) && buf &&
            (b = create_bitmap((int32_t)ai.canvas_width, (int32_t)ai.canvas_height)))
            for (uint32_t y = 0; y < ai.canvas_height; y++)
                memcpy(b->row((int32_t)y), buf + (size_t)y * ai.canvas_width * 4, (size_t)ai.canvas_width * 4);
        WebPAnimDecoderDelete(ad);
        return b;
    }
    int w = f.width, h = f.height;
    if (max_size > 0 && (w > max_size || h > max_size)) fit_size(f.width, f.height, max_size, max_size, false, &w, &h);
    Bitmap* b = create_bitmap(w, h);
    if (!b) return nullptr;
    WebPDecoderConfig c;
    if (!WebPInitDecoderConfig(&c)) { destroy_bitmap(b); return nullptr; }
    if (w != f.width || h != f.height) { c.options.use_scaling = 1; c.options.scaled_width = w; c.options.scaled_height = h; }
    c.output.colorspace = MODE_BGRA;
    c.output.is_external_memory = 1;
    c.output.u.RGBA.rgba = (uint8_t*)b->pixels;
    c.output.u.RGBA.stride = b->stride * 4;
    c.output.u.RGBA.size = (size_t)b->stride * 4 * (size_t)h;
    if (WebPDecode(d, n, &c) != VP8_STATUS_OK) { WebPFreeDecBuffer(&c.output); destroy_bitmap(b); return nullptr; }
    WebPFreeDecBuffer(&c.output);
    if (!f.has_alpha) for (int y = 0; y < h; y++) { uint32_t* r = b->row(y); for (int x = 0; x < w; x++) r[x] |= 0xff000000u; }
    return b;
}
#else
static Bitmap* decode_webp(const uint8_t*, size_t, int, int*, int*) { return nullptr; }
#endif

// ---- any image -------------------------------------------------------------------

static bool is_ico(const uint8_t* d, size_t n)
{
    if (n < 22 || rd16(d) != 0 || (rd16(d + 2) != 1 && rd16(d + 2) != 2)) return false;
    int count = rd16(d + 4);
    if (count < 1 || count > 512) return false;
    uint32_t off = rd32(d + 6 + 12);
    return off >= 6u + 16u * count && off < n;
}

// Header dimensions of a PNG or BMP without decoding it; 0,0 if unknown.
static void header_size(const uint8_t* d, size_t n, int* w, int* h)
{
    *w = *h = 0;
    if (n >= 24 && !memcmp(d, "\x89PNG", 4)) {
        *w = (int)((uint32_t)d[16] << 24 | d[17] << 16 | d[18] << 8 | d[19]);
        *h = (int)((uint32_t)d[20] << 24 | d[21] << 16 | d[22] << 8 | d[23]);
    } else if (n >= 26 && d[0] == 'B' && d[1] == 'M') {
        *w = (int)(d[18] | d[19] << 8 | d[20] << 16 | d[21] << 24);
        *h = (int)(d[22] | d[23] << 8 | d[24] << 16 | d[25] << 24);
        if (*h < 0) *h = -*h;
    }
}

// Images with more pixels than this are not decoded for thumbnails (max_size > 0):
// PNG and BMP decode at full size, so a handful of huge files on the worker
// threads could otherwise eat all memory. JPEG scales while decoding and is exempt.
static const int64_t thumb_max_pixels = 40LL * 1000 * 1000;

Bitmap* decode_image(const uint8_t* d, size_t n, int max_size, int* orig_w, int* orig_h)
{
    Bitmap* b = nullptr;
    if (n < 8) return nullptr;
    if (d[0] == 0xff && d[1] == 0xd8) return decode_jpeg(d, n, max_size, orig_w, orig_h);
    if (is_webp(d, n)) return decode_webp(d, n, max_size, orig_w, orig_h);   // scales while decoding
    if (max_size > 0) {
        int hw, hh;
        header_size(d, n, &hw, &hh);
        if ((int64_t)hw * hh > thumb_max_pixels) {
            if (orig_w) *orig_w = hw;
            if (orig_h) *orig_h = hh;
            return nullptr;
        }
    }
    if (!memcmp(d, "\x89PNG", 4)) b = decode_png(d, n);
    else if (d[0] == 'B' && d[1] == 'M') b = decode_bmp(d, n);
    else if (is_ico(d, n)) b = decode_ico(d, n);
    if (b) {
        if (orig_w) *orig_w = b->width;
        if (orig_h) *orig_h = b->height;
    }
    return b;
}

Bitmap* load_image(const std::string& path, int max_size, int* orig_w, int* orig_h)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> buf;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz > 0 && sz < (1L << 30)) buf.resize((size_t)sz);
        fseek(f, 0, SEEK_SET);
    }
    size_t got = buf.empty() ? 0 : fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (got < 8) return nullptr;
    return decode_image(buf.data(), got, max_size, orig_w, orig_h);
}

// ---- resampling -------------------------------------------------------------------

void fit_size(int w, int h, int max_w, int max_h, bool grow, int* ow, int* oh)
{
    if (w <= 0 || h <= 0) { *ow = *oh = 0; return; }
    if (!grow && w <= max_w && h <= max_h) { *ow = w; *oh = h; return; }
    // the axis that limits decides; the other rounds to nearest
    if ((int64_t)w * max_h >= (int64_t)h * max_w) {
        *ow = max_w;
        *oh = (int)(((int64_t)h * max_w * 2 + w) / (2 * (int64_t)w));
    } else {
        *oh = max_h;
        *ow = (int)(((int64_t)w * max_h * 2 + h) / (2 * (int64_t)h));
    }
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
}

struct Tap { int i; float w; };

static std::vector<std::vector<Tap>> taps(int dn, int sn)
{
    std::vector<std::vector<Tap>> t((size_t)dn);
    double scale = (double)sn / dn;
    for (int d = 0; d < dn; d++) {
        if (scale > 1.0) {
            double a = d * scale, b = a + scale;
            for (int i = (int)a; i < (int)b + 1 && i < sn; i++) {
                double lo = i > a ? i : a, hi = i + 1 < b ? i + 1 : b;
                if (hi > lo) t[(size_t)d].push_back({ i, (float)((hi - lo) / scale) });
            }
        } else {
            double c = (d + 0.5) * scale - 0.5;
            int i0 = c < 0 ? 0 : (int)c;
            double f = c < 0 ? 0 : c - i0;
            int i1 = i0 + 1 < sn ? i0 + 1 : i0;
            t[(size_t)d].push_back({ i0, (float)(1 - f) });
            if (f > 0) t[(size_t)d].push_back({ i1, (float)f });
        }
    }
    return t;
}

Bitmap* resample(const Bitmap* src, int dw, int dh)
{
    if (!src || dw <= 0 || dh <= 0) return nullptr;
    Bitmap* out = create_bitmap(dw, dh);
    if (!out) return nullptr;
    int sw = src->width, sh = src->height;
    auto tx = taps(dw, sw), ty = taps(dh, sh);
    // horizontal pass into premultiplied floats (sh rows of dw), then vertical
    std::vector<float> mid((size_t)sh * dw * 4);
    for (int y = 0; y < sh; y++) {
        const uint32_t* s = src->row(y);
        float* m = &mid[(size_t)y * dw * 4];
        for (int x = 0; x < dw; x++) {
            float a = 0, r = 0, g = 0, b = 0;
            for (const Tap& t : tx[(size_t)x]) {
                uint32_t p = s[t.i];
                float pa = (float)(p >> 24) * t.w;
                a += pa;
                r += pa * (float)((p >> 16) & 255);
                g += pa * (float)((p >> 8) & 255);
                b += pa * (float)(p & 255);
            }
            m[x * 4] = a; m[x * 4 + 1] = r; m[x * 4 + 2] = g; m[x * 4 + 3] = b;
        }
    }
    for (int y = 0; y < dh; y++) {
        uint32_t* d = out->row(y);
        for (int x = 0; x < dw; x++) {
            float a = 0, r = 0, g = 0, b = 0;
            for (const Tap& t : ty[(size_t)y]) {
                const float* m = &mid[((size_t)t.i * dw + x) * 4];
                a += m[0] * t.w; r += m[1] * t.w; g += m[2] * t.w; b += m[3] * t.w;
            }
            if (a <= 0.001f) { d[x] = 0; continue; }
            auto c8 = [](float v) -> uint32_t { int i = (int)(v + 0.5f); return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i); };
            d[x] = c8(a) << 24 | c8(r / a) << 16 | c8(g / a) << 8 | c8(b / a);
        }
    }
    return out;
}

} // namespace gdi
