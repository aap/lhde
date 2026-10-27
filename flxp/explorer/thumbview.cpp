#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include "thumbview.h"
#include "uiicons.h"
#include "shell/config.h"
#include "shell/thumbs.h"
#include "gdi/image.h"

namespace thumbview {

static HWND g_list, g_notify;
static int g_size = 96;                          // the squares now
static int g_default = 96;                       // thumbnail_size
static bool g_on;
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
    uint32_t* bits = nullptr;   // top-down 0xAARRGGBB
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
    COLORREF bk = background();
    uint32_t px = 0xff000000u | (uint32_t)GetRValue(bk) << 16 | (uint32_t)GetGValue(bk) << 8 | GetBValue(bk);
    for (int i = 0; i < g_size * g_size; i++) sq.bits[i] = px;
    return true;
}

// straight-alpha bitmap over the square at (x, y)
static void put(Square& sq, const gdi::Bitmap* b, int x, int y)
{
    for (int j = 0; j < b->height; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= g_size) continue;
        const uint32_t* s = b->row(j);
        uint32_t* d = sq.bits + (size_t)dy * g_size;
        for (int i = 0; i < b->width; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= g_size) continue;
            uint32_t p = s[i], a = p >> 24, q = d[dx];
            if (a == 255) { d[dx] = p; continue; }
            if (!a) continue;
            uint32_t r = (((p >> 16) & 255) * a + ((q >> 16) & 255) * (255 - a) + 127) / 255;
            uint32_t g = (((p >> 8) & 255) * a + ((q >> 8) & 255) * (255 - a) + 127) / 255;
            uint32_t bl = ((p & 255) * a + (q & 255) * (255 - a) + 127) / 255;
            d[dx] = 0xff000000u | r << 16 | g << 8 | bl;
        }
    }
}

static int finish(Square& sq)
{
    for (int i = 0; i < g_size * g_size; i++) sq.bits[i] |= 0xff000000u;   // opaque: no mask needed
    SelectObject(sq.dc, sq.old);
    DeleteDC(sq.dc);
    int idx = ImageList_Add(g_il, sq.bmp, nullptr);
    DeleteObject(sq.bmp);
    return idx;
}

static void draw_icon(Square& sq, const std::string& key, int px, int x, int y)
{
    HICON ic = uiicons::load_icon(key, px);
    if (!ic) return;
    DrawIconEx(sq.dc, x, y, ic, px, px, 0, nullptr, DI_NORMAL);
    DestroyIcon(ic);
}

// XP CreateDefaultThumbnail: the SHIL_EXTRALARGE (48 px) icon centred
static int folder_image(const std::string& key, const std::vector<gdi::Bitmap*>& minis);

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
        int px = g_size >= 48 ? 48 : g_size;
        // flxp: a theme with bigger images (Longhorn's 256 px document) fills
        // more of the tile: 64 or 96, at most two thirds of it, never blown up
        std::string k = key.empty() ? "document" : key;
        for (int big : { 96, 64 })
            if (big <= g_size * 2 / 3 && uiicons::native_size(k, big) >= big) { px = big; break; }
        draw_icon(sq, k, px, (g_size - px) / 2, (g_size - px) / 2);
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
    put(sq, b, (g_size - b->width) / 2, (g_size - b->height) / 2);   // XP CalculateAspectRatio: centred
    return finish(sq);
}

// ---- the view -------------------------------------------------------------------------

static void reset_images()
{
    if (g_il) ImageList_Remove(g_il, -1);
    g_defaults.clear();
    g_done.clear();
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
    // dvthumbnail.cpp _SetThumbview: border select, the thumbnail image list,
    // spacing cxIcon/4 + size + 1 and 20 more "so people find the background"
    // (flxp: the border only for squares of 48 and more, the zoom's small ones are icons)
    if (g_size >= 48) SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_BORDERSELECT, LVS_EX_BORDERSELECT);
    SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g_il);
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
    SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_BORDERSELECT, 0);
    SendMessageW(g_list, LVM_SETICONSPACING, 0, (LPARAM)-1);
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
    lvi.iItem = index;
    lvi.iImage = image;
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
            if (g_size < 48) continue;   // the folder's icon: no pictures in it at this size
            g_asked[it.path] = it.mtime;
            g_keys[it.path] = it.icon;
            g_loader->request(gen, (int)i, it.path, it.mtime, g_size, true, mini_size());
        } else if (shell::thumbs::can_thumbnail(it.path)) {
            g_asked[it.path] = it.mtime;
            g_loader->request(gen, (int)i, it.path, it.mtime, g_size);
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
