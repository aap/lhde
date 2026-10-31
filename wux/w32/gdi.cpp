// GDI shim, objects and DCs: stock objects, pens/brushes/fonts/bitmaps/
// regions, memory and window DCs, the coordinate model and clipping.
// Drawing is in gdi_draw.cpp, text in gdi_text.cpp. Behaviour follows Wine's
// dlls/gdi32 + dlls/win32u.
//
// Coordinates: GDI logical -> device = - window_org + viewport_org; device ->
// target (the toplevel backbuffer or the selected bitmap) = + base_origin.
// The gdi::DC's origin is kept at base_origin + viewport_org - window_org, so
// gdi logical coordinates are GDI logical coordinates.
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include "gdi_priv.h"

namespace w32 {

// ---------------------------------------------------------------------------
// Selection reference counts. A pen/brush/font/bitmap that is selected into a
// DC (or saved by SaveDC) is not freed by DeleteObject until it is released,
// so a careless DeleteObject never leaves a DC pointing at freed memory.

static std::unordered_map<Obj*, int> sel_count;
static std::unordered_set<Obj*> pending_delete;

static std::unordered_set<const void*>& live_objs() { static std::unordered_set<const void*> s; return s; }
Obj::Obj() : type(0), stock(false) { live_objs().insert(this); }
Obj::Obj(uint32_t t, bool s) : type(t), stock(s) { live_objs().insert(this); }
Obj::Obj(const Obj& o) : type(o.type), stock(o.stock) { live_objs().insert(this); }
Obj::~Obj() { live_objs().erase(this); }
bool obj_live(const void* h) { return live_objs().count(h) != 0; }
static void really_delete(Obj* o);

static void ref(Obj* o) { if (o && !o->stock) sel_count[o]++; }
static void unref(Obj* o)
{
    if (!o || o->stock) return;
    auto it = sel_count.find(o);
    if (it == sel_count.end()) return;
    if (--it->second > 0) return;
    sel_count.erase(it);
    if (pending_delete.erase(o)) really_delete(o);
}
static bool is_selected(Obj* o) { return sel_count.count(o) != 0; }

// ---------------------------------------------------------------------------
// Stock objects

static Obj* stock_objs[STOCK_LAST + 1];
static Bitmap* stock_bmp;
static Obj stock_palette = { T_PAL, true };

static Brush* new_brush(COLORREF c, UINT style, bool is_stock)
{
    Brush* b = new Brush;
    b->type = T_BRUSH; b->stock = is_stock;
    b->lb.lbStyle = style; b->lb.lbColor = c; b->lb.lbHatch = 0;
    b->pattern = nullptr; b->mono = false;
    return b;
}

static Pen* new_pen(UINT style, int width, COLORREF c, bool is_stock)
{
    Pen* p = new Pen;
    p->type = T_PEN; p->stock = is_stock;
    p->lp.lopnStyle = style; p->lp.lopnWidth.x = width; p->lp.lopnWidth.y = 0;
    p->lp.lopnColor = c;
    return p;
}

static Font* new_font(const LOGFONTW& lf, bool is_stock);

static LOGFONTW make_logfont(const char* face, int height, int weight)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    for (int i = 0; face[i] && i < LF_FACESIZE - 1; i++) lf.lfFaceName[i] = (WCHAR)face[i];
    return lf;
}

static Bitmap* new_bitmap_obj(gdi::Bitmap* bmp, int req_bpp, bool dib, bool is_stock)
{
    Bitmap* b = new Bitmap;
    b->type = T_BITMAP; b->stock = is_stock;
    b->bmp = bmp; b->req_bpp = req_bpp; b->dib = dib;
    return b;
}

static void fill_pixels(gdi::Bitmap* b, uint32_t v)
{
    for (int32_t y = 0; y < b->height; y++) {
        uint32_t* p = b->row(y);
        for (int32_t x = 0; x < b->width; x++) p[x] = v;
    }
}

void init_gdi()
{
    if (stock_objs[WHITE_BRUSH]) return;
    stock_objs[WHITE_BRUSH]  = new_brush(RGB(255, 255, 255), BS_SOLID, true);
    stock_objs[LTGRAY_BRUSH] = new_brush(RGB(192, 192, 192), BS_SOLID, true);
    stock_objs[GRAY_BRUSH]   = new_brush(RGB(128, 128, 128), BS_SOLID, true);
    stock_objs[DKGRAY_BRUSH] = new_brush(RGB(64, 64, 64), BS_SOLID, true);
    stock_objs[BLACK_BRUSH]  = new_brush(RGB(0, 0, 0), BS_SOLID, true);
    stock_objs[NULL_BRUSH]   = new_brush(0, BS_NULL, true);
    stock_objs[WHITE_PEN]    = new_pen(PS_SOLID, 0, RGB(255, 255, 255), true);
    stock_objs[BLACK_PEN]    = new_pen(PS_SOLID, 0, RGB(0, 0, 0), true);
    stock_objs[NULL_PEN]     = new_pen(PS_NULL, 0, 0, true);
    stock_objs[DC_BRUSH]     = new_brush(RGB(255, 255, 255), BS_SOLID, true);   // no SetDCBrushColor
    stock_objs[DC_PEN]       = new_pen(PS_SOLID, 0, RGB(0, 0, 0), true);
    LOGFONTW shell = make_logfont("MS Shell Dlg", -11, FW_NORMAL);
    LOGFONTW fixed = make_logfont("Courier New", -12, FW_NORMAL);
    fixed.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    stock_objs[DEFAULT_GUI_FONT]    = new_font(shell, true);
    stock_objs[SYSTEM_FONT]         = new_font(shell, true);
    stock_objs[ANSI_VAR_FONT]       = new_font(shell, true);
    stock_objs[DEVICE_DEFAULT_FONT] = new_font(shell, true);
    stock_objs[SYSTEM_FIXED_FONT]   = new_font(fixed, true);
    stock_objs[ANSI_FIXED_FONT]     = new_font(fixed, true);
    stock_objs[OEM_FIXED_FONT]      = new_font(fixed, true);
    stock_objs[DEFAULT_PALETTE]     = &stock_palette;
    gdi::Bitmap* one = gdi::create_bitmap(1, 1);
    one->pixels[0] = 0xff000000;
    stock_bmp = new_bitmap_obj(one, 1, false, true);
}

HGDIOBJ stock(int i)
{
    init_gdi();
    return i >= 0 && i <= STOCK_LAST ? (HGDIOBJ)stock_objs[i] : nullptr;
}

Bitmap* stock_bitmap() { init_gdi(); return stock_bmp; }
Font* stock_font() { return (Font*)stock(DEFAULT_GUI_FONT); }

// ---------------------------------------------------------------------------
// Pens, brushes

uint32_t dev_pixel(const DC* d, COLORREF c)
{
    uint32_t v = 0xff000000u | (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c);
    if (is_mono(d)) {
        // nearest of black and white
        int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
        int dk = r * r + g * g + b * b;
        int lt = (255 - r) * (255 - r) + (255 - g) * (255 - g) + (255 - b) * (255 - b);
        v = dk <= lt ? 0xff000000u : 0xffffffffu;
    }
    return v;
}

} // namespace w32

using namespace w32;

