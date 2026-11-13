// The Win32 cursors on X: a window sets each IDC_ cursor in turn (SetCursor from WM_SETCURSOR, as a program does) with the
// pointer over it, and reads back the cursor the X server shows (XFixesGetCursorImage on a second connection: its name,
// which wux gives every cursor it makes after the theme name it asked for first). Prints "IDC_NAME name" per cursor and
// exits 1 when one is not the expected one. tests/headless/cursors.sh moves the pointer into the window first.
// cursortest --shown: the name of the cursor on screen now, nothing else.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "w32/w32.h"

// Xlib's and XFixes' few declarations used here (their headers' macros clash with the win32 ones)
extern "C" {
typedef struct _XDisplay Display;
typedef struct {
    short x, y;
    unsigned short width, height, xhot, yhot;
    unsigned long cursor_serial;
    unsigned long* pixels;
    unsigned long atom;
    const char* name;
} XFixesCursorImage;
Display* XOpenDisplay(const char*);
int XFree(void*);
int XFixesQueryExtension(Display*, int*, int*);
XFixesCursorImage* XFixesGetCursorImage(Display*);
}

extern "C" void WINAPI WuxInit(void);

static const struct { LPCWSTR id; const char* name; const char* x; } cursors[] = {
    { (LPCWSTR)IDC_ARROW, "IDC_ARROW", "default" },         { (LPCWSTR)IDC_IBEAM, "IDC_IBEAM", "text" },
    { (LPCWSTR)IDC_WAIT, "IDC_WAIT", "wait" },              { (LPCWSTR)IDC_APPSTARTING, "IDC_APPSTARTING", "progress" },
    { (LPCWSTR)IDC_CROSS, "IDC_CROSS", "crosshair" },       { (LPCWSTR)IDC_UPARROW, "IDC_UPARROW", "up-arrow" },
    { (LPCWSTR)IDC_SIZEWE, "IDC_SIZEWE", "ew-resize" },     { (LPCWSTR)IDC_SIZENS, "IDC_SIZENS", "ns-resize" },
    { (LPCWSTR)IDC_SIZENWSE, "IDC_SIZENWSE", "nwse-resize" }, { (LPCWSTR)IDC_SIZENESW, "IDC_SIZENESW", "nesw-resize" },
    { (LPCWSTR)IDC_SIZEALL, "IDC_SIZEALL", "all-scroll" },  { (LPCWSTR)IDC_HAND, "IDC_HAND", "pointer" },
    { (LPCWSTR)IDC_NO, "IDC_NO", "not-allowed" },           { (LPCWSTR)IDC_HELP, "IDC_HELP", "help" },
};
static int g_cur = -1;
static Display* g_dpy;
static int g_fails;

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT && g_cur >= 0) { SetCursor(LoadCursorW(nullptr, cursors[g_cur].id)); return TRUE; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, msg, wp, lp);
}

// the name of the cursor on screen, waiting up to a second for it to be `want`
static std::string shown(const char* want)
{
    std::string name;
    for (int i = 0; i < 50; i++) {
        XFixesCursorImage* im = XFixesGetCursorImage(g_dpy);
        name = im && im->name ? im->name : "";
        if (im) XFree(im);
        if (name == want) break;
        usleep(20000);
    }
    return name;
}

static void CALLBACK step(HWND h, UINT, UINT_PTR, DWORD)
{
    if (g_cur >= 0) {
        std::string got = shown(cursors[g_cur].x);
        bool ok = got == cursors[g_cur].x;
        printf("%s %s %s\n", ok ? "ok  " : "FAIL", cursors[g_cur].name, got.empty() ? "(no name)" : got.c_str());
        if (!ok) g_fails++;
    }
    if (++g_cur >= (int)(sizeof cursors / sizeof cursors[0])) { KillTimer(h, 1); DestroyWindow(h); return; }
    SetCursor(LoadCursorW(nullptr, cursors[g_cur].id));   // the pointer is over the window: shown now
}

int main(int argc, char** argv)
{
    g_dpy = XOpenDisplay(nullptr);
    int ev, err;
    if (!g_dpy || !XFixesQueryExtension(g_dpy, &ev, &err)) { printf("skip: no XFixes\n"); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--shown")) {   // only the name of the cursor on screen (another program's)
        XFixesCursorImage* im = XFixesGetCursorImage(g_dpy);
        printf("%s\n", im && im->name && *im->name ? im->name : "(no name)");
        return 0;
    }
    WuxInit();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"cursortest";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"cursortest", L"cursortest", WS_POPUP | WS_VISIBLE, 0, 0, 300, 200, nullptr, nullptr, nullptr, nullptr);
    UpdateWindow(w);
    // the script moves the pointer in when the window shows; the steps start after it
    SetTimer(w, 1, 300, step);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    printf("%d failed\n", g_fails);
    return g_fails ? 1 : 0;
}
