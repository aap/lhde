#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <map>
#include <set>
#include "thumbview.h"
#include "uiicons.h"
#include "shell/config.h"
#include "shell/lhtheme.h"
#include "shell/thumbs.h"
#include "gdi/image.h"

namespace thumbview {

static HWND g_list, g_notify;
static int g_size = 96;                          // the squares now
static int g_default = 96;                       // thumbnail_size
static bool g_on;
static bool g_lh;                                // the Longhorn style (thumbview.h)
static bool g_small_slot;                        // in LVSIL_SMALL (List, Details) instead of LVSIL_NORMAL
static HIMAGELIST g_restore_small;               // what the small slot had before
static HIMAGELIST g_il;
static shell::thumbs::Loader* g_loader;
static std::map<std::string, int> g_defaults;    // icon key -> image (icon centred on the window colour)
static std::map<std::string, int> g_done;        // key(path, mtime) -> image, for the folder shown
static std::map<std::string, int64_t> g_asked;   // path -> the mtime requested (F5 asks again for changed files)
static std::map<std::string, std::pair<int, int64_t>> g_where;   // path -> list index and mtime, as last filled

static std::string key(const std::string& path, int64_t mtime) { return path + '\n' + std::to_string((long long)mtime); }
static std::map<std::string, std::string> g_keys; // folder path -> its icon key

static void notify(void*) { PostMessageW(g_notify, WM_THUMBS, 0, 0); }   // a worker thread

int size() { return g_size; }
int default_size() { return g_default; }
bool active() { return g_on; }

void init(HWND list, HWND notify_window)
{
    g_list = list;
    g_notify = notify_window;
    int s = atoi(shell::setting("thumbnail_size", "96").c_str());
    g_size = g_default = s >= 32 && s <= 256 ? s : 96;
}

// ---- composing the square images ----------------------------------------------------

struct Square {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    uint32_t* bits = nullptr;   // top-down 0xAARRGGBB, straight alpha
};

static COLORREF background()
{
    COLORREF c = (COLORREF)SendMessageW(g_list, LVM_GETBKCOLOR, 0, 0);
    return c == CLR_NONE ? GetSysColor(COLOR_WINDOW) : c;
}

static bool begin(Square& sq)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = g_size;
    bi.bmiHeader.biHeight = -g_size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    sq.bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!sq.bmp || !bits) return false;
    sq.bits = (uint32_t*)bits;
    sq.dc = CreateCompatibleDC(nullptr);
    sq.old = (HBITMAP)SelectObject(sq.dc, sq.bmp);
    // XP: the window colour; Longhorn: nothing behind the image
    COLORREF bk = background();
    uint32_t px = g_lh ? 0 : 0xff000000u | (uint32_t)GetRValue(bk) << 16 | (uint32_t)GetGValue(bk) << 8 | GetBValue(bk);
    for (int i = 0; i < g_size * g_size; i++) sq.bits[i] = px;
    return true;
}

// p (straight alpha) over q (straight alpha)
static inline uint32_t over(uint32_t p, uint32_t q)
{
    uint32_t a = p >> 24, qa = q >> 24;
    if (a == 255 || !qa) return p;
    if (!a) return q;
    uint32_t oa = a + qa * (255 - a) / 255;   // > 0
    uint32_t r = (((p >> 16) & 255) * a * 255 + ((q >> 16) & 255) * qa * (255 - a) + oa * 127) / (oa * 255);
    uint32_t g = (((p >> 8) & 255) * a * 255 + ((q >> 8) & 255) * qa * (255 - a) + oa * 127) / (oa * 255);
    uint32_t b = ((p & 255) * a * 255 + (q & 255) * qa * (255 - a) + oa * 127) / (oa * 255);
    return oa << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
}

