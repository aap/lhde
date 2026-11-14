// Menus: a menu bar with submenus, checks, separators, disabled items, a
// context menu on right click, and keyboard access (Alt, F10, accelerators).
// stderr: "WM_COMMAND ID" for a chosen item, "WM_INITMENUPOPUP POS" for a
// dropped menu-bar popup (POS its place in the bar); MENUTEST_EDIT=1 puts an
// Edit control with the focus in the window (tests/headless/menukeys.sh).
#include <stdio.h>
#include <stdlib.h>
#include "w32/w32.h"
#include "w32/user.h"

extern "C" void WINAPI WuxInit(void);

static void CALLBACK quit_timer(HWND h, UINT, UINT_PTR, DWORD) { DestroyWindow(h); }

static HMENU build_menu()
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu(), recent = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, 101, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, 102, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, 103, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING | MF_GRAYED, 104, L"Save &As...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(recent, MF_STRING, 121, L"&1 notes.txt");
    AppendMenuW(recent, MF_STRING, 122, L"&2 readme.txt");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)recent, L"&Recent Files");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, 199, L"E&xit\tAlt+F4");
    AppendMenuW(edit, MF_STRING, 201, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, 202, L"Cu&t\tCtrl+X");
    AppendMenuW(edit, MF_STRING, 203, L"&Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING, 204, L"&Paste\tCtrl+V");
    AppendMenuW(view, MF_STRING | MF_CHECKED, 301, L"&Toolbar");
    AppendMenuW(view, MF_STRING | MF_CHECKED, 302, L"&Status Bar");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, 311, L"Lar&ge Icons");
    AppendMenuW(view, MF_STRING, 312, L"S&mall Icons");
    AppendMenuW(view, MF_STRING, 313, L"&List");
    AppendMenuW(view, MF_STRING, 314, L"&Details");
    CheckMenuRadioItem(view, 311, 314, 314, MF_BYCOMMAND);
    AppendMenuW(help, MF_STRING, 401, L"&About...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        fprintf(stderr, "WM_COMMAND %d\n", LOWORD(wp));
        if (LOWORD(wp) == 199) DestroyWindow(h);
        if (LOWORD(wp) >= 311 && LOWORD(wp) <= 314) CheckMenuRadioItem(GetSubMenu(GetMenu(h), 2), 311, 314, LOWORD(wp), MF_BYCOMMAND);
        if (LOWORD(wp) == 301 || LOWORD(wp) == 302) {
            HMENU v = GetSubMenu(GetMenu(h), 2);
            UINT st = GetMenuState(v, LOWORD(wp), MF_BYCOMMAND);
            CheckMenuItem(v, LOWORD(wp), MF_BYCOMMAND | ((st & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED));
        }
        return 0;
    case WM_INITMENUPOPUP:
        if (!HIWORD(lp)) { fprintf(stderr, "WM_INITMENUPOPUP %d\n", LOWORD(lp)); fflush(stderr); }
        return 0;
    case WM_CONTEXTMENU: {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 501, L"&Refresh");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, 502, L"P&roperties");
        POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
        if (pt.x == -1) { pt.x = 50; pt.y = 50; ClientToScreen(h, &pt); }
        TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
        DestroyMenu(m);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int main(int argc, char** argv)
{
    WuxInit();   // the fonts and the session's visual style, as a program has them
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxpmenu";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"flxpmenu", L"flxp menus", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300, nullptr, build_menu(), nullptr, nullptr);
    CreateWindowExW(0, L"Static", L"Click the menu bar, right-click here for a context menu, or press Alt.", WS_CHILD | WS_VISIBLE, 20, 20, 340, 40, w, nullptr, nullptr, nullptr);
    if (getenv("MENUTEST_EDIT"))
        SetFocus(CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 80, 200, 22, w, nullptr,
                                 nullptr, nullptr));
    if (argc > 1) SetTimer(w, 1, atoi(argv[1]), quit_timer);
    // MENUTEST_RECTS=1: the menu bar's items as "item N L T R B" in window coordinates (tests/headless/menubar.sh)
    if (getenv("MENUTEST_RECTS")) {
        RECT wr; GetWindowRect(w, &wr);
        for (int i = 0; i < GetMenuItemCount(GetMenu(w)); i++) {
            RECT r;
            if (GetMenuItemRect(w, GetMenu(w), (UINT)i, &r))
                printf("item %d %ld %ld %ld %ld\n", i, r.left - wr.left, r.top - wr.top, r.right - wr.left, r.bottom - wr.top);
        }
        RECT cr; GetClientRect(w, &cr); POINT o = { 0, 0 }; ClientToScreen(w, &o);
        printf("client %ld %ld %ld %ld\n", o.x - wr.left, o.y - wr.top, o.x - wr.left + cr.right, o.y - wr.top + cr.bottom);
        fflush(stdout);
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
