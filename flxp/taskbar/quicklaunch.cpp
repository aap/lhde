// XP's Quick Launch toolbar after the Start button: Show Desktop first, then one 16 px button per .desktop file of the
// Quick Launch folder (settings.ini [quicklaunch] dir, luxshell's Quick Launch tile's folder; default
// ~/.config/lhde/quicklaunch), in file-name order, read again when the folder changes. A comctl32 toolbar, flat and
// transparent, its buttons 23 x 22 (TB_SETPADDING 7, 6 around the icon), themed TaskBar::Toolbar; a file dropped on it
// becomes an entry (a .desktop file is copied, anything else gets one that opens it), as XP's folder took a shortcut.
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include "shell/apps.h"
#include "shell/config.h"
#include "shell/settings.h"
#include "shell/util.h"
#include "taskbar.h"
extern "C" {
#include "shellapi.h"
}

namespace tb {
namespace quicklaunch {

namespace {

const UINT FIRST_CMD = 0x3000, CMD_DESKTOP = 0x2fff;
const int BUTTON_W = 23, BUTTON_H = 22;   // XP's: a 16 px icon with the toolbar's default 7 x 6 padding

struct Entry { std::string file; shell::App app; };
HWND tbw;
HIMAGELIST il;
std::vector<Entry> entries;
WNDPROC tb_proc;
time_t dir_mtime;
bool shown_flag = true;

std::string dir()
{
    std::string d = shell::store::get("quicklaunch", "dir");
    if (!d.empty() && d[0] == '~') d = std::string(getenv("HOME") ? getenv("HOME") : "") + d.substr(1);
    return d.empty() ? shell::store::dir() + "/quicklaunch" : d;
}

time_t mtime(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? st.st_mtime : 0;
}

void add_image(HBITMAP bm)
{
    if (bm) { ImageList_Add(il, bm, nullptr); DeleteObject(bm); return; }
    HICON def = (HICON)LoadImageW(nullptr, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_SHARED);
    ImageList_AddIcon(il, def);
}

void load()
{
    while (SendMessageW(tbw, TB_BUTTONCOUNT, 0, 0) > 0) SendMessageW(tbw, TB_DELETEBUTTON, 0, 0);
    ImageList_RemoveAll(il);
    entries.clear();
    std::string d = dir();
    dir_mtime = mtime(d);
    std::vector<std::string> names;
    if (DIR* dp = opendir(d.c_str())) {
        while (struct dirent* e = readdir(dp)) {
            std::string n = e->d_name;
            if (n.size() > 8 && n.compare(n.size() - 8, 8, ".desktop") == 0) names.push_back(n);
        }
        closedir(dp);
    }
    std::sort(names.begin(), names.end());
    // Show Desktop: XP's first entry (its Show Desktop.scf), the desktop's icon
    add_image(startpanel::menu_bitmap("", "desktop", 16));
    std::vector<TBBUTTON> b;
    TBBUTTON sd = {};
    sd.iBitmap = 0; sd.idCommand = CMD_DESKTOP; sd.fsState = TBSTATE_ENABLED; sd.fsStyle = BTNS_BUTTON; sd.iString = -1;
    b.push_back(sd);
    for (const std::string& n : names) {
        Entry e;
        e.file = d + "/" + n;
        if (!shell::read_desktop_file(e.file, e.app)) continue;
        add_image(startpanel::menu_bitmap(e.app.icon, "exe", 16));
        TBBUTTON t = {};
        t.iBitmap = (int)entries.size() + 1;
        t.idCommand = (int)(FIRST_CMD + entries.size());
        t.fsState = TBSTATE_ENABLED;
        t.fsStyle = BTNS_BUTTON;
        t.iString = -1;
        b.push_back(t);
        entries.push_back(e);
    }
    SendMessageW(tbw, TB_ADDBUTTONSW, b.size(), (LPARAM)b.data());
    SendMessageW(tbw, TB_SETBUTTONSIZE, 0, MAKELONG(BUTTON_W, BUTTON_H));
}

// a dropped file: a .desktop file is copied into the folder, anything else gets one that opens it (xdg-open)
void add_file(const std::string& path)
{
    std::string d = dir();
    shell::make_dirs(d);
    std::string base = path.substr(path.rfind('/') + 1);
    if (base.size() > 8 && base.compare(base.size() - 8, 8, ".desktop") == 0) {
        std::string body;
        if (shell::read_file(path, body)) shell::write_file_atomic(d + "/" + base, body);
        return;
    }
    std::string name = base, quoted = "\"";
    for (char c : path) { if (c == '"' || c == '\\' || c == '`' || c == '$') quoted += '\\'; quoted += c; }
    quoted += "\"";
    std::string body = "[Desktop Entry]\nType=Application\nName=" + name + "\nExec=xdg-open " + quoted + "\n";
    std::string out = d + "/" + base + ".desktop";
    for (int k = 2; access(out.c_str(), F_OK) == 0; k++) out = d + "/" + base + " (" + std::to_string(k) + ").desktop";
    shell::write_file_atomic(out, body);
}

void entry_menu(int k, POINT pt)
{
    enum { CMD_OPEN = 1, CMD_DELETE, CMD_FOLDER };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_OPEN, L"&Open");
    if (k >= 0) AppendMenuW(m, MF_STRING, CMD_DELETE, L"&Delete");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_FOLDER, L"Open Quick Launch &Folder");
    SetMenuDefaultItem(m, CMD_OPEN, FALSE);
    xwm::focus_own();
    UINT cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | (g.edge == EdgeBottom ? TPM_BOTTOMALIGN : TPM_TOPALIGN),
                                    pt.x, pt.y, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd == CMD_OPEN) command(k < 0 ? CMD_DESKTOP : FIRST_CMD + (UINT)k);
    else if (cmd == CMD_DELETE && k >= 0 && k < (int)entries.size()) { unlink(entries[(size_t)k].file.c_str()); load(); relayout_bar(); }
    else if (cmd == CMD_FOLDER) { shell::make_dirs(dir()); startmenu::open_folder(dir()); }
}

