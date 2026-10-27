// Images for the win32 shim: bitmaps, icons and cursors loaded from
// resources or files, DrawIconEx, and the comctl32 image list. Behaviour
// follows Wine's user32/cursoricon.c and comctl32/imagelist.c.
//
// Everything is 32bpp ARGB (non-premultiplied). An icon keeps a single ARGB
// image: the AND mask is folded into alpha, so XOR-inverting cursor pixels
// are approximated as opaque black. An image list keeps one ARGB bitmap per
// image with ILC_MASK likewise folded into alpha.
//
// HINSTANCE is a res::Module*. HINSTANCE NULL means user32's own resources
// (OEM bitmaps, IDI_* icons, IDC_* cursors), which are Wine's
// dlls/user32/resources compiled into src/w32/stdres.res by mkstdres.py and
// linked in with .incbin.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "res/res.h"         // before winuser.h, whose RT_* macros clash with res::RT_*
#include "w32/image.h"
#include "gdi/image.h"
#include "gdi/bmp.h"
#include "w32/flxp_shell.h"
extern "C" {
}

asm(".section .rodata\n"
    ".balign 16\n"
    ".global flxp_stdres_begin\n"
    "flxp_stdres_begin:\n"
    ".incbin \"wux/w32/stdres.res\"\n"
    ".global flxp_stdres_end\n"
    "flxp_stdres_end:\n"
    ".previous\n");
extern "C" const uint8_t flxp_stdres_begin[], flxp_stdres_end[];

namespace w32 {

// Resource types as plain numbers (winuser.h's RT_* are MAKEINTRESOURCE macros).
enum : uint32_t { kRT_CURSOR = 1, kRT_BITMAP = 2, kRT_ICON = 3, kRT_GROUP_CURSOR = 12, kRT_GROUP_ICON = 14 };

// ---------------------------------------------------------------------------
// Small helpers

static std::u16string u16_of(const WCHAR* s)
{
    std::u16string o;
    if (s) while (*s) o += (char16_t)*s++;
    return o;
}

static std::u16string u16_of_a(const char* s)
{
    std::u16string o;
    if (s) while (*s) o += (char16_t)(uint8_t)*s++;   // ANSI as Latin-1
    return o;
}

static std::string utf8_of_u16(const std::u16string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00);
            i++;
        }
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xc0 | c >> 6); o += (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) {
            o += (char)(0xe0 | c >> 12); o += (char)(0x80 | ((c >> 6) & 0x3f)); o += (char)(0x80 | (c & 0x3f));
        } else {
            o += (char)(0xf0 | c >> 18); o += (char)(0x80 | ((c >> 12) & 0x3f));
            o += (char)(0x80 | ((c >> 6) & 0x3f)); o += (char)(0x80 | (c & 0x3f));
        }
    }
    return o;
}

// A resource name: MAKEINTRESOURCE, "#123", or a string.
static res::Id id_of(const std::u16string& s)
{
    if (s.size() > 1 && s[0] == '#') {
        uint32_t n = 0;
        for (size_t i = 1; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++) n = n * 10 + (s[i] - '0');
        return res::Id(n);
    }
    return res::Id(s.c_str());
}

static uint16_t rd16(const uint8_t* p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return !out.empty();
}

res::Module* std_module()
{
    static res::Module* m = res::load_memory(flxp_stdres_begin, flxp_stdres_end - flxp_stdres_begin);
    return m;
}

static res::Module* module_of(HINSTANCE h) { return h ? (res::Module*)h : std_module(); }

gdi::Bitmap* copy_bitmap(const gdi::Bitmap* b)
{
    gdi::Bitmap* o = gdi::create_bitmap(b->width, b->height);
    for (int y = 0; y < b->height; y++) memcpy(o->row(y), b->row(y), b->width * 4);
    return o;
}

static gdi::Bitmap* sub_bitmap(const gdi::Bitmap* b, const gdi::Rect& r)
{
    gdi::Bitmap* o = gdi::create_bitmap(r.width(), r.height());
    for (int y = 0; y < r.height(); y++) {
        uint32_t* d = o->row(y);
        int sy = r.top + y;
        for (int x = 0; x < r.width(); x++) {
            int sx = r.left + x;
            d[x] = (sx >= 0 && sy >= 0 && sx < b->width && sy < b->height) ? b->row(sy)[sx] : 0;
        }
    }
    return o;
}