extern "C" {

HGDIOBJ WINAPI GetStockObject(INT i) { return stock(i); }

HPEN WINAPI CreatePen(INT style, INT width, COLORREF color)
{
    if (style < 0 || style > PS_INSIDEFRAME) style = PS_SOLID;
    return (HPEN)new_pen(style, width < 0 ? 0 : width, color & 0xffffff, false);
}

HPEN WINAPI CreatePenIndirect(const LOGPEN* lp)
{
    if (!lp) return nullptr;
    return CreatePen(lp->lopnStyle, lp->lopnWidth.x, lp->lopnColor);
}

HPEN WINAPI ExtCreatePen(DWORD style, DWORD width, const LOGBRUSH* brush, DWORD nstyles, const DWORD* styles)
{
    (void)nstyles; (void)styles;
    DWORD s = style & PS_STYLE_MASK;
    if (s == PS_USERSTYLE) { GDI_FIXME("ExtCreatePen: PS_USERSTYLE drawn solid\n"); s = PS_SOLID; }
    if (brush && brush->lbStyle != BS_SOLID && brush->lbStyle != BS_NULL)
        GDI_FIXME("ExtCreatePen: brush style %u drawn solid\n", (unsigned)brush->lbStyle);
    COLORREF c = brush ? brush->lbColor & 0xffffff : 0;
    if (brush && brush->lbStyle == BS_NULL) s = PS_NULL;
    // cosmetic pens are 1 pixel; we only draw 1-pixel lines anyway
    return (HPEN)new_pen(s, (style & PS_GEOMETRIC) ? width : 0, c, false);
}

HBRUSH WINAPI CreateSolidBrush(COLORREF c) { return (HBRUSH)new_brush(c & 0xffffff, BS_SOLID, false); }

HBRUSH WINAPI CreatePatternBrush(HBITMAP hbm)
{
    Bitmap* bm = as<Bitmap>(hbm, T_BITMAP);
    if (!bm) return nullptr;
    Brush* b = new_brush(0, BS_PATTERN, false);
    b->lb.lbHatch = (ULONG_PTR)hbm;
    gdi::Bitmap* p = gdi::create_bitmap(bm->bmp->width, bm->bmp->height);
    for (int32_t y = 0; y < p->height; y++)
        memcpy(p->row(y), bm->bmp->row(y), p->width * 4);
    b->pattern = p;
    b->mono = bm->req_bpp == 1;
    return (HBRUSH)b;
}

HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH* lb)
{
    if (!lb) return nullptr;
    switch (lb->lbStyle) {
    case BS_NULL: return (HBRUSH)new_brush(0, BS_NULL, false);
    case BS_PATTERN: return CreatePatternBrush((HBITMAP)lb->lbHatch);
    case BS_SOLID: return CreateSolidBrush(lb->lbColor);
    default:
        GDI_FIXME("CreateBrushIndirect: style %u drawn solid\n", (unsigned)lb->lbStyle);
        return CreateSolidBrush(lb->lbColor);
    }
}

HBRUSH WINAPI CreateHatchBrush(INT style, COLORREF c)
{
    (void)style;
    GDI_FIXME("CreateHatchBrush: drawn solid\n");
    return CreateSolidBrush(c);
}

} // extern "C"

// ---------------------------------------------------------------------------
// Fonts

namespace w32 {

static Font* new_font(const LOGFONTW& lf, bool is_stock)
{
    Font* f = new Font;
    f->type = T_FONT; f->stock = is_stock;
    f->lf = lf;
    std::string face = to_utf8(lf.lfFaceName, -1);
    if (face.empty()) face = "MS Shell Dlg";
    gdi::FontDesc d;
    d.family = face.c_str();
    d.height = lf.lfHeight;
    d.weight = lf.lfWeight;
    d.italic = lf.lfItalic != 0;
    d.underline = lf.lfUnderline != 0;
    d.strikeout = lf.lfStrikeOut != 0;
    // ANTIALIASED_QUALITY forces grey smoothing, the ClearType ones ClearType, NONANTIALIASED_QUALITY forbids both
    d.quality = lf.lfQuality == ANTIALIASED_QUALITY ? 1 : lf.lfQuality == CLEARTYPE_QUALITY ||
                lf.lfQuality == CLEARTYPE_NATURAL_QUALITY ? 3 : lf.lfQuality == NONANTIALIASED_QUALITY ? 2 : 0;
    f->font = gdi::create_font(d);
    if (!f->font) {
        d.family = "MS Shell Dlg";
        f->font = gdi::create_font(d);
    }
    if (!f->font) {
        static bool once;
        if (!once) { once = true; fprintf(stderr, "err:gdi: no font for \"%s\" (and no MS Shell Dlg)\n", face.c_str()); }
    }
    return f;
}

} // namespace w32

extern "C" {

HFONT WINAPI CreateFontIndirectW(const LOGFONTW* lf)
{
    if (!lf) return nullptr;
    return (HFONT)new_font(*lf, false);
}

HFONT WINAPI CreateFontIndirectA(const LOGFONTA* lfa)
{
    if (!lfa) return nullptr;
    LOGFONTW lf;
    memcpy(&lf, lfa, offsetof(LOGFONTA, lfFaceName));
    memset(lf.lfFaceName, 0, sizeof lf.lfFaceName);
    std::u16string face = to_utf16(lfa->lfFaceName, strnlen(lfa->lfFaceName, LF_FACESIZE));
    for (size_t i = 0; i < face.size() && i < LF_FACESIZE - 1; i++) lf.lfFaceName[i] = face[i];
    return (HFONT)new_font(lf, false);
}

HFONT WINAPI CreateFontW(INT height, INT width, INT esc, INT orient, INT weight, DWORD italic,
                         DWORD underline, DWORD strikeout, DWORD charset, DWORD outprec,
                         DWORD clipprec, DWORD quality, DWORD pitch, LPCWSTR face)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = height; lf.lfWidth = width; lf.lfEscapement = esc; lf.lfOrientation = orient;
    lf.lfWeight = weight; lf.lfItalic = (BYTE)italic; lf.lfUnderline = (BYTE)underline;
    lf.lfStrikeOut = (BYTE)strikeout; lf.lfCharSet = (BYTE)charset; lf.lfOutPrecision = (BYTE)outprec;
    lf.lfClipPrecision = (BYTE)clipprec; lf.lfQuality = (BYTE)quality; lf.lfPitchAndFamily = (BYTE)pitch;
    if (face) for (int i = 0; face[i] && i < LF_FACESIZE - 1; i++) lf.lfFaceName[i] = face[i];
    return CreateFontIndirectW(&lf);
}

} // extern "C"

// ---------------------------------------------------------------------------
// Bitmaps