LRESULT CALLBACK subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DROPFILES) {
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            WCHAR buf[4096];
            if (DragQueryFileW(hd, i, buf, 4096)) add_file(U(buf));
        }
        DragFinish(hd);
        load();
        relayout_bar();
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int k = (int)SendMessageW(h, TB_HITTEST, 0, (LPARAM)&p);
        POINT s = p;
        ClientToScreen(h, &s);
        if (k < 0) taskbar_menu(s);
        else entry_menu(k - 1, s);
        return 0;
    }
    return CallWindowProcW(tb_proc, h, msg, wp, lp);
}

} // namespace

HWND create(HWND parent)
{
    tbw = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                          WS_CHILD | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN | TBSTYLE_TOOLTIPS | TBSTYLE_TRANSPARENT |
                          TBSTYLE_FLAT,
                          0, 0, 1, 1, parent, nullptr, nullptr, nullptr);
    SetWindowTextW(tbw, L"Quick Launch");
    SendMessageW(tbw, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 8, 8);
    SendMessageW(tbw, TB_SETIMAGELIST, 0, (LPARAM)il);
    SendMessageW(tbw, TB_SETBITMAPSIZE, 0, MAKELONG(16, 16));
    SendMessageW(tbw, TB_SETPADDING, 0, MAKELONG(BUTTON_W - 16, BUTTON_H - 16));
    tb_proc = (WNDPROC)SetWindowLongPtrW(tbw, GWLP_WNDPROC, (LONG_PTR)subclass);
    DragAcceptFiles(tbw, TRUE);
    load();
    return tbw;
}

void theme_changed()
{
    SetWindowTheme(tbw, L"TaskBar", nullptr);       // TaskBar::Toolbar's buttons
    SendMessageW(tbw, TB_SETBUTTONSIZE, 0, MAKELONG(BUTTON_W, BUTTON_H));
    InvalidateRect(tbw, nullptr, TRUE);
}

bool shown() { return shown_flag; }
void read_setting() { shown_flag = shell::store::flag("taskbar", "quicklaunch", true); }

int length() { return (int)(entries.size() + 1) * (g.horizontal() ? BUTTON_W : BUTTON_H); }
int button_size() { return BUTTON_W; }
int count() { return (int)entries.size() + 1; }

