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

// ---- message box (copy and rename errors) ---------------------------------------------

struct Box { std::string title, text; UINT type; };   // MB_OK, MB_RETRYCANCEL or MB_YESNO, and an MB_ICON*

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
        UINT icon = b->type & MB_ICONMASK;
        SendMessageW(ic, STM_SETICON, (WPARAM)LoadIconW(nullptr, (LPCWSTR)(icon == MB_ICONEXCLAMATION ? IDI_EXCLAMATION : IDI_HAND)), 0);
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
        switch (b->type & MB_TYPEMASK) {
        case MB_RETRYCANCEL: ids = { IDRETRY, IDCANCEL }; break;
        case MB_YESNO: ids = { IDYES, IDNO }; break;
        default: ids = { IDOK };
        }
        for (int id : { IDOK, IDRETRY, IDCANCEL, IDYES, IDNO })
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
        MessageBeep(icon);
        SetFocus(GetDlgItem(h, ids[0]));
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: case IDRETRY: case IDYES: case IDNO: EndDialog(h, LOWORD(wp)); return TRUE;
        case IDCANCEL:   // Escape, the close box: OK, Cancel; a Yes/No box has no Cancel (user32: No)
            EndDialog(h, GetDlgItem(h, IDOK) ? IDOK : GetDlgItem(h, IDNO) ? IDNO : IDCANCEL);
            return TRUE;
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

// ---- the flying papers ------------------------------------------------------------------

// XP's animations are AVI resources of shell32 (IDA_FILEMOVE 160, IDA_FILECOPY
// 161: 272 x 60, 8 bpp RLE, 20 frames at 10 per second, each frame a delta on
// the one before). They are read from the imported shell32.dll and played as
// comctl32's animate control plays them with ACS_TRANSPARENT: the colour of the
// first frame's top-left pixel shows the background. Without the import a
// sheet of paper flies between two folder icons.
struct Animation {
    int w = 0, h = 0, frames = 0, ms = 100;
    std::vector<std::pair<const uint8_t*, size_t>> chunks;   // per frame, into data
    std::vector<uint8_t> data;
    uint32_t pal[256] = {};
    std::vector<uint8_t> idx;   // the current frame, palette indices, top row first
    int frame = -1;
    int key = -1;               // the transparent palette entry
    HICON folder = nullptr, paper = nullptr;   // the drawn fallback

    bool load(int id);
    void step();
    void paint(HDC hdc, const RECT& rc, HBRUSH bg);
    ~Animation() { if (folder) DestroyIcon(folder); if (paper) DestroyIcon(paper); }
};

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool Animation::load(int id)
{
    std::string dll = shell::resource_file("xp", "shell32.dll");
    res::Module* m = dll.empty() ? nullptr : res::load(dll.c_str());
    res::Blob b = {};
    bool found = m && res::find(m, res::Id("AVI"), res::Id((uint32_t)id), b);
    if (found) data.assign(b.data, b.data + b.size);
    if (m) res::unload(m);
    if (!found || data.size() < 12 || memcmp(data.data(), "RIFF", 4) || memcmp(data.data() + 8, "AVI ", 4)) {
        data.clear();
        chunks.clear();
        folder = uiicons::load_icon("folder", 32);
        paper = uiicons::load_icon("document", 32);
        w = 272; h = 60; frames = 20;
        return false;
    }
    // walk the RIFF: avih (frame time, size), strf (BITMAPINFOHEADER + palette), movi's 00dc/00db chunks
    const uint8_t* d = data.data();
    size_t n = data.size();
    std::vector<std::pair<size_t, size_t>> lists = { { 12, std::min<size_t>(n, 8 + rd32(d + 4)) } };
    while (!lists.empty()) {
        size_t off = lists.back().first, end = lists.back().second;
        lists.pop_back();
        while (off + 8 <= end) {
            const uint8_t* c = d + off;
            size_t size = rd32(c + 4);
            if (off + 8 + size > end) break;
            if (!memcmp(c, "LIST", 4) && size >= 4) lists.push_back({ off + 12, off + 8 + size });
            else if (!memcmp(c, "avih", 4) && size >= 40) { ms = std::max(10, (int)(rd32(c + 8) / 1000)); w = (int)rd32(c + 40); h = (int)rd32(c + 44); }
            else if (!memcmp(c, "strf", 4) && size >= 40) {
                uint32_t hs = rd32(c + 8), bpp = c[8 + 14] | c[8 + 15] << 8, comp = rd32(c + 8 + 16), used = rd32(c + 8 + 32);
                if (bpp != 8 || comp != 1) { data.clear(); return load(-1); }   // only XP's RLE8
                if (!used || used > 256) used = 256;
                for (uint32_t i = 0; i < used && 8 + hs + i * 4 + 4 <= size; i++) {
                    const uint8_t* q = c + 8 + hs + i * 4;
                    pal[i] = 0xff000000u | (uint32_t)q[2] << 16 | (uint32_t)q[1] << 8 | q[0];
                }
            } else if (!memcmp(c + 2, "dc", 2) || !memcmp(c + 2, "db", 2)) chunks.push_back({ c + 8, size });
            off += 8 + size + (size & 1);
        }
    }
    // chunks were collected list by list; movi's are the only frame chunks, in file order
    frames = (int)chunks.size();
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || !frames) { data.clear(); chunks.clear(); return load(-1); }
    idx.assign((size_t)w * h, 0);
    step();
    key = idx[0];
    return true;
}

