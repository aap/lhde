// The popup menu painter (menu.h): XP's popup menus as ntuser draws them (mncomput.c, mndraw.c; read, not copied), in the
// visual style's colours and menu font, and the Longhorn / Vista Menu class where the style has one. Drawn with GDI into any
// DC: user32's menus (wux, menu.c.patch) and luxwm's window menu (ut_menu_draw, ut.cpp) are the same pixels.
#include <string.h>
#include <string>
#include "wuxtheme/v4.h"
#include "w32/w32.h"
extern "C" {
#include "uxtheme.h"
#include "vssym32.h"
#include "msstyles.h"
}
#include "wuxtheme/menu.h"

namespace {

// a part of the style's Menu class by name: the v4 (Longhorn) style's own numbering, else the Vista table; 0 when the
// style does not have it
int menu_part(HTHEME th, const char* name)
{
    PTHEME_CLASS tc = (PTHEME_CLASS)th;
    if (!tc) return 0;
    int id = -1;
    if (tc->tf && tc->tf->v4) id = v4::part_id(*(v4::Style*)tc->tf->v4, "Menu", name);
    if (id < 0) {
        int p, s;
        std::u16string pn = w32::utf16_of(name);
        if (MSSTYLES_LookupPartState(L"Menu", (LPCWSTR)pn.c_str(), nullptr, &p, &s)) id = p;
    }
    if (id <= 0) return 0;
    for (PTHEME_PARTSTATE ps = tc->partstate; ps; ps = ps->next)
        if (ps->iPartId == id) return id;
    return 0;
}

void fill(HDC dc, COLORREF c, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void part(const UXMENU_LOOK* L, HDC dc, int p, int state, int x, int y, int w, int h)
{
    if (!p || w <= 0 || h <= 0) return;
    RECT r = { x, y, x + w, y + h };
    DrawThemeBackground(L->theme, dc, p, state, &r, nullptr);
}

bool part_size(const UXMENU_LOOK* L, HDC dc, int p, int state, int* cx, int* cy)
{
    SIZE s = {};
    if (!p || FAILED(GetThemePartSize(L->theme, dc, p, state, nullptr, TS_TRUE, &s)) || s.cx <= 0 || s.cy <= 0) return false;
    *cx = s.cx; *cy = s.cy;
    return true;
}

// the system menu bitmaps in a 13 x 13 cell (XP draws them from Marlett: '0' minimise, '1' maximise, '2' restore, 'r' close),
// as rectangles in the current colour
void glyph(HDC dc, COLORREF c, int g, int x, int y)
{
    auto R = [&](int a, int b, int w, int h) { fill(dc, c, x + a, y + b, w, h); };
    switch (g) {
    case UXMENU_GLYPH_MINIMIZE: R(3, 8, 6, 2); break;
    case UXMENU_GLYPH_MAXIMIZE: R(2, 2, 9, 2); R(2, 4, 1, 6); R(10, 4, 1, 6); R(2, 10, 9, 1); break;
    case UXMENU_GLYPH_RESTORE:  R(4, 1, 6, 2); R(9, 3, 1, 5); R(8, 7, 1, 1);
                                R(1, 5, 6, 2); R(1, 7, 1, 4); R(6, 7, 1, 4); R(1, 10, 6, 1); break;
    case UXMENU_GLYPH_CLOSE:    for (int i = 0; i < 6; i++) { R(3 + i, 3 + i, 2, 1); R(8 - i, 3 + i, 2, 1); } break;
    }
}

// XP's submenu arrow (OBI_MENUARROW, Marlett '8' in the 13 px check-sized cell at the item's end): 4 x 7, its right column 6 px
// before the item's end (WinXP-193221.png)
void arrow(HDC dc, COLORREF c, int right, int y, int h)
{
    int x = right - 9, top = y + (h - 7) / 2;
    for (int k = 0; k < 4; k++) fill(dc, c, x + k, top + k, 1, 7 - 2 * k);
}

// XP's check mark and radio bullet (DrawFrameControl DFC_MENU: Marlett 'a' / 'h' in the 13 x 13 check cell)
void check(HDC dc, COLORREF c, bool radio, int x, int y)
{
    LOGFONTW lf = {};
    lf.lfHeight = 13; lf.lfCharSet = SYMBOL_CHARSET; lf.lfQuality = NONANTIALIASED_QUALITY;
    memcpy(lf.lfFaceName, L"Marlett", 8 * sizeof(WCHAR));
    HFONT f = CreateFontIndirectW(&lf);
    HGDIOBJ o = SelectObject(dc, f);
    SetTextColor(dc, c);
    RECT r = { x, y, x + 13, y + 13 };
    WCHAR ch = radio ? 'h' : 'a';
    DrawTextW(dc, &ch, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, o);
    DeleteObject(f);
}

} // namespace

extern "C" {

void UXMENU_InitLook(UXMENU_LOOK* L, HTHEME theme, const COLORREF* sys, BOOL flat, const LOGFONTW* font)
{
    memset(L, 0, sizeof *L);
    auto C = [&](int i) { return sys ? sys[i] : GetSysColor(i); };
    L->menu = C(COLOR_MENU); L->text = C(COLOR_MENUTEXT); L->gray = C(COLOR_GRAYTEXT);
    L->hlframe = C(COLOR_HIGHLIGHT); L->hltext = C(COLOR_HIGHLIGHTTEXT); L->hl = C(COLOR_MENUHILIGHT);
    L->face = C(COLOR_BTNFACE); L->shadow = C(COLOR_BTNSHADOW); L->hilight = C(COLOR_BTNHIGHLIGHT);
    L->dkshadow = C(COLOR_3DDKSHADOW); L->light = C(COLOR_3DLIGHT);
    L->flat = flat ? 1 : 0;

    LOGFONTW lf = {};
    if (font) lf = *font;
    else { lf.lfHeight = -11; lf.lfWeight = FW_NORMAL; memcpy(lf.lfFaceName, L"Tahoma", 7 * sizeof(WCHAR)); }
    L->font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    L->bold = CreateFontIndirectW(&lf);

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, L->font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    L->asc = tm.tmAscent; L->th = tm.tmHeight;
    int cw = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 6;

    int item_t = 1, item_b = 1, sep_cy = 2, sep_t = -1, sep_b = -1;   // XP: text height + CYEDGE + 2
    int p_item = menu_part(theme, "POPUPITEM");
    if (p_item) {   // the Longhorn / Vista Menu class
        L->theme = theme; L->p_item = p_item;
        L->p_borders = menu_part(theme, "POPUPBORDERS"); L->p_bg = menu_part(theme, "POPUPBACKGROUND");
        L->p_gutter = menu_part(theme, "POPUPGUTTER");
        if (!(L->p_sep = menu_part(theme, "POPUPSEPARATOR"))) L->p_sep = menu_part(theme, "SEPARATOR");
        if (!(L->p_check = menu_part(theme, "CHECK"))) L->p_check = menu_part(theme, "POPUPCHECK");
        if (!(L->p_arrow = menu_part(theme, "FLYOUTARROW"))) L->p_arrow = menu_part(theme, "POPUPSUBMENU");
        static const char* names[5] = { nullptr, "SYSTEMRESTORE", "SYSTEMMINIMIZE", "SYSTEMMAXIMIZE", "SYSTEMCLOSE" };
        for (int i = 1; i < 5; i++) L->p_sys[i] = menu_part(theme, names[i]);
        COLORREF c;
        L->t_norm = SUCCEEDED(GetThemeColor(theme, p_item, 1, TMT_TEXTCOLOR, &c)) ? c : L->text;
        L->t_hot = SUCCEEDED(GetThemeColor(theme, p_item, 2, TMT_TEXTCOLOR, &c)) ? c : L->t_norm;
        L->t_dis = SUCCEEDED(GetThemeColor(theme, p_item, 3, TMT_TEXTCOLOR, &c)) ? c : L->gray;
        L->t_dishot = SUCCEEDED(GetThemeColor(theme, p_item, 4, TMT_TEXTCOLOR, &c)) ? c : L->t_dis;
        MARGINS m;
        if (SUCCEEDED(GetThemeMargins(theme, dc, p_item, 0, TMT_CONTENTMARGINS, nullptr, &m))) { item_t = m.cyTopHeight; item_b = m.cyBottomHeight; }
        else item_t = item_b = 3;
        if (L->p_sep) {
            int cx, cy;
            if (part_size(L, dc, L->p_sep, 0, &cx, &cy)) sep_cy = cy;
            if (SUCCEEDED(GetThemeMargins(theme, dc, L->p_sep, 0, TMT_CONTENTMARGINS, nullptr, &m)) && (m.cyTopHeight || m.cyBottomHeight)) {
                sep_t = m.cyTopHeight; sep_b = m.cyBottomHeight;
            }
        }
    } else {   // XP's mndraw.c colours: the selected item MENUHILIGHT framed in HIGHLIGHT (flat), else HIGHLIGHT
        L->t_norm = L->text; L->t_hot = L->hltext; L->t_dis = L->gray;
        if (!L->flat) L->hl = L->hlframe;
        L->t_dishot = L->gray == L->hl ? L->hltext : L->gray;
    }
    SelectObject(dc, old);
    DeleteDC(dc);

    // geometry (XP: 1 px outline + CXEDGE inside; item = text + CYEDGE + 2; separator CYMENUSIZE / 2 with the etch at its
    // middle - 1; text after MNLEFTMARGIN + the 13 px check column + MNXSPACE; the 4074 menus measure the same: border 1 + 2,
    // text 22 px in)
    L->pad = 3;
    L->item_h = L->th + item_t + item_b + (L->theme ? 0 : 2);
    if (L->p_sep) { L->sep_ch = sep_cy; L->sep_top = sep_t >= 0 ? sep_t : 3; L->sep_h = sep_cy + L->sep_top + (sep_b >= 0 ? sep_b : 3); }
    else { L->sep_h = 9; L->sep_top = 9 / 2 - 1; L->sep_ch = 2; }
    L->glyph_x = 2; L->glyph_w = 13;
    L->text_x = 19;
    if (L->p_gutter) { L->gutter_w = 26; L->glyph_x = 4; L->glyph_w = 18; L->text_x = 31; }   // Vista: icons and checks in a gutter
    L->gap = 2 * cw;
    L->right_pad = 17;
}

void UXMENU_SetBand(UXMENU_LOOK* L, HTHEME toolbar, HTHEME menuband)
{
    MARGINS m;
    if (!L->theme || !toolbar) return;
    HDC dc = CreateCompatibleDC(nullptr);
    bool ok = SUCCEEDED(GetThemeMargins(toolbar, dc, 0, 0, TMT_SIZINGMARGINS, nullptr, &m)) && m.cxLeftWidth > 0;
    DeleteDC(dc);
    if (!ok) return;                        // a Toolbar class without the start menu's plate
    COLORREF c;
    L->band = 1;
    L->band_theme = toolbar;
    L->band_arrow = menuband && SUCCEEDED(GetThemeColor(menuband, 0, 0, TMT_TEXTCOLOR, &c)) ? c : L->t_norm;
    L->band_gutter = m.cxLeftWidth;
    L->pad = 0;
    L->band_icon_x = 10;                    // lh4074-215101.png: the icon 10 px from the outline, the text 35 (gutter + 3)
    L->band_text_x = L->band_gutter + 3;
    L->text_x = L->band_text_x;
}

void UXMENU_FreeLook(UXMENU_LOOK* L)
{
    if (L->font) DeleteObject(L->font);
    if (L->bold) DeleteObject(L->bold);
    L->font = L->bold = nullptr;
}

void UXMENU_MeasureItem(const UXMENU_LOOK* L, HDC dc, const WCHAR* text, int len, const WCHAR* accel, unsigned flags, int* tw, int* aw)
{
    HGDIOBJ old = SelectObject(dc, (flags & UXMENU_DEFAULT) ? L->bold : L->font);
    *tw = *aw = 0;
    if (text && len) { RECT r = {}; DrawTextW(dc, text, len, &r, DT_SINGLELINE | DT_CALCRECT); *tw = r.right - r.left; }
    if (accel && *accel) { RECT r = {}; DrawTextW(dc, accel, -1, &r, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX); *aw = r.right - r.left; }
    SelectObject(dc, old);
}

void UXMENU_DrawFrame(const UXMENU_LOOK* L, HDC dc, const RECT* m)
{
    int x = m->left, y = m->top, W = m->right - m->left, H = m->bottom - m->top;
    if (L->band) {          // the menu band: the StartMenu::Toolbar plate (outline, icon gutter, background) over the whole menu
        fill(dc, L->menu, x, y, W, H);
        RECT r = { x, y, x + W, y + H };
        DrawThemeBackground(L->band_theme, dc, 0, 0, &r, nullptr);
    } else if (L->theme) {
        fill(dc, L->menu, x, y, W, H);
        if (L->p_borders) part(L, dc, L->p_borders, 0, x, y, W, H);
        else {
            part(L, dc, L->p_bg, 0, x, y, W, H);
            fill(dc, L->shadow, x, y, W, 1); fill(dc, L->shadow, x, y + H - 1, W, 1);
            fill(dc, L->shadow, x, y, 1, H); fill(dc, L->shadow, x + W - 1, y, 1, H);
        }
        if (L->p_gutter) part(L, dc, L->p_gutter, 0, x + L->pad, y + L->pad, L->gutter_w, H - 2 * L->pad);
    } else if (L->flat) {   // MNDrawFlatMenu...: 1 px BTNSHADOW outline, MENU inside
        fill(dc, L->shadow, x, y, W, H); fill(dc, L->menu, x + 1, y + 1, W - 2, H - 2);
    } else {                // classic popup: EDGE_RAISED (LIGHT3D / DKSHADOW, BTNHIGHLIGHT / BTNSHADOW), then MENU
        fill(dc, L->dkshadow, x, y, W, H); fill(dc, L->light, x, y, W - 1, H - 1);
        fill(dc, L->shadow, x + 1, y + 1, W - 2, H - 2); fill(dc, L->hilight, x + 1, y + 1, W - 3, H - 3);
        fill(dc, L->menu, x + 2, y + 2, W - 4, H - 4);
    }
}

static COLORREF text_color(const UXMENU_LOOK* L, unsigned flags)
{
    bool hot = flags & UXMENU_HOT, dis = flags & UXMENU_DISABLED;
    return dis ? (hot ? L->t_dishot : L->t_dis) : hot ? L->t_hot : L->t_norm;
}

COLORREF UXMENU_TextColor(const UXMENU_LOOK* L, unsigned flags) { return text_color(L, flags); }

// XP greys by embossing when the menu is the 3D face colour (classic), else with GRAYTEXT
static bool emboss(const UXMENU_LOOK* L, unsigned flags)
{
    return (flags & UXMENU_DISABLED) && !(flags & UXMENU_HOT) && !L->theme && !L->flat && L->menu == L->face;
}

void UXMENU_DrawArrow(const UXMENU_LOOK* L, HDC dc, const RECT* rc, unsigned flags)
{
    int h = rc->bottom - rc->top;
    int cx, cy;
    if (L->band) {          // lh4074-215101.png: 4x7 in the menu band's text colour, its right column 7 px before the edge
        int x = rc->right - 10, top = rc->top + (h - 6) / 2;
        for (int k = 0; k < 4; k++) fill(dc, L->band_arrow, x + k, top + k, 1, 7 - 2 * k);
        return;
    }
    // the style's arrow: its right edge 6 px before the item's end (lh4074-195235.png: the 9 x 9 FLYOUTARROW of Slate)
    if (L->theme && part_size(L, dc, L->p_arrow, (flags & UXMENU_DISABLED) ? 2 : 1, &cx, &cy)) {
        part(L, dc, L->p_arrow, (flags & UXMENU_DISABLED) ? 2 : 1, rc->right - 6 - cx, rc->top + (h - cy) / 2, cx, cy);
        return;
    }
    if (emboss(L, flags)) arrow(dc, L->hilight, rc->right + 1, rc->top + 1, h);
    arrow(dc, emboss(L, flags) ? L->shadow : text_color(L, flags), rc->right, rc->top, h);
}

void UXMENU_DrawItem(const UXMENU_LOOK* L, HDC dc, const RECT* rc, unsigned flags, int g, const WCHAR* text, int len,
                     const WCHAR* accel, int accel_x)
{
    int x = rc->left, y = rc->top, iw = rc->right - rc->left, h = rc->bottom - rc->top;
    if (flags & UXMENU_SEPARATOR) {
        if (L->band) x += L->band_gutter, iw -= L->band_gutter;   // the band's separator starts after the icon gutter
        if (L->theme && L->p_sep) { part(L, dc, L->p_sep, 0, x, y + L->sep_top, iw, L->sep_ch); return; }
        fill(dc, L->shadow, x + 1, y + L->sep_top, iw - 2, 1); fill(dc, L->hilight, x + 1, y + L->sep_top + 1, iw - 2, 1);   // BDR_SUNKENOUTER
        return;
    }
    bool hot = flags & UXMENU_HOT, dis = flags & UXMENU_DISABLED, emb = emboss(L, flags);
    COLORREF tc = text_color(L, flags);
    if (flags & UXMENU_TEXTONLY) goto text;
    if (hot) {
        if (L->theme) part(L, dc, L->p_item, dis ? 4 : 2, x, y, iw, h);
        else {
            fill(dc, L->hl, x, y, iw, h);
            if (L->flat) {
                fill(dc, L->hlframe, x, y, iw, 1); fill(dc, L->hlframe, x, y + h - 1, iw, 1);
                fill(dc, L->hlframe, x, y, 1, h); fill(dc, L->hlframe, x + iw - 1, y, 1, h);
            }
        }
    }
    int cx, cy;
    if (g > UXMENU_GLYPH_NONE && g <= UXMENU_GLYPH_CLOSE) {
        if (L->theme) {
            cx = cy = 16;
            part_size(L, dc, L->p_sys[g], dis ? 2 : 1, &cx, &cy);
            part(L, dc, L->p_sys[g], dis ? 2 : 1, x + L->glyph_x + (L->glyph_w - cx) / 2, y + (h - cy) / 2, cx, cy);
        } else {
            int gx = x + L->glyph_x, gy = y + (h - 13) / 2;
            if (emb) glyph(dc, L->hilight, g, gx + 1, gy + 1);
            glyph(dc, emb ? L->shadow : tc, g, gx, gy);
        }
    }
    if (flags & UXMENU_CHECKED) {
        int st = ((flags & UXMENU_RADIO) ? 3 : 1) + (dis ? 1 : 0);
        if (L->theme && part_size(L, dc, L->p_check, st, &cx, &cy))   // the 4074 CHECK part, centred in the check column
            part(L, dc, L->p_check, st, x + L->glyph_x + (L->glyph_w - cx) / 2, y + (h - cy) / 2, cx, cy);
        else {
            int gx = x + L->glyph_x + (L->glyph_w - 13) / 2, gy = y + (h - 13) / 2;
            int mode = SetBkMode(dc, TRANSPARENT);
            if (emb) check(dc, L->hilight, flags & UXMENU_RADIO, gx + 1, gy + 1);
            check(dc, emb ? L->shadow : tc, flags & UXMENU_RADIO, gx, gy);
            SetBkMode(dc, mode);
        }
    }
    if (flags & UXMENU_SUBMENU) UXMENU_DrawArrow(L, dc, rc, flags);
text:
    if (!text && !accel) return;
    HGDIOBJ old = SelectObject(dc, (flags & UXMENU_DEFAULT) ? L->bold : L->font);
    int mode = SetBkMode(dc, TRANSPARENT);
    int ty = y + (h - L->th) / 2;
    // without the keyboard cues the mnemonic is not underlined (DT_HIDEPREFIX, which this DrawText lacks): '&' dropped here
    std::u16string plain;
    if (text && !(flags & UXMENU_CUES)) {
        for (int i = 0; i < len && text[i]; i++) {
            if (text[i] == '&' && i + 1 < len && text[i + 1] == '&') { plain += u'&'; i++; continue; }
            if (text[i] == '&') continue;
            plain += (char16_t)text[i];
        }
        text = (const WCHAR*)plain.c_str(); len = (int)plain.size();
    }
    for (int k = 0; k < 2; k++) {
        const WCHAR* s = k ? accel : text;
        if (!s || !*s || (!k && !len)) continue;
        RECT r;
        UINT f = DT_SINGLELINE | DT_TOP | DT_NOCLIP;
        if (!k) { r = RECT{ x + L->text_x, ty, rc->right, ty + L->th }; f |= DT_LEFT | ((flags & UXMENU_CUES) ? 0 : DT_NOPREFIX); }
        else if (accel_x >= 0) { r = RECT{ accel_x, ty, rc->right, ty + L->th }; f |= DT_LEFT | DT_NOPREFIX; }
        else { r = RECT{ x, ty, rc->right - L->right_pad, ty + L->th }; f |= DT_RIGHT | DT_NOPREFIX; }
        if (emb) {
            RECT e = { r.left + 1, r.top + 1, r.right + 1, r.bottom + 1 };
            SetTextColor(dc, L->hilight);
            DrawTextW(dc, s, k ? -1 : len, &e, f);
        }
        SetTextColor(dc, emb ? L->shadow : tc);
        DrawTextW(dc, s, k ? -1 : len, &r, f);
    }
    SetBkMode(dc, mode);
    SelectObject(dc, old);
}

} // extern "C"
