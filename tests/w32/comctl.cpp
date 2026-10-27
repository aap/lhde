// Common-controls demo, laid out like a file explorer: rebar with a toolbar
// and an address band, folder tree, file list, status bar; and a second
// window with a tab control holding the remaining controls.
//
//   build/w32test/comctl [quit_ms]
//
// Notifications from the tree, list and toolbar go to stderr.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>
#include "w32/w32.h"
#include "w32/user.h"
extern "C" {
#include "commctrl.h"
#include "uxtheme.h"
#include "vssym32.h"
}

enum {
    ID_REBAR = 100, ID_TOOLBAR, ID_ADDRESS, ID_TREE, ID_LIST, ID_STATUS, ID_TAB,
    ID_PROGRESS, ID_MARQUEE, ID_TRACK, ID_UPDOWN, ID_BUDDY, ID_DTP, ID_MONTHCAL, ID_HOTKEY, ID_IPADDR,
    ID_TRACKLABEL, ID_ABOUT,
    IDC_BACK = 200, IDC_FORWARD, IDC_UP, IDC_SEARCH, IDC_FOLDERS, IDC_VIEWS,
};

// Icon indices in the image lists, in load order.
enum { IC_FOLDER, IC_FOLDER_OPEN, IC_DOCUMENT, IC_DRIVE, IC_DESKTOP, IC_COMPUTER, IC_IMAGE,
       IC_AUDIO, IC_CDROM, IC_NETWORK, IC_TRASH, IC_MYDOCS, IC_VIDEO, IC_CONTROL, IC_COUNT };
static const char* icon_files[IC_COUNT] = { "folder", "folder_open", "document", "drive", "desktop", "mycomputer",
    "image", "audio", "optical_drive", "network", "trash_file", "mydocs", "video", "control" };
// Wine's shell32 icons, as flxp ships them in flxp/res/icons
static std::string icon_dir()
{
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return "flxp/res/icons/";
    exe[n] = 0;
    std::string p = exe;
    return p.substr(0, p.rfind('/') + 1) + "../../../flxp/res/icons/";
}

static HWND g_main, g_rebar, g_toolbar, g_address, g_tree, g_list, g_status, g_ctlwin, g_tab;
static HIMAGELIST g_small, g_large;
static int g_split = 200;          // tree width
static bool g_dragging, g_show_tree = true;
static int g_view;                 // 0 report, 1 icon, 2 list, 3 small icon
static int g_sort_col = 0;
static bool g_sort_down;

struct FileRow { std::u16string name; int size_kb; const char16_t* type; int icon; int y, mo, d, h, mi; bool dir; };
static std::vector<FileRow> g_rows;

static HTREEITEM tv_next(HTREEITEM h, UINT code) { return (HTREEITEM)SendMessageW(g_tree, TVM_GETNEXTITEM, code, (LPARAM)h); }
static std::u16string w(const char* s) { return w32::utf16_of(s); }
static const WCHAR* W(const std::u16string& s) { return (const WCHAR*)s.c_str(); }

static HIMAGELIST make_imagelist(int size)
{
    HIMAGELIST il = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, IC_COUNT, 4);
    for (int i = 0; i < IC_COUNT; i++) {
        std::string path = icon_dir() + icon_files[i] + ".ico";
        HICON ic = (HICON)LoadImageW(nullptr, W(w(path.c_str())), IMAGE_ICON, size, size, LR_LOADFROMFILE);
        if (!ic) fprintf(stderr, "cannot load %s\n", path.c_str());
        int idx = ImageList_AddIcon(il, ic);
        if (idx != i) fprintf(stderr, "ImageList_AddIcon(%s) = %d, expected %d\n", icon_files[i], idx, i);
        if (ic) DestroyIcon(ic);
    }
    return il;
}

// ---- toolbar and rebar -------------------------------------------------------

