// user32 shim, part 3: system metrics and parameters, monitors, clipboard,
// menu stubs, message boxes and other small things.
#include <stdio.h>
#include <string.h>
#include <map>
#include "user.h"
extern "C" {
#include "flxp_shell.h"
}
#include "sys/sys.h"
#include "gdi/font.h"

using namespace w32;

static NONCLIENTMETRICSW g_ncm;
static LOGFONTW g_icon_font;
static BOOL g_flat_menu = FALSE;   // SPI_GETFLATMENU; visual styles turn it on

static void set_font(LOGFONTW& lf, const char16_t* face, int height, int weight)
{
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    size_t n = 0; while (face[n] && n < LF_FACESIZE - 1) { lf.lfFaceName[n] = face[n]; n++; }
}

namespace w32 {
void init_sysparams()
{
    g_flat_menu = FALSE;    // a style's [SysMetrics] FlatMenus turns it on again; classic and a switch to it: off
    memset(&g_ncm, 0, sizeof g_ncm);
    g_ncm.cbSize = sizeof g_ncm;
    g_ncm.iBorderWidth = 1;
    g_ncm.iScrollWidth = 16;
    g_ncm.iScrollHeight = 16;
    g_ncm.iCaptionWidth = 18;
    g_ncm.iCaptionHeight = 18;
    g_ncm.iSmCaptionWidth = 13;
    g_ncm.iSmCaptionHeight = 15;
    g_ncm.iMenuWidth = 18;
    g_ncm.iMenuHeight = 18;
    g_ncm.iPaddedBorderWidth = 0;
    set_font(g_ncm.lfCaptionFont, u"Tahoma", -11, FW_BOLD);
    set_font(g_ncm.lfSmCaptionFont, u"Tahoma", -11, FW_NORMAL);
    set_font(g_ncm.lfMenuFont, u"Tahoma", -11, FW_NORMAL);
    set_font(g_ncm.lfStatusFont, u"Tahoma", -11, FW_NORMAL);
    set_font(g_ncm.lfMessageFont, u"Tahoma", -11, FW_NORMAL);
    set_font(g_icon_font, u"Tahoma", -11, FW_NORMAL);
    set_shell_dlg("Tahoma");
}
}

