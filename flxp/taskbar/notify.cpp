// The notification area's parts besides the XEmbed icons (those are xwm.cpp's): XP's chevron, which hides the inactive
// icons ([taskbar] hide_inactive, XP's "Hide inactive icons"), the tooltips of the docked icons, and our own three items,
// as luxshell's tray tiles have them: Volume (XP's tray volume popup: a slider with Mute), Brightness (the same popup
// without Mute) and Touchpad (a click turns it on or off), read and set through shell/traycontrol.h on a thread of their own.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "shell/apps.h"
#include "shell/config.h"
#include "shell/settings.h"
#include "shell/traycontrol.h"
#include "taskbar.h"
extern "C" {
#include "w32/flxp_shell.h"
}

namespace tb {
namespace notify {

namespace {

enum { OWN_VOLUME, OWN_BRIGHTNESS, OWN_TOUCHPAD, OWN_COUNT };
enum { TIMER_HOVER = 40, TIMER_DEMOTE = 41, TIMER_POPUP = 42 };
const int SLOT = 18;                    // a 16 px icon and XP's 2 px (TB_SETPADDING 2, 2 on the tray's toolbar)

struct State {
    shell::tray::Volume vol;
    shell::tray::Backlight bl;
    shell::tray::Touchpad tp;
};

// ---- the controls' thread ------------------------------------------------------------
std::mutex lock;
std::condition_variable cv;
State state;                            // what the thread read last
std::vector<std::function<void()>> todo;   // commands, run in order; a newer one of a kind replaces a waiting one
std::vector<int> todo_kind;
bool quit, have_state;
std::thread worker;

void post_changed() { PostMessageW(g.wnd, WM_TB_TRAYCTL, 0, 0); }

void thread_main()
{
    long tick = 0;
    for (;;) {
        std::vector<std::function<void()>> run;
        {
            std::unique_lock<std::mutex> l(lock);
            cv.wait_for(l, std::chrono::milliseconds(1000), [] { return quit || !todo.empty(); });
            if (quit) return;
            run.swap(todo);
            todo_kind.clear();
        }
        for (auto& f : run) f();
        State s;
        s.vol = shell::tray::read_volume();
        s.bl = shell::tray::read_backlight();
        {
            std::lock_guard<std::mutex> l(lock);
            s.tp = (tick % 2 == 0 || !run.empty() || !have_state) ? shell::tray::read_touchpad() : state.tp;   // every 2 s
        }
        tick++;
        bool changed;
        {
            std::lock_guard<std::mutex> l(lock);
            changed = !have_state || s.vol.percent != state.vol.percent || s.vol.muted != state.vol.muted || s.vol.ok != state.vol.ok ||
                      s.vol.error != state.vol.error || s.bl.present != state.bl.present || s.bl.percent != state.bl.percent ||
                      s.tp.present != state.tp.present || s.tp.enabled != state.tp.enabled;
            state = s;
            have_state = true;
        }
        if (changed) post_changed();
    }
}

void command(int kind, std::function<void()> f)
{
    {
        std::lock_guard<std::mutex> l(lock);
        for (size_t i = 0; i < todo_kind.size(); i++)
            if (todo_kind[i] == kind) { todo[i] = f; cv.notify_one(); return; }
        todo.push_back(f);
        todo_kind.push_back(kind);
    }
    cv.notify_one();
}

State current()
{
    std::lock_guard<std::mutex> l(lock);
    return state;
}

// ---- our icons, drawn: none of the imported art has a speaker, a sun or a touchpad -------------------------------
struct Px { double r, g, b, a; };

// a shape over a 16 x 16 premultiplied-in-the-end canvas, 4 x 4 samples per pixel
template <class F> void shape(std::vector<Px>& c, double r, double g, double b, F inside)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int n = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) n += inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4);
            double a = n / 16.0;
            if (a <= 0) continue;
            Px& p = c[(size_t)y * 16 + x];
            p.r = r * a + p.r * (1 - a); p.g = g * a + p.g * (1 - a); p.b = b * a + p.b * (1 - a);
            p.a = a + p.a * (1 - a);
        }
}

