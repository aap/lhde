// A window that prints, for every WM_KEYDOWN / WM_SYSKEYDOWN, the key and what
// GetKeyState says about Ctrl, Shift and Alt at that moment ("down 0x31 ctrl=1
// shift=0 alt=0"), one line each, flushed. tests/headless/modkeys.sh drives two
// of them with xdotool: a modifier pressed or released while the other had the
// focus must be seen right in this one. Usage: keystate TITLE (on $DISPLAY).
#include <stdio.h>
#include "w32/w32.h"
#include "w32/user.h"

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        printf("down 0x%02x ctrl=%d shift=%d alt=%d\n", (unsigned)wp, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
               (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_MENU) & 0x8000) != 0);
        fflush(stdout);
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
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"keystate";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    wchar_t title[64] = L"keystate";
    if (argc > 1) { int i = 0; for (; argv[1][i] && i < 63; i++) title[i] = (wchar_t)argv[1][i]; title[i] = 0; }
    HWND w = CreateWindowExW(0, L"keystate", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, argc > 2 ? 400 : 0, 0, 300, 200,
                             nullptr, nullptr, nullptr, nullptr);
    if (!w) { fprintf(stderr, "keystate: no window\n"); return 1; }
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
