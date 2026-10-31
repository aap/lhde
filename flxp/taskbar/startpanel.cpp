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
// MOREPROGRAMS 190x30, PLACESLIST 190x336, LOGOFF 380x40 (Luna, Slate and Jade alike); a style
// may say otherwise (ReactOS's Lunar, Mizu, Lautus: 357 wide, 178 + 179, user and log off panes 28)
const int PANEL_W = 380, USER_H = 64, COL_W = 190, COL_MIN_H = 336, MORE_H = 30, LOGOFF_H = 40;
// Without a visual style (deskhost.cpp g_spmDefault, the panes in a WS_DLGFRAME window that is the
// panes' size, so the frame covers their outer 3 px): user pane 40, log off 40; the program list's
// tiles 32 + 4 SM_CYEDGE, the places' 30 (SP3, measured), separator tiles 4 SM_CYEDGE
const int CL_USER_H = 40, CL_LOGOFF_H = 40, CL_FRAME = 3, CL_TILE_L = 40, CL_TILE_R = 30, CL_SEP = 8;
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
    int col_w = COL_W, more_h = MORE_H, logoff_h = LOGOFF_H, min_col_h = COL_MIN_H;   // the style's DEFAULTPANESIZEs
    int org = 0;                        // where the panes start (the non-themed frame)
    SIZE logoff_cell = { 24, 24 };      // one image of the style's LOGOFFBUTTONS strip
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

std::string mfu_path() { return shell::state_file("flxp-mfu"); }   // the start panel's launch counts: state (docs/SETTINGS.md "Files")

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
void layout_themed()
{
    int top = P->user_h;
    MARGINS ml = margins(SPP_PROGLIST, 6, 9, 4, 5), mr = margins(SPP_PLACESLIST, 4, 9, 6, 5);
    int y = top + ml.cyTopHeight, sep_l = sep_height(SPP_PROGLISTSEPARATOR), sep_r = sep_height(SPP_PLACESLISTSEPARATOR);
    bool pinned_done = false;
    for (Item& it : P->items) {
        if (it.col != ColLeft || it.kind == KMore) continue;
        if (!it.bold && !pinned_done) {      // the first MFU item: the separator under the pinned ones
            pinned_done = true;
            if (&it != &P->items[0]) { P->seps.push_back({ { ml.cxLeftWidth, y, P->col_w - ml.cxRightWidth, y + sep_l }, SPP_PROGLISTSEPARATOR }); y += sep_l; }
        }
        it.rc = { ml.cxLeftWidth, y, P->col_w - ml.cxRightWidth, y + 32 + 8 };
        y = it.rc.bottom;
    }
    int left_need = y + ml.cyBottomHeight - top + P->more_h;
    y = top + mr.cyTopHeight;
    for (Item& it : P->items) {
        if (it.col != ColRight) continue;
        if (it.kind == KControl || it.kind == KRun) {
            P->seps.push_back({ { P->col_w + mr.cxLeftWidth, y, P->w - mr.cxRightWidth, y + sep_r }, SPP_PLACESLISTSEPARATOR });
            y += sep_r;
        }
        it.rc = { P->col_w + mr.cxLeftWidth, y, P->w - mr.cxRightWidth, y + 24 + 8 };
        y = it.rc.bottom;
    }
    int right_need = y + mr.cyBottomHeight - top;
    P->col_h = (std::max)(P->min_col_h, (std::max)(left_need, right_need));
    P->h = P->user_h + P->col_h + P->logoff_h;

    // All Programs (moreprog.cpp): the button fills the pane's content rectangle
    MARGINS mm = margins(SPP_MOREPROGRAMS, 8, 8, 0, 6);
    int mt = top + P->col_h - P->more_h;
    for (Item& it : P->items) {
        if (it.kind == KMore) it.rc = { mm.cxLeftWidth, mt + mm.cyTopHeight, P->col_w - mm.cxRightWidth, top + P->col_h - mm.cyBottomHeight };
    }
    // log off pane (logoff.cpp): a flat list toolbar of 24 px images with text, right-aligned
    MARGINS mo = margins(SPP_LOGOFF, 0, 0, 4, 2);
    int lt = P->h - P->logoff_h + mo.cyTopHeight, lb = P->h - mo.cyBottomHeight;
    int x = P->w - mo.cxRightWidth - 6;
    for (int i = (int)P->items.size() - 1; i >= 0; i--) {
        Item& it = P->items[i];
        if (it.col != ColFooter) continue;
        int wdt = 4 + P->logoff_cell.cx + 4 + text_width(P->font, it.text) + 6;
        int cy = (lt + lb) / 2, half = (std::min)(15, (std::max)(12, (lb - lt) / 2));   // ReactOS's 28 px pane
        it.rc = { x - wdt, cy - half, x, cy + half };
        x -= wdt + 4;
    }
}

