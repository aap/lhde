// GDI shim, drawing: brushes, pens and lines, rectangles/ellipses/pies/
// polygons, blits (BitBlt/StretchBlt with ternary rops, AlphaBlend,
// TransparentBlt, GradientFill), DIB transfers and pixels.
//
// Rasterisation follows Wine's dlls/win32u/dibdrv (graphics.c, objects.c,
// primitives.c): lines include the start point and exclude the end point
// with Wine's octant bias; Rectangle/Ellipse/Pie paint the interior first and
// the 1-pixel outline over it; ellipses use the Zingl midpoint algorithm.
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "gdi/bmp.h"
#include "gdi_priv.h"

namespace w32 {

// ---------------------------------------------------------------------------
// Brushes

uint32_t brush_pixel(DC* d, Brush* b, int32_t x, int32_t y)
{
    if (b->lb.lbStyle != BS_PATTERN || !b->pattern) return dev_pixel(d, b->lb.lbColor);
    const gdi::Bitmap* p = b->pattern;
    int32_t px = (x - d->base_origin.x - d->brush_org.x) % p->width;
    int32_t py = (y - d->base_origin.y - d->brush_org.y) % p->height;
    if (px < 0) px += p->width;
    if (py < 0) py += p->height;
    uint32_t v = p->row(py)[px];
    if (b->mono) return dev_pixel(d, (v & 0xffffff) ? d->bk_color : d->text_color);
    return v | 0xff000000;
}

void fill_brush(DC* d, const gdi::Rect& lr, Brush* b, int rop2)
{
    if (rop2 == R2_NOP) return;
    if (b && b->lb.lbStyle == BS_NULL) return;     // a null brush paints nothing, whatever the ROP
    bool needs_brush = rop2 != R2_BLACK && rop2 != R2_WHITE && rop2 != R2_NOT;
    if (needs_brush && !b) return;
    bool solid = !needs_brush || b->lb.lbStyle != BS_PATTERN || !b->pattern;
    uint32_t sp = needs_brush && solid ? brush_pixel(d, b, 0, 0) : 0;
    gdi::Bitmap* t = d->dc->target;
    each_clip_rect(d, lr, [&](const gdi::Rect& r) {
        for (int32_t y = r.top; y < r.bottom; y++) {
            uint32_t* row = t->row(y);
            if (solid && rop2 == R2_COPYPEN) {
                for (int32_t x = r.left; x < r.right; x++) row[x] = sp;
                continue;
            }
            for (int32_t x = r.left; x < r.right; x++) {
                uint32_t p = solid ? sp : brush_pixel(d, b, x, y);
                row[x] = rop2_apply(rop2, p, row[x]);
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Pens

struct PenCtx {
    DC* d;
    bool null;
    int rop2;
    uint32_t fg, bg;
    bool opaque_gaps;
    const int* dashes;
    int ndash;
    int cur, left;
    bool mark;
};

static const int dash_dash[] = {18, 6};
static const int dash_dot[] = {3, 3};
static const int dash_dashdot[] = {9, 6, 3, 6};
static const int dash_dashdotdot[] = {9, 3, 3, 3, 3, 3};
static const int dash_alternate[] = {1, 1};

static PenCtx pen_ctx(DC* d)
{
    PenCtx c;
    memset(&c, 0, sizeof c);
    c.d = d;
    Pen* p = d->pen;
    UINT style = p ? p->lp.lopnStyle & PS_STYLE_MASK : PS_NULL;
    c.null = style == PS_NULL;
    c.rop2 = d->rop2;
    c.fg = p ? dev_pixel(d, p->lp.lopnColor) : 0;
    c.bg = dev_pixel(d, d->bk_color);
    c.opaque_gaps = d->bk_mode == OPAQUE;
    switch (style) {
    case PS_DASH: c.dashes = dash_dash; c.ndash = 2; break;
    case PS_DOT: c.dashes = dash_dot; c.ndash = 2; break;
    case PS_DASHDOT: c.dashes = dash_dashdot; c.ndash = 4; break;
    case PS_DASHDOTDOT: c.dashes = dash_dashdotdot; c.ndash = 6; break;
    case PS_ALTERNATE: c.dashes = dash_alternate; c.ndash = 2; break;
    default: break;
    }
    c.cur = 0; c.left = c.dashes ? c.dashes[0] : 0; c.mark = true;
    return c;
}

static inline void plot(DC* d, int32_t x, int32_t y, uint32_t v, int rop2)
{
    gdi::DC* g = d->dc;
    gdi::Point p{x, y};
    if (!g->clip_box.contains(p) || !g->clip.contains(p)) return;
    uint32_t* px = g->target->row(y) + x;
    *px = rop2 == R2_COPYPEN ? v : rop2_apply(rop2, v, *px);
}

static inline void pen_pixel(PenCtx& c, int32_t x, int32_t y)
{
    if (!c.dashes) { plot(c.d, x, y, c.fg, c.rop2); return; }
    if (c.mark) plot(c.d, x, y, c.fg, c.rop2);
    else if (c.opaque_gaps) plot(c.d, x, y, c.bg, c.rop2);
    if (--c.left == 0) {
        c.cur = (c.cur + 1) % c.ndash;
        c.left = c.dashes[c.cur];
        c.mark = !c.mark;
    }
}

// Target coordinates; start included, end excluded. Wine's bres_line_with_bias.
static void pen_line(PenCtx& c, gdi::Point a, gdi::Point b)
{
    if (c.null) return;
    int dx = b.x - a.x, dy = b.y - a.y;
    int adx = abs(dx), ady = abs(dy);
    if (!adx && !ady) return;
    // octants 3, 5, 6, 8 take a bias (get_octant_number / get_bias)
    int oct;
    if (dy > 0) oct = dx > 0 ? (dx > dy ? 1 : 2) : (-dx > dy ? 4 : 3);
    else oct = dx < 0 ? (-dx > -dy ? 5 : 6) : (dx > -dy ? 8 : 7);
    int bias = (oct == 3 || oct == 5 || oct == 6 || oct == 8) ? 1 : 0;
    bool xmajor = oct == 1 || oct == 4 || oct == 5 || oct == 8;
    int xinc = (oct == 1 || oct == 2 || oct == 7 || oct == 8) ? 1 : -1;
    int yinc = oct <= 4 ? 1 : -1;
    gdi::Point p = a;
    if (xmajor) {
        int err = 2 * ady - adx, add1 = 2 * ady - 2 * adx, add2 = 2 * ady;
        for (int n = adx; n > 0; n--) {
            pen_pixel(c, p.x, p.y);
            if (err + bias > 0) { p.y += yinc; err += add1; } else err += add2;
            p.x += xinc;
        }
    } else {
        int err = 2 * adx - ady, add1 = 2 * adx - 2 * ady, add2 = 2 * adx;
        for (int n = ady; n > 0; n--) {
            pen_pixel(c, p.x, p.y);
            if (err + bias > 0) { p.x += xinc; err += add1; } else err += add2;
            p.y += yinc;
        }
    }
}

static void pen_lines(PenCtx& c, const gdi::Point* pts, int n, bool close)
{
    if (c.null || n < 2) return;
    for (int i = 0; i < n - 1; i++) pen_line(c, pts[i], pts[i + 1]);
    if (close) pen_line(c, pts[n - 1], pts[0]);
}

static gdi::Point to_target(DC* d, int x, int y) { return gdi::Point{x + d->dc->origin.x, y + d->dc->origin.y}; }

// ---------------------------------------------------------------------------
// Area fills in target coordinates

static void fill_span_target(DC* d, int32_t y, int32_t x0, int32_t x1, Brush* b, int rop2)
{
    if (x1 <= x0) return;
    gdi::Point o = d->dc->origin;
    fill_brush(d, gdi::Rect{x0 - o.x, y - o.y, x1 - o.x, y + 1 - o.y}, b, rop2);
}

// Scanline polygon fill (pixel (x,y) is inside when the point (x,y) is),
// like X11's region scan conversion. Points in target coordinates.
static void fill_polygon(DC* d, const gdi::Point* pts, const int* counts, int npolys, int mode, Brush* b, int rop2)
{
    if (!b || b->lb.lbStyle == BS_NULL) return;
    struct Edge { int x1, y1, x2, y2, dir; };
    std::vector<Edge> edges;
    int ymin = INT32_MAX, ymax = INT32_MIN;
    int base = 0;
    for (int pi = 0; pi < npolys; pi++) {
        int n = counts[pi];
        for (int i = 0; i < n; i++) {
            gdi::Point a = pts[base + i], c = pts[base + (i + 1) % n];
            if (a.y == c.y) continue;
            Edge e;
            if (a.y < c.y) e = Edge{a.x, a.y, c.x, c.y, 1};
            else e = Edge{c.x, c.y, a.x, a.y, -1};
            edges.push_back(e);
            if (e.y1 < ymin) ymin = e.y1;
            if (e.y2 > ymax) ymax = e.y2;
        }
        base += n;
    }
    gdi::Rect cb = d->dc->clip_box;
    if (ymin < cb.top) ymin = cb.top;
    if (ymax > cb.bottom) ymax = cb.bottom;
    struct X { int x; int dir; };
    std::vector<X> xs;
    for (int y = ymin; y < ymax; y++) {
        xs.clear();
        for (const Edge& e : edges) {
            if (y < e.y1 || y >= e.y2) continue;
            // ceil(x1 + (y - y1) * (x2 - x1) / (y2 - y1))
            int64_t num = (int64_t)(y - e.y1) * (e.x2 - e.x1);
            int64_t den = e.y2 - e.y1;
            int64_t q = num / den, r = num % den;
            if (r > 0) q++;
            xs.push_back(X{(int)(e.x1 + q), e.dir});
        }
        if (xs.size() < 2) continue;
        for (size_t i = 1; i < xs.size(); i++)
            for (size_t j = i; j > 0 && xs[j - 1].x > xs[j].x; j--) { X t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        if (mode == WINDING) {
            int wind = 0;
            for (size_t i = 0; i + 1 < xs.size(); i++) {
                wind += xs[i].dir;
                if (wind) fill_span_target(d, y, xs[i].x, xs[i + 1].x, b, rop2);
            }
        } else {
            for (size_t i = 0; i + 1 < xs.size(); i += 2) fill_span_target(d, y, xs[i].x, xs[i + 1].x, b, rop2);
        }
    }
}

// ---------------------------------------------------------------------------
// Ellipses (dibdrv/graphics.c)

// First quadrant of an ellipse, counterclockwise from the x axis (Zingl).
static int ellipse_first_quadrant(int width, int height, gdi::Point* data)
{
    const int a = width - 1, b = height - 1;
    const int64_t asq = (int64_t)8 * a * a, bsq = (int64_t)8 * b * b;
    int64_t dx = (int64_t)4 * b * b * (1 - a);
    int64_t dy = (int64_t)4 * a * a * (1 + (b % 2));
    int64_t err = dx + dy + (int64_t)a * a * (b % 2);
    int pos = 0;
    gdi::Point pt{a, height / 2};
    while (pt.x >= width / 2) {
        int64_t e2 = 2 * err;
        data[pos++] = pt;
        if (e2 >= dx) { pt.x--; err += dx += bsq; }
        if (e2 <= dy) { pt.y++; err += dy += asq; }
    }
    return pos;
}

static int find_intersection(const gdi::Point* points, int x, int y, int count)
{
    int i;
    if (y >= 0) {
        if (x >= 0) {
            for (i = 0; i < count; i++) if (points[i].x * y <= points[i].y * x) break;
            return i;
        }
        for (i = 0; i < count; i++) if (points[i].x * y < points[i].y * -x) break;
        return 2 * count - i;
    }
    if (x >= 0) {
        for (i = 0; i < count; i++) if (points[i].x * -y <= points[i].y * x) break;
        return 4 * count - i;
    }
    for (i = 0; i < count; i++) if (points[i].x * -y < points[i].y * -x) break;
    return 2 * count + i;
}

// Arc points, counterclockwise (the default arc direction), target coords.
static int get_arc_points(const gdi::Rect& rect, gdi::Point start, gdi::Point end, gdi::Point* points)
{
    int width = rect.width(), height = rect.height();
    int count = ellipse_first_quadrant(width, height, points);
    for (int i = 0; i < count; i++) { points[i].x -= width / 2; points[i].y -= height / 2; }
    start.y = -start.y;
    end.y = -end.y;
    int start_pos = find_intersection(points, start.x, start.y, count);
    int end_pos = find_intersection(points, end.x, end.y, count);
    if (end_pos <= start_pos) end_pos += 4 * count;
    int pos = count;
    for (int i = start_pos; i < end_pos; i++, pos++) {
        switch ((i / count) % 4) {
        case 0:
            points[pos].x = rect.left + width / 2 + points[i % count].x;
            points[pos].y = rect.bottom - 1 - height / 2 - points[i % count].y;
            break;
        case 1:
            points[pos].x = rect.right - 1 - width / 2 - points[count - 1 - i % count].x;
            points[pos].y = rect.bottom - 1 - height / 2 - points[count - 1 - i % count].y;
            break;
        case 2:
            points[pos].x = rect.right - 1 - width / 2 - points[i % count].x;
            points[pos].y = rect.top + height / 2 + points[i % count].y;
            break;
        case 3:
            points[pos].x = rect.left + width / 2 + points[count - 1 - i % count].x;
            points[pos].y = rect.top + height / 2 + points[count - 1 - i % count].y;
            break;
        }
    }
    memmove(points, points + count, (pos - count) * sizeof(gdi::Point));
    return pos - count;
}

static gdi::Rect device_rect(DC* d, int l, int t, int r, int b)
{
    if (l > r) { int x = l; l = r; r = x; }
    if (t > b) { int x = t; t = b; b = x; }
    gdi::Point o = d->dc->origin;
    return gdi::Rect{l + o.x, t + o.y, r + o.x, b + o.y};
}

// extra_lines: 0 Arc, 1 Chord, 2 Pie
static BOOL draw_arc(HDC hdc, int left, int top, int right, int bottom, int sx, int sy, int ex, int ey, int extra_lines)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    gdi::Rect rect = device_rect(d, left, top, right, bottom);
    if (rect.left == rect.right || rect.top == rect.bottom) return TRUE;
    int width = rect.width(), height = rect.height();
    gdi::Point o = d->dc->origin;
    gdi::Point pt0{sx + o.x - (rect.left + width / 2), sy + o.y - (rect.top + height / 2)};
    gdi::Point pt1{ex + o.x - (rect.left + width / 2), ey + o.y - (rect.top + height / 2)};
    std::vector<gdi::Point> points((width + height) * 3 + 4);
    int count = get_arc_points(rect, pt0, pt1, points.data());
    if (extra_lines == 2) points[count++] = gdi::Point{rect.left + width / 2, rect.top + height / 2};
    if (count < 2) return TRUE;
    if (extra_lines > 0) fill_polygon(d, points.data(), &count, 1, WINDING, d->brush, d->rop2);
    PenCtx c = pen_ctx(d);
    pen_lines(c, points.data(), count, extra_lines > 0);
    return TRUE;
}

} // namespace w32

using namespace w32;

extern "C" {

// ---------------------------------------------------------------------------
// Lines and shapes

BOOL WINAPI MoveToEx(HDC hdc, INT x, INT y, LPPOINT old)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    if (old) *old = d->cur_pos;
    d->cur_pos = POINT{x, y};
    return TRUE;
}

BOOL WINAPI GetCurrentPositionEx(HDC hdc, LPPOINT pt)
{
    DC* d = dc_of(hdc);
    if (!d || !pt) return FALSE;
    *pt = d->cur_pos;
    return TRUE;
}

BOOL WINAPI LineTo(HDC hdc, INT x, INT y)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    PenCtx c = pen_ctx(d);
    pen_line(c, to_target(d, d->cur_pos.x, d->cur_pos.y), to_target(d, x, y));
    d->cur_pos = POINT{x, y};
    return TRUE;
}

BOOL WINAPI Polyline(HDC hdc, const POINT* pts, INT n)
{
    DC* d = dc_of(hdc);
    if (!d || !pts || n < 2) return FALSE;
    std::vector<gdi::Point> v(n);
    for (int i = 0; i < n; i++) v[i] = to_target(d, pts[i].x, pts[i].y);
    PenCtx c = pen_ctx(d);
    pen_lines(c, v.data(), n, false);
    return TRUE;
}

BOOL WINAPI PolylineTo(HDC hdc, const POINT* pts, DWORD n)
{
    DC* d = dc_of(hdc);
    if (!d || !pts || !n) return FALSE;
    std::vector<gdi::Point> v(n + 1);
    v[0] = to_target(d, d->cur_pos.x, d->cur_pos.y);
    for (DWORD i = 0; i < n; i++) v[i + 1] = to_target(d, pts[i].x, pts[i].y);
    PenCtx c = pen_ctx(d);
    pen_lines(c, v.data(), n + 1, false);
    d->cur_pos = pts[n - 1];
    return TRUE;
}

BOOL WINAPI PolyPolygon(HDC hdc, const POINT* pts, const INT* counts, UINT npolys_)
{
    int npolys = (int)npolys_;
    DC* d = dc_of(hdc);
    if (!d || !pts || !counts || npolys <= 0) return FALSE;
    int total = 0;
    for (int i = 0; i < npolys; i++) { if (counts[i] < 2) return FALSE; total += counts[i]; }
    std::vector<gdi::Point> v(total);
    for (int i = 0; i < total; i++) v[i] = to_target(d, pts[i].x, pts[i].y);
    fill_polygon(d, v.data(), counts, npolys, d->poly_fill_mode, d->brush, d->rop2);
    PenCtx c = pen_ctx(d);
    int pos = 0;
    for (int i = 0; i < npolys; i++) {
        c.cur = 0; c.left = c.dashes ? c.dashes[0] : 0; c.mark = true;
        pen_lines(c, v.data() + pos, counts[i], true);
        pos += counts[i];
    }
    return TRUE;
}

BOOL WINAPI Polygon(HDC hdc, const POINT* pts, INT n) { return PolyPolygon(hdc, pts, &n, 1); }

BOOL WINAPI Rectangle(HDC hdc, INT left, INT top, INT right, INT bottom)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    gdi::Rect r = device_rect(d, left, top, right, bottom);
    if (r.left == r.right || r.top == r.bottom) return TRUE;
    r.right--; r.bottom--;
    PenCtx c = pen_ctx(d);
    // counterclockwise from top-right (the default arc direction)
    gdi::Point pts[4] = { {r.right, r.top}, {r.left, r.top}, {r.left, r.bottom}, {r.right, r.bottom} };
    pen_lines(c, pts, 4, true);
    int pw = c.null ? 0 : 1;
    gdi::Rect in{r.left + (pw + 1) / 2, r.top + (pw + 1) / 2, r.right - pw / 2, r.bottom - pw / 2};
    gdi::Point o = d->dc->origin;
    fill_brush(d, gdi::offset(in, -o.x, -o.y), d->brush, d->rop2);
    return TRUE;
}

BOOL WINAPI RoundRect(HDC hdc, INT left, INT top, INT right, INT bottom, INT ew, INT eh)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    gdi::Rect rect = device_rect(d, left, top, right, bottom);
    if (rect.left == rect.right || rect.top == rect.bottom) return TRUE;
    ew = min(rect.width(), abs(ew));
    eh = min(rect.height(), abs(eh));
    if (ew <= 2 || eh <= 2) return Rectangle(hdc, left, top, right, bottom);

    // interior: CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1)
    if (d->brush && d->brush->lb.lbStyle != BS_NULL) {
        gdi::Point o = d->dc->origin;
        HRGN rgn = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, ew, eh);
        Region* rg = region_of(rgn);
        for (const gdi::Rect& x : rg->rgn.rects()) fill_brush(d, gdi::offset(x, -o.x, -o.y), d->brush, d->rop2);
        DeleteObject(rgn);
    }

    std::vector<gdi::Point> points((ew + eh) * 2 + 4);
    int count = ellipse_first_quadrant(ew, eh, points.data());
    for (int i = 0; i < count; i++) {
        points[i].x = rect.right - ew + points[i].x;
        points[i].y = rect.top + eh - 1 - points[i].y;
    }
    // horizontal symmetry
    int end = 2 * count - 1;
    if (ew % 2 && ew == rect.width()) end--;
    for (int i = 0; i < count; i++) {
        points[end - i].x = rect.left + rect.right - 1 - points[i].x;
        points[end - i].y = points[i].y;
    }
    count = end + 1;
    // vertical symmetry
    end = 2 * count - 1;
    if (eh % 2 && eh == rect.height()) end--;
    if ((size_t)end >= points.size()) points.resize(end + 1);
    for (int i = 0; i < count; i++) {
        points[end - i].x = points[i].x;
        points[end - i].y = rect.top + rect.bottom - 1 - points[i].y;
    }
    count = end + 1;
    PenCtx c = pen_ctx(d);
    pen_lines(c, points.data(), count, true);
    return TRUE;
}

BOOL WINAPI Ellipse(HDC hdc, INT left, INT top, INT right, INT bottom)
{
    return RoundRect(hdc, left, top, right, bottom, right - left, bottom - top);
}

BOOL WINAPI Pie(HDC hdc, INT l, INT t, INT r, INT b, INT sx, INT sy, INT ex, INT ey)
{
    return draw_arc(hdc, l, t, r, b, sx, sy, ex, ey, 2);
}

BOOL WINAPI Chord(HDC hdc, INT l, INT t, INT r, INT b, INT sx, INT sy, INT ex, INT ey)
{
    return draw_arc(hdc, l, t, r, b, sx, sy, ex, ey, 1);
}

BOOL WINAPI Arc(HDC hdc, INT l, INT t, INT r, INT b, INT sx, INT sy, INT ex, INT ey)
{
    return draw_arc(hdc, l, t, r, b, sx, sy, ex, ey, 0);
}

BOOL WINAPI FillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr)
{
    DC* d = dc_of(hdc);
    Region* rg = region_of(hrgn);
    Brush* b = as<Brush>(hbr, T_BRUSH);
    if (!d || !rg || !b) return FALSE;
    for (const gdi::Rect& r : rg->rgn.rects()) fill_brush(d, r, b, R2_COPYPEN);
    return TRUE;
}