// XP's "not available" overlay at the lower right: a white disc, a red ring and slash
void no_overlay(std::vector<Px>& c)
{
    double cx = 11.5, cy = 11.5;
    shape(c, 1, 1, 1, [&](double x, double y) { return hypot(x - cx, y - cy) <= 4.4; });
    shape(c, 0.85, 0.1, 0.1, [&](double x, double y) { double d = hypot(x - cx, y - cy); return d <= 4.2 && d >= 2.9; });
    shape(c, 0.85, 0.1, 0.1, [&](double x, double y) {
        double u = (x - cx + y - cy) / sqrt(2.0);
        return fabs((x - cx) - (y - cy)) / sqrt(2.0) <= 0.75 && fabs(u) <= 3.2;
    });
}

std::vector<Px> speaker(int percent, bool muted)
{
    std::vector<Px> c(256, Px{ 0, 0, 0, 0 });
    shape(c, 0.25, 0.25, 0.25, [](double x, double y) { return x >= 1.5 && x <= 5.5 && y >= 5.5 && y <= 10.5; });
    shape(c, 0.25, 0.25, 0.25, [](double x, double y) { return x >= 4.5 && x <= 9.5 && fabs(y - 8) <= 2.5 + (x - 4.5) * 0.9; });
    shape(c, 0.62, 0.62, 0.62, [](double x, double y) { return x >= 2.5 && x <= 4.6 && y >= 6.5 && y <= 9.5; });
    shape(c, 0.62, 0.62, 0.62, [](double x, double y) { return x >= 5.3 && x <= 8.6 && fabs(y - 8) <= 1.8 + (x - 5.3) * 0.75; });
    int waves = muted ? 0 : percent <= 0 ? 0 : percent < 50 ? 1 : 2;
    for (int w = 0; w < waves; w++) {
        double rad = 3.0 + 2.6 * w;
        shape(c, 0.11, 0.32, 0.75, [&](double x, double y) {
            double d = hypot(x - 9.5, y - 8);
            return x > 10.0 && fabs(d - rad) <= 0.65 && fabs(y - 8) < rad * 0.8;
        });
    }
    if (muted) no_overlay(c);
    return c;
}

std::vector<Px> sun()
{
    std::vector<Px> c(256, Px{ 0, 0, 0, 0 });
    for (int k = 0; k < 8; k++) {
        double a = k * M_PI / 4, dx = cos(a), dy = sin(a);
        shape(c, 0.93, 0.55, 0.05, [&](double x, double y) {
            double u = (x - 8) * dx + (y - 8) * dy, v = -(x - 8) * dy + (y - 8) * dx;
            return u >= 4.6 && u <= 7.4 && fabs(v) <= 0.7;
        });
    }
    shape(c, 0.93, 0.55, 0.05, [](double x, double y) { return hypot(x - 8, y - 8) <= 4.2; });
    shape(c, 1.0, 0.85, 0.2, [](double x, double y) { return hypot(x - 8, y - 8) <= 3.1; });
    return c;
}

std::vector<Px> pad(bool on)
{
    std::vector<Px> c(256, Px{ 0, 0, 0, 0 });
    shape(c, 0.3, 0.3, 0.3, [](double x, double y) { return x >= 1.5 && x <= 14.5 && y >= 2.5 && y <= 13.5; });
    shape(c, 0.78, 0.78, 0.78, [](double x, double y) { return x >= 2.5 && x <= 13.5 && y >= 3.5 && y <= 9.5; });
    shape(c, 0.6, 0.6, 0.6, [](double x, double y) { return x >= 2.5 && x <= 7.6 && y >= 10.5 && y <= 12.5; });
    shape(c, 0.6, 0.6, 0.6, [](double x, double y) { return x >= 8.4 && x <= 13.5 && y >= 10.5 && y <= 12.5; });
    if (!on) no_overlay(c);
    return c;
}