static gdi::Bitmap* scale_rect(const gdi::Bitmap* b, const gdi::Rect& sr, int w, int h)
{
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    gdi::Bitmap* o = gdi::create_bitmap(w, h);
    int sw = sr.width(), sh = sr.height();
    for (int y = 0; y < h; y++) {
        const uint32_t* s = b->row(sr.top + (int)((int64_t)y * sh / h));
        uint32_t* d = o->row(y);
        for (int x = 0; x < w; x++) d[x] = s[sr.left + (int)((int64_t)x * sw / w)];
    }
    return o;
}

gdi::Bitmap* scale_bitmap(const gdi::Bitmap* b, int w, int h)
{
    if (w == b->width && h == b->height) return copy_bitmap(b);
    return scale_rect(b, b->rect(), w, h);
}

Bitmap* new_bitmap(gdi::Bitmap* b, bool dib, int bpp)
{
    Bitmap* o = new Bitmap();
    o->type = T_BITMAP;
    o->stock = false;
    o->bmp = b;
    o->req_bpp = bpp;
    o->dib = dib;
    return o;
}

Icon* new_icon(gdi::Bitmap* argb, bool cursor, gdi::Point hotspot)
{
    Icon* o = new Icon();
    o->type = T_ICON;
    o->stock = false;
    o->image = argb;
    o->hotspot = hotspot;
    o->is_cursor = cursor;
    return o;
}

static void free_icon(Icon* ic)
{
    gdi::destroy_bitmap(ic->image);
    ic->type = 0;
    delete ic;
}

static bool any_alpha(const gdi::Bitmap* b)
{
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++)
            if (b->row(y)[x] >> 24) return true;
    return false;
}

static void set_opaque(gdi::Bitmap* b)
{
    for (int y = 0; y < b->height; y++)
        for (int x = 0; x < b->width; x++) b->row(y)[x] |= 0xff000000;
}

// Maps a logical point of a w32::DC onto its gdi::DC for the duration of a
// draw: device = logical - window_org + viewport_org, target = device + base_origin.
namespace {
struct OriginScope {
    gdi::DC* g;
    gdi::Point saved;
    explicit OriginScope(DC* d) : g(d->dc), saved(d->dc->origin) {
        g->origin = gdi::Point{d->base_origin.x + d->viewport_org.x - d->window_org.x,
                               d->base_origin.y + d->viewport_org.y - d->window_org.y};
    }
    ~OriginScope() { g->origin = saved; }
};
}

void draw_argb(gdi::DC* dc, int x, int y, int w, int h, const gdi::Bitmap* src, const gdi::Rect& sr, uint8_t alpha)
{
    if (w <= 0 || h <= 0 || sr.empty()) return;
    if (w == sr.width() && h == sr.height()) {
        gdi::blit_alpha(dc, gdi::Point{x, y}, src, sr, alpha);
        return;
    }
    gdi::Bitmap* t = scale_rect(src, sr, w, h);
    gdi::blit_alpha(dc, gdi::Point{x, y}, t, t->rect(), alpha);
    gdi::destroy_bitmap(t);
}

// ---------------------------------------------------------------------------
// DIB headers

struct DibInfo {
    int hdr;            // header size (12 core, 40+ info)
    int width, height;  // height signed as stored
    int bpp;
    uint32_t compression;
    int colors;         // palette entries
    int palette_bytes;  // including BI_BITFIELDS masks for a 40-byte header
};

static bool dib_info(const uint8_t* d, size_t n, DibInfo& o)
{
    if (n < 12) return false;
    o.hdr = rd32(d);
    if (o.hdr == 12) {
        o.width = rd16(d + 4);
        o.height = (int16_t)rd16(d + 6);
        o.bpp = rd16(d + 10);
        o.compression = BI_RGB;
        o.colors = o.bpp <= 8 ? 1 << o.bpp : 0;
        o.palette_bytes = o.colors * 3;
        return true;
    }
    if (o.hdr < 40 || n < 40) return false;
    o.width = (int32_t)rd32(d + 4);
    o.height = (int32_t)rd32(d + 8);
    o.bpp = rd16(d + 14);
    o.compression = rd32(d + 16);
    uint32_t used = rd32(d + 32);
    o.colors = o.bpp <= 8 ? (used && used <= 256u ? (int)used : 1 << o.bpp) : (used <= 256u ? (int)used : 0);
    o.palette_bytes = o.colors * 4;
    if (o.compression == BI_BITFIELDS && o.hdr == 40) o.palette_bytes += 12;
    return true;
}

