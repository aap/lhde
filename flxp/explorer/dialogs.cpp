// The explorer's dialogs; templates in rc/explorer.rc (embedded below).
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dialogs.h"
#include "resource.h"
#include "shell/apps.h"
#include "shell/bookmarks.h"
#include "shell/config.h"
#include "shell/fileops.h"
#include "shell/mime.h"
#include "shell/namespace.h"
#include "commands.h"
extern "C" {
#include "prsht.h"
}

asm(".section .rodata\n"
    ".global flxp_explorer_res_start\n.global flxp_explorer_res_end\n"
    "flxp_explorer_res_start:\n.incbin \"flxp/rc/explorer.res\"\nflxp_explorer_res_end:\n"
    ".previous\n");
extern "C" const unsigned char flxp_explorer_res_start[], flxp_explorer_res_end[];

namespace dialogs {

HINSTANCE module()
{
    static res::Module* m;
    if (!m) m = res::load_memory(flxp_explorer_res_start, (size_t)(flxp_explorer_res_end - flxp_explorer_res_start));
    return (HINSTANCE)m;
}

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
#define WS(s) ((LPCWSTR)W(s).c_str())

static void set_text(HWND dlg, int id, const std::string& s) { SetDlgItemTextW(dlg, id, WS(s)); }

static std::string get_text(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::vector<WCHAR> buf((size_t)n + 1);
    GetWindowTextW(h, buf.data(), n + 1);
    return w32::utf8_of(buf.data());
}

// ---- Open With ------------------------------------------------------------------

namespace openwith {

struct State {
    std::string file, mime;
    HICON icon;
    HIMAGELIST small;
    int app_image;
    bool always;
    OpenWith* out;
};
State* s;

static HTREEITEM add(HWND tree, HTREEITEM parent, const std::string& text, const shell::App* app)
{
    TVINSERTSTRUCTW ins = {};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    std::u16string t = W(text);
    ins.item.pszText = (LPWSTR)t.c_str();
    ins.item.lParam = (LPARAM)app;
    ins.item.iImage = ins.item.iSelectedImage = app ? s->app_image : 30000;   // out of range: the group nodes draw no icon
    return (HTREEITEM)SendMessageW(tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
}

static const shell::App* selected(HWND dlg)
{
    HWND tree = GetDlgItem(dlg, IDC_OW_TREE);
    HTREEITEM h = (HTREEITEM)SendMessageW(tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
    if (!h) return nullptr;
    TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = h;
    SendMessageW(tree, TVM_GETITEMW, 0, (LPARAM)&tvi);
    return (const shell::App*)tvi.lParam;
}

static bool finish(HWND dlg)
{
    const shell::App* a = selected(dlg);
    if (!a) { MessageBeep(0); return false; }
    s->out->app_id = a->id;
    s->out->always = IsDlgButtonChecked(dlg, IDC_OW_ALWAYS) == BST_CHECKED;
    EndDialog(dlg, IDOK);
    return true;
}

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        if (s->icon) SendDlgItemMessageW(dlg, IDC_OW_ICON, STM_SETICON, (WPARAM)s->icon, 0);
        set_text(dlg, IDC_OW_FILE, s->file);
        std::string desc = shell::mime_comment(s->mime);
        set_text(dlg, IDC_OW_TYPE, "Type: " + (desc.empty() ? s->mime : desc + " (" + s->mime + ")"));
        CheckDlgButton(dlg, IDC_OW_ALWAYS, s->always ? BST_CHECKED : BST_UNCHECKED);
        HWND tree = GetDlgItem(dlg, IDC_OW_TREE);
        SendMessageW(tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)s->small);
        std::vector<const shell::App*> rec = shell::apps_for(s->mime);
        HTREEITEM first = nullptr;
        if (!rec.empty()) {
            HTREEITEM r = add(tree, TVI_ROOT, "Recommended Programs:", nullptr);
            for (const shell::App* a : rec) { HTREEITEM h = add(tree, r, a->name, a); if (!first) first = h; }
            SendMessageW(tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)r);
        }
        HTREEITEM o = add(tree, TVI_ROOT, "Other Programs:", nullptr);
        for (const shell::App& a : shell::all_apps()) {
            if (a.no_display) continue;
            bool dup = false;
            for (const shell::App* r : rec) if (r == &a) dup = true;
            if (dup) continue;
            HTREEITEM h = add(tree, o, a.name, &a);
            if (!first) first = h;
        }
        SendMessageW(tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)o);
        if (first) SendMessageW(tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)first);
        SendMessageW(tree, TVM_ENSUREVISIBLE, 0, (LPARAM)SendMessageW(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0));
        SetFocus(tree);
        return FALSE;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_OW_TREE && nm->code == NM_DBLCLK && selected(dlg)) { finish(dlg); return TRUE; }
        if (nm->idFrom == IDC_OW_TREE && nm->code == TVN_SELCHANGEDW) EnableWindow(GetDlgItem(dlg, IDOK), selected(dlg) != nullptr);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { finish(dlg); return TRUE; }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        return FALSE;
    }
    return FALSE;
}

} // namespace openwith