HICON to_icon(const std::vector<Px>& c)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = -16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;
    uint8_t* p = (uint8_t*)bits;
    for (int i = 0; i < 256; i++) {     // straight alpha, as an icon's
        double a = c[(size_t)i].a;
        p[i * 4 + 0] = (uint8_t)lround(a > 0 ? c[(size_t)i].b / a * 255 : 0);
        p[i * 4 + 1] = (uint8_t)lround(a > 0 ? c[(size_t)i].g / a * 255 : 0);
        p[i * 4 + 2] = (uint8_t)lround(a > 0 ? c[(size_t)i].r / a * 255 : 0);
        p[i * 4 + 3] = (uint8_t)lround(a * 255);
    }
    static const uint8_t zeros[16 * 2] = {};
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, zeros);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return ic;
}

// ---- the area ---------------------------------------------------------------------------
struct Own {
    int kind;
    bool shown = false;
    HICON icon = nullptr;
    std::string tip, key;               // key: what the icon shows (redrawn when it changes)
    RECT rc = {};                       // in the tray window
};
Own own[OWN_COUNT] = { { OWN_VOLUME }, { OWN_BRIGHTNESS }, { OWN_TOUCHPAD } };
bool enabled;                           // our items at all (the tray runs)
bool expanded;                          // the chevron shows the demoted icons (XP's "bang menu" open)
bool have_demoted;                      // at least two inactive icons: the chevron is there
RECT chevron_rc;
bool chevron_hot, chevron_down;
HTHEME th_chevron, th_chevron_open;
HWND tips, hover_tip;
uint64_t hover;                         // the docked icon under the pointer
std::string hover_name;
int hover_rect[4];
HWND popup;                             // the volume / brightness popup
int popup_kind = -1;

long idle_limit_ms()
{
    long s = atol(shell::setting("tray_idle", "3600").c_str());   // XP: TrayIconResidentInterval, an hour
    return (s > 0 ? s : 3600) * 1000;
}

bool hide_inactive() { return shell::store::flag("taskbar", "hide_inactive", true); }