namespace w32 {

static int norm_bpp(int bpp)
{
    if (bpp <= 1) return 1;
    if (bpp <= 4) return 4;
    if (bpp <= 8) return 8;
    if (bpp <= 16) return 16;
    if (bpp <= 24) return 24;
    return 32;
}

// DDB rows are WORD aligned.
static int ddb_stride(int w, int bpp) { return ((w * bpp + 15) >> 4) << 1; }

static void set_ddb_bits(gdi::Bitmap* b, int bpp, const void* bits, size_t size)
{
    const uint8_t* src = (const uint8_t*)bits;
    int stride = ddb_stride(b->width, bpp);
    for (int32_t y = 0; y < b->height; y++) {
        if ((size_t)(y + 1) * stride > size) break;
        const uint8_t* s = src + (size_t)y * stride;
        uint32_t* d = b->row(y);
        for (int32_t x = 0; x < b->width; x++) {
            uint32_t v;
            switch (bpp) {
            case 1: v = (s[x >> 3] & (0x80 >> (x & 7))) ? 0xffffffff : 0xff000000; break;
            case 4: { int g = (s[x >> 1] >> (x & 1 ? 0 : 4)) & 15; g *= 17; v = 0xff000000 | g << 16 | g << 8 | g; break; }
            case 8: { int g = s[x]; v = 0xff000000 | g << 16 | g << 8 | g; break; }
            case 16: {
                int p = s[2 * x] | s[2 * x + 1] << 8;
                int r = (p >> 10) & 31, g = (p >> 5) & 31, bl = p & 31;
                v = 0xff000000 | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (bl << 3 | bl >> 2);
                break;
            }
            case 24: v = 0xff000000 | s[3 * x + 2] << 16 | s[3 * x + 1] << 8 | s[3 * x]; break;
            default: memcpy(&v, s + 4 * x, 4); break;
            }
            d[x] = v;
        }
    }
    if (bpp == 4 || bpp == 8) GDI_FIXME("CreateBitmap: %d bpp bits read as grayscale\n", bpp);
}

static void get_ddb_bits(const gdi::Bitmap* b, int bpp, void* bits, size_t size)
{
    uint8_t* dst = (uint8_t*)bits;
    int stride = ddb_stride(b->width, bpp);
    for (int32_t y = 0; y < b->height; y++) {
        if ((size_t)(y + 1) * stride > size) break;
        uint8_t* d = dst + (size_t)y * stride;
        memset(d, 0, stride);
        const uint32_t* s = b->row(y);
        for (int32_t x = 0; x < b->width; x++) {
            uint32_t v = s[x];
            switch (bpp) {
            case 1: if (v & 0xffffff) d[x >> 3] |= 0x80 >> (x & 7); break;
            case 16: { int p = ((v >> 19) & 31) << 10 | ((v >> 11) & 31) << 5 | ((v >> 3) & 31); d[2 * x] = (uint8_t)p; d[2 * x + 1] = (uint8_t)(p >> 8); break; }
            case 24: d[3 * x] = (uint8_t)v; d[3 * x + 1] = (uint8_t)(v >> 8); d[3 * x + 2] = (uint8_t)(v >> 16); break;
            case 32: memcpy(d + 4 * x, &v, 4); break;
            default: break;
            }
        }
    }
}

static void free_bitmap(Bitmap* b)
{
    gdi::Bitmap* g = b->bmp;
    if (b->dib) {
        uint32_t* start = g->stride < 0 ? g->pixels + (intptr_t)(g->height - 1) * g->stride : g->pixels;
        free(start);
        g->owned = false;
    }
    gdi::destroy_bitmap(g);
}

// Pixel buffer start of a DIB section, i.e. what CreateDIBSection returned.
static void* dib_bits(const Bitmap* b)
{
    const gdi::Bitmap* g = b->bmp;
    return g->stride < 0 ? g->pixels + (intptr_t)(g->height - 1) * g->stride : g->pixels;
}

} // namespace w32

