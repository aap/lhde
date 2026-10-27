#include <vector>
#include "dc.h"

namespace gdi {

DC* create_dc(Bitmap* target)
{
    DC* dc = new DC;
    dc->target = target;
    dc->origin = Point{0, 0};
    dc->clip.set(target->rect());
    dc->clip_box = target->rect();
    return dc;
}

void destroy_dc(DC* dc) { delete dc; }

void set_clip(DC* dc, const Region& r)
{
    dc->clip = r;
    dc->clip.intersect(dc->target->rect());
    dc->clip_box = dc->clip.bounds();
}

void set_origin(DC* dc, Point o) { dc->origin = o; }

Rect clip_box(const DC* dc)
{
    return offset(dc->clip_box, -dc->origin.x, -dc->origin.y);
}

bool rect_visible(const DC* dc, const Rect& r)
{
    return dc->clip.overlaps(offset(r, dc->origin.x, dc->origin.y));
}

static inline void fill_span(uint32_t* p, int32_t n, uint32_t v)
{
    while (n-- > 0) *p++ = v;
}

void fill_rect(DC* dc, const Rect& lr, Color c)
{
    Rect r = offset(lr, dc->origin.x, dc->origin.y);
    if (!overlaps(r, dc->clip_box)) return;
    uint32_t v = pack(c);
    for (const Rect& cr : dc->clip.rects()) {
        Rect i = intersect(r, cr);
        if (i.empty()) continue;
        for (int32_t y = i.top; y < i.bottom; y++)
            fill_span(dc->target->row(y) + i.left, i.width(), v);
    }
}

void clear_rect(DC* dc, const Rect& r)
{
    fill_rect(dc, r, rgb(0, 0, 0, 0));
}

void hline(DC* dc, int32_t x0, int32_t x1, int32_t y, Color c)
{
    fill_rect(dc, Rect{x0, y, x1, y + 1}, c);
}

void vline(DC* dc, int32_t x, int32_t y0, int32_t y1, Color c)
{
    fill_rect(dc, Rect{x, y0, x + 1, y1}, c);
}

void frame_rect(DC* dc, const Rect& r, Color c)
{
    if (r.empty()) return;
    hline(dc, r.left, r.right, r.top, c);
    hline(dc, r.left, r.right, r.bottom - 1, c);
    vline(dc, r.left, r.top + 1, r.bottom - 1, c);
    vline(dc, r.right - 1, r.top + 1, r.bottom - 1, c);
}

void set_pixel(DC* dc, Point lp, Color c)
{
    Point p = lp + dc->origin;
    if (!dc->clip_box.contains(p) || !dc->clip.contains(p)) return;
    dc->target->row(p.y)[p.x] = pack(c);
}

Color get_pixel(const DC* dc, Point lp)
{
    Point p = lp + dc->origin;
    if (!dc->target->rect().contains(p)) return rgb(0, 0, 0, 0);
    return unpack(dc->target->row(p.y)[p.x]);
}

void line(DC* dc, Point a, Point b, Color c)
{
    if (a.y == b.y) {
        if (a.x < b.x) hline(dc, a.x, b.x, a.y, c); else hline(dc, b.x + 1, a.x + 1, a.y, c);
        return;
    }
    if (a.x == b.x) {
        if (a.y < b.y) vline(dc, a.x, a.y, b.y, c); else vline(dc, a.x, b.y + 1, a.y + 1, c);
        return;
    }
    // Bresenham, endpoint excluded.
    int32_t dx = b.x > a.x ? b.x - a.x : a.x - b.x;
    int32_t dy = b.y > a.y ? b.y - a.y : a.y - b.y;
    int32_t sx = b.x > a.x ? 1 : -1, sy = b.y > a.y ? 1 : -1;
    int32_t err = dx - dy;
    Point p = a;
    while (p.x != b.x || p.y != b.y) {
        set_pixel(dc, p, c);
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; p.x += sx; }
        if (e2 < dx)  { err += dx; p.y += sy; }
    }
}

