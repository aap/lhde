// The file dialog (see filedlg.h). The XP look follows XP's comdlg32
// filenew.cpp (CFileOpenBrowser): the template's lst1 list box is a
// placeholder the view is created over, stc1 marks where the small toolbar
// goes, ctl1 is the places bar (a wrapable toolbar themed as "Placesbar"),
// cmb2 the owner-drawn Look in combo (PaintDriveLine), cmb13 the File name
// combo with edt1 hidden. The Longhorn look ("---- the Longhorn look") keeps
// the bottom controls' ids and builds the rest from the explorer's browsing
// widget. In both the view is the shared browser::Browser list (the dialog's
// Dlg derives from it) over shell::Folder.
#define NOMINMAX
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <map>
#include <memory>
#include "filedlg.h"
#include "resource.h"
#include "shell/namespace.h"
#include "shell/icons.h"
#include "shell/config.h"
#include "shell/lhtheme.h"
#include "shell/mime.h"
#include "shell/bookmarks.h"
#include "shell/fileops.h"
#include "shell/runtime.h"
#include "sys/sys.h"
#include "explorer/header.h"
#include "explorer/places.h"
#include "explorer/thumbview.h"
#include "explorer/viewbutton.h"
#include "explorer/lhbar.h"
#include "explorer/uiicons.h"
#include "explorer/verbs.h"
#include "explorer/fileopui.h"
#include "w32/w32.h"
#include "uxtheme.h"
extern "C" {
#include "commctrl.h"
#include "vssym32.h"
#include "dlgs.h"
#include "w32/flxp_dwmapi.h"
#include "w32/flxp_shell.h"
#undef small      // rpcndr.h (through dwmapi.h)
}
#include "explorer/browser.h"

asm(".section .rodata\n"
    ".global flxp_filedlg_res_start\n.global flxp_filedlg_res_end\n"
    "flxp_filedlg_res_start:\n.incbin \"flxp/rc/filedlg.res\"\nflxp_filedlg_res_end:\n"
    ".previous\n");
extern "C" const unsigned char flxp_filedlg_res_start[], flxp_filedlg_res_end[];

namespace filedlg {

using shell::Item;

static HINSTANCE module()
{
    static res::Module* m;
    if (!m) m = res::load_memory(flxp_filedlg_res_start, (size_t)(flxp_filedlg_res_end - flxp_filedlg_res_start));
    return (HINSTANCE)m;
}

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
#define WS(s) ((LPCWSTR)W(s).c_str())

static std::string text_of(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::vector<WCHAR> buf((size_t)n + 1);
    GetWindowTextW(h, buf.data(), n + 1);
    return w32::utf8_of(buf.data());
}

static bool is_dir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// "/a/./b/../c" -> "/a/c"; keeps a leading "/"
static std::string normalize(const std::string& p)
{
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string c = p.substr(i, j - i);
        if (c == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!c.empty() && c != ".") parts.push_back(c);
        i = j + 1;
    }
    std::string out;
    for (auto& c : parts) out += "/" + c;
    return out.empty() ? "/" : out;
}

static std::string expand_home(const std::string& s)
{
    if (s == "~" || s.rfind("~/", 0) == 0) return shell::home_dir() + s.substr(1);
    return s;
}

static bool has_wildcard(const std::string& s) { return s.find_first_of("*?") != std::string::npos; }

// ---- filters -------------------------------------------------------------------

static bool glob_match(const std::string& pat, const std::string& name)
{
    if (pat == "*" || pat == "*.*") return true;
    return fnmatch(pat.c_str(), name.c_str(), FNM_CASEFOLD | FNM_PERIOD) == 0;
}

bool matches(const Filter& f, const std::string& name)
{
    if (f.patterns.empty() && f.mime_types.empty()) return true;
    for (const std::string& p : f.patterns) if (glob_match(p, name)) return true;
    if (!f.mime_types.empty()) {
        std::string m = shell::mime_type_for_name(name);
        if (m.empty()) m = "application/octet-stream";
        for (const std::string& t : f.mime_types) {
            if (t == "*" || t == "*/*") return true;
            if (t.size() > 2 && t.compare(t.size() - 2, 2, "/*") == 0) {
                if (m.compare(0, t.size() - 1, t, 0, t.size() - 1) == 0) return true;
            } else if (shell::mime_is_a(m, shell::mime_canonical(t))) return true;
        }
    }
    return false;
}

// split "*.c;*.h" or "*.c *.h"
static std::vector<std::string> split_patterns(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ';' || c == ' ') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---- My Recent Documents: ~/.local/share/recently-used.xbel ----------------------

// places = true: "::recentplaces", the folders of those files (the Longhorn
// look's Recent Places), most recent first
class RecentFolder : public shell::Folder {
public:
    explicit RecentFolder(bool places = false) : m_places(places) {}
    bool m_places;
    std::string path() const override { return m_places ? "::recentplaces" : "::recent"; }
    std::string name() const override { return m_places ? "Recent Places" : "My Recent Documents"; }
    std::string icon() const override { return "recent"; }
    bool enumerate(std::vector<Item>& out, bool include_hidden) override
    {
        std::string xml;
        if (!shell::read_file(shell::data_home() + "/recently-used.xbel", xml)) return true;
        struct Entry { std::string path, modified; };
        std::vector<Entry> list;
        size_t pos = 0;
        while ((pos = xml.find("<bookmark ", pos)) != std::string::npos) {
            size_t end = xml.find('>', pos);
            if (end == std::string::npos) break;
            std::string tag = xml.substr(pos, end - pos);
            pos = end;
            auto attr = [&](const char* name) {
                std::string key = std::string(name) + "=\"";
                size_t a = tag.find(key);
                if (a == std::string::npos) return std::string();
                a += key.size();
                size_t b = tag.find('"', a);
                return b == std::string::npos ? std::string() : tag.substr(a, b - a);
            };
            std::string href = attr("href");
            // XML entities in the URI (&amp;) before percent-decoding
            for (size_t k; (k = href.find("&amp;")) != std::string::npos;) href.replace(k, 5, "&");
            std::string p = shell::path_from_uri(href);
            if (p.empty()) continue;
            std::string m = attr("modified");
            if (m.empty()) m = attr("visited");
            list.push_back({ p, m });
        }
        std::stable_sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
        std::map<std::string, bool> seen;
        int n = 0;
        for (const Entry& e0 : list) {
            Entry e = e0;
            if (m_places) e.path = shell::dir_of(e.path);
            if (seen[e.path] || n >= 200) continue;
            seen[e.path] = true;
            struct stat st;
            if (stat(e.path.c_str(), &st) != 0) continue;
            if (m_places && !S_ISDIR(st.st_mode)) continue;
            Item it;
            it.name = shell::base_of(e.path);
            if (!include_hidden && !it.name.empty() && it.name[0] == '.') continue;
            it.path = e.path;
            if (S_ISDIR(st.st_mode)) it.flags |= shell::IsFolder;
            it.size = (uint64_t)st.st_size;
            it.mtime = (int64_t)st.st_mtime;
            it.type = shell::type_name(it.name, it.is_folder());
            it.icon = shell::icon_key(it.name, it.is_folder());
            out.push_back(it);
            n++;
        }
        return true;
    }
    std::shared_ptr<shell::Folder> bind(const Item& child) override { return shell::folder_for(child.path); }
    std::shared_ptr<shell::Folder> parent() override { return shell::desktop(); }
};

static std::shared_ptr<shell::Folder> resolve(const std::string& path)
{
    if (path == "::recent") return std::make_shared<RecentFolder>();
    if (path == "::recentplaces") return std::make_shared<RecentFolder>(true);
    return shell::folder_for(path);
}

// ---- icons ---------------------------------------------------------------------

struct Icons {
    HIMAGELIST small = nullptr, large = nullptr;
    std::map<std::string, int> index;
    std::vector<std::string> src;              // each index's theme source (the zoom's lists)
    std::map<int, HIMAGELIST> sized;           // the Longhorn look's zoom: px -> a list with the same indices
};

