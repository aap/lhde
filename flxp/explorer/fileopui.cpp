// Copy and move with XP's user interface (fileopui.h). Behaviour, texts and
// layouts follow XP SP1's shell32 copy.c (ConfirmFileOp, CopyError,
// CopyMoveRetry, FOUIThreadProc, SetProgressText, SetProgressTime); the
// templates are rc/explorer.rc's IDD_OP_*.
#include "res/res.h"   // before the win32 headers: their macros clash with its names
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include "fileopui.h"
#include "dialogs.h"
#include "resource.h"
#include "uiicons.h"
#include "shell/config.h"
#include "shell/fileops.h"
#include "shell/icons.h"
#include "shell/namespace.h"
#include "sys/sys.h"
extern "C" {
#include "commctrl.h"
}
#undef min   // windows.h's, put back by fileops_job.h
#undef max

namespace fileopui {

using namespace shell::fileops;

static std::u16string W(const std::string& s) { return w32::utf16_of(s.c_str()); }
#define WS(s) ((LPCWSTR)W(s).c_str())

// ---- texts ------------------------------------------------------------------------

// shlwapi StrFormatByteSizeW: under 1024 in bytes, else three significant
// digits of the order (truncated, not rounded) below 1000 of it
std::string byte_size(uint64_t n)
{
    static const char* orders[] = { "bytes", "KB", "MB", "GB", "TB", "PB", "EB" };
    char num[48];
    int order = 0;
    if (n < 1024) snprintf(num, sizeof num, "%u", (unsigned)n);
    else {
        for (order = 1; order < 6 && n >= 1000ull * 1024; n >>= 10, order++) {}
        unsigned whole = (unsigned)(n >> 10);
        snprintf(num, sizeof num, "%u", whole);
        size_t len = strlen(num);
        if (len < 3) {
            unsigned dec = (unsigned)((n - (uint64_t)whole * 1024) * 1000 / 1024) / 10;
            if (len == 2) dec /= 10;
            snprintf(num + len, sizeof num - len, ".%0*u", (int)(3 - len), dec);
        }
    }
    return std::string(num) + " " + orders[order];
}

// copy.c BuildDateLine: IDS_DATESIZELINE "%s\nmodified: %s" with SHFormatDateTime
// (FDTF_LONGDATE | FDTF_RELATIVE | FDTF_LONGTIME): "Today"/"Yesterday" for the
// weekday, ", " before the time
std::string date_line(uint64_t size, int64_t mtime)
{
    std::string s = byte_size(size);
    if (!mtime) return s;
    time_t t = (time_t)mtime, now = time(nullptr), yday = now - 24 * 3600;
    struct tm tm, tn, ty;
    localtime_r(&t, &tm); localtime_r(&now, &tn); localtime_r(&yday, &ty);
    auto same = [&](const struct tm& a) { return a.tm_year == tm.tm_year && a.tm_mon == tm.tm_mon && a.tm_mday == tm.tm_mday; };
    char day[32], rest[64];
    strftime(day, sizeof day, "%A", &tm);
    strftime(rest, sizeof rest, "%B %-d, %Y", &tm);
    int h = tm.tm_hour % 12;
    char clock[32];
    snprintf(clock, sizeof clock, "%d:%02d:%02d %s", h ? h : 12, tm.tm_min, tm.tm_sec, tm.tm_hour < 12 ? "AM" : "PM");
    std::string d = same(tn) ? "Today" : same(ty) ? "Yesterday" : day;
    return s + "\nmodified: " + d + ", " + rest + ", " + clock;
}

// shell32's IDS_REASONS_* for the errno (CopyError: the system's own text otherwise)
static std::string reason_text(const Error& e)
{
    switch (e.reason) {
    case Reason::SameFile: return "The source and destination file names are the same.";
    case Reason::IntoItself: return "The destination folder is the same as the source folder.";
    case Reason::IntoSubfolder: return "The destination folder is a subfolder of the source folder.";
    case Reason::FileOntoFolder: return "There is already a folder with the same name as the file name you specified. Specify a different name.";
    case Reason::FolderOntoFile: return "There is already a file with the same name as the folder name you specified. Specify a different name.";
    case Reason::Changed: return "The file changed while it was being copied.";
    case Reason::None: break;
    }
    if (e.what.compare(0, 12, "Cannot merge") == 0) return "The destination folder is a subfolder of the source folder.";
    if (e.what.compare(0, 11, "Not removed") == 0) return "It changed after it was copied, so the original was kept.";
    switch (e.code) {
    case EACCES: case EPERM:
        return "Access is denied.\n\nMake sure the disk is not full or write-protected\nand that the file is not currently in use.";
    case ENOENT: return "Cannot find the specified file.\n\nMake sure you specify the correct path and file name.";
    case EROFS: return "The disk is write-protected.\n\nRemove the write-protection or use another disk.";
    case EIO: return e.dest ? "Cannot write to the destination file or disk." : "Cannot read from the source file or disk.";
    case ENAMETOOLONG: return "The file name you specified is not valid or too long. \nSpecify a different file name.";
    case ENOMEM: return "There is not enough free memory.\n\nQuit one or more programs, and then try again.";
    case EMFILE: case ENFILE: return "Too many files are currently in use. Quit one or more programs, and then try again.";
    case EBUSY: case ETXTBSY: return "It is being used by another person or program.\n\nClose any programs that might be using the file and try again.";
    case ELOOP: return "The path is too deep.";
    case EEXIST: return "A file with the name you specified already exists. Specify a different file name.";
    case 0: return "";
    }
    std::string s = strerror(e.code);
    if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
    return s + ".";
}

ErrorText error_text(Op op, const Error& e)
{
    ErrorText t;
    bool move = op == Op::Move;
    t.title = move ? "Error Moving File or Folder" : "Error Copying File or Folder";
    if (e.code == ENOSPC || e.code == EDQUOT) {   // CopyMoveRetry: IDS_DESTFULL, titled IDS_COPY / IDS_MOVE
        t.title = move ? "Move" : "Copy";
        t.text = "The disk in the destination drive is full.\n\nInsert a new disk to continue.";
        t.retry = true;
        return t;
    }
    std::string name = e.path.empty() ? "file" : shell::base_of(e.path);
    struct stat st;
    bool dir = !e.path.empty() && lstat(e.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    // CopyError: a file is named without its extension (PathRemoveExtension for OPER_DOFILE)
    size_t dot = name.rfind('.');
    if (!dir && dot != std::string::npos && dot > 0) name.resize(dot);
    std::string verb;
    if (e.what.compare(0, 24, "Cannot remove the partial") == 0) verb = "Cannot delete";
    else if (e.what.compare(0, 26, "Cannot remove the original") == 0 || e.what.compare(0, 11, "Not removed") == 0)
        verb = dir ? "Cannot remove folder" : "Cannot move";
    else if (e.dest) verb = "Cannot create or replace";   // IDS_REPLACING (an error on the destination)
    else verb = move ? "Cannot move" : "Cannot copy";     // IDS_VERBSCOPY / IDS_VERBSMOVE
    t.text = verb + " " + name + ": " + reason_text(e);
    return t;
}

// a name cut to fit width px, "..." at its end (PathCompactPath on a bare name)
static std::string compact(HDC hdc, const std::string& s, int width)
{
    auto fits = [&](const std::string& t) {
        std::u16string w = W(t);
        SIZE sz = {};
        GetTextExtentPoint32W(hdc, (LPCWSTR)w.c_str(), (int)w.size(), &sz);
        return sz.cx <= width;
    };
    if (fits(s)) return s;
    std::u16string w = W(s);
    size_t lo = 0, hi = w.size();
    while (lo < hi) {   // the longest prefix that fits with the dots
        size_t mid = (lo + hi + 1) / 2;
        if (fits(w32::utf8_of((const WCHAR*)w.substr(0, mid).c_str()) + "...")) lo = mid; else hi = mid - 1;
    }
    return w32::utf8_of((const WCHAR*)w.substr(0, lo).c_str()) + "...";
}

static RECT child_rect(HWND dlg, HWND h)
{
    RECT r;
    GetWindowRect(h, &r);
    MapWindowPoints(nullptr, dlg, (POINT*)&r, 2);
    return r;
}

// a dialog owned by another window stays above it (the window manager's transient-for)
static void keep_above(HWND dlg, bool modal)
{
    HWND owner = GetWindow(dlg, GW_OWNER);
    if (!owner) return;
    sys::NativeWindow* nw = sys::window_for_user((void*)dlg);
    if (nw) sys::set_transient_for(nw, sys::native_id(sys::window_for_user((void*)GetAncestor(owner, GA_ROOT))), modal);
}

static HICON shell32_icon(int id)
{
    std::string src = shell::resource_src("xp", "shell32.dll", id);
    HICON h = src.empty() ? nullptr : (HICON)uiicons::load_themed(src, IMAGE_ICON, 32, 32, 0);
    return h ? h : uiicons::load_icon("folder", 32);
}

// ---- message box (copy errors) -----------------------------------------------------

struct Box { std::string title, text; bool retry; };

// user32's MessageBox layout (MSGBOX_OnInit): the text wrapped at a third of
// the screen (or the template's width), the icon beside it, the buttons centred below
static INT_PTR CALLBACK box_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        Box* b = (Box*)lp;
        SetWindowTextW(h, WS(b->title));
        SetDlgItemTextW(h, IDC_OP_TEXT, WS(b->text));
        HWND ic = GetDlgItem(h, IDC_OP_ICON), t = GetDlgItem(h, IDC_OP_TEXT);
        SendMessageW(ic, STM_SETICON, (WPARAM)LoadIconW(nullptr, (LPCWSTR)(b->retry ? IDI_EXCLAMATION : IDI_HAND)), 0);
        RECT tr0 = child_rect(h, t), ir = child_rect(h, ic), br = child_rect(h, GetDlgItem(h, IDOK));
        HDC hdc = GetDC(t);
        HGDIOBJ of = SelectObject(hdc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        RECT calc = { 0, 0, std::max((int)(tr0.right - tr0.left), (int)sys::screen_size().cx / 3), 0 };
        std::u16string w = W(b->text);
        DrawTextW(hdc, (LPCWSTR)w.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        SelectObject(hdc, of);
        ReleaseDC(t, hdc);
        int tw = calc.right, th = std::max((int)calc.bottom, (int)(ir.bottom - ir.top));
        SetWindowPos(t, nullptr, tr0.left, std::max((int)tr0.top, (int)(ir.top + (ir.bottom - ir.top - calc.bottom) / 2)), tw, th, SWP_NOZORDER);
        int bw = br.right - br.left, bh = br.bottom - br.top, gap = 6;
        std::vector<int> ids;
        if (b->retry) ids = { IDRETRY, IDCANCEL }; else ids = { IDOK };
        for (int id : { IDOK, IDRETRY, IDCANCEL })
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) DestroyWindow(GetDlgItem(h, id));
        int nb = (int)ids.size();
        int cw = std::max((int)(tr0.left + tw + ir.left), nb * bw + (nb - 1) * gap + 2 * (int)ir.left);
        int by = std::max((int)ir.bottom, (int)(tr0.top + th)) + 12;
        int bx = (cw - (nb * bw + (nb - 1) * gap)) / 2;
        for (int i = 0; i < nb; i++) SetWindowPos(GetDlgItem(h, ids[i]), nullptr, bx + i * (bw + gap), by, bw, bh, SWP_NOZORDER);
        SendMessageW(h, DM_SETDEFID, ids[0], 0);
        RECT wr, cr;
        GetWindowRect(h, &wr); GetClientRect(h, &cr);
        int ww = cw + (wr.right - wr.left) - cr.right, wh = by + bh + 12 + (wr.bottom - wr.top) - cr.bottom;
        HWND owner = GetWindow(h, GW_OWNER);
        RECT orc;
        gdi::Size ss = sys::screen_size();
        if (owner) GetWindowRect(owner, &orc); else orc = { 0, 0, ss.cx, ss.cy };
        SetWindowPos(h, nullptr, orc.left + (orc.right - orc.left - ww) / 2, orc.top + (orc.bottom - orc.top - wh) / 2, ww, wh, SWP_NOZORDER);
        keep_above(h, true);
        MessageBeep(b->retry ? MB_ICONEXCLAMATION : MB_ICONHAND);
        SetFocus(GetDlgItem(h, ids[0]));
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: case IDRETRY: EndDialog(h, LOWORD(wp)); return TRUE;
        case IDCANCEL: EndDialog(h, GetDlgItem(h, IDOK) ? IDOK : IDCANCEL); return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

// ---- Confirm File Replace / Confirm Folder Replace / Confirm File Move -------------------

enum { ID_NOTOALL = 0x7001 };

struct Confirm {
    const Conflict* c;
    HICON icons[3] = {};
};

// copy.c HideConfirmButtons(hdlg, IDCANCEL): No takes Cancel's place, Yes No's;
// Yes to All and Cancel are destroyed (hidden ones would keep their accelerators)
static void yes_no_only(HWND h)
{
    RECT cancel = child_rect(h, GetDlgItem(h, IDCANCEL)), no = child_rect(h, GetDlgItem(h, IDNO));
    SetWindowPos(GetDlgItem(h, IDNO), nullptr, cancel.left, cancel.top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
    SetWindowPos(GetDlgItem(h, IDYES), nullptr, no.left, no.top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
    DestroyWindow(GetDlgItem(h, IDC_OP_YESTOALL));
    DestroyWindow(GetDlgItem(h, IDCANCEL));
}

static INT_PTR CALLBACK confirm_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        Confirm* cf = (Confirm*)lp;
        SetWindowLongPtrW(h, DWLP_USER, lp);
        const Conflict& c = *cf->c;
        HWND text = GetDlgItem(h, IDC_OP_TEXT);
        // ConfirmFileOp: the name fits the text's width less two "0"s (the quotes may wrap otherwise)
        RECT tr = child_rect(h, text);
        HDC hdc = GetDC(text);
        HGDIOBJ of = SelectObject(hdc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        SIZE zero = {};
        GetTextExtentPoint32W(hdc, L"0", 1, &zero);
        std::string name = compact(hdc, shell::base_of(c.kind == Conflict::MoveReadOnly ? c.src : c.dst), tr.right - tr.left - 2 * zero.cx);
        SelectObject(hdc, of);
        ReleaseDC(text, hdc);
        std::string s;
        if (c.kind == Conflict::MoveReadOnly) {
            s = "Are you sure you want to move the read-only file '" + name + "'?";
            cf->icons[0] = shell32_icon(148);   // IDI_MOVE
        } else if (c.folder) {
            s = "This folder already contains a folder named '" + name + "'.\n\nIf the files in the existing folder have the same "
                "name as files in the folder you are moving or copying, they will be replaced.  Do you still want to move or copy the folder?";
            cf->icons[0] = shell32_icon(146);   // IDI_REPLACE_FOLDER
        } else {
            s = std::string("This folder already contains a ") + (c.dst_readonly ? "read-only " : "") + "file named '" + name + "'.";
            cf->icons[0] = shell32_icon(146);   // DLG_REPLACE_FILE shows IDI_REPLACE_FOLDER too
            std::string key = shell::icon_key(shell::base_of(c.dst), false);
            cf->icons[1] = uiicons::load_icon(key, 32);
            cf->icons[2] = uiicons::load_icon(key, 32);
            SetDlgItemTextW(h, IDC_OP_OLDINFO, WS(date_line(c.dst_size, c.dst_mtime)));
            SetDlgItemTextW(h, IDC_OP_NEWINFO, WS(date_line(c.src_size, c.src_mtime)));
            SendDlgItemMessageW(h, IDC_OP_OLDICON, STM_SETICON, (WPARAM)cf->icons[1], 0);
            SendDlgItemMessageW(h, IDC_OP_NEWICON, STM_SETICON, (WPARAM)cf->icons[2], 0);
        }
        SetWindowTextW(text, WS(s));
        SendDlgItemMessageW(h, IDC_OP_ICON, STM_SETICON, (WPARAM)cf->icons[0], 0);
        // one file, not inside a folder: Yes and No alone (bShowCancel); a folder always has Cancel
        if (c.only && !c.folder) yes_no_only(h);
        keep_above(h, true);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDYES: case IDC_OP_YESTOALL: case IDCANCEL: EndDialog(h, LOWORD(wp)); return TRUE;
        case IDNO: EndDialog(h, GetKeyState(VK_SHIFT) < 0 ? ID_NOTOALL : IDNO); return TRUE;   // Shift+No: No to All
        }
        return FALSE;
    }
    return FALSE;
}

// ---- the running operations ----------------------------------------------------------

struct Running {
    Job* job = nullptr;
    Op op = Op::Copy;
    HWND owner = nullptr;
    bool asking = false;        // a question is open (the timer must not ask again)
    bool finished = false;
};

static std::vector<Running*> g_running;
static UINT_PTR g_timer;
enum { WM_OP_NOTIFY = WM_APP + 0x51 };

static void notify(void*) { PostThreadMessageW(0, WM_OP_NOTIFY, 0, 0); }   // wakes the waiting loop

static HWND dialog_owner(Running& r) { return IsWindow(r.owner) ? r.owner : nullptr; }

static Answer ask(Running& r, const Conflict& c)
{
    if (c.kind == Conflict::Failed) {
        ErrorText et = error_text(r.op, c.error);
        Box b = { et.title, et.text, et.retry };
        INT_PTR id = DialogBoxParamW(dialogs::module(), MAKEINTRESOURCEW(IDD_OP_MESSAGE), dialog_owner(r), box_proc, (LPARAM)&b);
        return id == IDRETRY ? Answer::Retry : Answer::Cancel;   // XP ends the operation after an error
    }
    Confirm cf;
    cf.c = &c;
    int tpl = c.kind == Conflict::MoveReadOnly ? IDD_OP_MOVEFILE : c.folder ? IDD_OP_REPLACEFOLDER : IDD_OP_REPLACEFILE;
    INT_PTR id = DialogBoxParamW(dialogs::module(), MAKEINTRESOURCEW(tpl), dialog_owner(r), confirm_proc, (LPARAM)&cf);
    for (HICON i : cf.icons) if (i) DestroyIcon(i);
    switch (id) {
    case IDYES: return Answer::Yes;
    case IDC_OP_YESTOALL: return Answer::YesToAll;
    case IDNO: return Answer::No;
    case ID_NOTOALL: return Answer::NoToAll;
    default: return Answer::Cancel;
    }
}

static void poll(Running& r)
{
    if (r.asking || r.finished) return;
    Conflict c;
    if (r.job->question(&c)) {
        r.asking = true;
        Answer a = ask(r, c);
        r.asking = false;
        r.job->answer(a);
        return;
    }
    if (r.job->done()) r.finished = true;
}

// every 100 ms, from whichever message loop runs (a menu, another dialog): questions get asked
static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD)
{
    std::vector<Running*> now = g_running;
    for (Running* r : now)
        if (std::find(g_running.begin(), g_running.end(), r) != g_running.end()) poll(*r);
}

Result run(HWND owner, Op op, const std::vector<std::string>& sources, const std::string& dest_dir)
{
    Job job(op, sources, dest_dir, notify, nullptr, true);
    Running r;
    r.job = &job;
    r.op = op;
    r.owner = owner ? owner : GetActiveWindow();
    g_running.push_back(&r);
    if (!g_timer) g_timer = SetTimer(nullptr, 0, 100, tick);
    job.start();
    bool quit = false;
    int quit_code = 0;
    for (;;) {
        poll(r);
        if (r.finished) break;
        if (quit) { usleep(10000); continue; }
        MSG m;
        if (!GetMessageW(&m, nullptr, 0, 0)) {
            // the program is ending: stop (the current item is rolled back), WM_QUIT stays posted
            quit = true;
            quit_code = (int)m.wParam;
            job.cancel();
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_running.erase(std::find(g_running.begin(), g_running.end(), &r));
    if (g_running.empty() && g_timer) { KillTimer(nullptr, g_timer); g_timer = 0; }
    if (quit) PostQuitMessage(quit_code);
    return job.result();
}

// shell::paste_items' user interface (shell/fileops.h)
static bool paste_ui(const std::vector<std::string>& sources, const std::string& dest_dir, bool move, std::string* err)
{
    Result res = run(nullptr, move ? Op::Move : Op::Copy, sources, dest_dir);
    if (!res.ok && err) *err = res.errors.empty() ? std::string() : res.summary();
    return res.ok;
}

static struct Install { Install() { shell::set_paste_ui(paste_ui); } } g_install;

} // namespace fileopui
