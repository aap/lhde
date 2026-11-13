// The task band's picture of a selected folder against the Icons view's (docs/USAGE.md "The Explorer's details and task
// band"): a folder holding two JPEGs (written here), its square made by the view (thumbview: a list view, enter_at, fill,
// the worker threads, on_ready; the image list's image) and the band's (preview::Pictures::want_folder / get_folder, its
// own loader), both at 128 px, in XP's style and the Longhorn one: the same pixels (the band's premultiplied, as it
// draws them), and not the folder alone (the pictures are in it). Needs a display; tests/headless/folderpic.sh runs it
// on Xvfb with a scratch home. Usage: folderpictest [THEME] (a theme whose build has 4074's open folder: the photo).
// Prints one line per check, exit 1 on a failure.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#define XMD_H   // jmorecfg.h: no INT32 of its own (basetsd.h has one)
#include <jpeglib.h>
#include "w32/w32.h"
#include "w32/user.h"
#include "gdi/bitmap.h"
#include "explorer/preview.h"
#include "explorer/thumbview.h"
#include "shell/lhtheme.h"
#include "shell/thumbs.h"
extern "C" {
#include "commctrl.h"
}

static int fails = 0;
static void check(bool ok, const std::string& what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) fails++;
}

// a w x h JPEG of a diagonal gradient between two colours
static bool write_jpeg(const std::string& path, int w, int h, uint32_t c0, uint32_t c1)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    jpeg_compress_struct cinfo;
    jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f);
    cinfo.image_width = (JDIMENSION)w; cinfo.image_height = (JDIMENSION)h;
    cinfo.input_components = 3; cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    std::vector<unsigned char> row((size_t)w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int t = (x + y) * 255 / (w + h);
            for (int k = 0; k < 3; k++) {
                int a = (int)((c0 >> (16 - 8 * k)) & 255), b = (int)((c1 >> (16 - 8 * k)) & 255);
                row[(size_t)x * 3 + k] = (unsigned char)(a + (b - a) * t / 255);
            }
        }
        JSAMPROW r = row.data();
        jpeg_write_scanlines(&cinfo, &r, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(f);
    return true;
}

static const UINT WM_PICTURE = WM_APP + 0x31;
static preview::Pictures* g_pics;
static bool g_pane_ready;

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == thumbview::WM_THUMBS) { thumbview::on_ready(); return 0; }
    if (msg == WM_PICTURE) { if (g_pics && g_pics->take()) g_pane_ready = true; return 0; }
    return DefWindowProcW(h, msg, wp, lp);
}

static void pump_until(bool (*done)(), int ms)
{
    for (int i = 0; i < ms / 10 && !done(); i++) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        usleep(10000);
    }
}

static HWND g_list;
static int image_of_item()
{
    LVITEMW it = {};
    it.mask = LVIF_IMAGE;
    it.iItem = 0;
    SendMessageW(g_list, LVM_GETITEMW, 0, (LPARAM)&it);
    return it.iImage;
}
static int g_default_image = -1;
static bool view_ready() { return image_of_item() != g_default_image; }
static bool pane_ready() { return g_pane_ready; }