extern "C" {

HBITMAP WINAPI CreateBitmap(INT w, INT h, UINT planes, UINT bpp, const void* bits)
{
    if (w < 0 || h < 0) return nullptr;
    if (!w || !h) return (HBITMAP)stock_bitmap();
    int b = norm_bpp((int)(planes * bpp));
    gdi::Bitmap* g = gdi::create_bitmap(w, h);
    fill_pixels(g, 0xff000000);
    if (bits) set_ddb_bits(g, b, bits, (size_t)ddb_stride(w, b) * h);
    return (HBITMAP)new_bitmap_obj(g, b, false, false);
}

HBITMAP WINAPI CreateBitmapIndirect(const BITMAP* bm)
{
    if (!bm) return nullptr;
    return CreateBitmap(bm->bmWidth, bm->bmHeight, bm->bmPlanes, bm->bmBitsPixel, bm->bmBits);
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, INT w, INT h)
{
    DC* d = dc_of(hdc);
    if (!d || w < 0 || h < 0) return nullptr;
    if (!w || !h) return (HBITMAP)stock_bitmap();
    int bpp = is_mono(d) ? 1 : 32;
    gdi::Bitmap* g = gdi::create_bitmap(w, h);
    fill_pixels(g, 0xff000000);
    return (HBITMAP)new_bitmap_obj(g, bpp, false, false);
}

HBITMAP WINAPI CreateDiscardableBitmap(HDC hdc, INT w, INT h) { return CreateCompatibleBitmap(hdc, w, h); }

HBITMAP WINAPI CreateDIBSection(HDC hdc, const BITMAPINFO* bmi, UINT usage, void** bits, HANDLE section, DWORD offset)
{
    (void)hdc; (void)usage; (void)offset;
    if (bits) *bits = nullptr;
    if (!bmi) return nullptr;
    int32_t w, h, bpp;
    if (bmi->bmiHeader.biSize == sizeof(BITMAPCOREHEADER)) {
        const BITMAPCOREHEADER* c = (const BITMAPCOREHEADER*)bmi;
        w = c->bcWidth; h = c->bcHeight; bpp = c->bcBitCount;
    } else if (bmi->bmiHeader.biSize >= sizeof(BITMAPINFOHEADER)) {
        w = bmi->bmiHeader.biWidth; h = bmi->bmiHeader.biHeight; bpp = bmi->bmiHeader.biBitCount;
        if (bmi->bmiHeader.biCompression != BI_RGB && bmi->bmiHeader.biCompression != BI_BITFIELDS) return nullptr;
    } else return nullptr;
    if (w <= 0 || h == 0) return nullptr;
    if (section) GDI_FIXME("CreateDIBSection: hSection ignored\n");
    bool top_down = h < 0;
    int32_t ah = top_down ? -h : h;
    uint32_t* buf = (uint32_t*)calloc((size_t)w * ah, 4);
    if (!buf) return nullptr;
    gdi::Bitmap* g = top_down ? gdi::wrap_bitmap(w, ah, w, buf)
                              : gdi::wrap_bitmap(w, ah, -w, buf + (intptr_t)(ah - 1) * w);
    Bitmap* b = new_bitmap_obj(g, norm_bpp(bpp), true, false);
    if (b->req_bpp != 32) GDI_FIXME("CreateDIBSection: %d bpp allocated as 32 bpp\n", bpp);
    if (bits) *bits = buf;
    return (HBITMAP)b;
}

LONG WINAPI GetBitmapBits(HBITMAP hbm, LONG count, void* bits)
{
    Bitmap* b = as<Bitmap>(hbm, T_BITMAP);
    if (!b) return 0;
    int bpp = b->dib ? 32 : b->req_bpp;
    LONG size = ddb_stride(b->bmp->width, bpp) * b->bmp->height;
    if (!bits) return size;
    if (count > size) count = size;
    get_ddb_bits(b->bmp, bpp, bits, count);
    return count;
}

LONG WINAPI SetBitmapBits(HBITMAP hbm, LONG count, const void* bits)
{
    Bitmap* b = as<Bitmap>(hbm, T_BITMAP);
    if (!b || !bits || b->stock) return 0;
    int bpp = b->dib ? 32 : b->req_bpp;
    set_ddb_bits(b->bmp, bpp, bits, count);
    return count;
}

BOOL WINAPI GetBitmapDimensionEx(HBITMAP hbm, LPSIZE size)
{
    if (!as<Bitmap>(hbm, T_BITMAP) || !size) return FALSE;
    size->cx = size->cy = 0;
    return TRUE;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Regions

namespace w32 {

Region* region_of(HRGN h) { return as<Region>(h, T_REGION); }

static Region* new_region()
{
    Region* r = new Region;
    r->type = T_REGION; r->stock = false;
    return r;
}

int region_type(const gdi::Region& r)
{
    if (r.empty()) return NULLREGION;
    // gdi::Region isn't minimal, so count area instead of rects.
    gdi::Rect b = r.bounds();
    int64_t area = 0;
    for (const gdi::Rect& x : r.rects()) area += (int64_t)x.width() * x.height();
    return area == (int64_t)b.width() * b.height() ? SIMPLEREGION : COMPLEXREGION;
}

static gdi::Rect ordered(int l, int t, int r, int b)
{
    if (l > r) { int x = l; l = r; r = x; }
    if (t > b) { int x = t; t = b; b = x; }
    return gdi::Rect{l, t, r, b};
}

static void combine(gdi::Region& dst, const gdi::Region& a, const gdi::Region& b, int mode)
{
    gdi::Region r = a;
    switch (mode) {
    case RGN_AND: r.intersect(b); break;
    case RGN_OR: r.add(b); break;
    case RGN_DIFF: r.subtract(b); break;
    case RGN_XOR: { gdi::Region t = b; t.subtract(a); r.subtract(b); r.add(t); break; }
    default: break;
    }
    dst = r;
}

static const gdi::Rect huge_rect = { -(1 << 27), -(1 << 27), 1 << 27, 1 << 27 };

} // namespace w32

extern "C" {

HRGN WINAPI CreateRectRgn(INT l, INT t, INT r, INT b)
{
    Region* rg = new_region();
    rg->rgn.set(ordered(l, t, r, b));
    return (HRGN)rg;
}

HRGN WINAPI CreateRectRgnIndirect(const RECT* r)
{
    if (!r) return nullptr;
    return CreateRectRgn(r->left, r->top, r->right, r->bottom);
}

BOOL WINAPI SetRectRgn(HRGN h, INT l, INT t, INT r, INT b)
{
    Region* rg = region_of(h);
    if (!rg) return FALSE;
    rg->rgn.set(ordered(l, t, r, b));
    return TRUE;
}

// Port of NtGdiCreateRoundRectRgn (one rect per row of the rounded part).
HRGN WINAPI CreateRoundRectRgn(INT left, INT top, INT right, INT bottom, INT ew, INT eh)
{
    if (left > right) { INT t = left; left = right; right = t; }
    if (top > bottom) { INT t = top; top = bottom; bottom = t; }
    right--; bottom--;
    ew = min(right - left, abs(ew));
    eh = min(bottom - top, abs(eh));
    if (ew < 2 || eh < 2) return CreateRectRgn(left, top, right, bottom);

    std::vector<RECT> rects(eh + 2);
    int a = ew - 1, b = eh - 1;
    int64_t asq = (int64_t)8 * a * a, bsq = (int64_t)8 * b * b;
    int64_t dx = (int64_t)4 * b * b * (1 - a);
    int64_t dy = (int64_t)4 * a * a * (1 + (b % 2));
    int64_t err = dx + dy + (int64_t)a * a * (b % 2);
    int x = 0, y = eh / 2;
    rects[y].left = left; rects[y].right = right;
    while (x <= ew / 2) {
        int64_t e2 = 2 * err;
        if (e2 >= dx) { x++; err += dx += bsq; }
        if (e2 <= dy) {
            y++; err += dy += asq;
            if (y < (int)rects.size()) { rects[y].left = left + x; rects[y].right = right - x; }
        }
    }
    int i;
    for (i = 0; i < eh / 2; i++) {
        rects[i].left = rects[b - i].left; rects[i].right = rects[b - i].right;
        rects[i].top = top + i; rects[i].bottom = rects[i].top + 1;
    }
    for (; i < eh; i++) { rects[i].top = bottom - eh + i; rects[i].bottom = rects[i].top + 1; }
    rects[eh / 2].top = top + eh / 2;
    Region* rg = new_region();
    for (i = 0; i < eh; i++) rg->rgn.add(rect_of(rects[i]));
    return (HRGN)rg;
}

HRGN WINAPI CreateEllipticRgn(INT l, INT t, INT r, INT b) { return CreateRoundRectRgn(l, t, r, b, r - l, b - t); }
HRGN WINAPI CreateEllipticRgnIndirect(const RECT* r) { return r ? CreateEllipticRgn(r->left, r->top, r->right, r->bottom) : nullptr; }

INT WINAPI CombineRgn(HRGN dst, HRGN s1, HRGN s2, INT mode)
{
    Region* d = region_of(dst);
    Region* a = region_of(s1);
    if (!d || !a) return ERROR;
    if (mode == RGN_COPY) { d->rgn = a->rgn; return region_type(d->rgn); }
    Region* b = region_of(s2);
    if (!b || mode < RGN_AND || mode > RGN_DIFF) return ERROR;
    combine(d->rgn, a->rgn, b->rgn, mode);
    return region_type(d->rgn);
}

HRGN WINAPI ExtCreateRegion(const XFORM* xform, DWORD count, const RGNDATA* data)
{
    (void)xform;
    if (!data || data->rdh.iType != RDH_RECTANGLES) return nullptr;
    (void)count;
    Region* rg = new_region();
    const RECT* r = (const RECT*)data->Buffer;
    for (DWORD i = 0; i < data->rdh.nCount; i++) {
        gdi::Rect x = ordered(r[i].left, r[i].top, r[i].right, r[i].bottom);
        if (!x.empty()) rg->rgn.add(x);
    }
    return (HRGN)rg;
}

DWORD WINAPI GetRegionData(HRGN h, DWORD size, LPRGNDATA data)
{
    Region* rg = region_of(h);
    if (!rg) return 0;
    const auto& rs = rg->rgn.rects();
    DWORD need = sizeof(RGNDATAHEADER) + rs.size() * sizeof(RECT);
    if (!data) return need;
    if (size < need) return 0;
    data->rdh.dwSize = sizeof(RGNDATAHEADER);
    data->rdh.iType = RDH_RECTANGLES;
    data->rdh.nCount = rs.size();
    data->rdh.nRgnSize = rs.size() * sizeof(RECT);
    data->rdh.rcBound = RECT_of(rg->rgn.bounds());
    RECT* out = (RECT*)data->Buffer;
    for (size_t i = 0; i < rs.size(); i++) out[i] = RECT_of(rs[i]);
    return need;
}

INT WINAPI OffsetRgn(HRGN h, INT x, INT y)
{
    Region* rg = region_of(h);
    if (!rg) return ERROR;
    rg->rgn.offset(x, y);
    return region_type(rg->rgn);
}

INT WINAPI GetRgnBox(HRGN h, LPRECT r)
{
    Region* rg = region_of(h);
    if (!rg || !r) return ERROR;
    *r = RECT_of(rg->rgn.empty() ? gdi::empty_rect() : rg->rgn.bounds());
    return region_type(rg->rgn);
}

BOOL WINAPI PtInRegion(HRGN h, INT x, INT y)
{
    Region* rg = region_of(h);
    return rg && rg->rgn.contains(gdi::Point{x, y});
}

BOOL WINAPI RectInRegion(HRGN h, const RECT* r)
{
    Region* rg = region_of(h);
    if (!rg || !r) return FALSE;
    return rg->rgn.overlaps(ordered(r->left, r->top, r->right, r->bottom));
}

BOOL WINAPI EqualRgn(HRGN h1, HRGN h2)
{
    Region* a = region_of(h1);
    Region* b = region_of(h2);
    if (!a || !b) return FALSE;
    gdi::Region x = a->rgn; x.subtract(b->rgn);
    gdi::Region y = b->rgn; y.subtract(a->rgn);
    return x.empty() && y.empty();
}

} // extern "C"

// ---------------------------------------------------------------------------
// DCs

namespace w32 {

struct SavedDC {
    Pen* pen; Brush* brush; Font* font; Bitmap* bitmap;
    COLORREF text_color, bk_color;
    int bk_mode; UINT text_align; int rop2, stretch_mode;
    POINT cur_pos, brush_org, viewport_org, window_org;
    bool has_clip; gdi::Region clip;
    DWORD layout; int poly_fill_mode, map_mode;
};
static std::unordered_map<DC*, std::vector<SavedDC>> saved_dcs;

static void init_dc_state(DC* d)
{
    d->pen = (Pen*)stock(BLACK_PEN);
    d->brush = (Brush*)stock(WHITE_BRUSH);
    d->font = (Font*)stock(SYSTEM_FONT);
    d->text_color = RGB(0, 0, 0);
    d->bk_color = RGB(255, 255, 255);
    d->bk_mode = OPAQUE;
    d->text_align = TA_LEFT | TA_TOP | TA_NOUPDATECP;
    d->rop2 = R2_COPYPEN;
    d->stretch_mode = BLACKONWHITE;
    d->cur_pos = POINT{0, 0};
    d->brush_org = POINT{0, 0};
    d->viewport_org = POINT{0, 0};
    d->window_org = POINT{0, 0};
    d->clip = nullptr;
    d->layout = 0;
    d->poly_fill_mode = ALTERNATE;
    d->map_mode = MM_TEXT;
}

void apply_clip(DC* d)
{
    gdi::Point o = d->base_origin;
    gdi::set_origin(d->dc, gdi::Point{o.x + d->viewport_org.x - d->window_org.x,
                                      o.y + d->viewport_org.y - d->window_org.y});
    gdi::Region r = d->base_clip;
    if (d->clip) {
        gdi::Region c = d->clip->rgn;
        c.offset(o.x, o.y);
        r.intersect(c);
    }
    gdi::set_clip(d->dc, r);
}

DC* create_window_dc(gdi::DC* base, HWND hwnd)
{
    init_gdi();
    DC* d = new DC;
    d->type = T_DC; d->stock = false;
    d->dc = base;
    d->bitmap = nullptr;
    d->hwnd = hwnd;
    init_dc_state(d);
    d->base_origin = base->origin;
    d->base_clip = base->clip;
    apply_clip(d);
    return d;
}

static void release_dc_objects(DC* d)
{
    auto it = saved_dcs.find(d);
    if (it != saved_dcs.end()) {
        for (SavedDC& s : it->second) { unref(s.pen); unref(s.brush); unref(s.font); unref(s.bitmap); }
        saved_dcs.erase(it);
    }
    unref(d->pen); unref(d->brush); unref(d->font);
    if (d->bitmap) unref(d->bitmap);
    if (d->clip) { delete d->clip; d->clip = nullptr; }
}

void destroy_window_dc(DC* d)
{
    if (!d) return;
    release_dc_objects(d);
    delete d;
}

static void set_memdc_bitmap(DC* d, Bitmap* b)
{
    d->bitmap = b;
    d->dc->target = b->bmp;
    d->base_origin = gdi::Point{0, 0};
    d->base_clip.set(b->bmp->rect());
    apply_clip(d);
}

static int clip_type(DC* d) { return region_type(d->dc->clip); }

static gdi::Point lp_to_dp(DC* d, int x, int y)
{
    return gdi::Point{x - d->window_org.x + d->viewport_org.x, y - d->window_org.y + d->viewport_org.y};
}

static void really_delete(Obj* o)
{
    switch (o->type) {
    case T_PEN: delete (Pen*)o; break;
    case T_BRUSH: { Brush* b = (Brush*)o; if (b->pattern) gdi::destroy_bitmap(b->pattern); delete b; break; }
    case T_FONT: { Font* f = (Font*)o; gdi::destroy_font(f->font); delete f; break; }
    case T_BITMAP: { Bitmap* b = (Bitmap*)o; free_bitmap(b); delete b; break; }
    case T_REGION: delete (Region*)o; break;
    default: break;
    }
}

} // namespace w32

extern "C" {

HDC WINAPI CreateCompatibleDC(HDC hdc)
{
    init_gdi();
    DC* src = hdc ? dc_of(hdc) : nullptr;
    if (hdc && !src) return nullptr;
    DC* d = new DC;
    d->type = T_MEMDC; d->stock = false;
    d->dc = gdi::create_dc(stock_bmp->bmp);
    d->bitmap = nullptr;
    d->hwnd = nullptr;
    init_dc_state(d);
    if (src) d->layout = src->layout;
    set_memdc_bitmap(d, stock_bmp);
    return (HDC)d;
}

BOOL WINAPI DeleteDC(HDC hdc)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    if (d->type != T_MEMDC) {
        // A window DC: it is the user shim's to release; just drop our side.
        destroy_window_dc(d);
        return TRUE;
    }
    release_dc_objects(d);
    gdi::destroy_dc(d->dc);
    delete d;
    return TRUE;
}

INT WINAPI SaveDC(HDC hdc)
{
    DC* d = dc_of(hdc);
    if (!d) return 0;
    SavedDC s;
    s.pen = d->pen; s.brush = d->brush; s.font = d->font; s.bitmap = d->bitmap;
    ref(s.pen); ref(s.brush); ref(s.font); ref(s.bitmap);
    s.text_color = d->text_color; s.bk_color = d->bk_color; s.bk_mode = d->bk_mode;
    s.text_align = d->text_align; s.rop2 = d->rop2; s.stretch_mode = d->stretch_mode;
    s.cur_pos = d->cur_pos; s.brush_org = d->brush_org;
    s.viewport_org = d->viewport_org; s.window_org = d->window_org;
    s.has_clip = d->clip != nullptr;
    if (d->clip) s.clip = d->clip->rgn;
    s.layout = d->layout; s.poly_fill_mode = d->poly_fill_mode; s.map_mode = d->map_mode;
    auto& v = saved_dcs[d];
    v.push_back(s);
    return (INT)v.size();
}

BOOL WINAPI RestoreDC(HDC hdc, INT level)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    auto it = saved_dcs.find(d);
    if (it == saved_dcs.end()) return FALSE;
    auto& v = it->second;
    int n = (int)v.size();
    if (level < 0) level = n + level + 1;
    if (level < 1 || level > n) return FALSE;
    SavedDC s = v[level - 1];
    // drop the states above and including `level`; their refs move to the DC
    for (int i = n - 1; i >= level; i--) { unref(v[i].pen); unref(v[i].brush); unref(v[i].font); unref(v[i].bitmap); }
    v.resize(level - 1);
    Pen* old_pen = d->pen; Brush* old_brush = d->brush; Font* old_font = d->font;
    Bitmap* old_bitmap = d->bitmap;
    d->pen = s.pen; d->brush = s.brush; d->font = s.font;   // refs inherited from the saved state
    d->text_color = s.text_color; d->bk_color = s.bk_color; d->bk_mode = s.bk_mode;
    d->text_align = s.text_align; d->rop2 = s.rop2; d->stretch_mode = s.stretch_mode;
    d->cur_pos = s.cur_pos; d->brush_org = s.brush_org;
    d->viewport_org = s.viewport_org; d->window_org = s.window_org;
    d->layout = s.layout; d->poly_fill_mode = s.poly_fill_mode; d->map_mode = s.map_mode;
    if (s.has_clip) {
        if (!d->clip) d->clip = new_region();
        d->clip->rgn = s.clip;
    } else if (d->clip) { delete d->clip; d->clip = nullptr; }
    if (d->type == T_MEMDC && s.bitmap) set_memdc_bitmap(d, s.bitmap);
    else apply_clip(d);
    unref(old_pen); unref(old_brush); unref(old_font); unref(old_bitmap);
    if (v.empty()) saved_dcs.erase(it);
    return TRUE;
}

HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h)
{
    DC* d = dc_of(hdc);
    Obj* o = (Obj*)h;
    if (!d || !o || !obj_live(o)) return nullptr;
    switch (o->type) {
    case T_PEN: { Pen* old = d->pen; ref(o); d->pen = (Pen*)o; unref(old); return (HGDIOBJ)old; }
    case T_BRUSH: { Brush* old = d->brush; ref(o); d->brush = (Brush*)o; unref(old); return (HGDIOBJ)old; }
    case T_FONT: { Font* old = d->font; ref(o); d->font = (Font*)o; unref(old); return (HGDIOBJ)old; }
    case T_BITMAP: {
        if (d->type != T_MEMDC) return nullptr;
        Bitmap* b = (Bitmap*)o;
        Bitmap* old = d->bitmap;
        if (b == old) return h;
        if (!b->stock && is_selected(b)) return nullptr;   // already selected into another DC
        ref(b);
        set_memdc_bitmap(d, b);
        unref(old);
        return (HGDIOBJ)old;
    }
    case T_REGION: {
        INT r = ExtSelectClipRgn(hdc, (HRGN)h, RGN_COPY);
        return r == ERROR ? HGDI_ERROR : (HGDIOBJ)(ULONG_PTR)r;
    }
    default: return nullptr;
    }
}