static HINSTANCE module_for(const std::string& path)
{
    static std::map<std::string, HINSTANCE> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    HINSTANCE h = LoadLibraryExW(WS(path), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    cache[path] = h;
    return h;
}

// A theme source: a .ico/.bmp file or "pe:<file>#<id>" (as the explorer loads them).
static HANDLE load_themed(const std::string& src, UINT type, int cx, int cy, UINT extra)
{
    if (src.empty()) return nullptr;
    UINT depth_flag = (type == IMAGE_ICON && shell::icon_theme().depth <= 4) ? LR_VGACOLOR : 0;
    if (src.rfind("pe:", 0) == 0) {
        size_t hash = src.rfind('#');
        if (hash == std::string::npos) return nullptr;
        HINSTANCE h = module_for(src.substr(3, hash - 3));
        if (!h) return nullptr;
        return LoadImageW(h, MAKEINTRESOURCEW(atoi(src.c_str() + hash + 1)), type, cx, cy, depth_flag | extra);
    }
    return LoadImageW(nullptr, WS(src), type, cx, cy, LR_LOADFROMFILE | depth_flag | extra);
}

// exact key lookup (shell::icon_file falls back to "document")
static std::string theme_source(const std::string& key)
{
    for (auto& kv : shell::icon_theme().icons) if (kv.first == key) return kv.second;
    return "";
}

static int icon_for(Icons& ic, const std::string& key)
{
    auto it = ic.index.find(key);
    if (it != ic.index.end()) return it->second;
    std::string src = theme_source(key);
    if (src.empty()) src = shell::icon_file(key);
    HICON s = (HICON)load_themed(src, IMAGE_ICON, 16, 16, 0);
    HICON l = (HICON)load_themed(src, IMAGE_ICON, 32, 32, 0);
    int idx = -1;
    if (s && l) {
        idx = ImageList_ReplaceIcon(ic.small, -1, s);
        ImageList_ReplaceIcon(ic.large, -1, l);
        if ((int)ic.src.size() <= idx) ic.src.resize((size_t)idx + 1);
        ic.src[(size_t)idx] = src;
        for (auto& z : ic.sized) {
            HICON zi = (HICON)load_themed(src, IMAGE_ICON, z.first, z.first, 0);
            ImageList_ReplaceIcon(z.second, -1, zi ? zi : l);
            if (zi) DestroyIcon(zi);
        }
    } else if (key != "document") {
        idx = icon_for(ic, key == "recent" || key == "home" || key == "desktop" ? "folder" : "document");
    }
    if (s) DestroyIcon(s);
    if (l) DestroyIcon(l);
    ic.index[key] = idx;
    return idx;
}

// the zoom's list at px (the same indices as small/large)
static HIMAGELIST icons_sized(Icons& ic, int px)
{
    if (px == 16) return ic.small;
    if (px == 32) return ic.large;
    auto it = ic.sized.find(px);
    if (it != ic.sized.end()) return it->second;
    HIMAGELIST il = ImageList_Create(px, px, ILC_COLOR32 | ILC_MASK, 16, 16);
    int n = ImageList_GetImageCount(ic.small);
    for (int i = 0; i < n; i++) {
        HICON z = (size_t)i < ic.src.size() ? (HICON)load_themed(ic.src[(size_t)i], IMAGE_ICON, px, px, 0) : nullptr;
        if (!z) z = ImageList_GetIcon(ic.large, i, ILD_NORMAL);
        if (z) { ImageList_ReplaceIcon(il, -1, z); DestroyIcon(z); }
    }
    ic.sized[px] = il;
    return il;
}

// A bitmap strip as an image list: real alpha kept, otherwise colour-keyed on the top-left pixel.
static HIMAGELIST strip_list(const std::string& src, int size)
{
    HBITMAP strip = (HBITMAP)load_themed(src, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!strip) return nullptr;
    DIBSECTION ds = {};
    bool has_alpha = false;
    if (GetObjectW(strip, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32) {
        const uint8_t* p = (const uint8_t*)ds.dsBm.bmBits;
        size_t n = (size_t)ds.dsBm.bmWidth * (size_t)(ds.dsBmih.biHeight < 0 ? -ds.dsBmih.biHeight : ds.dsBmih.biHeight);
        for (size_t i = 0; i < n && !has_alpha; i++) if (p[i * 4 + 3] != 0xff) has_alpha = true;
    }
    HIMAGELIST il = ImageList_Create(size, size, (has_alpha ? ILC_COLOR32 : ILC_COLOR24) | ILC_MASK, 16, 4);
    if (has_alpha) ImageList_Add(il, strip, nullptr);
    else ImageList_AddMasked(il, strip, CLR_DEFAULT);
    DeleteObject(strip);
    return il;
}

// ---- the dialog ------------------------------------------------------------------

enum {
    ID_TOOLBAR = 0x4a0,      // XP reuses stc1 for it; a distinct id keeps GetDlgItem(stc1) unambiguous
    ID_BACK = 0xa001, ID_UP, ID_NEWFOLDER, ID_VIEWMENU,   // XP's IDC_BACK.. are private; same roles
    ID_VIEW_ICONS = 0xa101, ID_VIEW_LIST, ID_VIEW_DETAILS, ID_VIEW_THUMBS, ID_GROUPS, ID_VIEW_SMALL, ID_FORWARD, ID_CMDBAR, ID_PANE_ICONS,
    ID_VIEW_TILES, ID_VIEW_HIDENAMES,   // the Longhorn look's (browser::ViewCommand)
    ID_ZOOM_FIRST = 0xa160,  // View > Zoom (browser::ZOOM_MENU_IDS)
    ID_BAR = 0xa120, ID_BARTB, ID_PANE, ID_HEADER_ID,
    ID_HEADER_FIRST = 0xa140,  // View > Toolbars (header::MENU_IDS)
    ID_SORT_NAME = 0xa110, ID_SORT_SIZE, ID_SORT_TYPE, ID_SORT_DATE, ID_REFRESH, ID_HIDDEN,
    ID_PLACE_BASE = 0xa200,  // XP IDC_PLACESBAR_BASE
    ID_LIST = 0xa300,
};
enum { WM_APP_NAVIGATE = WM_APP + 1 };

struct Place { std::string name, path, icon; };
struct LookIn { std::string name, path, icon; int indent; };

// The list, its folder, items, views, sort, hidden files and history are the
// shared browser::Browser's (browser.h).
struct Dlg : browser::Browser {
    const Options* o = nullptr;
    Result* r = nullptr;
    HWND dlg = nullptr, lookin = nullptr, name_cbx = nullptr, name_edit = nullptr, types = nullptr;
    HWND toolbar = nullptr, places = nullptr;   // XP: the small toolbar, the places bar
    Icons icons;
    std::vector<Place> place_list;
    std::vector<LookIn> lookin_list;
    int current_lookin = -1;
    int type_filter = -1;                    // the Files of type entry in effect
    std::string custom;                      // a pattern typed into File name ("*.txt")
    std::string accept;                      // the default button's own label (Save turns it into Open on a folder)
    WNDPROC edit_proc = nullptr;
    // the Longhorn look
    bool lh = false;
    HWND bar = nullptr, bar_tb = nullptr, pane = nullptr, hide = nullptr;
    HWND viewstrip = nullptr;                // 4074's view button under the left pane (lh4074-180309)
    bool show_explorer = true;               // Save: Hide Explorer folds the browser away
    bool show_bar = true;                    // the 4074 command bar
    int pane_w = 155, bar_h = 0;
    bool splitting = false;
    int full_h = 0;                          // the window's height with the browser shown
    bool focus_active = true;
    UINT w_active = 0;
    HFONT label_font = nullptr;
    verbs::Host* outer_host = nullptr;
    bool ready = false;                      // set up: view changes from now on are the user's (remembered)

    std::shared_ptr<shell::Folder> resolve(const std::string& path) override;
    bool visible(const Item& it) override;
    bool ghosted(const Item& it) override { return (it.flags & shell::IsHidden) != 0; }   // ghosted, as XP shows hidden files
    bool size_shown(const Item& it) override { return !it.is_folder() && !(it.flags & shell::IsVirtual); }
    int icon(const std::string& key) override;
    HIMAGELIST icons_at(int px) override { return icons_sized(icons, px); }
    void filled() override;
    void changed_on_disk() override {}       // (filled() would move the focus to the first item)
    void shown() override;
    void view_changed() override;
    std::string pending_nav;                 // WM_APP_NAVIGATE target
    // layout
    SIZE client0 = {0, 0};
    struct Anchor { HWND h; RECT r; int how; };
    std::vector<Anchor> anchors;
    int place_line = 13;
};

enum { MOVE_X = 1, MOVE_Y = 2, SIZE_X = 4, SIZE_Y = 8 };

static Dlg* g_cur;                           // the innermost dialog (for the message filter hook)
static HHOOK g_hook;

static bool is_fs_path(const std::string& p) { return !p.empty() && p[0] == '/'; }

static std::string cur_dir(Dlg& d) { return d.folder && d.folder->is_filesystem() ? d.folder->path() : ""; }

static void fill_list(Dlg& d, const std::string& select = "");
static bool navigate(Dlg& d, const std::string& path, bool record = true);

// The filter in effect, as a Filter (typed pattern first).
bool Dlg::visible(const Item& it)
{
    if (o->mode == Mode::Folder) return it.is_folder();
    if (it.is_folder()) return true;
    if (!custom.empty()) {
        for (const std::string& p : split_patterns(custom)) if (glob_match(p, it.name)) return true;
        return false;
    }
    if (type_filter < 0 || type_filter >= (int)o->filters.size()) return true;
    return matches(o->filters[(size_t)type_filter], it.name);
}

std::shared_ptr<shell::Folder> Dlg::resolve(const std::string& path) { return filedlg::resolve(path); }

// ---- Look in ----

static void build_lookin(Dlg& d)
{
    std::vector<LookIn> L;
    auto root = shell::desktop();
    L.push_back({ root->name(), root->path(), root->icon(), 0 });
    std::vector<Item> top;
    root->enumerate(top, false);
    for (const Item& it : top) {
        L.push_back({ it.name, it.path, it.icon, 1 });
        if (it.path == "::computer") {
            auto c = shell::folder_for("::computer");
            std::vector<Item> drives;
            if (c) c->enumerate(drives, false);
            for (const Item& dv : drives) L.push_back({ dv.name, dv.path, dv.icon, 2 });
        }
    }
    // the current folder's ancestry, under its deepest ancestor already listed
    std::vector<std::shared_ptr<shell::Folder>> chain;
    for (auto f = d.folder; f; f = f->parent()) chain.push_back(f);
    std::reverse(chain.begin(), chain.end());   // root first
    int at = 0;
    size_t ci = 0;
    for (size_t i = 0; i < chain.size(); i++) {
        for (size_t k = 0; k < L.size(); k++)
            if (L[k].path == chain[i]->path()) { at = (int)k; ci = i + 1; break; }
    }
    int indent = L[(size_t)at].indent;
    int pos = at + 1;
    for (size_t i = ci; i < chain.size(); i++) {
        std::string icon = chain[i]->icon();
        L.insert(L.begin() + pos, LookIn{ chain[i]->name(), chain[i]->path(), icon, ++indent });
        pos++;
    }
    d.lookin_list = L;
    d.current_lookin = 0;
    for (size_t k = 0; k < L.size(); k++) if (d.folder && L[k].path == d.folder->path()) d.current_lookin = (int)k;
    SendMessageW(d.lookin, WM_SETREDRAW, FALSE, 0);
    SendMessageW(d.lookin, CB_RESETCONTENT, 0, 0);
    for (const LookIn& e : L) SendMessageW(d.lookin, CB_ADDSTRING, 0, (LPARAM)WS(e.name));
    SendMessageW(d.lookin, CB_SETCURSEL, (WPARAM)d.current_lookin, 0);
    SendMessageW(d.lookin, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d.lookin, nullptr, TRUE);
}

// XP MeasureDriveItems: "W" height + DRIVELIST_BORDER (3), at least a small icon
static void measure_lookin(HWND dlg, MEASUREITEMSTRUCT* mi)   // before WM_INITDIALOG: no Dlg yet
{
    HDC hdc = GetDC(nullptr);
    HFONT old = (HFONT)SelectObject(hdc, (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0));
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(hdc, L"W", 1, &sz);
    SelectObject(hdc, old);
    ReleaseDC(nullptr, hdc);
    int h = sz.cy + 3;
    mi->itemHeight = (UINT)std::max(h, 16);
}

// XP PaintDriveLine
static void draw_lookin(Dlg& d, DRAWITEMSTRUCT* di)
{
    if ((int)di->itemID < 0 || di->itemID >= d.lookin_list.size()) return;
    const LookIn& e = d.lookin_list[di->itemID];
    HDC hdc = di->hDC;
    RECT rc = di->rcItem;
    int offset = (di->itemState & ODS_COMBOBOXEDIT) ? 0 : 10 * e.indent;
    int xicon = rc.left + 3 + offset;
    int xtext = xicon + 16 + 4;
    std::u16string t = W(e.name);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(hdc, (LPCWSTR)t.c_str(), (int)t.size(), &sz);
    RECT tr = { xtext - 1, rc.top, xtext + sz.cx + 1, rc.bottom };
    if (di->itemAction != ODA_FOCUS) {
        FillRect(hdc, &di->rcItem, GetSysColorBrush(COLOR_WINDOW));
        bool sel = di->itemState & ODS_SELECTED;
        SetBkColor(hdc, GetSysColor(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        int y = rc.top + (rc.bottom - rc.top - sz.cy) / 2;
        UINT eto = ETO_OPAQUE;
        if ((di->itemState & ODS_COMBOBOXEDIT) && tr.right > rc.right) { tr.right = rc.right; eto |= ETO_CLIPPED; }
        ExtTextOutW(hdc, xtext, y, eto, &tr, (LPCWSTR)t.c_str(), (UINT)t.size(), nullptr);
        std::string key = e.icon;
        if ((int)di->itemID == d.current_lookin && key == "folder") key = "folder_open";
        int img = icon_for(d.icons, key);
        ImageList_Draw(d.icons.small, img, hdc, xicon, rc.top + (rc.bottom - rc.top - 16) / 2,
                       sel ? (ILD_SELECTED | ILD_FOCUS | ILD_TRANSPARENT) : ILD_TRANSPARENT);
    }
    if (di->itemAction == ODA_FOCUS || (di->itemState & ODS_FOCUS)) DrawFocusRect(hdc, &tr);
}

// ---- places bar ----

static void build_places(Dlg& d)
{
    std::vector<Place>& P = d.place_list;
    P.clear();
    P.push_back({ "My Recent Documents", "::recent", "recent" });
    P.push_back({ "Desktop", "::desktop", "desktop" });
    P.push_back({ "Home", shell::home_dir(), "home" });
    P.push_back({ "My Computer", "::computer", "computer" });
    for (const shell::Bookmark& b : shell::load_bookmarks()) {
        std::string p = b.path();
        if (p.empty() || !is_dir(p)) continue;
        P.push_back({ b.display_name(), p, "folder" });
    }
}

static void fill_places(Dlg& d)
{
    HWND tb = d.places;
    SendMessageW(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SetWindowTheme(tb, L"Placesbar", nullptr);
    bool themed = GetWindowTheme(tb) != nullptr;
    // XP OnThemeActive: themed = no client edge, opaque, padding 2,2
    LONG_PTR ex = GetWindowLongPtrW(tb, GWL_EXSTYLE);
    SetWindowLongPtrW(tb, GWL_EXSTYLE, themed ? (ex & ~WS_EX_CLIENTEDGE) : (ex | WS_EX_CLIENTEDGE));
    SetWindowPos(tb, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOMOVE | SWP_FRAMECHANGED);
    if (themed) SendMessageW(tb, TB_SETPADDING, 0, MAKELPARAM(2, 2));
    // Our toolbar draws one text row; the items are custom drawn with up to two
    // (TB_SETMAXTEXTROWS 2 in XP), so the image cell is made one line taller.
    HDC hdc = GetDC(tb);
    HFONT old = (HFONT)SelectObject(hdc, (HFONT)SendMessageW(d.dlg, WM_GETFONT, 0, 0));
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(tb, hdc);
    d.place_line = tm.tmHeight;
    SendMessageW(tb, WM_SETFONT, SendMessageW(d.dlg, WM_GETFONT, 0, 0), FALSE);
    SendMessageW(tb, TB_SETBITMAPSIZE, 0, MAKELONG(32, 32 + d.place_line + 4));
    SendMessageW(tb, TB_SETMAXTEXTROWS, 1, 0);
    for (size_t i = 0; i < d.place_list.size(); i++) {
        TBBUTTON b = {};
        b.iBitmap = I_IMAGENONE;
        b.idCommand = ID_PLACE_BASE + (int)i;
        b.fsState = TBSTATE_ENABLED | TBSTATE_WRAP;
        b.fsStyle = BTNS_BUTTON | BTNS_NOPREFIX;
        std::u16string n = W(d.place_list[i].name);
        b.iString = (INT_PTR)n.c_str();
        SendMessageW(tb, TB_ADDBUTTONSW, 1, (LPARAM)&b);
    }
    RECT rc;
    GetClientRect(tb, &rc);
    SendMessageW(tb, TB_SETBUTTONWIDTH, 0, MAKELONG(rc.right - rc.left, rc.right - rc.left));
    SendMessageW(tb, TB_AUTOSIZE, 0, 0);
    // XP's five places fill the bar's height (MAXPLACESBARITEMS); make ours the
    // same size, so the first five fit and bookmarks beyond show when it grows
    RECT ir = {};
    if (SendMessageW(tb, TB_GETITEMRECT, 0, (LPARAM)&ir)) {
        int want = std::max((int)(rc.bottom - rc.top) / 5, 32 + 2 * d.place_line + 8);
        int have = ir.bottom - ir.top;
        if (want != have) {
            SendMessageW(tb, TB_SETBITMAPSIZE, 0, MAKELONG(32, 32 + d.place_line + 4 + want - have));
            SendMessageW(tb, TB_SETBUTTONWIDTH, 0, MAKELONG(rc.right - rc.left, rc.right - rc.left));
            SendMessageW(tb, TB_AUTOSIZE, 0, 0);
        }
    }
}

static void check_place(Dlg& d)
{
    for (size_t i = 0; i < d.place_list.size(); i++)
        SendMessageW(d.places, TB_CHECKBUTTON, ID_PLACE_BASE + (int)i,
                     MAKELONG(d.folder && d.place_list[i].path == d.folder->path(), 0));
}

// Places items are drawn here: icon centred at the top, the name word-wrapped in
// up to two lines below. Classic: XP's dark (COLOR_BTNSHADOW) bar, white text,
// edges in highlight / 3D dark shadow; themed: Placesbar::Toolbar parts.
static LRESULT places_customdraw(Dlg& d, NMTBCUSTOMDRAW* cd)
{
    HTHEME th = GetWindowTheme(d.places);
    HDC hdc = cd->nmcd.hdc;
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREERASE: {
        RECT rc;
        GetClientRect(d.places, &rc);
        if (th) DrawThemeBackground(th, hdc, 0, 0, &rc, nullptr);
        else { HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW)); FillRect(hdc, &rc, b); DeleteObject(b); }
        return CDRF_SKIPDEFAULT;
    }
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        int i = (int)cd->nmcd.dwItemSpec - ID_PLACE_BASE;
        if (i < 0 || i >= (int)d.place_list.size()) return CDRF_DODEFAULT;
        RECT rc = cd->nmcd.rc;
        UINT st = cd->nmcd.uItemState;
        bool hot = st & CDIS_HOT, pressed = st & CDIS_SELECTED, checked = st & CDIS_CHECKED;
        COLORREF text = GetSysColor(COLOR_HIGHLIGHTTEXT);
        int push = 0;
        if (th) {
            int state = checked ? (hot ? TS_HOTCHECKED : TS_CHECKED) : pressed ? TS_PRESSED : hot ? TS_HOT : TS_NORMAL;
            if (state != TS_NORMAL) DrawThemeBackground(th, hdc, TP_BUTTON, state, &rc, nullptr);
            if (FAILED(GetThemeColor(th, TP_BUTTON, state, TMT_TEXTCOLOR, &text))) text = GetSysColor(COLOR_BTNTEXT);
        } else if (pressed || checked || hot) {
            bool sunken = pressed || checked;
            HPEN hi = CreatePen(PS_SOLID, 1, GetSysColor(sunken ? COLOR_3DDKSHADOW : COLOR_BTNHIGHLIGHT));
            HPEN lo = CreatePen(PS_SOLID, 1, GetSysColor(sunken ? COLOR_BTNHIGHLIGHT : COLOR_3DDKSHADOW));
            HGDIOBJ op = SelectObject(hdc, hi);
            MoveToEx(hdc, rc.left, rc.bottom - 1, nullptr); LineTo(hdc, rc.left, rc.top); LineTo(hdc, rc.right - 1, rc.top);
            SelectObject(hdc, lo);
            LineTo(hdc, rc.right - 1, rc.bottom - 1); LineTo(hdc, rc.left - 1, rc.bottom - 1);
            SelectObject(hdc, op);
            DeleteObject(hi); DeleteObject(lo);
            if (sunken) push = 1;
        }
        // icon and up to two lines of text, centred as a block
        std::u16string n = W(d.place_list[(size_t)i].name);
        HGDIOBJ of = SelectObject(hdc, (HFONT)SendMessageW(d.dlg, WM_GETFONT, 0, 0));
        RECT calc = { 0, 0, rc.right - rc.left - 4, 0 };
        DrawTextW(hdc, (LPCWSTR)n.c_str(), (int)n.size(), &calc, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        int lines = calc.bottom > d.place_line ? 2 : 1;
        int block = 32 + 2 + lines * d.place_line;
        int top = rc.top + std::max(2, (int)(rc.bottom - rc.top - block) / 2);
        int img = icon_for(d.icons, d.place_list[(size_t)i].icon);
        int x = rc.left + (rc.right - rc.left - 32) / 2 + push;
        ImageList_Draw(d.icons.large, img, hdc, x, top + push, ILD_TRANSPARENT);
        RECT tr = { rc.left + 2 + push, top + 32 + 2 + push, rc.right - 2 + push, 0 };
        tr.bottom = tr.top + lines * d.place_line;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text);
        DrawTextW(hdc, (LPCWSTR)n.c_str(), (int)n.size(), &tr, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(hdc, of);
        if ((st & CDIS_FOCUS) && GetFocus() == d.places) { RECT f = rc; InflateRect(&f, -2, -2); DrawFocusRect(hdc, &f); }
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

// ---- small toolbar ----

static void load_toolbar_images(HWND tb, int* view0, int* hist0);

static void create_toolbar(Dlg& d)
{
    RECT r;
    GetWindowRect(GetDlgItem(d.dlg, stc1), &r);
    MapWindowPoints(nullptr, d.dlg, (POINT*)&r, 2);
    d.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | CCS_NORESIZE | CCS_NODIVIDER | CCS_NOPARENTALIGN,
        r.left, r.top, r.right - r.left, r.bottom - r.top, d.dlg, (HMENU)(INT_PTR)ID_TOOLBAR, nullptr, nullptr);
    SendMessageW(d.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(d.toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
    SendMessageW(d.toolbar, TB_SETMAXTEXTROWS, 0, 0);
    int view0 = 0, hist0 = 12;
    load_toolbar_images(d.toolbar, &view0, &hist0);
    TBBUTTON b[] = {
        { hist0 + HIST_BACK, ID_BACK, 0, BTNS_BUTTON, {0}, 0, -1 },
        { view0 + VIEW_PARENTFOLDER, ID_UP, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { view0 + VIEW_NEWFOLDER, ID_NEWFOLDER, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, -1 },
        { view0 + VIEW_LIST, ID_VIEWMENU, TBSTATE_ENABLED, BTNS_WHOLEDROPDOWN, {0}, 0, -1 },
    };
    SendMessageW(d.toolbar, TB_ADDBUTTONSW, 4, (LPARAM)b);
}

// XP loads comctl32's IDB_VIEW_SMALL_COLOR + IDB_HIST_SMALL_COLOR; icon themes may supply
// the real strips (filedlg_view / filedlg_hist), else Wine's built-in ones.
static void load_toolbar_images(HWND tb, int* view0_out, int* hist0_out)
{
    int view0 = 0, hist0 = 12;
    HIMAGELIST v = strip_list(theme_source("filedlg_view"), 16);
    HIMAGELIST h = v ? strip_list(theme_source("filedlg_hist"), 16) : nullptr;
    if (v && h) {
        HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 20, 4);
        int nv = ImageList_GetImageCount(v);
        for (int i = 0; i < nv; i++) { HICON ic = ImageList_GetIcon(v, i, ILD_NORMAL); ImageList_ReplaceIcon(il, -1, ic); DestroyIcon(ic); }
        hist0 = nv;
        int nh = ImageList_GetImageCount(h);
        for (int i = 0; i < nh; i++) { HICON ic = ImageList_GetIcon(h, i, ILD_NORMAL); ImageList_ReplaceIcon(il, -1, ic); DestroyIcon(ic); }
        SendMessageW(tb, TB_SETIMAGELIST, 0, (LPARAM)il);
    } else {
        view0 = (int)SendMessageW(tb, TB_LOADIMAGES, IDB_VIEW_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
        hist0 = (int)SendMessageW(tb, TB_LOADIMAGES, IDB_HIST_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    }
    if (v) ImageList_Destroy(v);
    if (h) ImageList_Destroy(h);
    *view0_out = view0;
    *hist0_out = hist0;
}

static void update_toolbar(Dlg& d)
{
    if (!d.toolbar) return;
    SendMessageW(d.toolbar, TB_ENABLEBUTTON, ID_BACK, MAKELONG(!d.back.empty(), 0));
    SendMessageW(d.toolbar, TB_ENABLEBUTTON, ID_UP, MAKELONG(d.folder && d.folder->parent() != nullptr, 0));
    SendMessageW(d.toolbar, TB_ENABLEBUTTON, ID_NEWFOLDER, MAKELONG(d.folder && d.folder->is_filesystem() && access(d.folder->path().c_str(), W_OK) == 0, 0));
}

// ---- the view ----

// view: LVS_* or VIEW_THUMBNAILS (the Longhorn look's Thumbnails)
static void set_view(Dlg& d, int view) { browser::set_view(d, view); }

// The list from the folder again (refresh), selecting `select` when it is there
static void fill_list(Dlg& d, const std::string& select)
{
    d.select_after_refresh = select;
    browser::refresh(d);
}

int Dlg::icon(const std::string& key) { return icon_for(icons, key); }

// XP: the first item has the focus (not the selection) unless one was selected
static void update_ok_label(Dlg& d);

void Dlg::filled()
{
    update_ok_label(*this);   // a folder opened from a selection that had turned Save into Open
    if (items.empty() || SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0) > 0) return;
    LVITEMW st = {};
    st.stateMask = LVIS_FOCUSED;
    st.state = LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, 0, (LPARAM)&st);
    SendMessageW(list, LVM_ENSUREVISIBLE, 0, FALSE);
}

static std::vector<int> selected_indices(Dlg& d)
{
    std::vector<int> out;
    int i = -1;
    while ((i = (int)SendMessageW(d.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0) {
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = i;
        SendMessageW(d.list, LVM_GETITEMW, 0, (LPARAM)&lvi);
        if (lvi.lParam >= 0 && (size_t)lvi.lParam < d.items.size()) out.push_back((int)lvi.lParam);
    }
    return out;
}

static void set_name(Dlg& d, const std::string& s)
{
    SetWindowTextW(d.name_edit, WS(s));
}

// Save: a selection of folders only turns Save into Open (XP); anything else, and a fresh folder, is Save again
static void update_ok_label(Dlg& d)
{
    if (d.o->mode != Mode::Save) return;
    bool folder = false, file = false;
    for (int i : selected_indices(d)) (d.items[(size_t)i].is_folder() ? folder : file) = true;
    SetDlgItemTextW(d.dlg, IDOK, WS(folder && !file ? std::string("&Open") : d.accept));
}

// XP SelFocusChange: the selected files (not folders) go into File name;
// several are quoted. Folder mode puts the selected folder there.
static void selection_to_name(Dlg& d)
{
    if (d.filling) return;
    std::vector<std::string> names;
    for (int i : selected_indices(d)) {
        const Item& it = d.items[(size_t)i];
        bool want = d.o->mode == Mode::Folder ? (it.is_folder() && is_fs_path(it.path)) : !it.is_folder();
        if (!want) continue;
        // items from elsewhere (Recent) carry their full path
        bool here = is_fs_path(it.path) && shell::dir_of(it.path) == cur_dir(d);
        names.push_back(here ? it.name : it.path);
    }
    update_ok_label(d);
    if (names.empty()) return;
    if (names.size() == 1) { set_name(d, names[0]); return; }
    std::string s;
    for (auto& n : names) { if (!s.empty()) s += ' '; s += '"' + n + '"'; }
    set_name(d, s);
}

static void lh_folder_shown(Dlg& d);
static void lh_view_changed(Dlg& d);

// after refresh: XP: Look in, the places bar, the toolbar; Longhorn: the
// header, the places pane's bold entry, the command bar, "Will be saved in"
void Dlg::shown()
{
    if (lh) { lh_folder_shown(*this); return; }
    build_lookin(*this);
    check_place(*this);
    update_toolbar(*this);
}

void Dlg::view_changed() { if (lh) lh_view_changed(*this); }

static bool navigate(Dlg& d, const std::string& path, bool record) { return browser::navigate(d, path, record); }
static void go_up(Dlg& d) { browser::go_up(d); }
static void go_back(Dlg& d) { browser::go_back(d); update_toolbar(d); }
static void go_forward(Dlg& d) { browser::go_forward(d); }

static void new_folder(Dlg& d)
{
    std::string dir = cur_dir(d), path, err;
    if (dir.empty()) return;
    if (!shell::new_folder(dir, &path, &err)) { message_box(d.dlg, "Error Creating Folder", "Cannot create the folder.\n" + err, MB_OK | MB_ICONEXCLAMATION); return; }
    fill_list(d, path);
    for (size_t i = 0; i < d.items.size(); i++)
        if (d.items[i].path == path) { SetFocus(d.list); SendMessageW(d.list, LVM_EDITLABELW, (WPARAM)i, 0); break; }
}

static void delete_selection(Dlg& d)
{
    std::vector<int> sel = selected_indices(d);
    if (sel.empty()) return;
    std::vector<std::string> paths;
    for (int i : sel) if (is_fs_path(d.items[(size_t)i].path) && !(d.items[(size_t)i].flags & shell::IsVirtual)) paths.push_back(d.items[(size_t)i].path);
    if (paths.empty()) return;
    std::string q = paths.size() == 1 ? "Are you sure you want to send '" + shell::base_of(paths[0]) + "' to the Trash?"
                                      : "Are you sure you want to send these " + std::to_string(paths.size()) + " items to the Trash?";
    if (message_box(d.dlg, paths.size() == 1 ? "Confirm File Delete" : "Confirm Multiple File Delete", q, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    for (const std::string& p : paths) {
        std::string err;
        if (!shell::trash(p, &err)) { message_box(d.dlg, "Error Deleting File", "Cannot delete " + shell::base_of(p) + ": " + err, MB_OK | MB_ICONEXCLAMATION); break; }
    }
    fill_list(d);
}

// The XP look's views (XP's Open dialog: Thumbnails, Tiles, Icons, List, Details; no Thumbnails here)
static void xp_view_items(Dlg& d, HMENU m)
{
    AppendMenuW(m, MF_STRING, ID_VIEW_TILES, L"Tile&s");
    AppendMenuW(m, MF_STRING, ID_VIEW_ICONS, L"Ic&ons");
    AppendMenuW(m, MF_STRING, ID_VIEW_LIST, L"&List");
    AppendMenuW(m, MF_STRING, ID_VIEW_DETAILS, L"&Details");
    int at = d.tiles ? 0 : d.view == LVS_ICON || d.view == LVS_SMALLICON ? 1 : d.view == LVS_LIST ? 2 : 3;
    CheckMenuRadioItem(m, 0, 3, (UINT)at, MF_BYPOSITION);
}

static void view_menu(Dlg& d)
{
    RECT r = {};
    SendMessageW(d.toolbar, TB_GETRECT, ID_VIEWMENU, (LPARAM)&r);
    MapWindowPoints(d.toolbar, nullptr, (POINT*)&r, 2);
    HMENU m = CreatePopupMenu();
    xp_view_items(d, m);
    SendMessageW(d.toolbar, TB_PRESSBUTTON, ID_VIEWMENU, MAKELONG(TRUE, 0));
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, d.dlg, nullptr);
    SendMessageW(d.toolbar, TB_PRESSBUTTON, ID_VIEWMENU, MAKELONG(FALSE, 0));
    DestroyMenu(m);
    if (cmd == ID_VIEW_TILES) set_view(d, VIEW_TILES);
    else if (cmd == ID_VIEW_ICONS) set_view(d, LVS_ICON);
    else if (cmd == ID_VIEW_LIST) set_view(d, LVS_LIST);
    else if (cmd == ID_VIEW_DETAILS) set_view(d, LVS_REPORT);
}

// The Longhorn look's views (the explorer's: 4074's Details, Tiles, Icons, Hide File Names;
// Show Thumbnails, the thumbnails setting), then a separator
static void lh_view_items(Dlg& d, HMENU m)
{
    int code = browser::view_code(d);
    UINT radio = d.view == LVS_REPORT ? ID_VIEW_DETAILS : d.tiles ? ID_VIEW_TILES : ID_VIEW_ICONS;
    AppendMenuW(m, MF_STRING, ID_VIEW_DETAILS, L"&Details\tCtrl+1");
    AppendMenuW(m, MF_STRING, ID_VIEW_TILES, L"Tile&s\tCtrl+2");
    AppendMenuW(m, MF_STRING, ID_VIEW_ICONS, L"Ic&ons");
    CheckMenuRadioItem(m, 0, 2, radio == ID_VIEW_DETAILS ? 0 : radio == ID_VIEW_TILES ? 1 : 2, MF_BYPOSITION);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | ((code & VIEW_NONAMES_FLAG) ? MF_CHECKED : 0) | (radio == ID_VIEW_ICONS ? 0 : MF_GRAYED), ID_VIEW_HIDENAMES, L"Hide Fi&le Names");
    AppendMenuW(m, MF_STRING | (shell::setting("thumbnails", "1") != "0" ? MF_CHECKED : 0), ID_VIEW_THUMBS, L"Sho&w Thumbnails");
}

// Right-click on the view background: View, Arrange Icons By, Refresh, New Folder (XP's defview background menu, trimmed).
static void background_menu(Dlg& d, POINT pt)
{
    HMENU view = CreatePopupMenu();
    if (d.lh) {
        lh_view_items(d, view);
        AppendMenuW(view, MF_STRING | (d.grouped ? MF_CHECKED : 0) | (browser::group_view_applies(d) ? 0 : MF_GRAYED), ID_GROUPS, L"Show in &Groups");
    } else xp_view_items(d, view);
    HMENU arrange = CreatePopupMenu();
    AppendMenuW(arrange, MF_STRING, ID_SORT_NAME, L"&Name");
    AppendMenuW(arrange, MF_STRING, ID_SORT_SIZE, L"&Size");
    AppendMenuW(arrange, MF_STRING, ID_SORT_TYPE, L"&Type");
    AppendMenuW(arrange, MF_STRING, ID_SORT_DATE, L"&Modified");
    CheckMenuRadioItem(arrange, ID_SORT_NAME, ID_SORT_DATE, ID_SORT_NAME + (int)d.sort, MF_BYCOMMAND);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_POPUP, (UINT_PTR)view, L"&View");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)arrange, L"Arrange &Icons By");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_REFRESH, L"R&efresh");
    AppendMenuW(m, MF_STRING | (d.show_hidden ? MF_CHECKED : 0), ID_HIDDEN, L"Show &Hidden Files\tCtrl+H");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (cur_dir(d).empty() ? MF_GRAYED : 0), ID_NEWFOLDER, L"New &Folder");
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, d.dlg, nullptr);
    DestroyMenu(m);
    if (cmd) SendMessageW(d.dlg, WM_COMMAND, (WPARAM)cmd, 0);
}

// Right-click on an item: Select (the dialog's verb: open a folder, accept a
// file), Rename, Delete. XP shows the shell's full item menu here.
static void on_ok(Dlg& d);
static void item_menu(Dlg& d, int index, POINT pt)
{
    LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = index;
    SendMessageW(d.list, LVM_GETITEMW, 0, (LPARAM)&lvi);
    if (lvi.lParam < 0 || (size_t)lvi.lParam >= d.items.size()) return;
    const Item it = d.items[(size_t)lvi.lParam];
    bool fs = is_fs_path(it.path) && !(it.flags & shell::IsVirtual);
    enum { CMD_SELECT = 1, CMD_RENAME, CMD_DELETE };
    HMENU m = CreatePopupMenu();
    bool open_folder = it.is_folder() && d.o->mode != Mode::Folder;
    AppendMenuW(m, MF_STRING, CMD_SELECT, open_folder ? L"&Open" : d.o->mode == Mode::Save ? L"&Save" : L"&Select");
    SetMenuDefaultItem(m, CMD_SELECT, FALSE);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (fs ? 0 : MF_GRAYED), CMD_RENAME, L"Rena&me");
    AppendMenuW(m, MF_STRING | (fs ? 0 : MF_GRAYED), CMD_DELETE, L"&Delete");
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, d.dlg, nullptr);
    DestroyMenu(m);
    if (cmd == CMD_SELECT) {
        if (open_folder) navigate(d, it.path, true);
        else { selection_to_name(d); on_ok(d); }
    } else if (cmd == CMD_RENAME) {
        SetFocus(d.list);
        SendMessageW(d.list, LVM_EDITLABELW, (WPARAM)index, 0);
    } else if (cmd == CMD_DELETE) delete_selection(d);
}

