// The task band (explorer's CTaskBand, taskband.cpp in the XP SP1 source): a
// comctl32 toolbar in list style with one button per window, themed
// TaskBand::Toolbar (TB_SETWINDOWTHEME "TaskBand"). Buttons are custom drawn
// as XP's _HandleCustomDraw does (the checked button is the active window,
// TaskBand.FLASHBUTTON for windows demanding attention) and sized by its
// _GetIdealWidth: the band's width shared out, at most SM_CXMINIMIZED, at
// least 1.8 caption buttons. Click activates or minimises the active one,
// the middle button closes, the right one opens the window menu.
#include <string.h>
#include <vector>
#include "taskbar.h"

namespace tb {
namespace band {

namespace {

enum { TIMER_FLASH = 20, FLASH_MS = 500, FLASH_TIMES = 7 };   // blinks, then stays lit (XP's TIF_RENDERFLASHED)
const UINT FIRST_CMD = 0x1000;

struct Item {
    xwm::Task t;
    UINT cmd = 0;
    int image = -1;
    int flashes = 0;            // blinks done
    bool lit = false;           // drawn with FLASHBUTTON now
    RECT geom = {};             // _NET_WM_ICON_GEOMETRY written
};

HWND tbw;
HIMAGELIST il;
HTHEME th_tb, th_band;          // TaskBand::Toolbar, TaskBand
std::vector<Item> items;
uint64_t active;
RECT area;                      // in the bar's client coordinates
UINT next_cmd = FIRST_CMD;
WNDPROC tb_proc;
bool flash_timer;

Item* by_cmd(UINT cmd)
{
    for (Item& i : items) if (i.cmd == cmd) return &i;
    return nullptr;
}

int add_icon(const xwm::Task& t)
{
    if (t.icon.size() != 16 * 16) {
        HICON def = (HICON)LoadImageW(nullptr, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_SHARED);
        return def ? ImageList_AddIcon(il, def) : -1;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = -16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return -1;
    memcpy(bits, t.icon.data(), 16 * 16 * 4);       // straight alpha, as an icon's
    static const uint8_t zeros[16 * 2] = {};
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, zeros);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!ic) return -1;
    int idx = ImageList_AddIcon(il, ic);
    DestroyIcon(ic);
    return idx;
}

// taskband.cpp _OpenTheme: cxButtonSpacing 0 themed, 3 classic
int spacing() { return th_tb ? 0 : 3; }

void set_check_states()
{
    for (Item& i : items)
        SendMessageW(tbw, TB_CHECKBUTTON, i.cmd, MAKELONG(i.t.id == active && !i.t.minimized, 0));
}

// taskband.cpp _GetIdealWidth / _SizeItems: one row
void size_buttons()
{
    int n = (int)items.size();
    if (!n) return;
    bool horz = g.horizontal();
    int bh = g.button_height;
    SendMessageW(tbw, TB_SETBUTTONSIZE, 0, MAKELONG(0, bh));
    int width = area.right - area.left;
    if (horz) {
        int sp = spacing();
        int ideal = width / n - (n > 1 ? sp : 0), rem = 0;
        int imax = GetSystemMetrics(SM_CXMINIMIZED);
        int imin = (int)((GetSystemMetrics(SM_CYSIZE) + 2 * GetSystemMetrics(SM_CXEDGE)) * 1.8);
        if (ideal > imax) ideal = imax;
        else rem = width - n * (ideal + sp);
        if (rem < 0) rem = 0;
        if (ideal < imin) ideal = imin, rem = 0;     // XP groups or scrolls from here (phase 2)
        for (int k = 0; k < n; k++) {
            TBBUTTONINFOW bi = {};
            bi.cbSize = sizeof bi;
            bi.dwMask = TBIF_SIZE | TBIF_STATE;
            SendMessageW(tbw, TB_GETBUTTONINFOW, items[k].cmd, (LPARAM)&bi);
            bi.dwMask = TBIF_SIZE | TBIF_STATE;
            bi.cx = (WORD)(ideal + sp + (k < rem ? 1 : 0));   // the spacing is drawn as part of the button (draw_item)
            bi.fsState &= ~TBSTATE_WRAP;
            SendMessageW(tbw, TB_SETBUTTONINFOW, items[k].cmd, (LPARAM)&bi);
        }
    } else {
        for (int k = 0; k < n; k++) {
            TBBUTTONINFOW bi = {};
            bi.cbSize = sizeof bi;
            bi.dwMask = TBIF_SIZE | TBIF_STATE;
            SendMessageW(tbw, TB_GETBUTTONINFOW, items[k].cmd, (LPARAM)&bi);
            bi.cx = (WORD)width;
            bi.fsState |= TBSTATE_WRAP;
            SendMessageW(tbw, TB_SETBUTTONINFOW, items[k].cmd, (LPARAM)&bi);
        }
    }
    SendMessageW(tbw, TB_AUTOSIZE, 0, 0);
}

// _NET_WM_ICON_GEOMETRY: lhwm flies minimised windows to their button
void publish_geometry()
{
    for (size_t k = 0; k < items.size(); k++) {
        RECT r;
        if (!SendMessageW(tbw, TB_GETITEMRECT, k, (LPARAM)&r)) continue;
        POINT a = { r.left, r.top }, b = { r.right, r.bottom };
        ClientToScreen(tbw, &a);
        ClientToScreen(tbw, &b);
        RECT s = { a.x, a.y, b.x, b.y };
        if (g.horizontal()) s.bottom = s.top + g.button_height;   // the toolbar reckons with a taller button
        if (EqualRect(&s, &items[k].geom)) continue;
        items[k].geom = s;
        xwm::set_icon_geometry(items[k].t.id, s.left, s.top, s.right - s.left, s.bottom - s.top);
    }
}

void rebuild(const std::vector<xwm::Task>& tasks)
{
    while (SendMessageW(tbw, TB_BUTTONCOUNT, 0, 0) > 0) SendMessageW(tbw, TB_DELETEBUTTON, 0, 0);
    ImageList_RemoveAll(il);
    std::vector<Item> old;
    old.swap(items);
    for (const xwm::Task& t : tasks) {
        Item it;
        for (const Item& o : old) if (o.t.id == t.id) { it = o; break; }
        it.t = t;
        if (!it.cmd) it.cmd = next_cmd++;
        it.image = add_icon(t);
        items.push_back(it);
    }
    std::vector<TBBUTTON> b(items.size());
    for (size_t k = 0; k < items.size(); k++) {
        b[k] = TBBUTTON{};
        b[k].iBitmap = items[k].image;
        b[k].idCommand = (int)items[k].cmd;
        b[k].fsState = TBSTATE_ENABLED;
        b[k].fsStyle = BTNS_BUTTON | BTNS_NOPREFIX;
        b[k].iString = -1;
    }
    if (!b.empty()) SendMessageW(tbw, TB_ADDBUTTONSW, b.size(), (LPARAM)b.data());
}

void update_flashing()
{
    bool any = false;
    for (Item& i : items) {
        if (!i.t.attention) { i.flashes = 0; i.lit = false; continue; }
        if (i.flashes < FLASH_TIMES * 2) any = true;
    }
    if (any && !flash_timer) { SetTimer(g.wnd, TIMER_FLASH, FLASH_MS, nullptr); flash_timer = true; }
    if (!any && flash_timer) { KillTimer(g.wnd, TIMER_FLASH); flash_timer = false; }
}

void draw_item(NMTBCUSTOMDRAW* cd, Item& it)
{
    HDC hdc = cd->nmcd.hdc;
    RECT rc = cd->nmcd.rc;
    if (g.horizontal()) { rc.top = 0; rc.bottom = g.button_height; }   // the toolbar's own idea of the height may differ
    if (g.horizontal()) rc.right -= spacing();
    UINT st = cd->nmcd.uItemState;
    bool checked = st & CDIS_CHECKED, pressed = st & CDIS_SELECTED, hot = st & CDIS_HOT;
    SetBkMode(hdc, TRANSPARENT);
    COLORREF text = GetSysColor(COLOR_BTNTEXT);
    HFONT font = checked ? g.font_caption_bold : g.font_caption;   // _HandleCustomDraw: the active task in bold
    HFONT themed_font = nullptr;
    int d = 0;
    if (th_tb) {
        int state = pressed ? TS_PRESSED : checked ? (hot ? TS_HOTCHECKED : TS_CHECKED) : hot ? TS_HOT : TS_NORMAL;
        if (it.lit && th_band) DrawThemeBackground(th_band, hdc, TDP_FLASHBUTTON, 0, &rc, nullptr);
        else DrawThemeBackground(th_tb, hdc, TP_BUTTON, state, &rc, nullptr);
        GetThemeColor(th_tb, TP_BUTTON, state, TMT_TEXTCOLOR, &text);
        // DrawThemeText's rule: the part's font wins over the one in the DC (4051's active button is not bold)
        LOGFONTW lf;
        if (SUCCEEDED(GetThemeFont(th_tb, hdc, TP_BUTTON, state, TMT_FONT, &lf))) font = themed_font = CreateFontIndirectW(&lf);
    } else {
        if (it.lit) {
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_HIGHLIGHT));
            text = GetSysColor(COLOR_HIGHLIGHTTEXT);
            DrawEdge(hdc, &rc, EDGE_RAISED, BF_RECT);
        } else if (checked || pressed) {
            // the pushed-in look with the light dither of a checked button
            if (checked && !pressed) {
                static const WORD pat[8] = { 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa };
                HBITMAP pb = CreateBitmap(8, 8, 1, 1, pat);
                HBRUSH br = CreatePatternBrush(pb);
                COLORREF ot = SetTextColor(hdc, GetSysColor(COLOR_3DFACE)), ob = SetBkColor(hdc, GetSysColor(COLOR_3DHILIGHT));
                FillRect(hdc, &rc, br);
                SetTextColor(hdc, ot); SetBkColor(hdc, ob);
                DeleteObject(br); DeleteObject(pb);
            } else {
                FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
            }
            DrawEdge(hdc, &rc, EDGE_SUNKEN, BF_RECT);
            d = 1;
        } else {
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
            DrawEdge(hdc, &rc, EDGE_RAISED, BF_RECT);
        }
    }
    // measured on 4051 (Slate, XP's explorer): icon 8 px in, text 3 px after it
    int pad = th_tb ? 8 : 5;
    int ix = rc.left + pad + d, iy = rc.top + (rc.bottom - rc.top - 16) / 2 + d;
    if (it.image >= 0) ImageList_Draw(il, it.image, hdc, ix, iy, ILD_TRANSPARENT);
    RECT tr = { ix + 16 + 3, rc.top + d, rc.right - (th_tb ? 6 : 4) + d, rc.bottom + d };
    if (tr.right > tr.left) {
        HGDIOBJ of = SelectObject(hdc, font);
        SetTextColor(hdc, text);
        std::u16string w = W(it.t.title);
        DrawTextW(hdc, (LPCWSTR)w.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, of);
    }
    if (themed_font) DeleteObject(themed_font);
}

