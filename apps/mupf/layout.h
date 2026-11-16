// mupf's page layout and render cache (no win32, no mupdf): where each page of a document goes for a view mode, a zoom
// and a rotation, which page is the current one, and the rendered pixels kept around it within a byte budget. The window
// (mupf.cpp) lays out, asks the engine (engine.h) for what the cache lacks and draws what the cache holds; the toolkit test
// (tests/toolkit/mupftest.cpp) drives this file alone.
//
// Coordinates: a page's size is in points (1/72 in), unrotated, as the document gives it; the zoom is a fraction of the
// actual size, where 100% is 96 pixels to the inch (XP's default screen resolution, which Acrobat's "Actual Size" used);
// the plane is the whole laid-out document in pixels at the zoom, the view a window onto it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace mupf {

struct SizeF { float w = 0, h = 0; };
struct Box {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(const Box& o) const { return o.x >= x && o.y >= y && o.x + o.w <= x + w && o.y + o.h <= y + h; }
};
Box intersect(const Box& a, const Box& b);

const double PX_PER_PT = 96.0 / 72.0;

// The page layouts of Acrobat 6's View > Page Layout: one page (or one pair) at a time, or all of them in a column; the
// pairs side by side ("Facing"). With the cover page the first page is alone on the right and the pairs are 2-3, 4-5, ...
// (a book's spreads); without it 1-2, 3-4, ...
enum Mode { MODE_SINGLE, MODE_CONTINUOUS, MODE_FACING, MODE_CONTINUOUS_FACING };
inline bool is_facing(Mode m) { return m == MODE_FACING || m == MODE_CONTINUOUS_FACING; }
inline bool is_continuous(Mode m) { return m == MODE_CONTINUOUS || m == MODE_CONTINUOUS_FACING; }

// The zoom steps of Zoom In and Zoom Out, Acrobat's: 8.33% .. 1600%
extern const double ZOOM_STEPS[];
extern const int ZOOM_STEP_COUNT;
const double ZOOM_MIN = 0.0833, ZOOM_MAX = 16.0;
double zoom_in(double z);       // the next step above z (the top step itself at the top)
double zoom_out(double z);      // the next step below z
double clamp_zoom(double z);

// a size turned by rot degrees (0, 90, 180, 270 clockwise)
SizeF rotated(SizeF s, int rot);
// a point of the unrotated page (points from its top left) to pixels of the page turned by rot at zoom (from its top left)
void page_to_px(float x, float y, SizeF size, int rot, double zoom, double* px, double* py);
// the inverse: pixels of the turned page at zoom to points of the unrotated page
void px_to_page(double px, double py, SizeF size, int rot, double zoom, float* x, float* y);
// the page's size in pixels at zoom, turned
void page_px(SizeF size, int rot, double zoom, int* w, int* h);

// spreads: the rows of pages a mode shows together (one page each without facing)
int spread_count(int pages, Mode m, bool cover);
int spread_of(int page, Mode m, bool cover);
void spread_pages(int spread, int pages, Mode m, bool cover, int* left, int* right);   // -1 for an empty side

struct Layout {
    Mode mode = MODE_CONTINUOUS;
    bool cover = false;
    int rot = 0;
    double zoom = 1;
    std::vector<Box> page;      // each page's box in the plane; empty for a page the mode does not show now
    int width = 0, height = 0;  // the plane
    int shown_spread = 0;       // the non-continuous modes: the spread shown
};
// the gap between pages and spreads, and around the plane's edge (Acrobat's grey around the pages; mupf's measure)
const int PAGE_GAP = 10, PLANE_MARGIN = 10;

// the layout of a document's pages; the non-continuous modes show current's spread alone. The plane is at least the view's
// size and is the scroll range: the pages and PLANE_MARGIN around them, so a drag stops at their edge. A spread narrower
// than the view is centred across it (mupdf's and Acrobat's); in the non-continuous modes a spread shorter than the view is
// centred down it too (mupdf's single page), while the continuous modes start at the top (Acrobat's).
Layout make_layout(const std::vector<SizeF>& sizes, int rot, double zoom, Mode m, bool cover, int current, int view_w,
                   int view_h);
// the zoom at which current's spread fills the view's width (Fit Width), or fits in the view whole (Fit Page)
double fit_width(const std::vector<SizeF>& sizes, int rot, Mode m, bool cover, int current, int view_w);
double fit_page(const std::vector<SizeF>& sizes, int rot, Mode m, bool cover, int current, int view_w, int view_h);
// the current page for a view at (left, top) of view_w x view_h: of the pages in view the one showing most of its height
// (the left of a spread first); the nearest when none is in view; -1 for no pages
int current_page(const Layout& l, int left, int top, int view_w, int view_h);
// the pages in the view (their boxes intersect it), in page order
std::vector<int> pages_in_view(const Layout& l, int left, int top, int view_w, int view_h);

// ---- the render cache ----

// A rendering of a page, or of a part of it: the page is full_w x full_h pixels at zoom (turned by rot); the part rendered
// is (x, y, w, h) of that, px its pixels, 0xAARRGGBB with A 255 (a 32 bpp DIB's order).
struct Tile {
    uint32_t gen = 0;           // the document's generation (a new document is a new one)
    int page = 0, rot = 0;
    double zoom = 0;
    int full_w = 0, full_h = 0;
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<uint32_t> px;
    size_t bytes() const { return px.size() * sizeof(uint32_t); }
    bool whole() const { return x == 0 && y == 0 && w >= full_w && h >= full_h; }
    bool same_zoom(double z) const;
    // whether it holds need (a part of the page at zoom, in pixels) as the page at zoom and rot
    bool covers(int pg, int r, double z, const Box& need) const;
};

// What a render of one page may cost before only the part in view is rendered (a page at 1600%)
const size_t WHOLE_TILE_MAX = 24u << 20;

// The tiles kept: at most budget bytes, the tile just put always kept. Putting a page's tile drops the tiles of that page it
// makes useless: a whole tile all others of its page; a part the other parts at its zoom and every tile at another zoom but
// the largest whole one (kept as the background drawn scaled while the part covers only the view). Over the budget the
// tiles farthest from the current page go first, tiles at another zoom than the current before tiles at it.
class Cache {
public:
    explicit Cache(size_t budget) : budget_(budget) {}
    ~Cache() { clear(); }
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    void put(Tile* t, int current, double zoom);    // takes it
    // the tiles of a page to draw, back to front: those at another zoom (drawn scaled) before those at zoom
    std::vector<const Tile*> tiles_for(int page, int rot, double zoom) const;
    bool covered(int page, int rot, double zoom, const Box& need) const;
    void trim(int current, double zoom);            // over the budget (after a budget change): evict
    void clear();
    void drop_page(int page);
    size_t bytes() const { return bytes_; }
    size_t count() const { return tiles_.size(); }
    size_t budget() const { return budget_; }
    void set_budget(size_t b) { budget_ = b; }

private:
    void evict(int current, double zoom, const Tile* keep);
    void remove(size_t i);
    std::vector<Tile*> tiles_;
    size_t budget_ = 0, bytes_ = 0;
};

// The part of a page to render for a view: the whole page when it costs at most WHOLE_TILE_MAX, else what is in view with
// half a view's margin around it (in the page's pixels; empty when the page is not near the view)
Box render_part(const Box& page_box, int left, int top, int view_w, int view_h);

}   // namespace mupf