// the next frame: RLE8 drawn over the previous one (a delta), the first again after the last
void Animation::step()
{
    frame = (frame + 1) % std::max(1, frames);
    if (data.empty()) return;
    if (frame == 0) std::fill(idx.begin(), idx.end(), key >= 0 ? (uint8_t)key : 0);
    const uint8_t* p = chunks[(size_t)frame].first;
    size_t n = chunks[(size_t)frame].second, i = 0;
    int x = 0, y = 0;   // y from the bottom
    auto put = [&](uint8_t v) { if (x < w && y < h) idx[(size_t)(h - 1 - y) * w + x] = v; x++; };
    while (i + 1 < n && y < h) {
        uint8_t a = p[i], b = p[i + 1];
        i += 2;
        if (a) { for (int k = 0; k < a; k++) put(b); continue; }
        if (b == 0) { x = 0; y++; continue; }
        if (b == 1) break;
        if (b == 2) { if (i + 1 >= n) break; x += p[i]; y += p[i + 1]; i += 2; continue; }
        if (i + b > n) break;
        for (int k = 0; k < b; k++) put(p[i + k]);
        i += (b + 1) & ~1u;
    }
}

void Animation::paint(HDC hdc, const RECT& rc, HBRUSH bg)
{
    FillRect(hdc, &rc, bg);
    if (data.empty()) {
        // two folders, a sheet flying from the left one to the right one on an arc
        if (folder) {
            DrawIconEx(hdc, rc.left + 12, rc.top + 20, folder, 32, 32, 0, nullptr, DI_NORMAL);
            DrawIconEx(hdc, rc.left + w - 44, rc.top + 20, folder, 32, 32, 0, nullptr, DI_NORMAL);
        }
        if (paper && frames > 1) {
            double t = (double)frame / (frames - 1);
            int px = rc.left + 12 + (int)(t * (w - 56)), py = rc.top + 16 - (int)(4 * t * (1 - t) * 18);
            DrawIconEx(hdc, px, py, paper, 32, 32, 0, nullptr, DI_NORMAL);
        }
        return;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); return; }
    uint32_t* px = (uint32_t*)bits;
    uint32_t kc = key >= 0 ? pal[key] : 0;
    for (size_t i = 0; i < idx.size(); i++) {
        uint32_t c = pal[idx[i]];
        px[i] = c == kc ? 0 : c;   // premultiplied: transparent is 0
    }
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, rc.left, rc.top, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
}

