#include <stdlib.h>
#include <map>
#include <vector>
#include "uiicons.h"
#include "shell/icons.h"
#include "w32/flxp_shell.h"

namespace uiicons {

static HIMAGELIST g_small, g_large;
static int g_large_px = 32;
static std::map<std::string, int> g_index;
static std::map<std::string, bool> g_own;     // keys whose index holds their own image (not "document"'s)
static std::map<std::string, int> g_native;
// the zoom's other sizes (at_size): one list per size with the same indices,
// filled from g_by_index (the key each index was loaded for)
static std::map<int, HIMAGELIST> g_sized;
static std::vector<std::string> g_by_index;
static int g_generation = -1;

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }

void init(int large_px)
{
    if (g_small) return;
    if (large_px < 16 || large_px > 256) large_px = 32;
    g_large_px = large_px;
    g_small = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
    g_large = ImageList_Create(large_px, large_px, ILC_COLOR32 | ILC_MASK, 16, 16);
    g_generation = shell::icon_theme_generation();
}

bool sync()
{
    shell::refresh_icon_theme();
    int gen = shell::icon_theme_generation();
    if (gen == g_generation) return false;
    g_generation = gen;
    g_native.clear();
    // every index keeps its key: views need no new items, only a repaint
    for (auto& kv : g_own) {
        if (!kv.second) continue;
        int idx = g_index[kv.first];
        std::string src = shell::icon_file(kv.first);
        HICON s = (HICON)load_themed(src, IMAGE_ICON, 16, 16, 0);
        HICON l = (HICON)load_themed(src, IMAGE_ICON, g_large_px, g_large_px, 0);
        if (s) { ImageList_ReplaceIcon(g_small, idx, s); DestroyIcon(s); }
        if (l) { ImageList_ReplaceIcon(g_large, idx, l); DestroyIcon(l); }
        for (auto& sz : g_sized) {
            HICON z = (HICON)load_themed(src, IMAGE_ICON, sz.first, sz.first, 0);
            if (z) { ImageList_ReplaceIcon(sz.second, idx, z); DestroyIcon(z); }
        }
    }
    return true;
}

int large_size() { return g_large_px; }

HIMAGELIST small() { return g_small; }
HIMAGELIST large() { return g_large; }

HIMAGELIST at_size(int px)
{
    if (px == 16) return g_small;
    if (px == g_large_px) return g_large;
    if (px < 8 || px > 256) return g_large;
    auto it = g_sized.find(px);
    if (it != g_sized.end()) return it->second;
    HIMAGELIST il = ImageList_Create(px, px, ILC_COLOR32 | ILC_MASK, 16, 16);
    int n = ImageList_GetImageCount(g_small);
    for (int i = 0; i < n; i++) {
        std::string key = (size_t)i < g_by_index.size() ? g_by_index[(size_t)i] : "";
        HICON z = key.empty() ? nullptr : (HICON)load_themed(shell::icon_file(key), IMAGE_ICON, px, px, 0);
        if (!z) z = ImageList_GetIcon(g_large, i, ILD_NORMAL);   // stretched by the list
        if (z) { ImageList_ReplaceIcon(il, -1, z); DestroyIcon(z); }
    }
    g_sized[px] = il;
    return il;
}

static HINSTANCE module_for(const std::string& path)
{
    static std::map<std::string, HINSTANCE> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    std::u16string w = W(path);
    HINSTANCE h = LoadLibraryExW((LPCWSTR)w.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    cache[path] = h;
    return h;
}

HANDLE load_themed(const std::string& src, UINT type, int cx, int cy, UINT extra)
{
    if (src.empty()) return nullptr;
    UINT depth_flag = (type == IMAGE_ICON && shell::icon_theme().depth <= 4) ? LR_VGACOLOR : 0;   // bitmaps keep their colour key
    if (src.rfind("pe:", 0) == 0) {
        size_t hash = src.rfind('#');
        if (hash == std::string::npos) return nullptr;
        HINSTANCE h = module_for(src.substr(3, hash - 3));
        if (!h) return nullptr;
        return LoadImageW(h, MAKEINTRESOURCEW(atoi(src.c_str() + hash + 1)), type, cx, cy, depth_flag | extra);
    }
    std::u16string w = W(src);
    return LoadImageW(nullptr, (LPCWSTR)w.c_str(), type, cx, cy, LR_LOADFROMFILE | depth_flag | extra);
}

int index(const std::string& key)
{
    auto it = g_index.find(key);
    if (it != g_index.end()) return it->second;
    std::string src = shell::icon_file(key);
    HICON s = (HICON)load_themed(src, IMAGE_ICON, 16, 16, 0);
    HICON l = (HICON)load_themed(src, IMAGE_ICON, g_large_px, g_large_px, 0);
    int idx = -1;
    if (s && l) {
        idx = ImageList_ReplaceIcon(g_small, -1, s);
        ImageList_ReplaceIcon(g_large, -1, l);
        g_own[key] = true;
        if ((int)g_by_index.size() <= idx) g_by_index.resize((size_t)idx + 1);
        g_by_index[(size_t)idx] = key;
        for (auto& sz : g_sized) {
            HICON z = (HICON)load_themed(src, IMAGE_ICON, sz.first, sz.first, 0);
            if (!z) z = (HICON)load_themed(src, IMAGE_ICON, 0, 0, 0);
            if (z) { ImageList_ReplaceIcon(sz.second, -1, z); DestroyIcon(z); }
            else ImageList_ReplaceIcon(sz.second, -1, l);   // keeps the indices in step
        }
    } else if (key != "document") {
        idx = index("document");
    }
    if (s) DestroyIcon(s);
    if (l) DestroyIcon(l);
    g_index[key] = idx;
    return idx;
}

HICON large_icon(const std::string& key)
{
    int idx = index(key.empty() ? "document" : key);
    return idx >= 0 ? ImageList_GetIcon(g_large, idx, ILD_NORMAL) : nullptr;
}

int native_size(const std::string& key, int want)
{
    std::map<std::string, int>& cache = g_native;
    std::string k = key + '#' + std::to_string(want);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    int n = 0;
    HICON h = (HICON)load_themed(shell::icon_file(key.empty() ? "document" : key), IMAGE_ICON, want, want, LR_FLXP_NATIVESIZE);
    if (h) {
        ICONINFO ii = {};
        BITMAP bm = {};
        if (GetIconInfo(h, &ii) && ii.hbmColor && GetObjectW(ii.hbmColor, sizeof bm, &bm)) n = bm.bmWidth;
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        DestroyIcon(h);
    }
    return cache[k] = n;
}

HICON load_icon(const std::string& key, int size)
{
    HICON h = (HICON)load_themed(shell::icon_file(key), IMAGE_ICON, size, size, 0);
    if (!h && key != "document") h = (HICON)load_themed(shell::icon_file("document"), IMAGE_ICON, size, size, 0);
    return h;
}

} // namespace uiicons