// a straight-alpha bitmap over a w x h straight-alpha buffer at (x, y)
static void put_into(uint32_t* dst, int w, int h, const gdi::Bitmap* b, int x, int y)
{
    for (int j = 0; j < b->height; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= h) continue;
        const uint32_t* s = b->row(j);
        uint32_t* d = dst + (size_t)dy * w;
        for (int i = 0; i < b->width; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= w) continue;
            d[dx] = over(s[i], d[dx]);
        }
    }
}

static void put(Square& sq, const gdi::Bitmap* b, int x, int y) { put_into(sq.bits, g_size, g_size, b, x, y); }

static void fill_rect(Square& sq, int x, int y, int w, int h, uint32_t argb)
{
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= g_size) continue;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < g_size) sq.bits[(size_t)j * g_size + i] = over(argb, sq.bits[(size_t)j * g_size + i]);
    }
}

static int finish(Square& sq)
{
    if (!g_lh) for (int i = 0; i < g_size * g_size; i++) sq.bits[i] |= 0xff000000u;   // opaque: no mask needed
    SelectObject(sq.dc, sq.old);
    DeleteDC(sq.dc);
    int idx = ImageList_Add(g_il, sq.bmp, nullptr);
    DeleteObject(sq.bmp);
    return idx;
}

// an icon's pixels at px, straight alpha (the mask where it has no alpha); null if none
static gdi::Bitmap* icon_pixels(HICON ic, int px)
{
    if (!ic) return nullptr;
    gdi::Bitmap* out = nullptr;
    ICONINFO ii = {};
    if (!GetIconInfo(ic, &ii)) return nullptr;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = px; bi.bmiHeader.biHeight = -px; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HDC dc = GetDC(nullptr);
    gdi::Bitmap* b = gdi::create_bitmap(px, px);
    if (b && ii.hbmColor && GetDIBits(dc, ii.hbmColor, 0, (UINT)px, b->pixels, &bi, DIB_RGB_COLORS) > 0) {
        bool alpha = false;
        for (int i = 0; i < px * px && !alpha; i++) alpha = (b->pixels[i] >> 24) != 0;
        if (!alpha) {
            std::vector<uint32_t> mask((size_t)px * px);
            bool m = ii.hbmMask && GetDIBits(dc, ii.hbmMask, 0, (UINT)px, mask.data(), &bi, DIB_RGB_COLORS) > 0;
            for (int i = 0; i < px * px; i++) b->pixels[i] = m && (mask[(size_t)i] & 0xffffff) ? 0 : b->pixels[i] | 0xff000000u;
        }
        out = b;
        b = nullptr;
    }
    if (b) gdi::destroy_bitmap(b);
    ReleaseDC(nullptr, dc);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return out;
}

static void draw_icon(Square& sq, const std::string& key, int px, int x, int y)
{
    HICON ic = uiicons::load_icon(key, px);
    if (!ic) return;
    if (g_lh) {   // composed with its alpha (DrawIconEx would flatten it onto the transparency)
        if (gdi::Bitmap* b = icon_pixels(ic, px)) { put(sq, b, x, y); gdi::destroy_bitmap(b); }
    } else DrawIconEx(sq.dc, x, y, ic, px, px, 0, nullptr, DI_NORMAL);
    DestroyIcon(ic);
}

// ---- the Longhorn pictures: the photo frame --------------------------------------------

// The frame around a picture (measured in 4051/4074, the spec's section 2.3; lh4074-195358
// at about 208 px, from the outside: left 251 240 223, top 251 223, right 240 201, bottom
// 251 223 on white, i.e. a soft shadow darker to the right; a 1 px line, 201 at the top,
// 173 elsewhere; a mat graded 255 at the top to 233 at the bottom, about size/28 wide):
// the shadow's inner rings only at smaller sizes, nothing below 32 px.
static int frame_shadow() { return g_size >= 160 ? 3 : g_size >= 64 ? 2 : 1; }
static int frame_mat() { int m = (g_size + 14) / 28; return m < 1 ? 1 : m > 8 ? 8 : m; }
static int frame_border() { return g_size < 32 ? 0 : frame_shadow() + 1 + frame_mat(); }
// the picture's box inside the frame (what the loader is asked for)
static int picture_box() { return g_size - 2 * frame_border(); }