static void create_rebar(HWND parent)
{
    g_rebar = CreateWindowExW(WS_EX_TOOLWINDOW, REBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | RBS_VARHEIGHT | RBS_BANDBORDERS | RBS_DBLCLKTOGGLE | CCS_NODIVIDER,
        0, 0, 0, 0, parent, (HMENU)ID_REBAR, nullptr, nullptr);
    REBARINFO ri = { sizeof ri };
    SendMessageW(g_rebar, RB_SETBARINFO, 0, (LPARAM)&ri);

    g_toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0, g_rebar, (HMENU)ID_TOOLBAR, nullptr, nullptr);
    SendMessageW(g_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
    int std_base = (int)SendMessageW(g_toolbar, TB_LOADIMAGES, IDB_STD_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    int view_base = (int)SendMessageW(g_toolbar, TB_LOADIMAGES, IDB_VIEW_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    int hist_base = (int)SendMessageW(g_toolbar, TB_LOADIMAGES, IDB_HIST_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    fprintf(stderr, "TB_LOADIMAGES bases: std=%d view=%d hist=%d\n", std_base, view_base, hist_base);
    int s_back = (int)SendMessageW(g_toolbar, TB_ADDSTRINGW, 0, (LPARAM)L"Back\0Forward\0Up\0Search\0Folders\0Views\0");
    TBBUTTON b[] = {
        { hist_base + HIST_BACK, IDC_BACK, TBSTATE_ENABLED, BTNS_DROPDOWN | BTNS_AUTOSIZE, {0}, 0, s_back + 0 },
        { hist_base + HIST_FORWARD, IDC_FORWARD, 0, BTNS_DROPDOWN | BTNS_AUTOSIZE, {0}, 0, s_back + 1 },
        { view_base + VIEW_PARENTFOLDER, IDC_UP, TBSTATE_ENABLED, BTNS_AUTOSIZE, {0}, 0, s_back + 2 },
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },
        { std_base + STD_FIND, IDC_SEARCH, TBSTATE_ENABLED, BTNS_AUTOSIZE, {0}, 0, s_back + 3 },
        { hist_base + HIST_VIEWTREE, IDC_FOLDERS, TBSTATE_ENABLED | TBSTATE_CHECKED, BTNS_CHECK | BTNS_AUTOSIZE, {0}, 0, s_back + 4 },
        { 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0 },
        { view_base + VIEW_DETAILS, IDC_VIEWS, TBSTATE_ENABLED, BTNS_DROPDOWN | BTNS_AUTOSIZE, {0}, 0, s_back + 5 },
    };
    SendMessageW(g_toolbar, TB_ADDBUTTONSW, sizeof b / sizeof b[0], (LPARAM)b);
    SendMessageW(g_toolbar, TB_AUTOSIZE, 0, 0);
    SIZE sz; SendMessageW(g_toolbar, TB_GETMAXSIZE, 0, (LPARAM)&sz);
    fprintf(stderr, "toolbar max size %ldx%ld\n", (long)sz.cx, (long)sz.cy);

    REBARBANDINFOW bi = { sizeof bi };
    bi.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
    bi.fStyle = RBBS_CHILDEDGE | RBBS_GRIPPERALWAYS;
    bi.hwndChild = g_toolbar;
    bi.cxMinChild = sz.cx; bi.cyMinChild = sz.cy;
    bi.cx = sz.cx + 20; bi.cxIdeal = sz.cx;
    SendMessageW(g_rebar, RB_INSERTBANDW, (WPARAM)-1, (LPARAM)&bi);

    g_address = CreateWindowExW(0, WC_COMBOBOXEXW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        0, 0, 300, 200, g_rebar, (HMENU)ID_ADDRESS, nullptr, nullptr);
    SendMessageW(g_address, CBEM_SETIMAGELIST, 0, (LPARAM)g_small);
    struct { const char* text; int icon; int indent; } addr[] = {
        { "Desktop", IC_DESKTOP, 0 }, { "My Documents", IC_MYDOCS, 1 }, { "My Computer", IC_COMPUTER, 1 },
        { "Local Disk (C:)", IC_DRIVE, 2 }, { "DVD Drive (D:)", IC_CDROM, 2 }, { "Control Panel", IC_CONTROL, 2 },
        { "My Network Places", IC_NETWORK, 1 }, { "Recycle Bin", IC_TRASH, 1 },
    };
    for (int i = 0; i < (int)(sizeof addr / sizeof addr[0]); i++) {
        std::u16string t = w(addr[i].text);
        COMBOBOXEXITEMW it = {};
        it.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_INDENT;
        it.iItem = i; it.pszText = (LPWSTR)W(t); it.iImage = it.iSelectedImage = addr[i].icon; it.iIndent = addr[i].indent;
        SendMessageW(g_address, CBEM_INSERTITEMW, 0, (LPARAM)&it);
    }
    SendMessageW(g_address, CB_SETCURSEL, 3, 0);
    RECT ar; GetWindowRect(g_address, &ar);
    HWND combo = (HWND)SendMessageW(g_address, CBEM_GETCOMBOCONTROL, 0, 0);
    RECT cr; GetWindowRect(combo ? combo : g_address, &cr);

    bi.fMask = RBBIM_STYLE | RBBIM_TEXT | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE;
    bi.fStyle = RBBS_BREAK | RBBS_CHILDEDGE | RBBS_GRIPPERALWAYS;
    bi.lpText = (LPWSTR)L"Address";
    bi.hwndChild = g_address;
    bi.cxMinChild = 100; bi.cyMinChild = cr.bottom - cr.top;
    bi.cx = 400;
    SendMessageW(g_rebar, RB_INSERTBANDW, (WPARAM)-1, (LPARAM)&bi);
}

// ---- tree --------------------------------------------------------------------

static HTREEITEM tree_add(HTREEITEM parent, const char* text, int icon, int sel_icon, bool children = false)
{
    std::u16string t = w(text);
    TVINSERTSTRUCTW is = {};
    is.hParent = parent; is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
    is.item.pszText = (LPWSTR)W(t);
    is.item.iImage = icon; is.item.iSelectedImage = sel_icon;
    is.item.cChildren = children ? 1 : 0;
    return (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&is);
}
static HTREEITEM tree_folder(HTREEITEM parent, const char* text) { return tree_add(parent, text, IC_FOLDER, IC_FOLDER_OPEN); }

static void fix_children(HTREEITEM h)
{
    // cChildren=1 was set on containers only; leaves keep the default 0
    for (HTREEITEM c = tv_next(h, TVGN_CHILD); c; c = tv_next(c, TVGN_NEXT)) {
        TVITEMW it = {}; it.mask = TVIF_CHILDREN | TVIF_HANDLE; it.hItem = c;
        it.cChildren = tv_next(c, TVGN_CHILD) ? 1 : 0;
        SendMessageW(g_tree, TVM_SETITEMW, 0, (LPARAM)&it);
        fix_children(c);
    }
}

static void create_tree(HWND parent)
{
    g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, (HMENU)ID_TREE, nullptr, nullptr);
    SendMessageW(g_tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)g_small);
    HTREEITEM desk = tree_add(TVI_ROOT, "Desktop", IC_DESKTOP, IC_DESKTOP, true);
    HTREEITEM docs = tree_add(desk, "My Documents", IC_MYDOCS, IC_MYDOCS, true);
    tree_folder(docs, "My Music"); tree_folder(docs, "My Pictures"); tree_folder(docs, "My Videos");
    HTREEITEM comp = tree_add(desk, "My Computer", IC_COMPUTER, IC_COMPUTER, true);
    HTREEITEM c = tree_add(comp, "Local Disk (C:)", IC_DRIVE, IC_DRIVE, true);
    HTREEITEM ds = tree_folder(c, "Documents and Settings");
    tree_folder(ds, "Administrator"); tree_folder(ds, "All Users");
    HTREEITEM pf = tree_folder(c, "Program Files");
    tree_folder(pf, "Common Files"); tree_folder(pf, "Internet Explorer"); tree_folder(pf, "Windows Media Player");
    HTREEITEM win = tree_folder(c, "WINDOWS");
    tree_folder(win, "Fonts"); tree_folder(win, "Resources"); HTREEITEM sys = tree_folder(win, "system32");
    tree_folder(sys, "drivers"); tree_folder(sys, "spool");
    tree_add(comp, "DVD Drive (D:)", IC_CDROM, IC_CDROM);
    tree_add(comp, "Control Panel", IC_CONTROL, IC_CONTROL);
    HTREEITEM net = tree_add(desk, "My Network Places", IC_NETWORK, IC_NETWORK, true);
    tree_add(net, "Entire Network", IC_NETWORK, IC_NETWORK);
    tree_add(desk, "Recycle Bin", IC_TRASH, IC_TRASH);
    fix_children(TVI_ROOT);
    SendMessageW(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)desk);
    SendMessageW(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)comp);
    SendMessageW(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)c);
    SendMessageW(g_tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)c);
}

