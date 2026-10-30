// The vendored list view's tile view and hidden labels (listview.c, flxp:):
// LV_VIEW_TILE over the icon layout, LVM_SET/GETTILEVIEWINFO's lines,
// LVM_SET/GETTILEINFO with subitems beyond the columns, the rectangles of a
// tile, LVS_EX_HIDELABELS. Needs a display (make check runs it under Xvfb).
#include <stdio.h>
#include <string.h>
#include "w32/w32.h"
#include "w32/user.h"
extern "C" {
#include "commctrl.h"
}

static int fails, checks;
static void check(const char* what, long want, long got)
{
    checks++;
    if (want == got) return;
    fails++;
    printf("FAIL %s: expected %ld got %ld\n", what, want, got);
}

int main()
{
    w32::init();
    HWND top = CreateWindowExW(0, L"Static", L"lvtiles", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, nullptr, nullptr);
    HWND lv = CreateWindowExW(0, WC_LISTVIEWW, nullptr, WS_CHILD | WS_VISIBLE | LVS_ICON, 0, 0, 600, 400, top, nullptr, nullptr, nullptr);
    HIMAGELIST il = ImageList_Create(48, 48, ILC_COLOR32, 4, 4);
    SendMessageW(lv, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)il);
    LVCOLUMNW c = {};
    c.mask = LVCF_TEXT | LVCF_WIDTH; c.cx = 100; c.pszText = (LPWSTR)L"Name";
    SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&c);
    for (int i = 0; i < 3; i++) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (LPWSTR)(i == 0 ? L"alpha" : i == 1 ? L"beta" : L"gamma");
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
    }

    // the tile view: the icon layout, LVM_GETVIEW says tiles; a style change ends it
    check("set tile view", 1, (long)SendMessageW(lv, LVM_SETVIEW, LV_VIEW_TILE, 0));
    check("get tile view", LV_VIEW_TILE, (long)SendMessageW(lv, LVM_GETVIEW, 0, 0));
    SendMessageW(lv, LVM_SETICONSPACING, 0, MAKELPARAM(218, 56));
    SendMessageW(lv, LVM_ARRANGE, LVA_DEFAULT, 0);

    // lines: subitems beyond the one column, two of three shown
    LVTILEVIEWINFO tvi = {};
    tvi.cbSize = sizeof tvi; tvi.dwMask = LVTVIM_COLUMNS; tvi.cLines = 2;
    check("set tile view info", 1, (long)SendMessageW(lv, LVM_SETTILEVIEWINFO, 0, (LPARAM)&tvi));
    LVTILEVIEWINFO got = {};
    got.cbSize = sizeof got;
    SendMessageW(lv, LVM_GETTILEVIEWINFO, 0, (LPARAM)&got);
    check("tile lines", 2, got.cLines);
    check("tile width", 218, got.sizeTile.cx);
    const wchar_t* texts[3] = { L"Type: File Folder", L"", L"Size: 1 KB" };
    for (int k = 0; k < 3; k++) {
        LVITEMW sub = {};
        sub.mask = LVIF_TEXT; sub.iItem = 0; sub.iSubItem = 8 + k; sub.pszText = (LPWSTR)texts[k];
        check("subitem beyond the columns", 1, (long)SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&sub));
    }
    LVITEMW too_far = {};
    too_far.mask = LVIF_TEXT; too_far.iItem = 0; too_far.iSubItem = 40; too_far.pszText = (LPWSTR)L"x";
    check("subitem 40 refused", 0, (long)SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&too_far));
    UINT cols[3] = { 8, 9, 10 };
    LVTILEINFO ti = {};
    ti.cbSize = sizeof ti; ti.iItem = 0; ti.cColumns = 3; ti.puColumns = cols;
    check("set tile info", 1, (long)SendMessageW(lv, LVM_SETTILEINFO, 0, (LPARAM)&ti));
    UINT back[4] = {};
    LVTILEINFO tb = {};
    tb.cbSize = sizeof tb; tb.iItem = 0; tb.cColumns = 4; tb.puColumns = back;
    SendMessageW(lv, LVM_GETTILEINFO, 0, (LPARAM)&tb);
    check("tile info columns", 3, (long)tb.cColumns);
    check("tile info column 3", 10, (long)back[2]);

    // the rectangles: the cell, the image at the left centred, the name and two lines right of it
    RECT b = {}, ic = {}, lb = {}, sel = {};
    b.left = LVIR_BOUNDS; SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&b);
    ic.left = LVIR_ICON; SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&ic);
    lb.left = LVIR_LABEL; SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&lb);
    sel.left = LVIR_SELECTBOUNDS; SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&sel);
    check("tile width", 218, b.right - b.left);
    check("tile height", 56, b.bottom - b.top);
    check("image left", b.left + 4, ic.left);
    check("image centred", b.top + 4, ic.top);
    check("label after the image", ic.right + 5, lb.left);
    TEXTMETRICW tm = {};
    HDC dc = GetDC(lv);
    SelectObject(dc, (HFONT)SendMessageW(lv, WM_GETFONT, 0, 0));
    GetTextMetricsW(dc, &tm);
    ReleaseDC(lv, dc);
    check("three lines (two non-empty tile lines)", 3 * tm.tmHeight, lb.bottom - lb.top);
    check("selection 2 px inside", b.left + 2, sel.left);
    check("selection height", 52, sel.bottom - sel.top);
    RECT b1 = {};
    b1.left = LVIR_BOUNDS; SendMessageW(lv, LVM_GETITEMRECT, 1, (LPARAM)&b1);
    check("next tile to the right", b.left + 218, b1.left);
    LVHITTESTINFO ht = {};
    ht.pt.x = lb.left + 3; ht.pt.y = lb.top + tm.tmHeight + 2;   // on the first tile line
    check("hit on a tile line", 0, (long)SendMessageW(lv, LVM_HITTEST, 0, (LPARAM)&ht));

    // a style change (or another view) ends the tiles
    SendMessageW(lv, LVM_SETVIEW, LV_VIEW_ICON, 0);
    check("icon view again", LV_VIEW_ICON, (long)SendMessageW(lv, LVM_GETVIEW, 0, 0));
    SendMessageW(lv, LVM_SETVIEW, LV_VIEW_TILE, 0);
    SetWindowLongW(lv, GWL_STYLE, (GetWindowLongW(lv, GWL_STYLE) & ~LVS_TYPEMASK) | LVS_REPORT);
    check("style ends tiles", LV_VIEW_DETAILS, (long)SendMessageW(lv, LVM_GETVIEW, 0, 0));
    SetWindowLongW(lv, GWL_STYLE, (GetWindowLongW(lv, GWL_STYLE) & ~LVS_TYPEMASK) | LVS_ICON);

    // no labels: the label is empty under the image, the selection the image
    SendMessageW(lv, LVM_SETICONSPACING, 0, MAKELPARAM(110, 105));
    SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_HIDELABELS, LVS_EX_HIDELABELS);
    SendMessageW(lv, LVM_ARRANGE, LVA_DEFAULT, 0);
    ic.left = LVIR_ICON; SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&ic);
    lb.left = LVIR_LABEL; SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&lb);
    check("hidden label height", 0, lb.bottom - lb.top);
    check("hidden label under the image", ic.bottom, lb.top);
    SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_HIDELABELS, 0);
    lb.left = LVIR_LABEL; SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&lb);
    check("label back", 1, lb.bottom - lb.top >= tm.tmHeight);

    DestroyWindow(top);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