// ---- accepting ----

static std::string resolve_name(Dlg& d, const std::string& name_in)
{
    std::string name = expand_home(name_in);
    if (name.empty()) return "";
    if (name[0] == '/') return normalize(name);
    std::string dir = cur_dir(d);
    if (dir.empty()) dir = shell::home_dir();   // a relative name typed in a virtual folder
    return normalize(dir + "/" + name);
}

static std::vector<std::string> split_names(const std::string& s)
{
    std::vector<std::string> out;
    if (s.find('"') == std::string::npos) { out.push_back(s); return out; }
    size_t i = 0;
    while (i < s.size()) {
        size_t a = s.find('"', i);
        if (a == std::string::npos) { std::string rest = trim(s.substr(i)); if (!rest.empty()) out.push_back(rest); break; }
        std::string before = trim(s.substr(i, a - i));
        if (!before.empty()) out.push_back(before);
        size_t b = s.find('"', a + 1);
        if (b == std::string::npos) { out.push_back(s.substr(a + 1)); break; }
        out.push_back(s.substr(a + 1, b - a - 1));
        i = b + 1;
    }
    return out;
}

static std::string ext_of(const std::string& name)
{
    std::string b = shell::base_of(name);
    size_t dot = b.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? "" : b.substr(dot + 1);
}

static bool finish(Dlg& d, const std::vector<std::string>& paths)
{
    d.r->ok = true;
    d.r->paths = paths;
    d.r->filter_index = d.custom.empty() ? d.type_filter : -1;
    d.r->read_only = IsDlgButtonChecked(d.dlg, chx1) == BST_CHECKED;
    d.r->folder = d.folder ? d.folder->path() : "";
    EndDialog(d.dlg, IDOK);
    return true;
}

static void error(Dlg& d, const std::string& text)
{
    message_box(d.dlg, d.o->title.empty() ? (d.o->mode == Mode::Save ? "Save As" : "Open") : d.o->title, text, MB_OK | MB_ICONEXCLAMATION);
    SetFocus(d.name_edit);
    SendMessageW(d.name_edit, EM_SETSEL, 0, -1);
}

// XP CFileOpenBrowser::OnOKButton / ProcessEdit, for our namespace.
static void on_ok(Dlg& d)
{
    std::vector<int> sel = selected_indices(d);
    std::string text = trim(text_of(d.name_edit));
    // Open/Save with a single folder selected in the view (or an empty name): open the folder
    if (d.o->mode != Mode::Folder && sel.size() == 1 && d.items[(size_t)sel[0]].is_folder() && (GetFocus() == d.list || text.empty())) {
        navigate(d, d.items[(size_t)sel[0]].path, true);
        return;
    }
    if (text.empty()) {
        if (d.o->mode == Mode::Folder) {
            std::string dir = cur_dir(d);
            if (!dir.empty()) finish(d, { dir });
            else MessageBeep(MB_OK);
        }
        return;
    }
    std::vector<std::string> names = (d.o->multiple && d.o->mode == Mode::Open) ? split_names(text) : std::vector<std::string>{ text };
    if (names.size() == 1) {
        std::string name = names[0];
        if (d.o->mode != Mode::Folder && has_wildcard(name)) {
            // a pattern filters the view; "dir/*.c" also changes folder (XP)
            size_t slash = name.find_last_of('/');
            if (slash != std::string::npos) {
                std::string dir = resolve_name(d, name.substr(0, slash + 1));
                if (!is_dir(dir)) { error(d, name + "\nPath does not exist.\nPlease verify the correct path was given."); return; }
                d.custom = name.substr(slash + 1);
                navigate(d, dir, true);
            } else {
                d.custom = name;
                fill_list(d);
            }
            set_name(d, d.custom);
            SendMessageW(d.name_edit, EM_SETSEL, 0, -1);
            return;
        }
        std::string p = resolve_name(d, name);
        if (is_dir(p)) {
            if (d.o->mode == Mode::Folder && (sel.size() == 1 || GetFocus() != d.list)) {
                // Select Folder takes a typed or selected folder (Vista's FOS_PICKFOLDERS)
                finish(d, { p });
                return;
            }
            navigate(d, p, true);
            set_name(d, "");
            return;
        }
        if (!name.empty() && name.back() == '/') { error(d, name + "\nPath does not exist.\nPlease verify the correct path was given."); return; }
    }
    std::vector<std::string> out;
    for (const std::string& n : names) {
        std::string p = resolve_name(d, n);
        if (p.empty()) continue;
        if (d.o->mode == Mode::Save && !d.o->default_ext.empty() && ext_of(p).empty()) {
            std::string e = d.o->default_ext;
            if (e[0] == '.') e = e.substr(1);
            p += "." + e;
        }
        std::string dir = shell::dir_of(p);
        if (!is_dir(dir)) {
            if (d.o->path_must_exist || d.o->mode != Mode::Save) { error(d, n + "\nPath does not exist.\nPlease verify the correct path was given."); return; }
        }
        bool there = exists(p);
        if (d.o->mode == Mode::Folder) {
            if (!there) { error(d, n + "\nPath does not exist.\nPlease verify the correct path was given."); return; }
            if (!is_dir(p)) { error(d, n + "\nThe above name is not a folder."); return; }
        } else if (d.o->mode == Mode::Open) {
            if (!there && d.o->create_prompt) {
                if (message_box(d.dlg, "Open", n + "\nThis file does not exist.\n\nCreate the file?", MB_YESNO | MB_ICONEXCLAMATION) != IDYES) return;
            } else if (!there && d.o->file_must_exist) {
                error(d, n + "\nFile not found.\nPlease verify the correct file name was given.");
                return;
            }
        } else {   // Save
            if (there && is_dir(p)) { navigate(d, p, true); set_name(d, ""); return; }
            if (there && access(p.c_str(), W_OK) != 0) { error(d, n + "\nThis file exists with Read Only attributes.\nPlease use a different file name."); return; }
            if (there && d.o->overwrite_prompt) {
                if (message_box(d.dlg, d.o->title.empty() ? "Save As" : d.o->title,
                                shell::base_of(p) + " already exists.\nDo you want to replace it?", MB_YESNO | MB_ICONEXCLAMATION) != IDYES) {
                    SetFocus(d.name_edit);
                    return;
                }
            }
            if (!there && is_dir(dir) && access(dir.c_str(), W_OK) != 0) {
                error(d, n + "\nYou do not have permission to save in this folder.");
                return;
            }
        }
        out.push_back(p);
    }
    if (!out.empty()) finish(d, out);
}