// Without a visual style (the unthemed branches of sfthost.cpp, moreprog.cpp, logoff.cpp; measured on XP SP3 with
// Windows Classic): the list margins are 2 SM_CXEDGE left, top and right, the tiles start SM_CXEDGE further in; the
// program list ends in a separator above All Programs; the panel is as high as the taller column needs (deskhost.cpp
// _ComputeActualSize), there is no minimum. Coordinates are the window's: the panes start inside the 3 px frame.
void layout_classic()
{
    const int O = P->org, top = O + P->user_h, cx = 2 * GetSystemMetrics(SM_CXEDGE), tx = cx + GetSystemMetrics(SM_CXEDGE);
    const int left_r = O + P->col_w - cx, right_l = O + P->col_w;
    int y = top + cx;
    bool pinned_done = false;
    for (Item& it : P->items) {
        if (it.col != ColLeft || it.kind == KMore) continue;
        if (!it.bold && !pinned_done) {
            pinned_done = true;
            if (&it != &P->items[0]) { P->seps.push_back({ { O + tx, y + 3, left_r, y + 5 }, SPP_PROGLISTSEPARATOR }); y += CL_SEP; }
        }
        it.rc = { O + tx, y, left_r, y + CL_TILE_L };
        y = it.rc.bottom;
    }
    int left_need = y - top + 2 + P->more_h;            // the separator at the bottom of the list (SM_CYEDGE)
    y = top + cx;
    for (Item& it : P->items) {
        if (it.col != ColRight) continue;
        if (it.kind == KControl || it.kind == KRun) {
            P->seps.push_back({ { right_l + tx, y + 3, O + P->w - cx, y + 5 }, SPP_PLACESLISTSEPARATOR });
            y += CL_SEP;
        }
        it.rc = { right_l + tx, y, O + P->w - cx, y + CL_TILE_R };
        y = it.rc.bottom;
    }
    P->col_h = (std::max)(left_need, y - top);
    P->h = P->user_h + P->col_h + P->logoff_h;          // the window is the panes' size: the frame covers 3 px of them
    int mt = top + P->col_h - P->more_h;
    P->seps.push_back({ { O + tx, mt - 2, left_r, mt }, SPP_PROGLISTSEPARATOR });
    for (Item& it : P->items)
        if (it.kind == KMore) it.rc = { O + cx, mt, left_r, mt + P->more_h };
    // the log off toolbar: list buttons (3 + image + 4 + text + 8) right-aligned to the pane's width less SM_CXEDGE,
    // 2 SM_CYEDGE below the pane's top
    int lt = top + P->col_h + 2 * GetSystemMetrics(SM_CYEDGE), x = O + P->w - GetSystemMetrics(SM_CXEDGE);
    for (int i = (int)P->items.size() - 1; i >= 0; i--) {
        Item& it = P->items[i];
        if (it.col != ColFooter) continue;
        int wdt = 3 + 24 + 4 + text_width(P->font, it.text) + 8;
        it.rc = { x - wdt, lt, x, lt + 30 };
        x -= wdt;
    }
}

