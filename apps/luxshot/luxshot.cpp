// luxshot: lhde's screenshot tool, the first application on the public entry point (wux/wux.h, shell/shell.h,
// apps/app.mk; docs/APPLICATIONS.md). docs/USAGE.md "Screenshots" is the user's side.
//
//   luxshot [--screen | --window [--no-frame] | --region] [--delay S] [--desc TEXT] [--tags A,B] [FILE.png]
//
// It captures first (capture.h), then shows one small window at once with the picture scaled to fit, the file name
// (Screenshot YYYY-MM-DD HHMMSS.png in XDG_PICTURES_DIR, else ~/Pictures), the folder with Browse..., a Description and
// Tags (written as user.lux.desc and user.xdg.tags by the shell layer's annotation writer, as the Explorer writes them),
// Save (Enter), Copy (image/png on the clipboard), Open Folder (the Explorer with the file selected) and Cancel (Escape).
// The PNG is encoded on Save or Copy. With FILE it writes FILE and exits without a window (scripts, tests). After a Copy
// it stays, without a window, until another program takes the clipboard (an X clipboard lives in its owner).
// Exit status: 0 saved or copied, 1 cancelled or failed, 2 bad usage.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>
#include "wux/wux.h"
#include "shell/shell.h"
#include "capture.h"
#include "resource.h"

WUX_RESOURCES(luxshot_res, "apps/luxshot/luxshot.res");

namespace {

struct State {
    capture::Shot shot;
    capture::Mode mode = capture::Mode::Screen;
    HBITMAP full = nullptr;            // the picture as a DIB section (the PNG's source)
    HBITMAP preview = nullptr;         // scaled to the preview's box once, at start
    int pw = 0, ph = 0;                // its size
    std::string folder, name, desc, tags;
    std::string saved;                 // the file written (stdout)
    bool copied = false;
};
State g;

void usage(FILE* f)
{
    fputs("usage: luxshot [--screen | --window [--no-frame] | --region] [--delay S] [--desc TEXT] [--tags A,B] [FILE.png]\n"
          "  --screen        the whole screen (default)\n"
          "  --window        the active window with its frame; --no-frame: the window alone\n"
          "  --region        a rectangle dragged with the mouse (Escape or the right button cancel)\n"
          "  --delay S       wait S seconds first\n"
          "  --desc TEXT     the description (user.lux.desc), --tags A,B the tags (user.xdg.tags)\n"
          "  FILE.png        write FILE.png and exit, no window\n"
          "Without FILE a window shows the picture to save it, copy it or open its folder.\n"
          "Exit status: 0 saved or copied, 1 cancelled or failed, 2 usage error.\n", f);
}

// a 32bpp top-down DIB section of w x h; bits filled from px (0xAARRGGBB) when given
HBITMAP make_dib(int w, int h, const uint32_t* px, uint32_t** bits_out)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!b || !bits) return b;
    if (px) memcpy(bits, px, (size_t)w * h * 4);
    if (bits_out) *bits_out = (uint32_t*)bits;
    return b;
}

// the preview: the picture fitted into the box (never enlarged), each pixel the average of the source pixels it covers
void make_preview(int box_w, int box_h)
{
    const capture::Shot& s = g.shot;
    double k = std::min(1.0, std::min((double)box_w / s.width, (double)box_h / s.height));
    g.pw = std::max(1, (int)(s.width * k + 0.5));
    g.ph = std::max(1, (int)(s.height * k + 0.5));
    uint32_t* out = nullptr;
    g.preview = make_dib(g.pw, g.ph, nullptr, &out);
    if (!out) return;
    for (int y = 0; y < g.ph; y++) {
        int sy0 = (int)((long long)y * s.height / g.ph), sy1 = std::max(sy0 + 1, (int)((long long)(y + 1) * s.height / g.ph));
        for (int x = 0; x < g.pw; x++) {
            int sx0 = (int)((long long)x * s.width / g.pw), sx1 = std::max(sx0 + 1, (int)((long long)(x + 1) * s.width / g.pw));
            unsigned long r = 0, gr = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const uint32_t* row = &s.pixels[(size_t)sy * s.width];
                for (int sx = sx0; sx < sx1; sx++) { uint32_t p = row[sx]; r += p >> 16 & 255; gr += p >> 8 & 255; b += p & 255; n++; }
            }
            out[(size_t)y * g.pw + x] = 0xff000000u | (uint32_t)(r / n) << 16 | (uint32_t)(gr / n) << 8 | (uint32_t)(b / n);
        }
    }
}

