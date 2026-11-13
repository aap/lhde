// Visual styles: wiring the vendored uxtheme into the shim. A theme is
// selected the way Windows does it, through the ThemeManager registry key,
// which here lives in the shim's in-memory registry. The session theme (root
// _LH_THEME resolved by liblhde, shell/lhtheme) is followed at run time: a
// change reloads the style and sends WM_THEMECHANGED to every window.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <vector>
#include "w32.h"
#include "kernel.h"
#include "image.h"
#include "shell/lhtheme.h"
#include "shell/config.h"
#include "shell/runtime.h"
#include "gdi/font.h"
#include "user.h"
extern "C" {
#include "winreg.h"
#include "uxtheme.h"
#include "vssym32.h"
#include "msstyles.h"
void UXTHEME_InitSystem(HINSTANCE hInst);
void UXTHEME_UninitSystem(void);
void UXTHEME_ReloadTheme(void);
}

namespace w32 {

static bool g_theme_inited;
static std::string g_path, g_color, g_size;     // what is loaded

// "luna" -> the first <dir>/luna/luna.msstyles (or lhde's styles/luna-xp.msstyles, ...) found in
// the usual places: shell::find_msstyles (lhde's styles/, wine's and ~/.themes)
static std::string resolve_theme(const char* name)
{
    std::string n = name;
    if (n.find('/') != std::string::npos) return n;
    std::string f = shell::find_msstyles(n);
    return f.empty() ? n : f;
}

bool set_theme(const char* path_or_name, const char* color, const char* size)
{
    std::string resolved = path_or_name ? resolve_theme(path_or_name) : std::string();
    const char* path = resolved.c_str();
    const char* key = "Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";
    if (!path || !*path) {
        reg_set_string(HKEY_CURRENT_USER, key, "ThemeActive", "0");
        reg_set_string(HKEY_CURRENT_USER, key, "DllName", "");
    } else {
        reg_set_string(HKEY_CURRENT_USER, key, "DllName", path);
        reg_set_string(HKEY_CURRENT_USER, key, "ColorName", color ? color : "");
        reg_set_string(HKEY_CURRENT_USER, key, "SizeName", size ? size : "");
        reg_set_string(HKEY_CURRENT_USER, key, "ThemeActive", "1");
    }
    g_path = path ? path : ""; g_color = color ? color : ""; g_size = size ? size : "";
    if (g_theme_inited) UXTHEME_ReloadTheme();      // load it, WM_THEMECHANGED to every window
    return true;
}

static void set_env(const char* var, const std::string& v)
{
    if (v.empty()) unsetenv(var); else setenv(var, v.c_str(), 1);
}

static BOOL CALLBACK post_theme_changed(HWND h, LPARAM top)
{
    PostMessageW(h, WM_THEMECHANGED, 0, 0);
    if (top) EnumChildWindows(h, post_theme_changed, 0);
    return TRUE;
}

static BOOL CALLBACK refit_menu_bar(HWND h, LPARAM)
{
    if (GetMenu(h)) DrawMenuBar(h);
    return TRUE;
}

static BOOL CALLBACK redraw_all(HWND h, LPARAM)
{
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

// the record without the keys only flxp-desktop reads (the wallpaper) and the theme's identity (a switch to
// NAME-modified.theme for a new wallpaper): a change there is no theme change for the windows
static std::string look_of(const shell::SessionTheme& t)
{
    static const char* const skip[] = { "path.wallpaper", "theme", "file", "chain", "Theme.name", "Theme.base", "Theme.aliases" };
    std::string out;
    for (auto& kv : t.keys) {
        bool own = kv.first.compare(0, 8, "Desktop.") == 0 && kv.first != "Desktop.icons";
        for (const char* k : skip) own = own || kv.first == k;
        if (!own) out += kv.first + "=" + kv.second + "\n";
    }
    return out;
}

// flxp: the classic look (docs/THEME-FORMAT.md section 2a): with VisualStyle.style = classic the record's Colors.* (a
// scheme's or the theme's / current layer's) become the system colours and Metrics.* the non-client metrics, as Windows'
// Appearance tab does with SetSysColors / SPI_SETNONCLIENTMETRICS. Absent keys keep the built-in classic values.
static bool parse_rgb(const std::string& v, COLORREF& out)
{
    int r, g, b;
    if (sscanf(v.c_str(), "%d %d %d", &r, &g, &b) != 3 && sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) != 3) return false;
    out = RGB(r & 255, g & 255, b & 255);
    return true;
}

// "Tahoma, 8, bold, italic": the size in points, or negative = pixels (LOGFONT's lfHeight, as the scheme importer writes it)
static bool parse_font(const std::string& v, LOGFONTW& lf)
{
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= v.size()) {
        size_t j = v.find(',', i);
        if (j == std::string::npos) j = v.size();
        std::string p = v.substr(i, j - i);
        while (!p.empty() && isspace((unsigned char)p[0])) p.erase(0, 1);
        while (!p.empty() && isspace((unsigned char)p.back())) p.pop_back();
        parts.push_back(p);
        i = j + 1;
    }
    if (parts.empty() || parts[0].empty()) return false;
    std::u16string face = utf16_of(parts[0].c_str());
    memset(lf.lfFaceName, 0, sizeof lf.lfFaceName);
    for (size_t k = 0; k < face.size() && k < LF_FACESIZE - 1; k++) lf.lfFaceName[k] = face[k];
    if (parts.size() > 1 && !parts[1].empty()) {
        int h = atoi(parts[1].c_str());
        lf.lfHeight = h > 0 ? -MulDiv(h, 96, 72) : h;
    }
    lf.lfWeight = FW_NORMAL;
    lf.lfItalic = FALSE;
    for (size_t k = 2; k < parts.size(); k++) {
        if (!strcasecmp(parts[k].c_str(), "bold")) lf.lfWeight = FW_BOLD;
        else if (!strcasecmp(parts[k].c_str(), "italic")) lf.lfItalic = TRUE;
    }
    lf.lfWidth = 0;
    return true;
}