HPALETTE WINAPI SelectPalette(HDC hdc, HPALETTE pal, BOOL bg)
{
    (void)pal; (void)bg;
    return dc_of(hdc) ? (HPALETTE)stock(DEFAULT_PALETTE) : nullptr;
}
UINT WINAPI RealizePalette(HDC hdc) { (void)hdc; return 0; }

HGDIOBJ WINAPI GetCurrentObject(HDC hdc, UINT type)
{
    DC* d = dc_of(hdc);
    if (!d) return nullptr;
    switch (type) {
    case OBJ_PEN: case OBJ_EXTPEN: return (HGDIOBJ)d->pen;
    case OBJ_BRUSH: return (HGDIOBJ)d->brush;
    case OBJ_FONT: return (HGDIOBJ)d->font;
    case OBJ_BITMAP: return (HGDIOBJ)(d->bitmap ? d->bitmap : stock_bitmap());
    case OBJ_PAL: return stock(DEFAULT_PALETTE);
    default: return nullptr;
    }
}

DWORD WINAPI GetObjectType(HGDIOBJ h)
{
    Obj* o = (Obj*)h;
    if (!o || !obj_live(o)) return 0;
    switch (o->type) {
    case T_PEN: case T_BRUSH: case T_DC: case T_FONT: case T_BITMAP: case T_REGION: case T_MEMDC: case T_PAL:
        return o->type;
    default: return 0;
    }
}

