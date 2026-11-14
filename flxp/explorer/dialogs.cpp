// The explorer's dialogs; templates in rc/explorer.rc (embedded below).
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include "dialogs.h"
#include "resource.h"
#include "shell/apps.h"
#include "shell/bookmarks.h"
#include "shell/config.h"
#include "shell/fileops.h"
#include "shell/mime.h"
#include "shell/namespace.h"
#include "shell/roles.h"
#include "shell/ui.h"
#include "commands.h"
#include "uiicons.h"
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
    HIMAGELIST apps;   // the programs' icons (shell::app_hicon), the dialog's own
};
State* s;

// a program's 16 px icon in the dialog's list: the shared lookup (its desktop file's Icon=, the class map, the generic
// application icon), else the caller's application image
static int app_image(const shell::App* app)
{
    HICON h = shell::app_hicon(app->id, 16);
    if (!h) h = ImageList_GetIcon(s->small, s->app_image, ILD_NORMAL);
    int idx = h ? ImageList_AddIcon(s->apps, h) : -1;
    if (h) DestroyIcon(h);
    return idx >= 0 ? idx : 30000;
}

static HTREEITEM add(HWND tree, HTREEITEM parent, const std::string& text, const shell::App* app)
{
    TVINSERTSTRUCTW ins = {};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    std::u16string t = W(text);
    ins.item.pszText = (LPWSTR)t.c_str();
    ins.item.lParam = (LPARAM)app;
    ins.item.iImage = ins.item.iSelectedImage = app ? app_image(app) : 30000;   // out of range: the group nodes draw no icon
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
        SendMessageW(tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)s->apps);
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
    openwith::State st = { file_name, mime, file_icon, small, app_image, always_default, out,
                           ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16) };
    openwith::s = &st;
    INT_PTR r = DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_OPENWITH), owner, openwith::proc, 0);
    openwith::s = nullptr;
    ImageList_Destroy(st.apps);
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
        if (g_msg->icon) SendDlgItemMessageW(dlg, IDC_CD_ICON, STM_SETICON, (WPARAM)g_msg->icon, 0);
        set_text(dlg, IDC_CD_TEXT, g_msg->text);
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
    shell::message_box(owner, title, text, MB_OK | (error ? MB_ICONHAND : MB_ICONINFORMATION));
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
        o->full_path = IsDlgButtonChecked(view, IDC_FO_FULLPATH) == BST_CHECKED;
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
        CheckDlgButton(dlg, IDC_FO_FULLPATH, o->full_path ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_FO_APPLYALL) {
            // XP's question
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

// ---- File Types ----------------------------------------------------------------------
// XP's page over the freedesktop files: one row per registered extension (shared-mime-info's "*.EXT" globs), its type's
// description as the Details view names it; the details of the selected one: the program that opens it (Change... is the
// Open With dialog, written to the user's mimeapps.list at once, as XP's), where that comes from, XP's sentence, and
// Advanced (Edit File Type: the program's actions) or Restore when the user changed something (the user's lines for the
// type, flxp's copy of the program's desktop file). New and Delete: the user's own extensions.

namespace filetypes {

struct Row { std::string ext, type; };
std::vector<Row> rows;
HWND page;
std::u16string text_buf;            // LVN_GETDISPINFO's text lives until the next call
bool restore_mode;

static int current() { return (int)SendDlgItemMessageW(page, IDC_FT_LIST, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); }
static std::string upper(std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; }
static std::string type_label(const Row& r) { return shell::type_name("x." + r.ext, false); }

static void fill(const std::string& select_ext)
{
    rows.clear();
    for (const shell::Extension& e : shell::mime_extensions()) rows.push_back({ e.ext, e.type });
    HWND lv = GetDlgItem(page, IDC_FT_LIST);
    SendMessageW(lv, LVM_SETITEMCOUNT, rows.size(), 0);
    int sel = -1;
    for (size_t i = 0; i < rows.size(); i++) if (rows[i].ext == select_ext) sel = (int)i;
    if (sel < 0 && !rows.empty()) sel = 0;
    if (sel >= 0) {
        LVITEMW it = {}; it.stateMask = LVIS_SELECTED | LVIS_FOCUSED; it.state = it.stateMask;
        SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)sel, (LPARAM)&it);
        SendMessageW(lv, LVM_ENSUREVISIBLE, (WPARAM)sel, FALSE);
    }
    InvalidateRect(lv, nullptr, TRUE);
}

