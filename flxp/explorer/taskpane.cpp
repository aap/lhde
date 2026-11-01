// The task band (taskpane.h): 4051's preview pane, measured on the 4051
// screenshots at 1:1 (lh4051-152854/152900 nothing selected, 152336/152329 one
// file, 152415/152354 one picture, 152420/152358 four pictures; the band
// starts under the command bar, x from its left edge) and laid out with the
// rules of System.Windows.Explorer (taskpane/managed.md):
// - the editable rows (a stored property's value or invitation, one item):
//   the hand over them, underlined while hot; a click puts an edit box in the
//   row's place (white, the line's font, the column's width), Enter or leaving
//   it keeps the value (the explorer writes the attribute), Escape drops it.
// - PreviewManager: padding 18 left; the preview box (the band's height - 22
//   + the 30 px overhang = the height + 8 wide: 134 expanded, 56 compact);
//   18 px; the title (14 pt) and the metadata, two 180 px columns of 8 pt
//   rows 17 px apart from y 36, "all properties..." (white in the screenshots,
//   not the sheet's #C4ECFD; pointer over #F3B680) on the last line, 14 px above the bottom; the preview's natural
//   width ends 18 px after the two columns.
// - a framed picture: 2 px white, fitted into the box less 12 (122 x 92 for
//   800 x 600 expanded, 44 x 33 compact), its bottom 10 px below the band; the
//   stack's front one 1 - (n-1)/6 * 0.3 of that, the ones behind it 7 and 6 px
//   further up and left each (ShellViewMultiSelectPreviewer: up to 7, the scale
//   stepping down; its opacity steps do not show in 152420: drawn opaque).
// - an icon: 128 px expanded, 48 compact, its bottom 11 px below the band.
// - WelcomePane: "N" 18 pt, " items" 12 pt, "in this folder" 10 pt, from the
//   left 18 and the bottom 14; compact on one line ("in this folder" 5 px
//   after).
// - PreviewTaskPresenter / OverflowCanvas: the tasks start 8 px right of the
//   preview's natural width (123 for the count; compact without a selection:
//   half the band), nothing when fewer than 200 px are left; 189 px columns,
//   26 px rows (5 + 16 + 5) from y 12, the icon then 4 px then the 10 pt text
//   in #DBE9F3; compact: one row, the tasks side by side.
// The faded folder image of the count, the aurora and the band's ends are as
// before (lh4051-232843.png, the PDC 2003 demo's resources).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include "taskpane.h"
#include "lhbar.h"
#include "preview.h"
#include "uiicons.h"
#include "commands.h"
#include "gdi/png.h"
#include "shell/config.h"
#include "shell/lhdata.h"
#include "shell/lhtheme.h"
#include "shell/netres.h"
extern "C" {
#include "commctrl.h"
}