// ---- the running operations ----------------------------------------------------------

struct Running {
    Job* job = nullptr;
    Op op = Op::Copy;
    HWND owner = nullptr;
    bool asking = false;        // a question is open (the timer must not ask again)
    bool finished = false;
    bool same_fs = false;       // a move by renames: progress counts items only
    // the progress dialog (copy.c FOUIThreadProc, SetProgressText, SetProgressTime)
    HWND dlg = nullptr;
    DWORD start = 0, shown_at = 0;
    bool cancelled = false;
    Animation anim;
    std::string last_src, last_dst;
    DWORD est_time = 0;         // the time estimate's last sample
    uint64_t est_points = 0;
    uint64_t rate = 0;          // points per second, smoothed
    RECT place = {};            // where the template puts it
};

static std::vector<Running*> g_running;
static bool (*g_filter)(MSG&);
void set_message_filter(bool (*filter)(MSG&)) { g_filter = filter; }
static UINT_PTR g_timer;
enum { WM_OP_NOTIFY = WM_APP + 0x51 };

static void notify(void*) { PostThreadMessageW(0, WM_OP_NOTIFY, 0, 0); }   // wakes the waiting loop

// the questions belong to the progress dialog while it is up (copy.c: hwndDlgParent)
static HWND dialog_owner(Running& r)
{
    if (r.dlg && IsWindow(r.dlg) && IsWindowVisible(r.dlg)) return r.dlg;
    return IsWindow(r.owner) ? r.owner : nullptr;
}