extern "C" {

INT WINAPI GetSystemMetrics(INT i)
{
    gdi::Size ss = sys::screen_size();
    switch (i) {
    case SM_CXSCREEN: return ss.cx;
    case SM_CYSCREEN: return ss.cy;
    // the scroll bar sizes follow SPI_SETNONCLIENTMETRICS (a visual style sets them: Luna 17)
    case SM_CXVSCROLL: case SM_CYVTHUMB: return g_ncm.iScrollWidth;
    case SM_CYHSCROLL: case SM_CXHTHUMB: return g_ncm.iScrollHeight;
    case SM_CYCAPTION: return g_ncm.iCaptionHeight + 1;
    case SM_CXBORDER: case SM_CYBORDER: return 1;
    case SM_CXDLGFRAME: case SM_CYDLGFRAME: return 3;
    case SM_CXICON: case SM_CYICON: case SM_CXCURSOR: case SM_CYCURSOR: return 32;
    case SM_CYMENU: return g_ncm.iMenuHeight + 1;
    case SM_CXFULLSCREEN: return ss.cx;
    case SM_CYFULLSCREEN: return ss.cy - g_ncm.iCaptionHeight - 1;
    case SM_CYKANJIWINDOW: return 0;
    case SM_MOUSEPRESENT: return 1;
    case SM_CYVSCROLL: return g_ncm.iScrollWidth;
    case SM_CXHSCROLL: return g_ncm.iScrollHeight;
    case SM_DEBUG: case SM_SWAPBUTTON: return 0;
    case SM_CXMIN: return 112;
    case SM_CYMIN: return 27;
    case SM_CXSIZE: case SM_CYSIZE: return 18;
    case SM_CXFRAME: case SM_CYFRAME: return 4;
    case SM_CXMINTRACK: return 112;
    case SM_CYMINTRACK: return 27;
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
    case SM_CXICONSPACING: case SM_CYICONSPACING: return 75;
    case SM_MENUDROPALIGNMENT: case SM_PENWINDOWS: case SM_DBCSENABLED: return 0;
    case SM_CMOUSEBUTTONS: return 3;
    case SM_SECURE: return 0;
    case SM_CXEDGE: case SM_CYEDGE: return 2;
    case SM_CXMINSPACING: return 160;
    case SM_CYMINSPACING: return 24;
    case SM_CXSMICON: case SM_CYSMICON: return 16;
    case SM_CYSMCAPTION: return g_ncm.iSmCaptionHeight;
    case SM_CXSMSIZE: case SM_CYSMSIZE: return 13;
    case SM_CXMENUSIZE: case SM_CYMENUSIZE: return 18;
    case SM_ARRANGE: return 8;
    case SM_CXMINIMIZED: return 160;
    case SM_CYMINIMIZED: return 24;
    case SM_CXMAXTRACK: return ss.cx + 12;
    case SM_CYMAXTRACK: return ss.cy + 12;
    case SM_CXMAXIMIZED: return ss.cx + 8;
    case SM_CYMAXIMIZED: return ss.cy + 8;
    case SM_NETWORK: return 3;
    case SM_CLEANBOOT: return 0;
    case SM_CXDRAG: case SM_CYDRAG: return 4;
    case SM_SHOWSOUNDS: return 0;
    case SM_CXMENUCHECK: case SM_CYMENUCHECK: return 13;
    case SM_SLOWMACHINE: case SM_MIDEASTENABLED: return 0;
    case SM_MOUSEWHEELPRESENT: return 1;
    case SM_XVIRTUALSCREEN: case SM_YVIRTUALSCREEN: return 0;
    case SM_CXVIRTUALSCREEN: return ss.cx;
    case SM_CYVIRTUALSCREEN: return ss.cy;
    case SM_CMONITORS: return 1;
    case SM_SAMEDISPLAYFORMAT: return 1;
    case SM_IMMENABLED: return 0;
    case SM_CXFOCUSBORDER: case SM_CYFOCUSBORDER: return 1;
    case SM_TABLETPC: case SM_MEDIACENTER: case SM_STARTER: case SM_SERVERR2: return 0;
    case SM_REMOTESESSION: case SM_SHUTTINGDOWN: case SM_REMOTECONTROL: return 0;
    default: return 0;
    }
}

INT WINAPI GetSystemMetricsForDpi(INT i, UINT) { return GetSystemMetrics(i); }

static void adjust_window_rect(RECT* rect, DWORD style, BOOL menu, DWORD ex, const NONCLIENTMETRICSW* ncm)
{
    int adjust = 0;
    if ((ex & (WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME)) == WS_EX_STATICEDGE) adjust = 1;
    else if ((ex & WS_EX_DLGMODALFRAME) || (style & (WS_THICKFRAME | WS_DLGFRAME))) adjust = 2;
    if (style & WS_THICKFRAME) adjust += ncm->iBorderWidth + ncm->iPaddedBorderWidth;
    if ((style & (WS_BORDER | WS_DLGFRAME)) || (ex & WS_EX_DLGMODALFRAME)) adjust++;
    InflateRect(rect, adjust, adjust);
    if ((style & WS_CAPTION) == WS_CAPTION) {
        if (ex & WS_EX_TOOLWINDOW) rect->top -= ncm->iSmCaptionHeight + 1;
        else rect->top -= ncm->iCaptionHeight + 1;
    }
    if (menu) rect->top -= ncm->iMenuHeight + 1;
    if (ex & WS_EX_CLIENTEDGE) InflateRect(rect, GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE));
}

}   // extern "C"
namespace w32 {
// The frame we draw ourselves (children, undecorated popups): nc_calc_size.
void adjust_frame_rect(RECT* rect, DWORD style, BOOL menu, DWORD ex) { adjust_window_rect(rect, style, menu, ex, &g_ncm); }
}
extern "C" {

// An overlapped window, or a popup with a caption or sizing frame, is framed
// by the window manager outside our window (win::create: wm_decorated): its
// window rect is the X window, so only what we draw inside counts, the menu
// bar and a client edge. Children and other popups (a tooltip's WS_BORDER,
// set after creation) draw their frame themselves and keep the win32 sizes.
BOOL WINAPI AdjustWindowRectEx(RECT* rect, DWORD style, BOOL menu, DWORD ex)
{
    bool wm_framed = !(style & WS_CHILD) && (!(style & WS_POPUP) || (style & WS_CAPTION) == WS_CAPTION || (style & WS_THICKFRAME));
    if (!wm_framed) { adjust_window_rect(rect, style, menu, ex, &g_ncm); return TRUE; }
    if (menu) rect->top -= g_ncm.iMenuHeight + 1;
    if (ex & WS_EX_CLIENTEDGE) InflateRect(rect, GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE));
    return TRUE;
}
BOOL WINAPI AdjustWindowRect(RECT* rect, DWORD style, BOOL menu) { return AdjustWindowRectEx(rect, style, menu, 0); }
BOOL WINAPI AdjustWindowRectExForDpi(RECT* rect, DWORD style, BOOL menu, DWORD ex, UINT) { return AdjustWindowRectEx(rect, style, menu, ex); }