BOOL WINAPI PaintRgn(HDC hdc, HRGN hrgn)
{
    DC* d = dc_of(hdc);
    Region* rg = region_of(hrgn);
    if (!d || !rg) return FALSE;
    for (const gdi::Rect& r : rg->rgn.rects()) fill_brush(d, r, d->brush, d->rop2);
    return TRUE;
}

BOOL WINAPI InvertRgn(HDC hdc, HRGN hrgn)
{
    DC* d = dc_of(hdc);
    Region* rg = region_of(hrgn);
    if (!d || !rg) return FALSE;
    for (const gdi::Rect& r : rg->rgn.rects()) fill_brush(d, r, nullptr, R2_NOT);
    return TRUE;
}

COLORREF WINAPI SetPixel(HDC hdc, INT x, INT y, COLORREF c)
{
    DC* d = dc_of(hdc);
    if (!d) return CLR_INVALID;
    gdi::Point p = to_target(d, x, y);
    gdi::DC* g = d->dc;
    if (!g->clip_box.contains(p) || !g->clip.contains(p)) return CLR_INVALID;
    uint32_t v = dev_pixel(d, c);
    g->target->row(p.y)[p.x] = v;
    return RGB(v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff);
}

BOOL WINAPI SetPixelV(HDC hdc, INT x, INT y, COLORREF c) { return SetPixel(hdc, x, y, c) != CLR_INVALID; }

