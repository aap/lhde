// mupf's page layout and render cache (layout.h): plain C++, no win32, no mupdf.
#include "layout.h"
#include <math.h>
#include <stdlib.h>
#include <algorithm>

namespace mupf {

const double ZOOM_STEPS[] = { 0.0833, 0.125, 0.25, 0.3333, 0.5, 0.6667, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0 };
const int ZOOM_STEP_COUNT = sizeof ZOOM_STEPS / sizeof ZOOM_STEPS[0];

Box intersect(const Box& a, const Box& b)
{
    int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    int x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    Box r;
    if (x1 > x0 && y1 > y0) r = { x0, y0, x1 - x0, y1 - y0 };
    return r;
}

double clamp_zoom(double z)
{
    if (!(z > 0)) return 1;
    return std::min(ZOOM_MAX, std::max(ZOOM_MIN, z));
}

double zoom_in(double z)
{
    for (int i = 0; i < ZOOM_STEP_COUNT; i++)
        if (ZOOM_STEPS[i] > z * 1.001) return ZOOM_STEPS[i];
    return ZOOM_STEPS[ZOOM_STEP_COUNT - 1];
}

double zoom_out(double z)
{
    for (int i = ZOOM_STEP_COUNT - 1; i >= 0; i--)
        if (ZOOM_STEPS[i] < z * 0.999) return ZOOM_STEPS[i];
    return ZOOM_STEPS[0];
}

SizeF rotated(SizeF s, int rot)
{
    if (rot == 90 || rot == 270) std::swap(s.w, s.h);
    return s;
}

void page_to_px(float x, float y, SizeF size, int rot, double zoom, double* px, double* py)
{
    double s = zoom * PX_PER_PT, rx = x, ry = y;
    switch (rot) {   // clockwise in a y-down plane, as mupdf's fz_rotate turns a page
    case 90: rx = size.h - y; ry = x; break;
    case 180: rx = size.w - x; ry = size.h - y; break;
    case 270: rx = y; ry = size.w - x; break;
    }
    *px = rx * s;
    *py = ry * s;
}

void px_to_page(double px, double py, SizeF size, int rot, double zoom, float* x, float* y)
{
    double s = zoom * PX_PER_PT, rx = px / s, ry = py / s;
    switch (rot) {
    case 90: *x = (float)ry; *y = (float)(size.h - rx); break;
    case 180: *x = (float)(size.w - rx); *y = (float)(size.h - ry); break;
    case 270: *x = (float)(size.w - ry); *y = (float)rx; break;
    default: *x = (float)rx; *y = (float)ry; break;
    }
}

void page_px(SizeF size, int rot, double zoom, int* w, int* h)
{
    // rounded up as mupdf's fz_round_rect rounds a page's box, so a page renders exactly as large as its box
    SizeF r = rotated(size, rot);
    *w = std::max(1, (int)ceil(r.w * zoom * PX_PER_PT - 0.001));
    *h = std::max(1, (int)ceil(r.h * zoom * PX_PER_PT - 0.001));
}

int spread_count(int pages, Mode m, bool cover)
{
    if (pages <= 0) return 0;
    if (!is_facing(m)) return pages;
    return cover ? 1 + pages / 2 : (pages + 1) / 2;
}

int spread_of(int page, Mode m, bool cover)
{
    if (page < 0) return 0;
    if (!is_facing(m)) return page;
    return cover ? (page + 1) / 2 : page / 2;
}

void spread_pages(int spread, int pages, Mode m, bool cover, int* left, int* right)
{
    *left = *right = -1;
    if (spread < 0) return;
    if (!is_facing(m)) { if (spread < pages) *left = spread; return; }
    int a = cover ? spread * 2 - 1 : spread * 2;   // the left page (cover: spread 0's left is page -1, none)
    if (a >= 0 && a < pages) *left = a;
    if (a + 1 >= 0 && a + 1 < pages) *right = a + 1;
}

namespace {

SizeF size_of(const std::vector<SizeF>& sizes, int i, int rot)
{
    if (i < 0 || i >= (int)sizes.size()) return SizeF();
    return rotated(sizes[i], rot);
}

// a spread's width and height in pixels at zoom: a facing spread is two halves as wide as its wider page, so the fold
// stays in the middle of the plane
void spread_size(const std::vector<SizeF>& sizes, int s, int rot, double zoom, Mode m, bool cover, int* w, int* h)
{
    int l, r;
    spread_pages(s, (int)sizes.size(), m, cover, &l, &r);
    int lw = 0, lh = 0, rw = 0, rh = 0;
    if (l >= 0) page_px(sizes[l], rot, zoom, &lw, &lh);
    if (r >= 0) page_px(sizes[r], rot, zoom, &rw, &rh);
    if (is_facing(m)) *w = 2 * std::max(lw, rw) + PAGE_GAP;
    else *w = lw;
    *h = std::max(lh, rh);
}

}   // namespace

Layout make_layout(const std::vector<SizeF>& sizes, int rot, double zoom, Mode m, bool cover, int current, int view_w,
                   int view_h)
{
    Layout l;
    const int margin = PLANE_MARGIN;
    l.mode = m;
    l.cover = cover;
    l.rot = rot;
    l.zoom = zoom;
    int n = (int)sizes.size();
    l.page.assign(n, Box());
    if (n == 0) { l.width = std::max(0, view_w); l.height = std::max(0, view_h); return l; }
    if (current < 0) current = 0;
    if (current >= n) current = n - 1;
    int spreads = spread_count(n, m, cover);
    int first = 0, last = spreads - 1;
    if (!is_continuous(m)) first = last = l.shown_spread = spread_of(current, m, cover);
    int widest = 0;
    for (int s = first; s <= last; s++) {
        int w, h;
        spread_size(sizes, s, rot, zoom, m, cover, &w, &h);
        widest = std::max(widest, w);
    }
    l.width = std::max(view_w, widest + 2 * margin);
    int cx = l.width / 2;
    int y = margin;
    for (int s = first; s <= last; s++) {
        int w, h;
        spread_size(sizes, s, rot, zoom, m, cover, &w, &h);
        int a, b;
        spread_pages(s, n, m, cover, &a, &b);
        if (!is_facing(m)) {
            int pw, ph;
            page_px(sizes[a], rot, zoom, &pw, &ph);
            l.page[a] = { cx - pw / 2, y, pw, ph };
        } else {
            // the left page against the fold from the left, the right one from the right; each centred in the row's height
            int half = PAGE_GAP / 2;
            if (a >= 0) {
                int pw, ph;
                page_px(sizes[a], rot, zoom, &pw, &ph);
                l.page[a] = { cx - half - pw, y + (h - ph) / 2, pw, ph };
            }
            if (b >= 0) {
                int pw, ph;
                page_px(sizes[b], rot, zoom, &pw, &ph);
                l.page[b] = { cx + (PAGE_GAP - half), y + (h - ph) / 2, pw, ph };
            }
        }
        y += h + (s < last ? PAGE_GAP : 0);
    }
    l.height = std::max(view_h, y + margin);
    // a spread shown alone and shorter than the view: in the middle of it, as mupdf shows a page
    int down = l.height - (y + margin);
    if (!is_continuous(m) && down > 1)
        for (Box& b : l.page)
            if (!b.empty()) b.y += down / 2;
    return l;
}

double fit_width(const std::vector<SizeF>& sizes, int rot, Mode m, bool cover, int current, int view_w)
{
    int n = (int)sizes.size();
    if (n == 0) return 1;
    int a, b;
    spread_pages(spread_of(std::min(std::max(current, 0), n - 1), m, cover), n, m, cover, &a, &b);
    double w = is_facing(m) ? 2 * std::max(size_of(sizes, a, rot).w, size_of(sizes, b, rot).w) : size_of(sizes, a, rot).w;
    // a pixel a page to spare: page_px rounds up
    double avail = view_w - 2 * PLANE_MARGIN - (is_facing(m) ? PAGE_GAP + 2 : 1);
    if (w <= 0 || avail <= 0) return 1;
    return clamp_zoom(avail / (w * PX_PER_PT));
}

double fit_page(const std::vector<SizeF>& sizes, int rot, Mode m, bool cover, int current, int view_w, int view_h)
{
    int n = (int)sizes.size();
    if (n == 0) return 1;
    int a, b;
    spread_pages(spread_of(std::min(std::max(current, 0), n - 1), m, cover), n, m, cover, &a, &b);
    double h = std::max(size_of(sizes, a, rot).h, size_of(sizes, b, rot).h);
    double avail = view_h - 2 * PLANE_MARGIN;
    double zw = fit_width(sizes, rot, m, cover, current, view_w);
    if (h <= 0 || avail <= 0) return zw;
    return clamp_zoom(std::min(zw, avail / (h * PX_PER_PT)));
}

int current_page(const Layout& l, int left, int top, int view_w, int view_h)
{
    int best = -1, best_h = 0, nearest = -1;
    long nearest_d = 0;
    for (int i = 0; i < (int)l.page.size(); i++) {
        const Box& b = l.page[i];
        if (b.empty()) continue;
        int y0 = std::max(b.y, top), y1 = std::min(b.y + b.h, top + view_h);
        int x0 = std::max(b.x, left), x1 = std::min(b.x + b.w, left + view_w);
        if (y1 > y0 && x1 > x0 && y1 - y0 > best_h) { best = i; best_h = y1 - y0; }
        long d = b.y + b.h < top ? top - (b.y + b.h) : b.y > top + view_h ? b.y - (top + view_h) : 0;
        if (nearest < 0 || d < nearest_d) { nearest = i; nearest_d = d; }
    }
    return best >= 0 ? best : nearest;
}

std::vector<int> pages_in_view(const Layout& l, int left, int top, int view_w, int view_h)
{
    std::vector<int> out;
    Box v = { left, top, view_w, view_h };
    for (int i = 0; i < (int)l.page.size(); i++)
        if (!l.page[i].empty() && !intersect(l.page[i], v).empty()) out.push_back(i);
    return out;
}

Box render_part(const Box& page_box, int left, int top, int view_w, int view_h)
{
    Box whole = { 0, 0, page_box.w, page_box.h };
    if ((size_t)page_box.w * page_box.h * 4 <= WHOLE_TILE_MAX) return whole;
    // the view with half a view around it, in the page's own pixels, on a 256 px grid so small scrolls ask for the same part
    Box near = { left - view_w / 2 - page_box.x, top - view_h / 2 - page_box.y, view_w * 2, view_h * 2 };
    int x0 = std::max(0, near.x) & ~255, y0 = std::max(0, near.y) & ~255;
    int x1 = std::min(page_box.w, (near.x + near.w + 255) & ~255), y1 = std::min(page_box.h, (near.y + near.h + 255) & ~255);
    Box r;
    if (x1 > x0 && y1 > y0) r = { x0, y0, x1 - x0, y1 - y0 };
    return r;
}

// ---- the cache ----

bool Tile::same_zoom(double z) const { return fabs(zoom - z) <= z * 1e-6; }

bool Tile::covers(int pg, int r, double z, const Box& need) const
{
    if (page != pg || rot != r || !same_zoom(z)) return false;
    Box have = { x, y, w, h };
    return have.contains(need);
}

void Cache::remove(size_t i)
{
    bytes_ -= tiles_[i]->bytes();
    delete tiles_[i];
    tiles_.erase(tiles_.begin() + i);
}

void Cache::put(Tile* t, int current, double zoom)
{
    // the largest whole tile of the page at another zoom stays as a part's background
    const Tile* background = nullptr;
    if (!t->whole())
        for (const Tile* o : tiles_)
            if (o->page == t->page && o->whole() && !(o->same_zoom(t->zoom) && o->rot == t->rot) &&
                (!background || o->bytes() > background->bytes()))
                background = o;
    for (size_t i = tiles_.size(); i-- > 0;) {
        const Tile* o = tiles_[i];
        if (o->page != t->page || o == background) continue;
        if (t->whole() || o->rot != t->rot || !o->same_zoom(t->zoom) || !o->whole()) remove(i);
    }
    tiles_.push_back(t);
    bytes_ += t->bytes();
    evict(current, zoom, t);
}

void Cache::evict(int current, double zoom, const Tile* keep)
{
    while (bytes_ > budget_ && tiles_.size() > (keep ? 1u : 0u)) {
        size_t worst = tiles_.size();
        long worst_score = -1;
        for (size_t i = 0; i < tiles_.size(); i++) {
            const Tile* o = tiles_[i];
            if (o == keep) continue;
            // the farther the worse; a tile at another zoom counts as farther than any at the current one
            long score = labs((long)o->page - current) + (o->same_zoom(zoom) ? 0 : 1000000L);
            if (score > worst_score) { worst = i; worst_score = score; }
        }
        if (worst == tiles_.size()) break;
        remove(worst);
    }
}

void Cache::trim(int current, double zoom) { evict(current, zoom, nullptr); }

std::vector<const Tile*> Cache::tiles_for(int page, int rot, double zoom) const
{
    std::vector<const Tile*> other, same;
    for (const Tile* t : tiles_) {
        if (t->page != page) continue;
        if (t->rot == rot && t->same_zoom(zoom)) same.push_back(t);
        else if (t->rot == rot) other.push_back(t);
    }
    other.insert(other.end(), same.begin(), same.end());
    return other;
}

bool Cache::covered(int page, int rot, double zoom, const Box& need) const
{
    for (const Tile* t : tiles_)
        if (t->covers(page, rot, zoom, need)) return true;
    return false;
}

void Cache::drop_page(int page)
{
    for (size_t i = tiles_.size(); i-- > 0;)
        if (tiles_[i]->page == page) remove(i);
}

void Cache::clear()
{
    for (Tile* t : tiles_) delete t;
    tiles_.clear();
    bytes_ = 0;
}

}   // namespace mupf