LRESULT CALLBACK subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MBUTTONUP) {   // middle click closes (not XP; every Linux taskbar does it)
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int k = (int)SendMessageW(h, TB_HITTEST, 0, (LPARAM)&p);
        if (k >= 0 && k < (int)items.size()) xwm::close(items[k].t.id);
        return 0;
    }
    return CallWindowProcW(tb_proc, h, msg, wp, lp);
}

} // namespace

HWND create(HWND parent)
{
    tbw = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                          WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN |
                          TBSTYLE_LIST | TBSTYLE_TOOLTIPS | TBSTYLE_TRANSPARENT | TBSTYLE_FLAT,
                          0, 0, 1, 1, parent, nullptr, nullptr, nullptr);
    SendMessageW(tbw, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 8, 8);
    SendMessageW(tbw, TB_SETIMAGELIST, 0, (LPARAM)il);
    SendMessageW(tbw, TB_SETBITMAPSIZE, 0, MAKELONG(16, 16));
    tb_proc = (WNDPROC)SetWindowLongPtrW(tbw, GWLP_WNDPROC, (LONG_PTR)subclass);
    HWND tt = (HWND)SendMessageW(tbw, TB_GETTOOLTIPS, 0, 0);
    if (tt) SetWindowLongW(tt, GWL_STYLE, GetWindowLongW(tt, GWL_STYLE) | TTS_ALWAYSTIP | TTS_NOPREFIX);
    return tbw;
}