static void framed(Square& sq, const gdi::Bitmap* b)
{
    int bw = frame_border();
    int x = (g_size - b->width) / 2, y = (g_size - b->height) / 2;
    if (bw) {
        int m = frame_mat(), n = frame_shadow();
        // the shadow's alpha per side, outermost ring first (black over the background)
        static const uint8_t left[3] = { 4, 15, 32 }, top[3] = { 0, 4, 32 }, right[3] = { 0, 15, 54 }, bottom[3] = { 0, 4, 32 };
        int L = x - bw, T = y - bw, W = b->width + 2 * bw, H = b->height + 2 * bw;
        for (int i = 0; i < n; i++) {   // ring i from the outside (the inner n of the three)
            int k = 3 - n + i;
            int l = L + i, t = T + i, w = W - 2 * i, h = H - 2 * i;
            if (top[k]) fill_rect(sq, l + 1, t, w - 2, 1, (uint32_t)top[k] << 24);
            if (bottom[k]) fill_rect(sq, l + 1, t + h - 1, w - 2, 1, (uint32_t)bottom[k] << 24);
            if (left[k]) fill_rect(sq, l, t + 1, 1, h - 2, (uint32_t)left[k] << 24);
            if (right[k]) fill_rect(sq, l + w - 1, t + 1, 1, h - 2, (uint32_t)right[k] << 24);
        }
        int l = L + n, t = T + n, w = W - 2 * n, h = H - 2 * n;   // the line
        fill_rect(sq, l, t, w, 1, 0xffc9c9c9u);                   // top (201)
        fill_rect(sq, l, t + h - 1, w, 1, 0xffadadadu);           // bottom, sides (173)
        fill_rect(sq, l, t + 1, 1, h - 2, 0xffadadadu);
        fill_rect(sq, l + w - 1, t + 1, 1, h - 2, 0xffadadadu);
        int ml = l + 1, mt = t + 1, mw = w - 2, mh = h - 2;       // the mat: 255 -> 233 down
        for (int j = 0; j < mh; j++) {
            uint32_t g = 255 - (uint32_t)(22 * j / (mh > 1 ? mh - 1 : 1));
            fill_rect(sq, ml, mt + j, mw, 1, 0xff000000u | g << 16 | g << 8 | g);
        }
        (void)m;
    }
    put(sq, b, x, y);
}

// ---- the Longhorn folder: 4074's photo standing in the open folder --------------------

// shell32 #389 (the back half) and #390 (the front cover) of the build, when it has them
static std::string folder_art(int id)
{
    std::string sys = shell::longhorn_resources();
    return sys.empty() ? "" : "pe:" + sys + "/shell32.dll#" + std::to_string(id);
}

static bool g_art_checked, g_have_art;
static bool have_folder_art()
{
    if (!g_art_checked) {
        g_art_checked = true;
        HICON a = (HICON)uiicons::load_themed(folder_art(389), IMAGE_ICON, 48, 48, 0);
        HICON b = (HICON)uiicons::load_themed(folder_art(390), IMAGE_ICON, 48, 48, 0);
        g_have_art = a && b;
        if (a) DestroyIcon(a);
        if (b) DestroyIcon(b);
    }
    return g_have_art;
}

static void put_art(Square& sq, int id)
{
    HICON ic = (HICON)uiicons::load_themed(folder_art(id), IMAGE_ICON, g_size, g_size, 0);
    if (!ic) return;
    if (gdi::Bitmap* b = icon_pixels(ic, g_size)) { put(sq, b, 0, 0); gdi::destroy_bitmap(b); }
    DestroyIcon(ic);
}