static void details()
{
    int i = current();
    bool have = i >= 0 && (size_t)i < rows.size();
    for (int id : { IDC_FT_CHANGE, IDC_FT_ADVANCED }) EnableWindow(GetDlgItem(page, id), have);
    bool own = false;
    if (have) for (const shell::Extension& e : shell::user_extensions()) if (e.ext == rows[(size_t)i].ext) own = true;
    EnableWindow(GetDlgItem(page, IDC_FT_DELETE), own);
    if (!have) {
        set_text(page, IDC_FT_DETAILS, "Details");
        for (int id : { IDC_FT_APPNAME, IDC_FT_SETBY, IDC_FT_DESC }) set_text(page, id, "");
        return;
    }
    const Row& r = rows[(size_t)i];
    std::string EXT = upper(r.ext), label = type_label(r);
    set_text(page, IDC_FT_DETAILS, "Details for '" + EXT + "' extension");
    shell::AppSource s = shell::default_source(r.type);
    set_text(page, IDC_FT_APPNAME, s.app ? s.app->name : "Unknown application");
    HICON hi = s.app ? shell::app_hicon(s.app->id, 16) : nullptr;
    if (s.app && !hi) hi = ImageList_GetIcon(uiicons::small(), uiicons::index("exe"), ILD_NORMAL);   // a program without an icon
    HICON old = (HICON)SendDlgItemMessageW(page, IDC_FT_APPICON, STM_SETICON, (WPARAM)hi, 0);
    if (old) DestroyIcon(old);
    std::string by;
    std::string file = s.file;
    std::string home = shell::home_dir();
    if (!home.empty() && file.compare(0, home.size() + 1, home + "/") == 0) file = "~" + file.substr(home.size());
    const shell::Role* role = s.layer == "role" ? shell::role_of(s.type) : nullptr;
    if (s.layer == "role") {
        std::string name = s.app ? s.app->name : "";
        if (role) by = (shell::role_choice(*role).user ? "your " : "lhde's ") + role->id + " role (" + name + ")";
        else by = "a role (" + file + ")";
    }
    else if (s.layer == "user") by = "your choice (" + file + ")";
    else if (s.layer == "lhde") by = "lhde (" + file + ")";
    else if (s.layer == "desktop") by = "another desktop's defaults (" + file + ")";
    else if (s.layer == "system") by = "the distribution (" + file + ")";
    else if (s.layer == "claimant") by = "nothing, the first claimant of " + s.type + " (" + file + ")";
    else by = "nothing; no program claims " + r.type;
    if (s.app && s.type != shell::mime_canonical(r.type) && s.layer != "claimant") by += ", for " + s.type;
    set_text(page, IDC_FT_SETBY, "Set by: " + by);
    restore_mode = shell::user_default_set(r.type) || (s.app && shell::app_overridden(*s.app));
    std::string desc = "Files with extension '" + EXT + "' are of type '" + label + "' (" + r.type + "). ";
    if (restore_mode) desc += "You have changed how they open. To go back to the defaults, click Restore.";
    else desc += "To change settings that affect all '" + label + "' files, click Advanced.";
    set_text(page, IDC_FT_DESC, desc);
    set_text(page, IDC_FT_ADVANCED, restore_mode ? "&Restore" : "Ad&vanced");
    if (getenv("FLXP_TRACE_FILETYPES"))
        printf("filetypes: %s %s %s %s%s%s\n", EXT.c_str(), r.type.c_str(), s.app ? s.app->id.c_str() : "-", s.layer.empty() ? "-" : s.layer.c_str(),
               restore_mode ? " restore" : "", role ? (" [" + by + "]").c_str() : ""), fflush(stdout);
}

// ---- New Action / Edit Action ----
struct ActionAsk { std::string title, name, exec; };
ActionAsk* ask_action;

static INT_PTR CALLBACK action_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowTextW(dlg, WS(ask_action->title));
        set_text(dlg, IDC_AC_ACTION, ask_action->name);
        set_text(dlg, IDC_AC_APP, ask_action->exec);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            std::string n = shell::trim(get_text(GetDlgItem(dlg, IDC_AC_ACTION))), e = shell::trim(get_text(GetDlgItem(dlg, IDC_AC_APP)));
            if (n.empty() || e.empty()) { MessageBeep(0); return TRUE; }
            ask_action->name = n; ask_action->exec = e;
            EndDialog(dlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static bool edit_action(HWND owner, ActionAsk& a)
{
    ask_action = &a;
    bool ok = DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_FT_ACTION), owner, action_proc, 0) == IDOK;
    ask_action = nullptr;
    return ok;
}