BOOL WINAPI SystemParametersInfoW(UINT action, UINT param, void* pv, UINT)
{
    gdi::Size ss = sys::screen_size();
    switch (action) {
    case SPI_GETNONCLIENTMETRICS: {
        NONCLIENTMETRICSW* p = (NONCLIENTMETRICSW*)pv;
        if (!p) return FALSE;
        UINT size = p->cbSize ? p->cbSize : sizeof g_ncm;
        if (size > sizeof g_ncm) size = sizeof g_ncm;
        UINT keep = p->cbSize;
        memcpy(p, &g_ncm, size);
        p->cbSize = keep ? keep : size;
        return TRUE;
    }
    case SPI_SETNONCLIENTMETRICS: {
        NONCLIENTMETRICSW* p = (NONCLIENTMETRICSW*)pv;
        if (!p) return FALSE;
        UINT size = p->cbSize < sizeof g_ncm ? p->cbSize : sizeof g_ncm;
        memcpy(&g_ncm, p, size);
        g_ncm.cbSize = sizeof g_ncm;
        // as Wine's normalize_nonclientmetrics: a positive height is in points
        // (uxtheme passes the theme's "Tahoma, 8" unconverted)
        for (LOGFONTW* f : { &g_ncm.lfCaptionFont, &g_ncm.lfSmCaptionFont, &g_ncm.lfMenuFont, &g_ncm.lfStatusFont, &g_ncm.lfMessageFont })
            if (f->lfHeight > 0) f->lfHeight = -MulDiv(f->lfHeight, 96, 72);
        // MS Shell Dlg: Tahoma, as XP's FontSubstitutes, except under a Longhorn style whose message font is Segoe UI
        // (MSGBOXFONT): 4074's dialogs are in Segoe UI at the template's size (4074-dwm.png's About box: 8 pt, its
        // lines 221 px wide where Segoe UI 8 makes 226 and 9 makes 247). Classic sets Tahoma again after this (theme.cpp)
        static const char16_t segoe[] = u"Segoe UI";
        bool seg = true;
        for (int i = 0; i < 9; i++) if (g_ncm.lfMessageFont.lfFaceName[i] != (WCHAR)segoe[i]) { seg = false; break; }
        w32::set_shell_dlg(seg && gdi::have_family("Segoe UI") ? "Segoe UI" : "Tahoma");
        return TRUE;
    }
    case SPI_GETICONTITLELOGFONT: if (!pv) return FALSE; *(LOGFONTW*)pv = g_icon_font; return TRUE;
    case SPI_SETICONTITLELOGFONT:
        if (!pv) return FALSE;
        g_icon_font = *(LOGFONTW*)pv;
        if (g_icon_font.lfHeight > 0) g_icon_font.lfHeight = -MulDiv(g_icon_font.lfHeight, 96, 72);
        return TRUE;
    case SPI_GETWHEELSCROLLLINES: if (pv) *(UINT*)pv = 3; return TRUE;
    case SPI_GETWHEELSCROLLCHARS: if (pv) *(UINT*)pv = 3; return TRUE;
    case SPI_GETFLATMENU: if (pv) *(BOOL*)pv = g_flat_menu; return TRUE;
    case SPI_SETFLATMENU: g_flat_menu = (BOOL)(INT_PTR)pv; return TRUE;   // uxtheme sets it from the theme (Luna: flat)
    case SPI_GETGRADIENTCAPTIONS: if (pv) *(BOOL*)pv = TRUE; return TRUE;
    case SPI_GETKEYBOARDDELAY: if (pv) *(INT*)pv = 1; return TRUE;
    case SPI_GETKEYBOARDSPEED: if (pv) *(DWORD*)pv = 31; return TRUE;
    // off by default as XP's (mnemonics underlined while Alt is held and in menus opened from the keyboard: defwnd, menu.c)
    case SPI_GETKEYBOARDCUES: if (pv) *(BOOL*)pv = win::keyboard_cues(); return TRUE;
    case SPI_SETKEYBOARDCUES: win::set_keyboard_cues((BOOL)(INT_PTR)pv != FALSE); return TRUE;
    case SPI_GETKEYBOARDPREF: if (pv) *(BOOL*)pv = FALSE; return TRUE;
    case SPI_GETWORKAREA:   // the window manager's _NET_WORKAREA, else the screen
        if (pv) { gdi::Rect wa = win::work_area(); RECT* r = (RECT*)pv; r->left = wa.left; r->top = wa.top; r->right = wa.right; r->bottom = wa.bottom; }
        return TRUE;
    case SPI_GETDRAGFULLWINDOWS: if (pv) *(BOOL*)pv = TRUE; return TRUE;
    // Display Properties' "Smooth edges of screen fonts" (gdi::set_font_smoothing;
    // shell::setup_fonts sets it from font_smoothing=)
    case SPI_GETFONTSMOOTHING: if (pv) *(BOOL*)pv = gdi::font_smoothing() != gdi::SMOOTH_OFF; return TRUE;
    case SPI_SETFONTSMOOTHING:
        if (!param) gdi::set_font_smoothing(gdi::SMOOTH_OFF);
        else if (gdi::font_smoothing() == gdi::SMOOTH_OFF) gdi::set_font_smoothing(gdi::SMOOTH_STANDARD);
        return TRUE;
    case SPI_GETFONTSMOOTHINGTYPE:
        if (pv) *(UINT*)pv = gdi::font_smoothing() == gdi::SMOOTH_CLEARTYPE ? FE_FONTSMOOTHINGCLEARTYPE : FE_FONTSMOOTHINGSTANDARD;
        return TRUE;
    case SPI_SETFONTSMOOTHINGTYPE:
        if (gdi::font_smoothing() != gdi::SMOOTH_OFF)
            gdi::set_font_smoothing((UINT)(UINT_PTR)pv == FE_FONTSMOOTHINGCLEARTYPE ? gdi::SMOOTH_CLEARTYPE : gdi::SMOOTH_STANDARD);
        return TRUE;
    // XP's "Show shadows under menus" (on by default; flxp: menu_shadows), for the popups created after it
    case SPI_GETDROPSHADOW: if (pv) *(BOOL*)pv = win::drop_shadows(); return TRUE;
    case SPI_SETDROPSHADOW: win::set_drop_shadows((BOOL)(INT_PTR)pv != FALSE); return TRUE;
    case SPI_GETLISTBOXSMOOTHSCROLLING: if (pv) *(BOOL*)pv = FALSE; return TRUE;
    case SPI_GETMOUSEHOVERTIME: if (pv) *(UINT*)pv = 400; return TRUE;
    case SPI_GETMOUSEHOVERWIDTH: case SPI_GETMOUSEHOVERHEIGHT: if (pv) *(UINT*)pv = 4; return TRUE;
    case SPI_GETMENUSHOWDELAY: if (pv) *(UINT*)pv = 400; return TRUE;
    case SPI_GETSCREENREADER: case SPI_GETHIGHCONTRAST: if (pv && action == SPI_GETSCREENREADER) *(BOOL*)pv = FALSE; return action == SPI_GETSCREENREADER;
    case SPI_GETICONMETRICS: {
        ICONMETRICSW* im = (ICONMETRICSW*)pv;
        if (!im) return FALSE;
        im->iHorzSpacing = 75; im->iVertSpacing = 75; im->iTitleWrap = 1; im->lfFont = g_icon_font;
        return TRUE;
    }
    case SPI_GETSHOWSOUNDS: if (pv) *(BOOL*)pv = FALSE; return TRUE;
    case SPI_GETMENUFADE: case SPI_GETMENUANIMATION: case SPI_GETSELECTIONFADE: case SPI_GETTOOLTIPANIMATION:
    case SPI_GETCOMBOBOXANIMATION: case SPI_GETLISTBOXSMOOTHSCROLLING + 1000:
        if (pv) *(BOOL*)pv = FALSE; return TRUE;
    case SPI_GETBEEP: if (pv) *(BOOL*)pv = FALSE; return TRUE;
    case SPI_GETCARETWIDTH: if (pv) *(DWORD*)pv = 1; return TRUE;
    case SPI_GETFOCUSBORDERWIDTH: case SPI_GETFOCUSBORDERHEIGHT: if (pv) *(UINT*)pv = 1; return TRUE;
    case SPI_GETMOUSEVANISH: case SPI_GETCURSORSHADOW: if (pv) *(BOOL*)pv = FALSE; return TRUE;
    case SPI_GETSNAPTODEFBUTTON: if (pv) *(BOOL*)pv = FALSE; return TRUE;
    case SPI_GETCLIENTAREAANIMATION: if (pv) *(BOOL*)pv = FALSE; return TRUE;
    default:
        if (action & 1) return TRUE;   // SPI_SET*: accept silently
        return FALSE;
    }
    (void)param;
}
BOOL WINAPI SystemParametersInfoA(UINT action, UINT param, void* pv, UINT f)
{
    if (action == SPI_GETNONCLIENTMETRICS || action == SPI_SETNONCLIENTMETRICS || action == SPI_GETICONTITLELOGFONT ||
        action == SPI_SETICONTITLELOGFONT || action == SPI_GETICONMETRICS)
        return FALSE;   // the A structures differ; nothing vendored uses them
    return SystemParametersInfoW(action, param, pv, f);
}
BOOL WINAPI SystemParametersInfoForDpi(UINT action, UINT param, void* pv, UINT f, UINT) { return SystemParametersInfoW(action, param, pv, f); }