std::string default_name()
{
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    char base[64];
    strftime(base, sizeof base, "Screenshot %Y-%m-%d %H%M%S", &tm);
    std::string folder = shell::user_dir("PICTURES", "Pictures"), name = std::string(base) + ".png";
    for (int i = 2; i < 100; i++) {   // two in the same second: "Screenshot ... (2).png", as the Explorer numbers copies
        struct stat st;
        if (stat((folder + "/" + name).c_str(), &st) != 0) break;
        name = std::string(base) + " (" + std::to_string(i) + ").png";
    }
    return name;
}

// the annotations, as the Explorer writes them (shell::details): an empty box writes nothing
bool annotate(const std::string& path, const std::string& desc, const std::string& tags, std::string* err)
{
    using namespace shell::details;
    if (!shell::trim(desc).empty() && !write(path, *property("description"), desc, err)) return false;
    if (!shell::trim(tags).empty() && !write(path, *property("tags"), tags, err)) return false;
    return true;
}

// the file name box and the folder box as one path ("x" -> folder/x.png; a name with a slash is a path of its own)
std::string target_path(const std::string& folder, std::string name)
{
    name = shell::trim(name);
    if (name.empty()) return std::string();
    if (name.size() < 4 || strcasecmp(name.c_str() + name.size() - 4, ".png") != 0) name += ".png";
    if (name[0] == '/') return name;
    if (name[0] == '~' && name.size() > 1 && name[1] == '/') return shell::home_dir() + name.substr(1);
    std::string f = shell::trim(folder);
    if (f == "~" || f.compare(0, 2, "~/") == 0) f = shell::home_dir() + f.substr(1);
    if (f.empty()) f = shell::user_dir("PICTURES", "Pictures");
    return f + "/" + name;
}

void error_box(HWND dlg, const std::string& text)
{
    filedlg::message_box(dlg, "Save Screenshot", text, MB_OK | MB_ICONEXCLAMATION);
}

void set_text(HWND dlg, int id, const std::string& s) { SetDlgItemTextW(dlg, id, (LPCWSTR)WuxWide(s).c_str()); }
std::string get_text(HWND dlg, int id) { return WuxWindowText(GetDlgItem(dlg, id)); }

bool save(HWND dlg)
{
    std::string path = target_path(get_text(dlg, IDC_FOLDER), get_text(dlg, IDC_NAME));
    if (path.empty()) { error_box(dlg, "Type a file name."); SetFocus(GetDlgItem(dlg, IDC_NAME)); return false; }
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) { error_box(dlg, shell::base_of(path) + " is a folder."); return false; }
        if (filedlg::message_box(dlg, "Save Screenshot", shell::base_of(path) + " already exists.\nDo you want to replace it?",
                                 MB_YESNO | MB_ICONEXCLAMATION) != IDYES) return false;
    }
    shell::make_dirs(shell::dir_of(path));
    if (!FlxpWritePng(g.full, path.c_str())) { error_box(dlg, "Cannot write " + path + "."); return false; }
    std::string err;
    if (!annotate(path, get_text(dlg, IDC_DESC), get_text(dlg, IDC_TAGS), &err))
        error_box(dlg, "The picture is saved, but its description or tags are not: " + err);
    g.saved = path;
    return true;
}

void copy(HWND dlg)
{
    HGLOBAL png = FlxpEncodePng(g.full);
    if (!png) { error_box(dlg, "Cannot make a PNG of the picture."); return; }
    OpenClipboard(dlg);
    EmptyClipboard();
    SetClipboardData(RegisterClipboardFormatW(L"image/png"), png);
    CloseClipboard();
    g.copied = true;
    set_text(dlg, IDC_INFO, "Copied to the clipboard.");
}

// the Explorer on the folder, the file selected when it is there
void open_folder(HWND dlg)
{
    std::string path = target_path(get_text(dlg, IDC_FOLDER), get_text(dlg, IDC_NAME)), folder = shell::dir_of(path);
    std::string flxp = shell::exe_dir() + "/flxp";
    if (access(flxp.c_str(), X_OK) != 0) flxp = "flxp";
    struct stat st;
    std::vector<std::string> argv = { flxp };
    if (!path.empty() && stat(path.c_str(), &st) == 0) argv.insert(argv.end(), { "--select", path });
    else argv.push_back(path.empty() || stat(folder.c_str(), &st) != 0 ? shell::user_dir("PICTURES", "Pictures") : folder);
    std::string err;
    if (!shell::spawn(argv, shell::home_dir(), &err)) error_box(dlg, err);
}