// ---- Edit File Type: the default program's actions ----
struct EditType { Row row; const shell::App* app; std::vector<shell::AppAction> acts; bool changed; HFONT bold; };
EditType* et;

static std::string plain(const std::string& s) { std::string r; for (char c : s) if (c != '&') r += c; return r; }

static void et_fill(HWND dlg, int sel)
{
    HWND lb = GetDlgItem(dlg, IDC_ET_ACTIONS);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (const shell::AppAction& a : et->acts) SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)WS(plain(a.name)));
    SendMessageW(lb, LB_SETCURSEL, (WPARAM)sel, 0);
    bool app = et->app != nullptr;
    EnableWindow(GetDlgItem(dlg, IDC_ET_NEW), app);
    EnableWindow(GetDlgItem(dlg, IDC_ET_EDIT), app && sel >= 0);
    EnableWindow(GetDlgItem(dlg, IDC_ET_REMOVE), app && sel > 0);     // the program's own command stays
    EnableWindow(GetDlgItem(dlg, IDC_ET_DEFAULT), app && sel > 0);
}

static std::string action_id(const std::string& name)
{
    std::string id;
    for (char c : plain(name)) id += isalnum((unsigned char)c) ? (char)tolower((unsigned char)c) : '-';
    if (id.empty()) id = "action";
    std::string base = id;
    for (int n = 2;; n++) {
        bool taken = false;
        for (const shell::AppAction& a : et->acts) if (a.id == id) taken = true;
        if (!taken) return id;
        id = base + "-" + std::to_string(n);
    }
}