UINT WINAPI GetDoubleClickTime(void) { return 500; }
BOOL WINAPI SetDoubleClickTime(UINT) { return TRUE; }
UINT WINAPI GetDpiForWindow(HWND) { return 96; }
UINT WINAPI GetDpiForSystem(void) { return 96; }
DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT c) { return c ? c : (DPI_AWARENESS_CONTEXT)0x10; }
DPI_AWARENESS_CONTEXT WINAPI GetThreadDpiAwarenessContext(void) { return (DPI_AWARENESS_CONTEXT)0x10; }
DPI_AWARENESS_CONTEXT WINAPI GetWindowDpiAwarenessContext(HWND) { return (DPI_AWARENESS_CONTEXT)0x10; }
BOOL WINAPI IsProcessDPIAware(void) { return TRUE; }
BOOL WINAPI SetProcessDPIAware(void) { return TRUE; }

// ---- monitors -------------------------------------------------------------

HMONITOR WINAPI MonitorFromRect(const RECT*, DWORD) { return (HMONITOR)1; }
HMONITOR WINAPI MonitorFromPoint(POINT, DWORD) { return (HMONITOR)1; }
HMONITOR WINAPI MonitorFromWindow(HWND, DWORD) { return (HMONITOR)1; }
BOOL WINAPI GetMonitorInfoW(HMONITOR, MONITORINFO* mi)
{
    if (!mi || mi->cbSize < sizeof(MONITORINFO)) return FALSE;
    gdi::Size ss = sys::screen_size();
    RECT r = {0, 0, ss.cx, ss.cy};
    gdi::Rect wa = win::work_area();
    RECT w = {wa.left, wa.top, wa.right, wa.bottom};
    mi->rcMonitor = r;
    mi->rcWork = w;
    mi->dwFlags = MONITORINFOF_PRIMARY;
    if (mi->cbSize >= sizeof(MONITORINFOEXW)) {
        static const char16_t name[] = u"\\\\.\\DISPLAY1";
        memcpy(((MONITORINFOEXW*)mi)->szDevice, name, sizeof name);
    }
    return TRUE;
}
BOOL WINAPI GetMonitorInfoA(HMONITOR m, MONITORINFO* mi) { return GetMonitorInfoW(m, mi); }
BOOL WINAPI EnumDisplayMonitors(HDC, LPRECT, MONITORENUMPROC proc, LPARAM lp)
{
    gdi::Size ss = sys::screen_size();
    RECT r = {0, 0, ss.cx, ss.cy};
    return proc((HMONITOR)1, nullptr, &r, lp);
}