void set_tip(int id, const RECT& r, const std::string& text)
{
    if (!tips) return;
    std::u16string t = W(text);
    TTTOOLINFOW ti = {};
    ti.cbSize = sizeof ti;
    ti.hwnd = g.tray;
    ti.uId = (UINT_PTR)id;
    ti.rect = r;
    ti.lpszText = (LPWSTR)t.c_str();
    ti.uFlags = TTF_SUBCLASS;
    SendMessageW(tips, TTM_DELTOOLW, 0, (LPARAM)&ti);
    if (r.right > r.left) SendMessageW(tips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void refresh_own(bool relayout)
{
    State s = current();
    bool before[OWN_COUNT];
    for (int k = 0; k < OWN_COUNT; k++) before[k] = own[k].shown;
    own[OWN_VOLUME].shown = enabled;
    own[OWN_BRIGHTNESS].shown = enabled && s.bl.present;
    own[OWN_TOUCHPAD].shown = enabled && s.tp.present;
    for (Own& o : own) {
        std::string key, tip;
        std::vector<Px> px;
        if (o.kind == OWN_VOLUME) {
            key = std::to_string(s.vol.ok ? (s.vol.percent <= 0 ? 0 : s.vol.percent < 50 ? 1 : 2) : 0) + (s.vol.muted ? "m" : "");
            tip = s.vol.ok || !s.vol.error.empty() ? shell::tray::volume_tip(s.vol) : "Volume";
            if (key != o.key) px = speaker(s.vol.ok ? s.vol.percent : 0, s.vol.muted);
        } else if (o.kind == OWN_BRIGHTNESS) {
            key = "sun"; tip = shell::tray::backlight_tip(s.bl);
            if (key != o.key) px = sun();
        } else {
            key = s.tp.enabled ? "on" : "off"; tip = shell::tray::touchpad_tip(s.tp);
            if (key != o.key) px = pad(s.tp.enabled);
        }
        if (!px.empty()) {
            if (o.icon) DestroyIcon(o.icon);
            o.icon = to_icon(px);
            o.key = key;
        }
        if (tip != o.tip) { o.tip = tip; set_tip(1 + o.kind, o.rc, tip); }
    }
    bool moved = false;
    for (int k = 0; k < OWN_COUNT; k++) moved |= before[k] != own[k].shown;
    if (relayout && moved) relayout_bar();
    InvalidateRect(g.tray, nullptr, FALSE);
}

// ---- the popup: XP's tray volume control ---------------------------------------------------
enum { IDC_SLIDER = 100, IDC_MUTE = 101 };

void popup_sync()
{
    if (!popup) return;
    State s = current();
    HWND sl = GetDlgItem(popup, IDC_SLIDER);
    if (GetCapture() != sl) {
        int v = popup_kind == OWN_VOLUME ? s.vol.percent : s.bl.percent;
        SendMessageW(sl, TBM_SETPOS, TRUE, 100 - (v > 100 ? 100 : v));   // loud at the top
    }
    if (HWND m = GetDlgItem(popup, IDC_MUTE)) SendMessageW(m, BM_SETCHECK, s.vol.muted ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK popup_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_VSCROLL: {
        int v = 100 - (int)SendMessageW(GetDlgItem(h, IDC_SLIDER), TBM_GETPOS, 0, 0);
        if (popup_kind == OWN_VOLUME) command(1, [v] { shell::tray::set_volume(v); });
        else command(2, [v] { shell::tray::set_backlight(v); });
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_MUTE && HIWORD(wp) == BN_CLICKED) {
            bool on = SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
            command(3, [on] { shell::tray::set_mute(on ? 1 : 0); });
        }
        return 0;
    case WM_ACTIVATE:                   // XP's popup closes when it loses the focus
        if (LOWORD(wp) == WA_INACTIVE) DestroyWindow(h);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_ERASEBKGND: {               // the face and a dialog frame's raised edges
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, GetSysColorBrush(COLOR_3DFACE));
        DrawEdge((HDC)wp, &rc, EDGE_RAISED, BF_RECT);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, GetSysColor(COLOR_3DFACE));
        return (LRESULT)GetSysColorBrush(COLOR_3DFACE);
    case WM_DESTROY:
        if (h == popup) { popup = nullptr; popup_kind = -1; }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// XP's sndvol32 /t: the caption "Volume" centred at the top, a vertical trackbar loud at the top with ticks on both sides,
// "Mute" under it, in a raised dialog frame above the tray; the pixel layout is ours (no reference of the popup is measured)
void open_popup(int kind, const RECT& icon_screen)
{
    if (popup) { bool same = popup_kind == kind; DestroyWindow(popup); if (same) return; }
    static bool reg;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = popup_proc;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
        wc.lpszClassName = L"FlxpTrayVolume";
        RegisterClassW(&wc);
        reg = true;
    }
    bool vol = kind == OWN_VOLUME;
    int cw = 72, ch = vol ? 160 : 136;
    RECT r = { 0, 0, cw + 6, ch + 6 };   // a 3 px raised dialog frame of our own: a framed window would get luxwm's caption
    int w = r.right - r.left, hgt = r.bottom - r.top;
    int x = (icon_screen.left + icon_screen.right) / 2 - w / 2, y = g.screen.top - hgt;
    if (g.edge == EdgeTop) y = g.screen.bottom;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    if (x + w > sw) x = sw - w;
    if (x < 0) x = 0;
    popup = CreateWindowExW(WS_EX_FLXP_DOCK | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"FlxpTrayVolume", vol ? L"Volume" : L"Brightness",
                            WS_POPUP, x, y, w, hgt, g.wnd, nullptr, nullptr, nullptr);
    popup_kind = kind;
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    HFONT f = CreateFontIndirectW(&ncm.lfMessageFont);
    HWND cap = CreateWindowExW(0, L"Static", vol ? L"Volume" : L"Brightness", WS_CHILD | WS_VISIBLE | SS_CENTER, 3, 9, cw, 14, popup,
                               nullptr, nullptr, nullptr);
    HWND sl = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_VERT | TBS_BOTH | TBS_AUTOTICKS,
                              3 + (cw - 32) / 2, 27, 32, 110, popup, (HMENU)IDC_SLIDER, nullptr, nullptr);
    SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(sl, TBM_SETTICFREQ, 10, 0);
    SendMessageW(sl, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(sl, TBM_SETLINESIZE, 0, vol ? 2 : 5);
    SendMessageW(cap, WM_SETFONT, (WPARAM)f, FALSE);
    if (vol) {
        HWND m = CreateWindowExW(0, L"Button", L"&Mute", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 15, 141, cw - 16, 16,
                                 popup, (HMENU)IDC_MUTE, nullptr, nullptr);
        SendMessageW(m, WM_SETFONT, (WPARAM)f, FALSE);
    }
    popup_sync();
    ShowWindow(popup, SW_SHOW);
    SetForegroundWindow(popup);
    SetFocus(sl);
    SetTimer(g.wnd, TIMER_POPUP, 100, nullptr);   // a press outside closes it, as losing the focus does
}

// ---- the chevron ---------------------------------------------------------------------------
void draw_chevron(HDC hdc)
{
    RECT rc = chevron_rc;
    if (rc.right <= rc.left) return;
    HTHEME th = expanded ? th_chevron_open : th_chevron;
    if (th) {
        int state = chevron_down ? PBS_PRESSED : chevron_hot ? PBS_HOT : PBS_NORMAL;
        DrawThemeBackground(th, hdc, BP_PUSHBUTTON, state, &rc, nullptr);
        return;
    }
    // classic: a flat button (raised when hot, sunken when pressed) with a double arrow: two 2 px strokes 3 rows each way,
    // pointing left while the icons are hidden (right when they are shown)
    if (chevron_hot || chevron_down) DrawEdge(hdc, &rc, chevron_down ? BDR_SUNKENOUTER : BDR_RAISEDINNER, BF_RECT);
    if (chevron_down) OffsetRect(&rc, 1, 1);
    int seg = GetSystemMetrics(SM_CXVSCROLL) / 7;
    if (seg < 2) seg = 2;
    bool flipped = g.horizontal() ? expanded : !expanded;
    HBRUSH b = GetSysColorBrush(COLOR_BTNTEXT);
    if (g.horizontal()) {
        int x = (rc.left + rc.right - 4 * seg) / 2;
        if (!flipped) x += seg;
        int yb = (rc.top + rc.bottom) / 2;
        for (int y = -seg; y <= seg; y++) {
            RECT a = { x, yb + y, x + seg, yb + y + 1 }, c = { x + 2 * seg, yb + y, x + 3 * seg, yb + y + 1 };
            FillRect(hdc, &a, b); FillRect(hdc, &c, b);
            x += (flipped ? (y < 0) : (y >= 0)) ? 1 : -1;
        }
    } else {
        int y = (rc.top + rc.bottom - 4 * seg) / 2;
        if (!flipped) y += seg;
        int xb = (rc.left + rc.right) / 2;
        for (int x = -seg; x <= seg; x++) {
            RECT a = { xb + x, y, xb + x + 1, y + seg }, c = { xb + x, y + 2 * seg, xb + x + 1, y + 3 * seg };
            FillRect(hdc, &a, b); FillRect(hdc, &c, b);
            y += (flipped ? (x < 0) : (x >= 0)) ? -1 : 1;
        }
    }
}

void update_chevron_tip()
{
    set_tip(10, chevron_rc, expanded ? "Hide" : "Show hidden icons");   // XP's
}

void own_click(Own& o, UINT msg)
{
    RECT sr = o.rc;
    MapWindowPoints(g.tray, nullptr, (POINT*)&sr, 2);
    if (o.kind == OWN_TOUCHPAD) {
        State s = current();
        bool on = !s.tp.enabled;
        command(4, [on] { shell::tray::set_touchpad(on); });
        return;
    }
    if (msg == WM_MBUTTONUP && o.kind == OWN_VOLUME) { command(3, [] { shell::tray::set_mute(-1); }); return; }
    open_popup(o.kind, sr);
}

// the program a Volume Control item opens: the mixers XP's sndvol32 stands in for
std::string mixer()
{
    for (const char* c : { "pavucontrol", "pwvucontrol", "gnome-control-center sound", "xfce4-mixer" }) {
        std::string prog = c;
        prog = prog.substr(0, prog.find(' '));
        std::string path = getenv("PATH") ? getenv("PATH") : "/usr/bin";
        size_t i = 0;
        while (i <= path.size()) {
            size_t j = path.find(':', i);
            if (j == std::string::npos) j = path.size();
            if (access((path.substr(i, j - i) + "/" + prog).c_str(), X_OK) == 0) return c;
            i = j + 1;
        }
    }
    return "";
}

void own_menu(Own& o, POINT pt)
{
    HMENU m = CreatePopupMenu();
    State s = current();
    enum { CMD_OPEN = 1, CMD_MUTE, CMD_SLIDER, CMD_TOUCH };
    if (o.kind == OWN_VOLUME) {
        std::string mx = mixer();
        AppendMenuW(m, MF_STRING | (mx.empty() ? MF_GRAYED : 0), CMD_OPEN, L"&Open Volume Control");
        AppendMenuW(m, MF_STRING | (s.vol.muted ? MF_CHECKED : 0), CMD_MUTE, L"&Mute");
        SetMenuDefaultItem(m, CMD_OPEN, FALSE);
    } else if (o.kind == OWN_BRIGHTNESS) {
        AppendMenuW(m, MF_STRING, CMD_SLIDER, L"&Brightness...");
        SetMenuDefaultItem(m, CMD_SLIDER, FALSE);
    } else {
        AppendMenuW(m, MF_STRING | (s.tp.enabled ? MF_CHECKED : 0), CMD_TOUCH, L"&Touchpad on");
    }
    xwm::focus_own();
    UINT cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | (g.edge == EdgeBottom ? TPM_BOTTOMALIGN : TPM_TOPALIGN),
                                    pt.x, pt.y, 0, g.wnd, nullptr);
    DestroyMenu(m);
    if (cmd == CMD_OPEN) startmenu::run_line(mixer());
    else if (cmd == CMD_MUTE) command(3, [] { shell::tray::set_mute(-1); });
    else if (cmd == CMD_SLIDER) own_click(o, WM_LBUTTONUP);
    else if (cmd == CMD_TOUCH) own_click(o, WM_LBUTTONUP);
}

} // namespace