COLORREF WINAPI GetPixel(HDC hdc, INT x, INT y)
{
    DC* d = dc_of(hdc);
    if (!d) return CLR_INVALID;
    gdi::Point p = to_target(d, x, y);
    gdi::DC* g = d->dc;
    if (!g->clip_box.contains(p) || !g->clip.contains(p)) return CLR_INVALID;
    uint32_t v = g->target->row(p.y)[p.x];
    return RGB(v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff);
}

BOOL WINAPI PlayEnhMetaFile(HDC hdc, HENHMETAFILE emf, const RECT* r)
{
    (void)hdc; (void)emf; (void)r;
    GDI_FIXME("PlayEnhMetaFile: not supported\n");
    return FALSE;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Blits

namespace w32 {

static inline uint32_t rop3_apply(uint32_t code, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t r = 0;
    for (int i = 0; i < 8; i++)
        if (code & (1u << i))
            r |= (i & 4 ? p : ~p) & (i & 2 ? s : ~s) & (i & 1 ? d : ~d);
    // Results are opaque, except that ROPs which leave D alone where S and P
    // are 0 (SRCPAINT, SRCINVERT, NOTSRCAND, PATINVERT, ...) keep D's alpha:
    // in Wine's DIB engine brush and mono-expanded colours carry alpha 0, so
    // e.g. ImageList_AddMasked's NOTSRCAND keeps a 32bpp bitmap's alpha.
    if ((code & 3) == 2) return (r & 0xffffff) | (d & 0xff000000);
    return r | 0xff000000;
}

static inline bool rop_uses_src(DWORD rop) { return ((rop >> 2) ^ rop) & 0x330000; }
static inline bool rop_uses_pat(DWORD rop) { return ((rop >> 4) ^ rop) & 0x0f0000; }

// A source image already in the destination's colour space, one pixel per
// destination pixel, plus the sub-rect (relative) where the source existed.
struct SrcImg {
    int w, h;
    gdi::Rect valid;
    std::vector<uint32_t> px;
};

enum Convert { CONV_NONE, CONV_DST };   // CONV_DST: apply mono<->colour rules for dst

// Sample src (logical xs,ys,ws,hs; ws/hs may be negative to mirror) into a
// wd x hd image (wd,hd > 0; flip flags for a mirrored destination).
static void sample_src(SrcImg& img, DC* dst, DC* src, int xs, int ys, int ws, int hs,
                       int wd, int hd, bool flipx, bool flipy, Convert conv,
                       COLORREF mono_fg, COLORREF mono_bg)
{
    if (ws < 0) { xs += ws; ws = -ws; flipx = !flipx; }
    if (hs < 0) { ys += hs; hs = -hs; flipy = !flipy; }
    img.w = wd; img.h = hd;
    img.px.assign((size_t)wd * hd, 0);
    gdi::Bitmap* sb = src->dc->target;
    gdi::Point so = src->dc->origin;
    bool src_mono = is_mono(src), dst_mono = dst ? is_mono(dst) : false;
    uint32_t fg = dst ? dev_pixel(dst, mono_fg) : 0, bg = dst ? dev_pixel(dst, mono_bg) : 0;
    uint32_t src_bk = 0xff000000u | (uint32_t)GetRValue(src->bk_color) << 16 |
                      (uint32_t)GetGValue(src->bk_color) << 8 | GetBValue(src->bk_color);
    std::vector<int> sxs(wd), sys(hd);
    int vx0 = wd, vx1 = 0, vy0 = hd, vy1 = 0;
    for (int i = 0; i < wd; i++) {
        int k = flipx ? wd - 1 - i : i;
        int sx = xs + (int)(((int64_t)2 * k + 1) * ws / (2 * (int64_t)wd)) + so.x;
        sxs[i] = sx;
        if (sx >= 0 && sx < sb->width) { if (i < vx0) vx0 = i; if (i + 1 > vx1) vx1 = i + 1; }
    }
    for (int j = 0; j < hd; j++) {
        int k = flipy ? hd - 1 - j : j;
        int sy = ys + (int)(((int64_t)2 * k + 1) * hs / (2 * (int64_t)hd)) + so.y;
        sys[j] = sy;
        if (sy >= 0 && sy < sb->height) { if (j < vy0) vy0 = j; if (j + 1 > vy1) vy1 = j + 1; }
    }
    img.valid = vx0 < vx1 && vy0 < vy1 ? gdi::Rect{vx0, vy0, vx1, vy1} : gdi::empty_rect();
    for (int j = img.valid.top; j < img.valid.bottom; j++) {
        if (sys[j] < 0 || sys[j] >= sb->height) continue;
        const uint32_t* srow = sb->row(sys[j]);
        uint32_t* drow = img.px.data() + (size_t)j * wd;
        for (int i = img.valid.left; i < img.valid.right; i++) {
            int sx = sxs[i];
            if (sx < 0 || sx >= sb->width) continue;
            uint32_t v = srow[sx];
            if (conv == CONV_DST && src_mono != dst_mono) {
                if (src_mono) v = (v & 0xffffff) ? bg : fg;
                else v = ((v & 0xffffff) == (src_bk & 0xffffff)) ? 0xffffffffu : 0xff000000u;
            }
            drow[i] = v;
        }
    }
}

// Apply rop with img as S over the destination logical rect at (xd,yd).
static void apply_rop(DC* d, int xd, int yd, const SrcImg& img, DWORD rop)
{
    uint32_t code = (rop >> 16) & 0xff;
    bool pat = rop_uses_pat(rop);
    Brush* b = d->brush;
    if (pat && (!b || b->lb.lbStyle == BS_NULL)) {
        // a null brush acts as if it were absent; Wine skips the operation
        return;
    }
    gdi::Rect lr = gdi::offset(img.valid, xd, yd);
    gdi::Point o = d->dc->origin;
    gdi::Bitmap* t = d->dc->target;
    each_clip_rect(d, lr, [&](const gdi::Rect& r) {
        for (int32_t y = r.top; y < r.bottom; y++) {
            uint32_t* row = t->row(y);
            const uint32_t* s = img.px.data() + (size_t)(y - o.y - yd) * img.w - (o.x + xd);
            if (code == 0xcc) {
                for (int32_t x = r.left; x < r.right; x++) row[x] = s[x];
                continue;
            }
            for (int32_t x = r.left; x < r.right; x++) {
                uint32_t p = pat ? brush_pixel(d, b, x, y) : 0;
                row[x] = rop3_apply(code, p, s[x], row[x]);
            }
        }
    });
}

static bool norm_dst(int& x, int& w, bool& flip)
{
    flip = false;
    if (w < 0) { x += w; w = -w; flip = true; }
    return w > 0;
}

// Raw (unconverted) copy of a source rect for blending; FALSE if it falls
// outside the source surface, like AlphaBlend.
static bool src_rect_ok(DC* src, int xs, int ys, int ws, int hs)
{
    if (ws < 0 || hs < 0) return false;
    gdi::Point so = src->dc->origin;
    gdi::Bitmap* sb = src->dc->target;
    int x = xs + so.x, y = ys + so.y;
    return x >= 0 && y >= 0 && x + ws <= sb->width && y + hs <= sb->height;
}

// Bits + BITMAPINFO -> decoded top-down 32bpp bitmap.
static gdi::Bitmap* decode_dib(const BITMAPINFO* bmi, const void* bits, int lines_override = 0)
{
    if (!bmi || !bits) return nullptr;
    const BITMAPINFOHEADER& h = bmi->bmiHeader;
    size_t hsize, table, rows, stride;
    int bpp;
    int32_t w, ht;
    if (h.biSize == sizeof(BITMAPCOREHEADER)) {
        const BITMAPCOREHEADER* c = (const BITMAPCOREHEADER*)bmi;
        hsize = c->bcSize; bpp = c->bcBitCount; w = c->bcWidth; ht = c->bcHeight;
        table = bpp <= 8 ? (size_t)3 << bpp : 0;
    } else {
        hsize = h.biSize; bpp = h.biBitCount; w = h.biWidth; ht = h.biHeight;
        if (h.biCompression != BI_RGB && h.biCompression != BI_BITFIELDS) {
            GDI_FIXME("DIB compression %u not supported\n", (unsigned)h.biCompression);
            return nullptr;
        }
        size_t n = bpp <= 8 ? (h.biClrUsed && h.biClrUsed <= (1u << bpp) ? h.biClrUsed : 1u << bpp) : h.biClrUsed;
        table = n * 4;
        if (h.biCompression == BI_BITFIELDS && hsize == sizeof(BITMAPINFOHEADER)) table += 12;
    }
    if (w <= 0 || ht == 0 || bpp <= 0) return nullptr;
    if (lines_override) ht = ht < 0 ? -lines_override : lines_override;
    rows = ht < 0 ? -ht : ht;
    stride = (((size_t)w * bpp + 31) / 32) * 4;
    std::vector<uint8_t> buf(hsize + table + stride * rows);
    memcpy(buf.data(), bmi, hsize + table);
    if (lines_override) {
        if (hsize == sizeof(BITMAPCOREHEADER)) ((BITMAPCOREHEADER*)buf.data())->bcHeight = (WORD)ht;
        else ((BITMAPINFOHEADER*)buf.data())->biHeight = ht;
    }
    memcpy(buf.data() + hsize + table, bits, stride * rows);
    return gdi::decode_bmp(buf.data(), buf.size());
}

static void img_from_bitmap(SrcImg& img, const gdi::Bitmap* b, int xs, int ys, int ws, int hs, int wd, int hd, bool flipx, bool flipy)
{
    if (ws < 0) { xs += ws; ws = -ws; flipx = !flipx; }
    if (hs < 0) { ys += hs; hs = -hs; flipy = !flipy; }
    img.w = wd; img.h = hd;
    img.px.assign((size_t)wd * hd, 0);
    int vx0 = wd, vx1 = 0, vy0 = hd, vy1 = 0;
    for (int j = 0; j < hd; j++) {
        int k = flipy ? hd - 1 - j : j;
        int sy = ys + (int)(((int64_t)2 * k + 1) * hs / (2 * (int64_t)hd));
        if (sy < 0 || sy >= b->height) continue;
        for (int i = 0; i < wd; i++) {
            int kx = flipx ? wd - 1 - i : i;
            int sx = xs + (int)(((int64_t)2 * kx + 1) * ws / (2 * (int64_t)wd));
            if (sx < 0 || sx >= b->width) continue;
            img.px[(size_t)j * wd + i] = b->row(sy)[sx];
            if (i < vx0) vx0 = i;
            if (i + 1 > vx1) vx1 = i + 1;
            if (j < vy0) vy0 = j;
            if (j + 1 > vy1) vy1 = j + 1;
        }
    }
    img.valid = vx0 < vx1 && vy0 < vy1 ? gdi::Rect{vx0, vy0, vx1, vy1} : gdi::empty_rect();
}

static inline uint8_t blend_color(uint8_t dst, uint8_t src, uint32_t alpha)
{
    return (uint8_t)((src * alpha + dst * (255 - alpha) + 127) / 255);
}

static inline uint32_t blend_argb_constant_alpha(uint32_t dst, uint32_t src, uint32_t alpha)
{
    return blend_color(dst, src, alpha) | blend_color(dst >> 8, src >> 8, alpha) << 8 |
           blend_color(dst >> 16, src >> 16, alpha) << 16 | blend_color(dst >> 24, src >> 24, alpha) << 24;
}

static inline uint32_t blend_argb_alpha(uint32_t dst, uint32_t src, uint32_t alpha)
{
    uint8_t b = (uint8_t)(((uint8_t)src * alpha + 127) / 255);
    uint8_t g = (uint8_t)(((uint8_t)(src >> 8) * alpha + 127) / 255);
    uint8_t r = (uint8_t)(((uint8_t)(src >> 16) * alpha + 127) / 255);
    alpha = ((uint8_t)(src >> 24) * alpha + 127) / 255;
    return (b + ((uint8_t)dst * (255 - alpha) + 127) / 255) |
           (g + ((uint8_t)(dst >> 8) * (255 - alpha) + 127) / 255) << 8 |
           (r + ((uint8_t)(dst >> 16) * (255 - alpha) + 127) / 255) << 16 |
           (alpha + ((uint8_t)(dst >> 24) * (255 - alpha) + 127) / 255) << 24;
}

} // namespace w32

extern "C" {

BOOL WINAPI PatBlt(HDC hdc, INT x, INT y, INT w, INT h, DWORD rop)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    int rop2 = (((rop >> 18) & 0x0c) | ((rop >> 16) & 0x03)) + 1;
    fill_brush(d, gdi::Rect{x, y, x + w, y + h}, d->brush, rop2);
    return TRUE;
}

BOOL WINAPI StretchBlt(HDC hdst, INT xd, INT yd, INT wd, INT hd, HDC hsrc, INT xs, INT ys, INT ws, INT hs, DWORD rop)
{
    DC* d = dc_of(hdst);
    if (!d) return FALSE;
    if (!rop_uses_src(rop)) return PatBlt(hdst, xd, yd, wd, hd, rop);
    DC* s = dc_of(hsrc);
    if (!s) return FALSE;
    bool fx, fy;
    if (!norm_dst(xd, wd, fx) || !norm_dst(yd, hd, fy)) return TRUE;
    if (!ws || !hs) return TRUE;
    SrcImg img;
    sample_src(img, d, s, xs, ys, ws, hs, wd, hd, fx, fy, CONV_DST, d->text_color, d->bk_color);
    apply_rop(d, xd, yd, img, rop);
    return TRUE;
}

BOOL WINAPI BitBlt(HDC hdst, INT xd, INT yd, INT w, INT h, HDC hsrc, INT xs, INT ys, DWORD rop)
{
    return StretchBlt(hdst, xd, yd, w, h, hsrc, xs, ys, w, h, rop);
}

BOOL WINAPI MaskBlt(HDC hdst, INT xd, INT yd, INT w, INT h, HDC hsrc, INT xs, INT ys,
                    HBITMAP mask, INT xm, INT ym, DWORD rop)
{
    (void)mask; (void)xm; (void)ym;
    GDI_FIXME("MaskBlt: mask ignored\n");
    return BitBlt(hdst, xd, yd, w, h, hsrc, xs, ys, rop & 0xffffff);
}

BOOL WINAPI GdiAlphaBlend(HDC hdst, INT xd, INT yd, INT wd, INT hd, HDC hsrc, INT xs, INT ys, INT ws, INT hs, BLENDFUNCTION bf)
{
    DC* d = dc_of(hdst);
    DC* s = dc_of(hsrc);
    if (!d || !s) return FALSE;
    if (wd < 0 || hd < 0 || !src_rect_ok(s, xs, ys, ws, hs)) return FALSE;
    if (!wd || !hd || !ws || !hs) return TRUE;
    if (bf.BlendOp != AC_SRC_OVER) return FALSE;
    SrcImg img;
    sample_src(img, nullptr, s, xs, ys, ws, hs, wd, hd, false, false, CONV_NONE, 0, 0);
    gdi::Rect lr = gdi::offset(img.valid, xd, yd);
    gdi::Point o = d->dc->origin;
    gdi::Bitmap* t = d->dc->target;
    uint32_t sca = bf.SourceConstantAlpha;
    bool per_pixel = bf.AlphaFormat & AC_SRC_ALPHA;
    each_clip_rect(d, lr, [&](const gdi::Rect& r) {
        for (int32_t y = r.top; y < r.bottom; y++) {
            uint32_t* row = t->row(y);
            const uint32_t* sp = img.px.data() + (size_t)(y - o.y - yd) * img.w - (o.x + xd);
            for (int32_t x = r.left; x < r.right; x++) {
                if (per_pixel) row[x] = blend_argb_alpha(row[x], sp[x], sca);
                else row[x] = blend_argb_constant_alpha(row[x], sp[x], sca);
            }
        }
    });
    return TRUE;
}

BOOL WINAPI AlphaBlend(HDC hdst, INT xd, INT yd, INT wd, INT hd, HDC hsrc, INT xs, INT ys, INT ws, INT hs, BLENDFUNCTION bf)
{
    return GdiAlphaBlend(hdst, xd, yd, wd, hd, hsrc, xs, ys, ws, hs, bf);
}

BOOL WINAPI GdiTransparentBlt(HDC hdst, INT xd, INT yd, INT wd, INT hd, HDC hsrc, INT xs, INT ys, INT ws, INT hs, UINT key)
{
    DC* d = dc_of(hdst);
    DC* s = dc_of(hsrc);
    if (!d || !s) return FALSE;
    if (wd < 0 || hd < 0 || ws < 0 || hs < 0) return FALSE;
    if (!wd || !hd || !ws || !hs) return TRUE;
    SrcImg img;
    // like Wine: the work copy is made with text black / bk white
    DC tmp = *d;
    tmp.bitmap = nullptr;   // a colour target for the conversion
    sample_src(img, &tmp, s, xs, ys, ws, hs, wd, hd, false, false, CONV_DST, RGB(0, 0, 0), RGB(255, 255, 255));
    uint32_t k = (uint32_t)GetRValue(key) << 16 | (uint32_t)GetGValue(key) << 8 | GetBValue(key);
    gdi::Rect lr = gdi::offset(img.valid, xd, yd);
    gdi::Point o = d->dc->origin;
    gdi::Bitmap* t = d->dc->target;
    bool mono = is_mono(d);
    each_clip_rect(d, lr, [&](const gdi::Rect& r) {
        for (int32_t y = r.top; y < r.bottom; y++) {
            uint32_t* row = t->row(y);
            const uint32_t* sp = img.px.data() + (size_t)(y - o.y - yd) * img.w - (o.x + xd);
            for (int32_t x = r.left; x < r.right; x++) {
                uint32_t v = sp[x];
                if ((v & 0xffffff) == k) continue;
                if (mono) v = dev_pixel(d, RGB(v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff));
                row[x] = v | 0xff000000;
            }
        }
    });
    return TRUE;
}

BOOL WINAPI TransparentBlt(HDC hdst, INT xd, INT yd, INT wd, INT hd, HDC hsrc, INT xs, INT ys, INT ws, INT hs, UINT key)
{
    return GdiTransparentBlt(hdst, xd, yd, wd, hd, hsrc, xs, ys, ws, hs, key);
}

// GRADIENT_FILL_RECT as GDI computes it: per channel 8.24 fixed point, start + pos * (((end - start) << 24) / len), truncated
// (a 4051 caption gradient matches this to the bit; luxwm's classic frames were measured against it)
static inline uint32_t gradient_channel(int c0, int c1, int64_t pos, int64_t len)
{
    int64_t d = (((int64_t)c1 - c0) << 24) / len, v = ((int64_t)c0 << 24) + d * pos;
    int c = (int)(v >> 24);
    return (uint32_t)(c < 0 ? 0 : c > 255 ? 255 : c);
}
static inline uint32_t gradient_8888(const TRIVERTEX* v, int64_t pos, int64_t len)
{
    if (len <= 0) len = 1;
    uint32_t r = gradient_channel(v[0].Red >> 8, v[1].Red >> 8, pos, len);
    uint32_t g = gradient_channel(v[0].Green >> 8, v[1].Green >> 8, pos, len);
    uint32_t b = gradient_channel(v[0].Blue >> 8, v[1].Blue >> 8, pos, len);
    uint32_t a = gradient_channel(v[0].Alpha >> 8, v[1].Alpha >> 8, pos, len);
    return a << 24 | r << 16 | g << 8 | b;
}

static inline int edge_coord(int y, int x1, int y1, int x2, int y2)
{
    if (x2 > x1) return x2 + (y - y2) * (x2 - x1) / (y2 - y1);
    return x1 + (y - y1) * (x2 - x1) / (y2 - y1);
}

BOOL WINAPI GdiGradientFill(HDC hdc, PTRIVERTEX vert, ULONG nvert, PVOID grad, ULONG ngrad, ULONG mode)
{
    DC* d = dc_of(hdc);
    if (!d || !vert || !grad) return FALSE;
    gdi::Point o = d->dc->origin;
    gdi::Bitmap* t = d->dc->target;
    bool mono = is_mono(d);
    auto store = [&](uint32_t* p, uint32_t v) {
        *p = mono ? dev_pixel(d, RGB(v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff)) : v;
    };
    switch (mode) {
    case GRADIENT_FILL_RECT_H:
    case GRADIENT_FILL_RECT_V: {
        const GRADIENT_RECT* gr = (const GRADIENT_RECT*)grad;
        for (ULONG i = 0; i < ngrad; i++) {
            ULONG u = gr[i].UpperLeft, l = gr[i].LowerRight;
            if (u >= nvert || l >= nvert) return FALSE;
            TRIVERTEX v[2];
            gdi::Rect bounds;
            if (mode == GRADIENT_FILL_RECT_H) {
                if (vert[l].x < vert[u].x) { ULONG x = u; u = l; l = x; }
                v[0] = vert[u]; v[1] = vert[l];
                bounds = gdi::Rect{v[0].x, min(v[0].y, v[1].y), v[1].x, max(v[0].y, v[1].y)};
            } else {
                if (vert[l].y < vert[u].y) { ULONG x = u; u = l; l = x; }
                v[0] = vert[u]; v[1] = vert[l];
                bounds = gdi::Rect{min(v[0].x, v[1].x), v[0].y, max(v[0].x, v[1].x), v[1].y};
            }
            each_clip_rect(d, bounds, [&](const gdi::Rect& r) {
                for (int32_t y = r.top; y < r.bottom; y++) {
                    uint32_t* row = t->row(y);
                    for (int32_t x = r.left; x < r.right; x++) {
                        if (mode == GRADIENT_FILL_RECT_H) store(row + x, gradient_8888(v, x - o.x - v[0].x, v[1].x - v[0].x));
                        else store(row + x, gradient_8888(v, y - o.y - v[0].y, v[1].y - v[0].y));
                    }
                }
            });
        }
        return TRUE;
    }
    case GRADIENT_FILL_TRIANGLE: {
        const GRADIENT_TRIANGLE* tri = (const GRADIENT_TRIANGLE*)grad;
        for (ULONG i = 0; i < ngrad; i++) {
            ULONG i1 = tri[i].Vertex1, i2 = tri[i].Vertex2, i3 = tri[i].Vertex3;
            if (i1 >= nvert || i2 >= nvert || i3 >= nvert) return FALSE;
            // sort by y (get_gradient_triangle_vertices)
            ULONG a = i1, b = i2, c = i3, x;
            if (vert[a].y > vert[b].y) { x = a; a = b; b = x; }
            if (vert[b].y > vert[c].y) { x = b; b = c; c = x; }
            if (vert[a].y > vert[b].y) { x = a; a = b; b = x; }
            TRIVERTEX v[3] = { vert[a], vert[b], vert[c] };
            int det = (v[2].y - v[1].y) * (v[2].x - v[0].x) - (v[2].x - v[1].x) * (v[2].y - v[0].y);
            if (!det) continue;
            gdi::Rect bounds{ min(v[0].x, min(v[1].x, v[2].x)), v[0].y, max(v[0].x, max(v[1].x, v[2].x)), v[2].y };
            each_clip_rect(d, bounds, [&](const gdi::Rect& r) {
                for (int32_t ty = r.top; ty < r.bottom; ty++) {
                    int y = ty - o.y;
                    int x1 = y < v[1].y ? edge_coord(y, v[0].x, v[0].y, v[1].x, v[1].y)
                                        : edge_coord(y, v[1].x, v[1].y, v[2].x, v[2].y);
                    int x2 = edge_coord(y, v[0].x, v[0].y, v[2].x, v[2].y);
                    int left = max(r.left - o.x, min(x1, x2)), right = min(r.right - o.x, max(x1, x2));
                    uint32_t* row = t->row(ty);
                    for (int lx = left; lx < right; lx++) {
                        int64_t l1 = (int64_t)(v[1].y - v[2].y) * (lx - v[2].x) - (int64_t)(v[1].x - v[2].x) * (y - v[2].y);
                        int64_t l2 = (int64_t)(v[2].y - v[0].y) * (lx - v[2].x) - (int64_t)(v[2].x - v[0].x) * (y - v[2].y);
                        int64_t l3 = det - l1 - l2;
                        uint32_t rr = (uint32_t)((v[0].Red * l1 + v[1].Red * l2 + v[2].Red * l3) / det / 256);
                        uint32_t gg = (uint32_t)((v[0].Green * l1 + v[1].Green * l2 + v[2].Green * l3) / det / 256);
                        uint32_t bb = (uint32_t)((v[0].Blue * l1 + v[1].Blue * l2 + v[2].Blue * l3) / det / 256);
                        uint32_t aa = (uint32_t)((v[0].Alpha * l1 + v[1].Alpha * l2 + v[2].Alpha * l3) / det / 256);
                        store(row + lx + o.x, (aa & 0xff) << 24 | (rr & 0xff) << 16 | (gg & 0xff) << 8 | (bb & 0xff));
                    }
                }
            });
        }
        return TRUE;
    }
    default: return FALSE;
    }
}

BOOL WINAPI GradientFill(HDC hdc, PTRIVERTEX vert, ULONG nvert, PVOID grad, ULONG ngrad, ULONG mode)
{
    return GdiGradientFill(hdc, vert, nvert, grad, ngrad, mode);
}

// ---------------------------------------------------------------------------
// DIBs

INT WINAPI StretchDIBits(HDC hdc, INT xd, INT yd, INT wd, INT hd, INT xs, INT ys, INT ws, INT hs,
                         const void* bits, const BITMAPINFO* bmi, UINT usage, DWORD rop)
{
    DC* d = dc_of(hdc);
    if (!d || !bmi || !bits) return 0;
    if (usage != DIB_RGB_COLORS) GDI_FIXME("StretchDIBits: usage %u treated as DIB_RGB_COLORS\n", usage);
    gdi::Bitmap* b = decode_dib(bmi, bits);
    if (!b) return 0;
    bool top_down = bmi->bmiHeader.biSize != sizeof(BITMAPCOREHEADER) && bmi->bmiHeader.biHeight < 0;
    int height = b->height;
    bool nsfo = xs == 0 && ys == 0 && ws == wd && hs == hd;
    // nulldrv_StretchDIBits: the source y is from the bottom for bottom-up
    // DIBs (and, quirk, for stretched SRCCOPY from top-down ones)
    if (!top_down || (rop == SRCCOPY && !nsfo)) ys = height - ys - hs;
    bool fx, fy;
    if (norm_dst(xd, wd, fx) && norm_dst(yd, hd, fy) && ws && hs) {
        SrcImg img;
        img_from_bitmap(img, b, xs, ys, ws, hs, wd, hd, fx, fy);
        if (is_mono(d))
            for (uint32_t& v : img.px) v = dev_pixel(d, RGB(v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff));
        apply_rop(d, xd, yd, img, rop);
    }
    gdi::destroy_bitmap(b);
    return height;
}

INT WINAPI SetDIBitsToDevice(HDC hdc, INT xd, INT yd, DWORD cx, DWORD cy, INT xs, INT ys,
                             UINT start, UINT lines, const void* bits, const BITMAPINFO* bmi, UINT usage)
{
    DC* d = dc_of(hdc);
    if (!d || !bmi || !bits) return 0;
    (void)usage;
    if (start != 0) GDI_FIXME("SetDIBitsToDevice: startscan %u\n", start);
    gdi::Bitmap* b = decode_dib(bmi, bits, (int)lines);
    if (!b) return 0;
    bool top_down = bmi->bmiHeader.biSize != sizeof(BITMAPCOREHEADER) && bmi->bmiHeader.biHeight < 0;
    if (!top_down) ys = b->height - ys - (int)cy;
    SrcImg img;
    img_from_bitmap(img, b, xs, ys, (int)cx, (int)cy, (int)cx, (int)cy, false, false);
    apply_rop(d, xd, yd, img, SRCCOPY);
    gdi::destroy_bitmap(b);
    return (INT)lines;
}

INT WINAPI GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT lines, void* bits, BITMAPINFO* bmi, UINT usage)
{
    (void)usage;
    Bitmap* bm = as<Bitmap>(hbm, T_BITMAP);
    if (!dc_of(hdc) || !bm || !bmi) return 0;
    BITMAPINFOHEADER& h = bmi->bmiHeader;
    if (h.biSize < sizeof(BITMAPINFOHEADER)) { GDI_FIXME("GetDIBits: core headers\n"); return 0; }
    const gdi::Bitmap* b = bm->bmp;
    if (!bits) {
        if (h.biBitCount == 0) {
            int bpp = bm->req_bpp ? bm->req_bpp : 32;
            h.biWidth = b->width;
            h.biHeight = (bm->dib && b->stride > 0) ? -b->height : b->height;
            h.biPlanes = 1;
            h.biBitCount = (WORD)bpp;
            h.biCompression = BI_RGB;
            h.biSizeImage = (((b->width * bpp + 31) / 32) * 4) * b->height;
            h.biXPelsPerMeter = h.biYPelsPerMeter = 0;
            h.biClrUsed = h.biClrImportant = 0;
            return 1;
        }
        h.biSizeImage = (((b->width * h.biBitCount + 31) / 32) * 4) * b->height;
        return b->height;
    }
    if ((h.biBitCount != 32 && h.biBitCount != 24 && h.biBitCount != 1) || (h.biCompression != BI_RGB)) {
        GDI_FIXME("GetDIBits: %u bpp compression %u not supported\n", h.biBitCount, (unsigned)h.biCompression);
        return 0;
    }
    if (h.biWidth <= 0) return 0;
    bool top_down = h.biHeight < 0;
    int dh = top_down ? -h.biHeight : h.biHeight;
    int bpp = h.biBitCount;
    size_t stride = (((size_t)h.biWidth * bpp + 31) / 32) * 4;
    int w = min((int)h.biWidth, b->width);
    UINT done = 0;
    for (UINT i = 0; i < lines; i++) {
        UINT line = start + i;              // DIB scanline (from the bottom if bottom-up)
        if ((int)line >= dh) break;
        int y = top_down ? (int)line : dh - 1 - (int)line;    // row in the DIB image, top-down
        if (y >= b->height) continue;
        uint8_t* out = (uint8_t*)bits + stride * i;
        const uint32_t* src = b->row(y);
        if (bpp == 32) memcpy(out, src, (size_t)w * 4);
        else if (bpp == 1) {
            memset(out, 0, stride);
            for (int x = 0; x < w; x++) if (src[x] & 0xffffff) out[x >> 3] |= 0x80 >> (x & 7);
        }
        else for (int x = 0; x < w; x++) { out[3 * x] = (uint8_t)src[x]; out[3 * x + 1] = (uint8_t)(src[x] >> 8); out[3 * x + 2] = (uint8_t)(src[x] >> 16); }
        done++;
    }
    h.biSizeImage = stride * dh;
    if (bpp == 1) {
        RGBQUAD* ct = (RGBQUAD*)((uint8_t*)bmi + h.biSize);
        ct[0] = RGBQUAD{0, 0, 0, 0};
        ct[1] = RGBQUAD{255, 255, 255, 0};
    }
    return done;
}

