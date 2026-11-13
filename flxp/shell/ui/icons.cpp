// Shell icons as win32 handles (shell/ui.h): an icon theme's sources (shell/icons.h) loaded with LoadImageW, a PE file's
// resources read as data (one module per file, kept for the process).
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <map>
#include <vector>
#include "shell/icons.h"
#include "shell/namespace.h"
#include "shell/config.h"
#include "w32/w32.h"
#include "shell/ui.h"

namespace shell {

static HINSTANCE module_for(const std::string& path)
{
    static std::map<std::string, HINSTANCE> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    std::u16string w = w32::utf16_of(path.c_str());
    HINSTANCE h = LoadLibraryExW((LPCWSTR)w.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    cache[path] = h;
    return h;
}

// a swatch source (shell::swatch_key): the colour in a square of 3/4 of the size with a 1 px dark grey border, centred,
// transparent around it; straight alpha, as an icon's
static HICON swatch(const std::string& src, int cx, int cy)
{
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = cx;
    unsigned rgb = (unsigned)strtoul(src.c_str() + 7, nullptr, 16);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = -cy;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) return nullptr;
    uint32_t* px = (uint32_t*)bits;
    int s = (cx < cy ? cx : cy) * 3 / 4, x0 = (cx - s) / 2, y0 = (cy - s) / 2;
    for (int y = 0; y < cy; y++)
        for (int x = 0; x < cx; x++) {
            bool in = x >= x0 && x < x0 + s && y >= y0 && y < y0 + s;
            bool edge = in && (x == x0 || y == y0 || x == x0 + s - 1 || y == y0 + s - 1);
            px[y * cx + x] = !in ? 0 : edge ? 0xff505050u : (0xff000000u | (rgb & 0xffffff));
        }
    std::vector<uint8_t> zeros((size_t)((cx + 15) / 16 * 2 * cy));
    HBITMAP mask = CreateBitmap(cx, cy, 1, 1, zeros.data());
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return ic;
}

HANDLE load_image(const std::string& src, UINT type, int cx, int cy, UINT flags)
{
    if (src.empty()) return nullptr;
    if (src.compare(0, 7, "swatch:") == 0) return type == IMAGE_ICON ? swatch(src, cx, cy) : nullptr;
    UINT depth_flag = (type == IMAGE_ICON && icon_theme().depth <= 4) ? LR_VGACOLOR : 0;   // bitmaps keep their colour key
    if (src.rfind("pe:", 0) == 0) {
        size_t hash = src.rfind('#');
        if (hash == std::string::npos) return nullptr;
        HINSTANCE h = module_for(src.substr(3, hash - 3));
        if (!h) return nullptr;
        return LoadImageW(h, MAKEINTRESOURCEW(atoi(src.c_str() + hash + 1)), type, cx, cy, depth_flag | flags);
    }
    std::u16string w = w32::utf16_of(src.c_str());
    return LoadImageW(nullptr, (LPCWSTR)w.c_str(), type, cx, cy, LR_LOADFROMFILE | depth_flag | flags);
}

HICON icon_for_key(const std::string& key, int px)
{
    HICON h = key.empty() ? nullptr : (HICON)load_image(icon_file(key), IMAGE_ICON, px, px);
    if (!h && key != "document") h = (HICON)load_image(icon_file("document"), IMAGE_ICON, px, px);
    return h;
}

HICON icon_for(const std::string& path, int px)
{
    struct stat st;
    bool dir = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    return icon_for_key(icon_key(base_of(path), dir), px);
}

} // namespace shell