void start(bool tray_running)
{
    enabled = tray_running && shell::setting_flag("tray_controls", true);
    tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, g.wnd,
                           nullptr, nullptr, nullptr);
    hover_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, g.wnd,
                                nullptr, nullptr, nullptr);
    TTTOOLINFOW ti = {};
    ti.cbSize = sizeof ti;
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd = g.wnd;
    ti.uId = 1;
    ti.lpszText = (LPWSTR)L"";
    SendMessageW(hover_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    if (enabled) worker = std::thread(thread_main);
    long every = idle_limit_ms() / 2;   // an icon becomes inactive while nothing happens: looked at twice per interval
    SetTimer(g.wnd, TIMER_DEMOTE, (UINT)(every < 30000 ? (every < 250 ? 250 : every) : 30000), nullptr);
    refresh_own(false);
}

void stop()
{
    {
        std::lock_guard<std::mutex> l(lock);
        quit = true;
    }
    cv.notify_one();
    if (worker.joinable()) worker.join();
    if (popup) DestroyWindow(popup);
}

void theme_changed()
{
    if (th_chevron) CloseThemeData(th_chevron);
    if (th_chevron_open) CloseThemeData(th_chevron_open);
    th_chevron = g.theme ? OpenThemeData(nullptr, g.horizontal() ? L"TrayNotifyHoriz::Button" : L"TrayNotifyVert::Button") : nullptr;
    th_chevron_open = g.theme ? OpenThemeData(nullptr, g.horizontal() ? L"TrayNotifyHorizOpen::Button" : L"TrayNotifyVertOpen::Button")
                              : nullptr;
}

