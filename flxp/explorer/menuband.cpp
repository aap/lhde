#include <string.h>
#include <string>
#include <vector>
#include "menuband.h"
#include "uxtheme.h"

namespace menuband {

static HWND g_tb, g_owner, g_rebar;
static HMENU g_menu;
static int g_tracking = -1;      // index of the open menu, -1 when none
static int g_switch_to = -1;     // set by the hook: open this one next
static HHOOK g_hook;
static HMENU g_cur_popup;        // popup the selection is in (from WM_MENUSELECT)
static bool g_sel_has_sub;       // selected item opens a submenu
static int g_sel_level;          // 0 = top-level popup
static bool g_kbd_mode;          // Alt pressed: hot item moves with the keys
static WNDPROC g_tb_proc;
static bool g_active = true;    // the owner window is active (WM_ACTIVATE)
static LRESULT (*g_custom_draw)(NMTBCUSTOMDRAW*, bool);

static int count() { return g_menu ? GetMenuItemCount(g_menu) : 0; }

static std::u16string item_text(int i)
{
    wchar_t buf[128] = {};
    GetMenuStringW(g_menu, (UINT)i, buf, 128, MF_BYPOSITION);
    return std::u16string((const char16_t*)buf);
}

static int mnemonic_index(WCHAR ch)
{
    ch = (WCHAR)flxp_towupper(ch);
    for (int i = 0; i < count(); i++) {
        std::u16string t = item_text(i);
        size_t amp = t.find(u'&');
        if (amp != std::u16string::npos && amp + 1 < t.size() && flxp_towupper(t[amp + 1]) == ch) return i;
    }
    return -1;
}

static int hit(POINT screen)
{
    POINT p = screen;
    ScreenToClient(g_tb, &p);
    int i = (int)SendMessageW(g_tb, TB_HITTEST, 0, (LPARAM)&p);
    return i >= 0 && i < count() ? i : -1;
}

static LRESULT CALLBACK filter(INT code, WPARAM wp, LPARAM lp)
{
    if (code != MSGF_MENU || g_tracking < 0) return 0;
    MSG* m = (MSG*)lp;
    switch (m->message) {
    case WM_MOUSEMOVE: {
        int i = hit(m->pt);
        if (i >= 0 && i != g_tracking) { g_switch_to = i; EndMenu(); return 1; }
        break;
    }
    case WM_LBUTTONDOWN: {
        int i = hit(m->pt);
        if (i == g_tracking) { g_switch_to = -2; EndMenu(); return 1; }   // click on the open item closes it
        break;
    }
    case WM_KEYDOWN:
        if (m->wParam == VK_LEFT && g_sel_level == 0) { g_switch_to = (g_tracking + count() - 1) % count(); EndMenu(); return 1; }
        if (m->wParam == VK_RIGHT && g_sel_level == 0 && !g_sel_has_sub) { g_switch_to = (g_tracking + 1) % count(); EndMenu(); return 1; }
        break;
    }
    return 0;
}

static void track(int index)
{
    if (!g_menu || index < 0 || index >= count()) return;
    g_hook = SetWindowsHookExW(WH_MSGFILTER, filter, nullptr, GetCurrentThreadId());
    for (;;) {
        g_tracking = index;
        g_switch_to = -1;
        g_sel_level = 0; g_sel_has_sub = false;
        HMENU sub = GetSubMenu(g_menu, index);
        RECT r; SendMessageW(g_tb, TB_GETRECT, (WPARAM)SendMessageW(g_tb, TB_GETBUTTON, 0, 0) ? 0 : 0, 0);
        SendMessageW(g_tb, TB_GETITEMRECT, (WPARAM)index, (LPARAM)&r);
        MapWindowPoints(g_tb, nullptr, (POINT*)&r, 2);
        SendMessageW(g_tb, TB_PRESSBUTTON, (WPARAM)SendMessageW(g_tb, TB_GETBUTTON, 0, 0) ? 0 : 0, 0);
        SendMessageW(g_tb, TB_SETSTATE, (WPARAM)(1000 + index), MAKELONG(TBSTATE_ENABLED | TBSTATE_PRESSED, 0));
        UpdateWindow(g_tb);
        TPMPARAMS tp = { sizeof tp, r };
        if (sub) TrackPopupMenuEx(sub, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL, r.left, r.bottom, g_owner, &tp);
        SendMessageW(g_tb, TB_SETSTATE, (WPARAM)(1000 + index), MAKELONG(TBSTATE_ENABLED, 0));
        g_tracking = -1;
        if (g_switch_to < 0) break;
        index = g_switch_to;
    }
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    g_kbd_mode = false;
    SendMessageW(g_tb, TB_SETHOTITEM, (WPARAM)-1, 0);
}

// XP's menu band draws in the menu font (browseui menuband.cpp: _hFontMenu from NONCLIENTMETRICS lfMenuFont): the style's
// MENUFONT, Segoe UI 9 under Jade and Aero
static HFONT g_font;
static void set_font()
{
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    HFONT f = CreateFontIndirectW(&ncm.lfMenuFont);
    SendMessageW(g_tb, WM_SETFONT, (WPARAM)f, TRUE);
    if (g_font) DeleteObject(g_font);
    g_font = f;
}

static void rebuild()
{
    while (SendMessageW(g_tb, TB_BUTTONCOUNT, 0, 0) > 0) SendMessageW(g_tb, TB_DELETEBUTTON, 0, 0);
    std::vector<std::u16string> texts;
    for (int i = 0; i < count(); i++) texts.push_back(item_text(i));
    std::vector<TBBUTTON> btns(texts.size());
    for (size_t i = 0; i < texts.size(); i++) {
        TBBUTTON& b = btns[i];
        memset(&b, 0, sizeof b);
        b.iBitmap = I_IMAGECALLBACK;   // XP (mnstatic.cpp): iIcon = -1, no image list; the 1px bitmap slot still counts for the width
        b.idCommand = 1000 + (int)i;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT | BTNS_DROPDOWN;   // whole button drops (no DRAWDDARROWS)
        b.iString = (INT_PTR)texts[i].c_str();
    }
    SendMessageW(g_tb, TB_ADDBUTTONSW, btns.size(), (LPARAM)btns.data());
    SendMessageW(g_tb, TB_AUTOSIZE, 0, 0);
}

static LRESULT CALLBACK tb_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (g_kbd_mode) {
            int hot = (int)SendMessageW(h, TB_GETHOTITEM, 0, 0);
            if (wp == VK_LEFT) { SendMessageW(h, TB_SETHOTITEM, (WPARAM)((hot + count() - 1) % count()), 0); return 0; }
            if (wp == VK_RIGHT) { SendMessageW(h, TB_SETHOTITEM, (WPARAM)((hot + 1) % count()), 0); return 0; }
            if (wp == VK_RETURN || wp == VK_DOWN || wp == VK_UP) { track(hot < 0 ? 0 : hot); return 0; }
            if (wp == VK_ESCAPE) { g_kbd_mode = false; SendMessageW(h, TB_SETHOTITEM, (WPARAM)-1, 0); SetFocus(g_owner); return 0; }
        }
        break;
    case WM_CHAR:
        if (g_kbd_mode) { int i = mnemonic_index((WCHAR)wp); if (i >= 0) track(i); return 0; }
        break;
    case WM_KILLFOCUS:
        if (g_kbd_mode && g_tracking < 0) { g_kbd_mode = false; SendMessageW(h, TB_SETHOTITEM, (WPARAM)-1, 0); }
        break;
    }
    return CallWindowProcW(g_tb_proc, h, msg, wp, lp);
}