// ---- clipboard (process-local) --------------------------------------------

static std::map<UINT, HANDLE> g_clip;
static UINT g_next_clipfmt = 0xC000;
static bool g_clip_text_set;   // flxp: text was set since OpenClipboard: CloseClipboard puts it on the X CLIPBOARD

BOOL WINAPI OpenClipboard(HWND) { g_clip_text_set = false; return TRUE; }
BOOL WINAPI CloseClipboard(void)
{
    if (g_clip_text_set) {
        g_clip_text_set = false;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            const WCHAR* t = (const WCHAR*)GlobalLock((HGLOBAL)h);
            if (t) FlxpSetSelectionText(FALSE, t, -1);
            GlobalUnlock((HGLOBAL)h);
        }
    }
    return TRUE;
}
BOOL WINAPI EmptyClipboard(void)
{
    for (auto& e : g_clip) if (e.second) GlobalFree((HGLOBAL)e.second);
    g_clip.clear();
    return TRUE;
}
HANDLE WINAPI SetClipboardData(UINT fmt, HANDLE h)
{
    auto it = g_clip.find(fmt);
    if (it != g_clip.end() && it->second && it->second != h) GlobalFree((HGLOBAL)it->second);
    g_clip[fmt] = h;
    if (fmt == CF_UNICODETEXT || fmt == CF_TEXT) {
        g_clip_text_set = true;
        if (fmt == CF_TEXT) { auto u = g_clip.find(CF_UNICODETEXT); if (u != g_clip.end()) { if (u->second) GlobalFree((HGLOBAL)u->second); g_clip.erase(u); } }
        else { auto a = g_clip.find(CF_TEXT); if (a != g_clip.end()) { if (a->second) GlobalFree((HGLOBAL)a->second); g_clip.erase(a); } }
    }
    return h;
}
HANDLE WINAPI GetClipboardData(UINT fmt)
{
    // flxp: text comes from the X CLIPBOARD (ours or another program's)
    if ((fmt == CF_UNICODETEXT || fmt == CF_TEXT) && !g_clip_text_set) {
        if (HGLOBAL x = FlxpGetSelectionText(FALSE)) {
            for (UINT f : { (UINT)CF_UNICODETEXT, (UINT)CF_TEXT }) {
                auto it = g_clip.find(f);
                if (it != g_clip.end()) { if (it->second) GlobalFree((HGLOBAL)it->second); g_clip.erase(it); }
            }
            g_clip[CF_UNICODETEXT] = x;
        }
    }
    auto it = g_clip.find(fmt);
    if (it != g_clip.end()) return it->second;
    // synthesize CF_TEXT from CF_UNICODETEXT and vice versa
    if (fmt == CF_UNICODETEXT) {
        auto t = g_clip.find(CF_TEXT);
        if (t == g_clip.end()) return nullptr;
        std::u16string s = utf16_of((const char*)GlobalLock((HGLOBAL)t->second));
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * 2);
        memcpy(GlobalLock(hg), s.c_str(), (s.size() + 1) * 2);
        g_clip[CF_UNICODETEXT] = hg;
        return hg;
    }
    if (fmt == CF_TEXT) {
        auto t = g_clip.find(CF_UNICODETEXT);
        if (t == g_clip.end()) return nullptr;
        std::string s = utf8_of((const WCHAR*)GlobalLock((HGLOBAL)t->second));
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
        memcpy(GlobalLock(hg), s.c_str(), s.size() + 1);
        g_clip[CF_TEXT] = hg;
        return hg;
    }
    return nullptr;
}
BOOL WINAPI IsClipboardFormatAvailable(UINT fmt)
{
    if ((fmt == CF_UNICODETEXT || fmt == CF_TEXT) && GetClipboardData(CF_UNICODETEXT)) return TRUE;   // flxp: the X CLIPBOARD
    if (g_clip.count(fmt)) return TRUE;
    if (fmt == CF_UNICODETEXT) return g_clip.count(CF_TEXT) != 0;
    if (fmt == CF_TEXT) return g_clip.count(CF_UNICODETEXT) != 0;
    return FALSE;
}
INT WINAPI CountClipboardFormats(void) { return (INT)g_clip.size(); }
UINT WINAPI RegisterClipboardFormatW(LPCWSTR) { return g_next_clipfmt++; }
UINT WINAPI RegisterClipboardFormatA(LPCSTR) { return g_next_clipfmt++; }
HWND WINAPI GetClipboardOwner(void) { return nullptr; }
DWORD WINAPI GetClipboardSequenceNumber(void) { return (DWORD)g_clip.size(); }