static BOOL CALLBACK post_settings_changed(HWND h, LPARAM)
{
    PostMessageW(h, WM_SYSCOLORCHANGE, 0, 0);
    PostMessageW(h, WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0);
    return TRUE;
}

static std::string classic_look_of(const shell::SessionTheme& t)
{
    std::string out;
    if (!t.classic) return out;
    for (auto& kv : t.keys)
        if (kv.first.compare(0, 7, "Colors.") == 0 || kv.first.compare(0, 8, "Metrics.") == 0) out += kv.first + "=" + kv.second + "\n";
    return out;
}

// the record's Colors.* names by COLOR_ index (liblhde's lhde_color_name; the shim does not link liblhde)
static const char* const kColorNames[] = {
    "scrollbar", "background", "activecaption", "inactivecaption", "menu", "window", "windowframe", "menutext", "windowtext",
    "captiontext", "activeborder", "inactiveborder", "appworkspace", "highlight", "highlighttext", "btnface", "btnshadow",
    "graytext", "btntext", "inactivecaptiontext", "btnhighlight", "3ddkshadow", "3dlight", "infotext", "infobk",
    "alternatebtnface", "hotlight", "gradientactivecaption", "gradientinactivecaption", "menuhilight", "menubar", nullptr };

void apply_classic_look(const shell::SessionTheme& t)
{
    if (!t.classic) return;
    reset_syscolors();
    init_sysparams();
    std::vector<INT> idx;
    std::vector<COLORREF> val;
    for (int i = 0; kColorNames[i]; i++) {
        auto it = t.keys.find(std::string("Colors.") + kColorNames[i]);
        COLORREF c;
        if (it != t.keys.end() && parse_rgb(it->second, c)) { idx.push_back(i); val.push_back(c); }
    }
    if (!idx.empty()) SetSysColors((INT)idx.size(), idx.data(), val.data());
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    LOGFONTW icon = {};
    SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof icon, &icon, 0);
    auto num = [&](const char* k, int& f) {
        auto it = t.keys.find(std::string("Metrics.") + k);
        if (it != t.keys.end() && !it->second.empty()) { int v = atoi(it->second.c_str()); if (v > 0 && v < 200) f = v; }
    };
    auto font = [&](const char* k, LOGFONTW& f) {
        auto it = t.keys.find(std::string("Metrics.") + k);
        if (it != t.keys.end()) parse_font(it->second, f);
    };
    num("borderwidth", ncm.iBorderWidth); num("scrollwidth", ncm.iScrollWidth); num("scrollheight", ncm.iScrollHeight);
    num("captionwidth", ncm.iCaptionWidth); num("captionheight", ncm.iCaptionHeight);
    num("smcaptionwidth", ncm.iSmCaptionWidth); num("smcaptionheight", ncm.iSmCaptionHeight);
    num("menuwidth", ncm.iMenuWidth); num("menuheight", ncm.iMenuHeight);
    font("captionfont", ncm.lfCaptionFont); font("smcaptionfont", ncm.lfSmCaptionFont); font("menufont", ncm.lfMenuFont);
    font("statusfont", ncm.lfStatusFont); font("messagefont", ncm.lfMessageFont); font("icontitlefont", icon);
    SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    SystemParametersInfoW(SPI_SETICONTITLELOGFONT, sizeof icon, &icon, 0);
    set_shell_dlg("Tahoma");   // Windows Classic: XP's MS Shell Dlg, whatever the scheme's message font
    if (getenv("FLXP_DEBUG"))
        fprintf(stderr, "flxp: classic look of %s: scheme %s, %d colours, caption %d px, active caption %06x\n", t.name.c_str(),
                t.value("VisualStyle.scheme").c_str(), (int)idx.size(), ncm.iCaptionHeight, (unsigned)GetSysColor(COLOR_ACTIVECAPTION));
}

