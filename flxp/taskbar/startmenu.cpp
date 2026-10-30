// The classic start menu (start_menu = classic): a popup menu as the classic
// (Windows 2000 / "Classic Start menu") one, without its large icons and
// banner: Programs (the .desktop applications by their main category), My
// Documents, My Computer, Settings (Display Properties, the session themes),
// Run..., Log Off and Shut Down. XP's two-column start panel is startpanel.cpp
// (it borrows Programs and Settings from here). Also the Run dialog and the
// window menu of a task button.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "shell/settings.h"
#include "shell/apps.h"
#include "shell/config.h"
#include "shell/lhtheme.h"
#include "taskbar.h"

namespace tb {
namespace startmenu {

namespace {

enum : UINT {
    CMD_DOCUMENTS = 1, CMD_COMPUTER, CMD_DISPLAY, CMD_RUN, CMD_LOGOFF, CMD_SHUTDOWN,
    CMD_THEME = 100,            // + index into the theme list (liblhde)
    CMD_APP = 1000,             // + index into the application list
};

// freedesktop main categories (Desktop Menu spec) -> the Programs submenus, in menu order
struct Group { const char* title; const char* cats[4]; };
const Group groups[] = {
    { "Accessories", { "Utility", nullptr } },
    { "Development", { "Development", nullptr } },
    { "Education", { "Education", "Science", nullptr } },
    { "Games", { "Game", nullptr } },
    { "Graphics", { "Graphics", nullptr } },
    { "Internet", { "Network", nullptr } },
    { "Multimedia", { "AudioVideo", "Audio", "Video", nullptr } },
    { "Office", { "Office", nullptr } },
    { "System Tools", { "System", "Settings", nullptr } },
    { "Other", { nullptr } },
};

std::vector<const shell::App*> app_cmds;
std::vector<std::pair<std::string, std::string>> theme_cmds;   // section, title
HWND run_wnd, run_combo;
const UINT ID_RUN_COMBO = 100;

int group_of(const shell::App& a)
{
    int n = (int)(sizeof groups / sizeof groups[0]);
    for (int g = 0; g < n - 1; g++)
        for (const char* const* c = groups[g].cats; *c; c++)
            for (const std::string& ac : a.categories)
                if (ac == *c) return g;
    return n - 1;
}

void append(HMENU m, UINT id, const std::string& text, UINT flags = 0)
{
    std::u16string w = W(text);
    AppendMenuW(m, MF_STRING | flags, id, (LPCWSTR)w.c_str());
}

void append_popup(HMENU m, HMENU sub, const std::string& text)
{
    std::u16string w = W(text);
    AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR)sub, (LPCWSTR)w.c_str());
}

// the Programs menus' 16 px icons (XP's start menu has them): kept until the next Programs menu is built
std::vector<HBITMAP> menu_bitmaps;

void set_bitmap(HMENU m, int pos, HBITMAP b)
{
    if (!b) return;
    menu_bitmaps.push_back(b);
    MENUITEMINFOW mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIIM_BITMAP;
    mi.hbmpItem = b;
    SetMenuItemInfoW(m, (UINT)pos, TRUE, &mi);
}

std::string menu_text(std::string s)
{
    for (size_t p = 0; (p = s.find('&', p)) != std::string::npos; p += 2) s.insert(p, "&");   // no mnemonics from names
    return s;
}

HMENU programs_menu()
{
    shell::reload_apps();
    app_cmds.clear();
    const int n = (int)(sizeof groups / sizeof groups[0]);
    std::vector<std::vector<const shell::App*>> by(n);
    for (const shell::App& a : shell::all_apps()) {
        if (a.no_display || a.name.empty()) continue;
        by[group_of(a)].push_back(&a);
    }
    for (HBITMAP b : menu_bitmaps) DeleteObject(b);    // the previous Programs menu is destroyed by now
    menu_bitmaps.clear();
    HMENU m = CreatePopupMenu();
    for (int gi = 0; gi < n; gi++) {
        if (by[gi].empty()) continue;
        std::sort(by[gi].begin(), by[gi].end(), [](const shell::App* a, const shell::App* b) {
            return strcasecmp(a->name.c_str(), b->name.c_str()) < 0;
        });
        HMENU sub = CreatePopupMenu();
        for (const shell::App* a : by[gi]) {
            append(sub, CMD_APP + (UINT)app_cmds.size(), menu_text(a->name));
            set_bitmap(sub, GetMenuItemCount(sub) - 1, startpanel::menu_bitmap(a->icon, "exe"));
            app_cmds.push_back(a);
        }
        append_popup(m, sub, groups[gi].title);
        set_bitmap(m, GetMenuItemCount(m) - 1, startpanel::menu_bitmap("", "folder"));
    }
    if (app_cmds.empty()) append(m, 0, "(Empty)", MF_GRAYED);
    return m;
}

// the theme files as liblhde lists them (the user's, the shipped ones; the bare styles are Display Properties' Appearance
// tab now; docs/THEME-FORMAT.md), for Settings > Theme
HMENU themes_menu()
{
    theme_cmds.clear();
    const shell::SessionTheme& cur = shell::session_theme();
    std::vector<shell::store::ThemeEntry> list = shell::store::theme_files();
    if (list.empty()) return nullptr;
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < list.size(); i++) {
        if (i && list[i].group != list[i - 1].group) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        theme_cmds.push_back({ list[i].name, list[i].title.empty() ? list[i].name : list[i].title });
        append(m, CMD_THEME + (UINT)i, menu_text(theme_cmds[i].second), theme_cmds[i].first == cur.name ? MF_CHECKED : 0);
    }
    return m;
}

