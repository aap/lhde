// A region is a set of pairwise disjoint rectangles. Not minimal, but correct;
// good enough for clipping and update regions until it shows up in a profile.
#pragma once
#include <vector>
#include "geom.h"

namespace gdi {

class Region {
public:
    Region() = default;
    explicit Region(const Rect& r) { set(r); }

    void clear() { rects_.clear(); }
    void set(const Rect& r);
    void add(const Rect& r);          // union
    void subtract(const Rect& r);
    void intersect(const Rect& r);
    void add(const Region& o);
    void subtract(const Region& o);
    void intersect(const Region& o);
    void offset(int32_t dx, int32_t dy);

    bool empty() const { return rects_.empty(); }
    Rect bounds() const;
    bool contains(Point p) const;
    bool overlaps(const Rect& r) const;
    const std::vector<Rect>& rects() const { return rects_; }

private:
    std::vector<Rect> rects_;
};

} // namespace gdi
