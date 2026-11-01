// flxp_shell.h: root window properties for shell programs.
#include <string.h>
#include <vector>
#include <stddef.h>
#include "w32.h"
#include "image.h"
#include "shell/xclip.h"
#include "gdi/png.h"
extern "C" {
#include "flxp_shell.h"
}

static_assert(sizeof(FLXPDROP) == sizeof(win::DropInfo) && offsetof(FLXPDROP, ppszFiles) == offsetof(win::DropInfo, paths) &&
              offsetof(FLXPDROP, hwndSource) == offsetof(win::DropInfo, source) && offsetof(FLXPDROP, cFiles) == offsetof(win::DropInfo, count),
              "FLXPDROP is win::DropInfo");

extern "C" {

UINT WINAPI FlxpWatchRootProperty(const char* name) { return name ? win::watch_root_property(name) : 0; }

INT WINAPI FlxpGetRootCardinals(const char* name, long* values, INT max)
{
    if (!name || !values || max <= 0) return -1;
    return win::root_cardinals(name, values, max);
}

INT WINAPI FlxpGetRootString(const char* name, char* buf, INT cch)
{
    if (!name) return -1;
    std::string v = win::root_string(name);
    if (v.empty()) return -1;
    if (buf && cch > 0) {
        size_t n = v.size() < (size_t)cch - 1 ? v.size() : (size_t)cch - 1;
        memcpy(buf, v.data(), n);
        buf[n] = 0;
    }
    return (INT)v.size();
}

BOOL WINAPI FlxpSetRootString(const char* name, const char* value)
{
    if (!name) return FALSE;
    win::set_root_string(name, value ? value : "");
    return TRUE;
}

DWORD WINAPI FlxpDragFiles(HWND hwnd, const char* const* paths, UINT count, DWORD allowed, HIMAGELIST himl, const INT* images, UINT nimages)
{
    win::Window* w = w32::window_of(hwnd);
    if (!win::is_window(w) || !paths || !count) return 0;
    std::vector<std::string> list(paths, paths + count);
    // the drag image: the icons stacked diagonally (XP draws the selection as laid out)
    gdi::Bitmap* img = nullptr;
    gdi::Point hot{0, 0};
    int cx = 0, cy = 0;
    if (himl && images && nimages && ImageList_GetIconSize(himl, &cx, &cy) && cx > 0 && cy > 0) {
        UINT n = nimages < 4 ? nimages : 4;
        const int step = cx / 4;
        img = gdi::create_bitmap(cx + (int)(n - 1) * step, cy + (int)(n - 1) * step);
        for (int y = 0; y < img->height; y++) memset(img->row(y), 0, (size_t)img->width * 4);
        for (UINT k = n; k-- > 0;) {
            HICON hi = ImageList_GetIcon(himl, images[k], ILD_NORMAL);
            w32::Icon* ic = hi ? w32::icon_of(hi) : nullptr;
            if (ic && ic->image) {
                for (int y = 0; y < cy && y < ic->image->height; y++)
                    for (int x = 0; x < cx && x < ic->image->width; x++) {
                        uint32_t s = ic->image->row(y)[x], a = s >> 24;
                        if (!a) continue;
                        uint32_t& d = img->row(y + (int)k * step)[x + (int)k * step];
                        uint32_t r = ((s >> 16) & 255) * a / 255, g = ((s >> 8) & 255) * a / 255, b = (s & 255) * a / 255;
                        uint32_t ia = 255 - a;   // src over (d premultiplied)
                        uint32_t da = (d >> 24) * ia / 255 + a, dr = ((d >> 16) & 255) * ia / 255 + r,
                                 dg = ((d >> 8) & 255) * ia / 255 + g, db = (d & 255) * ia / 255 + b;
                        d = da << 24 | dr << 16 | dg << 8 | db;
                    }
            }
            if (hi) DestroyIcon(hi);
        }
        hot = gdi::Point{ cx / 2, cy / 2 };
    }
    DWORD r = win::drag_files(w, list, allowed, img, hot);
    if (img) gdi::destroy_bitmap(img);
    return r;
}

BOOL WINAPI FlxpSetSelectionText(BOOL primary, const WCHAR* text, INT len)
{
    std::vector<WCHAR> t;
    if (text) { size_t n = 0; if (len < 0) while (text[n]) n++; else n = (size_t)len; t.assign(text, text + n); }
    t.push_back(0);
    return shell::xclip::set_text(w32::utf8_of(t.data()), primary != FALSE);
}

BOOL WINAPI FlxpClipboardOwned(void) { return shell::xclip::owned(); }

BOOL WINAPI FlxpWritePng(HBITMAP bitmap, const char* path)
{
    w32::Bitmap* b = w32::as<w32::Bitmap>((HANDLE)bitmap, w32::T_BITMAP);
    if (!b || !b->bmp || !path) return FALSE;
    return gdi::write_png(b->bmp, path, {});
}

HGLOBAL WINAPI FlxpEncodePng(HBITMAP bitmap)
{
    w32::Bitmap* b = w32::as<w32::Bitmap>((HANDLE)bitmap, w32::T_BITMAP);
    std::string bytes;
    if (!b || !b->bmp || !gdi::encode_png(b->bmp, bytes, {})) return nullptr;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hg) return nullptr;
    memcpy(GlobalLock(hg), bytes.data(), bytes.size());
    GlobalUnlock(hg);
    return hg;
}

HGLOBAL WINAPI FlxpGetSelectionText(BOOL primary)
{
    std::string s;
    if (!shell::xclip::get_text(s, primary != FALSE)) return nullptr;
    std::u16string w = w32::utf16_of(s.c_str());
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * 2);
    if (!hg) return nullptr;
    memcpy(GlobalLock(hg), w.c_str(), (w.size() + 1) * 2);
    GlobalUnlock(hg);
    return hg;
}

}
