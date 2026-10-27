#include <stdio.h>
#include <string.h>
#include "bmp.h"

namespace gdi {

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline bool fits(size_t size, uint64_t off, uint64_t len) { return off <= size && len <= size - off; }

enum { BI_RGB = 0, BI_RLE8 = 1, BI_RLE4 = 2, BI_BITFIELDS = 3, BI_ALPHABITFIELDS = 6 };

struct Channel { uint32_t mask; int shift, bits; };

static Channel channel(uint32_t mask)
{
    Channel c{mask, 0, 0};
    if (!mask) return c;
    while (!(mask & 1)) { mask >>= 1; c.shift++; }
    while (mask & 1) { mask >>= 1; c.bits++; }
    return c;
}

static inline uint32_t extract(const Channel& c, uint32_t v, uint32_t none)
{
    if (!c.bits) return none;
    v = (v & c.mask) >> c.shift;
    if (c.bits >= 8) return v >> (c.bits - 8);
    return v * 255 / ((1u << c.bits) - 1);
}

// BI_RLE8 / BI_RLE4 (bottom-up): runs, escapes 0 = end of line, 1 = end of
// bitmap, 2 = delta (dx, dy), n >= 3 = n literal pixels (padded to words).
// Pixels never written stay transparent, as Windows leaves them untouched.
static Bitmap* decode_rle(const uint8_t* p, size_t n, int32_t w, int32_t h, int bpp, const uint32_t* pal)
{
    Bitmap* bm = create_bitmap(w, h);
    if (!bm) return nullptr;
    for (int32_t y = 0; y < h; y++) memset(bm->row(y), 0, (size_t)w * 4);
    int32_t x = 0, y = 0;   // y counts from the bottom
    size_t i = 0;
    auto put = [&](uint32_t idx) { if (x < w && y < h) bm->row(h - 1 - y)[x] = pal[idx & 255]; x++; };
    while (i + 1 < n && y < h) {
        uint8_t a = p[i], b = p[i + 1];
        i += 2;
        if (a) {   // a run of a pixels
            for (int k = 0; k < a; k++) put(bpp == 8 ? b : (k & 1) ? b & 15 : b >> 4);
            continue;
        }
        if (b == 0) { x = 0; y++; continue; }
        if (b == 1) break;
        if (b == 2) {
            if (i + 1 >= n) break;
            x += p[i]; y += p[i + 1];
            i += 2;
            continue;
        }
        size_t bytes = bpp == 8 ? b : (b + 1u) / 2;
        if (i + bytes > n) break;
        for (int k = 0; k < b; k++) put(bpp == 8 ? p[i + k] : (k & 1) ? p[i + k / 2] & 15 : p[i + k / 2] >> 4);
        i += (bytes + 1) & ~(size_t)1;
    }
    return bm;
}

Bitmap* decode_bmp(const uint8_t* d, size_t size)
{
    const uint8_t* base = d;          // start of the DIB (info header)
    size_t avail = size;
    int64_t bits_off = -1;            // pixel data offset relative to d, if a file header says so
    if (size >= 14 && d[0] == 'B' && d[1] == 'M') {
        bits_off = rd32(d + 10);
        base = d + 14;
        avail = size - 14;
    }
    if (avail < 12) return nullptr;
    uint32_t hsize = rd32(base);
    int32_t w, h;
    uint16_t bpp;
    uint32_t comp = BI_RGB, clr_used = 0;
    bool core = false;
    uint32_t masks[4] = {0, 0, 0, 0};
    size_t after;                     // offset in base after header (+ separate masks)
    if (hsize == 12) {                // BITMAPCOREHEADER
        core = true;
        w = rd16(base + 4); h = (int16_t)rd16(base + 6); bpp = rd16(base + 10);
        after = 12;
    } else if (hsize >= 40 && fits(avail, 0, hsize)) {
        w = (int32_t)rd32(base + 4); h = (int32_t)rd32(base + 8);
        bpp = rd16(base + 14); comp = rd32(base + 16); clr_used = rd32(base + 32);
        after = hsize;
        if (comp == BI_BITFIELDS || comp == BI_ALPHABITFIELDS) {
            int n = comp == BI_ALPHABITFIELDS ? 4 : 3;
            if (hsize >= 52) {         // V2+ headers carry the masks inline
                for (int i = 0; i < 3; i++) masks[i] = rd32(base + 40 + i * 4);
                if (hsize >= 56) masks[3] = rd32(base + 52);
            } else {
                if (!fits(avail, after, n * 4)) return nullptr;
                for (int i = 0; i < n; i++) masks[i] = rd32(base + after + i * 4);
                after += n * 4;
            }
        }
    } else
        return nullptr;

    bool rle = (comp == BI_RLE8 && bpp == 8) || (comp == BI_RLE4 && bpp == 4);
    if (comp != BI_RGB && comp != BI_BITFIELDS && comp != BI_ALPHABITFIELDS && !rle) return nullptr;
    if ((comp == BI_BITFIELDS || comp == BI_ALPHABITFIELDS) && bpp != 16 && bpp != 32) return nullptr;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return nullptr;
    bool top_down = h < 0;
    if (h == INT32_MIN) return nullptr;
    if (top_down) h = -h;
    if (w <= 0 || h <= 0 || w > 65536 || h > 65536) return nullptr;

    // palette
    uint32_t ncolors = clr_used;
    if (bpp <= 8 && (ncolors == 0 || ncolors > (1u << bpp))) ncolors = 1u << bpp;
    if (core) ncolors = bpp <= 8 ? 1u << bpp : 0;
    if (ncolors > 65536) return nullptr;
    size_t entry = core ? 3 : 4;
    uint32_t pal[256];
    size_t pal_bytes = (size_t)ncolors * entry;
    if (bpp <= 8) {
        // Some writers truncate the palette; accept what is there if the bits still fit.
        for (uint32_t i = 0; i < 256; i++) pal[i] = 0xff000000u;
        for (uint32_t i = 0; i < ncolors; i++) {
            if (!fits(avail, after + i * entry, entry)) break;
            const uint8_t* p = base + after + i * entry;
            pal[i] = 0xff000000u | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
        }
    }

    if (rle) {
        if (top_down) return nullptr;   // RLE bitmaps are bottom-up by definition
        const uint8_t* src = bits_off >= 14 && (uint64_t)bits_off < size ? d + bits_off : base + after + pal_bytes;
        if (src < d || src >= d + size) return nullptr;
        return decode_rle(src, (size_t)(d + size - src), w, h, bpp, pal);
    }

    uint64_t stride = (((uint64_t)w * bpp + 31) / 32) * 4;
    uint64_t need = stride * (uint64_t)h;
    const uint8_t* bits;
    if (bits_off >= 14 && fits(size, (uint64_t)bits_off, need))
        bits = d + bits_off;
    else if (fits(avail, after + pal_bytes, need))
        bits = base + after + pal_bytes;
    else
        return nullptr;

    Channel cr{}, cg{}, cb{}, ca{};
    if (comp == BI_RGB) {
        if (bpp == 16) { cr = channel(0x7c00); cg = channel(0x03e0); cb = channel(0x001f); ca = channel(0); }
    } else {
        cr = channel(masks[0]); cg = channel(masks[1]); cb = channel(masks[2]); ca = channel(masks[3]);
    }

    Bitmap* bm = create_bitmap(w, h);
    for (int32_t y = 0; y < h; y++) {
        const uint8_t* s = bits + stride * (uint64_t)(top_down ? y : h - 1 - y);
        uint32_t* o = bm->row(y);
        switch (bpp) {
        case 1:
            for (int32_t x = 0; x < w; x++) o[x] = pal[s[x >> 3] >> (7 - (x & 7)) & 1];
            break;
        case 4:
            for (int32_t x = 0; x < w; x++) o[x] = pal[(x & 1) ? s[x >> 1] & 15 : s[x >> 1] >> 4];
            break;
        case 8:
            for (int32_t x = 0; x < w; x++) o[x] = pal[s[x]];
            break;
        case 16:
            for (int32_t x = 0; x < w; x++) {
                uint32_t v = rd16(s + x * 2);
                o[x] = extract(ca, v, 255) << 24 | extract(cr, v, 0) << 16 | extract(cg, v, 0) << 8 | extract(cb, v, 0);
            }
            break;
        case 24:
            for (int32_t x = 0; x < w; x++, s += 3)
                o[x] = 0xff000000u | (uint32_t)s[2] << 16 | (uint32_t)s[1] << 8 | s[0];
            break;
        case 32:
            if (comp == BI_RGB)
                memcpy(o, s, (size_t)w * 4);     // BGRA in memory == 0xAARRGGBB on little-endian
            else
                for (int32_t x = 0; x < w; x++) {
                    uint32_t v = rd32(s + x * 4);
                    o[x] = extract(ca, v, 255) << 24 | extract(cr, v, 0) << 16 | extract(cg, v, 0) << 8 | extract(cb, v, 0);
                }
            break;
        }
    }
    return bm;
}

bool write_ppm(const Bitmap* b, const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", b->width, b->height);
    for (int32_t y = 0; y < b->height; y++) {
        const uint32_t* r = b->row(y);
        for (int32_t x = 0; x < b->width; x++) {
            uint8_t px[3] = {(uint8_t)(r[x] >> 16), (uint8_t)(r[x] >> 8), (uint8_t)r[x]};
            fwrite(px, 1, 3, f);
        }
    }
    return fclose(f) == 0;
}

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

bool write_bmp(const Bitmap* b, const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    uint32_t img = (uint32_t)b->width * b->height * 4;
    uint8_t h[54];
    memset(h, 0, sizeof h);
    h[0] = 'B'; h[1] = 'M';
    put32(h + 2, 54 + img);
    put32(h + 10, 54);
    put32(h + 14, 40);
    put32(h + 18, (uint32_t)b->width);
    put32(h + 22, (uint32_t)b->height);
    put16(h + 26, 1);
    put16(h + 28, 32);
    put32(h + 34, img);
    put32(h + 38, 3780); put32(h + 42, 3780);    // 96 dpi
    fwrite(h, 1, sizeof h, f);
    for (int32_t y = b->height - 1; y >= 0; y--) {
        const uint32_t* r = b->row(y);
        for (int32_t x = 0; x < b->width; x++) {
            uint8_t px[4];
            put32(px, r[x]);
            fwrite(px, 1, 4, f);
        }
    }
    return fclose(f) == 0;
}

} // namespace gdi