static INT_PTR CALLBACK edittype_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        int img = uiicons::index(shell::icon_key("x." + et->row.ext, false));
        HICON ic = img >= 0 ? uiicons::large_icon(shell::icon_key("x." + et->row.ext, false)) : nullptr;
        if (ic) SendDlgItemMessageW(dlg, IDC_ET_ICON, STM_SETICON, (WPARAM)ic, 0);
        set_text(dlg, IDC_ET_TEXT, type_label(et->row));
        set_text(dlg, IDC_ET_PROGRAM, et->app ? "(" + et->app->name + ", " + et->app->id + ")" : "(no program opens this type)");
        HFONT f = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);
        LOGFONTW lf = {};
        if (f && GetObjectW(f, sizeof lf, &lf)) { lf.lfWeight = FW_BOLD; et->bold = CreateFontIndirectW(&lf); }
        et_fill(dlg, 0);
        return TRUE;
    }
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT*)lp)->itemHeight = 16; return TRUE;
    case WM_DRAWITEM: {
        // XP draws the default action bold
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (d->itemID == (UINT)-1) return TRUE;
        bool sel = (d->itemState & ODS_SELECTED) != 0;
        FillRect(d->hDC, &d->rcItem, GetSysColorBrush(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetTextColor(d->hDC, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        SetBkMode(d->hDC, TRANSPARENT);
        HGDIOBJ of = SelectObject(d->hDC, d->itemID == 0 && et->bold ? (HGDIOBJ)et->bold : (HGDIOBJ)SendMessageW(d->hwndItem, WM_GETFONT, 0, 0));
        RECT r = d->rcItem; r.left += 2;
        if (d->itemID < et->acts.size()) DrawTextW(d->hDC, WS(plain(et->acts[d->itemID].name)), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(d->hDC, of);
        if (d->itemState & ODS_FOCUS) DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    case WM_COMMAND: {
        int sel = (int)SendDlgItemMessageW(dlg, IDC_ET_ACTIONS, LB_GETCURSEL, 0, 0);
        switch (LOWORD(wp)) {
        case IDC_ET_ACTIONS:
            if (HIWORD(wp) == LBN_SELCHANGE) et_fill(dlg, sel);
            if (HIWORD(wp) == LBN_DBLCLK) SendMessageW(dlg, WM_COMMAND, IDC_ET_EDIT, 0);
            return TRUE;
        case IDC_ET_NEW: {
            ActionAsk a = { "New Action", "", "" };
            if (!edit_action(dlg, a)) return TRUE;
            et->acts.push_back({ action_id(a.name), a.name, a.exec });
            et->changed = true;
            et_fill(dlg, (int)et->acts.size() - 1);
            return TRUE;
        }
        case IDC_ET_EDIT: {
            if (sel < 0 || (size_t)sel >= et->acts.size()) return TRUE;
            ActionAsk a = { "Editing action for type: " + type_label(et->row), plain(et->acts[(size_t)sel].name), et->acts[(size_t)sel].exec };
            if (!edit_action(dlg, a)) return TRUE;
            if (sel > 0) et->acts[(size_t)sel].name = a.name;
            et->acts[(size_t)sel].exec = a.exec;
            et->changed = true;
            et_fill(dlg, sel);
            return TRUE;
        }
        case IDC_ET_REMOVE:
            if (sel <= 0) return TRUE;
            if (MessageBoxW(dlg, L"Are you sure you want to remove this action?", L"Edit File Type", MB_YESNO | MB_ICONQUESTION) != IDYES) return TRUE;
            et->acts.erase(et->acts.begin() + sel);
            et->changed = true;
            et_fill(dlg, sel - 1);
            return TRUE;
        case IDC_ET_DEFAULT: {
            // the program's command becomes the chosen action's; the old one stays as an action
            if (sel <= 0) return TRUE;
            shell::AppAction was = et->acts[0], chosen = et->acts[(size_t)sel];
            et->acts[0] = { "", chosen.name, chosen.exec };
            et->acts[(size_t)sel] = { action_id(was.name), plain(was.name), was.exec };
            et->changed = true;
            et_fill(dlg, 0);
            return TRUE;
        }
        case IDOK:
            if (et->changed && et->app) {
                std::string err;
                if (!shell::write_app_override(*et->app, et->acts, &err)) { message(dlg, "Edit File Type", err); return TRUE; }
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        return FALSE;
    }
    case WM_DESTROY: {
        HICON ic = (HICON)SendDlgItemMessageW(dlg, IDC_ET_ICON, STM_GETICON, 0, 0);
        if (ic) DestroyIcon(ic);
        if (et->bold) DeleteObject(et->bold);
        return FALSE;
    }
    }
    return FALSE;
}

// ---- Create New Extension ----
std::string new_ext, new_type;

static INT_PTR CALLBACK newext_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND cb = GetDlgItem(dlg, IDC_NE_TYPE);
        std::vector<std::string> types;
        for (const Row& r : rows) types.push_back(r.type);
        std::sort(types.begin(), types.end());
        types.erase(std::unique(types.begin(), types.end()), types.end());
        for (const std::string& t : types) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)WS(t));
        set_text(dlg, IDC_NE_TYPE, new_type);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            new_ext = shell::trim(get_text(GetDlgItem(dlg, IDC_NE_EXT)));
            new_type = shell::trim(get_text(GetDlgItem(dlg, IDC_NE_TYPE)));
            if (new_ext.empty() || new_type.empty()) { MessageBeep(0); return TRUE; }
            std::string err;
            if (!shell::add_user_extension(new_ext, new_type, &err)) { message(dlg, "Create New Extension", err); return TRUE; }
            EndDialog(dlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void change()
{
    int i = current();
    if (i < 0) return;
    Row r = rows[(size_t)i];
    OpenWith ow;
    HICON icon = uiicons::large_icon(shell::icon_key("x." + r.ext, false));
    bool ok = choose_program(page, "." + r.ext, r.type, icon, uiicons::small(), uiicons::index("exe"), true, &ow);
    if (icon) DestroyIcon(icon);
    if (!ok) return;
    std::string err;
    if (!shell::set_default_app(r.type, ow.app_id, &err)) message(page, "File Types", err);
    details();
}

static void advanced()
{
    int i = current();
    if (i < 0) return;
    Row r = rows[(size_t)i];
    if (restore_mode) {
        // Restore: the user's lines for the type and flxp's copy of the program's desktop file
        std::string err;
        shell::AppSource s = shell::default_source(r.type);
        if (s.app && shell::app_overridden(*s.app) && !shell::remove_app_override(*s.app, &err)) message(page, "File Types", err);
        if (!shell::reset_default_app(r.type, &err)) message(page, "File Types", err);
        details();
        return;
    }
    EditType e = { r, shell::default_app(r.type), {}, false, nullptr };
    if (e.app) e.acts = shell::app_actions(*e.app);
    et = &e;
    DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_FT_EDITTYPE), page, edittype_proc, 0);
    et = nullptr;
    details();
}

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        page = dlg;
        HWND lv = GetDlgItem(dlg, IDC_FT_LIST);
        SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
        SendMessageW(lv, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)uiicons::small());
        RECT rc; GetClientRect(lv, &rc);
        int w0 = MulDiv(80, rc.right, 330);
        LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)L"Extensions"; c.cx = w0;
        SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&c);
        c.pszText = (LPWSTR)L"File Types"; c.cx = rc.right - w0 - GetSystemMetrics(SM_CXVSCROLL);
        SendMessageW(lv, LVM_INSERTCOLUMNW, 1, (LPARAM)&c);
        fill("txt");
        details();
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->idFrom == IDC_FT_LIST) {
            if (n->code == LVN_GETDISPINFOW) {
                NMLVDISPINFOW* d = (NMLVDISPINFOW*)lp;
                if (d->item.iItem < 0 || (size_t)d->item.iItem >= rows.size()) return TRUE;
                const Row& r = rows[(size_t)d->item.iItem];
                if (d->item.mask & LVIF_TEXT) {
                    text_buf = W(d->item.iSubItem == 0 ? upper(r.ext) : type_label(r));
                    lstrcpynW(d->item.pszText, (LPCWSTR)text_buf.c_str(), d->item.cchTextMax);
                }
                if ((d->item.mask & LVIF_IMAGE) && d->item.iSubItem == 0) d->item.iImage = uiicons::index(shell::icon_key("x." + r.ext, false));
                return TRUE;
            }
            if (n->code == LVN_ODFINDITEMW) {
                // typing in the list: the next extension that starts with what was typed
                NMLVFINDITEMW* f = (NMLVFINDITEMW*)lp;
                if (!(f->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL)) || !f->lvfi.psz) { SetWindowLongPtrW(dlg, DWLP_MSGRESULT, -1); return TRUE; }
                std::string want = shell::lower(w32::utf8_of((const WCHAR*)f->lvfi.psz));
                size_t n0 = rows.size();
                for (size_t k = 0; k < n0; k++) {
                    size_t i = ((size_t)(f->iStart < 0 ? 0 : f->iStart) + k) % n0;
                    if (rows[i].ext.compare(0, want.size(), want) == 0) { SetWindowLongPtrW(dlg, DWLP_MSGRESULT, (LONG_PTR)i); return TRUE; }
                }
                SetWindowLongPtrW(dlg, DWLP_MSGRESULT, -1);
                return TRUE;
            }
            if (n->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* nl = (NMLISTVIEW*)lp;
                if ((nl->uChanged & LVIF_STATE) && (nl->uNewState & LVIS_SELECTED)) details();
                return TRUE;
            }
            if (n->code == NM_DBLCLK) { advanced(); return TRUE; }
        }
        if (n->code == PSN_SETACTIVE) SetFocus(GetDlgItem(dlg, IDC_FT_LIST));
        if (n->code == PSN_APPLY) SetWindowLongPtrW(dlg, DWLP_MSGRESULT, PSNRET_NOERROR);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_FT_CHANGE: change(); return TRUE;
        case IDC_FT_ADVANCED: advanced(); return TRUE;
        case IDC_FT_DEFPROGS: {
            // Default Programs at the role of the selected type
            int i = current();
            const shell::Role* r = i >= 0 ? shell::role_of(rows[(size_t)i].type) : nullptr;
            shell::default_programs(dlg, r ? r->id : "");
            details();
            return TRUE;
        }
        case IDC_FT_NEW: {
            int i = current();
            new_type = i >= 0 ? rows[(size_t)i].type : "";
            if (DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_FT_NEWEXT), dlg, newext_proc, 0) == IDOK) {
                std::string e = shell::lower(new_ext);
                if (!e.empty() && e[0] == '.') e.erase(0, 1);
                fill(e);
                details();
            }
            return TRUE;
        }
        case IDC_FT_DELETE: {
            int i = current();
            if (i < 0) return TRUE;
            // XP's question
            if (MessageBoxW(dlg, L"If you remove a registered file name extension, you will not be able to open files with this extension by "
                                 L"double-clicking their icons.\n\nAre you sure you want to remove this extension?",
                            L"File Types", MB_YESNO | MB_ICONWARNING) != IDYES) return TRUE;
            std::string err;
            if (!shell::remove_user_extension(rows[(size_t)i].ext, &err)) message(dlg, "File Types", err);
            fill(i + 1 < (int)rows.size() ? rows[(size_t)i + 1].ext : "");
            details();
            return TRUE;
        }
        }
        return FALSE;
    case WM_DESTROY: {
        HICON ic = (HICON)SendDlgItemMessageW(dlg, IDC_FT_APPICON, STM_GETICON, 0, 0);
        if (ic) DestroyIcon(ic);
        page = nullptr;
        rows.clear();
        return FALSE;
    }
    }
    return FALSE;
}

} // namespace filetypes

} // namespace folderopts