// ---- list --------------------------------------------------------------------

static void make_rows(const std::u16string& folder)
{
    g_rows.clear();
    static const char* dirs[] = { "Documents and Settings", "Program Files", "WINDOWS", "Temp" };
    static const struct { const char* name; int kb; const char* type; int icon; } files[] = {
        { "AUTOEXEC.BAT", 0, "MS-DOS Batch File", IC_DOCUMENT }, { "boot.ini", 1, "Configuration Settings", IC_DOCUMENT },
        { "CONFIG.SYS", 0, "System file", IC_DOCUMENT }, { "hiberfil.sys", 523776, "System file", IC_DOCUMENT },
        { "IO.SYS", 0, "System file", IC_DOCUMENT }, { "MSDOS.SYS", 0, "System file", IC_DOCUMENT },
        { "NTDETECT.COM", 47, "MS-DOS Application", IC_DOCUMENT }, { "ntldr", 251, "System file", IC_DOCUMENT },
        { "pagefile.sys", 786432, "System file", IC_DOCUMENT }, { "readme.txt", 4, "Text Document", IC_DOCUMENT },
        { "bliss.bmp", 1407, "Bitmap Image", IC_IMAGE }, { "logo.png", 23, "PNG Image", IC_IMAGE },
        { "chimes.wav", 54, "Wave Sound", IC_AUDIO }, { "town.mid", 17, "MIDI Sequence", IC_AUDIO },
        { "clock.avi", 82, "Video Clip", IC_VIDEO }, { "setup.log", 12, "Text Document", IC_DOCUMENT },
    };
    int n = 0;
    for (const char* d : dirs) {
        FileRow r{ w(d), -1, u"File Folder", IC_FOLDER, 2004, 8, 3 + n, 9 + n, 12 * n % 60, true };
        g_rows.push_back(r); n++;
    }
    for (auto& f : files) {
        FileRow r{ w(f.name), f.kb, (const char16_t*)W(w(f.type)), f.icon, 2004 + n % 3, 1 + n % 12, 1 + n * 7 % 28, n * 5 % 24, n * 13 % 60, false };
        static std::vector<std::u16string> types;    // keep the type strings alive
        types.push_back(w(f.type)); r.type = types.back().c_str();
        g_rows.push_back(r); n++;
    }
    (void)folder;
}

static void row_text(int i, int col, WCHAR* buf, int n)
{
    const FileRow& r = g_rows[i];
    char tmp[64]; tmp[0] = 0;
    switch (col) {
    case 0: lstrcpynW(buf, W(r.name), n); return;
    case 1: if (!r.dir) snprintf(tmp, sizeof tmp, "%d KB", r.size_kb ? r.size_kb : 1); break;
    case 2: lstrcpynW(buf, (const WCHAR*)r.type, n); return;
    case 3: snprintf(tmp, sizeof tmp, "%d/%d/%d %d:%02d %s", r.mo, r.d, r.y, r.h % 12 ? r.h % 12 : 12, r.mi, r.h < 12 ? "AM" : "PM"); break;
    }
    lstrcpynW(buf, W(w(tmp)), n);
}