// The session theme changed (_LH_THEME, the published record, or a theme file: _LH_SETTINGS_CHANGED): resolve it
// (lhtheme.h) and reload the visual style if it is another one. Windows: the theme service's WM_THEMECHANGED; here
// the root PropertyNotify, coalesced by the toolkit.
void session_theme_refresh()
{
    if (!shell::follows_session_theme()) return;
    std::string name = win::root_string("_LH_THEME");
    if (name.empty()) return;                        // nobody publishes a theme: keep ours
    long serial = 0;
    if (win::root_cardinals("_LH_SETTINGS_SERIAL", &serial, 1) != 1) serial = 0;
    const shell::SessionTheme& cur = shell::session_theme();
    shell::SessionTheme t;
    std::string err;
    if (!shell::session_theme_from(name, win::root_string("_LH_THEME_RESOLVED"), (unsigned long)serial, t, &err)) {
        fprintf(stderr, "flxp: session theme \"%s\": %s; keeping %s\n", name.c_str(), err.c_str(),
                cur.name.empty() ? "the current style" : cur.name.c_str());
        return;
    }
    if (t.body == cur.body) return;                  // the same record (republished at another serial)
    bool look = look_of(t) != look_of(cur);
    // flxp: a classic scheme, size or Advanced edit (before cur, a reference to the session theme, is replaced)
    bool classic_look = classic_look_of(t) != classic_look_of(cur) || (t.classic && !cur.classic);
    shell::set_session_theme(t);
    set_env("FLXP_THEME_FOLLOWED", t.name);               // children follow too
    if (!look) return;                                    // the wallpaper only: flxp-desktop's business
    bool style = t.classic ? !g_path.empty()
                           : !t.msstyles.empty() && (t.msstyles != g_path || t.color != g_color || t.size != g_size);
    if (t.msstyles.empty() && !t.classic)
        fprintf(stderr, "flxp: no visual style for session theme %s (%s); keeping the current one\n", t.name.c_str(),
                t.value("VisualStyle.style").c_str());
    if (style) {
        set_env("FLXP_THEME", t.msstyles); set_env("FLXP_THEME_COLOR", t.color); set_env("FLXP_THEME_SIZE", t.size);
        reset_syscolors();                               // the new style's [SysMetrics] start from classic
        init_sysparams();                                // flxp: and the metrics (a classic scheme's Large Fonts)
        set_theme(t.classic ? nullptr : t.msstyles.c_str(), t.color.c_str(), t.size.c_str());
        if (t.classic) apply_classic_look(t);            // flxp: the scheme's colours and metrics (section 2a)
        if (getenv("FLXP_DEBUG")) fprintf(stderr, "flxp: session theme %s: %s (%s/%s), window text %06x\n", t.name.c_str(),
                                          t.classic ? "classic" : t.msstyles.c_str(), t.color.c_str(), t.size.c_str(),
                                          (unsigned)GetSysColor(COLOR_WINDOWTEXT));
    } else {
        // flxp: classic with another scheme, size or Advanced edit: the system colours and metrics again
        if (classic_look) { apply_classic_look(t); EnumWindows(post_settings_changed, 0); }
        // the same style (aero4074 and aerovision both load realaero): still a theme
        // change for the windows (chrome, navbar, icons, luxwm's frame)
        EnumWindows(post_theme_changed, 1);
    }
    // the menu font may be another one: menu bars measured again (DrawMenuBar: the frame recalculated), then repaint
    // everything: system colours, non-client areas, unthemed parts
    MENU_FontsChanged();
    EnumWindows(refit_menu_bar, 0);
    EnumWindows(redraw_all, 0);
}