namespace taskpane {

namespace {

using shell::details::Info;
using shell::details::Kind;

// The band's height below the command bar (4051: PreviewSplitter Min 150, Max
// 270, MiniModeHeight 72, all less HeightOffset 24): expanded 126 by default,
// dragged by the bottom edge between EXP_MIN and EXP_MAX (taskpane_height);
// compact (taskpane_compact = 1, Preview off) 48. A drag under SPLIT makes it
// compact, a compact one dragged to SPLIT expands it ((72 + 150) / 2 - 24); an
// expanded band that would leave the list less than LIST_MIN rows shows
// compact (4051's _OnDragDelta clamps to the total - 150).
const int EXP_DEFAULT = 126, EXP_MIN = 126, EXP_MAX = 246, COMPACT_H = 48, SPLIT = 87, GRIP = 4, LIST_MIN = 150;
const int HANG = 16;                   // the rows the preview may hang below the band (its 10 px, the shadow)
const int PAD = 18, COL = 180, LINE_Y = 36, LINE_H = 17, TASK_COL = 189, TASK_ROW = 26, TASK_Y = 12, TASK_MIN = 200;
const COLORREF WHITE = RGB(255, 255, 255), TASK_TEXT = RGB(0xDB, 0xE9, 0xF3), MORE_TEXT = RGB(255, 255, 255),
               MORE_HOT = RGB(0xF3, 0xB6, 0x80);
int g_room = 100000;                   // rows for the band and the list (set_room)
bool g_bottom;                         // the theme's Explorer.taskpane = bottom: the band under the list, mirrored
int g_exp_h = EXP_DEFAULT;
bool g_compact;
int H = EXP_DEFAULT;                   // the current height

int effective() { return g_compact || g_room - g_exp_h < LIST_MIN ? COMPACT_H : g_exp_h; }
// the band's first row in the bitmap render() makes: what hangs over the left pane is below the band at the top, above it at
// the bottom
int band_y() { return g_bottom ? HANG : 0; }
bool read_bottom() { return shell::session_theme().value("Explorer.taskpane") == "bottom"; }   // the theme's: top | bottom
bool small() { return H == COMPACT_H; }
int box_x() { return PAD; }
int box_w() { return H + 8; }
int text_x() { return PAD + box_w() + PAD; }

struct Link { int id; std::string text, icon; RECT r = {}; };
struct Text { std::string text; RECT r; int font; COLORREF color; };   // font: an index of g_fonts

HWND g_wnd, g_hang;
RECT g_hang_rc;                        // what of the preview is outside the band (band coordinates; empty: nothing)
void hang_add(int l, int t, int r, int b)
{
    if (g_bottom ? t >= 0 : b <= H) return;
    RECT x = g_bottom ? RECT{ l, t, r, b < 0 ? b : 0 } : RECT{ l, t > H ? t : H, r, b };
    if (g_hang_rc.right <= g_hang_rc.left) g_hang_rc = x;
    else UnionRect(&g_hang_rc, &g_hang_rc, &x);
}
Callbacks g_cb;
bool g_sizing;                         // dragging the bottom edge
int g_size_y0, g_size_h0;              // where the drag started (screen y) and the height then
bool g_size_compact0;
std::shared_ptr<shell::Folder> g_folder;
Info g_info;
bool g_pictures_ok;
preview::Pictures* g_pics;
std::vector<Link> g_links;
std::vector<Text> g_texts;             // the count, the title and the values
RECT g_more = {}, g_props = {};        // "more", "all properties..." (empty: none)
struct EditRow { RECT r; std::string id, raw; size_t text; };   // an editable value: where, which property, g_texts' index
std::vector<EditRow> g_rows;
HWND g_edit;                           // the open editor (a row's), or null
std::string g_edit_id, g_edit_raw, g_edit_path;
bool g_edit_closing;
int g_hot = -1;                        // link index, -2 = more, -3 = all properties, ROW_HOT - i = row i
bool g_tracking;
enum { F_NUMBER, F_ITEMS, F_INFOLDER, F_TITLE, F_LINE, F_TASK, F_TASK_U, F_LINE_U, F_COUNT };
HFONT g_fonts[F_COUNT];
HBITMAP g_aurora;                      // the demo's background for the current kind, or null
std::string g_aurora_file, g_aurora_dir;
bool g_aurora_looked;
const UINT WM_PICTURE = WM_APP + 0x31;
const int ROW_HOT = -100;

std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
inline int imax(int a, int b) { return a > b ? a : b; }
inline int imin(int a, int b) { return a < b ? a : b; }

bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

// 4051 draws the pane in Tahoma
HFONT tahoma(int px, bool underline)
{
    LOGFONTW lf = {};
    lf.lfHeight = -px; lf.lfWeight = FW_NORMAL; lf.lfUnderline = underline;
    lf.lfQuality = px > 13 ? ANTIALIASED_QUALITY : DEFAULT_QUALITY;   // small: as the gasp table says
    const wchar_t* t = L"Tahoma";
    for (int i = 0; t[i]; i++) lf.lfFaceName[i] = t[i];
    return CreateFontIndirectW(&lf);
}

void make_fonts()
{
    if (g_fonts[0]) return;
    // 18, 12, 10, 14, 8, 10 pt at 96 dpi
    g_fonts[F_NUMBER] = tahoma(24, false);
    g_fonts[F_ITEMS] = tahoma(16, false);
    g_fonts[F_INFOLDER] = tahoma(13, false);
    g_fonts[F_TITLE] = tahoma(19, false);
    g_fonts[F_LINE] = tahoma(11, false);
    g_fonts[F_TASK] = tahoma(13, false);
    g_fonts[F_TASK_U] = tahoma(13, true);
    g_fonts[F_LINE_U] = tahoma(11, true);
}

// The demo's resource directory, only with taskpane_background = aurora:
// lhde's resources/demo (shell/lhdata.h), else next to the Longhorn images
// (<the theme's Longhorn resources>/../../../../demo_resources).
// "" = none: the menu band's gradient (taskpane_background = menubar, the default).
std::string aurora_dir()
{
    if (g_aurora_looked) return g_aurora_dir;
    g_aurora_looked = true;
    if (shell::setting("taskpane_background", "menubar") != "aurora") return g_aurora_dir = "";
    std::vector<std::string> c;
    std::string demo = shell::lhde_find("resources/demo");
    if (!demo.empty()) c.push_back(demo);
    std::string sys = shell::longhorn_resources();
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

SIZE text_size(HDC dc, int font, const std::string& s)
{
    HGDIOBJ old = SelectObject(dc, g_fonts[font]);
    std::u16string w = W(s);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
    SelectObject(dc, old);
    return sz;
}

int ascent(HDC dc, int font)
{
    HGDIOBJ old = SelectObject(dc, g_fonts[font]);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmAscent;
}

// a text whose baseline is at `base`
void put_base(HDC dc, int font, const std::string& s, int x, int base, COLORREF c, int right = 100000)
{
    SIZE sz = text_size(dc, font, s);
    int top = base - ascent(dc, font);
    g_texts.push_back(Text{ s, RECT{ x, top, right < x + sz.cx ? right : x + sz.cx, top + sz.cy }, font, c });
}

void put_top(HDC dc, int font, const std::string& s, int x, int top, COLORREF c, int right = 100000)
{
    SIZE sz = text_size(dc, font, s);
    g_texts.push_back(Text{ s, RECT{ x, top, right < x + sz.cx ? right : x + sz.cx, top + sz.cy }, font, c });
}

void redraw()
{
    if (g_wnd) InvalidateRect(g_wnd, nullptr, FALSE);
    if (g_hang) InvalidateRect(g_hang, nullptr, FALSE);
}

// Where everything goes for the width w (layout() with the window's)
void layout_for(HDC dc, int w)
{
    g_texts.clear();
    g_links.clear();
    g_rows.clear();
    g_more = g_props = RECT{};
    bool sel = g_info.kind != Kind::Location;
    int w0;   // the preview's natural width (PreviewTaskPresenter's child 0)
    if (!sel) {
        // WelcomePane: the count over the folder's image
        std::string n = std::to_string(g_info.count), items = shell::details::lh_items_word(g_info.count);
        bool infolder = g_info.count != 0;
        int x = PAD, end;
        if (small()) {
            int base = H / 2 + 6;
            put_base(dc, F_NUMBER, n, x, base, WHITE);
            x += text_size(dc, F_NUMBER, n).cx;
            put_base(dc, F_ITEMS, items, x, base, WHITE);
            x += text_size(dc, F_ITEMS, items).cx;
            if (infolder) { put_base(dc, F_INFOLDER, "in this folder", x + 5, base, WHITE); x += 5 + text_size(dc, F_INFOLDER, "in this folder").cx; }
            end = x;
            w0 = w / 2 > end + PAD ? w / 2 : end + PAD;   // no natural width: half the band
        } else {
            int base2 = H - 14 - (text_size(dc, F_INFOLDER, "Ag").cy - ascent(dc, F_INFOLDER));
            int base1 = infolder ? base2 - text_size(dc, F_INFOLDER, "Ag").cy - 1 : base2;
            put_base(dc, F_NUMBER, n, x, base1, WHITE);
            int ix = x + text_size(dc, F_NUMBER, n).cx;
            put_base(dc, F_ITEMS, items, ix, base1, WHITE);
            end = ix + text_size(dc, F_ITEMS, items).cx;
            if (infolder) { put_base(dc, F_INFOLDER, "in this folder", x, base2, WHITE); end = imax(end, x + (int)text_size(dc, F_INFOLDER, "in this folder").cx); }
            w0 = imax(123, end + PAD);
        }
    } else {
        // PreviewManager: the title, the values in two columns, all properties
        int tx = text_x();
        w0 = tx + 2 * COL + PAD;
        int right = imax(tx + 40, imin(w0, w) - PAD);
        put_top(dc, F_TITLE, shell::details::lh_title(g_info), tx, small() ? 6 : 7, WHITE, right);
        if (!small()) {
            int lh = text_size(dc, F_LINE, "Ag").cy;
            int props_top = H - 14 - lh;
            std::vector<shell::details::LhLine> lines = shell::details::lh_lines(g_info);
            int rows = imax(1, (props_top - 4 - LINE_Y - lh) / LINE_H + 1);
            for (size_t i = 0; i < lines.size() && (int)i < 2 * rows; i++) {
                int col = (int)i / rows, row = (int)i % rows;
                put_top(dc, F_LINE, lines[i].text, tx + col * COL, LINE_Y + row * LINE_H, WHITE, tx + col * COL + COL - 8);
                if (lines[i].editable) {
                    // the row's clickable part: its text, at least a third of the column (an empty-looking value is
                    // still easy to hit)
                    RECT r = g_texts.back().r;
                    if (r.right - r.left < COL / 3) r.right = r.left + COL / 3;
                    g_rows.push_back(EditRow{ r, lines[i].id, lines[i].raw, g_texts.size() - 1 });
                }
            }
            SIZE ps = text_size(dc, F_LINE, "all properties...");
            g_props = RECT{ tx, props_top, tx + ps.cx, props_top + ps.cy };
        }
    }
    // the tasks right of the preview
    int x0 = w0 + 8;
    if (w - w0 < TASK_MIN) return;
    const Task* t = sel ? ITEM_TASKS : FOLDER_TASKS;
    std::vector<Link> all;
    for (int i = 0; i < 5; i++) {
        Link l; l.id = t[i].id; l.text = t[i].text; l.icon = t[i].icon;
        if (l.id == ID_OPEN && g_info.kind == Kind::One) l.icon = g_info.icon;
        all.push_back(l);
    }
    SIZE more = text_size(dc, F_TASK, "more");
    int more_w = 12 + more.cx;
    if (small()) {   // one row, side by side
        int y = (H - TASK_ROW) / 2 + 5, x = x0;
        for (size_t i = 0; i < all.size(); i++) {
            int lw = 16 + 4 + text_size(dc, F_TASK, all[i].text).cx;
            bool last = i + 1 == all.size();
            if (x + lw > w - 8 || (!last && x + lw + 15 + more_w > w - 8)) {
                g_more = RECT{ x, y, x + more_w, y + 16 };
                return;
            }
            all[i].r = RECT{ x, y, x + lw, y + 16 };
            g_links.push_back(all[i]);
            x += lw + 15;
        }
        return;
    }
    int cols = imax(1, (w - x0 - 8) / TASK_COL), rows = imax(1, (H - 2 * TASK_Y) / TASK_ROW);
    int slots = cols * rows;
    int shown = (int)all.size() <= slots - 1 ? (int)all.size() : slots - 1;   // the last slot: more
    for (int i = 0; i < shown; i++) {
        int x = x0 + (i / rows) * TASK_COL, y = TASK_Y + (i % rows) * TASK_ROW + 5;
        all[(size_t)i].r = RECT{ x, y, x + 16 + 4 + text_size(dc, F_TASK, all[(size_t)i].text).cx, y + 16 };
        g_links.push_back(all[(size_t)i]);
    }
    int x = x0 + (shown / rows) * TASK_COL, y = TASK_Y + (shown % rows) * TASK_ROW + 5;
    g_more = RECT{ x, y, x + more_w, y + 16 };
}

void layout()
{
    if (!g_wnd) return;
    RECT rc; GetClientRect(g_wnd, &rc);
    HDC dc = GetDC(g_wnd);
    layout_for(dc, rc.right);
    ReleaseDC(g_wnd, dc);
    redraw();
}

void draw_text(HDC dc, const Text& t)
{
    HGDIOBJ old = SelectObject(dc, g_fonts[t.font]);
    std::u16string w = W(t.text);
    RECT r = t.r;
    r.bottom += 4;
    SetTextColor(dc, t.color);
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

void notify_pictures()
{
    std::vector<std::string> want;
    if (g_pictures_ok) want = g_info.previews;
    if (!g_pics && !want.empty() && g_wnd) g_pics = new preview::Pictures(g_wnd, WM_PICTURE);
    if (g_pics) g_pics->want(want, 256);
}

// one framed picture: 2 px white around it, a 3 px shadow right and below;
// (x, y, w, h) is the frame's outside, `alpha` its opacity
void draw_framed(HDC dc, HBITMAP bmp, int bw, int bh, int x, int y, int alpha)
{
    int w = bw + 4, h = bh + 4;
    hang_add(x, y, x + w + 3, y + h + 3);
    static const int SHADOW[3] = { 150, 90, 40 };
    for (int i = 0; i < 3; i++) {
        preview::fill_alpha(dc, x + w + i, y + 3 + i, 1, h - 3, RGB(0, 0, 0), SHADOW[i] * alpha / 255);
        preview::fill_alpha(dc, x + 3 + i, y + h + i, w - 3 + 1, 1, RGB(0, 0, 0), SHADOW[i] * alpha / 255);
    }
    preview::fill_alpha(dc, x, y, w, h, WHITE, alpha);
    preview::blend(dc, bmp, x + 2, y + 2, bw, bh, alpha);
}

// the preview box: the picture(s) or the icon; nothing selected the folder's image faded
void draw_preview(HDC dc)
{
    int bx = box_x(), bw = box_w();
    if (g_info.kind == Kind::Location) {
        std::string key = g_folder ? g_folder->icon() : "folder";
        draw_icon(dc, key, 128, 4, 8, 90);
        return;
    }
    // the pictures there are, the item of interest first
    struct Pic { HBITMAP bmp; int w, h; };
    std::vector<Pic> pics;
    int n = (int)g_info.previews.size();
    double front = 1.0 - (n > 1 ? (n - 1) / 6.0 * 0.3 : 0.0);
    int maxf = (int)((bw - 12) * front);
    for (int i = 0; g_pics && g_pictures_ok && i < n; i++) {
        int k = i == 0 ? maxf : (int)(maxf * (1.0 - 0.01 * i));
        Pic p = {};
        p.bmp = g_pics->get(g_info.previews[(size_t)i], k - 4, k - 4, &p.w, &p.h);
        if (p.bmp) pics.push_back(p);
    }
    if (pics.empty()) {
        // hanging 11 px out of the band: below it (at the top of the window), above it (at the bottom)
        int size = small() ? 48 : 128, y = g_bottom ? -11 : H + 11 - size;
        draw_icon(dc, g_info.icon, size, bx + (bw - size) / 2, y, 255);
        hang_add(bx + (bw - size) / 2, y, bx + (bw + size) / 2, y + size);
        return;
    }
    // the pictures hang 10 px out of the band: their bottom 10 below it, or (at the bottom) their top 10 above it
    int bottom = H + 10, top = -10;
    if (pics.size() == 1) {
        const Pic& p = pics[0];
        draw_framed(dc, p.bmp, p.w, p.h, bx + (bw - p.w - 4) / 2, g_bottom ? top : bottom - p.h - 4, 255);
        return;
    }
    // the stack: the front one at the box's right and bottom (as one picture of
    // the box's width would end), the others up and left of it; mirrored at the
    // bottom of the window (the front one at the top, the others down and left)
    int right = bx + bw - 6;
    double dx = bw * 7.0 / 134, dy = bw * 6.0 / 134;
    for (int i = (int)pics.size() - 1; i >= 0; i--) {
        const Pic& p = pics[(size_t)i];
        int x = right - (int)(dx * i) - (p.w + 4);
        int y = g_bottom ? top + (int)(dy * i) : bottom - (int)(dy * i) - (p.h + 4);
        draw_framed(dc, p.bmp, p.w, p.h, x, y, 255);
    }
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
    // the band's end towards the list (4051: a light row, then a dark one; at the bottom of the window mirrored, its top edge)
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = -2; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP b = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (b) {
        ((uint32_t*)bits)[g_bottom ? 1 : 0] = 0x60606060;   // white at 0.38, premultiplied
        ((uint32_t*)bits)[g_bottom ? 0 : 1] = 0x90000000;   // black at 0.56
        HGDIOBJ old = SelectObject(mem, b);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(dc, 0, g_bottom ? 0 : H - 2, w, 2, mem, 0, 0, 1, 2, bf);
        SelectObject(mem, old);
        DeleteObject(b);
    }
    DeleteDC(mem);
    // beside the band (what the preview hangs over): the left pane's white
    RECT under = g_bottom ? RECT{ 0, -HANG, w, 0 } : RECT{ 0, H, w, H + HANG };
    HBRUSH wb = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(dc, &under, wb);
    DeleteObject(wb);
}

// The band and what hangs out of it, into a bitmap w x (H + HANG): the band
// from row band_y(), drawn in band coordinates
HBITMAP render(HDC wdc, int w)
{
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP buf = CreateCompatibleBitmap(wdc, w, H + HANG);
    HGDIOBJ oldb = SelectObject(dc, buf);
    SetViewportOrgEx(dc, 0, band_y(), nullptr);
    paint_background(dc, w);
    SetBkMode(dc, TRANSPARENT);
    g_hang_rc = RECT{};
    draw_preview(dc);
    for (size_t i = 0; i < g_texts.size(); i++) {
        Text t = g_texts[i];
        for (size_t k = 0; k < g_rows.size(); k++)
            if (g_rows[k].text == i && g_hot == ROW_HOT - (int)k) { t.font = F_LINE_U; t.color = MORE_HOT; }
        draw_text(dc, t);
    }
    if (g_props.right > g_props.left)
        draw_text(dc, Text{ "all properties...", g_props, g_hot == -3 ? F_LINE_U : F_LINE, g_hot == -3 ? MORE_HOT : MORE_TEXT });
    for (size_t i = 0; i < g_links.size(); i++) {
        const Link& l = g_links[i];
        if (!l.icon.empty()) draw_icon(dc, l.icon, 16, l.r.left, l.r.top, 255);
        RECT r = l.r; r.left += 20; r.top -= 1;
        draw_text(dc, Text{ l.text, r, (int)i == g_hot ? F_TASK_U : F_TASK, TASK_TEXT });
    }
    if (g_more.right > g_more.left) {
        // more: imageres 4110's 5 x 3 down arrow, then the word
        int ax = g_more.left + 2, ay = g_more.top + 7;
        HPEN pen = CreatePen(PS_SOLID, 1, TASK_TEXT);
        HGDIOBJ op = SelectObject(dc, pen);
        for (int k = 0; k < 3; k++) { MoveToEx(dc, ax + k, ay + k, nullptr); LineTo(dc, ax + 5 - k, ay + k); }
        SelectObject(dc, op);
        DeleteObject(pen);
        RECT r = g_more; r.left += 12; r.top -= 1;
        draw_text(dc, Text{ "more", r, g_hot == -2 ? F_TASK_U : F_TASK, TASK_TEXT });
    }
    SetViewportOrgEx(dc, 0, 0, nullptr);
    SelectObject(dc, oldb);
    DeleteDC(dc);
    return buf;
}

void place_hang();
void paint(HWND h, HDC wdc)
{
    RECT rc; GetClientRect(g_wnd, &rc);
    int w = rc.right;
    if (w <= 0) return;
    HBITMAP buf = render(wdc, w);
    HDC mem = CreateCompatibleDC(wdc);
    HGDIOBJ old = SelectObject(mem, buf);
    if (h == g_wnd) BitBlt(wdc, 0, 0, w, H, mem, 0, band_y(), SRCCOPY);
    else BitBlt(wdc, 0, 0, g_hang_rc.right - g_hang_rc.left, g_hang_rc.bottom - g_hang_rc.top, mem, g_hang_rc.left,
                g_hang_rc.top + band_y(), SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(buf);
    static RECT placed;
    if (h == g_wnd && !EqualRect(&placed, &g_hang_rc)) { placed = g_hang_rc; place_hang(); }
}

// the overhang window: over the left pane, under the preview box (above it at the bottom)
void place_hang()
{
    if (!g_wnd || !g_hang) return;
    RECT r; GetWindowRect(g_wnd, &r);
    POINT p = { r.left, r.top };
    ScreenToClient(GetParent(g_wnd), &p);
    bool show = IsWindowVisible(g_wnd) && g_hang_rc.right > g_hang_rc.left;
    if (!show) { if (IsWindowVisible(g_hang)) ShowWindow(g_hang, SW_HIDE); return; }   // (never moved to 0 x 0)
    SetWindowPos(g_hang, HWND_TOP, p.x + g_hang_rc.left, p.y + g_hang_rc.top, g_hang_rc.right - g_hang_rc.left,
                 g_hang_rc.bottom - g_hang_rc.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hang, nullptr, FALSE);
}

int hit(POINT p)
{
    for (size_t i = 0; i < g_links.size(); i++) if (PtInRect(&g_links[i].r, p)) return (int)i;
    for (size_t i = 0; i < g_rows.size(); i++) if (PtInRect(&g_rows[i].r, p)) return ROW_HOT - (int)i;
    if (PtInRect(&g_more, p)) return -2;
    if (PtInRect(&g_props, p)) return -3;
    return -1;
}

// ---- the row editor -----------------------------------------------------------------

void close_editor(bool keep)
{
    if (!g_edit || g_edit_closing) return;
    g_edit_closing = true;
    std::string text;
    if (keep) {
        int n = GetWindowTextLengthW(g_edit);
        std::vector<WCHAR> buf((size_t)n + 1);
        GetWindowTextW(g_edit, buf.data(), n + 1);
        text = w32::utf8_of(buf.data());
    }
    HWND e = g_edit;
    g_edit = nullptr;
    std::string id = g_edit_id, raw = g_edit_raw, path = g_edit_path;
    DestroyWindow(e);
    g_edit_closing = false;
    if (keep && text != raw && g_cb.edit) g_cb.edit(path, id, text);
}

LRESULT CALLBACK edit_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
    case WM_KEYDOWN:
        if (wp == VK_RETURN) { close_editor(true); return 0; }
        if (wp == VK_ESCAPE) { close_editor(false); return 0; }
        break;
    case WM_CHAR:
        if (wp == '\r' || wp == 27) return 0;   // (no beep)
        break;
    case WM_KILLFOCUS:
        // 4051's popup editor keeps the value when it closes; posted: the focus is moving now
        if (!g_edit_closing) PostMessageW(GetParent(h), WM_APP + 0x32, 0, 0);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void open_editor(size_t row)
{
    close_editor(true);
    if (row >= g_rows.size() || !g_wnd) return;
    const EditRow& r = g_rows[row];
    g_edit_id = r.id;
    g_edit_raw = r.raw;
    g_edit_path = g_info.path;
    int lh = r.r.bottom - r.r.top;
    int x = r.r.left - 3, w = COL - 4;
    g_edit = CreateWindowExW(0, L"Edit", (LPCWSTR)W(r.raw).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                             x, r.r.top - 3, w, lh + 6, g_wnd, nullptr, nullptr, nullptr);
    if (!g_edit) return;
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_fonts[F_LINE], TRUE);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
    SetWindowSubclass(g_edit, edit_subclass, 1, 0);
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
}

LRESULT CALLBACK hang_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        bool grip = g_bottom ? p.y < GRIP : p.y >= H - GRIP;   // the edge towards the list
        LPCWSTR c = g_sizing || grip ? (LPCWSTR)IDC_SIZENS : hit(p) != -1 ? (LPCWSTR)IDC_HAND : (LPCWSTR)IDC_ARROW;
        SetCursor(LoadCursorW(nullptr, c));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (g_bottom ? p.y >= GRIP : p.y < H - GRIP) return 0;
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
            int want = g_size_h0 + (g_bottom ? g_size_y0 - s.y : s.y - g_size_y0);   // at the bottom it grows upwards
            bool compact = want < SPLIT;
            int eh = g_exp_h;
            if (!compact) eh = want < EXP_MIN ? EXP_MIN : want;
            else if (!g_size_compact0) eh = g_size_h0;   // expands again to where it was
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
        if (i != g_hot) { g_hot = i; redraw(); }
        if (!g_tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            g_tracking = TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        if (g_hot != -1) { g_hot = -1; redraw(); }
        return 0;
    case WM_LBUTTONUP: {
        if (g_sizing) { ReleaseCapture(); return 0; }
        POINT p = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        int i = hit(p);
        bool sel = g_info.kind != Kind::Location;
        if (i == -2 && g_cb.more) {
            POINT s = { g_more.left, g_more.bottom };
            ClientToScreen(h, &s);
            g_cb.more(sel, s.x, s.y);
        } else if (i == -3 && g_cb.command) {
            g_cb.command(ID_PROPERTIES, true);     // MoreButton: the selection's "properties" verb
        } else if (i <= ROW_HOT) {
            open_editor((size_t)(ROW_HOT - i));
        } else if (i >= 0 && g_cb.command) {
            int id = g_links[(size_t)i].id;
            g_cb.command(id, sel);
        }
        return 0;
    }
    case WM_SIZE:
        layout();
        place_hang();
        return 0;
    case WM_WINDOWPOSCHANGED:
        place_hang();
        break;
    case WM_SHOWWINDOW:
        if (g_hang) ShowWindow(g_hang, wp && g_hang_rc.right > g_hang_rc.left ? SW_SHOWNA : SW_HIDE);
        break;
    case WM_PICTURE:
        if (g_pics && g_pics->take()) redraw();
        return 0;
    case WM_APP + 0x32:                     // the editor lost the focus
        if (g_edit && GetFocus() != g_edit) close_editor(true);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

bool wanted() { return shell::setting("taskpane", "0") == "1"; }

int height() { return H; }
bool bottom() { return g_bottom; }
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
    int eh = atoi(shell::setting("taskpane_height", "126").c_str());
    g_exp_h = eh < EXP_MIN ? EXP_MIN : eh > EXP_MAX ? EXP_MAX : eh;
    g_compact = shell::setting("taskpane_compact", "0") == "1";
    g_bottom = read_bottom();
    H = effective();
    make_fonts();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_taskband";
    RegisterClassW(&wc);
    wc.lpfnWndProc = hang_proc;
    wc.lpszClassName = L"flxp_taskband_hang";
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, L"flxp_taskband", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 0, H, frame, nullptr, nullptr, nullptr);
    g_hang = CreateWindowExW(0, L"flxp_taskband_hang", nullptr, WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 0, 0, frame, nullptr, nullptr, nullptr);
    notify_pictures();
    layout();
    return g_wnd;
}

void destroy()
{
    close_editor(false);
    if (g_hang) DestroyWindow(g_hang);
    if (g_wnd) DestroyWindow(g_wnd);
    g_wnd = g_hang = nullptr;
    g_hot = -1;
    g_tracking = false;
    delete g_pics;
    g_pics = nullptr;
}

HWND window() { return g_wnd; }

void set_folder(const std::shared_ptr<shell::Folder>& f)
{
    g_folder = f;
    layout();
}

void set_details(const Info& in, bool pictures)
{
    // another item (or none): its editor goes, keeping what was typed for the item it was for
    if (g_edit && (in.kind != Kind::One || in.path != g_edit_path)) close_editor(true);
    g_info = in;
    g_pictures_ok = pictures;
    notify_pictures();
    layout();
}

void theme_changed()
{
    g_bottom = read_bottom();   // (the explorer lays the band out again)
    g_aurora_looked = false;
    g_aurora_file = "-";
    redraw();
}

std::string source()
{
    std::string d = aurora_dir();
    if (!d.empty()) return "the Longhorn demo's aurora images (" + d + "), by folder kind";
    return "the command bar's background (the theme's Rebar.BACKGROUND strip or its gradient)";
}

}