bool choose_program(HWND owner, const std::string& file_name, const std::string& mime,
                    HICON file_icon, HIMAGELIST small, int app_image, bool always_default, OpenWith* out)
{
    openwith::State st = { file_name, mime, file_icon, small, app_image, always_default, out };
    openwith::s = &st;
    INT_PTR r = DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_OPENWITH), owner, openwith::proc, 0);
    openwith::s = nullptr;
    return r == IDOK;
}

// ---- message boxes -------------------------------------------------------------------

struct MsgState { std::string title, text; HICON icon; };
static MsgState* g_msg;

static INT_PTR CALLBACK msg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        if (!g_msg->title.empty()) SetWindowTextW(dlg, WS(g_msg->title));
        if (g_msg->icon) SendDlgItemMessageW(dlg, GetDlgItem(dlg, IDC_CD_ICON) ? IDC_CD_ICON : IDC_MSG_ICON, STM_SETICON, (WPARAM)g_msg->icon, 0);
        set_text(dlg, GetDlgItem(dlg, IDC_CD_TEXT) ? IDC_CD_TEXT : IDC_MSG_TEXT, g_msg->text);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDYES || LOWORD(wp) == IDNO) {
            EndDialog(dlg, LOWORD(wp) == IDCANCEL && GetDlgItem(dlg, IDNO) ? IDNO : LOWORD(wp));
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

bool confirm_delete(HWND owner, const std::vector<std::string>& names, HICON icon)
{
    MsgState st;
    if (names.size() == 1) {
        st.title = "Confirm File Delete";
        st.text = "Are you sure you want to send '" + names[0] + "' to the Trash?";
    } else {
        st.title = "Confirm Multiple File Delete";
        st.text = "Are you sure you want to send these " + std::to_string(names.size()) + " items to the Trash?";
    }
    st.icon = icon;
    g_msg = &st;
    INT_PTR r = DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_CONFIRMDELETE), owner, msg_proc, 0);
    g_msg = nullptr;
    return r == IDYES;
}

void message(HWND owner, const std::string& title, const std::string& text, bool error)
{
    MsgState st = { title, text, LoadIconW(nullptr, (LPCWSTR)(error ? IDI_ERROR : IDI_INFORMATION)) };
    g_msg = &st;
    DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_MESSAGE), owner, msg_proc, 0);
    g_msg = nullptr;
}

// ---- Organize Bookmarks ----------------------------------------------------------------

namespace organize {

std::vector<shell::Bookmark> list;
HIMAGELIST g_small;
int g_folder;
bool g_filling;

static int current(HWND dlg) { return (int)SendDlgItemMessageW(dlg, IDC_BM_LIST, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); }

static void fill(HWND dlg, int select)
{
    HWND lv = GetDlgItem(dlg, IDC_BM_LIST);
    g_filling = true;
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < list.size(); i++) {
        std::u16string name = W(list[i].display_name());
        std::string p = list[i].path();
        std::u16string loc = W(p.empty() ? list[i].uri : p);
        LVITEMW it = {};
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)name.c_str();
        it.iImage = g_folder;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        it.mask = LVIF_TEXT; it.iSubItem = 1; it.pszText = (LPWSTR)loc.c_str();
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&it);
    }
    if (select >= 0 && select < (int)list.size()) {
        LVITEMW st = {}; st.stateMask = LVIS_SELECTED | LVIS_FOCUSED; st.state = st.stateMask;
        SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)select, (LPARAM)&st);
        SendMessageW(lv, LVM_ENSUREVISIBLE, (WPARAM)select, FALSE);
    }
    g_filling = false;
}