HWND create(HWND rebar, HWND owner, HMENU menu, int band_id)
{
    g_rebar = rebar; g_owner = owner; g_menu = menu;
    g_tb = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0, rebar, (HMENU)(INT_PTR)band_id, nullptr, nullptr);
    SendMessageW(g_tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_tb, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
    SendMessageW(g_tb, TB_SETMAXTEXTROWS, 1, 0);
    // XP (shell32 menuband/mnstatic.cpp): the menu band is never themed, so an
    // open menu title shows the classic sunken flat-button edge even under Luna
    SetWindowTheme(g_tb, L"", L"");
    SendMessageW(g_tb, TB_SETBITMAPSIZE, 0, MAKELONG(0, 0));   // XP: TB_SETIMAGELIST(NULL) -> SetBitmapSize(0,0) -> 1x1 slot, button height 16->1: max(1,13)+6 = 19
    set_font();
    g_tb_proc = (WNDPROC)SetWindowLongPtrW(g_tb, GWLP_WNDPROC, (LONG_PTR)tb_proc);
    rebuild();
    SIZE sz = {0, 0};
    SendMessageW(g_tb, TB_GETMAXSIZE, 0, (LPARAM)&sz);
    RECT item = {0, 0, 0, 0};
    SendMessageW(g_tb, TB_GETITEMRECT, 0, (LPARAM)&item);
    REBARBANDINFOW rb = {};
    rb.cbSize = sizeof rb;
    rb.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID;
    rb.fStyle = RBBS_NOGRIPPER | RBBS_USECHEVRON;
    rb.hwndChild = g_tb;
    rb.wID = (UINT)band_id;
    // XP (browseui itbar.cpp GetBandInfo): band height = first button's height (22px: 16px bitmap slot + 6)
    rb.cxMinChild = (UINT)sz.cx; rb.cyMinChild = (UINT)(item.bottom - item.top); rb.cyChild = rb.cyMinChild; rb.cx = (UINT)sz.cx;
    // Measured on classic XP: the menu band's buttons start 1px further right
    // than the toolbar's (under Luna they start right at the band margin).
    if (!GetWindowTheme(rebar)) { rb.fMask |= RBBIM_HEADERSIZE; rb.cxHeader = 1; }
    SendMessageW(rebar, RB_INSERTBANDW, 0, (LPARAM)&rb);
    return g_tb;
}