static void fill_list()
{
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < (int)g_rows.size(); i++) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        it.iItem = i; it.pszText = LPSTR_TEXTCALLBACKW; it.iImage = g_rows[i].icon; it.lParam = i;
        SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
}

static int CALLBACK compare_rows(LPARAM a, LPARAM b, LPARAM col)
{
    const FileRow& x = g_rows[a]; const FileRow& y = g_rows[b];
    int r = 0;
    if (x.dir != y.dir) return x.dir ? -1 : 1;     // folders first, always
    switch (col) {
    case 0: r = lstrcmpiW(W(x.name), W(y.name)); break;
    case 1: r = x.size_kb - y.size_kb; break;
    case 2: r = lstrcmpiW((const WCHAR*)x.type, (const WCHAR*)y.type); break;
    case 3: r = ((((x.y * 13 + x.mo) * 32 + x.d) * 24 + x.h) * 60 + x.mi) - ((((y.y * 13 + y.mo) * 32 + y.d) * 24 + y.h) * 60 + y.mi); break;
    }
    if (!r) r = lstrcmpiW(W(x.name), W(y.name));
    return g_sort_down ? -r : r;
}

static void sort_list(int col)
{
    if (col == g_sort_col) g_sort_down = !g_sort_down; else { g_sort_col = col; g_sort_down = false; }
    SendMessageW(g_list, LVM_SORTITEMS, (WPARAM)g_sort_col, (LPARAM)compare_rows);
    HWND hdr = (HWND)SendMessageW(g_list, LVM_GETHEADER, 0, 0);
    int n = (int)SendMessageW(hdr, HDM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        HDITEMW hi = {}; hi.mask = HDI_FORMAT;
        SendMessageW(hdr, HDM_GETITEMW, i, (LPARAM)&hi);
        hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g_sort_col) hi.fmt |= g_sort_down ? HDF_SORTDOWN : HDF_SORTUP;
        SendMessageW(hdr, HDM_SETITEMW, i, (LPARAM)&hi);
    }
}

static void create_list(HWND parent)
{
    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE,
        0, 0, 0, 0, parent, (HMENU)ID_LIST, nullptr, nullptr);
    SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP);
    SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_small);
    SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g_large);
    struct { const wchar_t* name; int cx; int fmt; } cols[] = {
        { L"Name", 170, LVCFMT_LEFT }, { L"Size", 80, LVCFMT_RIGHT }, { L"Type", 140, LVCFMT_LEFT }, { L"Date Modified", 130, LVCFMT_LEFT },
    };
    for (int i = 0; i < 4; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        c.pszText = (LPWSTR)cols[i].name; c.cx = cols[i].cx; c.fmt = cols[i].fmt; c.iSubItem = i;
        SendMessageW(g_list, LVM_INSERTCOLUMNW, i, (LPARAM)&c);
    }
    make_rows(u"C:\\");
    fill_list();
    g_sort_col = -1; sort_list(0);
    LVITEMW st = {}; st.stateMask = st.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(g_list, LVM_SETITEMSTATE, 0, (LPARAM)&st);
}

static void set_view(int v)
{
    static const DWORD styles[] = { LVS_REPORT, LVS_ICON, LVS_LIST, LVS_SMALLICON };
    static const char* names[] = { "Details", "Icons", "List", "Small Icons" };
    static const int images[] = { VIEW_DETAILS, VIEW_LARGEICONS, VIEW_LIST, VIEW_SMALLICONS };
    g_view = v % 4;
    LONG st = GetWindowLongW(g_list, GWL_STYLE);
    SetWindowLongW(g_list, GWL_STYLE, (st & ~LVS_TYPEMASK) | styles[g_view]);
    // the Views button shows the current view's icon (view images were loaded after the 15 std ones)
    TBBUTTONINFOW bi = { sizeof bi }; bi.dwMask = TBIF_IMAGE;
    SendMessageW(g_toolbar, TB_GETBUTTONINFOW, IDC_VIEWS, (LPARAM)&bi);
    static int view_base = -1;
    if (view_base < 0) view_base = bi.iImage - VIEW_DETAILS;
    bi.iImage = view_base + images[g_view];
    SendMessageW(g_toolbar, TB_SETBUTTONINFOW, IDC_VIEWS, (LPARAM)&bi);
    fprintf(stderr, "view -> %s\n", names[g_view]);
    std::u16string t = w(names[g_view]);
    SendMessageW(g_status, SB_SETTEXTW, 2, (LPARAM)W(u"View: " + t));
}

// ---- status bar and layout ---------------------------------------------------

static void create_status(HWND parent)
{
    g_status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, parent, (HMENU)ID_STATUS, nullptr, nullptr);
}

static void update_status()
{
    RECT r; GetClientRect(g_main, &r);
    int wdt = r.right;
    int parts[3] = { wdt - 300 > 100 ? wdt - 300 : 100, wdt - 150 > 200 ? wdt - 150 : 200, -1 };
    SendMessageW(g_status, SB_SETPARTS, 3, (LPARAM)parts);
    char buf[64];
    int sel = (int)SendMessageW(g_list, LVM_GETSELECTEDCOUNT, 0, 0);
    if (sel) snprintf(buf, sizeof buf, "%d object(s) selected", sel);
    else snprintf(buf, sizeof buf, "%d objects", (int)g_rows.size());
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)W(w(buf)));
    SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)L"1.27 GB");
    if (!SendMessageW(g_status, SB_GETTEXTLENGTHW, 2, 0)) SendMessageW(g_status, SB_SETTEXTW, 2, (LPARAM)L"My Computer");
}

