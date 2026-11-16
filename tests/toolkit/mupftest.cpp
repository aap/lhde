// mupf's page layout and render cache (apps/mupf/layout.h), without mupdf or a window: the zoom steps; rotation of sizes
// and points (and back); the spreads of the facing layouts with and without the cover page; the four layouts' boxes
// (the column, one page at a time, the pairs against the fold, centring in a wide view); Fit Width and Fit Page; the
// current page as the view scrolls; the part of a page rendered at a zoom too large to render whole; and the cache: within
// its byte budget whatever is put, the farthest pages evicted first, a tile at another zoom before one at the current,
// a whole tile replacing the page's others, a part keeping one whole tile behind it as the background.
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "apps/mupf/layout.h"

using namespace mupf;

static int fails, checks;
static void check(const char* what, const std::string& want, const std::string& got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected [%s] got [%s]\n", what, want.c_str(), got.c_str());
}
static void check(const char* what, long long want, long long got) { check(what, std::to_string(want), std::to_string(got)); }
static void check(const char* what, bool ok) { check(what, std::string("true"), std::string(ok ? "true" : "false")); }
static std::string box(const Box& b) { return b.empty() ? "-" : std::to_string(b.x) + "," + std::to_string(b.y) + " " + std::to_string(b.w) + "x" + std::to_string(b.h); }
static std::string pct(double z) { return std::to_string((int)lround(z * 100)); }

static Tile* tile(int page, double zoom, int full_w, int full_h, Box part = Box())
{
    Tile* t = new Tile;
    t->page = page;
    t->zoom = zoom;
    t->full_w = full_w;
    t->full_h = full_h;
    if (part.empty()) part = { 0, 0, full_w, full_h };
    t->x = part.x; t->y = part.y; t->w = part.w; t->h = part.h;
    t->px.assign((size_t)part.w * part.h, 0xffffffffu);
    return t;
}

