// First real test of the shim: a window with Wine's classic controls.
#include <stdio.h>
#include "w32/w32.h"
#include "w32/user.h"

static void CALLBACK quit_timer(HWND h, UINT, UINT_PTR, DWORD) { DestroyWindow(h); }

static BOOL CALLBACK dump_child(HWND c, LPARAM)
{
    wchar_t cls[64]; GetClassNameW(c, cls, 64); RECT r; GetWindowRect(c, &r); RECT cr; GetClientRect(c, &cr);
    fprintf(stderr, "%p %-10s id=%-4d parent=%p vis=%d rect=(%ld,%ld)-(%ld,%ld) client=%ldx%ld style=%08lx\n", (void*)c, w32::utf8_of((const WCHAR*)cls).c_str(), (int)GetDlgCtrlID(c), (void*)GetParent(c), IsWindowVisible(c), (long)r.left, (long)r.top, (long)r.right, (long)r.bottom, (long)cr.right, (long)cr.bottom, (long)GetWindowLongW(c, GWL_STYLE));
    return TRUE;
}

static LRESULT CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        fprintf(stderr, "WM_COMMAND id=%d code=%d\n", LOWORD(wp), HIWORD(wp));
        if (LOWORD(wp) == 99) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int main(int argc, char** argv)
{
    w32::init();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = main_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"flxptest";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"flxptest", L"flxp controls", WS_OVERLAPPEDWINDOW, 100, 100, 420, 340, nullptr, nullptr, nullptr, nullptr);
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    struct { const wchar_t* cls; const wchar_t* text; DWORD style; DWORD ex; int x, y, cx, cy; int id; } items[] = {
        { L"Static", L"Name:", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 12, 14, 60, 13, 1 },
        { L"Edit", L"Some text in an edit control", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 72, 10, 200, 21, 2 },
        { L"Button", L"&Push Me", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 290, 10, 100, 23, 3 },
        { L"Button", L"&Default", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 290, 40, 100, 23, 4 },
        { L"Button", L"Check &box", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 12, 44, 120, 16, 5 },
        { L"Button", L"Checked", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 12, 64, 120, 16, 6 },
        { L"Button", L"&Radio one", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, 0, 150, 44, 120, 16, 7 },
        { L"Button", L"Radio &two", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 0, 150, 64, 120, 16, 8 },
        { L"Button", L"Group box", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 6, 86, 274, 120, 9 },
        { L"ListBox", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_BORDER | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE, 16, 104, 120, 94, 10 },
        { L"ComboBox", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 150, 104, 120, 200, 11 },
        { L"ComboBox", L"Editable", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN, 0, 150, 134, 120, 200, 12 },
        { L"ScrollBar", L"", WS_CHILD | WS_VISIBLE | SBS_HORZ, 0, 150, 170, 120, 16, 13 },
        { L"Button", L"Disabled", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON, 0, 290, 70, 100, 23, 14 },
        { L"Static", L"A static control with a longer text that wraps onto several lines inside its rectangle.", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 290, 104, 110, 70, 15 },
        { L"Edit", L"Multi-line\r\nedit control\r\nwith a vertical\r\nscrollbar and\r\nmore lines than fit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, 12, 214, 260, 60, 16 },
        { L"Button", L"&Quit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 290, 250, 100, 23, 99 },
        { L"Static", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 0, 290, 230, 110, 2, 17 },
        { L"ComboBox", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_SIMPLE, 0, 290, 180, 100, 60, 18 },
    };
    for (auto& it : items) {
        HWND c = CreateWindowExW(it.ex, (LPCWSTR)it.cls, (LPCWSTR)it.text, it.style, it.x, it.y, it.cx, it.cy, w, (HMENU)(intptr_t)it.id, nullptr, nullptr);
        if (!c) { fprintf(stderr, "failed to create %ls\n", it.cls); continue; }
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        if (it.id == 6) SendMessageW(c, BM_SETCHECK, BST_CHECKED, 0);
        if (it.id == 7) SendMessageW(c, BM_SETCHECK, BST_CHECKED, 0);
        if (it.id == 10) {
            const wchar_t* names[] = { L"Alpha", L"Bravo", L"Charlie", L"Delta", L"Echo", L"Foxtrot", L"Golf", L"Hotel", L"India", L"Juliet", L"Kilo" };
            for (auto n : names) SendMessageW(c, LB_ADDSTRING, 0, (LPARAM)n);
            SendMessageW(c, LB_SETCURSEL, 2, 0);
        }
        if (it.id == 11 || it.id == 12 || it.id == 18) {
            const wchar_t* names[] = { L"One", L"Two", L"Three", L"Four" };
            for (auto n : names) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)n);
            if (it.id == 11) SendMessageW(c, CB_SETCURSEL, 1, 0);
        }
        if (it.id == 13) { SCROLLINFO si = { sizeof si, SIF_ALL, 0, 100, 20, 30, 0 }; SetScrollInfo(c, SB_CTL, &si, TRUE); }
    }
    ShowWindow(w, SW_SHOW);
    EnumChildWindows(w, dump_child, 0);
    SetFocus(GetDlgItem(w, 2));
    if (argc > 1) SetTimer(w, 1234, atoi(argv[1]), quit_timer);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(w, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