static void layout(HWND h)
{
    RECT r; GetClientRect(h, &r);
    SendMessageW(g_rebar, WM_SIZE, 0, 0);
    SendMessageW(g_status, WM_SIZE, 0, 0);
    RECT rr, sr; GetWindowRect(g_rebar, &rr); GetWindowRect(g_status, &sr);
    int top = rr.bottom - rr.top, bottom = r.bottom - (sr.bottom - sr.top);
    int split = g_show_tree ? g_split : 0;
    if (split > r.right - 50) split = r.right - 50;
    if (split < 0) split = 0;
    ShowWindow(g_tree, g_show_tree ? SW_SHOW : SW_HIDE);
    if (g_show_tree) MoveWindow(g_tree, 0, top, split, bottom - top, TRUE);
    int gap = g_show_tree ? 4 : 0;
    MoveWindow(g_list, split + gap, top, r.right - split - gap, bottom - top, TRUE);
    update_status();
}

// ---- notifications -------------------------------------------------------------

static const char* tree_text(HTREEITEM h, char* out, int n)
{
    WCHAR buf[128]; TVITEMW it = {}; it.mask = TVIF_TEXT | TVIF_HANDLE; it.hItem = h; it.pszText = buf; it.cchTextMax = 128;
    buf[0] = 0;
    if (h) SendMessageW(g_tree, TVM_GETITEMW, 0, (LPARAM)&it);
    snprintf(out, n, "%s", w32::utf8_of(buf).c_str());
    return out;
}

static void address_for(HTREEITEM h)
{
    // build a path from the tree: skip Desktop/My Computer like XP's address bar
    std::u16string path;
    char t[128];
    for (HTREEITEM p = h; p; p = tv_next(p, TVGN_PARENT)) {
        tree_text(p, t, sizeof t);
        if (!tv_next(p, TVGN_PARENT)) { if (p == h) path = w(t); break; }
        std::string s = t;
        if (s == "My Computer" && p != h) break;
        if (s.size() > 4 && s.compare(s.size() - 4, 4, "(C:)") == 0) s = "C:";
        else if (s.size() > 4 && s.compare(s.size() - 4, 4, "(D:)") == 0) s = "D:";
        path = path.empty() ? w(s.c_str()) : w(s.c_str()) + u"\\" + path;
    }
    if (path == u"C:" || path == u"D:") path += u"\\";
    SetWindowTextW(g_address, W(path));
    SendMessageW(g_status, SB_SETTEXTW, 2, (LPARAM)W(path));
}