static Answer ask(Running& r, const Conflict& c)
{
    if (c.kind == Conflict::Failed) {
        ErrorText et = error_text(r.op, c.error);
        Box b = { et.title, et.text, et.retry ? (UINT)(MB_RETRYCANCEL | MB_ICONEXCLAMATION) : (UINT)(MB_OK | MB_ICONHAND) };
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

// ---- the progress dialog -------------------------------------------------------------------

enum {
    SHOW_PROGRESS_TIMEOUT = 1000,   // copy.c: the dialog comes up when the operation still runs after a second
    MINSHOWTIME = 1000,             // and once up it stays at least a second
    MS_TIMESLICE = 2000,            // the time estimate is sampled every 2 seconds
    MIN_MINTIME4FEEDBACK = 5,       // and shown from 5 seconds left
    TIMER_ANIM = 1, TIMER_ESTIMATE = 2, TIMER_PLACE = 3,
};

// copy.c CountProgressPoints: a point per 32 KB (none for moves by rename), one per file and
// folder (two for a move across filesystems: the original is removed too)
static uint64_t points(const Running& r, uint64_t bytes, uint64_t items)
{
    if (r.op == Op::Move && r.same_fs) return items;
    return bytes / 32768 + items * (r.op == Op::Move ? 2 : 1);
}

// copy.c SetProgressTimeEst
static std::string time_text(uint64_t left)
{
    char buf[64];
    if (left > 4 * 60 * 60) return "";
    if (left > 60) snprintf(buf, sizeof buf, "%u Minutes Remaining", (unsigned)(left / 60 + 1));
    else snprintf(buf, sizeof buf, "%u Seconds Remaining", (unsigned)((left + 4) / 5 * 5));
    return buf;
}

// copy.c SetProgressText: the file's name, and "From 'a' to 'b'" with the two folders' names, each
// compacted to half the room the words leave
static void progress_text(Running& r, const Progress& p)
{
    if (p.current == r.last_src && p.current_dst == r.last_dst && !p.counting) return;
    r.last_src = p.current; r.last_dst = p.current_dst;
    HWND name = GetDlgItem(r.dlg, IDC_OP_NAME), to = GetDlgItem(r.dlg, IDC_OP_TONAME);
    RECT rc;
    GetClientRect(to, &rc);
    int width = rc.right;
    HDC hdc = GetDC(to);
    HGDIOBJ of = SelectObject(hdc, (HFONT)SendMessageW(r.dlg, WM_GETFONT, 0, 0));
    if (p.current.empty()) {
        SetWindowTextW(name, L"");
        SetWindowTextW(to, WS(std::string("Preparing to ") + (r.op == Op::Move ? "Move" : "Copy")));
    } else {
        SetWindowTextW(name, WS(compact(hdc, shell::base_of(p.current), width)));
        std::u16string words = W("From '' to ''");
        SIZE sz = {};
        GetTextExtentPoint32W(hdc, (LPCWSTR)words.c_str(), (int)words.size(), &sz);
        int half = (width - sz.cx) / 2;
        std::string from = shell::base_of(shell::dir_of(p.current)), dst = shell::base_of(shell::dir_of(p.current_dst));
        if (from.empty()) from = "/";
        if (dst.empty()) dst = "/";
        SetWindowTextW(to, WS("From '" + compact(hdc, from, half) + "' to '" + compact(hdc, dst, half) + "'"));
    }
    SelectObject(hdc, of);
    ReleaseDC(to, hdc);
}

// copy.c SetProgressTime: the rate over the last slice, averaged with the earlier one (1:2)
static void estimate(Running& r)
{
    Progress p = r.job->progress();
    if (p.counting) return;
    DWORD now = GetTickCount();
    uint64_t total = points(r, p.bytes_total, p.items_total), done = points(r, p.bytes_done, p.items_done);
    if (r.est_time) {
        uint64_t delta = done > r.est_points ? done - r.est_points : 0;
        if (delta && done) {
            DWORD tenths = std::max<DWORD>(1, (now - r.est_time) / 100);
            uint64_t rate = std::max<uint64_t>(1, delta * 10 / tenths);
            if (now - r.est_time < MS_TIMESLICE / 2 && r.rate) rate = r.rate;   // too short a sample
            if (r.rate) rate = (rate + r.rate * 2) / 3;
            if (rate) {
                r.rate = rate;
                uint64_t left = (total > done ? total - done : 0) / rate + 1;
                if (left >= MIN_MINTIME4FEEDBACK) SetDlgItemTextW(r.dlg, IDC_OP_TIMEEST, WS(time_text(left)));
            }
        }
    }
    r.est_time = now;
    r.est_points = done;
}

static void update_progress(Running& r)
{
    if (!r.dlg || !IsWindow(r.dlg) || r.cancelled) return;
    Progress p = r.job->progress();
    progress_text(r, p);
    HWND bar = GetDlgItem(r.dlg, IDC_OP_PROGRESS);
    uint64_t total = points(r, p.bytes_total, p.items_total), done = points(r, p.bytes_done, p.items_done);
    // PBM_SETRANGE32 takes ints: scaled down for the very large
    int shift = 0;
    while ((total >> shift) > 0x7fffffff) shift++;
    SendMessageW(bar, PBM_SETRANGE32, 0, (LPARAM)(total >> shift));
    SendMessageW(bar, PBM_SETPOS, (WPARAM)(std::min(done, total) >> shift), 0);
}

static INT_PTR CALLBACK progress_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Running* r = (Running*)GetWindowLongPtrW(h, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(h, DWLP_USER, lp);
        r = (Running*)lp;
        SetWindowTextW(h, r->op == Op::Move ? L"Moving..." : L"Copying...");
        // the animate control without ACS_CENTER takes the size of the clip
        r->anim.load(r->op == Op::Move ? 160 : 161);   // IDA_FILEMOVE, IDA_FILECOPY
        HWND a = GetDlgItem(h, IDC_OP_ANIMATE);
        SetWindowPos(a, nullptr, 0, 0, r->anim.w, r->anim.h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return FALSE;
    }
    case WM_SHOWWINDOW:
        if (wp && r) {
            SetTimer(h, TIMER_ANIM, (UINT)r->anim.ms, nullptr);
            SetTimer(h, TIMER_ESTIMATE, MS_TIMESLICE, nullptr);
        }
        return FALSE;
    case WM_ENABLE:   // a question is open: the clip pauses, the estimate restarts after it
        if (!r) return FALSE;
        if (wp) { r->est_time = GetTickCount(); r->est_points = 0; SetTimer(h, TIMER_ANIM, (UINT)r->anim.ms, nullptr); }
        else KillTimer(h, TIMER_ANIM);
        return FALSE;
    case WM_TIMER:
        if (!r) return FALSE;
        if (wp == TIMER_ANIM) {
            r->anim.step();
            InvalidateRect(GetDlgItem(h, IDC_OP_ANIMATE), nullptr, FALSE);
        } else if (wp == TIMER_ESTIMATE && IsWindowEnabled(h)) estimate(*r);
        else if (wp == TIMER_PLACE) {
            KillTimer(h, TIMER_PLACE);
            SetWindowPos(h, nullptr, r->place.left, r->place.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return TRUE;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lp;
        if (!r || di->CtlID != IDC_OP_ANIMATE) return FALSE;
        HBRUSH bg = (HBRUSH)SendMessageW(h, WM_CTLCOLORSTATIC, (WPARAM)di->hDC, (LPARAM)di->hwndItem);
        if (!bg) bg = GetSysColorBrush(COLOR_BTNFACE);
        r->anim.paint(di->hDC, di->rcItem, bg);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL && r) {   // copy.c: bAbort, and the dialog hides
            r->cancelled = true;
            r->job->cancel();
            ShowWindow(h, SW_HIDE);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

static void show_progress(Running& r)
{
    r.dlg = CreateDialogParamW(dialogs::module(), MAKEINTRESOURCEW(IDD_OP_PROGRESS), IsWindow(r.owner) ? r.owner : nullptr, progress_proc, (LPARAM)&r);
    if (!r.dlg) return;
    update_progress(r);
    // XP's template has no DS_CENTER: 20, 20 dialog units into the owner's client area; the window
    // manager centres a dialog on its parent when it maps it, so it is put back there just after
    GetWindowRect(r.dlg, &r.place);
    keep_above(r.dlg, false);
    ShowWindow(r.dlg, SW_SHOW);
    SetTimer(r.dlg, TIMER_PLACE, 60, nullptr);
    SetForegroundWindow(r.dlg);
    SetFocus(GetDlgItem(r.dlg, IDCANCEL));
    r.shown_at = GetTickCount();
    r.est_time = r.shown_at;
    r.est_points = 0;
}

// ---- running ------------------------------------------------------------------------------

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
    if (r.job->done()) { r.finished = true; update_progress(r); return; }
    if (!r.dlg && GetTickCount() - r.start >= SHOW_PROGRESS_TIMEOUT) show_progress(r);
    else update_progress(r);
}

// every 100 ms, from whichever message loop runs (a menu, another dialog): questions get asked,
// the progress dialog comes up and follows
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
    r.same_fs = op == Op::Move && !sources.empty() && shell::same_filesystem(shell::dir_of(sources[0]), dest_dir);
    r.start = GetTickCount();
    g_running.push_back(&r);
    if (!g_timer) g_timer = SetTimer(nullptr, 0, 100, tick);
    job.start();
    bool quit = false;
    int quit_code = 0;
    for (;;) {
        poll(r);
        // done; a progress dialog that came up stays a second at least (FOUIThreadProc)
        if (r.finished && (!r.dlg || r.cancelled || quit || GetTickCount() - r.shown_at >= MINSHOWTIME)) break;
        if (quit) { usleep(10000); continue; }
        MSG m;
        if (!GetMessageW(&m, nullptr, 0, 0)) {
            // the program is ending: stop (the current item is rolled back), WM_QUIT stays posted
            quit = true;
            quit_code = (int)m.wParam;
            job.cancel();
            continue;
        }
        if (r.dlg && IsWindow(r.dlg) && IsDialogMessageW(r.dlg, &m)) continue;
        if (g_filter && g_filter(m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (r.dlg && IsWindow(r.dlg)) DestroyWindow(r.dlg);
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