INT WINAPI SetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT lines, const void* bits, const BITMAPINFO* bmi, UINT usage)
{
    (void)hdc; (void)usage;
    Bitmap* bm = as<Bitmap>(hbm, T_BITMAP);
    if (!bm || !bits || !bmi || !lines || bm->stock) return 0;
    gdi::Bitmap* src = decode_dib(bmi, bits, (int)lines);
    if (!src) return 0;
    bool top_down = bmi->bmiHeader.biSize != sizeof(BITMAPCOREHEADER) && bmi->bmiHeader.biHeight < 0;
    int full = bmi->bmiHeader.biSize == sizeof(BITMAPCOREHEADER) ? ((const BITMAPCOREHEADER*)bmi)->bcHeight
                                                                  : abs(bmi->bmiHeader.biHeight);
    gdi::Bitmap* dst = bm->bmp;
    int w = min(src->width, dst->width);
    bool mono = bm->req_bpp == 1;
    for (int j = 0; j < src->height; j++) {
        int y = top_down ? (int)start + j : full - 1 - ((int)start + (src->height - 1 - j));
        if (y < 0 || y >= dst->height) continue;
        uint32_t* out = dst->row(y);
        const uint32_t* in = src->row(j);
        for (int x = 0; x < w; x++) {
            uint32_t v = in[x];
            if (mono) {
                int r = v >> 16 & 0xff, g = v >> 8 & 0xff, b = v & 0xff;
                v = r * r + g * g + b * b <= (255 - r) * (255 - r) + (255 - g) * (255 - g) + (255 - b) * (255 - b)
                        ? 0xff000000u : 0xffffffffu;
            }
            out[x] = v;
        }
    }
    gdi::destroy_bitmap(src);
    return (INT)lines;
}

} // extern "C"