// 4074 shell32 (the spec's section 3.2): on a 512 canvas, the photo is drawn column by
// column as a keystone whose left edge stands at x 204 from y 488 up, with a flat shadow
// of 33 columns on the back wall when the photo is opaque; the canvas is scaled into the
// thumbnail between the back (#389) and the front (#390)
static gdi::Bitmap* photo_layer(const gdi::Bitmap* img)
{
    const int C = 512;
    int W = img->width, H = img->height;
    if (W < 1 || H < 1) return nullptr;
    gdi::Bitmap* out = gdi::create_bitmap(C, C);
    if (!out) return nullptr;
    for (int i = 0; i < C * C; i++) out->pixels[i] = 0;
    double s = fmin(512.0 / H, 396.0 / W), Hn = H * s / 512, Wn = W * s / 396;
    int ltx = 204, lty = 488 - (int)lround(404 * Hn), lby = 488;
    int rbx = 204 + (int)lround(200 * Wn), rby = 488 - (int)lround(84 * Wn);
    int rty = lty - (int)lround((84 - 30 * Hn) * Hn);
    bool opaque = true;
    for (int i = 0; i < W * H && opaque; i++) opaque = (img->pixels[(size_t)(i / W) * img->stride + i % W] >> 24) == 255;
    if (opaque) {   // 0x1E000000 over 33 one-pixel columns at x 170..202
        int top = (lty + 37) << 16, bot = 466 << 16, dbot = ((lby - 466) << 16) / 33;
        for (int i = 0; i < 33; i++) {
            for (int y = top >> 16; y < (top >> 16) + ((bot - top) >> 16); y++)
                if (y >= 0 && y < C) out->pixels[(size_t)y * C + 170 + i] = over(0x1e000000u, out->pixels[(size_t)y * C + 170 + i]);
            top -= 0x5555; bot += dbot;
        }
    }
    int cols = rbx - ltx;
    if (cols < 1) return out;
    int step = (W << 16) / cols;
    int ty = lty << 16, by = lby << 16, dty = ((rty - lty) << 16) / cols, dby = ((rby - lby) << 16) / cols;
    for (int c = 0; c < cols; c++, ty += dty, by += dby) {
        int sx = (c * step) >> 16;
        if (sx >= W) sx = W - 1;
        int y0 = ty >> 16, h = (by - ty) >> 16;
        for (int k = 0; k < h; k++) {
            int y = y0 + k;
            if (y < 0 || y >= C) continue;
            int sy = (int)((int64_t)k * H / h);
            out->pixels[(size_t)y * C + ltx + c] = over(img->row(sy)[sx], out->pixels[(size_t)y * C + ltx + c]);
        }
    }
    return out;
}

static int folder_photo_image(const gdi::Bitmap* photo)
{
    Square sq;
    if (!begin(sq)) return -1;
    put_art(sq, 389);
    if (photo) {
        if (gdi::Bitmap* layer = photo_layer(photo)) {
            if (gdi::Bitmap* small = gdi::resample(layer, g_size, g_size)) { put(sq, small, 0, 0); gdi::destroy_bitmap(small); }
            gdi::destroy_bitmap(layer);
        }
    }
    put_art(sq, 390);
    return finish(sq);
}

// the 4074 folder for folders at this size (else the folder icon with the minis)
static bool folder_photos() { return g_lh && g_size >= 32 && have_folder_art(); }

// ---- default images ---------------------------------------------------------------------

static int folder_image(const std::string& key, const std::vector<gdi::Bitmap*>& minis);

