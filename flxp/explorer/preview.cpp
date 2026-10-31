#include <sys/stat.h>
#include <map>
#include "preview.h"
#include "gdi/image.h"
#include "shell/thumbs.h"

namespace preview {

struct Pictures::Impl {
    HWND wnd;
    UINT msg;
    shell::thumbs::Loader* loader = nullptr;
    uint32_t gen = 0;
    int size = 0;
    std::vector<std::string> paths;                      // wanted
    std::map<std::string, gdi::Bitmap*> got;             // straight alpha, as the loader made them
    struct Fitted { HBITMAP bmp = nullptr; int w = 0, h = 0, max_w = 0, max_h = 0; };
    std::map<std::string, Fitted> fitted;
};

static void notify(void* ctx)   // a worker thread
{
    Pictures::Impl* p = (Pictures::Impl*)ctx;
    PostMessageW(p->wnd, p->msg, 0, 0);
}

Pictures::Pictures(HWND wnd, UINT msg) : impl_(new Impl)
{
    impl_->wnd = wnd;
    impl_->msg = msg;
}

static void drop_fitted(Pictures::Impl* p, const std::string& path)
{
    auto f = p->fitted.find(path);
    if (f == p->fitted.end()) return;
    if (f->second.bmp) DeleteObject(f->second.bmp);
    p->fitted.erase(f);
}

Pictures::~Pictures()
{
    if (impl_->loader) { impl_->loader->cancel(); delete impl_->loader; }
    for (auto& kv : impl_->got) gdi::destroy_bitmap(kv.second);
    for (auto& kv : impl_->fitted) if (kv.second.bmp) DeleteObject(kv.second.bmp);
    delete impl_;
}

void Pictures::want(const std::vector<std::string>& paths, int size)
{
    Impl* p = impl_;
    if (paths == p->paths && size == p->size) return;
    p->paths = paths;
    p->size = size;
    // drop what is no longer wanted
    for (auto it = p->got.begin(); it != p->got.end();) {
        bool keep = false;
        for (const std::string& q : paths) keep |= q == it->first;
        if (keep) { ++it; continue; }
        drop_fitted(p, it->first);
        gdi::destroy_bitmap(it->second);
        it = p->got.erase(it);
    }
    if (p->loader) p->gen = p->loader->cancel();
    for (size_t i = 0; i < paths.size(); i++) {
        if (p->got.count(paths[i])) continue;
        struct stat st;
        if (stat(paths[i].c_str(), &st) != 0) continue;
        if (!p->loader) { p->loader = new shell::thumbs::Loader(notify, p, 1); p->gen = p->loader->generation(); }
        p->loader->request(p->gen, (int)i, paths[i], (int64_t)st.st_mtime, size);
    }
}

bool Pictures::take()
{
    Impl* p = impl_;
    bool any = false;
    shell::thumbs::Loader::Result r;
    while (p->loader && p->loader->take(r)) {
        bool wanted = false;
        for (const std::string& q : p->paths) wanted |= q == r.path;
        if (r.gen == p->gen && r.bmp && wanted && !p->got.count(r.path)) {
            p->got[r.path] = r.bmp;
            r.bmp = nullptr;
            any = true;
        }
        shell::thumbs::free_result(r);
    }
    return any;
}

HBITMAP Pictures::get(const std::string& path, int max_w, int max_h, int* w, int* h)
{
    Impl* p = impl_;
    auto g = p->got.find(path);
    if (g == p->got.end() || max_w <= 0 || max_h <= 0) return nullptr;
    Impl::Fitted& f = p->fitted[path];
    if (!f.bmp || f.max_w != max_w || f.max_h != max_h) {
        if (f.bmp) DeleteObject(f.bmp);
        f = Impl::Fitted();
        const gdi::Bitmap* src = g->second;
        int fw = src->width, fh = src->height;
        gdi::fit_size(src->width, src->height, max_w, max_h, false, &fw, &fh);
        gdi::Bitmap* s = (fw == src->width && fh == src->height) ? nullptr : gdi::resample(src, fw, fh);
        const gdi::Bitmap* use = s ? s : src;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = use->width; bi.bmiHeader.biHeight = -use->height;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        f.bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (f.bmp && bits) {
            for (int y = 0; y < use->height; y++) {
                const uint32_t* sp = ((gdi::Bitmap*)use)->row(y);
                uint32_t* d = (uint32_t*)bits + (size_t)y * use->width;
                for (int x = 0; x < use->width; x++) {     // straight -> premultiplied
                    uint32_t q = sp[x], a = q >> 24;
                    d[x] = a << 24 | (((q >> 16) & 0xff) * a / 255) << 16 | (((q >> 8) & 0xff) * a / 255) << 8 | (q & 0xff) * a / 255;
                }
            }
            f.w = use->width; f.h = use->height;
        }
        f.max_w = max_w; f.max_h = max_h;
        if (s) gdi::destroy_bitmap(s);
    }
    if (!f.bmp) return nullptr;
    *w = f.w; *h = f.h;
    return f.bmp;
}

void blend(HDC dc, HBITMAP bmp, int x, int y, int w, int h, int alpha)
{
    if (!bmp || w <= 0 || h <= 0) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, AC_SRC_ALPHA };
    AlphaBlend(dc, x, y, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void fill_alpha(HDC dc, int x, int y, int w, int h, COLORREF c, int alpha)
{
    if (w <= 0 || h <= 0) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = 1; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP b = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (b && bits) {
        *(uint32_t*)bits = 0xff000000u | (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c);
        HGDIOBJ old = SelectObject(mem, b);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 };
        AlphaBlend(dc, x, y, w, h, mem, 0, 0, 1, 1, bf);
        SelectObject(mem, old);
    }
    if (b) DeleteObject(b);
    DeleteDC(mem);
}

}