static void sync_buttons(HWND dlg)
{
    int i = current(dlg);
    EnableWindow(GetDlgItem(dlg, IDC_BM_UP), i > 0);
    EnableWindow(GetDlgItem(dlg, IDC_BM_DOWN), i >= 0 && i + 1 < (int)list.size());
    EnableWindow(GetDlgItem(dlg, IDC_BM_REMOVE), i >= 0);
    EnableWindow(GetDlgItem(dlg, IDC_BM_RENAME), i >= 0);
    EnableWindow(GetDlgItem(dlg, IDC_BM_NAME), i >= 0);
    g_filling = true;
    set_text(dlg, IDC_BM_NAME, i >= 0 ? list[(size_t)i].display_name() : "");
    g_filling = false;
}

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND lv = GetDlgItem(dlg, IDC_BM_LIST);
        SendMessageW(lv, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_small);
        SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
        RECT rc; GetClientRect(lv, &rc);
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)L"Name"; c.cx = (rc.right - rc.left) * 2 / 5;
        SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&c);
        c.pszText = (LPWSTR)L"Location"; c.cx = (rc.right - rc.left) - c.cx - GetSystemMetrics(SM_CXVSCROLL);
        SendMessageW(lv, LVM_INSERTCOLUMNW, 1, (LPARAM)&c);
        fill(dlg, list.empty() ? -1 : 0);
        sync_buttons(dlg);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_BM_LIST && nm->code == LVN_ITEMCHANGED && !g_filling) sync_buttons(dlg);
        return FALSE;
    }
    case WM_COMMAND: {
        int i = current(dlg);
        switch (LOWORD(wp)) {
        case IDC_BM_UP:
            if (i > 0) { std::swap(list[(size_t)i], list[(size_t)i - 1]); fill(dlg, i - 1); sync_buttons(dlg); }
            return TRUE;
        case IDC_BM_DOWN:
            if (i >= 0 && i + 1 < (int)list.size()) { std::swap(list[(size_t)i], list[(size_t)i + 1]); fill(dlg, i + 1); sync_buttons(dlg); }
            return TRUE;
        case IDC_BM_REMOVE:
            if (i >= 0) { list.erase(list.begin() + i); fill(dlg, i < (int)list.size() ? i : i - 1); sync_buttons(dlg); }
            return TRUE;
        case IDC_BM_RENAME: {
            HWND e = GetDlgItem(dlg, IDC_BM_NAME);
            SetFocus(e);
            SendMessageW(e, EM_SETSEL, 0, -1);
            return TRUE;
        }
        case IDC_BM_NAME:
            if (HIWORD(wp) == EN_CHANGE && !g_filling && i >= 0) {
                // the label follows the edit; the folder's own name means "no label"
                std::string name = get_text((HWND)lp);
                std::string p = list[(size_t)i].path();
                list[(size_t)i].label = (!p.empty() && name == shell::base_of(p)) ? "" : name;
                LVITEMW it = {}; it.iSubItem = 0;
                std::u16string t = W(list[(size_t)i].display_name());
                it.pszText = (LPWSTR)t.c_str();
                g_filling = true;   // no LVN_ITEMCHANGED round trip into the edit (it would reset the caret)
                SendDlgItemMessageW(dlg, IDC_BM_LIST, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&it);
                g_filling = false;
            }
            return TRUE;
        case IDOK: EndDialog(dlg, IDOK); return TRUE;
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

} // namespace organize

bool organize_bookmarks(HWND owner, HIMAGELIST small, int folder_image)
{
    organize::list = shell::load_bookmarks();
    organize::g_small = small;
    organize::g_folder = folder_image;
    INT_PTR r = DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_BOOKMARKS), owner, organize::proc, 0);
    if (r != IDOK) return false;
    if (!shell::save_bookmarks(organize::list)) { message(owner, "Organize Bookmarks", "Cannot write " + shell::bookmarks_path() + "."); return false; }
    return true;
}