void folder_options(HWND owner, FolderOptions& o)
{
    folderopts::o = &o;
    PROPSHEETPAGEW pages[3] = {};
    for (int i = 0; i < 3; i++) {
        pages[i].dwSize = sizeof pages[i];
        pages[i].dwFlags = PSP_DEFAULT;
        pages[i].hInstance = module();
    }
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_FO_GENERAL);
    pages[0].pfnDlgProc = folderopts::general_proc;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_FO_VIEW);
    pages[1].pfnDlgProc = folderopts::view_proc;
    pages[2].pszTemplate = MAKEINTRESOURCEW(IDD_FO_FILETYPES);
    pages[2].pfnDlgProc = folderopts::filetypes::proc;
    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof psh;
    psh.dwFlags = PSH_PROPSHEETPAGE;
    psh.hwndParent = owner;
    psh.hInstance = module();
    psh.pszCaption = L"Folder Options";
    psh.nPages = 3;
    psh.nStartPage = (UINT)(o.start_page >= 0 && o.start_page < 3 ? o.start_page : 0);
    psh.ppsp = pages;
    PropertySheetW(&psh);
    folderopts::o = nullptr;
}

// ---- Choose Details ----------------------------------------------------------------------

namespace choosedetails {

std::vector<DetailColumn>* cols;
bool g_filling;

// LVS_EX_CHECKBOXES: the state image 2 is checked, 1 not (ListView_SetCheckState)
static void set_check(HWND lv, int i, bool on)
{
    LVITEMW st = {}; st.stateMask = LVIS_STATEIMAGEMASK; st.state = INDEXTOSTATEIMAGEMASK(on ? 2 : 1);
    SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
}
static bool get_check(HWND lv, int i) { return ((SendMessageW(lv, LVM_GETITEMSTATE, (WPARAM)i, LVIS_STATEIMAGEMASK) >> 12) - 1) == 1; }

static int current(HWND dlg) { return (int)SendDlgItemMessageW(dlg, IDC_CDT_LIST, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); }

static void fill(HWND dlg, int select)
{
    HWND lv = GetDlgItem(dlg, IDC_CDT_LIST);
    g_filling = true;
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < cols->size(); i++) {
        std::u16string t = W((*cols)[i].label);
        LVITEMW it = {};
        it.mask = LVIF_TEXT; it.iItem = (int)i; it.pszText = (LPWSTR)t.c_str();
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        set_check(lv, (int)i, (*cols)[i].shown);
    }
    if (select >= 0 && select < (int)cols->size()) {
        LVITEMW st = {}; st.stateMask = LVIS_SELECTED | LVIS_FOCUSED; st.state = st.stateMask;
        SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)select, (LPARAM)&st);
        SendMessageW(lv, LVM_ENSUREVISIBLE, (WPARAM)select, FALSE);
    }
    g_filling = false;
}