int own_count()
{
    int n = 0;
    for (const Own& o : own) n += o.shown;
    return n;
}

int slot() { return SLOT; }

// XP's chevron: the Button part's true size under a style (Luna 19 x 20), a small icon's size classic
SIZE chevron_size()
{
    SIZE s = { GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON) };
    if (th_chevron) {
        SIZE t = {};
        if (SUCCEEDED(GetThemePartSize(th_chevron, nullptr, BP_PUSHBUTTON, PBS_DEFAULTED, nullptr, TS_TRUE, &t)) && t.cx > 0) s = t;
    }
    return s;
}

bool chevron_shown() { return have_demoted; }

// Which docked icons are inactive (unchanged for an hour, flxp.ini tray_idle seconds) and hidden behind the chevron: none
// while the chevron is open, none with [taskbar] hide_inactive = 0, and only when two or more would go, as XP's
bool demote()
{
    std::vector<xwm::TrayIcon> v = xwm::tray_icons();
    std::vector<uint64_t> idle;
    long limit = idle_limit_ms();
    for (const xwm::TrayIcon& t : v) if (t.idle_ms >= limit) idle.push_back(t.id);
    bool had = have_demoted;
    have_demoted = hide_inactive() && idle.size() >= 2;
    std::vector<uint64_t> hide = have_demoted && !expanded ? idle : std::vector<uint64_t>();
    bool change = had != have_demoted;
    for (const xwm::TrayIcon& t : v) {
        bool h = false;
        for (uint64_t id : hide) if (id == t.id) h = true;
        if (h != t.hidden) change = true;
    }
    if (change) xwm::tray_hide(hide);
    return change;
}