void layout()
{
    if (P->th) layout_themed();
    else layout_classic();
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

// does the style draw this part (a style may leave parts out: then they are drawn as without a style)
bool has_part(int part) { return P->th && IsThemePartDefined(P->th, part, 0); }

// LOGOFFBUTTONS: three cells (eject, log off, turn off; logoff.cpp cuts the strip in three: 24x24 in Luna, Slate and
// Jade, 34x18 in ReactOS's styles); 5112's aero has two 24 px ones (lock, power)
int logoff_cells(SIZE* cell)
{
    SIZE ss = { 72, 24 };
    GetThemePartSize(P->th, nullptr, SPP_LOGOFFBUTTONS, SPLS_NORMAL, nullptr, TS_TRUE, &ss);
    int cells = ss.cx % 24 == 0 && ss.cx / 24 >= 1 && ss.cx / 24 <= 3 ? (int)ss.cx / 24 : 3;
    if (cell) *cell = { (std::max)(1, (int)ss.cx / cells), (std::max)(1, (int)ss.cy) };
    return cells;
}

void draw_footer_image(HDC dc, int idx, bool hot, int x, int y)
{
    if (has_part(SPP_LOGOFFBUTTONS)) {
        SIZE c;
        int cells = logoff_cells(&c);
        if (cells < 3) idx = (std::max)(0, idx - (3 - cells));
        RECT strip = { x - idx * c.cx, y, x - idx * c.cx + cells * c.cx, y + c.cy }, clip = { x, y, x + c.cx, y + c.cy };
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
    // the non-themed More Programs arrow (Marlett's right triangle at the 8 pt font's ascent): 4 x 7 px
    int cx = r.left + 4, cy = (r.top + r.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ o = SelectObject(dc, pen);
    for (int i = 0; i < 4; i++) { MoveToEx(dc, cx + i, cy - 3 + i, nullptr); LineTo(dc, cx + i, cy + 4 - i); }
    SelectObject(dc, o);
    DeleteObject(pen);
}

// moreprog.cpp without a style: the text indented by 3 edges and a large icon; Marlett's arrow after the width of
// "All &Programs" (it measures the ampersand too) and an edge. Where the arrow's 8 px cell starts.
int classic_more_text_x(const RECT& r) { return r.left + 3 * GetSystemMetrics(SM_CXEDGE) + 32; }
int classic_more_arrow_x(const RECT& r)
{
    return classic_more_text_x(r) + text_width(P->more_font, "All &Programs") + GetSystemMetrics(SM_CXEDGE);
}

// the user pane without a style: userpane.cpp's DrawCaption(DC_ACTIVE | DC_TEXT | DC_GRADIENT) of a window without
// a title, the active caption's gradient across the pane (each column a + (b - a) * x / width, as on XP SP3)
void caption_gradient(HDC dc, const RECT& r)
{
    COLORREF a = GetSysColor(COLOR_ACTIVECAPTION), b = GetSysColor(COLOR_GRADIENTACTIVECAPTION);
    int w = r.right - r.left;
    for (int x = 0; x < w; x++) {
        auto mix = [&](int ca, int cb) { return ca + (cb - ca) * x / w; };
        HBRUSH br = CreateSolidBrush(RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b))));
        RECT c = { r.left + x, r.top, r.left + x + 1, r.bottom };
        FillRect(dc, &c, br);
        DeleteObject(br);
    }
}

// a pane as XP draws it without a style: the menu colour; the places list's etched left edge (sfthost.cpp
// _OnEraseBackground), the log off pane's etched top (logoff.cpp)
void classic_pane(HDC dc, int part, const RECT& r)
{
    if (part == SPP_USERPANE) { caption_gradient(dc, r); return; }
    FillRect(dc, &r, GetSysColorBrush(COLOR_MENU));
    RECT e = r;
    if (part == SPP_PLACESLIST) DrawEdge(dc, &e, EDGE_ETCHED, BF_LEFT);
    if (part == SPP_LOGOFF) DrawEdge(dc, &e, EDGE_ETCHED, BF_TOP);
}