int main()
{
    // the zoom steps
    check("zoom in from 100%", "125", pct(zoom_in(1.0)));
    check("zoom in from 110%", "125", pct(zoom_in(1.1)));
    check("zoom out from 110%", "100", pct(zoom_out(1.1)));
    check("zoom in at the top stays", "1600", pct(zoom_in(16)));
    check("zoom out at the bottom stays", "8", pct(zoom_out(0.0833)));
    check("clamp", "1600", pct(clamp_zoom(100)));

    // rotation: Letter at 100% is 816 x 1056 px
    SizeF letter = { 612, 792 };
    int w, h;
    page_px(letter, 0, 1.0, &w, &h);
    check("Letter at 100%", "816x1056", std::to_string(w) + "x" + std::to_string(h));
    page_px(letter, 90, 1.0, &w, &h);
    check("Letter turned", "1056x816", std::to_string(w) + "x" + std::to_string(h));
    double px, py;
    page_to_px(0, 0, letter, 90, 1.0, &px, &py);   // the top left corner goes to the top right
    check("90: the top left at the top right", "1056,0", std::to_string((int)lround(px)) + "," + std::to_string((int)lround(py)));
    page_to_px(612, 0, letter, 180, 1.0, &px, &py);
    check("180: the top right at the bottom left", "0,1056", std::to_string((int)lround(px)) + "," + std::to_string((int)lround(py)));
    page_to_px(0, 0, letter, 270, 1.0, &px, &py);
    check("270: the top left at the bottom left", "0,816", std::to_string((int)lround(px)) + "," + std::to_string((int)lround(py)));
    bool back = true;
    for (int rot : { 0, 90, 180, 270 }) {
        float x, y;
        page_to_px(100, 200, letter, rot, 1.5, &px, &py);
        px_to_page(px, py, letter, rot, 1.5, &x, &y);
        if (fabs(x - 100) > 0.01 || fabs(y - 200) > 0.01) back = false;
    }
    check("px_to_page undoes page_to_px at every rotation", back);

    // spreads
    int l, r;
    check("facing: 5 pages in 3 spreads", 3, spread_count(5, MODE_FACING, false));
    check("facing with the cover: 5 pages in 3 spreads", 3, spread_count(5, MODE_FACING, true));
    check("facing with the cover: 4 pages in 3 spreads", 3, spread_count(4, MODE_FACING, true));
    spread_pages(0, 5, MODE_FACING, true, &l, &r);
    check("the cover alone on the right", "-1 0", std::to_string(l) + " " + std::to_string(r));
    spread_pages(1, 5, MODE_FACING, true, &l, &r);
    check("then 2-3", "1 2", std::to_string(l) + " " + std::to_string(r));
    spread_pages(2, 5, MODE_FACING, false, &l, &r);
    check("without: the last alone on the left", "4 -1", std::to_string(l) + " " + std::to_string(r));
    check("page 3 (index 2) is spread 1 with the cover", 1, spread_of(2, MODE_CONTINUOUS_FACING, true));
    check("page 3 is spread 1 without", 1, spread_of(2, MODE_CONTINUOUS_FACING, false));

    // the layouts: three Letter pages at 50% (408 x 528) in an 800 x 600 view
    std::vector<SizeF> three(3, letter);
    Layout c = make_layout(three, 0, 0.5, MODE_CONTINUOUS, false, 0, 800, 600);
    check("continuous: page 1 centred at the top", "196,10 408x528", box(c.page[0]));
    check("continuous: page 2 below it after the gap", "196,548 408x528", box(c.page[1]));
    check("continuous: the plane", "800x1624", std::to_string(c.width) + "x" + std::to_string(c.height));
    Layout s = make_layout(three, 0, 0.5, MODE_SINGLE, false, 1, 800, 600);
    check("single: page 2 alone, in the middle of the view both ways (mupdf's)", "196,36 408x528", box(s.page[1]));
    check("single: page 1 not laid out", "-", box(s.page[0]));
    check("single: the plane is the view", "800x600", std::to_string(s.width) + "x" + std::to_string(s.height));
    Layout f = make_layout(three, 0, 0.5, MODE_CONTINUOUS_FACING, false, 0, 800, 600);
    check("facing: pages 1 and 2 against the fold", "10,10 408x528 428,10 408x528", box(f.page[0]) + " " + box(f.page[1]));
    Layout big = make_layout(three, 0, 1.0, MODE_SINGLE, false, 0, 800, 600);
    check("single, taller than the view: from the margin, the plane the page and its margin", "10,10 816x1056 836x1076",
          box(big.page[0]) + " " + std::to_string(big.width) + "x" + std::to_string(big.height));
    check("facing: page 3 alone on the left", "10,548 408x528", box(f.page[2]));
    check("facing: the plane wider than the view", 846, f.width);
    Layout fc = make_layout(three, 0, 0.5, MODE_FACING, true, 0, 1000, 600);
    check("cover: page 1 right of the fold", "505,36 408x528", box(fc.page[0]));
    check("cover: page 2 not shown with it", "-", box(fc.page[1]));
    Layout fc2 = make_layout(three, 0, 0.5, MODE_FACING, true, 2, 1000, 600);
    check("cover: page 3 shows with page 2", "87,36 408x528 505,36 408x528", box(fc2.page[1]) + " " + box(fc2.page[2]));
    // a reflowable document's pages (laid out 600 pt wide, 800 px at 100%): no margin, no gap, the plane as wide as the view
    std::vector<SizeF> flow = { { 600, 900 }, { 600, 300 } };
    Layout fl = make_layout(flow, 0, 1.0, MODE_CONTINUOUS, false, 0, 800, 600, true);
    check("flow: the pages abut from the top", "0,0 800x1200 0,1200 800x400", box(fl.page[0]) + " " + box(fl.page[1]));
    check("flow: the plane", "800x1600", std::to_string(fl.width) + "x" + std::to_string(fl.height));

    // fits
    check("fit width: 800 px for 612 pt", "95", pct(fit_width(three, 0, MODE_CONTINUOUS, false, 0, 800)));
    check("fit page: 600 px for 792 pt", "55", pct(fit_page(three, 0, MODE_CONTINUOUS, false, 0, 800, 600)));
    check("fit page turned: still the height", "71", pct(fit_page(three, 90, MODE_CONTINUOUS, false, 0, 800, 600)));
    check("fit width facing: two pages", "47", pct(fit_width(three, 0, MODE_FACING, false, 0, 800)));

    // the current page
    check("at the top: page 1", 0, current_page(c, 0, 0, 800, 600));
    check("scrolled 400: page 2 shows more", 1, current_page(c, 0, 400, 800, 600));
    check("at the bottom: page 3", 2, current_page(c, 0, 1024, 800, 600));
    check("in view at 400: pages 1 and 2", "0 1", [&] { std::string o; for (int p : pages_in_view(c, 0, 400, 800, 600)) o += (o.empty() ? "" : " ") + std::to_string(p); return o; }());

    // the part rendered: at 100% the whole page; at 1600% (13056 x 16896, 880 MB) what is in view and half a view around
    Box whole = render_part({ 0, 0, 816, 1056 }, 0, 0, 800, 600);
    check("100%: the whole page", "0,0 816x1056", box(whole));
    Box part = render_part({ 10, 10, 13056, 16896 }, 5000, 8000, 800, 600);
    check("1600%: a part", part.w < 13056 && part.h < 16896);
    check("the part holds the view", part.contains({ 5000 - 10, 8000 - 10, 800, 600 }));
    check("the part within the budget for a whole tile", (size_t)part.w * part.h * 4 <= WHOLE_TILE_MAX);
    check("a page far from the view: nothing", "-", box(render_part({ 0, 0, 13056, 16896 }, 0, 40000, 800, 600)));

    // the cache: 10 MB, Letter tiles at 100% (3.4 MB each)
    {
        size_t one = 816 * 1056 * 4;
        Cache k(10u << 20);
        for (int p = 0; p < 10; p++) {
            k.put(tile(p, 1.0, 816, 1056), p, 1.0);
            if (k.bytes() > k.budget()) { check("within the budget after each put", false); break; }
        }
        check("ten pages put, three kept", 3, (long long)k.count());
        check("the bytes add up", (long long)(3 * one), (long long)k.bytes());
        check("the last three kept (current 9)", k.covered(7, 0, 1.0, { 0, 0, 816, 1056 }) && k.covered(8, 0, 1.0, { 0, 0, 816, 1056 }) &&
              k.covered(9, 0, 1.0, { 0, 0, 816, 1056 }));
        // back at page 2: the farthest (9) goes first
        k.put(tile(2, 1.0, 816, 1056), 2, 1.0);
        check("going back: page 9 evicted", !k.covered(9, 0, 1.0, { 0, 0, 816, 1056 }));
        check("page 7 and 8 kept", k.covered(7, 0, 1.0, { 0, 0, 816, 1056 }) && k.covered(8, 0, 1.0, { 0, 0, 816, 1056 }));
        // a new zoom: the tiles at the old one go before those at the new one, the nearest page notwithstanding
        k.put(tile(7, 0.5, 408, 528), 2, 0.5);
        k.put(tile(8, 0.5, 408, 528), 2, 0.5);
        k.put(tile(3, 1.25, 1020, 1320), 2, 1.25);
        check("within the budget", k.bytes() <= k.budget());
        check("the new zoom's tile kept", k.covered(3, 0, 1.25, { 0, 0, 1020, 1320 }));
        check("over the budget: of the other zooms' tiles the farthest went (page 9 at 50%)", !k.covered(8, 0, 0.5, { 0, 0, 408, 528 }));
        check("the nearer one stays (page 3 at 100%)", k.covered(2, 0, 1.0, { 0, 0, 816, 1056 }));
        // a whole tile replaces the page's others
        k.clear();
        k.put(tile(1, 1.0, 816, 1056), 1, 1.0);
        k.put(tile(1, 2.0, 1632, 2112), 1, 2.0);
        check("a whole tile at the new zoom replaces the old", 1, (long long)k.count());
        check("tiles_for: one", 1, (long long)k.tiles_for(1, 0, 2.0).size());
        // a part keeps the largest whole tile behind it
        k.clear();
        k.set_budget(200u << 20);
        k.put(tile(1, 1.0, 816, 1056), 1, 1.0);
        k.put(tile(1, 2.0, 1632, 2112), 1, 2.0);
        k.put(tile(1, 16.0, 13056, 16896, { 4096, 4096, 2048, 1536 }), 1, 16.0);
        std::vector<const Tile*> t = k.tiles_for(1, 0, 16.0);
        check("a part: the background and the part", 2, (long long)t.size());
        check("the background first, the largest whole one", t.size() == 2 && t[0]->zoom == 2.0 && t[1]->zoom == 16.0);
        k.put(tile(1, 16.0, 13056, 16896, { 4096, 6144, 2048, 1536 }), 1, 16.0);
        check("another part at that zoom replaces the first", 2, (long long)k.tiles_for(1, 0, 16.0).size());
        check("covered: the new part", k.covered(1, 0, 16.0, { 4100, 6200, 800, 600 }));
        check("not covered: the old part", !k.covered(1, 0, 16.0, { 4100, 4200, 800, 600 }));
        check("covered: a rotation is another rendering", !k.covered(1, 90, 16.0, { 4100, 6200, 800, 600 }));
        // a tile larger than the budget is kept alone
        k.clear();
        k.set_budget(1u << 20);
        k.put(tile(0, 1.0, 816, 1056), 0, 1.0);
        k.put(tile(1, 1.0, 816, 1056), 1, 1.0);
        check("over the budget alone: the one just put", 1, (long long)k.count());
        check("and it is page 2", k.covered(1, 0, 1.0, { 0, 0, 816, 1056 }));
        // a new generation (a reflowable document laid out again): the old tiles stale, drawn behind but covering nothing
        k.clear();
        k.set_budget(10u << 20);
        k.put(tile(0, 1.0, 816, 1056), 0, 1.0);
        k.set_gen(2);
        check("stale: not covered", !k.covered(0, 0, 1.0, { 0, 0, 816, 1056 }));
        check("stale: still drawn", 1, (long long)k.tiles_for(0, 0, 1.0).size());
        Tile* fresh = tile(0, 1.0, 816, 900);
        fresh->gen = 2;
        k.put(fresh, 0, 1.0);
        check("the new generation's tile replaces it", 1, (long long)k.count());
        check("and covers", k.covered(0, 0, 1.0, { 0, 0, 816, 900 }));
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