// LR_LOADTRANSPARENT / LR_LOADMAP3DCOLORS, applied after decoding. Wine
// patches the palette (<= 8bpp only); we replace the matching pixels.
static void fix_colors(gdi::Bitmap* b, const DibInfo& di, UINT flags)
{
    if (di.bpp > 8 || !(flags & (LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS))) return;
    uint32_t window = pack(color_of(GetSysColor(COLOR_WINDOW)));
    uint32_t shadow = pack(color_of(GetSysColor(COLOR_3DSHADOW)));
    uint32_t face = pack(color_of(GetSysColor(COLOR_3DFACE)));
    uint32_t light = pack(color_of(GetSysColor(COLOR_3DLIGHT)));
    // The first pixel in the bits is the bottom-left one for a bottom-up DIB.
    uint32_t first = 0;
    if (b->width && b->height) first = b->row(di.height > 0 ? b->height - 1 : 0)[0] & 0xffffff;
    uint32_t tcol = (flags & LR_LOADMAP3DCOLORS) ? face : window;
    for (int y = 0; y < b->height; y++) {
        uint32_t* p = b->row(y);
        for (int x = 0; x < b->width; x++) {
            uint32_t c = p[x] & 0xffffff;
            if ((flags & LR_LOADTRANSPARENT) && c == first) p[x] = tcol;
            else if (flags & LR_LOADMAP3DCOLORS) {
                if (c == 0x808080) p[x] = shadow;
                else if (c == 0xc0c0c0) p[x] = face;
                else if (c == 0xdfdfdf) p[x] = light;
            }
        }
    }
}

// Decode an RT_BITMAP blob or a .bmp file.
static gdi::Bitmap* decode_bitmap(const uint8_t* d, size_t n, UINT flags, int* bpp)
{
    DibInfo di;
    const uint8_t* h = (n >= 14 && d[0] == 'B' && d[1] == 'M') ? d + 14 : d;
    if (!dib_info(h, n - (h - d), di)) return nullptr;
    gdi::Bitmap* b = gdi::decode_bmp(d, n);
    if (!b) return nullptr;
    fix_colors(b, di, flags);
    if (bpp) *bpp = di.bpp;
    return b;
}

// Decode an icon/cursor image (BITMAPINFOHEADER with doubled height, colour
// bits, then the 1bpp AND mask, or a PNG) into ARGB: gdi/image.
static gdi::Bitmap* decode_icon_image(const uint8_t* d, size_t n, bool cursor = false) { return gdi::decode_icon_image(d, n, cursor); }

// ---------------------------------------------------------------------------
// Icon directories

struct DirEntry {
    int width, height, bpp;
    uint32_t id;                    // resource id (RT_ICON/RT_CURSOR) for resources
    const uint8_t* data; size_t size;   // image data for files
    gdi::Point hotspot;             // files: from the directory entry
};

// CURSORICON_FindBestIcon, flxp: the exact size, else the smallest larger
// one (shrunk with a box filter by make_icon), else the largest smaller one:
// a 64 px request from 48/128 gets the 128 (Wine/Windows: the nearest, the
// 48 blown up). Equal sizes are chosen by depth as before.
static int best_icon(const std::vector<DirEntry>& e, int width, int height, int depth, UINT flags)
{
    if (e.empty()) return -1;
    uint32_t total = 0xffffffff, xdiff = 0, ydiff = 0, cdiff = 0xffffffff;
    if (flags & LR_DEFAULTSIZE) {
        if (!width) width = 32;
        if (!height) height = 32;
    } else if (!width && !height) {
        width = e[0].width;
        height = e[0].height;
        total = 0;
    }
    if (total) {
        // flxp: larger ones (not smaller) count first; among them the nearest
        int bw = -1, bh = -1;
        for (auto& x : e)
            if (x.width >= width && x.height >= height && (bw < 0 || x.width + x.height < bw + bh)) { bw = x.width; bh = x.height; }
        if (bw < 0)
            for (auto& x : e)
                if (bw < 0 || x.width + x.height > bw + bh) { bw = x.width; bh = x.height; }
        xdiff = (uint32_t)abs(width - bw); ydiff = (uint32_t)abs(height - bh);
    }
    int best = -1;
    for (size_t i = 0; i < e.size(); i++) {
        if ((uint32_t)abs(width - e[i].width) == xdiff && (uint32_t)abs(height - e[i].height) == ydiff) {
            uint32_t c = abs(depth - e[i].bpp);
            if (cdiff > c) { best = (int)i; cdiff = c; }
        }
    }
    return best;
}