// ---- Properties ------------------------------------------------------------------------

namespace props {

struct State {
    std::string path, name, type;
    HICON icon;
    HIMAGELIST small;
    int app_image;
};
State* s;

static std::string long_date(int64_t t)
{
    // XP: "Friday, October 23, 2026, 1:45:12 PM"
    time_t tt = (time_t)t;
    struct tm tm; localtime_r(&tt, &tm);
    char buf[96];
    int h = tm.tm_hour % 12; if (!h) h = 12;
    char day[48]; strftime(day, sizeof day, "%A, %B %e, %Y", &tm);
    std::string d = day;
    for (size_t k; (k = d.find("  ")) != std::string::npos;) d.erase(k, 1);
    snprintf(buf, sizeof buf, "%s, %d:%02d:%02d %s", d.c_str(), h, tm.tm_min, tm.tm_sec, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

static std::string commas(uint64_t n)
{
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}

static std::string size_text(uint64_t b)
{
    char buf[64];
    if (b < 1024) snprintf(buf, sizeof buf, "%llu bytes", (unsigned long long)b);
    else if (b < 1024 * 1024) snprintf(buf, sizeof buf, "%.1f KB (%s bytes)", b / 1024.0, commas(b).c_str());
    else if (b < 1024ULL * 1024 * 1024) snprintf(buf, sizeof buf, "%.1f MB (%s bytes)", b / 1048576.0, commas(b).c_str());
    else snprintf(buf, sizeof buf, "%.2f GB (%s bytes)", b / 1073741824.0, commas(b).c_str());
    return buf;
}

static void show_opens_with(HWND dlg, const std::string& mime)
{
    const shell::App* a = shell::default_app(mime);
    set_text(dlg, IDC_PR_OPENS, a ? a->name : "(none)");
}

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG: {
        if (s->icon) SendDlgItemMessageW(dlg, IDC_PR_ICON, STM_SETICON, (WPARAM)s->icon, 0);
        set_text(dlg, IDC_PR_NAME, s->name);
        shell::FileInfo fi = shell::file_info(s->path, true);
        std::string mime = shell::mime_type(s->path);
        std::string desc = s->type.empty() ? shell::mime_comment(mime) : s->type;
        set_text(dlg, IDC_PR_TYPE, desc + " (" + mime + ")");
        if (fi.is_dir) {
            ShowWindow(GetDlgItem(dlg, IDC_PR_OPENSL), SW_HIDE);
            ShowWindow(GetDlgItem(dlg, IDC_PR_OPENS), SW_HIDE);
            ShowWindow(GetDlgItem(dlg, IDC_PR_CHANGE), SW_HIDE);
            set_text(dlg, IDC_PR_CONTAINS, commas(fi.files) + " Files, " + commas(fi.folders) + " Folders");
        } else {
            show_opens_with(dlg, mime);
            ShowWindow(GetDlgItem(dlg, IDC_PR_CONTAINSL), SW_HIDE);
        }
        set_text(dlg, IDC_PR_LOCATION, shell::dir_of(s->path));
        set_text(dlg, IDC_PR_SIZE, fi.ok ? size_text(fi.size) : "");
        if (fi.ok) {
            set_text(dlg, IDC_PR_MODIFIED, long_date(fi.mtime));
            set_text(dlg, IDC_PR_ACCESSED, long_date(fi.atime));
            set_text(dlg, IDC_PR_CHANGED, long_date(fi.ctime));
            set_text(dlg, IDC_PR_PERMS, fi.perms + "   " + fi.owner + ":" + fi.group);
        }
        if (fi.is_link) { set_text(dlg, IDC_PR_EXTRAL, "Link target:"); set_text(dlg, IDC_PR_EXTRA, fi.link_target); }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PR_CHANGE) {
            std::string mime = shell::mime_type(s->path);
            OpenWith ow;
            if (choose_program(GetParent(dlg), s->name, mime, s->icon, s->small, s->app_image, true, &ow)) {
                std::string err;
                if (!shell::set_default_app(mime, ow.app_id, &err)) message(GetParent(dlg), "Properties", err);
                show_opens_with(dlg, mime);
            }
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

} // namespace props

void properties(HWND owner, const std::string& path, const std::string& display_name, const std::string& type_name,
                HICON icon, HIMAGELIST small, int app_image)
{
    props::State st = { path, display_name, type_name, icon, small, app_image };
    props::s = &st;
    PROPSHEETPAGEW page = {};
    page.dwSize = sizeof page;
    page.dwFlags = PSP_DEFAULT;
    page.hInstance = module();
    page.pszTemplate = MAKEINTRESOURCEW(IDD_PROPERTIES);
    page.pfnDlgProc = props::proc;
    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof psh;
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_PROPTITLE;
    psh.hwndParent = owner;
    psh.hInstance = module();
    std::u16string cap = W(display_name);
    psh.pszCaption = (LPCWSTR)cap.c_str();
    psh.nPages = 1;
    psh.ppsp = &page;
    PropertySheetW(&psh);
    props::s = nullptr;
}

// ---- Folder Options ---------------------------------------------------------------

namespace folderopts {

FolderOptions* o;
HWND general, view;                    // the pages (General refreshed after Apply/Reset All)

struct ViewName { int view; const char* name; bool xp, lh; };
static const ViewName k_views[] = {
    { VIEW_THUMBNAILS, "Thumbnails", true, false }, { LVS_REPORT, "Details", false, true }, { VIEW_TILES, "Tiles", true, true },
    { LVS_ICON, "Icons", true, true }, { LVS_SMALLICON, "Small Icons", true, false },
    { LVS_LIST, "List", true, false }, { LVS_REPORT, "Details", true, false },
};

// the controls from a view code
static void show_view(HWND dlg, int v)
{
    int zoom = (v >> ZOOM_SHIFT) & 0x1ff;
    v &= ZOOM_VIEW & ~(VIEW_THUMBS_FLAG | VIEW_NONAMES_FLAG);
    if (o->longhorn && (v == VIEW_THUMBNAILS || v == LVS_SMALLICON)) v = LVS_ICON;
    if (o->longhorn && v == LVS_LIST) v = LVS_REPORT;
    HWND cb = GetDlgItem(dlg, IDC_FO_VIEW);
    int n = (int)SendMessageW(cb, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++)
        if ((int)SendMessageW(cb, CB_GETITEMDATA, (WPARAM)i, 0) == v) SendMessageW(cb, CB_SETCURSEL, (WPARAM)i, 0);
    HWND zc = GetDlgItem(dlg, IDC_FO_ZOOM);
    int best = 0, nz = (int)SendMessageW(zc, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < nz; i++)
        if ((int)SendMessageW(zc, CB_GETITEMDATA, (WPARAM)i, 0) == zoom) best = i;
    SendMessageW(zc, CB_SETCURSEL, (WPARAM)best, 0);
}

// and back
static int chosen_view(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_FO_VIEW);
    int i = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
    int v = i >= 0 ? (int)SendMessageW(cb, CB_GETITEMDATA, (WPARAM)i, 0) : LVS_REPORT;
    if (!o->longhorn) return v;
    HWND zc = GetDlgItem(dlg, IDC_FO_ZOOM);
    int zi = (int)SendMessageW(zc, CB_GETCURSEL, 0, 0);
    int zoom = zi >= 0 ? (int)SendMessageW(zc, CB_GETITEMDATA, (WPARAM)zi, 0) : 0;
    return v | zoom << ZOOM_SHIFT;   // (a size the view cannot have: the one it remembers)
}

static void take(HWND, bool from_general)
{
    if (from_general && general) {
        o->default_view = chosen_view(general);
        if (o->longhorn) o->thumbnails = IsDlgButtonChecked(general, IDC_FO_THUMBS) == BST_CHECKED;
    }
    if (view) {
        o->remember = IsDlgButtonChecked(view, IDC_FO_REMEMBER) == BST_CHECKED;
        o->hidden = IsDlgButtonChecked(view, IDC_FO_HIDDEN) == BST_CHECKED;
    }
}

static INT_PTR CALLBACK general_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        general = dlg;
        HWND cb = GetDlgItem(dlg, IDC_FO_VIEW);
        for (const ViewName& v : k_views) {
            if (o->longhorn ? !v.lh : !v.xp) continue;   // the frame's views (the Longhorn frame: thumbnails a setting, below)
            int i = (int)SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)WS(v.name));
            SendMessageW(cb, CB_SETITEMDATA, (WPARAM)i, (LPARAM)v.view);
        }
        HWND zc = GetDlgItem(dlg, IDC_FO_ZOOM);
        for (int z : o->zoom_levels) {
            int i = (int)SendMessageW(zc, CB_ADDSTRING, 0, (LPARAM)WS(z ? std::to_string(z) + " x " + std::to_string(z) + " pixels" : std::string("Default")));
            SendMessageW(zc, CB_SETITEMDATA, (WPARAM)i, (LPARAM)z);
        }
        for (int id : { IDC_FO_ZOOML, IDC_FO_ZOOM, IDC_FO_THUMBS }) EnableWindow(GetDlgItem(dlg, id), o->longhorn);
        CheckDlgButton(dlg, IDC_FO_THUMBS, o->longhorn && o->thumbnails ? BST_CHECKED : BST_UNCHECKED);
        show_view(dlg, o->default_view);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_FO_RESTORE) { show_view(dlg, o->builtin_default); SendMessageW(GetParent(dlg), PSM_CHANGED, (WPARAM)dlg, 0); }
        else if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED) SendMessageW(GetParent(dlg), PSM_CHANGED, (WPARAM)dlg, 0);
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->code == PSN_SETACTIVE) show_view(dlg, o->default_view);   // (Apply to All on the other page)
        else if (n->code == PSN_KILLACTIVE) o->default_view = chosen_view(dlg);
        else if (n->code == PSN_APPLY) {
            take(dlg, true);
            if (o->applied) o->applied(*o);
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
        }
        return TRUE;
    }
    case WM_DESTROY: general = nullptr; return FALSE;
    }
    return FALSE;
}