std::string self_dir()
{
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    std::string p(buf, (size_t)n);
    return p.substr(0, p.rfind('/'));
}

bool executable(const std::string& p) { return access(p.c_str(), X_OK) == 0; }

std::string find_in_path(const std::string& name)
{
    const char* path = getenv("PATH");
    std::string s = path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(':', a);
        if (b == std::string::npos) b = s.size();
        std::string p = s.substr(a, b - a) + "/" + name;
        if (executable(p)) return p;
        a = b + 1;
    }
    return "";
}

std::string home()
{
    const char* h = getenv("HOME");
    return h ? h : "/";
}

void report(const std::string& what, const std::string& err)
{
    std::u16string t = W(what + "\n\n" + err);
    MessageBoxW(g.wnd, (LPCWSTR)t.c_str(), L"Taskbar", MB_OK | MB_ICONERROR);
}

bool run_shell(const std::string& cmd)
{
    std::string err;
    if (!shell::spawn({ "/bin/sh", "-c", cmd }, home(), &err)) { report("Cannot run " + cmd, err); return false; }
    return true;
}

// ---- Run --------------------------------------------------------------------------------

std::string history_file() { return shell::state_file("flxp-run-history"); }   // what the Run box remembers: state, not a setting

std::vector<std::string> load_history()
{
    std::vector<std::string> h;
    std::ifstream f(history_file());
    std::string l;
    while (std::getline(f, l)) if (!l.empty()) h.push_back(l);
    return h;
}

void save_history(const std::string& cmd)
{
    std::vector<std::string> h = load_history();
    h.erase(std::remove(h.begin(), h.end(), cmd), h.end());
    h.insert(h.begin(), cmd);
    if (h.size() > 26) h.resize(26);      // XP's RunMRU: a..z
    std::string p = history_file();
    mkdir(p.substr(0, p.rfind('/')).c_str(), 0755);
    std::ofstream f(p);
    for (const std::string& s : h) f << s << "\n";
}

std::string expand_home(const std::string& s)
{
    if (s == "~" || s.rfind("~/", 0) == 0) return home() + s.substr(1);
    return s;
}