// CURSORICON_FindBestCursor
static int best_cursor(const std::vector<DirEntry>& e, int width, int height, UINT flags)
{
    if (e.empty()) return -1;
    if (flags & LR_DEFAULTSIZE) {
        if (!width) width = 32;
        if (!height) height = 32;
    } else if (!width && !height) return 0;
    int best = -1, mw = 0, mh = 0, mb = 0;
    for (size_t i = 0; i < e.size(); i++) {
        const DirEntry& x = e[i];
        if (x.width > width || x.height > height) continue;
        if (x.width < mw || x.height < mh) continue;
        if (x.width == mw && x.height == mh && x.bpp <= mb) continue;
        best = (int)i; mw = x.width; mh = x.height; mb = x.bpp;
    }
    if (best != -1) return best;
    mw = mh = 255; mb = 0;
    for (size_t i = 0; i < e.size(); i++) {
        const DirEntry& x = e[i];
        if (x.width > mw || x.height > mh) continue;
        if (x.width == mw && x.height == mh && x.bpp <= mb) continue;
        best = (int)i; mw = x.width; mh = x.height; mb = x.bpp;
    }
    return best == -1 ? 0 : best;
}

// Final icon size: requested, LR_DEFAULTSIZE, or the image's own.
static void final_size(int& cx, int& cy, int nw, int nh, UINT flags)
{
    if (!cx) cx = (flags & LR_DEFAULTSIZE) ? 32 : nw;
    if (!cy) cy = (flags & LR_DEFAULTSIZE) ? 32 : nh;
}

// flxp: shrinking an icon averages the source pixels each target pixel
// covers (alpha-weighted colour), where nearest-pixel sampling dropped rows
// and columns (a 128 px Longhorn icon at 96 or 48 lost its outline)
static gdi::Bitmap* shrink_box(const gdi::Bitmap* b, int w, int h)
{
    gdi::Bitmap* o = gdi::create_bitmap(w, h);
    int sw = b->width, sh = b->height;
    for (int y = 0; y < h; y++) {
        // the source span [y*sh/h, (y+1)*sh/h) in 1/h units, partial rows weighted
        int64_t y0 = (int64_t)y * sh, y1 = (int64_t)(y + 1) * sh;
        uint32_t* d = o->row(y);
        for (int x = 0; x < w; x++) {
            int64_t x0 = (int64_t)x * sw, x1 = (int64_t)(x + 1) * sw;
            uint64_t sa = 0, sr = 0, sg = 0, sb = 0, area = 0;
            for (int64_t sy = y0 / h; sy * h < y1 && sy < sh; sy++) {
                int64_t wy = (sy + 1) * h < y1 ? (sy + 1) * h : y1;
                wy -= sy * h > y0 ? sy * h : y0;
                const uint32_t* s = b->row((int)sy);
                for (int64_t sx = x0 / w; sx * w < x1 && sx < sw; sx++) {
                    int64_t wx = (sx + 1) * w < x1 ? (sx + 1) * w : x1;
                    wx -= sx * w > x0 ? sx * w : x0;
                    uint64_t wt = (uint64_t)(wx * wy);
                    uint32_t p = s[sx];
                    uint64_t a = p >> 24;
                    area += wt; sa += a * wt;
                    sr += ((p >> 16) & 0xff) * a * wt; sg += ((p >> 8) & 0xff) * a * wt; sb += (p & 0xff) * a * wt;
                }
            }
            uint32_t a = area ? (uint32_t)((sa + area / 2) / area) : 0;
            uint32_t r = sa ? (uint32_t)((sr + sa / 2) / sa) : 0, g = sa ? (uint32_t)((sg + sa / 2) / sa) : 0, bl = sa ? (uint32_t)((sb + sa / 2) / sa) : 0;
            d[x] = a << 24 | r << 16 | g << 8 | bl;
        }
    }
    return o;
}