void place(const RECT& chevron, const RECT& own_icons)
{
    chevron_rc = chevron;
    update_chevron_tip();
    int k = 0;
    for (Own& o : own) {
        if (!o.shown) { o.rc = RECT{}; set_tip(1 + o.kind, o.rc, o.tip); continue; }
        bool h = g.horizontal();
        int per = h ? 1 : (own_icons.right - own_icons.left) / SLOT;
        if (per < 1) per = 1;
        int x = h ? own_icons.left + k * SLOT : own_icons.left + (k % per) * SLOT;
        int y = h ? own_icons.top : own_icons.top + (k / per) * SLOT;
        o.rc = RECT{ x, y, x + SLOT, y + (h ? own_icons.bottom - own_icons.top : SLOT) };
        set_tip(1 + o.kind, o.rc, o.tip);
        k++;
    }
    InvalidateRect(g.tray, nullptr, FALSE);
}

void paint(HDC hdc)
{
    draw_chevron(hdc);
    for (Own& o : own) {
        if (!o.shown || !o.icon) continue;
        int x = o.rc.left + (o.rc.right - o.rc.left - 16) / 2, y = o.rc.top + (o.rc.bottom - o.rc.top - 16) / 2;
        DrawIconEx(hdc, x, y, o.icon, 16, 16, 0, nullptr, DI_NORMAL);
    }
}

bool mouse(UINT msg, POINT p)
{
    bool on_chevron = PtInRect(&chevron_rc, p);
    if (msg == WM_MOUSEMOVE) {
        if (on_chevron != chevron_hot) {
            chevron_hot = on_chevron;
            InvalidateRect(g.tray, &chevron_rc, TRUE);
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, g.tray, 0 };
            TrackMouseEvent(&t);
        }
        return false;
    }
    if (msg == WM_MOUSELEAVE) {
        if (chevron_hot || chevron_down) { chevron_hot = chevron_down = false; InvalidateRect(g.tray, &chevron_rc, TRUE); }
        return true;
    }
    if (on_chevron) {
        if (msg == WM_LBUTTONDOWN) { chevron_down = true; InvalidateRect(g.tray, &chevron_rc, TRUE); return true; }
        if (msg == WM_LBUTTONUP && chevron_down) {
            chevron_down = false;
            expanded = !expanded;
            demote();
            update_chevron_tip();
            relayout_bar();
            return true;
        }
        return msg == WM_LBUTTONDBLCLK;
    }
    for (Own& o : own) {
        if (!o.shown || !PtInRect(&o.rc, p)) continue;
        if (msg == WM_LBUTTONUP || msg == WM_MBUTTONUP) own_click(o, msg);
        else if (msg == WM_RBUTTONUP) {
            POINT s = p;
            ClientToScreen(g.tray, &s);
            own_menu(o, s);
        }
        return msg != WM_MOUSEMOVE;
    }
    return false;
}