// XP CreateDefaultThumbnail: the SHIL_EXTRALARGE (48 px) icon centred; Longhorn: the icon at full size
static int default_image(const std::string& key, bool folder)
{
    if (folder) {   // the folder thumbnail without pictures
        auto f = g_defaults.find("folder:" + key);
        if (f != g_defaults.end()) return f->second;
        int idx = folder_image(key, {});
        g_defaults["folder:" + key] = idx;
        return idx;
    }
    auto it = g_defaults.find(key);
    if (it != g_defaults.end()) return it->second;
    Square sq;
    int idx = -1;
    if (begin(sq)) {
        std::string k = key.empty() ? "document" : key;
        if (g_lh) draw_icon(sq, k, g_size, 0, 0);
        else {
            int px = g_size >= 48 ? 48 : g_size;
            // flxp: a theme with bigger images (Longhorn's 256 px document) fills
            // more of the tile: 64 or 96, at most two thirds of it, never blown up
            for (int big : { 96, 64 })
                if (big <= g_size * 2 / 3 && uiicons::native_size(k, big) >= big) { px = big; break; }
            draw_icon(sq, k, px, (g_size - px) / 2, (g_size - px) / 2);
        }
        idx = finish(sq);
    }
    g_defaults[key] = idx;
    return idx;
}

// XP folderthumb.cpp: the folder icon at thumbnail size, the pictures in a
// 2x2 layout (96: 40 px cells at x 7/49, y 11/52; scaled for other sizes)
static int mini_size() { return (40 * g_size + 48) / 96; }

static int folder_image(const std::string& key, const std::vector<gdi::Bitmap*>& minis)
{
    // a folder of the theme's own (a custom icon) keeps it; the plain folder is 4074's
    if (folder_photos() && (key.empty() || key == "folder")) return folder_photo_image(minis.empty() ? nullptr : minis[0]);
    Square sq;
    if (!begin(sq)) return -1;
    draw_icon(sq, key.empty() ? "folder" : key, g_size, 0, 0);
    static const int xs[2] = { 7, 49 }, ys[2] = { 11, 52 };
    int cell = mini_size();
    for (size_t i = 0; i < minis.size() && i < 4; i++) {
        const gdi::Bitmap* b = minis[i];
        int cx = xs[i & 1] * g_size / 96, cy = ys[i >> 1] * g_size / 96;
        put(sq, b, cx + (cell - b->width) / 2, cy + (cell - b->height) / 2);
    }
    return finish(sq);
}

static int picture_image(const gdi::Bitmap* b)
{
    Square sq;
    if (!begin(sq)) return -1;
    if (g_lh) framed(sq, b);
    else put(sq, b, (g_size - b->width) / 2, (g_size - b->height) / 2);   // XP CalculateAspectRatio: centred
    return finish(sq);
}

// ---- the view -------------------------------------------------------------------------

static void reset_images()
{
    if (g_il) ImageList_Remove(g_il, -1);
    g_defaults.clear();
    g_done.clear();
}

bool longhorn() { return g_lh; }

void set_longhorn(bool on)
{
    if (on == g_lh) return;
    g_lh = on;
    g_art_checked = false;
    if (g_loader) g_loader->cancel();
    g_asked.clear();
    reset_images();
}

void enter() { enter_at(g_default, false, nullptr, nullptr); }

bool enter_at(int px, bool small_slot, HIMAGELIST large, HIMAGELIST small)
{
    if (px < 16) px = 16;
    if (px > 256) px = 256;
    if (g_on && px == g_size && small_slot == g_small_slot) return false;
    if (g_on) leave(large, small);
    g_on = true;
    g_small_slot = small_slot;
    if (g_il && px != g_size) {   // another zoom: new squares (the list gets the new image list below)
        if (g_loader) g_loader->cancel();
        g_asked.clear();
        g_where.clear();
        reset_images();
        ImageList_Destroy(g_il);
        g_il = nullptr;
    }
    g_size = px;
    if (!g_il) g_il = ImageList_Create(g_size, g_size, ILC_COLOR32, 64, 64);
    if (!g_loader) g_loader = new shell::thumbs::Loader(notify, nullptr);
    if (small_slot) {   // List and Details: the squares in place of the 16 px icons
        g_restore_small = small;
        SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_il);
        return true;
    }
    SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g_il);
    if (g_lh) return true;   // (the browser sets the spacing of the Longhorn views)
    // dvthumbnail.cpp _SetThumbview: border select, the thumbnail image list,
    // spacing cxIcon/4 + size + 1 and 20 more "so people find the background"
    if (g_size >= 48) SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_BORDERSELECT, LVS_EX_BORDERSELECT);
    int cxicon = GetSystemMetrics(SM_CXICON);
    if (g_size >= 48) SendMessageW(g_list, LVM_SETICONSPACING, 0, MAKELPARAM(cxicon / 4 + g_size + 1 + 20, 0));
    else SendMessageW(g_list, LVM_SETICONSPACING, 0, MAKELPARAM(g_size <= 32 ? 75 : g_size + 43, g_size + 43));
    return true;
}