// _LH_SETTINGS_CHANGED (docs/SETTINGS.md): the settings every program follows
// live. A theme file may have changed (NAME-modified.theme): the record again.
// Font smoothing (settings.ini [fonts] smoothing, else the theme's
// Fonts.smoothing): the glyph caches are keyed by it, so a repaint is all it takes.
static void settings_changed()
{
    session_theme_refresh();
    std::string sm = shell::font_smoothing();
    int mode = sm == "off" || sm == "0" ? gdi::SMOOTH_OFF : sm == "cleartype" ? gdi::SMOOTH_CLEARTYPE : gdi::SMOOTH_STANDARD;
    if (mode == gdi::font_smoothing()) return;
    gdi::set_font_smoothing(mode);
    EnumWindows(redraw_all, 0);
}

void init_theme()
{
    if (g_theme_inited) return;
    const char* t = getenv("FLXP_THEME");
    if (t && *t) set_theme(t, getenv("FLXP_THEME_COLOR"), getenv("FLXP_THEME_SIZE"));
    UXTHEME_InitSystem(nullptr);
    g_theme_inited = true;
    apply_classic_look(shell::session_theme());   // flxp: a classic session theme's scheme from the start
    win::on_root_property_change("_LH_SETTINGS_CHANGED", settings_changed);
    if (shell::follows_session_theme()) {
        win::on_root_property_change("_LH_THEME", session_theme_refresh);
        win::on_root_property_change("_LH_THEME_RESOLVED", session_theme_refresh);   // luxwm's / luxshell's record
    }
}

} // namespace w32

// The size grip of a sizable window: the status bar's (SBARS_SIZEGRIP) and the scroll bars' corner at the window's bottom
// right. A Longhorn (v4) style: 4074 draws neither the style's ScrollBar SIZEBOX nor its Status GRIPPER image (under Jade
// both are one picture, a pyramid of white dots) but six 2 x 2 dots in a triangle on a 4 px pitch in the bottom right of the
// box, each 172 grey lit by a 255 L below and right of it, the scroll bars' corner 233 under them. FALSE for another style.
// (opened without the window: OpenThemeData with one makes the class its window theme, and the status bar would draw as a
// scroll bar)
static bool longhorn_style(HTHEME th)
{
    PTHEME_CLASS tc = (PTHEME_CLASS)th;
    return tc && tc->tf && tc->tf->v4;
}

extern "C" BOOL WUX_DrawLonghornSizeGrip(HWND hwnd, HDC hdc, const RECT* rc, BOOL fill)
{
    HTHEME th = OpenThemeData(nullptr, L"ScrollBar");
    bool v4 = longhorn_style(th);
    if (th) CloseThemeData(th);
    if (!v4) return FALSE;
    RECT r = *rc;
    if (fill) { HBRUSH b = CreateSolidBrush(RGB(233, 233, 233)); FillRect(hdc, &r, b); DeleteObject(b); }
    static const int dots[6][2] = { { 4, 12 }, { 8, 8 }, { 4, 8 }, { 12, 4 }, { 8, 4 }, { 4, 4 } };
    for (const auto& d : dots) {
        int x = r.right - d[0], y = r.bottom - d[1];
        for (int k = 0; k < 4; k++) SetPixel(hdc, x + (k & 1), y + (k >> 1), RGB(172, 172, 172));
        SetPixel(hdc, x + 2, y + 1, RGB(255, 255, 255));
        SetPixel(hdc, x + 1, y + 2, RGB(255, 255, 255));
        SetPixel(hdc, x + 2, y + 2, RGB(255, 255, 255));
    }
    return TRUE;
}

// the scroll bars' corner as a size grip: the Longhorn dots above; XP's styles the ScrollBar class's SIZEBOX part right
// aligned over BTNFACE (Luna's dotted triangle); Windows Classic DrawFrameControl's DFCS_SCROLLSIZEGRIP over BTNFACE
extern "C" void WUX_DrawSizeGrip(HWND hwnd, HDC hdc, const RECT* rc)
{
    if (WUX_DrawLonghornSizeGrip(hwnd, hdc, rc, TRUE)) return;
    RECT r = *rc;
    FillRect(hdc, &r, GetSysColorBrush(COLOR_BTNFACE));
    HTHEME th = OpenThemeData(nullptr, L"ScrollBar");
    if (th) DrawThemeBackground(th, hdc, SBP_SIZEBOX, SZB_RIGHTALIGN, &r, nullptr);
    else DrawFrameControl(hdc, &r, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
    if (th) CloseThemeData(th);
}
