// Private helpers shared by the GDI shim modules (gdi.cpp, gdi_draw.cpp,
// gdi_text.cpp). Not for the rest of the shim: use w32.h.
#pragma once
#include <stdio.h>
#include <string.h>
#include "w32.h"

#define GDI_FIXME(...) do { static bool once_; if (!once_) { once_ = true; fprintf(stderr, "fixme:gdi:" __VA_ARGS__); } } while (0)

namespace w32 {

constexpr uint32_t T_PAL = 5;             // OBJ_PAL, only the stock DEFAULT_PALETTE exists

HGDIOBJ stock(int i);                     // GetStockObject without the bounds noise
Bitmap* stock_bitmap();                   // the 1x1 mono bitmap of a fresh memory DC

inline bool is_mono(const DC* d) { return d->bitmap && d->bitmap->req_bpp == 1; }

// COLORREF -> pixel as it lands on the DC's surface (mono targets snap to
// black or white). Alpha is 255: the toolkit's surfaces are opaque.
uint32_t dev_pixel(const DC* d, COLORREF c);

// Logical (= gdi logical) rect -> each visible target-coordinate rect.
template <class F> inline void each_clip_rect(DC* d, const gdi::Rect& lr, F fn)
{
    gdi::DC* g = d->dc;
    gdi::Rect r = gdi::offset(lr, g->origin.x, g->origin.y);
    if (r.empty() || !gdi::overlaps(r, g->clip_box)) return;
    for (const gdi::Rect& cr : g->clip.rects()) {
        gdi::Rect i = gdi::intersect(r, cr);
        if (!i.empty()) fn(i);
    }
}

// Binary raster ops on 24-bit colour; alpha comes out 255.
inline uint32_t rop2_apply(int rop2, uint32_t p, uint32_t d)
{
    uint32_t t = (uint32_t)(rop2 - 1) & 15, r = 0;
    if (t & 1) r |= ~p & ~d;
    if (t & 2) r |= ~p & d;
    if (t & 4) r |= p & ~d;
    if (t & 8) r |= p & d;
    return r | 0xff000000;
}

// Brush fill of a logical rect with a binary rop (R2_*).
void fill_brush(DC* d, const gdi::Rect& lr, Brush* b, int rop2);
// Pixel of brush b at target coordinate (x,y), already mapped for the DC.
uint32_t brush_pixel(DC* d, Brush* b, int32_t x, int32_t y);

// Region helpers
int region_type(const gdi::Region& r);    // NULLREGION / SIMPLEREGION / COMPLEXREGION
Region* region_of(HRGN h);

// UTF-16 <-> UTF-8 for the GDI shim ("ANSI" is UTF-8 in this project).
inline std::string to_utf8(const WCHAR* s, int len)
{
    std::string o;
    if (!s) return o;
    if (len < 0) { len = 0; while (s[len]) len++; }
    for (int i = 0; i < len; i++) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < len && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xc0 | c >> 6); o += (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { o += (char)(0xe0 | c >> 12); o += (char)(0x80 | (c >> 6 & 0x3f)); o += (char)(0x80 | (c & 0x3f)); }
        else { o += (char)(0xf0 | c >> 18); o += (char)(0x80 | (c >> 12 & 0x3f)); o += (char)(0x80 | (c >> 6 & 0x3f)); o += (char)(0x80 | (c & 0x3f)); }
    }
    return o;
}

inline std::u16string to_utf16(const char* s, int len)
{
    std::u16string o;
    if (!s) return o;
    if (len < 0) len = (int)strlen(s);
    const uint8_t* p = (const uint8_t*)s;
    const uint8_t* e = p + len;
    while (p < e) {
        uint32_t c = *p++;
        if (c >= 0x80) {
            int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
            if (n < 0 || c >= 0xf8) c = 0xfffd;
            else {
                c &= 0x3f >> n;
                for (int i = 0; i < n; i++) {
                    if (p >= e || (*p & 0xc0) != 0x80) { c = 0xfffd; break; }
                    c = c << 6 | (*p++ & 0x3f);
                }
            }
        }
        if (c >= 0x10000) { c -= 0x10000; o += (char16_t)(0xd800 + (c >> 10)); o += (char16_t)(0xdc00 + (c & 0x3ff)); }
        else o += (char16_t)c;
    }
    return o;
}

} // namespace w32