void place(const RECT& r)
{
    if (!shown_flag || r.right <= r.left) { ShowWindow(tbw, SW_HIDE); return; }
    SetWindowPos(tbw, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    int bh = g.horizontal() ? r.bottom - r.top : BUTTON_H;
    SendMessageW(tbw, TB_SETPADDING, 0, MAKELONG(BUTTON_W - 16, bh - 16));
    SendMessageW(tbw, TB_SETBUTTONSIZE, 0, MAKELONG(BUTTON_W, bh));
    if (!g.horizontal()) {
        for (int i = 0; i < count(); i++) {
            TBBUTTONINFOW bi = {};
            bi.cbSize = sizeof bi;
            bi.dwMask = TBIF_STATE;
            SendMessageW(tbw, TB_GETBUTTONINFOW, (WPARAM)(i == 0 ? CMD_DESKTOP : FIRST_CMD + i - 1), (LPARAM)&bi);
            bi.fsState |= TBSTATE_WRAP;
            SendMessageW(tbw, TB_SETBUTTONINFOW, (WPARAM)(i == 0 ? CMD_DESKTOP : FIRST_CMD + i - 1), (LPARAM)&bi);
        }
    }
    InvalidateRect(tbw, nullptr, TRUE);
}

HWND window() { return tbw; }

// the folder changed (polled with the clock): read it again; true when the toolbar did
bool refresh()
{
    if (mtime(dir()) == dir_mtime) return false;
    load();
    return true;
}

LRESULT notify(NMHDR* h)
{
    if (h->hwndFrom != tbw) return 0;
    if (h->code == NM_CUSTOMDRAW) {
        // the buttons drawn here: the toolbar's own placement of the image under a style follows the button part's
        // margins, which put a 16 px icon below the middle of a bar-high button. Hot: TaskBar::Toolbar's (else
        // Toolbar's) BUTTON state, classic the flat toolbar's raised edge, pressed sunken; the icon centred
        NMTBCUSTOMDRAW* cd = (NMTBCUSTOMDRAW*)h;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
        HDC hdc = cd->nmcd.hdc;
        RECT rc = cd->nmcd.rc, client;
        GetClientRect(tbw, &client);
        if (g.horizontal()) { rc.top = 0; rc.bottom = client.bottom; }   // the toolbar's own idea of the height may differ
        UINT st = cd->nmcd.uItemState;
        bool hot = st & CDIS_HOT, down = st & CDIS_SELECTED;
        HTHEME th = g.theme ? OpenThemeData(nullptr, L"TaskBar::Toolbar") : nullptr;
        if (g.theme && !th) th = OpenThemeData(nullptr, L"Toolbar");
        int d = 0;
        if (th) {
            if (hot || down) DrawThemeBackground(th, hdc, TP_BUTTON, down ? TS_PRESSED : TS_HOT, &rc, nullptr);
            CloseThemeData(th);
        } else if (hot || down) {
            DrawEdge(hdc, &rc, down ? BDR_SUNKENOUTER : BDR_RAISEDINNER, BF_RECT);
            d = down ? 1 : 0;
        }
        int idx = (UINT)cd->nmcd.dwItemSpec == CMD_DESKTOP ? 0 : (int)((UINT)cd->nmcd.dwItemSpec - FIRST_CMD) + 1;
        ImageList_Draw(il, idx, hdc, rc.left + (rc.right - rc.left - 16) / 2 + d, rc.top + (rc.bottom - rc.top - 16) / 2 + d,
                       ILD_TRANSPARENT);
        return CDRF_SKIPDEFAULT;
    }
    if (h->code == TBN_GETINFOTIPW) {
        NMTBGETINFOTIPW* t = (NMTBGETINFOTIPW*)h;
        std::string text = (UINT)t->iItem == CMD_DESKTOP ? "Show Desktop" : "";
        if ((UINT)t->iItem >= FIRST_CMD && (UINT)t->iItem - FIRST_CMD < entries.size()) {
            const shell::App& a = entries[(UINT)t->iItem - FIRST_CMD].app;
            text = a.comment.empty() ? a.name : a.name + "\n" + a.comment;
        }
        std::u16string w = W(text);
        int n = (int)w.size() < t->cchTextMax - 1 ? (int)w.size() : t->cchTextMax - 1;
        if (n >= 0 && t->pszText) { memcpy(t->pszText, w.data(), (size_t)n * 2); t->pszText[n] = 0; }
    }
    return 0;
}

bool command(UINT id)
{
    if (id == CMD_DESKTOP) { toggle_desktop(); return true; }
    if (id < FIRST_CMD || id - FIRST_CMD >= entries.size()) return false;
    startmenu::launch(entries[id - FIRST_CMD].app);
    return true;
}

} // namespace quicklaunch
} // namespace tb