// a folder opens in the explorer, a file with its program, anything else is a command line
bool run_command(const std::string& line)
{
    std::string s = line;
    s.erase(0, s.find_first_not_of(" \t"));
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.empty()) return false;
    std::string p = expand_home(s);
    struct stat st;
    if (stat(p.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) { open_folder(p); return true; }
        if (!(st.st_mode & 0111)) {
            std::string err;
            if (!shell::open_files({ p }, &err)) { report("Cannot open " + p, err); return false; }
            return true;
        }
    }
    if (s.find("://") != std::string::npos && s.find(' ') == std::string::npos) {
        std::string err;
        if (shell::spawn({ "xdg-open", s }, home(), &err)) return true;
    }
    return run_shell(s);
}

LRESULT CALLBACK run_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            WCHAR buf[2048] = {};
            GetWindowTextW(run_combo, buf, 2047);
            std::string cmd = U(buf);
            if (run_command(cmd)) {
                save_history(cmd);
                DestroyWindow(h);
            }
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(h); return 0; }
        if (LOWORD(wp) == ID_RUN_COMBO && (HIWORD(wp) == CBN_EDITCHANGE || HIWORD(wp) == CBN_SELCHANGE)) {
            EnableWindow(GetDlgItem(h, IDOK), GetWindowTextLengthW(run_combo) > 0 || HIWORD(wp) == CBN_SELCHANGE);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        run_wnd = nullptr;
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_3DFACE);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND child(HWND parent, const WCHAR* cls, const WCHAR* text, DWORD style, int x, int y, int w, int hgt, UINT id, HFONT f)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, hgt, parent, (HMENU)(UINT_PTR)id, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)f, FALSE);
    return c;
}

} // namespace

// ---- session commands (SHELL-DESIGN 6.2) ------------------------------------------------