static bool ask(HWND dlg, const char* text)
{
    return MessageBoxW(dlg, WS(text), L"Folder Views", MB_YESNO | MB_ICONQUESTION) == IDYES;
}

static INT_PTR CALLBACK view_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        view = dlg;
        CheckDlgButton(dlg, IDC_FO_REMEMBER, o->remember ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_FO_HIDDEN, o->hidden ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_FO_APPLYALL) {
            // XP's question (shell32 IDS_APPLYALLFOLDERS... wording from memory)
            if (ask(dlg, "This will cause all folders to match the current folder's view settings. Do you want to continue?")) {
                o->default_view = o->current_view;
                if (o->apply_all) o->apply_all(*o);
            }
        } else if (LOWORD(wp) == IDC_FO_RESETALL) {
            if (ask(dlg, "This will reset all of your folders to the default view settings. Do you want to continue?")) {
                o->default_view = o->builtin_default;
                if (o->reset_all) o->reset_all(*o);
            }
        } else if (HIWORD(wp) == BN_CLICKED) SendMessageW(GetParent(dlg), PSM_CHANGED, (WPARAM)dlg, 0);
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->code == PSN_APPLY) {
            take(dlg, general != nullptr);
            if (o->applied && !general) o->applied(*o);   // (the General page calls it when it exists)
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
        }
        return TRUE;
    }
    case WM_DESTROY: view = nullptr; return FALSE;
    }
    return FALSE;
}

} // namespace folderopts

void folder_options(HWND owner, FolderOptions& o)
{
    folderopts::o = &o;
    PROPSHEETPAGEW pages[2] = {};
    for (int i = 0; i < 2; i++) {
        pages[i].dwSize = sizeof pages[i];
        pages[i].dwFlags = PSP_DEFAULT;
        pages[i].hInstance = module();
    }
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_FO_GENERAL);
    pages[0].pfnDlgProc = folderopts::general_proc;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_FO_VIEW);
    pages[1].pfnDlgProc = folderopts::view_proc;
    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof psh;
    psh.dwFlags = PSH_PROPSHEETPAGE;
    psh.hwndParent = owner;
    psh.hInstance = module();
    psh.pszCaption = L"Folder Options";
    psh.nPages = 2;
    psh.ppsp = pages;
    PropertySheetW(&psh);
    folderopts::o = nullptr;
}

} // namespace dialogs
