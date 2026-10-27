// XP's start panel (explorer/desktop2 in the XP SP1 source, read as the
// spec: deskhost.cpp, userpane.cpp, sfthost.cpp/proglist.cpp, specfldr.cpp,
// moreprog.cpp, logoff.cpp): one popup window drawn with the StartPanel
// classes of the visual style (Luna, and Longhorn's Slate/Jade, which carry
// the same parts), or XP's non-themed colours without them. 380 px wide at
// 96 dpi: the user pane (64), two columns of 190 (the program list with the
// pinned items, a separator and the most frequently used programs over "All
// Programs"; the places list), the log off pane (40). Self-drawn, no child
// controls. Runs its own modal loop like a menu: the dock takes the keyboard,
// the panel the capture; it ends on Escape, a click outside, a command, or
// WM_CANCELMODE (the dock lost the focus: a click into another application).
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>
#include "explorer/uiicons.h"
#include "shell/icons.h"
#include "gdi/image.h"
#include "shell/apps.h"
#include "shell/config.h"
#include "sys/sys.h"
#include "taskbar.h"

namespace tb {
namespace startpanel {

namespace {

// 96 dpi metrics: StartPanel DEFAULTPANESIZE 380x440, USERPANE 380x64, PROGLIST 190x306,
// MOREPROGRAMS 190x30, PLACESLIST 190x336, LOGOFF 380x40 (Luna, Slate and Jade alike)
const int PANEL_W = 380, USER_H = 64, COL_W = 190, COL_MIN_H = 336, MORE_H = 30, LOGOFF_H = 40;
const int USERPIC = 48;                 // userpane.h USERPICWIDTH/HEIGHT
const int MFU_MAX = 6;                  // XP's default "number of programs on Start menu"
const UINT TIMER_HOVER = 1;             // All Programs opens after hovering (as XP's menus: SPI_GETMENUSHOWDELAY 400)
const UINT TIMER_SWITCH = 2;            // another item hovered while a flyout is open: it closes after the same delay

enum Kind { KApp, KCommand, KTerminal, KExplorer, KMore, KDocuments, KPictures, KComputer, KControl, KRun, KSearch, KLogOff, KTurnOff };
enum Col { ColLeft, ColRight, ColFooter };

struct Item {
    Kind kind;
    Col col;
    std::string text, arg;              // arg: the .desktop id, the command or the folder
    HICON icon = nullptr;
    int icon_size = 0;
    bool bold = false;
    RECT rc = {};
};

struct Sep { RECT rc; int part; };

struct Panel {
    HWND wnd = nullptr;
    HTHEME th = nullptr;
    std::vector<Item> items;
    std::vector<Sep> seps;
    int hot = -1;
    bool done = false;
    bool had_focus = false;             // the dock got the keyboard (focus_own is asynchronous)
    int chosen = -1;                    // the item whose action runs after the panel is gone
    UINT menu_cmd = 0;                  // or a command from a flyout (startmenu::command)
    int open_item = -1;                 // the item whose flyout is open (All Programs, Control Panel)
    int reopen = -1;                    // the cascade to open once that flyout has ended
    HFONT font = nullptr, bold = nullptr, user_font = nullptr, more_font = nullptr;
    HICON user_pic = nullptr;
    SIZE pic_size = {};
    std::string user;
    int w = PANEL_W, h = 0, col_h = 0;
    int user_h = USER_H;                // 0: the style has no user pane (4074's Jade, Aero)
    HBITMAP classic_logoff[2] = {};     // desktop2's LogoffButtons.bmp / LogoffButtonsHot.bmp (non-themed)
};
Panel* P;

std::string home() { return shell::home_dir(); }

bool is_dir(const std::string& p)
{
    struct stat st;
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_file(const std::string& p)
{
    struct stat st;
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string trim(std::string s)
{
    s.erase(0, s.find_first_not_of(" \t"));
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

// XDG user dirs ($XDG_CONFIG_HOME/user-dirs.dirs), "" when unset or missing
std::string user_dir(const char* key, const char* fallback)
{
    std::string text, out, h = home();
    if (shell::read_file(shell::config_home() + "/user-dirs.dirs", text)) {
        size_t i = 0;
        while (i < text.size()) {
            size_t j = text.find('\n', i);
            if (j == std::string::npos) j = text.size();
            std::string line = text.substr(i, j - i);
            i = j + 1;
            size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos || line.substr(0, eq) != key) continue;
            out = line.substr(eq + 1);
            if (out.size() >= 2 && out[0] == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
            if (out.compare(0, 5, "$HOME") == 0) out = h + out.substr(5);
        }
    }
    if (out.empty()) out = h + "/" + fallback;
    if (out == h || out == h + "/") return "";
    return is_dir(out) ? out : "";
}

// ---- most frequently used: "COUNT ID" lines in $XDG_DATA_HOME/flxp/mfu ---------------

std::string mfu_path() { return shell::data_home() + "/flxp/mfu"; }

std::vector<std::pair<int, std::string>> load_mfu()
{
    std::vector<std::pair<int, std::string>> v;
    std::string text;
    if (!shell::read_file(mfu_path(), text)) return v;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        i = j + 1;
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        int n = atoi(line.c_str());
        std::string id = trim(line.substr(sp + 1));
        if (n > 0 && !id.empty()) v.push_back({ n, id });
    }
    std::stable_sort(v.begin(), v.end(), [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) { return a.first > b.first; });
    return v;
}

// ---- icons ----------------------------------------------------------------------------

HICON icon_from(gdi::Bitmap* src, int w, int h)
{
    gdi::Bitmap* b = gdi::resample(src, w, h);
    if (!b) return nullptr;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HICON ic = nullptr;
    if (color) {
        for (int y = 0; y < h; y++) memcpy((uint32_t*)bits + y * w, b->row(y), (size_t)w * 4);
        std::vector<uint8_t> zeros((size_t)((w + 15) / 16 * 2) * h);
        HBITMAP mask = CreateBitmap(w, h, 1, 1, zeros.data());
        ICONINFO ii = {};
        ii.fIcon = TRUE;
        ii.hbmColor = color;
        ii.hbmMask = mask;
        ic = CreateIconIndirect(&ii);
        DeleteObject(mask);
        DeleteObject(color);
    }
    gdi::destroy_bitmap(b);
    return ic;
}

// a .desktop Icon=: an absolute file, else NAME.png in the hicolor/Adwaita trees or /usr/share/pixmaps
std::string icon_path(const std::string& name, int size)
{
    if (name.empty()) return "";
    if (name[0] == '/') return is_file(name) ? name : "";
    std::vector<std::string> bases = { shell::data_home() + "/icons" };
    const char* dd = getenv("XDG_DATA_DIRS");
    std::string dirs = dd && *dd ? dd : "/usr/local/share:/usr/share";
    for (size_t a = 0; a <= dirs.size();) {
        size_t b = dirs.find(':', a);
        if (b == std::string::npos) b = dirs.size();
        if (b > a) bases.push_back(dirs.substr(a, b - a) + "/icons");
        a = b + 1;
    }
    std::string file = name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0 ? name : name + ".png";
    const int sizes[] = { size, 32, 48, 64, 128, 256, 24, 22 };
    for (const char* theme : { "hicolor", "Adwaita" })
        for (int s : sizes)
            for (const std::string& b : bases) {
                std::string p = b + "/" + theme + "/" + std::to_string(s) + "x" + std::to_string(s) + "/apps/" + file;
                if (is_file(p)) return p;
            }
    for (const char* d : { "/usr/share/pixmaps/", "/usr/local/share/pixmaps/" })
        if (is_file(d + file)) return d + file;
    return "";
}

HICON app_icon(const std::string& name, int size)
{
    std::string p = icon_path(name, size);
    if (p.empty()) return nullptr;
    gdi::Bitmap* b = gdi::load_image(p);
    if (!b) return nullptr;
    HICON ic = icon_from(b, size, size);
    gdi::destroy_bitmap(b);
    return ic;
}

// premultiplied 32 bpp top-down DIB (menu bitmaps are alpha-blended: menu.c flxp)
HBITMAP dib32(int w, int h, uint32_t** bits)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    return CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)bits, nullptr, 0);
}

HBITMAP bitmap_from(gdi::Bitmap* src, int size)
{
    gdi::Bitmap* b = gdi::resample(src, size, size);
    if (!b) return nullptr;
    uint32_t* bits = nullptr;
    HBITMAP bm = dib32(size, size, &bits);
    if (bm)
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                uint32_t p = b->row(y)[x], a = p >> 24;
                uint32_t r = ((p >> 16) & 255) * a / 255, g = ((p >> 8) & 255) * a / 255, bl = (p & 255) * a / 255;
                bits[y * size + x] = a << 24 | r << 16 | g << 8 | bl;
            }
    gdi::destroy_bitmap(b);
    return bm;
}

// an HICON with alpha: drawn over black and over white, alpha = 255 - the largest channel difference
HBITMAP bitmap_from(HICON ic, int size)
{
    uint32_t *k = nullptr, *wh = nullptr;
    HBITMAP bk = dib32(size, size, &k), bw = dib32(size, size, &wh);
    HDC dc = CreateCompatibleDC(nullptr);
    RECT r = { 0, 0, size, size };
    HGDIOBJ o = SelectObject(dc, bk);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    DrawIconEx(dc, 0, 0, ic, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(dc, bw);
    FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawIconEx(dc, 0, 0, ic, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(dc, o);
    DeleteDC(dc);
    for (int i = 0; i < size * size; i++) {
        int d = 0;
        for (int sh = 0; sh < 24; sh += 8) d = (std::max)(d, (int)((wh[i] >> sh) & 255) - (int)((k[i] >> sh) & 255));
        uint32_t a = (uint32_t)(255 - (std::min)(255, (std::max)(0, d)));
        k[i] = (k[i] & 0xffffff) | a << 24;
    }
    DeleteObject(bw);
    return bk;
}

std::string base_name(const std::string& s)
{
    size_t k = s.rfind('/');
    return k == std::string::npos ? s : s.substr(k + 1);
}

std::string first_word(const std::string& s)
{
    std::string t = trim(s);
    return t.substr(0, t.find_first_of(" \t"));
}

// the application whose Exec= runs this program (for a pinned command's name and icon)
const shell::App* app_for_program(const std::string& prog)
{
    std::string p = base_name(prog);
    for (const shell::App& a : shell::all_apps())
        if (base_name(first_word(a.exec)) == p) return &a;
    return nullptr;
}

// ---- building ------------------------------------------------------------------------

void add(Kind k, Col c, const std::string& text, const std::string& arg, HICON icon, int size, bool bold = false)
{
    Item it;
    it.kind = k; it.col = c; it.text = text; it.arg = arg; it.icon = icon; it.icon_size = size; it.bold = bold;
    P->items.push_back(it);
}

HICON key_icon(const char* key, int size) { return uiicons::load_icon(key, size); }

void add_app(const shell::App& a, bool bold)
{
    HICON ic = app_icon(a.icon, 32);
    if (!ic) ic = key_icon("exe", 32);
    add(KApp, ColLeft, a.name, a.id, ic, 32, bold);
}

// pinned = item;item...: a .desktop id, `terminal`, `explorer`, or a command line
void add_pinned(std::vector<std::string>& ids)
{
    std::string list = setting("pinned", "terminal;explorer");
    size_t a = 0;
    while (a <= list.size()) {
        size_t b = list.find(';', a);
        if (b == std::string::npos) b = list.size();
        std::string e = trim(list.substr(a, b - a));
        a = b + 1;
        if (e.empty()) continue;
        if (e == "terminal") {
            const shell::App* ap = app_for_program(first_word(shell::terminal_command()));
            HICON ic = ap ? app_icon(ap->icon, 32) : nullptr;
            add(KTerminal, ColLeft, "Terminal", "", ic ? ic : key_icon("exe", 32), 32, true);
        } else if (e == "explorer") {
            add(KExplorer, ColLeft, "Explorer", home(), key_icon("folder_open", 32), 32, true);
        } else if (e.size() > 8 && e.compare(e.size() - 8, 8, ".desktop") == 0) {
            if (const shell::App* ap = shell::find_app(e)) { add_app(*ap, true); ids.push_back(ap->id); }
        } else {
            const shell::App* ap = app_for_program(first_word(e));
            HICON ic = ap ? app_icon(ap->icon, 32) : nullptr;
            add(KCommand, ColLeft, ap ? ap->name : base_name(first_word(e)), e, ic ? ic : key_icon("exe", 32), 32, true);
        }
    }
}

void build_items()
{
    shell::reload_apps();
    std::vector<std::string> pinned_ids;
    add_pinned(pinned_ids);
    int n = 0;
    for (const auto& m : load_mfu()) {
        if (n >= MFU_MAX) break;
        if (std::find(pinned_ids.begin(), pinned_ids.end(), m.second) != pinned_ids.end()) continue;
        const shell::App* ap = shell::find_app(m.second);
        if (!ap || ap->no_display) continue;
        add_app(*ap, false);
        n++;
    }
    add(KMore, ColLeft, "All Programs", "", nullptr, 0, true);

    add(KDocuments, ColRight, "My Documents", home(), key_icon("home", 24), 24, true);
    std::string pics = user_dir("XDG_PICTURES_DIR", "Pictures");
    if (!pics.empty()) add(KPictures, ColRight, "My Pictures", pics, key_icon("pictures", 24), 24, true);
    add(KComputer, ColRight, "My Computer", "::computer", key_icon("computer", 24), 24, true);
    add(KControl, ColRight, "Control Panel", "", key_icon("control_panel", 24), 24);
    add(KRun, ColRight, "Run...", "", key_icon("run", 24), 24);
    add(KSearch, ColRight, "Search", home(), key_icon("search", 24), 24);

    add(KLogOff, ColFooter, "Log Off", "", nullptr, 24);
    add(KTurnOff, ColFooter, "Turn Off Computer", "", nullptr, 24);
}

MARGINS margins(int part, int l, int r, int t, int b)
{
    MARGINS m = { l, r, t, b };
    if (P->th) GetThemeMargins(P->th, nullptr, part, 0, TMT_CONTENTMARGINS, nullptr, &m);
    return m;
}

int sep_height(int part)
{
    SIZE s = { 0, 2 };
    if (P->th) GetThemePartSize(P->th, nullptr, part, 0, nullptr, TS_TRUE, &s);
    return s.cy + 6;
}

int text_width(HFONT f, const std::string& s)
{
    HDC dc = GetDC(nullptr);
    HGDIOBJ o = SelectObject(dc, f);
    std::u16string w = W(s);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
    SelectObject(dc, o);
    ReleaseDC(nullptr, dc);
    return sz.cx;
}

// sfthost.cpp: tile = icon + 4 * SM_CYEDGE; separators after the pinned items, after My Computer and Control Panel
void layout()
{
    int top = P->user_h;
    MARGINS ml = margins(SPP_PROGLIST, 6, 9, 4, 5), mr = margins(SPP_PLACESLIST, 4, 9, 6, 5);
    int y = top + ml.cyTopHeight, sep_l = sep_height(SPP_PROGLISTSEPARATOR), sep_r = sep_height(SPP_PLACESLISTSEPARATOR);
    bool pinned_done = false;
    for (Item& it : P->items) {
        if (it.col != ColLeft || it.kind == KMore) continue;
        if (!it.bold && !pinned_done) {      // the first MFU item: the separator under the pinned ones
            pinned_done = true;
            if (&it != &P->items[0]) { P->seps.push_back({ { ml.cxLeftWidth, y, COL_W - ml.cxRightWidth, y + sep_l }, SPP_PROGLISTSEPARATOR }); y += sep_l; }
        }
        it.rc = { ml.cxLeftWidth, y, COL_W - ml.cxRightWidth, y + 32 + 8 };
        y = it.rc.bottom;
    }
    int left_need = y + ml.cyBottomHeight - top + MORE_H;
    y = top + mr.cyTopHeight;
    for (Item& it : P->items) {
        if (it.col != ColRight) continue;
        if (it.kind == KControl || it.kind == KRun) {
            P->seps.push_back({ { COL_W + mr.cxLeftWidth, y, P->w - mr.cxRightWidth, y + sep_r }, SPP_PLACESLISTSEPARATOR });
            y += sep_r;
        }
        it.rc = { COL_W + mr.cxLeftWidth, y, P->w - mr.cxRightWidth, y + 24 + 8 };
        y = it.rc.bottom;
    }
    int right_need = y + mr.cyBottomHeight - top;
    P->col_h = (std::max)(COL_MIN_H, (std::max)(left_need, right_need));
    P->h = P->user_h + P->col_h + LOGOFF_H;

    // All Programs (moreprog.cpp): the button fills the pane's content rectangle
    MARGINS mm = margins(SPP_MOREPROGRAMS, 8, 8, 0, 6);
    int mt = top + P->col_h - MORE_H;
    for (Item& it : P->items) {
        if (it.kind == KMore) it.rc = { mm.cxLeftWidth, mt + mm.cyTopHeight, COL_W - mm.cxRightWidth, top + P->col_h - mm.cyBottomHeight };
    }
    // log off pane (logoff.cpp): a flat list toolbar of 24 px images with text, right-aligned
    MARGINS mo = margins(SPP_LOGOFF, 0, 0, 4, 2);
    int lt = P->h - LOGOFF_H + mo.cyTopHeight, lb = P->h - mo.cyBottomHeight;
    int x = P->w - mo.cxRightWidth - 6;
    for (int i = (int)P->items.size() - 1; i >= 0; i--) {
        Item& it = P->items[i];
        if (it.col != ColFooter) continue;
        int wdt = 4 + 24 + 4 + text_width(P->font, it.text) + 6;
        int cy = (lt + lb) / 2;
        it.rc = { x - wdt, cy - 15, x, cy + 15 };
        x -= wdt + 4;
    }
}

// ---- painting ------------------------------------------------------------------------

COLORREF theme_color(int part, int prop, COLORREF def)
{
    COLORREF c = def;
    if (P->th) GetThemeColor(P->th, part, 0, prop, &c);
    return c;
}

void text_out(HDC dc, const std::string& s, RECT r, UINT flags)
{
    std::u16string w = W(s);
    DrawTextW(dc, (LPCWSTR)w.c_str(), (int)w.size(), &r, flags | DT_SINGLELINE | DT_NOPREFIX);
}

void draw_footer_image(HDC dc, int idx, bool hot, int x, int y)
{
    // LOGOFFBUTTONS: three 24x24 cells (eject, log off, turn off); 5112's aero has two (lock, power)
    if (P->th) {
        SIZE ss = { 72, 24 };
        GetThemePartSize(P->th, nullptr, SPP_LOGOFFBUTTONS, SPLS_NORMAL, nullptr, TS_TRUE, &ss);
        int cells = (std::max)(1, (std::min)(3, (int)ss.cx / 24));
        if (cells < 3) idx = (std::max)(0, idx - (3 - cells));
        RECT strip = { x - idx * 24, y, x - idx * 24 + cells * 24, y + 24 }, clip = { x, y, x + 24, y + 24 };
        DrawThemeBackground(P->th, dc, SPP_LOGOFFBUTTONS, hot ? SPLS_HOT : SPLS_NORMAL, &strip, &clip);
        return;
    }
    HBITMAP b = P->classic_logoff[hot ? 1 : 0];
    if (!b) b = P->classic_logoff[0];
    if (!b) return;
    HDC m = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(m, b);
    BitBlt(dc, x, y, 24, 24, m, idx * 24, 0, SRCCOPY);
    SelectObject(m, o);
    DeleteDC(m);
}

void draw_arrow(HDC dc, const RECT& r, COLORREF c)
{
    // the non-themed More Programs arrow (Marlett's right triangle): a 4 px high triangle
    int cx = r.left + 4, cy = (r.top + r.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ o = SelectObject(dc, pen);
    for (int i = 0; i < 4; i++) { MoveToEx(dc, cx + i, cy - 3 + i, nullptr); LineTo(dc, cx + i, cy + 4 - i); }
    SelectObject(dc, o);
    DeleteObject(pen);
}

// the themed panes' backgrounds (deskhost.cpp: each pane draws its own part)
void draw_panes(HDC dc)
{
    const int UH = P->user_h;
    RECT user = { 0, 0, P->w, UH }, left = { 0, UH, COL_W, UH + P->col_h - MORE_H },
         more = { 0, UH + P->col_h - MORE_H, COL_W, UH + P->col_h }, right = { COL_W, UH, P->w, UH + P->col_h },
         logoff = { 0, P->h - LOGOFF_H, P->w, P->h };
    if (UH) DrawThemeBackground(P->th, dc, SPP_USERPANE, 0, &user, nullptr);
    DrawThemeBackground(P->th, dc, SPP_PROGLIST, 0, &left, nullptr);
    DrawThemeBackground(P->th, dc, SPP_MOREPROGRAMS, 0, &more, nullptr);
    DrawThemeBackground(P->th, dc, SPP_PLACESLIST, 0, &right, nullptr);
    DrawThemeBackground(P->th, dc, SPP_LOGOFF, 0, &logoff, nullptr);
}

// XP's panel has round top corners: the panes' images are transparent there (Luna's user pane, Slate's; Jade's
// lists). The window region leaves out, in each corner square, the pixels the panes do not cover (drawn over
// black and over white: unchanged in both = transparent), row by row from the edge inwards.
void apply_shape()
{
    if (!P->th) return;
    const int R = 12;
    uint32_t *k = nullptr, *wh = nullptr;
    HBITMAP bk = dib32(P->w, P->h, &k), bw = dib32(P->w, P->h, &wh);
    if (!bk || !bw) { if (bk) DeleteObject(bk); if (bw) DeleteObject(bw); return; }
    HDC dc = CreateCompatibleDC(nullptr);
    RECT all = { 0, 0, P->w, P->h };
    HGDIOBJ o = SelectObject(dc, bk);
    FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
    draw_panes(dc);
    SelectObject(dc, bw);
    FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    draw_panes(dc);
    SelectObject(dc, o);
    DeleteDC(dc);
    auto clear = [&](int x, int y) {
        uint32_t a = k[y * P->w + x] & 0xffffff, b = wh[y * P->w + x] & 0xffffff;
        int d = 0;
        for (int sh = 0; sh < 24; sh += 8) d = (std::max)(d, (int)((b >> sh) & 255) - (int)((a >> sh) & 255));
        return d >= 128;    // less than half covered
    };
    HRGN rgn = CreateRectRgn(0, 0, P->w, P->h);
    bool cut = false;
    for (int y = 0; y < R && y < P->h; y++)
        for (int bottom = 0; bottom < 2; bottom++) {
            int row = bottom ? P->h - 1 - y : y, l = 0, r = 0;
            while (l < R && clear(l, row)) l++;
            while (r < R && clear(P->w - 1 - r, row)) r++;
            if (l) { HRGN c = CreateRectRgn(0, row, l, row + 1); CombineRgn(rgn, rgn, c, RGN_DIFF); DeleteObject(c); cut = true; }
            if (r) { HRGN c = CreateRectRgn(P->w - r, row, P->w, row + 1); CombineRgn(rgn, rgn, c, RGN_DIFF); DeleteObject(c); cut = true; }
        }
    DeleteObject(bk);
    DeleteObject(bw);
    if (cut) SetWindowRgn(P->wnd, rgn, FALSE);
    else DeleteObject(rgn);
}

void paint(HDC dc)
{
    RECT all = { 0, 0, P->w, P->h };
    const int UH = P->user_h;
    RECT user = { 0, 0, P->w, UH }, more = { 0, UH + P->col_h - MORE_H, COL_W, UH + P->col_h }, logoff = { 0, P->h - LOGOFF_H, P->w, P->h };
    HTHEME th = P->th;
    SetBkMode(dc, TRANSPARENT);
    COLORREF hl = GetSysColor(th ? COLOR_MENUHILIGHT : COLOR_HIGHLIGHT), hl_text = GetSysColor(COLOR_HIGHLIGHTTEXT);

    if (th) {
        FillRect(dc, &all, GetSysColorBrush(COLOR_MENU));
        draw_panes(dc);
    } else {
        // XP without a visual style: the menu colour, the panes in the selection colour, a raised frame
        FillRect(dc, &all, GetSysColorBrush(COLOR_MENU));
        FillRect(dc, &user, GetSysColorBrush(COLOR_HIGHLIGHT));
        FillRect(dc, &logoff, GetSysColorBrush(COLOR_HIGHLIGHT));
        RECT v = { COL_W - 1, UH + 4, COL_W + 1, UH + P->col_h - 4 };
        DrawEdge(dc, &v, EDGE_ETCHED, BF_LEFT);
        RECT hsep = { 4, more.top, COL_W - 4, more.top + 2 };
        DrawEdge(dc, &hsep, EDGE_ETCHED, BF_TOP);
    }

    // the user pane: picture in its frame, the name (userpane.cpp); none in Jade/Aero (no USERPANE part)
    int name_x = 10;
    if (UH && P->user_pic) {
        MARGINS pm = margins(SPP_USERPICTURE, 0, 0, 0, 0);
        int fw = USERPIC + pm.cxLeftWidth + pm.cxRightWidth, fh = USERPIC + pm.cyTopHeight + pm.cyBottomHeight;
        int off = (std::max)(0, (UH - fh) / 2);
        if (th) {
            RECT fr = { off, off, off + fw, off + fh };
            DrawThemeBackground(th, dc, SPP_USERPICTURE, 0, &fr, nullptr);
        } else {
            RECT fr = { off + 1, off + 1, off + fw + 3, off + fh + 3 };
            DrawEdge(dc, &fr, EDGE_SUNKEN, BF_RECT);
            off += 3;
        }
        DrawIconEx(dc, off + pm.cxLeftWidth + (USERPIC - P->pic_size.cx) / 2, off + pm.cyTopHeight + (USERPIC - P->pic_size.cy) / 2,
                   P->user_pic, P->pic_size.cx, P->pic_size.cy, 0, nullptr, DI_NORMAL);
        name_x = off + fw + 2 * GetSystemMetrics(SM_CYEDGE) + (th ? 0 : 3);
    }
    if (UH) {
        HGDIOBJ o = SelectObject(dc, P->user_font);
        RECT tr = { name_x, 0, P->w - 6, UH };
        std::u16string w = W(P->user);
        COLORREF col = theme_color(SPP_USERPANE, TMT_TEXTCOLOR, hl_text), sh = 0;
        POINT so = { 0, 0 };
        if (th && SUCCEEDED(GetThemePosition(th, SPP_USERPANE, 0, TMT_TEXTSHADOWOFFSET, &so)) &&
            SUCCEEDED(GetThemeColor(th, SPP_USERPANE, 0, TMT_TEXTSHADOWCOLOR, &sh)))
            DrawShadowText(dc, (LPCWSTR)w.c_str(), (UINT)w.size(), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, col, sh, so.x, so.y);
        else {
            SetTextColor(dc, col);
            text_out(dc, P->user, tr, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }
        SelectObject(dc, o);
    }

    for (const Sep& s : P->seps) {
        // clipped to the list (sfthost's separators are in the pane's window): 5112's are 216 px TRUESIZE lines
        if (th) DrawThemeBackground(th, dc, s.part, 0, &s.rc, &s.rc);
        else { RECT r = s.rc; r.top = (r.top + r.bottom) / 2 - 1; DrawEdge(dc, &r, EDGE_ETCHED, BF_TOP); }
    }

    for (int i = 0; i < (int)P->items.size(); i++) {
        const Item& it = P->items[i];
        bool hot = i == P->hot;
        RECT r = it.rc;
        if (it.col == ColFooter) {
            int idx = it.kind == KLogOff ? 1 : 2;
            draw_footer_image(dc, idx, hot, r.left + 4, (r.top + r.bottom) / 2 - 12);
            HGDIOBJ o = SelectObject(dc, P->font);
            SetTextColor(dc, theme_color(SPP_LOGOFF, TMT_TEXTCOLOR, hl_text));
            RECT tr = { r.left + 32, r.top, r.right, r.bottom };
            text_out(dc, it.text, tr, DT_LEFT | DT_VCENTER);
            if (hot && !th) { RECT fr = r; DrawEdge(dc, &fr, BDR_RAISEDINNER, BF_RECT); }
            SelectObject(dc, o);
            continue;
        }
        if (hot) {
            HBRUSH b = CreateSolidBrush(hl);
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        if (it.kind == KMore) {
            // moreprog.cpp: the text lines up with the list's text (3 edges + a large icon), the arrow after it
            HGDIOBJ o = SelectObject(dc, P->more_font);
            COLORREF col = hot ? hl_text : theme_color(SPP_MOREPROGRAMS, TMT_TEXTCOLOR, GetSysColor(COLOR_MENUTEXT));
            SetTextColor(dc, col);
            int indent = 3 * GetSystemMetrics(SM_CXEDGE) + 32, tw = text_width(P->more_font, it.text);
            SIZE as = { 16, 16 };
            if (th) GetThemePartSize(th, nullptr, SPP_MOREPROGRAMSARROW, SPS_NORMAL, nullptr, TS_TRUE, &as);
            indent = (std::max)(0, (std::min)(indent, (int)(r.right - r.left) - tw - 4 - (int)as.cx));
            RECT tr = { r.left + indent, r.top, r.right, r.bottom };
            text_out(dc, it.text, tr, DT_LEFT | DT_VCENTER);
            RECT ar = { tr.left + tw + 4, (r.top + r.bottom - as.cy) / 2, tr.left + tw + 4 + as.cx, (r.top + r.bottom - as.cy) / 2 + as.cy };
            if (th) DrawThemeBackground(th, dc, SPP_MOREPROGRAMSARROW, hot ? SPS_HOT : SPS_NORMAL, &ar, nullptr);
            else draw_arrow(dc, ar, col);
            SelectObject(dc, o);
            continue;
        }
        int part = it.col == ColLeft ? SPP_PROGLIST : SPP_PLACESLIST;
        int ix = r.left + 2, iy = (r.top + r.bottom - it.icon_size) / 2;
        if (it.icon) DrawIconEx(dc, ix, iy, it.icon, it.icon_size, it.icon_size, 0, nullptr, DI_NORMAL);
        HGDIOBJ o = SelectObject(dc, it.bold ? P->bold : P->font);
        SetTextColor(dc, hot ? hl_text : theme_color(part, TMT_TEXTCOLOR, GetSysColor(COLOR_MENUTEXT)));
        RECT tr = { ix + it.icon_size + 2 * GetSystemMetrics(SM_CXEDGE) + 2, r.top, r.right - 2, r.bottom };
        text_out(dc, it.text, tr, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        if (it.kind == KControl) {      // cascades (sfthost HOSTF_CASCADEMENU): the arrow at the right
            RECT ar = { r.right - 12, r.top, r.right - 4, r.bottom };
            draw_arrow(dc, ar, hot ? hl_text : theme_color(part, TMT_TEXTCOLOR, GetSysColor(COLOR_MENUTEXT)));
        }
        SelectObject(dc, o);
    }
    if (!th) DrawEdge(dc, &all, EDGE_RAISED, BF_RECT);
}

// ---- behaviour -----------------------------------------------------------------------

UINT menu_delay()
{
    UINT d = 400;
    SystemParametersInfoW(SPI_GETMENUSHOWDELAY, 0, &d, 0);
    return d ? d : 1;
}

int hit(POINT pt)
{
    for (int i = 0; i < (int)P->items.size(); i++)
        if (PtInRect(&P->items[i].rc, pt)) return i;
    return -1;
}

void set_hot(int i)
{
    if (i == P->hot) return;
    P->hot = i;
    KillTimer(P->wnd, TIMER_HOVER);
    if (i >= 0 && (P->items[i].kind == KMore || P->items[i].kind == KControl)) SetTimer(P->wnd, TIMER_HOVER, menu_delay(), nullptr);
    InvalidateRect(P->wnd, nullptr, FALSE);
}

// is pt (screen) on the panel itself, not on a flyout menu over it?
BOOL CALLBACK over_other(HWND h, LPARAM lp)
{
    POINT* pt = (POINT*)lp;
    RECT r;
    if (h == P->wnd || h == g.wnd || !IsWindowVisible(h) || !GetWindowRect(h, &r) || !PtInRect(&r, *pt)) return TRUE;
    pt->x = -100000;    // found
    return FALSE;
}

// While a flyout runs (the menu loop has the capture), the panel still follows the pointer as XP's does (the
// start panel's menus are its own menu bands there): an item hovered on the panel is highlighted, and after
// SPI_GETMENUSHOWDELAY the flyout closes (another cascade opens instead); back on the open item it stays.
LRESULT CALLBACK flyout_filter(int code, WPARAM, LPARAM lp)
{
    MSG* m = (MSG*)lp;
    if (code != MSGF_MENU || !P || !m || m->message != WM_MOUSEMOVE) return 0;
    POINT pt = m->pt, probe = pt;
    RECT pr;
    GetWindowRect(P->wnd, &pr);
    if (!PtInRect(&pr, pt)) return 0;
    EnumWindows(over_other, (LPARAM)&probe);
    if (probe.x == -100000) return 0;
    ScreenToClient(P->wnd, &pt);
    int i = hit(pt);
    if (i < 0 || i == P->hot) return 0;
    P->hot = i;
    InvalidateRect(P->wnd, nullptr, FALSE);
    KillTimer(P->wnd, TIMER_SWITCH);
    if (i != P->open_item) SetTimer(P->wnd, TIMER_SWITCH, menu_delay(), nullptr);
    return 0;
}

// All Programs / Control Panel: a menu beside the item (XP's All Programs grows up from the pane)
void flyout(int i)
{
    for (;;) {
        KillTimer(P->wnd, TIMER_HOVER);
        const Item& it = P->items[i];
        HMENU m = it.kind == KMore ? startmenu::programs_popup() : startmenu::settings_popup();
        RECT r = it.rc;
        MapWindowPoints(P->wnd, nullptr, (POINT*)&r, 2);
        UINT flags = TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_LEFTALIGN | (it.kind == KMore && g.edge != EdgeTop ? TPM_BOTTOMALIGN : TPM_TOPALIGN);
        int y = it.kind == KMore && g.edge != EdgeTop ? r.bottom : r.top;
        ReleaseCapture();
        POINT edge = { it.col == ColLeft ? COL_W : P->w, 0 };
        ClientToScreen(P->wnd, &edge);
        P->open_item = i;
        P->reopen = -1;
        HHOOK hook = SetWindowsHookExW(WH_MSGFILTER, flyout_filter, nullptr, 0);
        UINT cmd = (UINT)TrackPopupMenu(m, flags, edge.x, y, 0, P->wnd, nullptr);
        UnhookWindowsHookEx(hook);
        DestroyMenu(m);
        if (!P) return;
        P->open_item = -1;
        KillTimer(P->wnd, TIMER_SWITCH);
        if (cmd) { P->menu_cmd = cmd; P->done = true; return; }
        if (IsWindow(P->wnd)) SetCapture(P->wnd);
        if (P->reopen < 0 || P->done) return;
        i = P->reopen;      // the pointer rested on another cascade: open it now (the delay has passed)
    }
}

void activate(int i)
{
    if (i < 0 || i >= (int)P->items.size()) return;
    Kind k = P->items[i].kind;
    if (k == KMore || k == KControl) { flyout(i); return; }
    P->chosen = i;
    P->done = true;
}

// arrows move within a column (Left/Right between the columns, along the footer), Tab through everything
void key(UINT vk)
{
    int n = (int)P->items.size(), cur = P->hot;
    auto first_of = [&](Col c) { for (int i = 0; i < n; i++) if (P->items[i].col == c) return i; return -1; };
    if (cur < 0) { set_hot(first_of(vk == VK_LEFT ? ColRight : ColLeft)); return; }
    Col c = P->items[cur].col;
    auto step = [&](int d) {
        for (int i = cur + d; i >= 0 && i < n; i += d) if (P->items[i].col == c) return i;
        for (int i = d > 0 ? 0 : n - 1; i >= 0 && i < n; i += d) if (P->items[i].col == c) return i;   // wrap
        return cur;
    };
    auto nearest = [&](Col to) {
        int best = -1, bd = 1 << 30, y = (P->items[cur].rc.top + P->items[cur].rc.bottom) / 2;
        for (int i = 0; i < n; i++) {
            if (P->items[i].col != to) continue;
            int d = abs((P->items[i].rc.top + P->items[i].rc.bottom) / 2 - y);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    };
    switch (vk) {
    case VK_UP: set_hot(c == ColFooter ? nearest(ColLeft) : step(-1)); break;
    case VK_DOWN: set_hot(step(1)); break;
    case VK_TAB: set_hot((cur + 1) % n); break;
    case VK_LEFT:
        if (c == ColFooter) set_hot(step(-1));
        else if (c == ColRight) set_hot(nearest(ColLeft));
        break;
    case VK_RIGHT:
        if (c == ColFooter) set_hot(step(1));
        else if (P->items[cur].kind == KMore || P->items[cur].kind == KControl) activate(cur);
        else if (c == ColLeft) set_hot(nearest(ColRight));
        break;
    case VK_RETURN: case VK_SPACE: activate(cur); break;
    }
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!P || h != P->wnd) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        HDC m = CreateCompatibleDC(dc);
        HBITMAP b = CreateCompatibleBitmap(dc, P->w, P->h);
        HGDIOBJ o = SelectObject(m, b);
        paint(m);
        BitBlt(dc, 0, 0, P->w, P->h, m, 0, 0, SRCCOPY);
        SelectObject(m, o);
        DeleteObject(b);
        DeleteDC(m);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int i = hit(pt);
        if (i >= 0 || P->hot < 0 || P->items[P->hot].kind != KMore) set_hot(i);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        RECT rc = { 0, 0, P->w, P->h };
        if (!PtInRect(&rc, pt)) P->done = true;     // a click outside (the start button too) closes it
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int i = hit(pt);
        if (i >= 0) activate(i);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_SWITCH) {      // (during a flyout) the pointer stayed on another item: close the flyout
            KillTimer(h, TIMER_SWITCH);
            if (P->open_item >= 0 && P->hot >= 0 && P->hot != P->open_item) {
                Kind k = P->items[P->hot].kind;
                P->reopen = k == KMore || k == KControl ? P->hot : -1;
                EndMenu();
            }
            return 0;
        }
        if (wp == TIMER_HOVER) {
            KillTimer(h, TIMER_HOVER);
            if (P->hot >= 0) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(h, &pt);
                if (hit(pt) == P->hot) flyout(P->hot);
            }
        }
        return 0;
    case WM_CANCELMODE:
        // the dock lost the focus (Alt+Tab, another program raised itself); not our own window
        // (the Run dialog) losing it to the dock as the panel opens
        if (P->had_focus) P->done = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void load_user()
{
    struct passwd* pw = getpwuid(getuid());
    std::string name;
    if (pw && pw->pw_gecos) name = std::string(pw->pw_gecos).substr(0, std::string(pw->pw_gecos).find(','));
    if (name.empty() && pw) name = pw->pw_name;
    if (name.empty() && getenv("USER")) name = getenv("USER");
    P->user = name;

    // ~/.face (the freedesktop/AccountsService picture), user_picture=, else XP's chess tile from the nt5src tree
    std::vector<std::string> cand = { home() + "/.face", home() + "/.face.icon", setting("user_picture") };
    std::string nt5 = shell::setting("nt5src");
    if (!nt5.empty()) cand.push_back(nt5 + "/Source/XPSP1/NT/windows/advcore/duser/directui/test/app/logonui/chess.bmp");
    bool show = P->user_h > 0;
    if (P->th) { BOOL b = TRUE; if (SUCCEEDED(GetThemeBool(P->th, SPP_USERPANE, 0, TMT_USERPICTURE, &b))) show = b; }
    if (!show) return;
    for (const std::string& c : cand) {
        if (!is_file(c)) continue;
        gdi::Bitmap* b = gdi::load_image(c);
        if (!b) continue;
        int w, h;
        gdi::fit_size(b->width, b->height, USERPIC, USERPIC, true, &w, &h);    // userpane.cpp: 48 on the longer side
        P->user_pic = icon_from(b, w, h);
        P->pic_size = { w, h };
        gdi::destroy_bitmap(b);
        if (P->user_pic) break;
    }
}

void make_fonts()
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    P->font = CreateFontIndirectW(&ncm.lfMenuFont);
    LOGFONTW lf = ncm.lfMenuFont;
    lf.lfWeight = FW_BOLD;
    P->bold = CreateFontIndirectW(&lf);
    LOGFONTW uf = ncm.lfCaptionFont;
    uf.lfHeight = -16;
    uf.lfWeight = FW_BOLD;
    LOGFONTW mf = lf;
    HDC dc = GetDC(nullptr);
    if (P->th) GetThemeFont(P->th, dc, SPP_USERPANE, 0, TMT_FONT, &uf);
    if (P->th) GetThemeFont(P->th, dc, SPP_MOREPROGRAMS, 0, TMT_FONT, &mf);
    ReleaseDC(nullptr, dc);
    P->user_font = CreateFontIndirectW(&uf);
    P->more_font = CreateFontIndirectW(&mf);
}

void load_classic_logoff()
{
    std::string nt5 = shell::setting("nt5src");
    if (nt5.empty()) return;
    const char* f[2] = { "logoffbuttons.bmp", "logoffbuttonshot.bmp" };
    for (int i = 0; i < 2; i++) {
        std::string p = nt5 + "/Source/XPSP1/NT/shell/explorer/desktop2/" + f[i];
        if (is_file(p)) P->classic_logoff[i] = (HBITMAP)LoadImageW(nullptr, (LPCWSTR)W(p).c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    }
}

void destroy()
{
    for (Item& it : P->items) if (it.icon) DestroyIcon(it.icon);
    if (P->user_pic) DestroyIcon(P->user_pic);
    for (HFONT f : { P->font, P->bold, P->user_font, P->more_font }) if (f) DeleteObject(f);
    for (HBITMAP b : P->classic_logoff) if (b) DeleteObject(b);
    if (P->th) CloseThemeData(P->th);
    if (P->wnd) DestroyWindow(P->wnd);
    delete P;
    P = nullptr;
}

void run(const Item& it)
{
    switch (it.kind) {
    case KApp:
        if (const shell::App* a = shell::find_app(it.arg)) startmenu::launch(*a);
        break;
    case KCommand: startmenu::run_line(it.arg); break;
    case KTerminal: {
        std::string err;
        if (!shell::open_terminal(home(), &err)) {
            std::u16string t = W(err);
            MessageBoxW(g.wnd, (LPCWSTR)t.c_str(), L"Terminal", MB_OK | MB_ICONERROR);
        }
        break;
    }
    case KExplorer: case KDocuments: case KPictures: case KComputer: case KSearch:
        startmenu::open_folder(it.arg);
        break;
    case KRun: startmenu::run_dialog(); break;
    case KLogOff: startmenu::log_off(); break;
    case KTurnOff: startmenu::shut_down(); break;
    default: break;
    }
}

} // namespace

HBITMAP menu_bitmap(const std::string& icon_name, const char* fallback_key, int size)
{
    std::string p = icon_path(icon_name, size);
    if (!p.empty())
        if (gdi::Bitmap* b = gdi::load_image(p)) {
            HBITMAP bm = bitmap_from(b, size);
            gdi::destroy_bitmap(b);
            if (bm) return bm;
        }
    if (!fallback_key || !*fallback_key) return nullptr;
    HICON ic = key_icon(fallback_key, size);
    if (!ic) return nullptr;
    HBITMAP bm = bitmap_from(ic, size);
    DestroyIcon(ic);
    return bm;
}

void count_launch(const std::string& id)
{
    if (id.empty()) return;
    std::vector<std::pair<int, std::string>> v = load_mfu();
    bool found = false;
    for (auto& e : v) if (e.second == id) { e.first++; found = true; }
    if (!found) v.push_back({ 1, id });
    std::string out;
    for (auto& e : v) out += std::to_string(e.first) + " " + e.second + "\n";
    shell::write_file_atomic(mfu_path(), out);
}

void close()
{
    if (P) P->done = true;
}

void show(const RECT& b, bool anchored)
{
    if (P) return;
    static bool registered;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = proc;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"DV2ControlHost";      // XP's name for the panel's host window
        RegisterClassW(&wc);
        registered = true;
    }
    P = new Panel;
    P->wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"DV2ControlHost", L"Start Menu", WS_POPUP,
                             0, 0, PANEL_W, 400, g.wnd, nullptr, nullptr, nullptr);
    if (!P->wnd) { destroy(); return; }
    P->th = OpenThemeData(P->wnd, L"StartPanel");
    // the user pane's height is its part's DEFAULTPANESIZE (deskhost.cpp sizes the panes from them): none in 4074's
    // Jade and Aero (no USERPANE part; the StartPanel is 380x376) and 0 in their XP backports (USERPANE 380x0)
    if (P->th) {
        RECT ps = {};
        if (!IsThemePartDefined(P->th, SPP_USERPANE, 0)) P->user_h = 0;
        else if (SUCCEEDED(GetThemeRect(P->th, SPP_USERPANE, 0, TMT_DEFAULTPANESIZE, &ps)) && ps.bottom - ps.top >= 0 && ps.bottom - ps.top < 200)
            P->user_h = ps.bottom - ps.top;
    }
    make_fonts();
    if (!P->th) load_classic_logoff();
    load_user();
    shell::refresh_icon_theme();   // the icon set is per theme and may have changed (Display Properties)
    build_items();
    layout();

    // above the start button (or beside / below it), inside the screen
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = b.left, y = g.screen.top - P->h;
    if (anchored) y = b.top >= sh / 2 ? b.top - P->h : b.bottom;    // SHELL-DESIGN: above the anchor, below it up top
    else switch (g.edge) {
    case EdgeBottom: break;
    case EdgeTop: y = g.screen.bottom; break;
    case EdgeLeft: x = g.screen.right; y = b.top; break;
    case EdgeRight: x = g.screen.left - P->w; y = b.top; break;
    }
    x = (std::max)(0, (std::min)(x, sw - P->w));
    y = (std::max)(0, (std::min)(y, sh - P->h));
    SetWindowPos(P->wnd, HWND_TOPMOST, x, y, P->w, P->h, SWP_NOACTIVATE);
    apply_shape();

    uint64_t prev = band::active_window();
    xwm::focus_own();       // the keyboard (as for the menus: a dock is never focused by the window manager)
    xwm::set_start_menu_open(true);     // _LH_STARTMENU_OPEN for lux's Start button, until the loop below ends
    ShowWindow(P->wnd, SW_SHOWNA);
    UpdateWindow(P->wnd);
    SetCapture(P->wnd);
    // clicks into other clients: lhwm does not refocus the window that is already active, so the
    // dock's FocusOut (WM_CANCELMODE) cannot be relied on; a pointer grab reports them to us
    sys::NativeWindow* nw = sys::window_for_user((void*)P->wnd);
    bool grabbed = nw && sys::grab_pointer(nw, true);

    bool quit = false;
    int quit_code = 0;
    while (P && !P->done) {
        if (GetActiveWindow() == g.wnd) P->had_focus = true;
        MSG m;
        if (!GetMessageW(&m, nullptr, 0, 0)) { quit = true; quit_code = (int)m.wParam; break; }
        if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) {
            if (m.wParam == VK_ESCAPE || (m.message == WM_SYSKEYDOWN && m.wParam == VK_F4)) P->done = true;
            else if (m.wParam == VK_LWIN || m.wParam == VK_RWIN) P->done = true;
            else key((UINT)m.wParam);
            continue;
        }
        if (m.message == WM_CHAR || m.message == WM_KEYUP || m.message == WM_SYSKEYUP || m.message == WM_SYSCHAR) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (grabbed) sys::grab_pointer(nw, false);
    xwm::set_start_menu_open(false);    // every way out (a choice, Escape, Super, a click outside, focus loss, a toggle)
    if (!P) return;
    if (GetCapture() == P->wnd) ReleaseCapture();
    Item chosen;
    bool have = P->chosen >= 0;
    if (have) chosen = P->items[P->chosen];
    chosen.icon = nullptr;
    UINT cmd = P->menu_cmd;
    destroy();
    if (quit) { PostQuitMessage(quit_code); return; }
    if (have) run(chosen);
    else if (cmd) startmenu::command(cmd);
    else if (prev) xwm::activate(prev);     // dismissed: the keyboard goes back where it was
}

} // namespace startpanel
} // namespace tb
