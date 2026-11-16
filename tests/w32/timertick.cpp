// A window that, a second after it opened, scrolls itself a step every 16 ms timer tick, as mupf's autoscroll does: ScrollWindowEx by 8 pixels and a
// paint of the strip it uncovers (rows numbered in the content, so each band is its own grey). Each tick is printed with
// its GetTickCount and GetMessagePos ("tick 12345 400,300"), flushed; after the given number of ticks (default 150) it
// prints "done" and quits. tests/headless/timerbusy.sh runs it under a busy X server and counts its round trips.
// Usage: timertick [TICKS] (on $DISPLAY).
#include <stdio.h>
#include <stdlib.h>
#include "w32/w32.h"
#include "w32/user.h"

static int g_ticks, g_left = 150, g_y;   // g_y: the content row at the window's top

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER: {
        if (wp == 2) { KillTimer(h, 2); SetTimer(h, 1, 16, nullptr); return 0; }   // the window managed and shown by now
        DWORD pos = GetMessagePos();
        printf("tick %lu %d,%d\n", (unsigned long)GetTickCount(), (short)LOWORD(pos), (short)HIWORD(pos));
        fflush(stdout);
        g_y += 8;
        ScrollWindowEx(h, 0, -8, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
        if (++g_ticks >= g_left) { KillTimer(h, 1); printf("done\n"); fflush(stdout); DestroyWindow(h); }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        for (int y = ps.rcPaint.top; y < ps.rcPaint.bottom; y++) {
            int band = ((g_y + y) / 16) % 8;
            RECT r = { ps.rcPaint.left, y, ps.rcPaint.right, y + 1 };
            HBRUSH b = CreateSolidBrush(RGB(band * 32, band * 32, 255 - band * 32));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        EndPaint(h, &ps);
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
    w32::init();
    if (argc > 1) g_left = atoi(argv[1]);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"timertick";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"timertick", L"timertick", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 640, 480, nullptr, nullptr,
                             nullptr, nullptr);
    if (!w) { fprintf(stderr, "timertick: no window\n"); return 1; }
    UpdateWindow(w);
    SetTimer(w, 2, 1000, nullptr);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