void set_menu(HMENU menu) { g_menu = menu; rebuild(); }

void fonts_changed()
{
    if (!g_tb) return;
    set_font();
    rebuild();
    // the band follows the buttons' new height (itbar.cpp GetBandInfo: the first button's)
    RECT item = {0, 0, 0, 0};
    SendMessageW(g_tb, TB_GETITEMRECT, 0, (LPARAM)&item);
    SIZE sz = {0, 0};
    SendMessageW(g_tb, TB_GETMAXSIZE, 0, (LPARAM)&sz);
    int i = (int)SendMessageW(g_rebar, RB_IDTOINDEX, (WPARAM)GetDlgCtrlID(g_tb), 0);
    if (i < 0) return;
    REBARBANDINFOW rb = {};
    rb.cbSize = sizeof rb;
    rb.fMask = RBBIM_CHILDSIZE;
    rb.cxMinChild = (UINT)sz.cx; rb.cyMinChild = (UINT)(item.bottom - item.top); rb.cyChild = rb.cyMinChild;
    SendMessageW(g_rebar, RB_SETBANDINFOW, (WPARAM)i, (LPARAM)&rb);
}

bool active() { return g_active; }

void set_custom_draw(LRESULT (*fn)(NMTBCUSTOMDRAW* cd, bool active)) { g_custom_draw = fn; if (g_tb) InvalidateRect(g_tb, nullptr, TRUE); }

int height() { SIZE sz = {0, 0}; SendMessageW(g_tb, TB_GETMAXSIZE, 0, (LPARAM)&sz); return sz.cy; }

LRESULT on_notify(NMHDR* nm, bool* handled)
{
    *handled = false;
    if (nm->hwndFrom != g_tb) return 0;
    if (nm->code == NM_CUSTOMDRAW) {
        // XP mnbase.cpp: a horizontal menu band of an inactive window draws its
        // text in COLOR_3DSHADOW
        NMTBCUSTOMDRAW* cd = (NMTBCUSTOMDRAW*)nm;
        *handled = true;
        if (g_custom_draw) return g_custom_draw(cd, g_active);
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && !g_active) cd->clrText = GetSysColor(COLOR_3DSHADOW);
        return CDRF_DODEFAULT;
    }
    if (nm->code == TBN_DROPDOWN) {
        NMTOOLBARW* t = (NMTOOLBARW*)nm;
        *handled = true;
        if (g_tracking >= 0) return TBDDRET_DEFAULT;
        track(t->iItem - 1000);
        return TBDDRET_DEFAULT;
    }
    return 0;
}

void on_menuselect(WPARAM wp, LPARAM lp)
{
    if (g_tracking < 0) return;
    UINT flags = HIWORD(wp);
    HMENU h = (HMENU)lp;
    if (flags == 0xffff && !h) return;   // menu closed
    HMENU top = GetSubMenu(g_menu, g_tracking);
    g_cur_popup = h;
    g_sel_level = h == top ? 0 : 1;
    g_sel_has_sub = (flags & MF_POPUP) != 0;
}

void on_activate(bool active)
{
    if (active == g_active) return;
    g_active = active;
    if (g_tb) RedrawWindow(g_tb, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE);
}

bool key_menu(WCHAR ch)
{
    if (!g_tb) return false;
    if (ch) { int i = mnemonic_index(ch); if (i < 0) return false; track(i); return true; }
    g_kbd_mode = true;
    SetFocus(g_tb);
    SendMessageW(g_tb, TB_SETHOTITEM, 0, 0);
    return true;
}

} // namespace menuband