// Clip a copy of src_rect at dst against the clip region and call fn for each
// visible device rectangle, with the matching source offset.
template <class F>
static void for_each_clipped(DC* dc, Point ldst, const Bitmap* src, Rect sr, F fn)
{
    sr = intersect(sr, src->rect());
    if (sr.empty()) return;
    Point dst = ldst + dc->origin;
    Rect dr = rect_xywh(dst.x, dst.y, sr.width(), sr.height());
    if (!overlaps(dr, dc->clip_box)) return;
    for (const Rect& cr : dc->clip.rects()) {
        Rect i = intersect(dr, cr);
        if (i.empty()) continue;
        fn(i, Point{sr.left + (i.left - dr.left), sr.top + (i.top - dr.top)});
    }
}

void blit(DC* dc, Point dst, const Bitmap* src, const Rect& src_rect)
{
    for_each_clipped(dc, dst, src, src_rect, [&](const Rect& i, Point s) {
        for (int32_t y = 0; y < i.height(); y++) {
            const uint32_t* sp = src->row(s.y + y) + s.x;
            uint32_t* dp = dc->target->row(i.top + y) + i.left;
            for (int32_t x = 0; x < i.width(); x++) dp[x] = sp[x];
        }
    });
}

static inline uint32_t blend(uint32_t d, uint32_t s, uint32_t alpha)
{
    // s is treated as non-premultiplied; alpha is the combined 0..255 factor
    if (alpha == 0) return d;
    if (alpha == 255) return (s & 0x00ffffff) | 0xff000000;
    uint32_t inv = 255 - alpha;
    uint32_t rb = (((s & 0x00ff00ff) * alpha) + ((d & 0x00ff00ff) * inv)) >> 8;
    uint32_t g  = (((s & 0x0000ff00) * alpha) + ((d & 0x0000ff00) * inv)) >> 8;
    return (rb & 0x00ff00ff) | (g & 0x0000ff00) | 0xff000000;
}

static inline uint32_t div255(uint32_t v) { v += 128; return (v + (v >> 8)) >> 8; }

// src-over onto a premultiplied surface: s is a non-premultiplied colour,
// alpha its coverage; colour and destination alpha both blend.
static inline uint32_t blend_over(uint32_t d, uint32_t s, uint32_t alpha)
{
    if (alpha == 0) return d;
    if (alpha == 255) return (s & 0x00ffffff) | 0xff000000;
    uint32_t inv = 255 - alpha;
    uint32_t b = div255((s & 0xff) * alpha + (d & 0xff) * inv);
    uint32_t g = div255((s >> 8 & 0xff) * alpha + (d >> 8 & 0xff) * inv);
    uint32_t r = div255((s >> 16 & 0xff) * alpha + (d >> 16 & 0xff) * inv);
    uint32_t a = alpha + div255((d >> 24) * inv);
    return a << 24 | r << 16 | g << 8 | b;
}

void blit_alpha(DC* dc, Point dst, const Bitmap* src, const Rect& src_rect, uint8_t alpha)
{
    if (dc->target->has_alpha) {
        for_each_clipped(dc, dst, src, src_rect, [&](const Rect& i, Point s) {
            for (int32_t y = 0; y < i.height(); y++) {
                const uint32_t* sp = src->row(s.y + y) + s.x;
                uint32_t* dp = dc->target->row(i.top + y) + i.left;
                for (int32_t x = 0; x < i.width(); x++)
                    dp[x] = blend_over(dp[x], sp[x], (sp[x] >> 24) * alpha / 255);
            }
        });
        return;
    }
    for_each_clipped(dc, dst, src, src_rect, [&](const Rect& i, Point s) {
        for (int32_t y = 0; y < i.height(); y++) {
            const uint32_t* sp = src->row(s.y + y) + s.x;
            uint32_t* dp = dc->target->row(i.top + y) + i.left;
            for (int32_t x = 0; x < i.width(); x++) {
                uint32_t a = (sp[x] >> 24) * alpha / 255;
                dp[x] = blend(dp[x], sp[x], a);
            }
        }
    });
}

void stretch_blit(DC* dc, const Rect& ldst, const Bitmap* src, const Rect& src_rect)
{
    Rect sr = intersect(src_rect, src->rect());
    if (sr.empty() || ldst.empty()) return;
    Rect dr = offset(ldst, dc->origin.x, dc->origin.y);
    if (!overlaps(dr, dc->clip_box)) return;
    int32_t dw = dr.width(), dh = dr.height(), sw = sr.width(), sh = sr.height();
    for (const Rect& cr : dc->clip.rects()) {
        Rect i = intersect(dr, cr);
        if (i.empty()) continue;
        for (int32_t y = i.top; y < i.bottom; y++) {
            int32_t sy = sr.top + (int64_t)(y - dr.top) * sh / dh;
            const uint32_t* sp = src->row(sy);
            uint32_t* dp = dc->target->row(y);
            for (int32_t x = i.left; x < i.right; x++) {
                int32_t sx = sr.left + (int64_t)(x - dr.left) * sw / dw;
                dp[x] = sp[sx];
            }
        }
    }
}