void browse(HWND dlg)
{
    std::string path = target_path(get_text(dlg, IDC_FOLDER), get_text(dlg, IDC_NAME));
    filedlg::Options o;
    o.mode = filedlg::Mode::Save;
    o.title = "Save Screenshot As";
    o.default_name = path;
    o.filters = { filedlg::Filter{ "PNG (*.png)", { "*.png" }, {} } };
    o.default_ext = "png";
    o.overwrite_prompt = false;   // asked on Save
    filedlg::Result r = filedlg::run(dlg, o);
    if (!r.ok || r.paths.empty()) return;
    set_text(dlg, IDC_FOLDER, shell::dir_of(r.paths[0]));
    set_text(dlg, IDC_NAME, shell::base_of(r.paths[0]));
}

std::string info_line()
{
    std::string s = std::to_string(g.shot.width) + " x " + std::to_string(g.shot.height) + " pixels, ";
    if (g.mode == capture::Mode::Screen) return s + "whole screen";
    if (g.mode == capture::Mode::Region) return s + "region at " + std::to_string(g.shot.x) + ", " + std::to_string(g.shot.y);
    return s + "window \"" + g.shot.what + "\"";
}

INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        RECT r;
        GetClientRect(GetDlgItem(dlg, IDC_PREVIEW), &r);
        make_preview(r.right - r.left, r.bottom - r.top);
        set_text(dlg, IDC_INFO, info_line());
        set_text(dlg, IDC_NAME, g.name);
        set_text(dlg, IDC_FOLDER, g.folder);
        set_text(dlg, IDC_DESC, g.desc);
        set_text(dlg, IDC_TAGS, g.tags);
        HWND name = GetDlgItem(dlg, IDC_NAME);
        SetFocus(name);
        SendMessageW(name, EM_SETSEL, 0, -1);
        return FALSE;   // the focus is set
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lp;
        if (di->CtlID != IDC_PREVIEW) break;
        RECT rc = di->rcItem;
        FillRect(di->hDC, &rc, GetSysColorBrush(COLOR_APPWORKSPACE));   // Paint's canvas surround
        int x = rc.left + (rc.right - rc.left - g.pw) / 2, y = rc.top + (rc.bottom - rc.top - g.ph) / 2;
        HDC mem = CreateCompatibleDC(di->hDC);
        HGDIOBJ old = SelectObject(mem, g.preview);
        BitBlt(di->hDC, x, y, g.pw, g.ph, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: if (save(dlg)) EndDialog(dlg, IDOK); return TRUE;
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        case IDC_COPY: copy(dlg); return TRUE;
        case IDC_OPENFOLDER: open_folder(dlg); return TRUE;
        case IDC_BROWSE: browse(dlg); return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

int main(int argc, char** argv)
{
    bool frame = true;
    double delay = 0;
    std::string out;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "luxshot: %s needs a value\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--screen") g.mode = capture::Mode::Screen;
        else if (a == "--window") g.mode = capture::Mode::Window;
        else if (a == "--region") g.mode = capture::Mode::Region;
        else if (a == "--no-frame") frame = false;
        else if (a == "--delay") delay = atof(value().c_str());
        else if (a == "--desc") g.desc = value();
        else if (a == "--tags") g.tags = value();
        else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else if (a.size() > 1 && a[0] == '-') { fprintf(stderr, "luxshot: unknown option %s\n", a.c_str()); usage(stderr); return 2; }
        else if (out.empty()) out = a;
        else { usage(stderr); return 2; }
    }

    std::string err;
    if (!capture::take(g.mode, frame, delay, g.shot, &err)) {
        if (!err.empty()) fprintf(stderr, "luxshot: %s\n", err.c_str());
        return 1;
    }
    WuxInit();
    g.full = make_dib(g.shot.width, g.shot.height, g.shot.pixels.data(), nullptr);
    if (!g.full) { fprintf(stderr, "luxshot: no memory for a %d x %d picture\n", g.shot.width, g.shot.height); return 1; }

    if (!out.empty()) {   // a script's: the file, its annotations, no window
        if (!FlxpWritePng(g.full, out.c_str())) { fprintf(stderr, "luxshot: cannot write %s\n", out.c_str()); return 1; }
        if (!annotate(out, g.desc, g.tags, &err)) { fprintf(stderr, "luxshot: %s: %s\n", out.c_str(), err.c_str()); return 1; }
        return 0;
    }

    g.folder = shell::user_dir("PICTURES", "Pictures");
    g.name = default_name();
    HINSTANCE res = WuxLoadResources(luxshot_res_start, (size_t)(luxshot_res_end - luxshot_res_start));
    INT_PTR r = DialogBoxParamW(res, MAKEINTRESOURCEW(IDD_LUXSHOT), nullptr, dlg_proc, 0);
    if (!g.saved.empty()) { printf("%s\n", g.saved.c_str()); fflush(stdout); }
    // an X clipboard lives in its owner: answer requests until another program takes it
    while (g.copied && FlxpClipboardOwned()) usleep(200 * 1000);
    return r == IDOK || g.copied ? 0 : 1;
}