// the panes' backgrounds (deskhost.cpp: each pane draws its own part). A part with BGTYPE NONE (ReactOS's Lunar
// and Mizu user panes) draws nothing; XP's panel shows black there (the host window paints nothing and the panel
// is shown from a snapshot), so do we. A part the style lacks is drawn as without a style.
void draw_panes(HDC dc)
{
    const int UH = P->user_h;
    struct { int part; RECT r; } panes[] = {
        { SPP_USERPANE, { 0, 0, P->w, UH } },
        { SPP_PROGLIST, { 0, UH, P->col_w, UH + P->col_h - P->more_h } },
        { SPP_MOREPROGRAMS, { 0, UH + P->col_h - P->more_h, P->col_w, UH + P->col_h } },
        { SPP_PLACESLIST, { P->col_w, UH, P->w, UH + P->col_h } },
        { SPP_LOGOFF, { 0, P->h - P->logoff_h, P->w, P->h } },
    };
    for (auto& p : panes) {
        if (p.part == SPP_USERPANE && !UH) continue;
        if (!has_part(p.part)) { classic_pane(dc, p.part, p.r); continue; }
        int bg = BT_IMAGEFILE;
        if (SUCCEEDED(GetThemeEnumValue(P->th, p.part, 0, TMT_BGTYPE, &bg)) && bg == BT_NONE)
            FillRect(dc, &p.r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        else DrawThemeBackground(P->th, dc, p.part, 0, &p.r, nullptr);
    }
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

int text_height(HDC dc, HFONT f, const std::string& s)
{
    HGDIOBJ o = SelectObject(dc, f);
    std::u16string w = W(s.empty() ? std::string("0") : s);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
    SelectObject(dc, o);
    return sz.cy;
}

// XP's panel without a visual style (Windows Classic), measured on XP SP3: everything on the menu colour, the
// user pane in the active caption's gradient with the name in a bold italic 175% of the GUI font and no picture,
// etched separators, the places list's etched left edge, the log off pane's etched top, a dialog frame
void paint_classic(HDC dc)
{
    RECT all = { 0, 0, P->w, P->h };
    const int O = P->org, UH = P->user_h;
    COLORREF hl_text = GetSysColor(COLOR_HIGHLIGHTTEXT), text = GetSysColor(COLOR_MENUTEXT);
    SetBkMode(dc, TRANSPARENT);
    FillRect(dc, &all, GetSysColorBrush(COLOR_MENU));
    RECT user = { O, O, O + P->w, O + UH }, places = { O + P->col_w, O + UH, O + P->w, O + UH + P->col_h },
         logoff = { O, O + UH + P->col_h, O + P->w, O + UH + P->col_h + P->logoff_h };
    classic_pane(dc, SPP_USERPANE, user);
    classic_pane(dc, SPP_PLACESLIST, places);
    classic_pane(dc, SPP_LOGOFF, logoff);

    // userpane.cpp: the name at (h - text height) / 2 from the top and from the left, COLOR_CAPTIONTEXT
    int off = (std::max)(0, (UH - text_height(dc, P->user_font, P->user)) / 2);
    HGDIOBJ o = SelectObject(dc, P->user_font);
    SetTextColor(dc, GetSysColor(COLOR_CAPTIONTEXT));
    text_out(dc, P->user, { user.left + off, user.top + off, user.right - off, user.bottom }, DT_LEFT | DT_TOP | DT_END_ELLIPSIS);
    SelectObject(dc, o);

    for (const Sep& s : P->seps) { RECT r = s.rc; DrawEdge(dc, &r, EDGE_ETCHED, BF_TOPLEFT); }   // sfthost _DrawSeparator

    for (int i = 0; i < (int)P->items.size(); i++) {
        const Item& it = P->items[i];
        bool hot = i == P->hot;
        RECT r = it.rc;
        if (hot) FillRect(dc, &r, GetSysColorBrush(COLOR_HIGHLIGHT));
        if (it.col == ColFooter) {
            // logoff.cpp: a flat list toolbar, the hot button filled with the selection colour, no edges
            draw_footer_image(dc, it.kind == KLogOff ? 1 : 2, hot, r.left + 3, r.top + 3);
            o = SelectObject(dc, P->font);
            SetTextColor(dc, hot ? hl_text : text);
            text_out(dc, it.text, { r.left + 3 + 24 + 4, r.top, r.right, r.bottom }, DT_LEFT | DT_VCENTER);
            SelectObject(dc, o);
            continue;
        }
        if (it.kind == KMore) {
            o = SelectObject(dc, P->more_font);
            COLORREF col = hot ? hl_text : text;
            SetTextColor(dc, col);
            int ax = classic_more_arrow_x(r), cy = (r.top + r.bottom) / 2;
            text_out(dc, it.text, { classic_more_text_x(r), r.top, r.right, r.bottom }, DT_LEFT | DT_VCENTER);
            draw_arrow(dc, { ax, cy - 9, ax + 8, cy + 7 }, col);
            SelectObject(dc, o);
            continue;
        }
        // sfthost.cpp: tiles of icon + 4 SM_CYEDGE (the places' 30 on SP3), the icon at the tile's left, the text
        // 6 px after it (an edge and the list view's label margin); both centred 2 px above the middle
        int tile = r.bottom - r.top;
        if (it.icon) DrawIconEx(dc, r.left, r.top + (tile - it.icon_size) / 2 - 2, it.icon, it.icon_size, it.icon_size, 0, nullptr, DI_NORMAL);
        HFONT f = it.bold ? P->bold : P->font;
        int th = text_height(dc, f, it.text), ty = r.top + (tile - th) / 2 - 2;
        o = SelectObject(dc, f);
        SetTextColor(dc, hot ? hl_text : text);
        int right = it.kind == KControl ? r.right - 12 : r.right;
        text_out(dc, it.text, { r.left + it.icon_size + 6, ty, right, ty + th }, DT_LEFT | DT_TOP | DT_END_ELLIPSIS);
        if (it.kind == KControl)        // HOSTF_CASCADEMENU: Marlett's arrow 5 px from the tile's right, on the text
            draw_arrow(dc, { r.right - 13, ty, r.right - 5, ty + th }, hot ? hl_text : text);
        SelectObject(dc, o);
    }
    // deskhost.cpp: WS_DLGFRAME without a style (a raised edge and a pixel of the face colour)
    DrawEdge(dc, &all, EDGE_RAISED, BF_RECT);
    RECT in = { 2, 2, P->w - 2, P->h - 2 };
    FrameRect(dc, &in, GetSysColorBrush(COLOR_BTNFACE));
}

void paint(HDC dc)
{
    if (!P->th) { paint_classic(dc); return; }
    RECT all = { 0, 0, P->w, P->h };
    const int UH = P->user_h;
    HTHEME th = P->th;
    SetBkMode(dc, TRANSPARENT);
    COLORREF hl = GetSysColor(COLOR_MENUHILIGHT), hl_text = GetSysColor(COLOR_HIGHLIGHTTEXT);

    FillRect(dc, &all, GetSysColorBrush(COLOR_MENU));
    draw_panes(dc);

    // the user pane: picture in its frame, the name (userpane.cpp); none in Jade/Aero (no USERPANE part)
    int name_x = -1;
    if (UH && P->user_pic) {
        MARGINS pm = margins(SPP_USERPICTURE, 0, 0, 0, 0);
        int fw = USERPIC + pm.cxLeftWidth + pm.cxRightWidth, fh = USERPIC + pm.cyTopHeight + pm.cyBottomHeight;
        int off = (std::max)(0, (UH - fh) / 2);
        bool framed = has_part(SPP_USERPICTURE);
        if (framed) {
            RECT fr = { off, off, off + fw, off + fh };
            DrawThemeBackground(th, dc, SPP_USERPICTURE, 0, &fr, nullptr);
        } else {
            RECT fr = { off + 1, off + 1, off + fw + 3, off + fh + 3 };
            DrawEdge(dc, &fr, EDGE_SUNKEN, BF_RECT);
            off += 3;
        }
        DrawIconEx(dc, off + pm.cxLeftWidth + (USERPIC - P->pic_size.cx) / 2, off + pm.cyTopHeight + (USERPIC - P->pic_size.cy) / 2,
                   P->user_pic, P->pic_size.cx, P->pic_size.cy, 0, nullptr, DI_NORMAL);
        name_x = off + fw + 2 * GetSystemMetrics(SM_CYEDGE) + (framed ? 0 : 3);
    }
    if (UH) {
        // userpane.cpp: without a picture the name is as far from the left as from the top
        if (name_x < 0) name_x = (std::max)(0, (UH - text_height(dc, P->user_font, P->user)) / 2);
        HGDIOBJ o = SelectObject(dc, P->user_font);
        RECT tr = { name_x, 0, P->w - 6, UH };
        std::u16string w = W(P->user);
        COLORREF col = theme_color(SPP_USERPANE, TMT_TEXTCOLOR, hl_text), sh = 0;
        POINT so = { 0, 0 };
        if (SUCCEEDED(GetThemePosition(th, SPP_USERPANE, 0, TMT_TEXTSHADOWOFFSET, &so)) &&
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
        if (has_part(s.part)) DrawThemeBackground(th, dc, s.part, 0, &s.rc, &s.rc);
        else { RECT r = s.rc; r.top = (r.top + r.bottom) / 2 - 1; r.bottom = r.top + 2; DrawEdge(dc, &r, EDGE_ETCHED, BF_TOPLEFT); }
    }

    for (int i = 0; i < (int)P->items.size(); i++) {
        const Item& it = P->items[i];
        bool hot = i == P->hot;
        RECT r = it.rc;
        if (it.col == ColFooter) {
            int idx = it.kind == KLogOff ? 1 : 2;
            draw_footer_image(dc, idx, hot, r.left + 4, (r.top + r.bottom - P->logoff_cell.cy) / 2);
            HGDIOBJ o = SelectObject(dc, P->font);
            SetTextColor(dc, theme_color(SPP_LOGOFF, TMT_TEXTCOLOR, hl_text));
            RECT tr = { r.left + 4 + P->logoff_cell.cx + 4, r.top, r.right, r.bottom };
            text_out(dc, it.text, tr, DT_LEFT | DT_VCENTER);
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
            bool arrow = has_part(SPP_MOREPROGRAMSARROW);
            SIZE as = { 16, 16 };
            if (arrow) GetThemePartSize(th, nullptr, SPP_MOREPROGRAMSARROW, SPS_NORMAL, nullptr, TS_TRUE, &as);
            indent = (std::max)(0, (std::min)(indent, (int)(r.right - r.left) - tw - 4 - (int)as.cx));
            RECT tr = { r.left + indent, r.top, r.right, r.bottom };
            text_out(dc, it.text, tr, DT_LEFT | DT_VCENTER);
            RECT ar = { tr.left + tw + 4, (r.top + r.bottom - as.cy) / 2, tr.left + tw + 4 + as.cx, (r.top + r.bottom - as.cy) / 2 + as.cy };
            if (arrow) DrawThemeBackground(th, dc, SPP_MOREPROGRAMSARROW, hot ? SPS_HOT : SPS_NORMAL, &ar, nullptr);
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
        POINT edge = { it.col == ColLeft ? P->col_w : P->w, 0 };
        if (!P->th && it.kind == KMore) {
            // XP without a style: All Programs opens right of its arrow, the menu's items (inside its 3 px border)
            // level with the button
            edge.x = classic_more_arrow_x(it.rc) + 8;
            y += g.edge != EdgeTop ? 3 : -3;
        }
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
    case WM_MEASUREITEM: if (startmenu::measure_item((MEASUREITEMSTRUCT*)lp)) return TRUE; break;
    case WM_DRAWITEM: if (startmenu::draw_item((const DRAWITEMSTRUCT*)lp)) return TRUE; break;
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

    // ~/.face (the freedesktop/AccountsService picture), user_picture=, else XP's chess tile
    // (logonui.exe's IDB_USER0 114, when lhde-import has it in resources/xp)
    std::vector<std::string> cand = { home() + "/.face", home() + "/.face.icon", setting("user_picture") };
    std::string chess = shell::xp_art("logonui.exe", 114, 2);
    if (!chess.empty()) cand.push_back(chess);
    bool show = P->user_h > 0 && P->th;    // userpane.cpp: no picture without a style
    if (P->th) { BOOL b = TRUE; if (SUCCEEDED(GetThemeBool(P->th, SPP_USERPANE, 0, TMT_USERPICTURE, &b))) show = b; }
    if (!show) return;
    for (const std::string& c : cand) {
        if (c.rfind("pe:", 0) != 0 && !is_file(c)) continue;
        gdi::Bitmap* b = shell::load_art(c);
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
    if (!P->th) {
        // without a style: the lists' font is the icon title font (sfthost.cpp LoadControlFont), the user name the
        // GUI font at 175%, bold and italic (userpane.cpp)
        LOGFONTW lf = {};
        SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0);
        P->font = CreateFontIndirectW(&lf);
        P->more_font = CreateFontIndirectW(&lf);
        lf.lfWeight = FW_BOLD;
        P->bold = CreateFontIndirectW(&lf);
        LOGFONTW uf = {};
        GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof uf, &uf);
        uf.lfItalic = TRUE;
        uf.lfHeight = uf.lfHeight * 175 / 100;
        uf.lfWidth = 0;
        uf.lfWeight = FW_BOLD;
        P->user_font = CreateFontIndirectW(&uf);
        return;
    }
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
    // explorer.exe's IDB_LOGOFF_NORMAL / _HOT (desktop2/logoffbuttons.bmp, logoffbuttonshot.bmp)
    for (int i = 0; i < 2; i++) P->classic_logoff[i] = xp_explorer_bitmap(7010 + i, 0);
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
        // the other panes' sizes (deskhost.cpp _ReadPaneSizeFromTheme: a part's size that is the panel's own is
        // inherited from it, not the part's)
        RECT all = {};
        bool have_all = SUCCEEDED(GetThemeRect(P->th, 0, 0, TMT_DEFAULTPANESIZE, &all));
        auto pane = [&](int part, SIZE& out) {
            RECT r = {};
            if (FAILED(GetThemeRect(P->th, part, 0, TMT_DEFAULTPANESIZE, &r))) return false;
            if (have_all && r.right - r.left == all.right - all.left && r.bottom - r.top == all.bottom - all.top) return false;
            out = { r.right - r.left, r.bottom - r.top };
            return out.cx > 0 && out.cx < 2000 && out.cy >= 0 && out.cy < 2000;
        };
        SIZE z;
        if (have_all && all.right - all.left >= 200 && all.right - all.left < 2000) P->w = all.right - all.left;
        if (pane(SPP_USERPANE, z) && z.cx >= 200) P->w = z.cx;
        if (pane(SPP_PROGLIST, z) && z.cx > 50 && z.cx < P->w - 50) P->col_w = z.cx;
        if (pane(SPP_MOREPROGRAMS, z) && z.cy >= 16) P->more_h = z.cy;
        if (pane(SPP_LOGOFF, z) && z.cy >= 16) P->logoff_h = z.cy;
        if (pane(SPP_PLACESLIST, z) && z.cy >= 100) P->min_col_h = z.cy;
    } else {
        P->w = PANEL_W; P->user_h = CL_USER_H; P->col_w = COL_W; P->more_h = MORE_H; P->logoff_h = CL_LOGOFF_H;
        P->org = CL_FRAME;
    }
    make_fonts();
    if (!has_part(SPP_LOGOFFBUTTONS)) load_classic_logoff();
    else logoff_cells(&P->logoff_cell);
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