static Icon* make_icon(gdi::Bitmap* img, bool cursor, gdi::Point hot, int cx, int cy, UINT flags)
{
    if (flags & LR_FLXP_NATIVESIZE) { cx = cy = 0; flags &= ~LR_DEFAULTSIZE; }   // flxp_shell.h
    final_size(cx, cy, img->width, img->height, flags);
    if (cx != img->width || cy != img->height) {
        bool alpha = any_alpha(img);
        gdi::Bitmap* s = !cursor && alpha && cx < img->width && cy < img->height ? shrink_box(img, cx, cy) : scale_bitmap(img, cx, cy);
        hot.x = hot.x * cx / img->width;
        hot.y = hot.y * cy / img->height;
        gdi::destroy_bitmap(img);
        img = s;
    }
    if (!cursor) hot = gdi::Point{cx / 2, cy / 2};
    return new_icon(img, cursor, hot);
}

static Icon* load_icon_res(res::Module* m, const res::Id& name, bool cursor, int cx, int cy, UINT flags)
{
    res::Blob g;
    if (!m || !res::find(m, cursor ? kRT_GROUP_CURSOR : kRT_GROUP_ICON, name, g)) return nullptr;
    if (g.size < 6) return nullptr;
    int n = rd16(g.data + 4);
    std::vector<DirEntry> e;
    for (int i = 0; i < n && 6 + 14 * (size_t)(i + 1) <= g.size; i++) {
        const uint8_t* p = g.data + 6 + 14 * i;
        DirEntry x = {};
        if (cursor) {
            x.width = rd16(p);
            x.height = rd16(p + 2) / 2;
        } else {
            x.width = p[0];
            x.height = p[1];
            if (!x.width && !x.height) x.width = x.height = 256;
        }
        x.bpp = rd16(p + 6);
        x.id = rd16(p + 12);
        e.push_back(x);
    }
    int i = cursor ? best_cursor(e, cx, cy, flags) : best_icon(e, cx, cy, (flags & LR_VGACOLOR) ? 4 : 32, flags);
    if (i < 0) return nullptr;
    res::Blob b;
    if (!res::find(m, cursor ? kRT_CURSOR : kRT_ICON, res::Id(e[i].id), b)) return nullptr;
    const uint8_t* d = b.data;
    size_t sz = b.size;
    gdi::Point hot = {0, 0};
    if (cursor) {
        if (sz < 4) return nullptr;
        hot = gdi::Point{rd16(d), rd16(d + 2)};
        d += 4; sz -= 4;
    }
    gdi::Bitmap* img = decode_icon_image(d, sz, cursor);
    if (!img) return nullptr;
    return make_icon(img, cursor, hot, cx, cy, flags);
}

// A .ico or .cur file.
static Icon* load_icon_file(const std::vector<uint8_t>& f, bool cursor, int cx, int cy, UINT flags)
{
    if (f.size() < 6 || rd16(f.data()) != 0) return nullptr;
    int type = rd16(f.data() + 2), n = rd16(f.data() + 4);
    if (type != 1 && type != 2) return nullptr;
    std::vector<DirEntry> e;
    for (int i = 0; i < n && 6 + 16 * (size_t)(i + 1) <= f.size(); i++) {
        const uint8_t* p = f.data() + 6 + 16 * i;
        DirEntry x = {};
        x.width = p[0] ? p[0] : 256;
        x.height = p[1] ? p[1] : 256;
        uint32_t size = rd32(p + 8), off = rd32(p + 12);
        if ((size_t)off + size > f.size()) continue;
        x.data = f.data() + off;
        x.size = size;
        x.bpp = type == 1 ? rd16(p + 6) : 0;
        if (type == 2) x.hotspot = gdi::Point{rd16(p + 4), rd16(p + 6)};
        if (!x.bpp && size >= 16 && memcmp(x.data, "\x89PNG", 4)) x.bpp = rd16(x.data + 14);
        e.push_back(x);
    }
    int i = cursor ? best_cursor(e, cx, cy, flags) : best_icon(e, cx, cy, (flags & LR_VGACOLOR) ? 4 : 32, flags);
    if (i < 0) return nullptr;
    gdi::Bitmap* img = decode_icon_image(e[i].data, e[i].size, cursor);
    if (!img) return nullptr;
    return make_icon(img, type == 2, e[i].hotspot, cx, cy, flags);
}

// ---------------------------------------------------------------------------
// LoadImage