// 32-bit top-down pixels of a bitmap
static std::vector<uint32_t> pixels(HBITMAP b, int x, int y, int w, int h)
{
    BITMAP bm = {};
    GetObjectW(b, sizeof bm, &bm);
    std::vector<uint32_t> all((size_t)bm.bmWidth * bm.bmHeight), out;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = bm.bmWidth; bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HDC dc = GetDC(nullptr);
    GetDIBits(dc, b, 0, (UINT)bm.bmHeight, all.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    for (int j = y; j < y + h && j < bm.bmHeight; j++)
        for (int i = x; i < x + w && i < bm.bmWidth; i++) out.push_back(all[(size_t)j * bm.bmWidth + i]);
    return out;
}

static uint32_t premultiplied(uint32_t q)
{
    uint32_t a = q >> 24;
    return a << 24 | (((q >> 16) & 0xff) * a / 255) << 16 | (((q >> 8) & 0xff) * a / 255) << 8 | (q & 0xff) * a / 255;
}

int main(int argc, char** argv)
{
    const char* t = getenv("TMPDIR");
    std::string tpl = std::string(t && *t ? t : "/tmp") + "/folderpictestXXXXXX";
    std::vector<char> buf(tpl.begin(), tpl.end());
    buf.push_back(0);
    if (!mkdtemp(buf.data())) { perror("mkdtemp"); return 2; }
    std::string base = buf.data();
    std::string dir = base + "/Holiday";
    mkdir(dir.c_str(), 0755);
    check(write_jpeg(dir + "/a.jpg", 200, 150, 0xd02020, 0xf0e040) && write_jpeg(dir + "/b.jpg", 120, 160, 0x2040d0, 0x40e0f0),
          "two JPEGs in " + dir);
    struct stat st;
    stat(dir.c_str(), &st);

    w32::init();
    InitCommonControls();
    if (argc > 1) {   // a theme: its build's resources (4074's shell32 has the open folder #389/#390 the photo stands in)
        shell::SessionTheme th;
        if (!shell::resolve_theme(argv[1], th)) { printf("skip theme %s: it does not resolve\n", argv[1]); return 0; }
        shell::set_session_theme(th);
        printf("theme %s, resources %s\n", argv[1], shell::longhorn_resources().empty() ? "(none)" : shell::longhorn_resources().c_str());
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"folderpictest";
    RegisterClassW(&wc);
    HWND top = CreateWindowExW(0, L"folderpictest", L"folderpictest", WS_OVERLAPPEDWINDOW, 0, 0, 600, 400, nullptr, nullptr, nullptr, nullptr);
    g_list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_ICON, 0, 0, 600, 400, top, nullptr, nullptr, nullptr);
    thumbview::init(g_list, top);
    shell::Item item;
    item.name = "Holiday"; item.path = dir; item.flags = shell::IsFolder; item.mtime = (int64_t)st.st_mtime; item.icon = "folder";
    LVITEMW lvi = {};
    lvi.mask = LVIF_TEXT;
    lvi.pszText = (LPWSTR)L"Holiday";
    SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

    const int SIZE = 128;
    for (int lh = 0; lh < 2; lh++) {
        std::string style = lh ? "Longhorn" : "XP";
        // the Icons view
        thumbview::set_longhorn(lh != 0);
        thumbview::enter_at(SIZE, false, nullptr, nullptr);
        thumbview::folder_changed();
        g_default_image = -2;
        thumbview::fill({ item });
        g_default_image = image_of_item();
        pump_until(view_ready, 10000);
        int idx = image_of_item();
        check(idx != g_default_image, style + ": the view's folder square arrived");
        HIMAGELIST il = (HIMAGELIST)SendMessageW(g_list, LVM_GETIMAGELIST, LVSIL_NORMAL, 0);
        IMAGEINFO ii = {};
        std::vector<uint32_t> view;
        if (il && ImageList_GetImageInfo(il, idx, &ii))
            view = pixels(ii.hbmImage, ii.rcImage.left, ii.rcImage.top, SIZE, SIZE);
        check(view.size() == (size_t)SIZE * SIZE, style + ": the view's square is " + std::to_string(SIZE) + " px");
        // the band
        delete g_pics;
        g_pics = new preview::Pictures(top, WM_PICTURE);
        g_pane_ready = false;
        g_pics->want_folder(dir, "folder", SIZE, lh != 0);
        pump_until(pane_ready, 10000);
        int w = 0, h = 0;
        HBITMAP b = g_pics->get_folder(dir, SIZE, &w, &h);
        check(b && w == SIZE && h == SIZE, style + ": the band's folder picture arrived at " + std::to_string(SIZE) + " px");
        std::vector<uint32_t> pane = b ? pixels(b, 0, 0, w, h) : std::vector<uint32_t>();
        size_t diff = 0;
        for (size_t i = 0; i < view.size() && i < pane.size(); i++) if (premultiplied(view[i]) != pane[i]) diff++;
        check(!view.empty() && view.size() == pane.size() && diff == 0,
              style + ": the band's picture is the view's square (" + std::to_string(diff) + " pixels differ)");
        // and the pictures are in it: not the folder alone
        gdi::Bitmap* bare = thumbview::folder_picture("folder", {}, SIZE, lh != 0);
        size_t same = 0;
        for (size_t i = 0; bare && i < pane.size(); i++) if (premultiplied(bare->pixels[(i / SIZE) * bare->stride + i % SIZE]) == pane[i]) same++;
        check(bare && same < pane.size() * 95 / 100, style + ": the pictures are in it (" + std::to_string(pane.size() - same) +
              " pixels differ from the folder alone)");
        if (bare) gdi::destroy_bitmap(bare);
        thumbview::leave(nullptr, nullptr);
    }
    delete g_pics;
    g_pics = nullptr;
    DestroyWindow(top);
    std::string rm = "rm -rf '" + base + "'";
    if (system(rm.c_str()) != 0) {}
    printf("%d failed\n", fails);
    return fails ? 1 : 0;
}
