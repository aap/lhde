// Basic geometry. Rects are half-open: [left,right) x [top,bottom), as in win32.
#pragma once
#include <stdint.h>

namespace gdi {

struct Point { int32_t x, y; };
struct Size  { int32_t cx, cy; };

struct Rect {
    int32_t left, top, right, bottom;

    int32_t width()  const { return right - left; }
    int32_t height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }
    bool contains(Point p) const {
        return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
    }
    bool operator==(const Rect& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    bool operator!=(const Rect& o) const { return !(*this == o); }
};

inline Rect rect(int32_t l, int32_t t, int32_t r, int32_t b) { return Rect{l, t, r, b}; }
inline Rect rect_xywh(int32_t x, int32_t y, int32_t w, int32_t h) { return Rect{x, y, x + w, y + h}; }
inline Rect empty_rect() { return Rect{0, 0, 0, 0}; }

inline Rect intersect(const Rect& a, const Rect& b) {
    Rect r{ a.left > b.left ? a.left : b.left,
            a.top > b.top ? a.top : b.top,
            a.right < b.right ? a.right : b.right,
            a.bottom < b.bottom ? a.bottom : b.bottom };
    return r.empty() ? empty_rect() : r;
}

inline Rect unite(const Rect& a, const Rect& b) {
    if (a.empty()) return b.empty() ? empty_rect() : b;
    if (b.empty()) return a;
    return Rect{ a.left < b.left ? a.left : b.left,
                 a.top < b.top ? a.top : b.top,
                 a.right > b.right ? a.right : b.right,
                 a.bottom > b.bottom ? a.bottom : b.bottom };
}

inline bool overlaps(const Rect& a, const Rect& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

inline Rect offset(const Rect& r, int32_t dx, int32_t dy) {
    return Rect{r.left + dx, r.top + dy, r.right + dx, r.bottom + dy};
}

inline Rect inflate(const Rect& r, int32_t dx, int32_t dy) {
    return Rect{r.left - dx, r.top - dy, r.right + dx, r.bottom + dy};
}

inline Point operator+(Point a, Point b) { return Point{a.x + b.x, a.y + b.y}; }
inline Point operator-(Point a, Point b) { return Point{a.x - b.x, a.y - b.y}; }

} // namespace gdi