struct CacheEntry {
    res::Module* module;
    res::Id name;
    std::string path;
    UINT type;
    int cx, cy;
    HANDLE handle;
};
static std::vector<CacheEntry> shared_cache;

static HANDLE load_image(HINSTANCE hinst, const res::Id& name, const std::string& path,
                         UINT type, int cx, int cy, UINT flags)
{
    res::Module* m = (flags & LR_LOADFROMFILE) ? nullptr : module_of(hinst);
    // Resources of user32 itself are always shared.
    if (!hinst && !(flags & LR_LOADFROMFILE) && type != IMAGE_BITMAP) flags |= LR_SHARED;
    if (flags & LR_SHARED) {
        for (auto& c : shared_cache)
            if (c.module == m && c.type == type && c.cx == cx && c.cy == cy &&
                ((flags & LR_LOADFROMFILE) ? c.path == path : (c.path.empty() && c.name == name)))
                return c.handle;
    }

    HANDLE h = nullptr;
    if (type == IMAGE_BITMAP) {
        gdi::Bitmap* b = nullptr;
        int bpp = 32;
        if (flags & LR_LOADFROMFILE) {
            std::vector<uint8_t> f;
            if (read_file(path, f)) b = decode_bitmap(f.data(), f.size(), flags, &bpp);
        } else {
            res::Blob blob;
            if (m && res::find(m, kRT_BITMAP, name, blob)) b = decode_bitmap(blob.data, blob.size, flags, &bpp);
        }
        if (!b) return nullptr;
        if ((cx && cx != b->width) || (cy && cy != b->height)) {
            gdi::Bitmap* s = scale_bitmap(b, cx ? cx : b->width, cy ? cy : b->height);
            gdi::destroy_bitmap(b);
            b = s;
        }
        if (flags & LR_MONOCHROME) {
            for (int y = 0; y < b->height; y++)
                for (int x = 0; x < b->width; x++) {
                    gdi::Color c = gdi::unpack(b->row(y)[x]);
                    b->row(y)[x] = (c.r * 30 + c.g * 59 + c.b * 11) / 100 >= 128 ? 0xffffffff : 0xff000000;
                }
            bpp = 1;
        }
        h = (HANDLE)new_bitmap(b, (flags & LR_CREATEDIBSECTION) != 0, bpp);
    } else if (type == IMAGE_ICON || type == IMAGE_CURSOR) {
        bool cursor = type == IMAGE_CURSOR;
        Icon* ic = nullptr;
        if (flags & LR_LOADFROMFILE) {
            std::vector<uint8_t> f;
            if (read_file(path, f)) ic = load_icon_file(f, cursor, cx, cy, flags);
        } else {
            ic = load_icon_res(m, name, cursor, cx, cy, flags);
        }
        if (!ic) return nullptr;
        h = (HANDLE)ic;
    } else {
        return nullptr;
    }

    if (flags & LR_SHARED) {
        ((Obj*)h)->stock = true;            // DestroyIcon/DeleteObject leave it alone
        shared_cache.push_back(CacheEntry{m, name, (flags & LR_LOADFROMFILE) ? path : std::string(), type, cx, cy, h});
    }
    return h;
}

} // namespace w32

using namespace w32;

