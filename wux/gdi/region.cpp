#include "region.h"

namespace gdi {

// Append the pieces of e that lie outside r (at most four) to out.
static void split(const Rect& e, const Rect& r, std::vector<Rect>& out)
{
    if (!gdi::overlaps(e, r)) { out.push_back(e); return; }
    if (r.top > e.top)       out.push_back(Rect{e.left, e.top, e.right, r.top});
    if (r.bottom < e.bottom) out.push_back(Rect{e.left, r.bottom, e.right, e.bottom});
    int32_t y0 = r.top > e.top ? r.top : e.top;
    int32_t y1 = r.bottom < e.bottom ? r.bottom : e.bottom;
    if (r.left > e.left)     out.push_back(Rect{e.left, y0, r.left, y1});
    if (r.right < e.right)   out.push_back(Rect{r.right, y0, e.right, y1});
}

// Merge rectangles that share an edge exactly. One pass, cheap.
static void coalesce(std::vector<Rect>& v)
{
    bool again = true;
    while (again) {
        again = false;
        for (size_t i = 0; i < v.size() && !again; i++) {
            for (size_t j = i + 1; j < v.size(); j++) {
                Rect& a = v[i]; Rect& b = v[j];
                bool merged = false;
                if (a.left == b.left && a.right == b.right) {
                    if (a.bottom == b.top) { a.bottom = b.bottom; merged = true; }
                    else if (b.bottom == a.top) { a.top = b.top; merged = true; }
                } else if (a.top == b.top && a.bottom == b.bottom) {
                    if (a.right == b.left) { a.right = b.right; merged = true; }
                    else if (b.right == a.left) { a.left = b.left; merged = true; }
                }
                if (merged) { v.erase(v.begin() + j); again = true; break; }
            }
        }
    }
}

void Region::set(const Rect& r)
{
    rects_.clear();
    if (!r.empty()) rects_.push_back(r);
}

void Region::add(const Rect& r)
{
    if (r.empty()) return;
    std::vector<Rect> out;
    out.reserve(rects_.size() + 4);
    for (const Rect& e : rects_) split(e, r, out);
    out.push_back(r);
    rects_.swap(out);
    coalesce(rects_);
}

void Region::subtract(const Rect& r)
{
    if (r.empty() || rects_.empty()) return;
    std::vector<Rect> out;
    out.reserve(rects_.size() + 4);
    for (const Rect& e : rects_) split(e, r, out);
    rects_.swap(out);
    coalesce(rects_);
}

void Region::intersect(const Rect& r)
{
    std::vector<Rect> out;
    for (const Rect& e : rects_) {
        Rect i = gdi::intersect(e, r);
        if (!i.empty()) out.push_back(i);
    }
    rects_.swap(out);
}

void Region::add(const Region& o)
{
    for (const Rect& r : o.rects_) add(r);
}

void Region::subtract(const Region& o)
{
    for (const Rect& r : o.rects_) subtract(r);
}

void Region::intersect(const Region& o)
{
    std::vector<Rect> out;
    for (const Rect& a : rects_)
        for (const Rect& b : o.rects_) {
            Rect i = gdi::intersect(a, b);
            if (!i.empty()) out.push_back(i);
        }
    rects_.swap(out);
    coalesce(rects_);
}

void Region::offset(int32_t dx, int32_t dy)
{
    for (Rect& r : rects_) r = gdi::offset(r, dx, dy);
}

Rect Region::bounds() const
{
    Rect b = empty_rect();
    for (const Rect& r : rects_) b = unite(b, r);
    return b;
}

bool Region::contains(Point p) const
{
    for (const Rect& r : rects_) if (r.contains(p)) return true;
    return false;
}

bool Region::overlaps(const Rect& q) const
{
    for (const Rect& r : rects_) if (gdi::overlaps(r, q)) return true;
    return false;
}

} // namespace gdi