BOOL WINAPI DeleteObject(HGDIOBJ h)
{
    Obj* o = (Obj*)h;
    if (!o || !obj_live(o)) return FALSE;
    if (o->stock) return TRUE;
    switch (o->type) {
    case T_PEN: case T_BRUSH: case T_FONT: case T_BITMAP: case T_REGION: break;
    default: return FALSE;
    }
    if (is_selected(o)) { pending_delete.insert(o); return TRUE; }
    really_delete(o);
    return TRUE;
}

INT WINAPI GetObjectW(HGDIOBJ h, INT size, void* buf)
{
    Obj* o = (Obj*)h;
    if (!o || !obj_live(o)) return 0;
    switch (o->type) {
    case T_PEN: {
        if (!buf) return sizeof(LOGPEN);
        if (size < (INT)sizeof(LOGPEN)) return 0;
        memcpy(buf, &((Pen*)o)->lp, sizeof(LOGPEN));
        return sizeof(LOGPEN);
    }
    case T_BRUSH: {
        if (!buf) return sizeof(LOGBRUSH);
        INT n = min(size, (INT)sizeof(LOGBRUSH));
        memcpy(buf, &((Brush*)o)->lb, n);
        return n;
    }
    case T_FONT: {
        if (!buf) return sizeof(LOGFONTW);
        INT n = min(size, (INT)sizeof(LOGFONTW));
        memcpy(buf, &((Font*)o)->lf, n);
        return n;
    }
    case T_BITMAP: {
        Bitmap* b = (Bitmap*)o;
        // Report the depth the creator asked for (req_bpp; 32 if unset),
        // also for DIB sections whose bits are really 32bpp: the image shim
        // records the source depth and uxtheme's prepare_alpha relies on it.
        // DIB sections are always 32bpp in memory, and uxtheme's prepare_alpha walks
        // the bits by the reported depth, so DIB sections report 32; DDBs report
        // the depth their creator asked for.
        int bpp = b->dib ? 32 : (b->req_bpp ? b->req_bpp : 32);
        BITMAP bm;
        bm.bmType = 0;
        bm.bmWidth = b->bmp->width;
        bm.bmHeight = b->bmp->height;
        bm.bmWidthBytes = b->dib ? b->bmp->width * 4 : ddb_stride(b->bmp->width, bpp);
        bm.bmPlanes = 1;
        bm.bmBitsPixel = bpp;
        bm.bmBits = b->dib ? dib_bits(b) : nullptr;
        if (b->dib && (!buf || size >= (INT)sizeof(DIBSECTION))) {
            if (!buf) return sizeof(DIBSECTION);
            DIBSECTION ds;
            memset(&ds, 0, sizeof ds);
            ds.dsBm = bm;
            ds.dsBmih.biSize = sizeof(BITMAPINFOHEADER);
            ds.dsBmih.biWidth = bm.bmWidth;
            ds.dsBmih.biHeight = b->bmp->stride < 0 ? bm.bmHeight : -bm.bmHeight;
            ds.dsBmih.biPlanes = 1;
            ds.dsBmih.biBitCount = (WORD)bpp;
            ds.dsBmih.biCompression = BI_RGB;
            ds.dsBmih.biSizeImage = bm.bmWidthBytes * bm.bmHeight;
            memcpy(buf, &ds, sizeof ds);
            return sizeof(DIBSECTION);
        }
        if (!buf) return sizeof(BITMAP);
        if (size < (INT)sizeof(BITMAP)) return 0;
        memcpy(buf, &bm, sizeof bm);
        return sizeof(BITMAP);
    }
    case T_PAL: {
        if (!buf) return sizeof(WORD);
        if (size < (INT)sizeof(WORD)) return 0;
        *(WORD*)buf = 20;
        return sizeof(WORD);
    }
    default: return 0;
    }
}