// ---- File name completion (XP SHAutoComplete with ACO_AUTOAPPEND) ----

static void autocomplete(Dlg& d)
{
    std::string text = text_of(d.name_edit);
    DWORD s = 0, e = 0;
    SendMessageW(d.name_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    std::u16string t16 = W(text);
    if (text.empty() || s != e || s != t16.size() || text.find('"') != std::string::npos || has_wildcard(text)) return;
    size_t slash = text.find_last_of('/');
    std::string dir_part = slash == std::string::npos ? "" : text.substr(0, slash + 1);
    std::string prefix = slash == std::string::npos ? text : text.substr(slash + 1);
    if (prefix.empty()) return;
    std::string dir = dir_part.empty() ? cur_dir(d) : resolve_name(d, dir_part);
    if (dir.empty()) return;
    DIR* dp = opendir(dir.c_str());
    if (!dp) return;
    std::vector<std::pair<std::string, bool>> cands;
    while (struct dirent* de = readdir(dp)) {
        std::string n = de->d_name;
        if (n == "." || n == "..") continue;
        if (n[0] == '.' && prefix[0] != '.') continue;
        if (n.size() <= prefix.size() || n.compare(0, prefix.size(), prefix) != 0) continue;
        bool folder = is_dir(dir + "/" + n);
        if (!folder && d.o->mode == Mode::Folder) continue;
        cands.push_back({ n, folder });
    }
    closedir(dp);
    if (cands.empty()) return;
    std::sort(cands.begin(), cands.end());
    std::string full = dir_part + cands[0].first + (cands[0].second ? "/" : "");
    std::u16string f16 = W(full);
    SetWindowTextW(d.name_edit, (LPCWSTR)f16.c_str());
    SendMessageW(d.name_edit, EM_SETSEL, (WPARAM)t16.size(), (LPARAM)f16.size());
}

static RECT lh_band_rect(Dlg& d);

static LRESULT CALLBACK edit_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Dlg* d = (Dlg*)GetPropW(h, L"flxp_filedlg");
    LRESULT r = CallWindowProcW(d->edit_proc, h, msg, wp, lp);
    if (msg == WM_CHAR && wp >= 0x20 && wp != 0x7f) autocomplete(*d);
    if (d->lh && d->o->mode == Mode::Save && (msg == WM_CHAR || msg == WM_SETTEXT || msg == WM_KEYDOWN || msg == WM_CUT || msg == WM_PASTE || msg == WM_CLEAR)) {
        RECT rc; GetClientRect(d->dlg, &rc);   // the details band's icon follows the name's type
        RECT band = lh_band_rect(*d);
        RECT ir = { 0, band.top, 72, band.bottom };
        InvalidateRect(d->dlg, &ir, TRUE);
    }
    return r;
}

// ---- layout ----

static RECT child_rect(HWND dlg, HWND h)
{
    RECT r;
    GetWindowRect(h, &r);
    MapWindowPoints(nullptr, dlg, (POINT*)&r, 2);
    return r;
}

static void add_anchor(Dlg& d, HWND h, int how)
{
    if (!h) return;
    d.anchors.push_back({ h, child_rect(d.dlg, h), how });
}