// ---- misc -----------------------------------------------------------------

INT WINAPI MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT)
{
    fprintf(stderr, "MessageBox: [%s] %s\n", caption ? utf8_of(caption).c_str() : "", text ? utf8_of(text).c_str() : "");
    return IDOK;
}
INT WINAPI MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
    fprintf(stderr, "MessageBox: [%s] %s\n", caption ? caption : "", text ? text : "");
    return IDOK;
}
BOOL WINAPI MessageBeep(UINT) { return TRUE; }
HWND WINAPI GetShellWindow(void) { return nullptr; }
BOOL WINAPI IsGUIThread(BOOL) { return TRUE; }
BOOL WINAPI IsHungAppWindow(HWND) { return FALSE; }
BOOL WINAPI LockWindowUpdate(HWND) { return TRUE; }
BOOL WINAPI SetLayeredWindowAttributes(HWND, COLORREF, BYTE, DWORD) { return FALSE; }
BOOL WINAPI AnimateWindow(HWND h, DWORD, DWORD flags) { ShowWindow(h, (flags & 0x10000) ? SW_HIDE : SW_SHOW); return TRUE; }
HWND WINAPI SetCapture_dummy_never_called(void) { return nullptr; }
BOOL WINAPI GetGUIThreadInfo(DWORD, GUITHREADINFO* info)
{
    if (!info) return FALSE;
    info->flags = 0;
    info->hwndActive = GetActiveWindow();
    info->hwndFocus = GetFocus();
    info->hwndCapture = GetCapture();
    info->hwndMenuOwner = nullptr; info->hwndMoveSize = nullptr; info->hwndCaret = hwnd_of(win::caret_window());
    gdi::Point c = win::caret_pos();
    info->rcCaret.left = c.x; info->rcCaret.top = c.y; info->rcCaret.right = c.x + 1; info->rcCaret.bottom = c.y + 13;
    return TRUE;
}
void WINAPI mouse_event(DWORD, DWORD, DWORD, DWORD, ULONG_PTR) {}
void WINAPI keybd_event(BYTE, BYTE, DWORD, ULONG_PTR) {}
UINT WINAPI SendInput(UINT, INPUT*, INT) { return 0; }
HWINEVENTHOOK WINAPI SetWinEventHook(DWORD, DWORD, HMODULE, WINEVENTPROC, DWORD, DWORD, DWORD) { return (HWINEVENTHOOK)1; }
BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK) { return TRUE; }
// Message-filter hooks: menu and dialog modal loops call CallMsgFilterW, which
// is how a menu band or an accelerator handler sees messages during tracking.
struct Hook { INT id; HOOKPROC proc; bool dead; };
static std::vector<Hook*> g_hooks;
HHOOK WINAPI SetWindowsHookExW(INT id, HOOKPROC proc, HINSTANCE, DWORD)
{
    Hook* h = new Hook{id, proc, false};
    g_hooks.push_back(h);
    return (HHOOK)h;
}
HHOOK WINAPI SetWindowsHookExA(INT id, HOOKPROC proc, HINSTANCE i, DWORD t) { return SetWindowsHookExW(id, proc, i, t); }
BOOL WINAPI UnhookWindowsHookEx(HHOOK hh)
{
    for (size_t i = 0; i < g_hooks.size(); i++)
        if ((HHOOK)g_hooks[i] == hh) { g_hooks[i]->dead = true; return TRUE; }
    return FALSE;
}
LRESULT WINAPI CallNextHookEx(HHOOK, INT, WPARAM, LPARAM) { return 0; }
BOOL flxp_call_msg_filter(MSG* msg, INT code)
{
    std::vector<Hook*> hooks = g_hooks;
    BOOL eaten = FALSE;
    for (Hook* h : hooks)
        if (!h->dead && h->id == WH_MSGFILTER && h->proc(code, 0, (LPARAM)msg)) { eaten = TRUE; break; }
    for (size_t i = 0; i < g_hooks.size();) { if (g_hooks[i]->dead) { delete g_hooks[i]; g_hooks.erase(g_hooks.begin() + i); } else i++; }
    return eaten;
}

} // extern "C"
