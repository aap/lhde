// The task band (taskpane.h). The look is taken, not measured: 4051's band
// (lh4051-232843.png: the folder's big document image faded at the left, links
// white Tahoma in two columns 26 rows apart, "more" with a small arrow, a light
// and a dark line at the bottom) and the PDC 2003 demo's (demo_resources:
// orange/blue/purple aurora backgrounds 788x180, a 128 px preview with the
// name in ~18 px and metadata in ~12 px beside it, the links in two columns
// ~180 px apart, 33 rows apart, 16 px icons).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "taskpane.h"
#include "lhbar.h"
#include "uiicons.h"
#include "commands.h"
#include "gdi/png.h"
#include "shell/config.h"
#include "shell/lhtheme.h"
#include "shell/netres.h"
#include "shell/thumbs.h"

namespace taskpane {

namespace {

// The band's height (doc/4051-explorer-research.md section 5): expanded
// (4051's Preview on) 150 rows by default with its two bottom lines, dragged
// by the bottom edge between EXP_MIN and EXP_MAX (taskpane_height); compact
// (taskpane_compact = 1, Preview off): 71 rows, a line of text and a row of
// tasks (lh4051-194746.png). Dragging an expanded band under EXP_MIN - SNAP
// makes it compact, a compact one dragged down by SNAP expands it; an
// expanded band that would leave the list less than LIST_MIN rows shows
// compact (4051's _ShouldShowPreview).
const int EXP_DEFAULT = 150, EXP_MIN = 100, EXP_MAX = 400, COMPACT_H = 71, SNAP = 20, GRIP = 4, LIST_MIN = 150;
int g_room = 100000;                   // rows for the band and the list (set_room)
const int ICON_X = 12;
const int COL_W = 196, ROW0 = 16, LINK_ICON = 16;
int g_exp_h = EXP_DEFAULT;
bool g_compact;
int H = EXP_DEFAULT;                   // the current height
// the big image and the text beside it follow the height (96 px at 104 rows)
int icon_px() { int i = H == COMPACT_H ? 48 : H - 8; return i < 48 ? 48 : i > 128 ? 128 : i; }
int text_x() { return ICON_X + icon_px() + 16; }
int row_h() { int r = (H - 2 - ROW0 - 18) / 2; return r > 27 ? 27 : r; }

struct Link { int id; std::string text, icon; RECT r = {}; };

int effective() { return g_compact || g_room - g_exp_h < LIST_MIN ? COMPACT_H : g_exp_h; }

HWND g_wnd;
Callbacks g_cb;
bool g_sizing;                         // dragging the bottom edge
int g_size_y0, g_size_h0;              // where the drag started (screen y) and the height then
bool g_size_compact0;
std::shared_ptr<shell::Folder> g_folder;
std::vector<shell::Item> g_sel;
std::vector<Link> g_links;
RECT g_more = {};
int g_hot = -1;                        // link index, or -2 = more
bool g_tracking;
HFONT g_title, g_text, g_text_u;
HBITMAP g_aurora;                      // the demo's background for the current kind, or null
std::string g_aurora_file, g_aurora_dir;
bool g_aurora_looked;
// the selected picture's preview in place of the big icon (the demo's 128 px
// preview, fitted into the icon's 96 px box: the band keeps its 104 rows)
const UINT WM_THUMB = WM_APP + 0x31;
shell::thumbs::Loader* g_loader;
HBITMAP g_thumb;                       // premultiplied, g_thumb_w x g_thumb_h
int g_thumb_w, g_thumb_h;
std::string g_thumb_path;              // the item it shows ("" = none)
uint32_t g_thumb_gen;

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }

bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

HFONT font(int px, int weight, bool underline)
{
    LOGFONTW lf = {};
    lf.lfHeight = -px; lf.lfWeight = weight; lf.lfUnderline = underline; lf.lfQuality = ANTIALIASED_QUALITY;
    const wchar_t* face = L"Segoe UI";
    for (int i = 0; face[i]; i++) lf.lfFaceName[i] = face[i];
    HFONT f = CreateFontIndirectW(&lf);
    // Tahoma when Segoe UI is not there (4051 itself uses Tahoma)
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, f);
    WCHAR got[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    if (lstrcmpiW(got, L"Segoe UI")) {
        DeleteObject(f);
        const wchar_t* t = L"Tahoma";
        memset(lf.lfFaceName, 0, sizeof lf.lfFaceName);
        for (int i = 0; t[i]; i++) lf.lfFaceName[i] = t[i];
        f = CreateFontIndirectW(&lf);
    }
    return f;
}

// The demo's resource directory, only with taskpane_style = aurora:
// taskpane_aurora (a directory), else next to the Longhorn images
// (<longhorn_system32>/../../../../demo_resources).
// "" = none: the menu band's gradient (taskpane_style = menubar, the default;
// "theme" is the old name).
std::string aurora_dir()
{
    if (g_aurora_looked) return g_aurora_dir;
    g_aurora_looked = true;
    std::string style = shell::theme_setting("taskpane_style", "menubar");
    if (style != "aurora") return g_aurora_dir = "";
    std::vector<std::string> c;
    std::string set = shell::theme_setting("taskpane_aurora");
    if (!set.empty()) c.push_back(set);
    std::string sys = shell::theme_setting("longhorn_system32");
    if (!sys.empty()) c.push_back(sys + "/../../../../demo_resources");
    for (const std::string& d : c)
        if (exists(d + "/docLib/exec_briefing_docLib.dir_3_404.png")) return g_aurora_dir = d;
    return g_aurora_dir = "";
}

// the kind of the folder: the demo's aurora for it (frame 00 of each loop)
std::string aurora_file()
{
    std::string d = aurora_dir();
    if (d.empty() || !g_folder) return "";
    std::string n = g_folder->name();
    for (char& ch : n) ch = (char)tolower((unsigned char)ch);
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    if (has("doc") || has("proposal") || has("text") || has("paper") || has("work"))
        return d + "/docLib/exec_briefing_docLib.dir_1_175.png";          // orange00
    if (has("music") || has("audio") || has("sound") || has("song"))
        return d + "/search/exec_briefing_search.dir_3_274.png";          // purple00
    return d + "/docLib/exec_briefing_docLib.dir_3_404.png";              // blue00
}

void load_aurora()
{
    std::string f = aurora_file();
    if (f == g_aurora_file) return;
    g_aurora_file = f;
    if (g_aurora) { DeleteObject(g_aurora); g_aurora = nullptr; }
    std::string data;
    if (f.empty() || !shell::read_file(f, data)) return;
    gdi::Bitmap* b = gdi::decode_png((const uint8_t*)data.data(), data.size());
    if (!b) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = b->width; bi.bmiHeader.biHeight = -b->height; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    g_aurora = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (g_aurora)
        for (int y = 0; y < b->height; y++) memcpy((uint32_t*)bits + (size_t)y * b->width, b->row(y), (size_t)b->width * 4);
    gdi::destroy_bitmap(b);
}

struct Task { int id; const char* text; const char* icon; };
const Task FOLDER_TASKS[] = {
    { ID_NEW_FOLDER, "Make a new folder", "folder" },
    { ID_SEARCH, "Search for files or folders", "search" },
    { ID_TERMINAL, "Open a terminal here", "run" },
    { ID_ADD_BOOKMARK, "Add this folder to Bookmarks", "favorites" },
    { ID_PROPERTIES, "View folder properties", "settings" },
};
const Task ITEM_TASKS[] = {
    { ID_OPEN, "Open", "" },
    { ID_RENAME, "Rename", "" },
    { ID_COPY, "Copy", "" },
    { ID_DELETE, "Delete", "trash" },
    { ID_PROPERTIES, "Properties", "settings" },
};

std::string title()
{
    if (g_sel.size() == 1) return g_sel[0].name;
    if (!g_sel.empty()) return std::to_string(g_sel.size()) + " items selected";
    return g_folder ? g_folder->name() : "";
}

std::string subtitle()
{
    if (g_sel.size() == 1) return g_sel[0].type;
    if (!g_sel.empty()) return "";
    return g_folder && g_folder->is_filesystem() ? g_folder->path() : "";
}

int text_width(HDC dc, HFONT f, const std::string& s)
{
    HGDIOBJ old = SelectObject(dc, f);
    std::u16string w = W(s);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

void layout()
{
    g_links.clear();
    const Task* t = g_sel.empty() ? FOLDER_TASKS : ITEM_TASKS;
    for (int i = 0; i < 5; i++) {
        Link l; l.id = t[i].id; l.text = t[i].text; l.icon = t[i].icon;
        if (l.id == ID_OPEN && g_sel.size() == 1) l.icon = g_sel[0].icon;
        g_links.push_back(l);
    }
    if (!g_wnd) return;
    HDC dc = GetDC(g_wnd);
    if (H == COMPACT_H) {   // a line of text, then the links side by side and more
        int x = text_x(), y = 38;
        for (size_t i = 0; i < g_links.size(); i++) {
            int w = LINK_ICON + 5 + text_width(dc, g_text, g_links[i].text);
            g_links[i].r = RECT{ x, y, x + w, y + 18 };
            x += w + 18;
        }
        g_more = RECT{ x, y, x + 12 + text_width(dc, g_text, "more"), y + 18 };
        ReleaseDC(g_wnd, dc);
        InvalidateRect(g_wnd, nullptr, FALSE);
        return;
    }
    int tw = text_width(dc, g_title, title());
    int sw = text_width(dc, g_text, subtitle());
    int c0 = text_x() + (tw > sw ? tw : sw) + 40;
    if (c0 < text_x() + 170) c0 = text_x() + 170;
    if (c0 > text_x() + 320) c0 = text_x() + 320;   // a long path is clipped
    for (size_t i = 0; i < g_links.size(); i++) {
        int col = (int)i / 3, row = (int)i % 3;
        int x = c0 + col * COL_W, y = ROW0 + row * row_h();
        int w = LINK_ICON + 5 + text_width(dc, g_text, g_links[i].text);
        g_links[i].r = RECT{ x, y, x + w, y + 18 };
    }
    int mx = c0 + COL_W + 8, my = ROW0 + 2 * row_h();
    g_more = RECT{ mx, my, mx + 12 + text_width(dc, g_text, "more"), my + 18 };
    ReleaseDC(g_wnd, dc);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

void draw_text(HDC dc, HFONT f, const std::string& s, int x, int y, COLORREF c, int right = 100000)
{
    HGDIOBJ old = SelectObject(dc, f);
    std::u16string w = W(s);
    RECT r = { x, y, right, y + 40 };
    SetTextColor(dc, c);
    DrawTextW(dc, (LPCWSTR)w.c_str(), (int)w.size(), &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

// an icon of the theme at `size`, alpha-blended with `alpha`. AlphaBlend wants
// premultiplied colour and the icon's alpha in the source; DrawIconEx into a
// plain DIB leaves the alpha byte at 0 (which made AlphaBlend add the icon to
// the gradient), so the icon is drawn over black (= premultiplied colour) and
// over white, and the alpha is what the two differ by.
void draw_icon(HDC dc, const std::string& key, int size, int x, int y, int alpha)
{
    HICON ic = uiicons::load_icon(key.empty() ? "document" : key, size);
    if (!ic) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = size; bi.bmiHeader.biHeight = -size; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* kbits = nullptr; void* wbits = nullptr;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP k = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &kbits, nullptr, 0);
    HBITMAP wb = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &wbits, nullptr, 0);
    if (k && wb) {
        size_t n = (size_t)size * size;
        memset(kbits, 0, n * 4);
        memset(wbits, 0xff, n * 4);
        HGDIOBJ old = SelectObject(mem, wb);
        DrawIconEx(mem, 0, 0, ic, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(mem, k);
        DrawIconEx(mem, 0, 0, ic, size, size, 0, nullptr, DI_NORMAL);
        uint32_t* kp = (uint32_t*)kbits; const uint32_t* wp = (const uint32_t*)wbits;
        for (size_t i = 0; i < n; i++) {
            int d = 0;
            for (int sh = 0; sh < 24; sh += 8) {
                int c = (int)((wp[i] >> sh) & 0xff) - (int)((kp[i] >> sh) & 0xff);
                if (c > d) d = c;
            }
            kp[i] = (kp[i] & 0xffffff) | (uint32_t)(255 - d) << 24;
        }
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, AC_SRC_ALPHA };
        AlphaBlend(dc, x, y, size, size, mem, 0, 0, size, size, bf);
        SelectObject(mem, old);
    }
    if (k) DeleteObject(k);
    if (wb) DeleteObject(wb);
    DeleteDC(mem);
    DestroyIcon(ic);
}

void notify(void*) { if (g_wnd) PostMessageW(g_wnd, WM_THUMB, 0, 0); }   // a worker thread

void drop_thumb()
{
    if (g_thumb) DeleteObject(g_thumb);
    g_thumb = nullptr;
    g_thumb_path.clear();
}

// one picture selected: ask for its thumbnail (cached 128 px, fitted to icon_px())
void request_thumb()
{
    if (g_loader) g_thumb_gen = g_loader->cancel();
    if (g_sel.size() == 1 && g_thumb_path == g_sel[0].path) return;
    drop_thumb();
    if (g_sel.size() != 1) return;
    const shell::Item& it = g_sel[0];
    if (it.is_folder() || (it.flags & shell::IsVirtual) || it.path.empty() || it.path[0] != '/') return;
    if (!shell::thumbs::can_thumbnail(it.path)) return;
    if (!g_loader) { g_loader = new shell::thumbs::Loader(notify, nullptr, 1); g_thumb_gen = g_loader->generation(); }
    g_loader->request(g_thumb_gen, 0, it.path, it.mtime, icon_px());
}

void take_thumbs()
{
    shell::thumbs::Loader::Result r;
    while (g_loader && g_loader->take(r)) {
        if (r.gen == g_thumb_gen && r.bmp && g_sel.size() == 1 && r.path == g_sel[0].path) {
            drop_thumb();
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = r.bmp->width; bi.bmiHeader.biHeight = -r.bmp->height;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            g_thumb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (g_thumb) {
                for (int y = 0; y < r.bmp->height; y++) {
                    const uint32_t* s = r.bmp->row(y);
                    uint32_t* d = (uint32_t*)bits + (size_t)y * r.bmp->width;
                    for (int x = 0; x < r.bmp->width; x++) {     // straight -> premultiplied
                        uint32_t p = s[x], a = p >> 24;
                        d[x] = a << 24 | (((p >> 16) & 0xff) * a / 255) << 16 | (((p >> 8) & 0xff) * a / 255) << 8 | (p & 0xff) * a / 255;
                    }
                }
                g_thumb_w = r.bmp->width; g_thumb_h = r.bmp->height;
                g_thumb_path = r.path;
                if (g_wnd) InvalidateRect(g_wnd, nullptr, FALSE);
            }
        }
        shell::thumbs::free_result(r);
    }
}

// the preview centred in the icon's box, a thin dark frame around it
void draw_thumb(HDC dc, int x, int y)
{
    int tx = x + (icon_px() - g_thumb_w) / 2, ty = y + (icon_px() - g_thumb_h) / 2;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_thumb);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, tx, ty, g_thumb_w, g_thumb_h, mem, 0, 0, g_thumb_w, g_thumb_h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void paint_background(HDC dc, int w)
{
    load_aurora();
    if (g_aurora) {
        // the aurora over the width, its middle rows (the demo shows ~150 of 180)
        BITMAP bm = {};
        GetObjectW(g_aurora, sizeof bm, &bm);
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, g_aurora);
        int sh = bm.bmWidth * H / (w > 0 ? w : 1);
        if (sh > bm.bmHeight) sh = bm.bmHeight;
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, 0, 0, w, H, mem, 0, (bm.bmHeight - sh) / 2, bm.bmWidth, sh, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        RECT r = { 0, -1, w, H };    // the command bar's strip, without its highlight row
        lhbar::paint_background(dc, r);
    }
    // the band's end (4051: a light row, then a dark one)
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = -2; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP b = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (b) {
        ((uint32_t*)bits)[0] = 0x60606060;       // white at 0.38, premultiplied
        ((uint32_t*)bits)[1] = 0x90000000;       // black at 0.56
        HGDIOBJ old = SelectObject(mem, b);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(dc, 0, H - 2, w, 2, mem, 0, 0, 1, 2, bf);
        SelectObject(mem, old);
        DeleteObject(b);
    }
    DeleteDC(mem);
}

void paint(HDC wdc)
{
    RECT rc; GetClientRect(g_wnd, &rc);
    int w = rc.right;
    if (w <= 0) return;
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP buf = CreateCompatibleBitmap(wdc, w, H);
    HGDIOBJ oldb = SelectObject(dc, buf);
    paint_background(dc, w);
    SetBkMode(dc, TRANSPARENT);
    // the big image: the folder's faded (4051), the selection's whole (the demo's preview)
    std::string key = g_sel.size() == 1 ? g_sel[0].icon : g_folder ? g_folder->icon() : "folder";
    const COLORREF WHITE = RGB(255, 255, 255), SOFT = RGB(226, 232, 245);
    int c0 = g_links.empty() ? w : g_links[0].r.left;
    if (H == COMPACT_H) {
        draw_icon(dc, key, 48, ICON_X, (H - 2 - 48) / 2, g_sel.size() == 1 ? 255 : 90);
        std::string line = title(), sub = subtitle();
        if (!sub.empty() && g_sel.size() == 1) line += "  -  " + sub;
        draw_text(dc, g_text, line, text_x(), 14, WHITE, w - 8);
    } else {
        if (g_thumb && g_sel.size() == 1 && g_thumb_path == g_sel[0].path) draw_thumb(dc, ICON_X, (H - icon_px()) / 2);
        else draw_icon(dc, key, icon_px(), ICON_X, (H - icon_px()) / 2, g_sel.size() == 1 ? 255 : 90);
        draw_text(dc, g_title, title(), text_x(), 12, WHITE, c0 - 16);
        draw_text(dc, g_text, subtitle(), text_x(), 40, SOFT, c0 - 16);
    }
    for (size_t i = 0; i < g_links.size(); i++) {
        const Link& l = g_links[i];
        if (l.r.left >= w) continue;
        if (!l.icon.empty()) draw_icon(dc, l.icon, LINK_ICON, l.r.left, l.r.top + 1, 255);
        draw_text(dc, (int)i == g_hot ? g_text_u : g_text, l.text, l.r.left + LINK_ICON + 5, l.r.top, WHITE, w);
    }
    // more: a small down arrow, then the word (4051)
    int ax = g_more.left + 2, ay = g_more.top + 8;
    HPEN pen = CreatePen(PS_SOLID, 1, SOFT);
    HGDIOBJ op = SelectObject(dc, pen);
    for (int k = 0; k < 3; k++) { MoveToEx(dc, ax + k, ay + k, nullptr); LineTo(dc, ax + 5 - k, ay + k); }
    SelectObject(dc, op);
    DeleteObject(pen);
    draw_text(dc, g_hot == -2 ? g_text_u : g_text, "more", g_more.left + 12, g_more.top, SOFT, w);
    BitBlt(wdc, 0, 0, w, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldb);
    DeleteObject(buf);
    DeleteDC(dc);
}

int hit(POINT p)
{
    for (size_t i = 0; i < g_links.size(); i++) if (PtInRect(&g_links[i].r, p)) return (int)i;
    if (PtInRect(&g_more, p)) return -2;
    return -1;
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        LPCWSTR c = g_sizing || p.y >= H - GRIP ? (LPCWSTR)IDC_SIZENS : hit(p) != -1 ? (LPCWSTR)IDC_HAND : (LPCWSTR)IDC_ARROW;
        SetCursor(LoadCursorW(nullptr, c));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (p.y < H - GRIP) return 0;
        POINT s = p; ClientToScreen(h, &s);
        g_sizing = true;
        g_size_y0 = s.y; g_size_h0 = H; g_size_compact0 = g_compact;
        SetCapture(h);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_sizing) {
            g_sizing = false;
            shell::write_setting("taskpane_compact", g_compact ? "1" : "0");
            shell::write_setting("taskpane_height", std::to_string(g_exp_h).c_str());
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (g_sizing) {
            POINT s = p; ClientToScreen(h, &s);
            int want = g_size_h0 + (s.y - g_size_y0);
            bool compact = g_compact;
            int eh = g_exp_h;
            if (compact) { if (want >= COMPACT_H + SNAP) { compact = false; eh = want < EXP_MIN ? EXP_MIN : want; } }
            else if (want < EXP_MIN - SNAP) { compact = true; if (!g_size_compact0) eh = g_size_h0; }   // expands again to where it was
            else eh = want < EXP_MIN ? EXP_MIN : want;
            if (eh > EXP_MAX) eh = EXP_MAX;
            if (!compact && eh > g_room - LIST_MIN) eh = g_room - LIST_MIN < EXP_MIN ? EXP_MIN : g_room - LIST_MIN;
            if (compact != g_compact || eh != g_exp_h) {
                g_compact = compact; g_exp_h = eh;
                H = effective();
                layout();
                if (g_cb.resized) g_cb.resized();
            }
            return 0;
        }
        int i = hit(p);
        if (i != g_hot) { g_hot = i; InvalidateRect(h, nullptr, FALSE); }
        if (!g_tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            g_tracking = TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        if (g_hot != -1) { g_hot = -1; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP: {
        if (g_sizing) { ReleaseCapture(); return 0; }
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        int i = hit(p);
        bool sel = !g_sel.empty();
        if (i == -2 && g_cb.more) {
            POINT s = { g_more.left, g_more.bottom };
            ClientToScreen(h, &s);
            g_cb.more(sel, s.x, s.y);
        } else if (i >= 0 && g_cb.command) {
            int id = g_links[(size_t)i].id;
            g_cb.command(id, sel);
        }
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_THUMB:
        take_thumbs();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

bool wanted() { return shell::setting("taskpane", "0") == "1"; }

int height() { return H; }
bool compact() { return g_compact; }

void set_room(int rows)
{
    g_room = rows;
    int h = effective();
    if (h != H) { H = h; if (g_wnd) layout(); }
}

void set_compact(bool on)
{
    g_compact = on;
    H = effective();
    shell::write_setting("taskpane_compact", on ? "1" : "0");
    if (g_wnd) layout();
}

HWND create(HWND frame, const Callbacks& cb)
{
    g_cb = cb;
    int eh = atoi(shell::setting("taskpane_height", "150").c_str());
    g_exp_h = eh < EXP_MIN ? EXP_MIN : eh > EXP_MAX ? EXP_MAX : eh;
    g_compact = shell::setting("taskpane_compact", "0") == "1";
    H = effective();
    if (!g_title) {
        g_title = font(18, FW_NORMAL, false);
        g_text = font(12, FW_NORMAL, false);
        g_text_u = font(12, FW_NORMAL, true);
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_taskband";
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, L"flxp_taskband", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 0, H, frame, nullptr, nullptr, nullptr);
    layout();
    return g_wnd;
}

void destroy()
{
    if (g_wnd) DestroyWindow(g_wnd);
    g_wnd = nullptr;
    g_hot = -1;
    g_tracking = false;
    if (g_loader) g_thumb_gen = g_loader->cancel();
    drop_thumb();
}

HWND window() { return g_wnd; }

void set_folder(const std::shared_ptr<shell::Folder>& f)
{
    g_folder = f;
    g_sel.clear();
    request_thumb();
    layout();
}

void set_selection(const std::vector<shell::Item>& items)
{
    if (items.size() == g_sel.size()) {
        bool same = true;
        for (size_t i = 0; i < items.size() && same; i++) same = items[i].path == g_sel[i].path;
        if (same) return;
    }
    g_sel = items;
    request_thumb();
    layout();
}

void theme_changed()
{
    g_aurora_looked = false;
    g_aurora_file = "-";
    if (g_wnd) InvalidateRect(g_wnd, nullptr, FALSE);
}

std::string source()
{
    std::string d = aurora_dir();
    if (!d.empty()) return "the Longhorn demo's aurora images (" + d + "), by folder kind";
    return "the command bar's background (the theme's Rebar.BACKGROUND strip or its gradient)";
}

}