// the wheel over our items: a volume step of 2 %, a brightness step of 5 %
bool wheel(POINT screen, int delta)
{
    POINT p = screen;
    ScreenToClient(g.tray, &p);
    for (Own& o : own) {
        if (!o.shown || !PtInRect(&o.rc, p)) continue;
        State s = current();
        int steps = delta / WHEEL_DELTA;
        if (o.kind == OWN_VOLUME) {
            int v = s.vol.percent + 2 * steps;
            command(1, [v] { shell::tray::set_volume(v); });
        } else if (o.kind == OWN_BRIGHTNESS) {
            int v = s.bl.percent + 5 * steps;
            command(2, [v] { shell::tray::set_backlight(v); });
        }
        return true;
    }
    return false;
}

void controls_changed()
{
    refresh_own(true);
    popup_sync();
}

// the docked icon under the pointer: its name as a tooltip after XP's initial delay, at the icon (as a tooltip control
// places one for a toolbar button)
void x_changed(const xwm::Snapshot& s)
{
    if (s.tray_hover == hover && s.tray_hover_name == hover_name) return;
    hover = s.tray_hover;
    hover_name = s.tray_hover_name;
    memcpy(hover_rect, s.tray_hover_rect, sizeof hover_rect);
    SendMessageW(hover_tip, TTM_TRACKACTIVATE, FALSE, 0);
    KillTimer(g.wnd, TIMER_HOVER);
    if (hover && !hover_name.empty()) SetTimer(g.wnd, TIMER_HOVER, (UINT)GetDoubleClickTime(), nullptr);
}

bool timer(UINT_PTR id)
{
    if (id == TIMER_DEMOTE) { if (demote()) relayout_bar(); return true; }
    if (id == TIMER_POPUP) {
        int x, y;
        unsigned b = 0;
        RECT pr, tr;
        if (!popup) { KillTimer(g.wnd, TIMER_POPUP); return true; }
        GetWindowRect(popup, &pr);
        GetWindowRect(g.tray, &tr);   // the tray's own click toggles it
        POINT p;
        if (xwm::pointer_state(&x, &y, &b) && (b & 0x700)) {   // X's Button1Mask .. Button3Mask
            p.x = x; p.y = y;
            if (!PtInRect(&pr, p) && !PtInRect(&tr, p)) DestroyWindow(popup);
        }
        return true;
    }
    if (id != TIMER_HOVER) return false;
    KillTimer(g.wnd, TIMER_HOVER);
    if (!hover || hover_name.empty()) return true;
    std::u16string t = W(hover_name);
    TTTOOLINFOW ti = {};
    ti.cbSize = sizeof ti;
    ti.hwnd = g.wnd;
    ti.uId = 1;
    ti.lpszText = (LPWSTR)t.c_str();
    SendMessageW(hover_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    // above the icon on a bottom bar, below it on a top one
    SendMessageW(hover_tip, TTM_TRACKPOSITION, 0, MAKELPARAM(hover_rect[0], g.edge == EdgeTop ? hover_rect[1] + hover_rect[3] + 2
                                                                                                  : hover_rect[1] - 22));
    SendMessageW(hover_tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    if (getenv("FLXP_TASKBAR_LOG")) fprintf(stderr, "flxp-taskbar: tooltip %s\n", hover_name.c_str());
    return true;
}

std::string hover_text() { return hover && IsWindowVisible(hover_tip) ? hover_name : ""; }
bool popup_open() { return popup != nullptr; }

} // namespace notify
} // namespace tb