void theme_changed()
{
    SetWindowTheme(tbw, g.horizontal() ? L"TaskBand" : L"TaskBandVert", nullptr);
    if (th_tb) CloseThemeData(th_tb);
    if (th_band) CloseThemeData(th_band);
    th_tb = g.theme ? OpenThemeData(nullptr, g.horizontal() ? L"TaskBand::Toolbar" : L"TaskBandVert::Toolbar") : nullptr;
    if (g.theme && !th_tb) th_tb = OpenThemeData(nullptr, L"Toolbar");
    th_band = g.theme ? OpenThemeData(nullptr, L"TaskBand") : nullptr;
    // taskband.cpp _OpenTheme: cxPad 20 themed (8 classic), no spacing themed (3 classic)
    // (Wine's toolbar has no TB_SETMETRICS; the spacing is ours: size_buttons)
    SendMessageW(tbw, TB_SETPADDING, 0, MAKELONG(g.theme ? 20 : 8, 0));
    SendMessageW(tbw, TB_SETMAXTEXTROWS, 0, 0);          // no labels: the buttons are custom drawn
    InvalidateRect(tbw, nullptr, TRUE);
}

int button_height() { return g.button_height; }
uint64_t active_window() { return active; }

void place(const RECT& r)
{
    area = r;
    SetWindowPos(tbw, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    size_buttons();
    publish_geometry();
    InvalidateRect(tbw, nullptr, TRUE);
}

void update(const xwm::Snapshot& s)
{
    std::vector<xwm::Task> tasks;
    for (const xwm::Task& t : s.tasks) if (t.shown) tasks.push_back(t);
    bool same = tasks.size() == items.size();
    for (size_t k = 0; same && k < tasks.size(); k++) same = tasks[k].id == items[k].t.id;
    active = s.active;
    if (!same) {
        rebuild(tasks);
        size_buttons();
    } else {
        for (size_t k = 0; k < tasks.size(); k++) {
            Item& it = items[k];
            if (tasks[k].icon_serial != it.t.icon_serial) {
                // replace the image in place (ImageList_ReplaceIcon keeps the index)
                xwm::Task t = tasks[k];
                int tmp = add_icon(t);
                if (tmp >= 0 && it.image >= 0) {
                    HICON ic = ImageList_GetIcon(il, tmp, ILD_NORMAL);
                    ImageList_ReplaceIcon(il, it.image, ic);
                    DestroyIcon(ic);
                    ImageList_Remove(il, tmp);
                }
            }
            it.t = tasks[k];
        }
    }
    set_check_states();
    update_flashing();
    publish_geometry();
    InvalidateRect(tbw, nullptr, TRUE);
}

LRESULT notify(NMHDR* h)
{
    switch (h->code) {
    case NM_CUSTOMDRAW: {
        NMTBCUSTOMDRAW* cd = (NMTBCUSTOMDRAW*)h;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            if (Item* it = by_cmd((UINT)cd->nmcd.dwItemSpec)) draw_item(cd, *it);
            return CDRF_SKIPDEFAULT;
        }
        return CDRF_DODEFAULT;
    }
    case TBN_GETINFOTIPW: {
        NMTBGETINFOTIPW* t = (NMTBGETINFOTIPW*)h;
        if (Item* it = by_cmd((UINT)t->iItem)) {
            std::u16string w = W(it->t.title);
            int n = (int)w.size() < t->cchTextMax - 1 ? (int)w.size() : t->cchTextMax - 1;
            if (n >= 0 && t->pszText) { memcpy(t->pszText, w.data(), (size_t)n * 2); t->pszText[n] = 0; }
        }
        return 0;
    }
    case NM_RCLICK: {
        NMMOUSE* m = (NMMOUSE*)h;
        if (Item* it = by_cmd((UINT)m->dwItemSpec)) {
            POINT p;
            GetCursorPos(&p);
            startmenu::window_menu(it->t.id, it->t, p);
            return TRUE;
        }
        return 0;
    }
    }
    return 0;
}

bool command(UINT id)
{
    Item* it = by_cmd(id);
    if (!it) return false;
    // taskband.cpp _HandleClick: the active window is minimised, any other activated (and restored)
    if (it->t.id == active && !it->t.minimized) xwm::minimize(it->t.id);
    else xwm::activate(it->t.id);
    return true;
}

void timer(UINT_PTR id)
{
    if (id != TIMER_FLASH) return;
    for (Item& i : items) {
        if (!i.t.attention || i.flashes >= FLASH_TIMES * 2) continue;
        i.flashes++;
        i.lit = !i.lit;
        if (i.flashes >= FLASH_TIMES * 2) i.lit = true;
    }
    update_flashing();
    InvalidateRect(tbw, nullptr, FALSE);
}

} // namespace band
} // namespace tb