static LRESULT on_notify(HWND h, NMHDR* nm)
{
    char t[128];
    if (nm->hwndFrom == g_tree) {
        NMTREEVIEWW* tv = (NMTREEVIEWW*)nm;
        switch (nm->code) {
        case TVN_SELCHANGEDW: fprintf(stderr, "TVN_SELCHANGED %s (action %u)\n", tree_text(tv->itemNew.hItem, t, sizeof t), tv->action); address_for(tv->itemNew.hItem); break;
        case TVN_SELCHANGINGW: fprintf(stderr, "TVN_SELCHANGING -> %s\n", tree_text(tv->itemNew.hItem, t, sizeof t)); break;
        case TVN_ITEMEXPANDINGW: fprintf(stderr, "TVN_ITEMEXPANDING %s action=%u\n", tree_text(tv->itemNew.hItem, t, sizeof t), tv->action); break;
        case TVN_ITEMEXPANDEDW: fprintf(stderr, "TVN_ITEMEXPANDED %s action=%u\n", tree_text(tv->itemNew.hItem, t, sizeof t), tv->action); break;
        case TVN_KEYDOWN: fprintf(stderr, "TVN_KEYDOWN vk=%u\n", ((NMTVKEYDOWN*)nm)->wVKey); break;
        case NM_CLICK: fprintf(stderr, "TV NM_CLICK\n"); break;
        case NM_DBLCLK: fprintf(stderr, "TV NM_DBLCLK\n"); break;
        }
        return 0;
    }
    if (nm->hwndFrom == g_list) {
        NMLISTVIEW* lv = (NMLISTVIEW*)nm;
        switch (nm->code) {
        case LVN_GETDISPINFOW: {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
            if (di->item.mask & LVIF_TEXT) row_text((int)di->item.lParam, di->item.iSubItem, di->item.pszText, di->item.cchTextMax);
            return 0;
        }
        case LVN_ITEMCHANGED:
            if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED)) {
                fprintf(stderr, "LVN_ITEMCHANGED item=%d %s\n", lv->iItem, (lv->uNewState & LVIS_SELECTED) ? "selected" : "deselected");
                update_status();
            }
            return 0;
        case LVN_COLUMNCLICK: fprintf(stderr, "LVN_COLUMNCLICK col=%d\n", lv->iSubItem); sort_list(lv->iSubItem); return 0;
        case LVN_KEYDOWN: fprintf(stderr, "LVN_KEYDOWN vk=%u\n", ((NMLVKEYDOWN*)nm)->wVKey); return 0;
        case LVN_ITEMACTIVATE: fprintf(stderr, "LVN_ITEMACTIVATE item=%d\n", ((NMITEMACTIVATE*)nm)->iItem); return 0;
        case LVN_BEGINDRAG: fprintf(stderr, "LVN_BEGINDRAG item=%d\n", lv->iItem); return 0;
        case NM_CLICK: fprintf(stderr, "LV NM_CLICK item=%d\n", ((NMITEMACTIVATE*)nm)->iItem); return 0;
        case NM_DBLCLK: fprintf(stderr, "LV NM_DBLCLK item=%d\n", ((NMITEMACTIVATE*)nm)->iItem); return 0;
        }
        return 0;
    }
    if (nm->hwndFrom == g_toolbar) {
        NMTOOLBARW* tb = (NMTOOLBARW*)nm;
        switch (nm->code) {
        case TBN_DROPDOWN:
            fprintf(stderr, "TBN_DROPDOWN id=%d\n", tb->iItem);
            if (tb->iItem == IDC_VIEWS) set_view(g_view + 1);
            return TBDDRET_DEFAULT;
        case TBN_HOTITEMCHANGE: {
            NMTBHOTITEM* hi = (NMTBHOTITEM*)nm;
            fprintf(stderr, "TBN_HOTITEMCHANGE %d -> %d flags=%lx\n", hi->idOld, hi->idNew, (long)hi->dwFlags);
            return 0;
        }
        case TBN_GETINFOTIPW: {
            NMTBGETINFOTIPW* ti = (NMTBGETINFOTIPW*)nm;
            const wchar_t* tips[] = { L"Back", L"Forward", L"Up one level", L"Search", L"Folders", L"Views" };
            if (ti->iItem >= IDC_BACK && ti->iItem <= IDC_VIEWS) lstrcpynW(ti->pszText, (LPCWSTR)tips[ti->iItem - IDC_BACK], ti->cchTextMax);
            fprintf(stderr, "TBN_GETINFOTIP id=%d\n", ti->iItem);
            return 0;
        }
        case NM_CLICK: fprintf(stderr, "TB NM_CLICK id=%d\n", (int)((NMMOUSE*)nm)->dwItemSpec); return 0;
        case TBN_BEGINDRAG: case TBN_ENDDRAG: return 0;
        }
        fprintf(stderr, "TB notify code=%d\n", (int)nm->code);
        return 0;
    }
    if (nm->hwndFrom == g_rebar) {
        switch (nm->code) {
        case RBN_HEIGHTCHANGE: fprintf(stderr, "RBN_HEIGHTCHANGE\n"); layout(h); return 0;
        case RBN_AUTOSIZE: return 0;
        }
        fprintf(stderr, "RB notify code=%d\n", (int)nm->code);
        return 0;
    }
    (void)t;
    if (nm->code == TTN_GETDISPINFOW) { fprintf(stderr, "TTN_GETDISPINFO id=%d\n", (int)nm->idFrom); return 0; }
    return 0;
}

static LRESULT CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_main = h;
        create_status(h);
        create_rebar(h);
        create_tree(h);
        create_list(h);
        return 0;
    case WM_SIZE:
        layout(h);
        return 0;
    case WM_NOTIFY:
        return on_notify(h, (NMHDR*)lp);
    case WM_COMMAND:
        fprintf(stderr, "WM_COMMAND id=%d code=%d\n", LOWORD(wp), HIWORD(wp));
        switch (LOWORD(wp)) {
        case IDC_VIEWS: set_view(g_view + 1); break;
        case IDC_FOLDERS: g_show_tree = SendMessageW(g_toolbar, TB_ISBUTTONCHECKED, IDC_FOLDERS, 0) != 0; layout(h); break;
        case IDC_UP: {
            HTREEITEM s = tv_next(nullptr, TVGN_CARET), p = s ? tv_next(s, TVGN_PARENT) : nullptr;
            if (p) SendMessageW(g_tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)p);
            break;
        }
        case IDC_BACK: SendMessageW(g_toolbar, TB_ENABLEBUTTON, IDC_FORWARD, TRUE); break;
        }
        return 0;
    case WM_SETCURSOR:
        if ((HWND)wp == h && LOWORD(lp) == HTCLIENT && g_show_tree) {
            POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            if (p.x >= g_split && p.x < g_split + 4) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_LBUTTONDOWN:
        if (g_show_tree && (short)LOWORD(lp) >= g_split && (short)LOWORD(lp) < g_split + 4) { g_dragging = true; SetCapture(h); }
        fprintf(stderr, "main WM_LBUTTONDOWN %d,%d%s\n", (short)LOWORD(lp), (short)HIWORD(lp), g_dragging ? " (splitter)" : "");
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging) { int x = (short)LOWORD(lp); if (x < 40) x = 40; g_split = x; layout(h); }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) { g_dragging = false; ReleaseCapture(); fprintf(stderr, "splitter at %d\n", g_split); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- second window: the other controls on tab pages ----------------------------
//
// Each tab page is a child window over the tab's display area that paints the
// tab body (TABP_BODY when themed), like a property sheet page with
// EnableThemeDialogTexture(ETDT_ENABLETAB); controls that draw their parent's
// background (trackbar, themed statics) then blend in.

static HWND g_page[2];

static HWND child(HWND parent, DWORD ex, const WCHAR* cls, const WCHAR* text, DWORD style, int x, int y, int cx, int cy, int id)
{
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | style, x, y, cx, cy, parent, (HMENU)(intptr_t)id, nullptr, nullptr);
    if (!c) { fprintf(stderr, "failed to create %s\n", w32::utf8_of(cls).c_str()); return nullptr; }
    SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    return c;
}