INT WINAPI GetObjectA(HGDIOBJ h, INT size, void* buf)
{
    Obj* o = (Obj*)h;
    if (!o || !obj_live(o)) return 0;
    if (o->type != T_FONT) return GetObjectW(h, size, buf);
    if (!buf) return sizeof(LOGFONTA);
    LOGFONTA lfa;
    const LOGFONTW& lf = ((Font*)o)->lf;
    memcpy(&lfa, &lf, offsetof(LOGFONTA, lfFaceName));
    std::string face = to_utf8(lf.lfFaceName, -1);
    memset(lfa.lfFaceName, 0, sizeof lfa.lfFaceName);
    memcpy(lfa.lfFaceName, face.c_str(), min(face.size(), (size_t)LF_FACESIZE - 1));
    INT n = min(size, (INT)sizeof(LOGFONTA));
    memcpy(buf, &lfa, n);
    return n;
}

// --- coordinates

BOOL WINAPI SetViewportOrgEx(HDC hdc, INT x, INT y, LPPOINT old)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    if (old) *old = d->viewport_org;
    d->viewport_org = POINT{x, y};
    apply_clip(d);
    return TRUE;
}

BOOL WINAPI GetViewportOrgEx(HDC hdc, LPPOINT pt)
{
    DC* d = dc_of(hdc);
    if (!d || !pt) return FALSE;
    *pt = d->viewport_org;
    return TRUE;
}

BOOL WINAPI OffsetViewportOrgEx(HDC hdc, INT x, INT y, LPPOINT old)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    return SetViewportOrgEx(hdc, d->viewport_org.x + x, d->viewport_org.y + y, old);
}

BOOL WINAPI SetWindowOrgEx(HDC hdc, INT x, INT y, LPPOINT old)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    if (old) *old = d->window_org;
    d->window_org = POINT{x, y};
    apply_clip(d);
    return TRUE;
}

BOOL WINAPI GetWindowOrgEx(HDC hdc, LPPOINT pt)
{
    DC* d = dc_of(hdc);
    if (!d || !pt) return FALSE;
    *pt = d->window_org;
    return TRUE;
}

BOOL WINAPI OffsetWindowOrgEx(HDC hdc, INT x, INT y, LPPOINT old)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    return SetWindowOrgEx(hdc, d->window_org.x + x, d->window_org.y + y, old);
}

BOOL WINAPI GetViewportExtEx(HDC hdc, LPSIZE s)
{
    if (!dc_of(hdc) || !s) return FALSE;
    s->cx = s->cy = 1;
    return TRUE;
}

BOOL WINAPI GetWindowExtEx(HDC hdc, LPSIZE s) { return GetViewportExtEx(hdc, s); }

INT WINAPI SetMapMode(HDC hdc, INT mode)
{
    DC* d = dc_of(hdc);
    if (!d) return 0;
    int old = d->map_mode;
    if (mode != MM_TEXT) GDI_FIXME("SetMapMode(%d): only MM_TEXT\n", mode);
    d->map_mode = mode;
    return old;
}

INT WINAPI GetMapMode(HDC hdc) { DC* d = dc_of(hdc); return d ? d->map_mode : 0; }

BOOL WINAPI DPtoLP(HDC hdc, LPPOINT pts, INT n)
{
    DC* d = dc_of(hdc);
    if (!d || (!pts && n)) return FALSE;
    for (INT i = 0; i < n; i++) {
        pts[i].x += d->window_org.x - d->viewport_org.x;
        pts[i].y += d->window_org.y - d->viewport_org.y;
    }
    return TRUE;
}

BOOL WINAPI LPtoDP(HDC hdc, LPPOINT pts, INT n)
{
    DC* d = dc_of(hdc);
    if (!d || (!pts && n)) return FALSE;
    for (INT i = 0; i < n; i++) {
        pts[i].x += d->viewport_org.x - d->window_org.x;
        pts[i].y += d->viewport_org.y - d->window_org.y;
    }
    return TRUE;
}

INT WINAPI GetGraphicsMode(HDC hdc) { return dc_of(hdc) ? GM_COMPATIBLE : 0; }
INT WINAPI SetGraphicsMode(HDC hdc, INT mode)
{
    if (!dc_of(hdc)) return 0;
    if (mode != GM_COMPATIBLE) GDI_FIXME("SetGraphicsMode(%d) ignored\n", mode);
    return GM_COMPATIBLE;
}

DWORD WINAPI SetLayout(HDC hdc, DWORD layout)
{
    DC* d = dc_of(hdc);
    if (!d) return GDI_ERROR;
    DWORD old = d->layout;
    if (layout & LAYOUT_RTL) GDI_FIXME("SetLayout: LAYOUT_RTL stored, not honoured\n");
    d->layout = layout;
    return old;
}

DWORD WINAPI GetLayout(HDC hdc) { DC* d = dc_of(hdc); return d ? d->layout : GDI_ERROR; }

BOOL WINAPI SetBrushOrgEx(HDC hdc, INT x, INT y, LPPOINT old)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    if (old) *old = d->brush_org;
    d->brush_org = POINT{x, y};
    return TRUE;
}

BOOL WINAPI GetBrushOrgEx(HDC hdc, LPPOINT pt)
{
    DC* d = dc_of(hdc);
    if (!d || !pt) return FALSE;
    *pt = d->brush_org;
    return TRUE;
}

INT WINAPI SetStretchBltMode(HDC hdc, INT mode)
{
    DC* d = dc_of(hdc);
    if (!d || mode < BLACKONWHITE || mode > HALFTONE) return 0;
    int old = d->stretch_mode;
    d->stretch_mode = mode;
    return old;
}
INT WINAPI GetStretchBltMode(HDC hdc) { DC* d = dc_of(hdc); return d ? d->stretch_mode : 0; }

INT WINAPI SetROP2(HDC hdc, INT rop)
{
    DC* d = dc_of(hdc);
    if (!d || rop < R2_BLACK || rop > R2_WHITE) return 0;
    int old = d->rop2;
    d->rop2 = rop;
    return old;
}
INT WINAPI GetROP2(HDC hdc) { DC* d = dc_of(hdc); return d ? d->rop2 : 0; }