void blend_mask(DC* dc, Point lp, const uint8_t* mask, int32_t w, int32_t h, int32_t stride, Color fg)
{
    Point p = lp + dc->origin;
    Rect r = rect_xywh(p.x, p.y, w, h);
    if (r.empty() || !overlaps(r, dc->clip_box)) return;
    uint32_t v = pack(fg);
    bool over = dc->target->has_alpha;
    for (const Rect& cr : dc->clip.rects()) {
        Rect i = intersect(r, cr);
        if (i.empty()) continue;
        for (int32_t y = i.top; y < i.bottom; y++) {
            const uint8_t* mp = mask + (intptr_t)(y - p.y) * stride + (i.left - p.x);
            uint32_t* dp = dc->target->row(y) + i.left;
            if (over) {
                for (int32_t x = 0; x < i.width(); x++)
                    if (mp[x]) dp[x] = blend_over(dp[x], v, mp[x]);
                continue;
            }
            for (int32_t x = 0; x < i.width(); x++)
                if (mp[x]) dp[x] = blend(dp[x], v, mp[x]);
        }
    }
}

// One box-blur pass of radius r along rows (step 1) or columns (step = w).
static void box_pass(std::vector<uint16_t>& a, int32_t w, int32_t h, int32_t r, bool rows)
{
    int32_t n = rows ? w : h, lines = rows ? h : w;
    int32_t step = rows ? 1 : w, lstep = rows ? w : 1;
    std::vector<uint16_t> line((size_t)n);
    for (int32_t l = 0; l < lines; l++) {
        uint16_t* p = a.data() + (intptr_t)l * lstep;
        for (int32_t i = 0; i < n; i++) line[i] = p[(intptr_t)i * step];
        uint32_t sum = 0;
        for (int32_t i = -r; i <= r; i++) if (i >= 0 && i < n) sum += line[i];
        for (int32_t i = 0; i < n; i++) {
            p[(intptr_t)i * step] = (uint16_t)(sum / (uint32_t)(2 * r + 1));
            int32_t out = i - r, in = i + r + 1;
            if (out >= 0) sum -= line[out];
            if (in < n) sum += line[in];
        }
    }
}

void glow_mask(DC* dc, Point pos, const uint8_t* mask, int32_t w, int32_t h, int32_t stride, Color glow, int32_t radius)
{
    if (w <= 0 || h <= 0 || radius <= 0) return;
    // dilate by `grow`, then two box passes of radius `b` (about a gaussian)
    int32_t b = (radius - 1) / 2 > 1 ? (radius - 1) / 2 : 1;
    int32_t grow = radius - 2 * b > 0 ? radius - 2 * b : 0;
    int32_t m = grow + 2 * b, bw = w + 2 * m, bh = h + 2 * m;
    std::vector<uint16_t> a((size_t)bw * bh, 0);
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) {
            uint8_t c = mask[(intptr_t)y * stride + x];
            if (!c) continue;
            for (int32_t dy = -grow; dy <= grow; dy++)
                for (int32_t dx = -grow; dx <= grow; dx++) {
                    uint16_t& t = a[(size_t)(y + m + dy) * bw + (x + m + dx)];
                    if (t < c) t = c;
                }
        }
    for (int pass = 0; pass < 2; pass++) { box_pass(a, bw, bh, b, true); box_pass(a, bw, bh, b, false); }
    std::vector<uint8_t> out((size_t)bw * bh);
    for (size_t i = 0; i < out.size(); i++) {
        uint32_t v = (uint32_t)a[i] * 2 * glow.a / 255;   // gain 2: solid near the glyphs, soft tail
        out[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    Color c = glow; c.a = 255;
    blend_mask(dc, Point{pos.x - m, pos.y - m}, out.data(), bw, bh, bw, c);
}

} // namespace gdi