static void fill_page_controls(HWND pg)
{
    int x = 12, y = 12;
    const WCHAR* ST = WC_STATICW;
    child(pg, 0, ST, L"Progress:", WS_VISIBLE, x, y + 3, 70, 14, -1);
    HWND pb = child(pg, 0, PROGRESS_CLASSW, nullptr, WS_VISIBLE, x + 80, y, 200, 18, ID_PROGRESS);
    SendMessageW(pb, PBM_SETRANGE32, 0, 100); SendMessageW(pb, PBM_SETSTEP, 2, 0);
    y += 26;
    child(pg, 0, ST, L"Marquee:", WS_VISIBLE, x, y + 3, 70, 14, -1);
    HWND mq = child(pg, 0, PROGRESS_CLASSW, nullptr, WS_VISIBLE | PBS_MARQUEE, x + 80, y, 200, 18, ID_MARQUEE);
    SendMessageW(mq, PBM_SETMARQUEE, TRUE, 50);
    y += 30;
    child(pg, 0, ST, L"Trackbar:", WS_VISIBLE, x, y + 6, 70, 14, -1);
    HWND tb = child(pg, 0, TRACKBAR_CLASSW, nullptr, WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_TOOLTIPS, x + 74, y, 212, 30, ID_TRACK);
    SendMessageW(tb, TBM_SETRANGE, FALSE, MAKELONG(0, 20)); SendMessageW(tb, TBM_SETPOS, TRUE, 7); SendMessageW(tb, TBM_SETPAGESIZE, 0, 5);
    child(pg, 0, ST, L"7", WS_VISIBLE, x + 292, y + 6, 24, 14, ID_TRACKLABEL);
    y += 38;
    child(pg, 0, ST, L"Up-down:", WS_VISIBLE, x, y + 3, 70, 14, -1);
    HWND ed = child(pg, WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_VISIBLE | WS_TABSTOP | ES_NUMBER, x + 80, y, 70, 21, ID_BUDDY);
    HWND ud = child(pg, 0, UPDOWN_CLASSW, nullptr, WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, ID_UPDOWN);
    SendMessageW(ud, UDM_SETBUDDY, (WPARAM)ed, 0);
    SendMessageW(ud, UDM_SETRANGE32, 0, 999); SendMessageW(ud, UDM_SETPOS32, 0, 42);
    y += 30;
    child(pg, 0, ST, L"Date:", WS_VISIBLE, x, y + 3, 70, 14, -1);
    child(pg, 0, DATETIMEPICK_CLASSW, nullptr, WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, x + 80, y, 200, 21, ID_DTP);
    y += 30;
    child(pg, 0, ST, L"Hotkey:", WS_VISIBLE, x, y + 3, 70, 14, -1);
    HWND hk = child(pg, WS_EX_CLIENTEDGE, HOTKEY_CLASSW, nullptr, WS_VISIBLE | WS_TABSTOP, x + 80, y, 200, 21, ID_HOTKEY);
    SendMessageW(hk, HKM_SETHOTKEY, MAKEWORD('E', HOTKEYF_CONTROL | HOTKEYF_ALT), 0);
    y += 30;
    child(pg, 0, ST, L"IP address:", WS_VISIBLE, x, y + 3, 70, 14, -1);
    HWND ip = child(pg, 0, WC_IPADDRESSW, nullptr, WS_VISIBLE | WS_TABSTOP, x + 80, y, 200, 21, ID_IPADDR);
    SendMessageW(ip, IPM_SETADDRESS, 0, MAKEIPADDRESS(192, 168, 0, 1));
    HWND mc = child(pg, 0, MONTHCAL_CLASSW, nullptr, WS_VISIBLE | WS_TABSTOP | MCS_WEEKNUMBERS, 318, 12, 0, 0, ID_MONTHCAL);
    RECT mr = {}; SendMessageW(mc, MCM_GETMINREQRECT, 0, (LPARAM)&mr);
    SetWindowPos(mc, nullptr, 0, 0, mr.right, mr.bottom, SWP_NOMOVE | SWP_NOZORDER);
    fprintf(stderr, "month calendar min rect %ldx%ld\n", (long)mr.right, (long)mr.bottom);
}