extern "C" {

HANDLE WINAPI LoadImageW(HINSTANCE hinst, LPCWSTR name, UINT type, INT cx, INT cy, UINT flags)
{
    if (flags & LR_LOADFROMFILE) {
        if (IS_INTRESOURCE(name)) return nullptr;
        return load_image(hinst, res::Id(), utf8_of_u16(u16_of(name)), type, cx, cy, flags);
    }
    res::Id id = IS_INTRESOURCE(name) ? res::Id((uint32_t)(uintptr_t)name) : id_of(u16_of(name));
    return load_image(hinst, id, std::string(), type, cx, cy, flags);
}

HANDLE WINAPI LoadImageA(HINSTANCE hinst, LPCSTR name, UINT type, INT cx, INT cy, UINT flags)
{
    if (flags & LR_LOADFROMFILE) {
        if (IS_INTRESOURCE(name)) return nullptr;
        return load_image(hinst, res::Id(), std::string(name), type, cx, cy, flags);
    }
    res::Id id = IS_INTRESOURCE(name) ? res::Id((uint32_t)(uintptr_t)name) : id_of(u16_of_a(name));
    return load_image(hinst, id, std::string(), type, cx, cy, flags);
}

HBITMAP WINAPI LoadBitmapW(HINSTANCE hinst, LPCWSTR name)
{
    return (HBITMAP)LoadImageW(hinst, name, IMAGE_BITMAP, 0, 0, 0);
}

HBITMAP WINAPI LoadBitmapA(HINSTANCE hinst, LPCSTR name)
{
    return (HBITMAP)LoadImageA(hinst, name, IMAGE_BITMAP, 0, 0, 0);
}

HICON WINAPI LoadIconW(HINSTANCE hinst, LPCWSTR name)
{
    return (HICON)LoadImageW(hinst, name, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

HICON WINAPI LoadIconA(HINSTANCE hinst, LPCSTR name)
{
    return (HICON)LoadImageA(hinst, name, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

HCURSOR WINAPI LoadCursorW(HINSTANCE hinst, LPCWSTR name)
{
    return (HCURSOR)LoadImageW(hinst, name, IMAGE_CURSOR, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

HCURSOR WINAPI LoadCursorA(HINSTANCE hinst, LPCSTR name)
{
    return (HCURSOR)LoadImageA(hinst, name, IMAGE_CURSOR, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

BOOL WINAPI DestroyIcon(HICON h)
{
    Icon* ic = icon_of(h);
    if (!ic) return FALSE;
    if (!ic->stock) free_icon(ic);
    return TRUE;
}

BOOL WINAPI DestroyCursor(HCURSOR h)
{
    return DestroyIcon((HICON)h);
}

HANDLE WINAPI CopyImage(HANDLE h, UINT type, INT cx, INT cy, UINT flags)
{
    if (type == IMAGE_BITMAP) {
        Bitmap* b = as<Bitmap>(h, T_BITMAP);
        if (!b || !b->bmp) return nullptr;
        int w = cx ? cx : b->bmp->width, hh = cy ? cy : b->bmp->height;
        bool same = w == b->bmp->width && hh == b->bmp->height;
        if ((flags & LR_COPYRETURNORG) && same && !(flags & LR_CREATEDIBSECTION)) return h;
        bool dib = (flags & LR_CREATEDIBSECTION) ? true : ((flags & LR_COPYRETURNORG) ? b->dib : false);
        Bitmap* o = new_bitmap(scale_bitmap(b->bmp, w, hh), dib, b->req_bpp);
        if ((flags & LR_COPYDELETEORG) && !b->stock) DeleteObject((HGDIOBJ)h);
        return (HANDLE)o;
    }
    if (type == IMAGE_ICON || type == IMAGE_CURSOR) {
        Icon* ic = icon_of((HICON)h);
        if (!ic) return nullptr;
        int w = cx, hh = cy;
        final_size(w, hh, ic->image->width, ic->image->height, flags);
        bool same = w == ic->image->width && hh == ic->image->height;
        if ((flags & LR_COPYRETURNORG) && same) return h;
        gdi::Point hot = ic->hotspot;
        hot.x = hot.x * w / ic->image->width;
        hot.y = hot.y * hh / ic->image->height;
        Icon* o = new_icon(scale_bitmap(ic->image, w, hh), ic->is_cursor, hot);
        if (flags & LR_COPYDELETEORG) DestroyIcon((HICON)h);
        return (HANDLE)o;
    }
    return nullptr;
}

BOOL WINAPI GetIconInfo(HICON h, PICONINFO info)
{
    Icon* ic = icon_of(h);
    if (!ic || !info) return FALSE;
    info->fIcon = !ic->is_cursor;
    info->xHotspot = ic->hotspot.x;
    info->yHotspot = ic->hotspot.y;
    gdi::Bitmap* mask = gdi::create_bitmap(ic->image->width, ic->image->height);
    for (int y = 0; y < mask->height; y++)
        for (int x = 0; x < mask->width; x++)
            mask->row(y)[x] = (ic->image->row(y)[x] >> 24) < 128 ? 0xffffffff : 0xff000000;
    info->hbmColor = (HBITMAP)new_bitmap(copy_bitmap(ic->image), false, 32);
    info->hbmMask = (HBITMAP)new_bitmap(mask, false, 1);
    return TRUE;
}

HICON WINAPI CreateIconIndirect(PICONINFO info)
{
    if (!info) return nullptr;
    Bitmap* color = as<Bitmap>(info->hbmColor, T_BITMAP);
    Bitmap* mask = as<Bitmap>(info->hbmMask, T_BITMAP);
    if (!mask || !mask->bmp) return nullptr;
    gdi::Bitmap* img;
    if (color && color->bmp) {
        img = copy_bitmap(color->bmp);
        bool alpha = color->req_bpp == 32 && any_alpha(img);
        for (int y = 0; y < img->height; y++) {
            uint32_t* p = img->row(y);
            const uint32_t* m = y < mask->bmp->height ? mask->bmp->row(y) : nullptr;
            for (int x = 0; x < img->width; x++) {
                if (alpha) continue;
                bool transparent = m && x < mask->bmp->width && (m[x] & 0xffffff) != 0;
                uint32_t c = p[x] & 0xffffff;
                if (transparent && c) p[x] = 0xff000000;
                else p[x] = transparent ? 0 : (c | 0xff000000);
            }
        }
    } else {
        // Monochrome: the mask holds AND on top and XOR below.
        int w = mask->bmp->width, hh = mask->bmp->height / 2;
        if (hh <= 0) return nullptr;
        img = gdi::create_bitmap(w, hh);
        for (int y = 0; y < hh; y++)
            for (int x = 0; x < w; x++) {
                bool a = (mask->bmp->row(y)[x] & 0xffffff) != 0;
                bool xr = (mask->bmp->row(y + hh)[x] & 0xffffff) != 0;
                img->row(y)[x] = a ? (xr ? 0xff000000 : 0) : (xr ? 0xffffffff : 0xff000000);
            }
    }
    gdi::Point hot = info->fIcon ? gdi::Point{img->width / 2, img->height / 2}
                                 : gdi::Point{(int32_t)info->xHotspot, (int32_t)info->yHotspot};
    return (HICON)new_icon(img, !info->fIcon, hot);
}

BOOL get_icon_size(HICON h, SIZE* size)
{
    Icon* ic = icon_of(h);
    if (!ic || !size) return FALSE;
    size->cx = ic->image->width;
    size->cy = ic->image->height;
    return TRUE;
}

BOOL WINAPI DrawIconEx(HDC hdc, INT x, INT y, HICON hicon, INT cx, INT cy, UINT step, HBRUSH brush, UINT flags)
{
    (void)step;
    DC* dc = dc_of(hdc);
    Icon* ic = icon_of(hicon);
    if (!dc || !ic) return FALSE;
    const gdi::Bitmap* img = ic->image;
    if (!cx) cx = (flags & DI_DEFAULTSIZE) ? 32 : img->width;
    if (!cy) cy = (flags & DI_DEFAULTSIZE) ? 32 : img->height;
    OriginScope os(dc);
    // Background brush: the flicker-free offscreen is not needed here, just
    // fill first. (HBRUSH)(COLOR_xxx + 1) is accepted as in class brushes.
    if ((uintptr_t)brush > 0 && (uintptr_t)brush <= COLOR_MENUBAR + 1)
        gdi::fill_rect(dc->dc, gdi::rect_xywh(x, y, cx, cy), color_of(GetSysColor((int)(uintptr_t)brush - 1)));
    else if (Brush* br = as<Brush>(brush, T_BRUSH))
        if (br->lb.lbStyle == BS_SOLID)
            gdi::fill_rect(dc->dc, gdi::rect_xywh(x, y, cx, cy), color_of(br->lb.lbColor));
    // Our icons always carry alpha, and Wine alpha-blends whenever DI_IMAGE
    // is set on an alpha icon; DI_MASK alone draws the mask.
    if (flags & DI_IMAGE) {
        draw_argb(dc->dc, x, y, cx, cy, img, img->rect());
    } else if (flags & DI_MASK) {
        gdi::Bitmap* t = copy_bitmap(img);
        for (int yy = 0; yy < t->height; yy++)
            for (int xx = 0; xx < t->width; xx++) {
                uint32_t& p = t->row(yy)[xx];
                p = (p >> 24) < 128 ? 0xffffffff : 0xff000000;
            }
        draw_argb(dc->dc, x, y, cx, cy, t, t->rect());
        gdi::destroy_bitmap(t);
    }
    return TRUE;
}

BOOL WINAPI DrawIcon(HDC hdc, INT x, INT y, HICON hicon)
{
    return DrawIconEx(hdc, x, y, hicon, 0, 0, 0, nullptr, DI_NORMAL | DI_COMPAT | DI_DEFAULTSIZE);
}

} // extern "C"
