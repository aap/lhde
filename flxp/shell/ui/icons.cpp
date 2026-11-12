// Shell icons as win32 handles (shell/ui.h): an icon theme's sources (shell/icons.h) loaded with LoadImageW, a PE file's
// resources read as data (one module per file, kept for the process).
#include <stdlib.h>
#include <sys/stat.h>
#include <map>
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

HANDLE load_image(const std::string& src, UINT type, int cx, int cy, UINT flags)
{
    if (src.empty()) return nullptr;
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