void log_off()
{
    if (MessageBoxW(g.wnd, L"Are you sure you want to log off?", L"Log Off", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    std::string c = setting("logoff_command");
    if (!c.empty()) { run_shell(c); return; }
    // lhwm: `lhwmc quit` ends the window manager cleanly; that ends a session whose last program it is
    std::string wm = xwm::wm_name();
    if (wm.find("lhwm") != std::string::npos || wm.find("berry") != std::string::npos) {
        std::string exe = xwm::wm_executable();
        std::string ctl = exe.empty() ? "" : exe.substr(0, exe.rfind('/')) + "/lhwmc";
        if (ctl.empty() || !executable(ctl)) ctl = find_in_path("lhwmc");
        if (!ctl.empty()) {
            std::string err;
            if (shell::spawn({ ctl, "quit" }, home(), &err)) { PostQuitMessage(0); return; }
        }
    }
    PostQuitMessage(0);     // no known session manager: the session shell (us) ends
}

void shut_down()
{
    std::string c = setting("shutdown_command", "loginctl poweroff");
    std::u16string t = W("Turn off the computer?\n\n(" + c + ")");
    if (MessageBoxW(g.wnd, (LPCWSTR)t.c_str(), L"Shut Down", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    run_shell(c);
}

void open_folder(const std::string& path)
{
    std::string dir = self_dir(), bin;
    for (const std::string& c : { dir + "/flxp" })
        if (executable(c)) { bin = c; break; }
    if (bin.empty()) bin = find_in_path("flxp");
    if (bin.empty()) { report("Cannot open " + path, "flxp (the explorer) was not found"); return; }
    std::string err;
    if (!shell::spawn({ bin, path }, path[0] == '/' ? path : home(), &err)) report("Cannot open " + path, err);
}

void run_dialog()
{
    if (run_wnd) { SetForegroundWindow(run_wnd); return; }
    static bool registered;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = run_proc;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
        wc.lpszClassName = L"FlxpRunDialog";
        RegisterClassW(&wc);
        registered = true;
    }
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    static HFONT font;
    if (!font) font = CreateFontIndirectW(&ncm.lfMessageFont);
    // XP's Run (shell32 RunFileDlg, DLG_RUN): 347 x 151 client at 96 dpi
    RECT r = { 0, 0, 347, 151 };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = g.edge == EdgeLeft ? g.screen.right + 6 : 6, y = g.edge == EdgeBottom ? sh - g.thickness - (r.bottom - r.top) - 6 : g.edge == EdgeTop ? g.thickness + 6 : sh - (r.bottom - r.top) - 6;
    run_wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"FlxpRunDialog", L"Run", style, x, y, r.right - r.left, r.bottom - r.top,
                              nullptr, nullptr, nullptr, nullptr);
    HWND ic = child(run_wnd, L"STATIC", nullptr, SS_ICON, 11, 11, 32, 32, 0xffff, font);
    SendMessageW(ic, STM_SETICON, (WPARAM)LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION), 0);
    child(run_wnd, L"STATIC", L"Type the name of a program, folder, document, or Internet resource, and Windows will open it for you.",
          0, 52, 11, 284, 30, 0xffff, font);
    child(run_wnd, L"STATIC", L"&Open:", 0, 11, 59, 36, 13, 0xffff, font);
    run_combo = child(run_wnd, L"COMBOBOX", nullptr, CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 52, 56, 284, 200, ID_RUN_COMBO, font);
    std::vector<std::string> hist = load_history();
    for (const std::string& s : hist) {
        std::u16string w = W(s);
        SendMessageW(run_combo, CB_ADDSTRING, 0, (LPARAM)w.c_str());
    }
    HWND ok = child(run_wnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 104, 118, 75, 23, IDOK, font);
    child(run_wnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 184, 118, 75, 23, IDCANCEL, font);
    HWND browse = child(run_wnd, L"BUTTON", L"&Browse...", BS_PUSHBUTTON | WS_TABSTOP, 264, 118, 75, 23, 0x3001, font);
    EnableWindow(browse, FALSE);      // phase 2 (the file dialog)
    if (!hist.empty()) {
        std::u16string w = W(hist[0]);
        SetWindowTextW(run_combo, (LPCWSTR)w.c_str());
        SendMessageW(run_combo, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
    } else {
        EnableWindow(ok, FALSE);
    }
    ShowWindow(run_wnd, SW_SHOW);
    SetForegroundWindow(run_wnd);
    SetFocus(run_combo);
}

bool dialog_message(MSG* m)
{
    return run_wnd && IsDialogMessageW(run_wnd, m);
}

bool menu_open;

void close()
{
    if (menu_open) EndMenu();
}

void show(const RECT& b, bool anchored)
{
    HMENU m = CreatePopupMenu();
    append_popup(m, programs_menu(), "&Programs");
    append(m, CMD_DOCUMENTS, "My &Documents");
    append(m, CMD_COMPUTER, "My &Computer");
    append_popup(m, settings_popup(), "&Settings");
    append(m, CMD_RUN, "&Run...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    const char* user = getenv("USER");
    append(m, CMD_LOGOFF, std::string("&Log Off ") + (user ? user : "") + "...");
    append(m, CMD_SHUTDOWN, "Sh&ut Down...");

    UINT flags = TPM_RETURNCMD | TPM_LEFTBUTTON;
    int x = b.left, y = b.top;
    if (anchored) {
        bool up = b.top >= GetSystemMetrics(SM_CYSCREEN) / 2;
        flags |= TPM_LEFTALIGN | (up ? TPM_BOTTOMALIGN : TPM_TOPALIGN);
        y = up ? b.top : b.bottom;
    } else switch (g.edge) {
    case EdgeBottom: flags |= TPM_LEFTALIGN | TPM_BOTTOMALIGN; break;
    case EdgeTop: flags |= TPM_LEFTALIGN | TPM_TOPALIGN; y = b.bottom; break;
    case EdgeLeft: flags |= TPM_LEFTALIGN | TPM_TOPALIGN; x = b.right; break;
    case EdgeRight: flags |= TPM_RIGHTALIGN | TPM_TOPALIGN; break;
    }
    uint64_t prev = band::active_window();
    xwm::focus_own();       // the menu's keyboard (a dock is never focused by the window manager)
    menu_open = true;
    xwm::set_start_menu_open(true);     // _LH_STARTMENU_OPEN for lux's Start button
    UINT cmd = (UINT)TrackPopupMenu(m, flags, x, y, 0, g.wnd, nullptr);   // modal: every way of closing ends it
    menu_open = false;
    xwm::set_start_menu_open(false);    // before the command (Run... is a dialog)
    DestroyMenu(m);
    if (!cmd && prev) xwm::activate(prev);   // dismissed: the keyboard goes back where it was
    command(cmd);
}

void launch(const shell::App& a)
{
    std::string err;
    if (!shell::launch(a, {}, &err)) { report("Cannot start " + a.name, err); return; }
    startpanel::count_launch(a.id);
}

bool run_line(const std::string& cmd) { return run_shell(cmd); }

HMENU programs_popup() { return programs_menu(); }

HMENU settings_popup()
{
    HMENU settings = CreatePopupMenu();
    append(settings, CMD_DISPLAY, "&Display Properties...");
    if (HMENU th = themes_menu()) append_popup(settings, th, "&Theme");
    return settings;
}

bool command(UINT cmd)
{
    if (cmd >= CMD_APP && cmd - CMD_APP < app_cmds.size()) {
        launch(*app_cmds[cmd - CMD_APP]);
    } else if (cmd >= CMD_THEME && cmd - CMD_THEME < theme_cmds.size()) {
        // settings.ini [theme] name, _LH_THEME, _LH_SETTINGS_CHANGED (shell/settings.h)
        if (!shell::store::apply_theme(theme_cmds[cmd - CMD_THEME].first)) xwm::set_root_utf8("_LH_THEME", theme_cmds[cmd - CMD_THEME].first);
    } else switch (cmd) {
    case CMD_DOCUMENTS: open_folder(home()); break;
    case CMD_COMPUTER: open_folder("::computer"); break;
    case CMD_DISPLAY:
        if (!shell::store::launch_settings()) report("Cannot start Display Properties", shell::store::settings_program());
        break;
    case CMD_RUN: run_dialog(); break;
    case CMD_LOGOFF: log_off(); break;
    case CMD_SHUTDOWN: shut_down(); break;
    default: return false;
    }
    return true;
}

void window_menu(uint64_t id, const xwm::Task& t, POINT pt)
{
    // the system menu explorer shows for a task button (Move/Size need the window manager's keyboard modes: left out)
    HMENU m = CreatePopupMenu();
    append(m, SC_RESTORE, "&Restore", t.minimized || t.maximized ? 0 : MF_GRAYED);
    append(m, SC_MINIMIZE, "Mi&nimize", t.minimized ? MF_GRAYED : 0);
    append(m, SC_MAXIMIZE, "Ma&ximize", t.maximized && !t.minimized ? MF_GRAYED : 0);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    append(m, SC_CLOSE, "&Close\tAlt+F4");
    SetMenuDefaultItem(m, SC_CLOSE, FALSE);
    uint64_t prev = band::active_window();
    xwm::focus_own();
    UINT cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | (g.edge == EdgeBottom ? TPM_BOTTOMALIGN : TPM_TOPALIGN),
                                    pt.x, pt.y, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (!cmd && prev) xwm::activate(prev);
    switch (cmd) {
    case SC_RESTORE:
        if (t.minimized) xwm::activate(id);
        else xwm::maximize(id, 0);
        break;
    case SC_MINIMIZE: xwm::minimize(id); break;
    case SC_MAXIMIZE:
        if (t.minimized) xwm::activate(id);
        xwm::maximize(id, 1);
        break;
    case SC_CLOSE: xwm::close(id); break;
    default: break;
    }
}

} // namespace startmenu
} // namespace tb