static LRESULT CALLBACK page_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT r; GetClientRect(h, &r);
        HTHEME th = GetWindowTheme(g_tab);
        if (th) DrawThemeBackground(th, dc, TABP_BODY, 0, &r, nullptr);
        else FillRect(dc, &r, (HBRUSH)(COLOR_3DFACE + 1));
        return 1;
    }
    case WM_CTLCOLORSTATIC:
        if (GetWindowTheme(g_tab)) {     // paint the tab body under the control, then draw transparently
            DrawThemeParentBackground((HWND)lp, (HDC)wp, nullptr);
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        break;
    case WM_TIMER:
        if (wp == 1) {
            HWND pb = GetDlgItem(h, ID_PROGRESS);
            if (SendMessageW(pb, PBM_GETPOS, 0, 0) >= 100) SendMessageW(pb, PBM_SETPOS, 0, 0);
            else SendMessageW(pb, PBM_STEPIT, 0, 0);
        }
        return 0;
    case WM_HSCROLL:
        if ((HWND)lp == GetDlgItem(h, ID_TRACK)) {
            char b[16]; snprintf(b, sizeof b, "%d", (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0));
            SetWindowTextW(GetDlgItem(h, ID_TRACKLABEL), W(w(b)));
            fprintf(stderr, "trackbar pos %s\n", b);
        }
        return 0;
    case WM_COMMAND:
        fprintf(stderr, "page WM_COMMAND id=%d code=%d\n", LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->code == DTN_DATETIMECHANGE) {
            NMDATETIMECHANGE* dc = (NMDATETIMECHANGE*)nm;
            fprintf(stderr, "DTN_DATETIMECHANGE %d-%02d-%02d\n", dc->st.wYear, dc->st.wMonth, dc->st.wDay);
        } else if (nm->code == MCN_SELCHANGE) {
            NMSELCHANGE* sc = (NMSELCHANGE*)nm;
            fprintf(stderr, "MCN_SELCHANGE %d-%02d-%02d\n", sc->stSelStart.wYear, sc->stSelStart.wMonth, sc->stSelStart.wDay);
        } else if (nm->code == UDN_DELTAPOS) {
            NMUPDOWN* ud = (NMUPDOWN*)nm;
            fprintf(stderr, "UDN_DELTAPOS pos=%d delta=%d\n", ud->iPos, ud->iDelta);
        } else if (nm->code == IPN_FIELDCHANGED) {
            NMIPADDRESS* ip = (NMIPADDRESS*)nm;
            fprintf(stderr, "IPN_FIELDCHANGED field=%d value=%d\n", ip->iField, ip->iValue);
        }
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void place_pages()
{
    RECT r; GetWindowRect(g_tab, &r);
    MapWindowPoints(nullptr, g_ctlwin, (POINT*)&r, 2);
    SendMessageW(g_tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&r);
    for (HWND pg : g_page) MoveWindow(pg, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}

static LRESULT CALLBACK ctl_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_ctlwin = h;
        g_tab = child(h, 0, WC_TABCONTROLW, nullptr, WS_VISIBLE | WS_CLIPSIBLINGS, 6, 6, 0, 0, ID_TAB);
        TCITEMW ti = {}; ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"Controls"; SendMessageW(g_tab, TCM_INSERTITEMW, 0, (LPARAM)&ti);
        ti.pszText = (LPWSTR)L"About"; SendMessageW(g_tab, TCM_INSERTITEMW, 1, (LPARAM)&ti);
        for (int i = 0; i < 2; i++)
            g_page[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"flxppage", nullptr, WS_CHILD | (i == 0 ? WS_VISIBLE : 0),
                                        0, 0, 0, 0, h, (HMENU)(intptr_t)(300 + i), nullptr, nullptr);
        for (HWND pg : g_page) SetWindowPos(pg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // above the tab, as propsheet.c does
        fill_page_controls(g_page[0]);
        child(g_page[1], 0, WC_STATICW, L"flxp common controls demo.\n\nThe file explorer window shows the rebar, toolbar, "
              L"ComboBoxEx, treeview, listview, header and status bar; this window shows the rest.",
              WS_VISIBLE, 12, 12, 400, 80, ID_ABOUT);
        SetTimer(g_page[0], 1, 60, nullptr);
        return 0;
    }
    case WM_SIZE: {
        RECT r; GetClientRect(h, &r);
        MoveWindow(g_tab, 6, 6, r.right - 12, r.bottom - 12, TRUE);
        place_pages();
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->hwndFrom == g_tab && nm->code == TCN_SELCHANGE) {
            int p = (int)SendMessageW(g_tab, TCM_GETCURSEL, 0, 0);
            fprintf(stderr, "TCN_SELCHANGE %d\n", p);
            for (int i = 0; i < 2; i++) ShowWindow(g_page[i], i == p ? SW_SHOW : SW_HIDE);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(g_main);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void CALLBACK quit_timer(HWND h, UINT, UINT_PTR, DWORD) { DestroyWindow(h); }

int main(int argc, char** argv)
{
    w32::init();
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_DATE_CLASSES | ICC_USEREX_CLASSES |
                                 ICC_INTERNET_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);
    g_small = make_imagelist(16);
    g_large = make_imagelist(32);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = main_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxpexplorer";
    RegisterClassW(&wc);
    wc.lpfnWndProc = ctl_proc;
    wc.lpszClassName = L"flxpcontrols";
    RegisterClassW(&wc);
    wc.lpfnWndProc = page_proc;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"flxppage";
    RegisterClassW(&wc);

    HWND h = CreateWindowExW(0, L"flxpexplorer", L"Local Disk (C:)", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             40, 40, 720, 500, nullptr, nullptr, nullptr, nullptr);
    g_ctlwin = CreateWindowExW(0, L"flxpcontrols", L"flxp controls", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
                               770, 40, 560, 330, nullptr, nullptr, nullptr, nullptr);
    ShowWindow(h, SW_SHOW);
    ShowWindow(g_ctlwin, SW_SHOW);
    SetFocus(g_list);
    if (argc > 1) SetTimer(h, 1234, atoi(argv[1]), quit_timer);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