static bool is_name(int i) { return i >= 0 && (size_t)i < cols->size() && (*cols)[(size_t)i].id == 0; }

static void sync(HWND dlg)
{
    int i = current(dlg);
    bool sel = i >= 0;
    // Name stays shown (XP's check is greyed) and where it is (a header drag moves it);
    // the others move past it and past the hidden ones, which keep their places
    bool name = sel && is_name(i);
    EnableWindow(GetDlgItem(dlg, IDC_CDT_UP), sel && !name && i > 0);
    EnableWindow(GetDlgItem(dlg, IDC_CDT_DOWN), sel && !name && i + 1 < (int)cols->size());
    EnableWindow(GetDlgItem(dlg, IDC_CDT_SHOW), sel && !name && !(*cols)[(size_t)i].shown);
    EnableWindow(GetDlgItem(dlg, IDC_CDT_HIDE), sel && !name && (*cols)[(size_t)i].shown);
    EnableWindow(GetDlgItem(dlg, IDC_CDT_WIDTH), sel);
    g_filling = true;
    set_text(dlg, IDC_CDT_WIDTH, sel ? std::to_string((*cols)[(size_t)i].width) : "");
    g_filling = false;
}

static void show(HWND dlg, int i, bool on)
{
    if (i < 0 || i >= (int)cols->size() || is_name(i)) return;
    (*cols)[(size_t)i].shown = on;
    g_filling = true;
    set_check(GetDlgItem(dlg, IDC_CDT_LIST), i, on);
    g_filling = false;
    sync(dlg);
}

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND lv = GetDlgItem(dlg, IDC_CDT_LIST);
        SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        RECT rc; GetClientRect(lv, &rc);
        LVCOLUMNW c = {};
        c.mask = LVCF_WIDTH; c.cx = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
        SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&c);
        fill(dlg, 0);
        sync(dlg);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom != IDC_CDT_LIST || nm->code != LVN_ITEMCHANGED || g_filling) return FALSE;
        NMLISTVIEW* lv = (NMLISTVIEW*)nm;
        if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK) && lv->iItem >= 0) {
            bool on = get_check(nm->hwndFrom, lv->iItem);
            if (is_name(lv->iItem) && !on) { g_filling = true; set_check(nm->hwndFrom, lv->iItem, TRUE); g_filling = false; }
            else if ((size_t)lv->iItem < cols->size()) (*cols)[(size_t)lv->iItem].shown = on;
        }
        sync(dlg);
        return FALSE;
    }
    case WM_COMMAND: {
        int i = current(dlg);
        switch (LOWORD(wp)) {
        case IDC_CDT_UP:
            if (i > 0 && !is_name(i)) { std::swap((*cols)[(size_t)i], (*cols)[(size_t)i - 1]); fill(dlg, i - 1); sync(dlg); }
            return TRUE;
        case IDC_CDT_DOWN:
            if (i >= 0 && !is_name(i) && i + 1 < (int)cols->size()) { std::swap((*cols)[(size_t)i], (*cols)[(size_t)i + 1]); fill(dlg, i + 1); sync(dlg); }
            return TRUE;
        case IDC_CDT_SHOW: show(dlg, i, true); return TRUE;
        case IDC_CDT_HIDE: show(dlg, i, false); return TRUE;
        case IDC_CDT_WIDTH:
            if (HIWORD(wp) == EN_CHANGE && !g_filling && i >= 0) {
                int w = atoi(get_text((HWND)lp).c_str());
                if (w > 0 && w < 4000) (*cols)[(size_t)i].width = w;
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

} // namespace choosedetails

bool choose_details(HWND owner, std::vector<DetailColumn>& cols)
{
    std::vector<DetailColumn> work = cols;
    choosedetails::cols = &work;
    INT_PTR r = DialogBoxParamW(module(), MAKEINTRESOURCEW(IDD_CHOOSEDETAILS), owner, choosedetails::proc, 0);
    choosedetails::cols = nullptr;
    if (r != IDOK) return false;
    cols = work;
    return true;
}

// ---- a line of text (Edit Description..., Add Tag...) ------------------------------------

namespace asktext {

const TextAsk* a;
std::string* out;

static INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(dlg, WS(a->title));
        if (a->icon) SendDlgItemMessageW(dlg, IDC_TX_ICON, STM_SETICON, (WPARAM)a->icon, 0);
        set_text(dlg, IDC_TX_PROMPT, a->prompt);
        set_text(dlg, IDC_TX_NOTE, a->note);
        HWND box = GetDlgItem(dlg, IDC_TX_TEXT);
        for (const std::string& t : a->suggestions) SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)WS(t));
        SetWindowTextW(box, WS(a->initial));
        SetFocus(box);
        if (a->dropdown) SendMessageW(box, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        else SendMessageW(box, EM_SETSEL, 0, -1);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { *out = get_text(GetDlgItem(dlg, IDC_TX_TEXT)); EndDialog(dlg, IDOK); return TRUE; }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        return FALSE;
    }
    return FALSE;
}

} // namespace asktext

bool ask_text(HWND owner, const TextAsk& a, std::string* text)
{
    asktext::a = &a;
    asktext::out = text;
    INT_PTR r = DialogBoxParamW(module(), MAKEINTRESOURCEW(a.dropdown ? IDD_ADDTAG : IDD_EDITTEXT), owner, asktext::proc, 0);
    asktext::a = nullptr;
    asktext::out = nullptr;
    return r == IDOK;
}

} // namespace dialogs