void leave(HIMAGELIST large, HIMAGELIST small)
{
    if (!g_on) return;
    g_on = false;
    if (g_loader) g_loader->cancel();
    g_asked.clear();
    if (g_small_slot) {
        SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)(small ? small : g_restore_small));
        g_small_slot = false;
        return;
    }
    if (!g_lh) {
        SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_BORDERSELECT, 0);
        SendMessageW(g_list, LVM_SETICONSPACING, 0, (LPARAM)-1);
    }
    SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)large);
}

bool busy() { return g_on && g_loader && g_loader->busy(); }

void stop()
{
    if (g_loader) g_loader->cancel();   // what was asked stays asked: the default images stay until F5
}

void folder_changed()
{
    if (g_loader) g_loader->cancel();
    g_asked.clear();
    g_where.clear();
    g_keys.clear();
    reset_images();
}

static void set_image(int index, int image)
{
    LVITEMW lvi = {};
    lvi.mask = LVIF_IMAGE;
    lvi.iImage = image;
    lvi.iItem = index;
    SendMessageW(g_list, LVM_SETITEMW, 0, (LPARAM)&lvi);
}

void fill(const std::vector<shell::Item>& items)
{
    if (!g_on) return;
    g_where.clear();
    uint32_t gen = g_loader->generation();
    for (size_t i = 0; i < items.size(); i++) {
        const shell::Item& it = items[i];
        g_where[it.path] = std::make_pair((int)i, it.mtime);
        auto d = g_done.find(key(it.path, it.mtime));
        set_image((int)i, d != g_done.end() ? d->second : default_image(it.icon, it.is_folder()));
        auto a = g_asked.find(it.path);
        if (d != g_done.end() || (a != g_asked.end() && a->second == it.mtime) || (it.flags & shell::IsVirtual) || it.path.empty() || it.path[0] != '/') continue;
        if (it.is_folder()) {
            if (g_size < (g_lh ? 32 : 48)) continue;   // the folder's icon: no pictures in it at this size
            g_asked[it.path] = it.mtime;
            g_keys[it.path] = it.icon;
            // 4074's photo: one picture, big enough for the folder's front at this size
            int mini = folder_photos() ? (g_size < 64 ? 64 : g_size > 256 ? 256 : g_size) : mini_size();
            g_loader->request(gen, (int)i, it.path, it.mtime, g_size, true, mini);
        } else if (shell::thumbs::can_thumbnail(it.path)) {
            g_asked[it.path] = it.mtime;
            g_loader->request(gen, (int)i, it.path, it.mtime, g_lh ? picture_box() : g_size);
        }
    }
}

void on_ready()
{
    if (!g_loader) return;
    shell::thumbs::Loader::Result r;
    while (g_loader->take(r)) {
        int image = -1;
        if (r.bmp) image = picture_image(r.bmp);
        else if (!r.minis.empty()) image = folder_image(g_keys[r.path], r.minis);
        shell::thumbs::free_result(r);
        if (image < 0) continue;   // no picture: the default image stays
        int64_t mtime = g_asked.count(r.path) ? g_asked[r.path] : 0;
        g_done[key(r.path, mtime)] = image;
        auto w = g_where.find(r.path);
        if (g_on && w != g_where.end() && w->second.second == mtime) set_image(w->second.first, image);
    }
}

} // namespace thumbview