INT WINAPI SetPolyFillMode(HDC hdc, INT mode)
{
    DC* d = dc_of(hdc);
    if (!d || (mode != ALTERNATE && mode != WINDING)) return 0;
    int old = d->poly_fill_mode;
    d->poly_fill_mode = mode;
    return old;
}
INT WINAPI GetPolyFillMode(HDC hdc) { DC* d = dc_of(hdc); return d ? d->poly_fill_mode : 0; }

INT WINAPI GetDeviceCaps(HDC hdc, INT cap)
{
    DC* d = dc_of(hdc);
    if (!d) return 0;
    switch (cap) {
    case DRIVERVERSION: return 0x0400;
    case TECHNOLOGY: return DT_RASDISPLAY;
    case HORZSIZE: return d->dc->target->width * 254 / 960;
    case VERTSIZE: return d->dc->target->height * 254 / 960;
    case HORZRES: case DESKTOPHORZRES: return d->dc->target->width;
    case VERTRES: case DESKTOPVERTRES: return d->dc->target->height;
    case BITSPIXEL: return 32;
    case PLANES: return 1;
    case NUMBRUSHES: case NUMPENS: return -1;
    case NUMMARKERS: case NUMFONTS: return 0;
    case NUMCOLORS: return -1;
    case PDEVICESIZE: return 0;
    case CURVECAPS: return CC_CIRCLES | CC_PIE | CC_CHORD | CC_ELLIPSES | CC_WIDE | CC_STYLED |
                           CC_WIDESTYLED | CC_INTERIORS | CC_ROUNDRECT;
    case LINECAPS: return LC_POLYLINE | LC_MARKER | LC_POLYMARKER | LC_WIDE | LC_STYLED |
                          LC_WIDESTYLED | LC_INTERIORS;
    case POLYGONALCAPS: return PC_POLYGON | PC_RECTANGLE | PC_WINDPOLYGON | PC_SCANLINE |
                               PC_WIDE | PC_STYLED | PC_WIDESTYLED | PC_INTERIORS;
    case TEXTCAPS: return TC_OP_CHARACTER | TC_OP_STROKE | TC_CP_STROKE | TC_CR_ANY | TC_SF_X_YINDEP |
                          TC_SA_DOUBLE | TC_SA_INTEGER | TC_SA_CONTIN | TC_UA_ABLE | TC_SO_ABLE | TC_RA_ABLE | TC_VA_ABLE;
    case CLIPCAPS: return CP_RECTANGLE;
    case RASTERCAPS: return RC_BITBLT | RC_BITMAP64 | RC_GDI20_OUTPUT | RC_DI_BITMAP | RC_DIBTODEV |
                            RC_BIGFONT | RC_STRETCHBLT | RC_STRETCHDIB | RC_DEVBITS;
    case SHADEBLENDCAPS: return SB_CONST_ALPHA | SB_PIXEL_ALPHA | SB_GRAD_RECT | SB_GRAD_TRI;
    case ASPECTX: case ASPECTY: return 36;
    case ASPECTXY: return 51;
    case LOGPIXELSX: case LOGPIXELSY: return 96;
    case SIZEPALETTE: case NUMRESERVED: return 0;
    case COLORRES: return 24;
    case VREFRESH: return 60;
    case BLTALIGNMENT: return 0;
    default: return 0;
    }
}

// --- clipping

INT WINAPI ExtSelectClipRgn(HDC hdc, HRGN hrgn, INT mode)
{
    DC* d = dc_of(hdc);
    if (!d) return ERROR;
    Region* src = region_of(hrgn);
    if (!src) {
        if (mode != RGN_COPY) return ERROR;
        if (d->clip) { delete d->clip; d->clip = nullptr; }
        apply_clip(d);
        return clip_type(d);
    }
    if (mode == RGN_COPY) {
        if (!d->clip) d->clip = new_region();
        d->clip->rgn = src->rgn;
    } else {
        if (mode < RGN_AND || mode > RGN_DIFF) return ERROR;
        if (!d->clip) { d->clip = new_region(); d->clip->rgn.set(huge_rect); }
        combine(d->clip->rgn, d->clip->rgn, src->rgn, mode);
    }
    apply_clip(d);
    return clip_type(d);
}

INT WINAPI SelectClipRgn(HDC hdc, HRGN hrgn) { return ExtSelectClipRgn(hdc, hrgn, RGN_COPY); }

INT WINAPI IntersectClipRect(HDC hdc, INT l, INT t, INT r, INT b)
{
    DC* d = dc_of(hdc);
    if (!d) return ERROR;
    gdi::Point a = lp_to_dp(d, l, t), c = lp_to_dp(d, r, b);
    gdi::Rect rc = ordered(a.x, a.y, c.x, c.y);
    if (!d->clip) { d->clip = new_region(); d->clip->rgn.set(rc); }
    else d->clip->rgn.intersect(rc);
    apply_clip(d);
    return clip_type(d);
}

INT WINAPI ExcludeClipRect(HDC hdc, INT l, INT t, INT r, INT b)
{
    DC* d = dc_of(hdc);
    if (!d) return ERROR;
    gdi::Point a = lp_to_dp(d, l, t), c = lp_to_dp(d, r, b);
    gdi::Rect rc = ordered(a.x, a.y, c.x, c.y);
    if (!d->clip) { d->clip = new_region(); d->clip->rgn.set(huge_rect); }
    d->clip->rgn.subtract(rc);
    apply_clip(d);
    return clip_type(d);
}

INT WINAPI OffsetClipRgn(HDC hdc, INT x, INT y)
{
    DC* d = dc_of(hdc);
    if (!d) return ERROR;
    if (d->clip) { d->clip->rgn.offset(x, y); apply_clip(d); }
    return clip_type(d);
}

INT WINAPI GetClipBox(HDC hdc, LPRECT r)
{
    DC* d = dc_of(hdc);
    if (!d || !r) return ERROR;
    int t = clip_type(d);
    *r = t == NULLREGION ? RECT{0, 0, 0, 0} : RECT_of(gdi::clip_box(d->dc));
    return t;
}

INT WINAPI GetClipRgn(HDC hdc, HRGN hrgn)
{
    DC* d = dc_of(hdc);
    Region* r = region_of(hrgn);
    if (!d || !r) return -1;
    if (!d->clip) return 0;
    r->rgn = d->clip->rgn;
    return 1;
}

BOOL WINAPI RectVisible(HDC hdc, const RECT* r)
{
    DC* d = dc_of(hdc);
    if (!d || !r) return FALSE;
    return gdi::rect_visible(d->dc, ordered(r->left, r->top, r->right, r->bottom));
}

BOOL WINAPI PtVisible(HDC hdc, INT x, INT y)
{
    DC* d = dc_of(hdc);
    if (!d) return FALSE;
    gdi::Point p{x + d->dc->origin.x, y + d->dc->origin.y};
    return d->dc->clip.contains(p);
}

// --- DIB colour tables

UINT WINAPI GetDIBColorTable(HDC hdc, UINT start, UINT count, RGBQUAD* colors)
{
    (void)hdc; (void)start; (void)count; (void)colors;
    return 0;
}

UINT WINAPI SetDIBColorTable(HDC hdc, UINT start, UINT count, const RGBQUAD* colors)
{
    (void)hdc; (void)start; (void)count; (void)colors;
    return 0;
}

} // extern "C"