static void layout(Dlg& d, int cx, int cy)
{
    if (!d.client0.cx) return;
    int dx = std::max(0, cx - (int)d.client0.cx), dy = std::max(0, cy - (int)d.client0.cy);
    for (const Dlg::Anchor& a : d.anchors) {
        RECT r = a.r;
        if (a.how & MOVE_X) OffsetRect(&r, dx, 0);
        if (a.how & MOVE_Y) OffsetRect(&r, 0, dy);
        if (a.how & SIZE_X) r.right += dx;
        if (a.how & SIZE_Y) r.bottom += dy;
        SetWindowPos(a.h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (d.places) {
        RECT rc;
        GetClientRect(d.places, &rc);
        SendMessageW(d.places, TB_SETBUTTONWIDTH, 0, MAKELONG(rc.right - rc.left, rc.right - rc.left));
    }
    if (d.view != LVS_REPORT) SendMessageW(d.list, LVM_ARRANGE, LVA_DEFAULT, 0);
    InvalidateRect(d.dlg, nullptr, TRUE);
}

// ---- placement ----

static void place_dialog(Dlg& d, HWND owner)
{
    sys::NativeWindow* nw = sys::window_for_user((void*)d.dlg);
    uint64_t owner_id = d.o->parent_window;
    RECT orc = {0, 0, 0, 0};
    bool have = false;
    if (owner) {
        HWND root = GetAncestor(owner, GA_ROOT);
        if (root) {
            GetWindowRect(root, &orc);
            have = true;
            if (!owner_id) owner_id = sys::native_id(sys::window_for_user((void*)root));
        }
    } else if (owner_id) {
        gdi::Rect r;
        if (sys::native_window_rect(owner_id, r)) { orc = { r.left, r.top, r.right, r.bottom }; have = true; }
    }
    sys::set_transient_for(nw, owner_id, d.o->modal && owner_id);
    RECT wr;
    GetWindowRect(d.dlg, &wr);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    if (!have) {
        gdi::Size s = sys::screen_size();
        orc = { 0, 0, s.cx, s.cy };
    }
    int x = orc.left + (orc.right - orc.left - w) / 2, y = orc.top + (orc.bottom - orc.top - h) / 2;
    gdi::Size s = sys::screen_size();
    x = std::max(0, std::min(x, (int)s.cx - w));
    y = std::max(0, std::min(y, (int)s.cy - h));
    SetWindowPos(d.dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (nw) sys::set_min_size(nw, w, h);
}

// ---- init ----

// Put the controls in this z-order (= tab order). A new child is linked at the
// bottom, so the template's controls are in template order already; the view
// and other controls created afterwards are slotted in where they belong.
static void order_children(const std::vector<HWND>& order)
{
    HWND after = HWND_TOP;
    for (HWND h : order) {
        if (!h) continue;
        SetWindowPos(h, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        after = h;
    }
}

static void init_dialog(Dlg& d, HWND owner)
{
    const Options& o = *d.o;
    HWND dlg = d.dlg;
    d.show_hidden = o.show_hidden;

    // texts
    std::string title = o.title;
    if (title.empty()) title = o.mode == Mode::Save ? "Save As" : o.mode == Mode::Folder ? "Select Folder" : "Open";
    SetWindowTextW(dlg, WS(title));
    std::string accept = o.accept_label;
    if (accept.empty()) accept = o.mode == Mode::Save ? "&Save" : o.mode == Mode::Folder ? "&Select" : "&Open";
    else if (accept.find('&') == std::string::npos && accept.find('_') != std::string::npos) {
        // GTK mnemonics ("_Open") from the portal
        for (char& c : accept) if (c == '_') { c = '&'; break; }
    }
    SetDlgItemTextW(dlg, IDOK, WS(accept));
    d.accept = accept;
    if (o.mode == Mode::Save) {
        SetDlgItemTextW(dlg, stc4, L"Save &in:");
        SetDlgItemTextW(dlg, stc2, L"Save as &type:");
    }
    if (o.mode == Mode::Folder) {
        SetDlgItemTextW(dlg, stc3, L"Folder &name:");
        ShowWindow(GetDlgItem(dlg, stc2), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, cmb1), SW_HIDE);
    }
    if (!o.show_read_only) ShowWindow(GetDlgItem(dlg, chx1), SW_HIDE);
    else CheckDlgButton(dlg, chx1, o.read_only ? BST_CHECKED : BST_UNCHECKED);
    ShowWindow(GetDlgItem(dlg, pshHelp), SW_HIDE);
    if (!o.show_read_only) {
        // XP ReAdjustDialog: without Help and the read-only box, everything below the
        // view moves down by the Help button's height plus the gap above it, and the
        // view grows by as much
        RECT help = child_rect(dlg, GetDlgItem(dlg, pshHelp)), cancel = child_rect(dlg, GetDlgItem(dlg, IDCANCEL));
        RECT view = child_rect(dlg, GetDlgItem(dlg, lst1));
        int delta = (help.bottom - help.top) + (help.top - cancel.bottom);
        for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            int id = GetDlgCtrlID(c);
            if (id == pshHelp || id == chx1) continue;
            RECT r = child_rect(dlg, c);
            if (r.top > view.bottom) SetWindowPos(c, nullptr, r.left, r.top + delta, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        SetWindowPos(GetDlgItem(dlg, lst1), nullptr, 0, 0, view.right - view.left, view.bottom - view.top + delta, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // (XP also deletes Minimize/Maximize/Restore from the system menu; the window manager owns it here)

    d.lookin = GetDlgItem(dlg, cmb2);
    d.types = GetDlgItem(dlg, cmb1);
    d.places = GetDlgItem(dlg, ctl1);
    d.name_cbx = GetDlgItem(dlg, cmb13);
    d.name_edit = (HWND)SendMessageW(d.name_cbx, CBEM_GETEDITCONTROL, 0, 0);
    if (!d.name_edit) d.name_edit = GetDlgItem(dlg, edt1);
    SetPropW(d.name_edit, L"flxp_filedlg", (HANDLE)&d);
    d.edit_proc = (WNDPROC)SetWindowLongPtrW(d.name_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass);

    // filters
    d.type_filter = o.filters.empty() ? -1 : std::max(0, std::min(o.filter_index, (int)o.filters.size() - 1));
    if (o.filters.empty()) SendMessageW(d.types, CB_ADDSTRING, 0, (LPARAM)L"All Files");
    for (const Filter& f : o.filters) SendMessageW(d.types, CB_ADDSTRING, 0, (LPARAM)WS(f.name));
    SendMessageW(d.types, CB_SETCURSEL, (WPARAM)std::max(d.type_filter, 0), 0);

    // the view, over lst1
    HWND lst1w = GetDlgItem(dlg, lst1);
    RECT lr = child_rect(dlg, lst1w);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_LIST | LVS_SHAREIMAGELISTS | LVS_SHOWSELALWAYS | LVS_EDITLABELS | LVS_AUTOARRANGE;
    if (!(o.multiple && o.mode == Mode::Open)) style |= LVS_SINGLESEL;
    static const int widths[4] = { 180, 70, 110, 120 };
    browser::create_list(d, dlg, ID_LIST, style, WS_EX_CLIENTEDGE, widths, (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0));
    SetWindowPos(d.list, nullptr, lr.left, lr.top, lr.right - lr.left, lr.bottom - lr.top, SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(d.list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_LABELTIP, LVS_EX_LABELTIP);
    if (const char* v = getenv("FLXP_FILEDLG_VIEW")) set_view(d, !strcmp(v, "details") ? LVS_REPORT : !strcmp(v, "icons") ? LVS_ICON : LVS_LIST);

    create_toolbar(d);
    build_places(d);
    fill_places(d);

    // starting folder and name
    std::string name = o.default_name, start = o.initial_dir;
    if (!name.empty() && name.find('/') != std::string::npos) {
        std::string full = resolve_name(d, name);
        if (start.empty() && is_dir(shell::dir_of(full))) start = shell::dir_of(full);
        name = shell::base_of(full);
    }
    if (start.empty()) start = shell::home_dir();
    start = expand_home(start);
    if (!navigate(d, start, false) && !navigate(d, shell::home_dir(), false)) navigate(d, "::desktop", false);
    set_name(d, name);

    order_children({ GetDlgItem(dlg, stc4), d.lookin, GetDlgItem(dlg, stc1), d.toolbar, d.places, lst1w, d.list,
                     GetDlgItem(dlg, stc3), GetDlgItem(dlg, edt1), d.name_cbx, GetDlgItem(dlg, stc2), d.types,
                     GetDlgItem(dlg, chx1), GetDlgItem(dlg, IDOK), GetDlgItem(dlg, IDCANCEL), GetDlgItem(dlg, pshHelp) });

    // resizing (XP _bEnableSizing): everything right of / below the view follows the frame
    RECT cr;
    GetClientRect(dlg, &cr);
    d.client0 = { cr.right, cr.bottom };
    add_anchor(d, d.lookin, SIZE_X);
    add_anchor(d, d.toolbar, MOVE_X);
    add_anchor(d, d.places, SIZE_Y);
    add_anchor(d, d.list, SIZE_X | SIZE_Y);
    add_anchor(d, GetDlgItem(dlg, stc3), MOVE_Y);
    add_anchor(d, GetDlgItem(dlg, stc2), MOVE_Y);
    add_anchor(d, d.name_cbx, MOVE_Y | SIZE_X);
    add_anchor(d, d.types, MOVE_Y | SIZE_X);
    add_anchor(d, GetDlgItem(dlg, chx1), MOVE_Y);
    add_anchor(d, GetDlgItem(dlg, IDOK), MOVE_X | MOVE_Y);
    add_anchor(d, GetDlgItem(dlg, IDCANCEL), MOVE_X | MOVE_Y);
    add_anchor(d, GetDlgItem(dlg, pshHelp), MOVE_X | MOVE_Y);

    place_dialog(d, owner);
    SetFocus(d.name_edit);
    SendMessageW(d.name_edit, EM_SETSEL, 0, -1);
}

// ---- the Longhorn look ------------------------------------------------------------
// The pre-reset Longhorn Save File concept (image9.jpeg) and the 4074 Open
// dialog (lh4074-180309.png), unified as Codex described them
// (NOTES "The Longhorn file dialog"): top to bottom the explorer's header
// (Back/Forward, title, breadcrumb, search) on lhwm's glass or the theme's
// navigation bar; the 4074 command bar (Up a Folder, New Folder, Change View)
// on the theme's per-folder Rebar strip; the browser: the places pane in the
// groups look (Common Places, Computer, Bookmarks, the Filter box) and the
// list (Icons grouped by type by default); then for Save the concept's blue
// details band (the file's icon, Name, "Will be saved in:", File type) and a
// strip with Hide Explorer and Save/Cancel, for Open and Select Folder 4074's
// compact rows (File name / Files of type) with Open and Cancel at the right.

enum {
    LH_MARGIN = 10, LH_ROW = 22, LH_ROWGAP = 8, LH_BTN_W = 84, LH_BTN_H = 23, LH_LABEL_W = 88,
    LH_BAND_H = 98, LH_STRIP_H = 46, LH_SPLIT = 4,
    LH_REDRAW_TIMER = 0x7f1,
};

static void lh_layout(Dlg& d);

// the session theme's flxp.ini knobs of the Longhorn look
static std::string fd_setting(const char* key, const char* fallback) { return shell::setting(key, fallback); }

bool longhorn_look(const Options& o)
{
    if (o.style == Options::Style::XP) return false;
    if (o.style == Options::Style::Longhorn) return true;
    std::string st = shell::setting("filedlg", "auto");   // FLXP_FILEDLG
    if (st == "xp") return false;
    if (st == "longhorn") return true;
    return browser::want_longhorn();
}

// filedlg_view: the view as the views file names it (browser::views: "icons@48", "tiles@64",
// "details@16", "icons@96+nonames"; the earlier "thumbnails@96", "list+thumbnails" are read too)
static int view_from_name(const std::string& name)
{
    int v = browser::views::parse(name);
    return v >= 0 ? v : (int)LVS_ICON;
}

// the list's folder for the details band and the places pane
static std::string folder_label(Dlg& d)
{
    if (!d.folder) return "";
    return d.folder->is_filesystem() ? d.folder->path() : d.folder->name();
}

static int lh_bottom_h(Dlg& d)
{
    if (d.o->mode == Mode::Save) return LH_BAND_H + LH_STRIP_H;
    int rows = d.o->mode == Mode::Folder ? 1 : 2;
    int h = LH_MARGIN + rows * LH_ROW + (rows - 1) * LH_ROWGAP + LH_MARGIN;
    if (d.o->show_read_only && d.o->mode == Mode::Open) h += 20;
    return h;
}

static void lh_folder_shown(Dlg& d)
{
    if (d.header) { header::set_history(!d.back.empty(), !d.forward.empty()); header::set_folder(d.folder); }
    places::folder_changed();
    browser::set_window_icon(d.dlg, d.folder ? d.folder->icon() : "folder");
    bool writable = d.folder && d.folder->is_filesystem() && access(d.folder->path().c_str(), W_OK) == 0;
    if (d.bar_tb) {
        SendMessageW(d.bar_tb, TB_ENABLEBUTTON, ID_UP, MAKELONG(d.folder && d.folder->parent() != nullptr, 0));
        SendMessageW(d.bar_tb, TB_ENABLEBUTTON, ID_NEWFOLDER, MAKELONG(writable, 0));
    }
    header::enable_tool(ID_NEWFOLDER, writable);
    if (d.o->mode == Mode::Save) {   // "Will be saved in:"
        RECT rc; GetClientRect(d.dlg, &rc);
        RECT band = { 0, rc.bottom - lh_bottom_h(d), rc.right, rc.bottom - LH_STRIP_H };
        InvalidateRect(d.dlg, &band, TRUE);
    }
}

static void lh_view_changed(Dlg& d)
{
    if (d.ready) shell::write_setting("filedlg_view", browser::views::name_of(browser::view_code(d)));
}

// ---- header and places callbacks (the innermost dialog: g_cur) ----

static void lh_navigate(const std::string& path)
{
    Dlg* d = g_cur;
    if (!d) return;
    if (d->folder && d->folder->path() == path) { fill_list(*d); return; }
    if (!navigate(*d, path, true)) MessageBeep(0);
}

static void lh_filter(const std::string& text) { if (g_cur) browser::set_filter(*g_cur, text); }
static void lh_done(bool) { if (g_cur) SetFocus(g_cur->list); }
static void lh_filter_done() { if (g_cur) SetFocus(g_cur->list); }
static void lh_command(int id)
{
    if (!g_cur) return;
    if (id == ::ID_BACK) go_back(*g_cur);
    else if (id == ::ID_FORWARD) go_forward(*g_cur);
}
static void lh_history(bool back, int x, int y) { if (g_cur) browser::history_menu(*g_cur, g_cur->dlg, back, x, y); }
static void lh_view_menu(Dlg& d, int x, int y);
// the header's glyph buttons (New Folder, Views)
static void lh_tool(int id, int x, int y)
{
    if (!g_cur) return;
    if (id == ID_VIEWMENU) lh_view_menu(*g_cur, x, y);
    else SendMessageW(g_cur->dlg, WM_COMMAND, (WPARAM)id, 0);
}

// the navigation bar's star: the bookmarks
static void lh_favorites(int x, int y)
{
    Dlg* d = g_cur;
    if (!d) return;
    std::vector<shell::Bookmark> list = shell::load_bookmarks();
    HMENU m = CreatePopupMenu();
    int n = 0;
    for (size_t i = 0; i < list.size() && i < 60; i++) {
        if (list[i].path().empty()) continue;
        AppendMenuW(m, MF_STRING, 7000 + (UINT)i, WS(list[i].display_name()));
        n++;
    }
    if (!n) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"(No bookmarks)");
    int cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, x, y, 0, d->dlg, nullptr);
    DestroyMenu(m);
    if (cmd >= 7000 && (size_t)(cmd - 7000) < list.size()) lh_navigate(list[(size_t)(cmd - 7000)].path());
}

static std::string lh_current_dir() { return g_cur ? cur_dir(*g_cur) : ""; }
static std::string lh_current_path() { return g_cur && g_cur->folder ? g_cur->folder->path() : ""; }

// ---- the dialog as the verbs' host (drops on the places pane, drags) ----

struct DlgHost : verbs::Host {
    Dlg* d = nullptr;
    HWND window() override { return d->dlg; }
    std::string current_dir() override { return cur_dir(*d); }
    Item folder_item() override { Item it; if (d->folder) { it.path = d->folder->path(); it.name = d->folder->name(); it.flags = shell::IsFolder; } return it; }
    void open_folder(const Item& it, bool) override { navigate(*d, it.path, true); }
    void changed(const std::vector<std::string>& dirs, const std::string& select, bool) override
    {
        std::string cur = cur_dir(*d);
        for (const std::string& p : dirs) if (p == cur) { fill_list(*d, select); return; }
    }
    void begin_rename() override {}
};

// ---- the command bar: 4074's Up a Folder, New Folder, Change View ----

static LRESULT CALLBACK bar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(h, &rc);
        HDC dc = (HDC)wp;
        if (lhbar::enabled()) lhbar::paint_background(dc, rc);
        else {
            HTHEME th = OpenThemeData(h, L"Rebar");
            if (th) { DrawThemeBackground(th, dc, 0, 0, &rc, nullptr); CloseThemeData(th); }
            else FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        }
        return 1;
    }
    case WM_NOTIFY:
    case WM_COMMAND:
        return SendMessageW(GetParent(h), msg, wp, lp);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void lh_create_bar(Dlg& d)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = bar_proc;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxp_fdbar";
    RegisterClassW(&wc);
    d.bar = CreateWindowExW(WS_EX_CONTROLPARENT, L"flxp_fdbar", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, d.dlg, (HMENU)(INT_PTR)ID_BAR, nullptr, nullptr);
    d.bar_tb = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0, d.bar, (HMENU)(INT_PTR)ID_BARTB, nullptr, nullptr);
    if (lhbar::enabled()) SetWindowTheme(d.bar_tb, L"", L"");   // custom drawn on the bar (lhbar)
    SendMessageW(d.bar_tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(d.bar_tb, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS);
    SendMessageW(d.bar_tb, WM_SETFONT, SendMessageW(d.dlg, WM_GETFONT, 0, 0), FALSE);
    int view0 = 0, hist0 = 12;
    load_toolbar_images(d.bar_tb, &view0, &hist0);
    TBBUTTON b[] = {
        { view0 + VIEW_PARENTFOLDER, ID_UP, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Up a Folder" },
        { view0 + VIEW_NEWFOLDER, ID_NEWFOLDER, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"New Folder" },
        { view0 + VIEW_LIST, ID_VIEWMENU, TBSTATE_ENABLED, BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE | BTNS_SHOWTEXT, {0}, 0, (INT_PTR)L"Change View" },
    };
    SendMessageW(d.bar_tb, TB_ADDBUTTONSW, 3, (LPARAM)b);
    SendMessageW(d.bar_tb, TB_AUTOSIZE, 0, 0);
    RECT r = {};
    SendMessageW(d.bar_tb, TB_GETITEMRECT, 0, (LPARAM)&r);
    d.bar_h = std::max(26, (int)(r.bottom - r.top) + 6);   // 4074: 31 rows with its larger glyphs
}

// the glyph buttons in the header while the strip is off
static void lh_sync_tools(Dlg& d)
{
    if (!d.header) return;
    if (d.show_bar) { header::set_tools({}); return; }
    // the Longhorn shell32's toolbar strip (as the explorer's 4051 bar: New
    // Folder 31, Views 32 of XP's tb_sh_def_16 layout), else the strip's images
    static HIMAGELIST shell = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string sys = shell::longhorn_resources();
        if (!sys.empty()) shell = strip_list("pe:" + sys + "/shell32.dll#216", 16);
        if (shell && ImageList_GetImageCount(shell) < 48) { ImageList_Destroy(shell); shell = nullptr; }
    }
    HIMAGELIST il = (HIMAGELIST)SendMessageW(d.bar_tb, TB_GETIMAGELIST, 0, 0);
    TBBUTTONINFOW bi = {}; bi.cbSize = sizeof bi; bi.dwMask = TBIF_IMAGE;
    std::vector<header::Tool> tools;
    for (int id : { (int)ID_NEWFOLDER, (int)ID_VIEWMENU }) {
        HICON ic = nullptr;
        if (shell) ic = ImageList_GetIcon(shell, id == ID_NEWFOLDER ? 31 : 32, ILD_NORMAL);
        else {
            SendMessageW(d.bar_tb, TB_GETBUTTONINFOW, (WPARAM)id, (LPARAM)&bi);
            ic = il ? ImageList_GetIcon(il, bi.iImage, ILD_NORMAL) : nullptr;
        }
        tools.push_back({ id, ic, id == ID_VIEWMENU });
    }
    header::set_tools(tools);
    for (const header::Tool& t : tools) if (t.icon) DestroyIcon(t.icon);
}

// ---- painting: the details band and the strip (Save) ----

static void fill_rect(HDC dc, const RECT& r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static COLORREF mix(COLORREF a, COLORREF b, int num, int den)
{
    if (den <= 0) return a;
    return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * num / den,
               GetGValue(a) + (GetGValue(b) - GetGValue(a)) * num / den,
               GetBValue(a) + (GetBValue(b) - GetBValue(a)) * num / den);
}

// the concept's band (image9.jpeg, rows 472..563): light at the top, darkest
// about a third down, lighter again towards the bottom
static const COLORREF BAND_TOP = RGB(228, 237, 252), BAND_MID = RGB(199, 214, 236), BAND_BOTTOM = RGB(212, 226, 243),
                      BAND_LINE = RGB(173, 192, 220), STRIP = RGB(244, 244, 244), LABEL = RGB(93, 110, 138),
                      LIST_BK = RGB(244, 244, 244);

static RECT lh_band_rect(Dlg& d)
{
    RECT rc; GetClientRect(d.dlg, &rc);
    int top = d.show_explorer ? rc.bottom - lh_bottom_h(d) : (d.header ? header::height() : 0);
    return RECT{ 0, top, rc.right, top + LH_BAND_H };
}

static void lh_erase(Dlg& d, HDC dc)
{
    RECT rc; GetClientRect(d.dlg, &rc);
    int top = d.header ? header::height() : 0;
    RECT all = { 0, top, rc.right, rc.bottom };
    if (d.o->mode != Mode::Save) {
        FillRect(dc, &all, GetSysColorBrush(COLOR_BTNFACE));
        if (d.show_explorer) {   // the splitter's gap: the list's background
            RECT body = { d.pane_w, top + (d.show_bar ? d.bar_h : 0), d.pane_w + LH_SPLIT, rc.bottom - lh_bottom_h(d) };
            fill_rect(dc, body, LIST_BK);
        }
        return;
    }
    fill_rect(dc, all, STRIP);
    if (d.show_explorer) {
        RECT body = { d.pane_w, top + (d.show_bar ? d.bar_h : 0), d.pane_w + LH_SPLIT, rc.bottom - lh_bottom_h(d) };
        fill_rect(dc, body, LIST_BK);
    }
    RECT band = lh_band_rect(d);
    int h = band.bottom - band.top;
    int knee = h / 3;
    for (int y = 0; y < h; y++) {
        RECT row = { 0, band.top + y, rc.right, band.top + y + 1 };
        fill_rect(dc, row, y < knee ? mix(BAND_TOP, BAND_MID, y, knee) : mix(BAND_MID, BAND_BOTTOM, y - knee, h - 1 - knee));
    }
    RECT line = { 0, band.top, rc.right, band.top + 1 };
    fill_rect(dc, line, BAND_LINE);
    RECT under = { 0, band.bottom - 1, rc.right, band.bottom };
    fill_rect(dc, under, mix(BAND_BOTTOM, BAND_LINE, 1, 2));
}

// the band's own drawing: the icon of the file being saved, "Will be saved in:" and the folder
static void lh_paint(Dlg& d, HDC dc)
{
    if (d.o->mode != Mode::Save) return;
    RECT band = lh_band_rect(d);
    std::string name = trim(text_of(d.name_edit));
    std::string key = name.empty() ? "document" : shell::icon_key(shell::base_of(name), false);
    int img = icon_for(d.icons, key);
    ImageList_Draw(d.icons.large, img, dc, 24, band.top + 16, ILD_TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, (HFONT)SendMessageW(d.dlg, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    int fx = 72 + LH_LABEL_W + 6;
    RECT lr = { 72, band.top + 12 + LH_ROW + 6, 72 + LH_LABEL_W, band.top + 12 + LH_ROW + 6 + 16 };
    SetTextColor(dc, LABEL);
    DrawTextW(dc, L"Will be saved in:", -1, &lr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT pr = { fx + 3, lr.top, band.right - 20, lr.bottom };
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    std::u16string where = W(folder_label(d));
    DrawTextW(dc, (LPCWSTR)where.c_str(), -1, &pr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_PATH_ELLIPSIS);
    SelectObject(dc, of);
}

// Hide Explorer / Browse Folders: a small box with a triangle (up: the
// browser folds away) and the text, as the concept's
static void lh_draw_hide(Dlg& d, DRAWITEMSTRUCT* di)
{
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    fill_rect(dc, r, STRIP);
    int by = r.top + (r.bottom - r.top - 13) / 2;
    RECT box = { r.left + 2, by, r.left + 15, by + 13 };
    HBRUSH frame = CreateSolidBrush(RGB(120, 136, 160));
    FrameRect(dc, &box, frame);
    DeleteObject(frame);
    RECT in = { box.left + 1, box.top + 1, box.right - 1, box.bottom - 1 };
    fill_rect(dc, in, (di->itemState & ODS_SELECTED) ? RGB(214, 226, 244) : RGB(250, 251, 253));
    POINT tri[3];
    int cx = box.left + 6, cy = box.top + 6;
    bool up = d.show_explorer;
    tri[0] = { cx - 3, up ? cy + 2 : cy - 1 };
    tri[1] = { cx + 4, tri[0].y };
    tri[2] = { cx, up ? cy - 2 : cy + 3 };
    HBRUSH b = CreateSolidBrush(RGB(60, 80, 110));
    HPEN p = CreatePen(PS_SOLID, 1, RGB(60, 80, 110));
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Polygon(dc, tri, 3);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
    WCHAR text[64] = {};
    GetWindowTextW(di->hwndItem, text, 63);
    HGDIOBJ of = SelectObject(dc, (HFONT)SendMessageW(d.dlg, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    RECT tr = { box.right + 6, r.top, r.right, r.bottom };
    UINT flags = DT_SINGLELINE | DT_VCENTER | ((di->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
    DrawTextW(dc, text, -1, &tr, flags);
    if (di->itemState & ODS_FOCUS) {
        RECT f = tr;
        DrawTextW(dc, text, -1, &f, flags | DT_CALCRECT);
        f.top = tr.top + 2; f.bottom = tr.bottom - 2; InflateRect(&f, 2, 0);
        DrawFocusRect(dc, &f);
    }
    SelectObject(dc, of);
}

// ---- layout ----

static void place(HWND h, int x, int y, int w, int hh) { if (h) SetWindowPos(h, nullptr, x, y, std::max(0, w), std::max(0, hh), SWP_NOZORDER | SWP_NOACTIVATE); }

static void lh_layout(Dlg& d)
{
    RECT rc; GetClientRect(d.dlg, &rc);
    int W_ = rc.right, H_ = rc.bottom;
    int top = 0;
    if (d.header) { header::relayout(W_, header::height()); top = header::height(); }
    bool br = d.show_explorer;
    int bottom_h = lh_bottom_h(d);
    if (d.bar) {
        ShowWindow(d.bar, br && d.show_bar ? SW_SHOWNA : SW_HIDE);
        if (br && d.show_bar) {
            place(d.bar, 0, top, W_, d.bar_h);
            SIZE sz = {};
            SendMessageW(d.bar_tb, TB_GETMAXSIZE, 0, (LPARAM)&sz);
            RECT r = {};
            SendMessageW(d.bar_tb, TB_GETITEMRECT, 0, (LPARAM)&r);
            int bh = r.bottom - r.top;
            place(d.bar_tb, 6, (d.bar_h - bh) / 2, std::max((int)sz.cx, W_ - 12), bh);
            top += d.bar_h;
        }
    }
    int body_bottom = H_ - bottom_h;
    if (d.pane) {
        int pane_bottom = d.viewstrip ? body_bottom - viewbutton::STRIP_H : body_bottom;
        ShowWindow(d.pane, br ? SW_SHOWNA : SW_HIDE);
        place(d.pane, 0, top, d.pane_w, pane_bottom - top);
        if (d.viewstrip) {
            ShowWindow(d.viewstrip, br ? SW_SHOWNA : SW_HIDE);
            place(d.viewstrip, 0, pane_bottom, d.pane_w, viewbutton::STRIP_H);
        }
    }
    ShowWindow(d.list, br ? SW_SHOWNA : SW_HIDE);
    int lx = d.pane ? d.pane_w + LH_SPLIT : 0;
    place(d.list, lx, top, W_ - lx, body_bottom - top);

    HWND lname = GetDlgItem(d.dlg, stc3), ltype = GetDlgItem(d.dlg, stc2), ro = GetDlgItem(d.dlg, chx1);
    HWND ok = GetDlgItem(d.dlg, IDOK), cancel = GetDlgItem(d.dlg, IDCANCEL);
    if (d.o->mode == Mode::Save) {
        RECT band = lh_band_rect(d);
        int lx0 = 72, fx = lx0 + LH_LABEL_W + 6, fw = W_ - fx - 20;
        int y1 = band.top + 12, y3 = band.top + 12 + LH_ROW + 6 + 16 + 6;
        place(lname, lx0, y1 + 3, LH_LABEL_W, LH_ROW - 4);
        place(d.name_cbx, fx, y1, fw, LH_ROW);
        place(ltype, lx0, y3 + 3, LH_LABEL_W, LH_ROW - 4);
        place(d.types, fx, y3, std::min(fw, 320), 200);
        int sy = br ? H_ - LH_STRIP_H : band.bottom;
        int by = sy + (LH_STRIP_H - LH_BTN_H) / 2;
        place(cancel, W_ - 12 - LH_BTN_W, by, LH_BTN_W, LH_BTN_H);
        place(ok, W_ - 12 - 2 * LH_BTN_W - 8, by, LH_BTN_W, LH_BTN_H);
        place(d.hide, 12, by, 130, LH_BTN_H);
        InvalidateRect(d.dlg, nullptr, TRUE);
    } else {
        int y = H_ - bottom_h + LH_MARGIN;
        int bx = W_ - LH_MARGIN - 2 * LH_BTN_W - 8;
        int fx = LH_MARGIN + LH_LABEL_W, fw = bx - 12 - fx;
        place(lname, LH_MARGIN, y + 4, LH_LABEL_W - 4, LH_ROW - 6);
        place(d.name_cbx, fx, y, fw, LH_ROW);
        place(ok, bx, y - 1, LH_BTN_W, LH_BTN_H);
        place(cancel, bx + LH_BTN_W + 8, y - 1, LH_BTN_W, LH_BTN_H);
        if (d.o->mode == Mode::Open) {
            int y2 = y + LH_ROW + LH_ROWGAP;
            place(ltype, LH_MARGIN, y2 + 4, LH_LABEL_W - 4, LH_ROW - 6);
            place(d.types, fx, y2, fw, 200);
            if (d.o->show_read_only) place(ro, fx, y2 + LH_ROW + 6, 200, 16);
        }
    }
    if (d.view != LVS_REPORT) SendMessageW(d.list, LVM_ARRANGE, LVA_DEFAULT, 0);
}

static void lh_layout_cur() { if (g_cur) lh_layout(*g_cur); }

// Hide Explorer: only the header, the details band and the strip stay; the
// height the browser had comes back with Browse Folders (remembered:
// filedlg_explorer in flxp.ini)
static void lh_set_explorer(Dlg& d, bool show, bool resize)
{
    d.show_explorer = show;
    SetWindowTextW(d.hide, show ? L"&Hide Explorer" : L"&Browse Folders");
    RECT wr; GetWindowRect(d.dlg, &wr);
    RECT cr; GetClientRect(d.dlg, &cr);
    int frame_h = (wr.bottom - wr.top) - cr.bottom;
    int collapsed = (d.header ? header::height() : 0) + LH_BAND_H + LH_STRIP_H + frame_h;
    sys::NativeWindow* nw = sys::window_for_user((void*)d.dlg);
    if (nw) sys::set_min_size(nw, 480, show ? collapsed + 200 : collapsed);   // before the resize: the WM keeps the old minimum
    if (resize) {
        if (!show) {
            d.full_h = wr.bottom - wr.top;
            SetWindowPos(d.dlg, nullptr, 0, 0, wr.right - wr.left, collapsed, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            int h = d.full_h > collapsed + 100 ? d.full_h : collapsed + 360;
            SetWindowPos(d.dlg, nullptr, 0, 0, wr.right - wr.left, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    lh_layout(d);
    InvalidateRect(d.dlg, nullptr, TRUE);
    if (d.hide) InvalidateRect(d.hide, nullptr, TRUE);
}

// ---- menus ----

static void lh_view_menu(Dlg& d, int x, int y)
{
    HMENU m = CreatePopupMenu();
    int mv = browser::menu_view(d);
    // the views (4074's), Hide File Names, Show Thumbnails, groups
    (void)mv;
    lh_view_items(d, m);
    AppendMenuW(m, MF_STRING | (d.grouped ? MF_CHECKED : 0) | (browser::group_view_applies(d) ? 0 : MF_GRAYED), ID_GROUPS, L"Show in &Groups");
    HMENU arrange = CreatePopupMenu();
    AppendMenuW(arrange, MF_STRING, ID_SORT_NAME, L"&Name");
    AppendMenuW(arrange, MF_STRING, ID_SORT_SIZE, L"&Size");
    AppendMenuW(arrange, MF_STRING, ID_SORT_TYPE, L"&Type");
    AppendMenuW(arrange, MF_STRING, ID_SORT_DATE, L"&Modified");
    CheckMenuRadioItem(arrange, ID_SORT_NAME, ID_SORT_DATE, ID_SORT_NAME + (int)d.sort, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)arrange, L"Arrange &Icons By");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (d.show_bar ? MF_CHECKED : 0), ID_CMDBAR, L"&Command Bar");
    HMENU zoom = CreatePopupMenu();
    browser::zoom_menu(d, zoom, ID_ZOOM_FIRST);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)zoom, L"&Zoom");
    AppendMenuW(m, MF_STRING | (places::icons() ? MF_CHECKED : 0), ID_PANE_ICONS, L"I&cons in the Pane");
    if (d.header) {   // the header's elements and 4074's Toolbar Size (explorer: the same under View > Toolbars)
        HMENU bars = CreatePopupMenu();
        header::append_menu(bars, ID_HEADER_FIRST);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)bars, L"&Toolbars");
    }
    AppendMenuW(m, MF_STRING | (d.show_hidden ? MF_CHECKED : 0), ID_HIDDEN, L"Show &Hidden Files\tCtrl+H");
    AppendMenuW(m, MF_STRING, ID_REFRESH, L"R&efresh\tF5");
    if (d.bar_tb) SendMessageW(d.bar_tb, TB_PRESSBUTTON, ID_VIEWMENU, MAKELONG(TRUE, 0));
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, d.dlg, nullptr);
    if (d.bar_tb) SendMessageW(d.bar_tb, TB_PRESSBUTTON, ID_VIEWMENU, MAKELONG(FALSE, 0));
    DestroyMenu(m);
    if (cmd) SendMessageW(d.dlg, WM_COMMAND, (WPARAM)cmd, 0);
}

// ---- init ----

static void lh_init(Dlg& d, HWND owner)
{
    const Options& o = *d.o;
    HWND dlg = d.dlg;
    d.show_hidden = o.show_hidden;
    uiicons::init();
    lhbar::init();

    // texts
    std::string title = o.title;
    if (title.empty()) title = o.mode == Mode::Save ? "Save File" : o.mode == Mode::Folder ? "Select Folder" : "Open";
    SetWindowTextW(dlg, WS(title));
    std::string accept = o.accept_label;
    if (accept.empty()) accept = o.mode == Mode::Save ? "&Save" : o.mode == Mode::Folder ? "&Select" : "&Open";
    else if (accept.find('&') == std::string::npos && accept.find('_') != std::string::npos) {
        for (char& c : accept) if (c == '_') { c = '&'; break; }
    }
    SetDlgItemTextW(dlg, IDOK, WS(accept));
    d.accept = accept;
    if (o.mode == Mode::Save) {
        SetDlgItemTextW(dlg, stc3, L"&Name:");
        SetDlgItemTextW(dlg, stc2, L"File &type:");
        for (int id : { stc3, stc2 }) {
            HWND l = GetDlgItem(dlg, id);
            SetWindowLongW(l, GWL_STYLE, (GetWindowLongW(l, GWL_STYLE) & ~SS_TYPEMASK) | SS_RIGHT);
        }
    }
    if (o.mode == Mode::Folder) {
        SetDlgItemTextW(dlg, stc3, L"Folder &name:");
        ShowWindow(GetDlgItem(dlg, stc2), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, cmb1), SW_HIDE);
    }
    if (!(o.show_read_only && o.mode == Mode::Open)) ShowWindow(GetDlgItem(dlg, chx1), SW_HIDE);
    else CheckDlgButton(dlg, chx1, o.read_only ? BST_CHECKED : BST_UNCHECKED);
    d.hide = GetDlgItem(dlg, IDC_FD_HIDE);
    if (o.mode != Mode::Save) { ShowWindow(d.hide, SW_HIDE); d.hide = nullptr; }

    d.types = GetDlgItem(dlg, cmb1);
    d.name_cbx = GetDlgItem(dlg, cmb13);
    d.name_edit = (HWND)SendMessageW(d.name_cbx, CBEM_GETEDITCONTROL, 0, 0);
    if (!d.name_edit) d.name_edit = GetDlgItem(dlg, edt1);
    SetPropW(d.name_edit, L"flxp_filedlg", (HANDLE)&d);
    d.edit_proc = (WNDPROC)SetWindowLongPtrW(d.name_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass);

    // filters
    d.type_filter = o.filters.empty() ? -1 : std::max(0, std::min(o.filter_index, (int)o.filters.size() - 1));
    if (o.filters.empty()) SendMessageW(d.types, CB_ADDSTRING, 0, (LPARAM)L"All Files");
    for (const Filter& f : o.filters) SendMessageW(d.types, CB_ADDSTRING, 0, (LPARAM)WS(f.name));
    SendMessageW(d.types, CB_SETCURSEL, (WPARAM)std::max(d.type_filter, 0), 0);

    // the header on lhwm's glass (or the navigation bar)
    header::Callbacks hcb = { lh_navigate, lh_filter, lh_done, lh_command, lh_history, lh_favorites, lh_tool };
    header::configure("filedlg_", "small");      // compact (the portal's dialog): 4074's Small unless set
    d.header = header::create(dlg, hcb);
    header::set_title(title);
    SetWindowLongPtrW(d.header, GWL_EXSTYLE, GetWindowLongPtrW(d.header, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);   // Tab reaches the search box
    browser::extend_frame(dlg);

    // New Folder and Views: glyph buttons in the header (4051's command row has
    // glyph buttons; 4074's text strip is filedlg_toolbar=bar, View > Command Bar)
    d.show_bar = fd_setting("filedlg_toolbar", "header") == "bar";
    lh_create_bar(d);
    lh_sync_tools(d);

    // the places pane: Common Places, Computer, Bookmarks
    places::Options po;
    po.groups = true;
    po.new_window = false;
    places::GroupSpec common;
    common.title = "Common Places";
    common.places.push_back({ "Recent Places", "::recentplaces", "recent" });
    common.places.push_back({ "Recent Documents", "::recent", "recent" });
    common.places.push_back({ "Desktop", "::desktop", "desktop" });
    common.places.push_back({ "Home", shell::home_dir(), "home" });
    std::string docs = places::user_dir("XDG_DOCUMENTS_DIR", "Documents"), pics = places::user_dir("XDG_PICTURES_DIR", "Pictures");
    if (!docs.empty()) common.places.push_back({ "Documents", docs, "folder" });
    if (!pics.empty()) common.places.push_back({ "Pictures", pics, "folder" });
    std::string app = o.app_location.empty() ? "" : normalize(expand_home(o.app_location));
    if (!app.empty() && is_dir(app) && app != shell::home_dir() && app != docs && app != pics) common.places.push_back({ "App Location", app, "folder" });
    places::GroupSpec bm;
    bm.kind = places::GroupSpec::Bookmarks;
    bm.title = "Bookmarks";
    po.spec = { common, places::computer_group(), bm };
    po.remember = "filedlg_collapsed";
    places::Callbacks pcb = { lh_navigate, lh_current_dir, lh_filter, lh_filter_done, nullptr, lh_current_path };
    d.pane = places::create(dlg, ID_PANE, pcb, po);
    SetWindowLongPtrW(d.pane, GWL_EXSTYLE, GetWindowLongPtrW(d.pane, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
    ShowWindow(d.pane, SW_SHOWNA);
    d.viewstrip = viewbutton::create(dlg, d);
    d.pane_w = std::max(100, std::min(400, atoi(fd_setting("filedlg_pane_width", "155").c_str())));

    // the list: Icons grouped by type unless the user chose otherwise (filedlg_view, filedlg_groups)
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_ICON | LVS_SHAREIMAGELISTS | LVS_SHOWSELALWAYS | LVS_EDITLABELS | LVS_AUTOARRANGE;
    if (!(o.multiple && o.mode == Mode::Open)) style |= LVS_SINGLESEL;
    static const int widths[4] = { 200, 80, 140, 130 };
    browser::create_list(d, dlg, ID_LIST, style, 0, widths, (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0));
    SetPropW(d.list, L"flxp_concept_groups", (HANDLE)1);   // the concept's group headers, not the theme's (listview.c)
    SendMessageW(d.list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_LABELTIP, LVS_EX_LABELTIP);
    // the concept's warm near-white behind the files (thumbnails are composed on it too)
    SendMessageW(d.list, LVM_SETBKCOLOR, 0, (LPARAM)LIST_BK);
    SendMessageW(d.list, LVM_SETTEXTBKCOLOR, 0, (LPARAM)CLR_NONE);
    thumbview::init(d.list, dlg);
    d.grouped = fd_setting("filedlg_groups", "1") != "0";
    d.zoomable = true;
    {
        std::string folded = fd_setting("filedlg_folded_types", "");   // the list's groups folded last time
        size_t i = 0;
        while (i < folded.size()) {
            size_t j = folded.find(';', i);
            if (j == std::string::npos) j = folded.size();
            if (j > i) d.collapsed[folded.substr(i, j - i)] = true;
            i = j + 1;
        }
    }
    int v = view_from_name(fd_setting("filedlg_view", "icons"));
    if (const char* e = getenv("FLXP_FILEDLG_VIEW")) v = view_from_name(e);
    browser::set_view(d, v, true);

    // size, before the list is filled (it scrolls to the focused item):
    // remembered (filedlg_size, the window's), else the concept's proportions
    int w = 720, h = o.mode == Mode::Save ? 600 : 560;
    sscanf(fd_setting("filedlg_size", "").c_str(), "%dx%d", &w, &h);
    w = std::max(480, std::min(w, (int)sys::screen_size().cx));
    h = std::max(360, std::min(h, (int)sys::screen_size().cy));
    SetWindowPos(dlg, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    d.full_h = h;
    lh_layout(d);

    // starting folder and name
    std::string name = o.default_name, start = o.initial_dir;
    if (!name.empty() && name.find('/') != std::string::npos) {
        std::string full = resolve_name(d, name);
        if (start.empty() && is_dir(shell::dir_of(full))) start = shell::dir_of(full);
        name = shell::base_of(full);
    }
    if (start.empty()) start = shell::home_dir();
    start = expand_home(start);
    if (!navigate(d, start, false) && !navigate(d, shell::home_dir(), false)) navigate(d, "::desktop", false);
    set_name(d, name);

    // tab order: the list, the bottom controls, the pane (Filter box), the header (search)
    order_children({ d.list, GetDlgItem(dlg, stc3), GetDlgItem(dlg, edt1), d.name_cbx, GetDlgItem(dlg, stc2), d.types,
                     GetDlgItem(dlg, chx1), GetDlgItem(dlg, IDOK), GetDlgItem(dlg, IDCANCEL), d.hide, d.pane, d.bar, d.header });

    if (o.mode == Mode::Save && fd_setting("filedlg_explorer", "1") == "0") lh_set_explorer(d, false, true);

    place_dialog(d, owner);
    sys::NativeWindow* nw = sys::window_for_user((void*)dlg);
    if (nw) {
        RECT wr; GetWindowRect(dlg, &wr);
        RECT cr; GetClientRect(dlg, &cr);
        int frame_h = (wr.bottom - wr.top) - cr.bottom;
        int collapsed = (d.header ? header::height() : 0) + LH_BAND_H + LH_STRIP_H + frame_h;
        sys::set_min_size(nw, 480, d.show_explorer ? std::max(360, collapsed + 200) : collapsed);
    }
    d.w_active = FlxpWatchRootProperty("_NET_ACTIVE_WINDOW");
    header::set_active(browser::frame_active(dlg, true));
    SetFocus(d.name_edit);
    SendMessageW(d.name_edit, EM_SETSEL, 0, -1);
    d.ready = true;
    // once shown: everything again. The window becomes ARGB (lhwm's glass) after
    // the first paints, which leave the glow text on black until a full repaint;
    // at the Medium size CLIENT_CAPTION's resize did that, at Small nothing does
    SetTimer(dlg, LH_REDRAW_TIMER, 150, nullptr);
}

// remembered when the dialog closes: the size (with the browser shown), the pane's width
static void lh_save_state(Dlg& d)
{
    RECT wr; GetWindowRect(d.dlg, &wr);
    int h = d.show_explorer ? wr.bottom - wr.top : d.full_h;
    // lhwm's CLIENT_CAPTION grew the client by its caption (less the top border
    // it keeps): the size asked for next time is the one without it
    DWORD flags = 0;
    DwmGetWindowAttribute(d.dlg, DWMWA_FLXP_FRAME_FLAGS, &flags, sizeof flags);
    FLXP_DWM_FRAME_METRICS m = {}; m.cbSize = sizeof m;
    if (d.show_explorer && (flags & FLXP_DWMFF_CLIENT_CAPTION) && FlxpDwmGetFrameMetrics(&m) == S_OK && m.bValid)
        h -= std::max(0, (int)m.cyCaption - (m.cxLeftBorder > 0 ? (int)m.cxLeftBorder : 9));
    char buf[64];
    snprintf(buf, sizeof buf, "%dx%d", (int)(wr.right - wr.left), h);
    shell::write_setting("filedlg_size", buf);
    shell::write_setting("filedlg_pane_width", std::to_string(d.pane_w));
    // the list's folded groups (by type), for the next dialog
    for (size_t i = 0; i < d.group_titles.size(); i++)
        d.collapsed[d.group_titles[i]] = (SendMessageW(d.list, LVM_GETGROUPSTATE, (WPARAM)i, LVGS_COLLAPSED) & LVGS_COLLAPSED) != 0;
    std::string folded;
    for (const auto& kv : d.collapsed) if (kv.second && kv.first.find(';') == std::string::npos) folded += (folded.empty() ? "" : ";") + kv.first;
    shell::write_setting("filedlg_folded_types", folded);
}

// frame messages of the Longhorn look (glass, theme, activation); true when handled
static bool lh_frame_message(Dlg& d, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
    HWND h = d.dlg;
    *result = 0;
    switch (msg) {
    case WM_NCHITTEST:
        return browser::header_hittest(h, lp, result);
    case WM_DWMCOMPOSITIONCHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        if (d.header) browser::header_refit(h, lh_layout_cur);
        header::set_active(browser::frame_active(h, d.focus_active));
        return true;
    case WM_FLXP_DWMCAPTIONRECTCHANGED:
        browser::caption_hole(h);
        return true;
    case WM_FLXP_DWMSTATECHANGED:
        header::state_changed();
        return true;
    case WM_FLXP_DWMHEADERCHANGED:
        header::pixmap_changed();
        return true;
    case WM_FLXP_ROOTPROPERTY:
        if ((UINT)wp == d.w_active) header::set_active(browser::frame_active(h, d.focus_active));
        return true;
    case WM_ACTIVATE:
        d.focus_active = LOWORD(wp) != WA_INACTIVE;
        header::set_active(browser::frame_active(h, d.focus_active));
        return false;
    case WM_THEMECHANGED:
        lhbar::theme_changed();
        if (d.header) {
            header::theme_changed();
            browser::header_refit(h, lh_layout_cur);
        }
        RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return false;
    case thumbview::WM_THUMBS:
        thumbview::on_ready();
        return true;
    case WM_TIMER:
        if (wp != LH_REDRAW_TIMER) return false;
        KillTimer(h, LH_REDRAW_TIMER);
        {
            RECT wr; GetWindowRect(h, &wr);
            int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
            SetWindowPos(h, nullptr, 0, 0, ww + 1, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(h, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        return true;
    case WM_ERASEBKGND:
        lh_erase(d, (HDC)wp);
        *result = 1;
        return true;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        lh_paint(d, dc);
        EndPaint(h, &ps);
        return true;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (d.o->mode == Mode::Save) {
            HWND c = (HWND)lp;
            int id = GetDlgCtrlID(c);
            if (id == stc3 || id == stc2) {
                // the band behind the label (the dialog clips its children out of its own erase)
                RECT r; GetWindowRect(c, &r);
                MapWindowPoints(nullptr, h, (POINT*)&r, 2);
                POINT org;
                OffsetViewportOrgEx((HDC)wp, -r.left, -r.top, &org);
                lh_erase(d, (HDC)wp);
                SetViewportOrgEx((HDC)wp, org.x, org.y, nullptr);
                SetBkMode((HDC)wp, TRANSPARENT);
                SetTextColor((HDC)wp, LABEL);
                *result = (LRESULT)GetStockObject(NULL_BRUSH);
                return true;
            }
        }
        return false;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlID == IDC_FD_HIDE) { lh_draw_hide(d, di); *result = TRUE; return true; }
        return false;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (d.show_explorer && pt.x >= d.pane_w && pt.x < d.pane_w + LH_SPLIT) { d.splitting = true; SetCapture(h); }
        return true;
    }
    case WM_MOUSEMOVE:
        if (d.splitting) {
            int x = (SHORT)LOWORD(lp);
            RECT rc; GetClientRect(h, &rc);
            if (x > 100 && x < rc.right - 200) { d.pane_w = x; lh_layout(d); InvalidateRect(h, nullptr, TRUE); }
        }
        return true;
    case WM_LBUTTONUP:
        if (d.splitting) { d.splitting = false; ReleaseCapture(); }
        return true;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && (HWND)wp == h) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            if (d.show_explorer && pt.x >= d.pane_w && pt.x < d.pane_w + LH_SPLIT) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_SIZEWE)); *result = TRUE; return true; }
        }
        return false;
    }
    return false;
}

// ---- notifications ----

// ---- drag and drop (flxp_shell.h): the list is a drop target like the explorer's
// (a folder item takes the files, elsewhere the folder shown) and a drag source

static int g_drop_item = -1;

static void drop_highlight(Dlg& d, int i)
{
    if (i == g_drop_item) return;
    LVITEMW st = {}; st.stateMask = LVIS_DROPHILITED;
    if (g_drop_item >= 0) SendMessageW(d.list, LVM_SETITEMSTATE, (WPARAM)g_drop_item, (LPARAM)&st);
    st.state = LVIS_DROPHILITED;
    if (i >= 0) SendMessageW(d.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
    g_drop_item = i;
}

static LRESULT drop_message(Dlg& d, UINT msg, const FLXPDROP* f)
{
    if (msg == WM_FLXP_DRAGLEAVE) { drop_highlight(d, -1); return 0; }
    if (f->hwndTarget != d.list && !IsChild(d.list, f->hwndTarget)) { drop_highlight(d, -1); return 0; }
    LVHITTESTINFO ht = {};
    ht.pt = { f->x, f->y };
    ScreenToClient(d.list, &ht.pt);
    int i = (int)SendMessageW(d.list, LVM_HITTEST, 0, (LPARAM)&ht), hi = -1;
    std::string dest = cur_dir(d);
    if (i >= 0) {
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = i;
        SendMessageW(d.list, LVM_GETITEMW, 0, (LPARAM)&lvi);
        if (lvi.lParam >= 0 && (size_t)lvi.lParam < d.items.size()) {
            const Item& it = d.items[(size_t)lvi.lParam];
            if (it.is_folder() && is_fs_path(it.path) && is_dir(it.path)) { dest = it.path; hi = i; }
        }
    }
    std::vector<std::string> paths(f->ppszFiles, f->ppszFiles + f->cFiles);
    DWORD e = dest.empty() ? 0 : shell::drop_effect(paths, dest, (f->grfKeyState & MK_SHIFT) != 0, (f->grfKeyState & MK_CONTROL) != 0, f->dwAllowed);
    if (msg == WM_FLXP_DRAGOVER) { drop_highlight(d, e ? hi : -1); return e; }
    drop_highlight(d, -1);
    if (!e) return 0;
    std::string err;
    bool ok = shell::drop_items(paths, dest, e, &err);
    if (!ok && !err.empty()) message_box(d.dlg, "Error Copying File or Folder", "Cannot copy or move the files.\n" + err, MB_OK | MB_ICONEXCLAMATION);
    fill_list(d);
    return ok ? e : 0;
}

static void drag_selection(Dlg& d)
{
    std::vector<std::string> paths;
    std::vector<int> images;
    int i = -1;
    while ((i = (int)SendMessageW(d.list, LVM_GETNEXTITEM, (WPARAM)i, LVNI_SELECTED)) >= 0) {
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM | LVIF_IMAGE; lvi.iItem = i;
        SendMessageW(d.list, LVM_GETITEMW, 0, (LPARAM)&lvi);
        if (lvi.lParam < 0 || (size_t)lvi.lParam >= d.items.size()) continue;
        const Item& it = d.items[(size_t)lvi.lParam];
        if (!is_fs_path(it.path) || (it.flags & shell::IsVirtual)) continue;
        paths.push_back(it.path);
        images.push_back(lvi.iImage);
    }
    if (paths.empty()) return;
    std::vector<const char*> ptrs;
    for (const std::string& p : paths) ptrs.push_back(p.c_str());
    DWORD e = FlxpDragFiles(d.list, ptrs.data(), (UINT)ptrs.size(), DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
                            (HIMAGELIST)SendMessageW(d.list, LVM_GETIMAGELIST, LVSIL_NORMAL, 0), images.data(), (UINT)images.size());
    if (e == DROPEFFECT_MOVE) fill_list(d);
}

static LRESULT on_notify(Dlg& d, NMHDR* nm)
{
    if (nm->hwndFrom == d.places && nm->code == NM_CUSTOMDRAW) return places_customdraw(d, (NMTBCUSTOMDRAW*)nm);
    if (nm->code == TTN_GETDISPINFOW) {
        NMTTDISPINFOW* di = (NMTTDISPINFOW*)nm;
        static std::u16string tip;
        int id = (int)nm->idFrom;
        const char* t = nullptr;
        std::string place;
        switch (id) {
        case ID_BACK: t = "Go To Last Folder Visited"; break;
        case ID_UP: t = "Up One Level"; break;
        case ID_NEWFOLDER: t = "Create New Folder"; break;
        case ID_VIEWMENU: t = "View Menu"; break;
        default:
            if (id >= ID_PLACE_BASE && id < ID_PLACE_BASE + (int)d.place_list.size()) {
                const Place& p = d.place_list[(size_t)(id - ID_PLACE_BASE)];
                place = is_fs_path(p.path) ? p.path : p.name;
                t = place.c_str();
            }
        }
        if (t) { tip = W(t); di->lpszText = (LPWSTR)tip.c_str(); }
        return 0;
    }
    if (d.bar_tb && nm->hwndFrom == d.bar_tb) {   // the Longhorn command bar
        if (nm->code == NM_CUSTOMDRAW) {
            if (!lhbar::enabled()) return CDRF_DODEFAULT;
            return lhbar::custom_draw((NMTBCUSTOMDRAW*)nm, browser::frame_active(d.dlg, d.focus_active), nullptr);
        }
        if (nm->code == TBN_DROPDOWN) {
            if (((NMTOOLBARW*)nm)->iItem == ID_VIEWMENU) SendMessageW(d.dlg, WM_COMMAND, ID_VIEWMENU, 0);
            return TBDDRET_DEFAULT;
        }
    }
    if (nm->hwndFrom == d.toolbar && nm->code == TBN_DROPDOWN) {
        if (((NMTOOLBARW*)nm)->iItem == ID_VIEWMENU) view_menu(d);
        return TBDDRET_DEFAULT;
    }
    if (nm->hwndFrom != d.list) return 0;
    switch (nm->code) {
    case LVN_BEGINDRAG:
        drag_selection(d);
        return 0;
    case LVN_ITEMCHANGED: {
        NMLISTVIEW* lv = (NMLISTVIEW*)nm;
        if ((lv->uChanged & LVIF_STATE) && ((lv->uOldState ^ lv->uNewState) & LVIS_SELECTED)) selection_to_name(d);
        return 0;
    }
    case NM_DBLCLK: {
        NMITEMACTIVATE* ia = (NMITEMACTIVATE*)nm;
        if (ia->iItem < 0) return 0;
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = ia->iItem;
        SendMessageW(d.list, LVM_GETITEMW, 0, (LPARAM)&lvi);
        if (lvi.lParam < 0 || (size_t)lvi.lParam >= d.items.size()) return 0;
        const Item it = d.items[(size_t)lvi.lParam];
        if (it.is_folder()) { d.pending_nav = it.path; PostMessageW(d.dlg, WM_APP_NAVIGATE, 0, 0); }
        else { selection_to_name(d); PostMessageW(d.dlg, WM_COMMAND, IDOK, 0); }
        return 0;
    }
    case LVN_COLUMNCLICK: {
        int col = ((NMLISTVIEW*)nm)->iSubItem;
        shell::SortBy by = (shell::SortBy)col;
        if (by == d.sort) d.ascending = !d.ascending; else { d.sort = by; d.ascending = true; }
        fill_list(d);
        return 0;
    }
    case LVN_BEGINLABELEDITW: {
        NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
        if (di->item.lParam < 0 || (size_t)di->item.lParam >= d.items.size()) return TRUE;
        const Item& it = d.items[(size_t)di->item.lParam];
        return !is_fs_path(it.path) || (it.flags & shell::IsVirtual) || it.path == shell::home_dir();
    }
    case LVN_ENDLABELEDITW: {
        NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
        if (!di->item.pszText) return FALSE;
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = di->item.iItem;
        SendMessageW(d.list, LVM_GETITEMW, 0, (LPARAM)&lvi);
        if (lvi.lParam < 0 || (size_t)lvi.lParam >= d.items.size()) return FALSE;
        std::string to = w32::utf8_of(di->item.pszText), np;
        if (to == d.items[(size_t)lvi.lParam].name) return FALSE;
        // XP: refused or failed, the label is edited again with what was typed
        if (!fileopui::rename(d.dlg, d.items[(size_t)lvi.lParam].path, to, &np)) { fileopui::edit_again(d.list, di->item.iItem, to); return FALSE; }
        if (np == d.items[(size_t)lvi.lParam].path) return FALSE;
        d.pending_nav.clear();
        fill_list(d, np);
        return FALSE;   // the list was rebuilt
    }
    }
    return 0;
}

static void on_command(Dlg& d, int id, int code, HWND ctl)
{
    if (d.header && header::menu_command((UINT)id, ID_HEADER_FIRST)) {   // View > Toolbars
        browser::header_refit(d.dlg, lh_layout_cur);
        InvalidateRect(d.dlg, nullptr, TRUE);
        return;
    }
    if (d.lh && browser::zoom_command(d, id, ID_ZOOM_FIRST)) return;   // View > Zoom
    switch (id) {
    case IDOK: on_ok(d); return;
    case IDCANCEL:
        d.r->ok = false;
        d.r->folder = d.folder ? d.folder->path() : "";
        EndDialog(d.dlg, IDCANCEL);
        return;
    case ID_BACK: go_back(d); return;
    case ID_FORWARD: go_forward(d); return;
    case ID_UP: go_up(d); return;
    case ID_NEWFOLDER: new_folder(d); return;
    case ID_VIEWMENU:
        if (d.lh) {
            RECT r = {};
            if (d.bar_tb && IsWindowVisible(d.bar)) { SendMessageW(d.bar_tb, TB_GETRECT, ID_VIEWMENU, (LPARAM)&r); MapWindowPoints(d.bar_tb, nullptr, (POINT*)&r, 2); }
            else if (!header::tool_rect(ID_VIEWMENU, &r)) { GetWindowRect(d.list, &r); r.bottom = r.top; }
            lh_view_menu(d, r.left, r.bottom);
        } else view_menu(d);
        return;
    case ID_VIEW_THUMBS:     // the Longhorn look: the thumbnails setting
        if (d.lh) browser::view_command(d, browser::VC_THUMBNAILS);
        else set_view(d, VIEW_THUMBNAILS);
        return;
    case ID_VIEW_TILES: if (!browser::view_command(d, browser::VC_TILES)) set_view(d, VIEW_TILES); return;
    case ID_VIEW_HIDENAMES: browser::view_command(d, browser::VC_HIDENAMES); return;
    case ID_VIEW_SMALL: set_view(d, LVS_SMALLICON); return;
    case ID_GROUPS:
        browser::set_grouped(d, !d.grouped);
        if (d.lh) shell::write_setting("filedlg_groups", d.grouped ? "1" : "0");
        return;
    case ID_PANE_ICONS: places::set_icons(!places::icons()); return;
    case ID_CMDBAR:
        d.show_bar = !d.show_bar;
        shell::write_setting("filedlg_toolbar", d.show_bar ? "bar" : "header");
        lh_sync_tools(d);
        lh_layout(d);
        InvalidateRect(d.dlg, nullptr, TRUE);
        return;
    case IDC_FD_HIDE:
        if (d.lh && d.hide) {
            lh_set_explorer(d, !d.show_explorer, true);
            shell::write_setting("filedlg_explorer", d.show_explorer ? "1" : "0");
            if (!d.show_explorer) SetFocus(d.name_edit);
        }
        return;
    case ID_VIEW_ICONS: if (!(d.lh && browser::view_command(d, browser::VC_ICONS))) set_view(d, LVS_ICON); return;
    case ID_VIEW_LIST: set_view(d, LVS_LIST); return;
    case ID_VIEW_DETAILS: if (!(d.lh && browser::view_command(d, browser::VC_DETAILS))) set_view(d, LVS_REPORT); return;
    case ID_SORT_NAME: case ID_SORT_SIZE: case ID_SORT_TYPE: case ID_SORT_DATE:
        d.sort = (shell::SortBy)(id - ID_SORT_NAME); d.ascending = true; fill_list(d); return;
    case ID_REFRESH: fill_list(d); return;
    case ID_HIDDEN: d.show_hidden = !d.show_hidden; fill_list(d); return;
    case cmb2:
        if (code == CBN_SELENDOK || (code == CBN_SELCHANGE && !SendMessageW(ctl, CB_GETDROPPEDSTATE, 0, 0))) {
            int i = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i < (int)d.lookin_list.size() && i != d.current_lookin) {
                d.pending_nav = d.lookin_list[(size_t)i].path;
                PostMessageW(d.dlg, WM_APP_NAVIGATE, 0, 0);
            }
        }
        return;
    case cmb1:
        if (code == CBN_SELCHANGE) {
            int i = (int)SendMessageW(ctl, CB_GETCURSEL, 0, 0);
            if (!d.o->filters.empty()) d.type_filter = i;
            if (has_wildcard(text_of(d.name_edit))) set_name(d, "");
            d.custom.clear();
            fill_list(d);
        }
        return;
    }
    if (id >= ID_PLACE_BASE && id < ID_PLACE_BASE + (int)d.place_list.size()) {
        d.pending_nav = d.place_list[(size_t)(id - ID_PLACE_BASE)].path;
        PostMessageW(d.dlg, WM_APP_NAVIGATE, 0, 0);
    }
}

static INT_PTR CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Dlg* d = (Dlg*)GetWindowLongPtrW(h, DWLP_USER);
    if (d && d->lh && d->dlg) {
        LRESULT r;
        if (lh_frame_message(*d, msg, wp, lp, &r)) {
            if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) return (INT_PTR)r;
            SetWindowLongPtrW(h, DWLP_MSGRESULT, r);
            return TRUE;
        }
    }
    switch (msg) {
    case WM_INITDIALOG: {
        d = (Dlg*)lp;
        SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)d);
        d->dlg = h;
        if (d->lh) lh_init(*d, GetWindow(h, GW_OWNER));
        else init_dialog(*d, GetWindow(h, GW_OWNER));
        if (const char* q = getenv("FLXP_FILEDLG_QUIT_MS")) SetTimer(h, 1, (UINT)atoi(q), nullptr);   // testing
        return FALSE;   // focus set to File name
    }
    case WM_TIMER:
        if (wp == 1) { KillTimer(h, 1); on_command(*d, IDCANCEL, 0, nullptr); }
        return TRUE;
    case WM_APP_NAVIGATE: {
        std::string p = d->pending_nav;
        d->pending_nav.clear();
        if (!p.empty() && !navigate(*d, p, true)) {
            message_box(h, "Open", p + "\nThe folder cannot be opened.", MB_OK | MB_ICONEXCLAMATION);
            build_lookin(*d);
        }
        return TRUE;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lp;
        if (mi->CtlID == cmb2) { measure_lookin(h, mi); return TRUE; }
        return FALSE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (d && di->CtlID == cmb2) { draw_lookin(*d, di); return TRUE; }
        return FALSE;
    }
    case WM_NOTIFY:
        if (!d) return FALSE;
        SetWindowLongPtrW(h, DWLP_MSGRESULT, on_notify(*d, (NMHDR*)lp));
        return TRUE;
    case WM_FLXP_DRAGOVER:
    case WM_FLXP_DROP:
    case WM_FLXP_DRAGLEAVE:
        if (!d) return FALSE;
        SetWindowLongPtrW(h, DWLP_MSGRESULT, drop_message(*d, msg, (const FLXPDROP*)lp));
        return TRUE;
    case WM_COMMAND:
        if (!d) return FALSE;
        on_command(*d, LOWORD(wp), HIWORD(wp), (HWND)lp);
        return TRUE;
    case WM_SIZE:
        if (d && d->lh) { lh_layout(*d); if (d->header) browser::caption_hole(h); }
        else if (d) layout(*d, LOWORD(lp), HIWORD(lp));
        return FALSE;
    case WM_CONTEXTMENU:
        if (d && (HWND)wp == d->list) {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            int item = -1;
            if (pt.x == -1 && pt.y == -1) {   // keyboard: the focused item, else the view's corner
                item = (int)SendMessageW(d->list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED | LVNI_SELECTED);
                RECT r = {};
                if (item >= 0) { r.left = LVIR_ICON; SendMessageW(d->list, LVM_GETITEMRECT, (WPARAM)item, (LPARAM)&r); MapWindowPoints(d->list, nullptr, (POINT*)&r, 2); pt = { r.left + 8, r.bottom }; }
                else { GetWindowRect(d->list, &r); pt = { r.left + 8, r.top + 8 }; }
            } else {
                LVHITTESTINFO ht = {};
                ht.pt = pt;
                ScreenToClient(d->list, &ht.pt);
                item = (int)SendMessageW(d->list, LVM_HITTEST, 0, (LPARAM)&ht);
            }
            if (item >= 0) item_menu(*d, item, pt); else background_menu(*d, pt);
            return TRUE;
        }
        return FALSE;
    case WM_THEMECHANGED:
        if (d && d->places) { SendMessageW(d->places, TB_SETPADDING, 0, MAKELPARAM(2, 2)); InvalidateRect(d->places, nullptr, TRUE); }
        return FALSE;
    case WM_DESTROY:
        if (d) {
            if (d->name_edit && d->edit_proc) SetWindowLongPtrW(d->name_edit, GWLP_WNDPROC, (LONG_PTR)d->edit_proc);
            RemovePropW(d->name_edit, L"flxp_filedlg");
            if (d->lh) {
                lh_save_state(*d);
                header::destroy();   // the shared widget's statics are free for the next dialog
                places::destroy();
                if (d->viewstrip) { viewbutton::destroy(d->viewstrip); d->viewstrip = nullptr; }
            }
            thumbview::leave(d->large);
            thumbview::folder_changed();
        }
        return FALSE;
    }
    return FALSE;
}

// Keys the dialog manager would otherwise eat or ignore (XP handles these in
// its view's TranslateAccelerator): Backspace = Up in the view, F5, Alt+Left,
// Alt+Up, Ctrl+H (hidden files; the GTK convention), F2, Delete.
static LRESULT CALLBACK msg_filter(int code, WPARAM wp, LPARAM lp)
{
    MSG* m = (MSG*)lp;
    Dlg* d = g_cur;
    if (code == MSGF_DIALOGBOX && d && m && (m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN) && IsChild(d->dlg, m->hwnd)) {
        bool alt = m->message == WM_SYSKEYDOWN || (GetKeyState(VK_MENU) & 0x8000);
        bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        HWND focus = GetFocus();
        bool in_list = focus == d->list;
        bool editing = (HWND)SendMessageW(d->list, LVM_GETEDITCONTROL, 0, 0) != nullptr;
        // the Longhorn look's search, address and Filter boxes keep Enter and
        // Escape for themselves (the dialog would accept or cancel)
        if (d->lh && m->message == WM_KEYDOWN && (m->wParam == VK_ESCAPE || m->wParam == VK_RETURN) && focus &&
            ((d->header && IsChild(d->header, focus)) || (d->pane && IsChild(d->pane, focus)))) {
            TranslateMessage(m);
            DispatchMessageW(m);
            return TRUE;
        }
        if (d->lh && !editing) {
            if (alt && m->wParam == VK_RIGHT) { go_forward(*d); return TRUE; }
            if (ctrl && m->wParam == 'L' && d->header) { header::edit_address(); return TRUE; }
            if (((ctrl && (m->wParam == 'E' || m->wParam == 'F')) || m->wParam == VK_F3) && d->header) { header::open_search(); return TRUE; }
            if (ctrl && !alt && (m->wParam == VK_OEM_PLUS || m->wParam == VK_ADD || m->wParam == VK_OEM_MINUS || m->wParam == VK_SUBTRACT)) {
                browser::zoom_step(*d, m->wParam == VK_OEM_PLUS || m->wParam == VK_ADD ? 1 : -1);
                return TRUE;
            }
            if (ctrl && !alt && m->wParam >= '0' && m->wParam <= '5') {
                // as the explorer's (browser::view_key): 0 the default view, 1..5 Details, Tiles,
                // Large, Medium and Small Icons
                browser::view_key(*d, (int)m->wParam);
                return TRUE;
            }
        }
        if (!editing) {
            if (m->wParam == VK_BACK && in_list) { go_up(*d); return TRUE; }
            if (m->wParam == VK_F5) { fill_list(*d); return TRUE; }
            if (alt && m->wParam == VK_LEFT) { go_back(*d); return TRUE; }
            if (alt && m->wParam == VK_UP) { go_up(*d); return TRUE; }
            if (ctrl && m->wParam == 'H') { d->show_hidden = !d->show_hidden; fill_list(*d); return TRUE; }
            if (ctrl && m->wParam == 'A' && in_list && d->o->multiple) {
                LVITEMW st = {}; st.stateMask = LVIS_SELECTED; st.state = LVIS_SELECTED;
                SendMessageW(d->list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
                return TRUE;
            }
            if (m->wParam == VK_F2 && in_list) {
                int i = (int)SendMessageW(d->list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED);
                if (i >= 0) SendMessageW(d->list, LVM_EDITLABELW, (WPARAM)i, 0);
                return TRUE;
            }
            if (m->wParam == VK_DELETE && in_list) { delete_selection(*d); return TRUE; }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

Result run(void* owner, const Options& o)
{
    Result r;
    Dlg d;
    d.o = &o;
    d.r = &r;
    d.view = LVS_LIST;                   // XP's file dialog starts in List
    d.remember_views = false;            // the explorer's views file is not the dialog's
    d.select_parent = true;              // going up selects the folder we came from (XP)
    d.record_same = false;
    d.icons.small = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
    d.icons.large = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 16, 16);
    d.small = d.icons.small;
    d.large = d.icons.large;
    // the Longhorn look uses the shared widget's singletons (header, places
    // pane): only one such dialog at a time, and not inside an explorer
    d.lh = longhorn_look(o) && !places::window() && !header::search_box();
    Dlg* outer = g_cur;
    HHOOK outer_hook = g_hook;
    g_cur = &d;
    g_hook = SetWindowsHookExW(WH_MSGFILTER, msg_filter, nullptr, GetCurrentThreadId());
    DlgHost host;
    host.d = &d;
    verbs::Host* outer_host = verbs::host();
    verbs::set_host(&host);
    INT_PTR rc = DialogBoxParamW(module(), MAKEINTRESOURCEW(d.lh ? IDD_FILEDLG_LH : IDD_FILEDLG), (HWND)owner, dlg_proc, (LPARAM)&d);
    verbs::set_host(outer_host);
    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = outer_hook;
    g_cur = outer;
    if (rc != IDOK) r.ok = false;
    if (d.icons.small) ImageList_Destroy(d.icons.small);
    if (d.icons.large) ImageList_Destroy(d.icons.large);
    for (auto& z : d.icons.sized) ImageList_Destroy(z.second);
    return r;
}

// ---- message box ------------------------------------------------------------------

struct Msg { std::string title, text; unsigned type; };

static INT_PTR CALLBACK msg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        Msg* m = (Msg*)lp;
        SetWindowTextW(h, WS(m->title));
        SetDlgItemTextW(h, IDC_FD_MSG_TEXT, WS(m->text));
        LPCWSTR icon = nullptr;
        switch (m->type & MB_ICONMASK) {
        case MB_ICONHAND: icon = MAKEINTRESOURCEW(32513); break;          // IDI_HAND
        case MB_ICONQUESTION: icon = MAKEINTRESOURCEW(32514); break;
        case MB_ICONEXCLAMATION: icon = MAKEINTRESOURCEW(32515); break;
        case MB_ICONASTERISK: icon = MAKEINTRESOURCEW(32516); break;
        }
        HWND ic = GetDlgItem(h, IDC_FD_MSG_ICON);
        if (icon) SendMessageW(ic, STM_SETICON, (WPARAM)LoadIconW(nullptr, icon), 0);
        else ShowWindow(ic, SW_HIDE);
        order_children({ GetDlgItem(h, IDC_FD_MSG_ICON), GetDlgItem(h, IDC_FD_MSG_TEXT), GetDlgItem(h, IDYES), GetDlgItem(h, IDNO), GetDlgItem(h, IDOK) });
        bool yesno = (m->type & MB_TYPEMASK) == MB_YESNO;
        ShowWindow(GetDlgItem(h, yesno ? IDOK : IDYES), SW_HIDE);
        ShowWindow(GetDlgItem(h, yesno ? IDOK : IDNO), SW_HIDE);
        if (yesno) SendMessageW(h, DM_SETDEFID, IDYES, 0);
        // size to the text (user32 MSGBOX_OnInit, roughly): wrap at 2/3 of the screen
        HWND t = GetDlgItem(h, IDC_FD_MSG_TEXT);
        RECT tr0 = child_rect(h, t);
        HDC hdc = GetDC(t);
        HGDIOBJ of = SelectObject(hdc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        RECT calc = { 0, 0, std::max((int)(tr0.right - tr0.left), (int)sys::screen_size().cx / 3), 0 };
        std::u16string w = W(m->text);
        DrawTextW(hdc, (LPCWSTR)w.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        SelectObject(hdc, of);
        ReleaseDC(t, hdc);
        int tw = calc.right, th = std::max((int)calc.bottom, 32);
        RECT ir = child_rect(h, ic);
        int left = icon ? tr0.left : ir.left;
        SetWindowPos(t, nullptr, left, std::max((int)tr0.top, (int)(ir.top + (ir.bottom - ir.top - calc.bottom) / 2)), tw, th, SWP_NOZORDER);
        RECT br = child_rect(h, GetDlgItem(h, IDOK));
        int bw = br.right - br.left, bh = br.bottom - br.top, gap = 6;
        int nb = yesno ? 2 : 1;
        int cw = std::max(left + tw + ir.left, nb * bw + (nb - 1) * gap + 2 * ir.left);
        int by = std::max((int)ir.bottom, (int)(tr0.top + th)) + 12;
        int bx = (cw - (nb * bw + (nb - 1) * gap)) / 2;
        int ids[2] = { yesno ? IDYES : IDOK, IDNO };
        for (int i = 0; i < nb; i++) SetWindowPos(GetDlgItem(h, ids[i]), nullptr, bx + i * (bw + gap), by, bw, bh, SWP_NOZORDER);
        RECT wr, cr;
        GetWindowRect(h, &wr); GetClientRect(h, &cr);
        int fw = (wr.right - wr.left) - cr.right, fh = (wr.bottom - wr.top) - cr.bottom;
        int W_ = cw + fw, H_ = by + bh + 12 + fh;
        HWND owner = GetWindow(h, GW_OWNER);
        RECT orc;
        gdi::Size ss = sys::screen_size();
        if (owner) GetWindowRect(owner, &orc); else orc = { 0, 0, ss.cx, ss.cy };
        SetWindowPos(h, nullptr, orc.left + (orc.right - orc.left - W_) / 2, orc.top + (orc.bottom - orc.top - H_) / 2, W_, H_, SWP_NOZORDER);
        if (owner) {
            sys::NativeWindow* nw = sys::window_for_user((void*)h);
            sys::set_transient_for(nw, sys::native_id(sys::window_for_user((void*)GetAncestor(owner, GA_ROOT))), true);
        }
        MessageBeep(m->type & MB_ICONMASK);
        SetFocus(GetDlgItem(h, yesno ? IDYES : IDOK));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDYES || LOWORD(wp) == IDNO) { EndDialog(h, LOWORD(wp)); return TRUE; }
        if (LOWORD(wp) == IDCANCEL) {
            // Escape / close: No for Yes/No boxes (they have no Cancel), OK otherwise
            EndDialog(h, IsWindowVisible(GetDlgItem(h, IDNO)) ? IDNO : IDOK);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

int message_box(void* owner, const std::string& title, const std::string& text, unsigned type)
{
    Msg m = { title, text, type };
    return (int)DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_FD_MESSAGE), (HWND)owner, msg_proc, (LPARAM)&m);
}

} // namespace filedlg
