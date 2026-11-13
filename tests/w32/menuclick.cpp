// Context menus and the clicks around them (docs/CLIENT-PROTOCOL.md "Menus and clicks"): a window that opens a
// popup menu (TrackPopupMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD) on a right button release and prints, one line each,
// flushed:
//   down NAME L|R X Y       a button press that reached the window (client coordinates)
//   open NAME               the menu is up (printed before TrackPopupMenu)
//   end NAME CMD            TrackPopupMenu returned (0: dismissed)
//   active NAME 1|0         WM_ACTIVATE
// tests/headless/menus.sh drives two of them (two processes) and a second window of one of them (one process) under
// luxwm with xdotool. Usage: menuclick NAME X Y [NAME2 X2 Y2] (a second top-level of the same process).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "w32/w32.h"
#include "w32/user.h"

static std::string name_of(HWND h)
{
    wchar_t t[64] = {};
    GetWindowTextW(h, t, 63);
    std::string s;
    for (int i = 0; t[i]; i++) s += (char)t[i];
    return s;
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        printf("down %s %c %d %d\n", name_of(h).c_str(), msg == WM_LBUTTONDOWN ? 'L' : 'R', (short)LOWORD(lp), (short)HIWORD(lp));
        fflush(stdout);
        return 0;
    case WM_RBUTTONUP: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ClientToScreen(h, &pt);
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 1, L"&Open");
        AppendMenuW(m, MF_STRING, 2, L"&Properties");
        printf("open %s\n", name_of(h).c_str());
        fflush(stdout);
        int cmd = (int)TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, h, nullptr);
        printf("end %s %d\n", name_of(h).c_str(), cmd);
        fflush(stdout);
        DestroyMenu(m);
        return 0;
    }
    case WM_ACTIVATE:
        printf("active %s %d\n", name_of(h).c_str(), LOWORD(wp) != WA_INACTIVE);
        fflush(stdout);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HWND make(const char* name, int x, int y)
{
    wchar_t title[64] = {};
    for (int i = 0; name[i] && i < 63; i++) title[i] = (wchar_t)name[i];
    return CreateWindowExW(0, L"menuclick", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, y, 300, 220, nullptr, nullptr, nullptr, nullptr);
}

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: menuclick NAME X Y [NAME2 X2 Y2]\n"); return 2; }
    w32::init();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"menuclick";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    if (!make(argv[1], atoi(argv[2]), atoi(argv[3]))) { fprintf(stderr, "menuclick: no window\n"); return 1; }
    if (argc >= 7 && !make(argv[4], atoi(argv[5]), atoi(argv[6]))) { fprintf(stderr, "menuclick: no second window\n"); return 1; }
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
