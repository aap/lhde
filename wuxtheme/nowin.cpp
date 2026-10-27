// libuxtheme.so without a window system: the few user32/window calls the
// vendored uxtheme makes (DrawThemeParentBackground, dialog textures, the
// non-client metrics msstyles.c reads) answered as if there were no windows.
// flxp itself links src/w32/user.cpp and sysparams.cpp instead of this file.
#include <string.h>
#include <time.h>
#include "w32/w32.h"
#include "sys/sys.h"

extern "C" {

ATOM atDialogThemeEnabled;

HANDLE WINAPI GetPropW(HWND, LPCWSTR) { return nullptr; }
BOOL WINAPI SetPropW(HWND, LPCWSTR, HANDLE) { return FALSE; }
HWND WINAPI GetParent(HWND) { return nullptr; }
BOOL WINAPI GetClientRect(HWND, RECT* r) { if (r) memset(r, 0, sizeof *r); return FALSE; }
INT WINAPI MapWindowPoints(HWND, HWND, POINT*, UINT) { return 0; }
LRESULT WINAPI SendMessageW(HWND, UINT, WPARAM, LPARAM) { return 0; }
DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT c) { return c; }

// Only the theme's own metrics matter here; nothing is applied.
BOOL WINAPI SystemParametersInfoW(UINT action, UINT, void* param, UINT)
{
    switch (action) {
    case SPI_GETNONCLIENTMETRICS:
        if (param) { UINT cb = ((NONCLIENTMETRICSW*)param)->cbSize; memset(param, 0, cb); ((NONCLIENTMETRICSW*)param)->cbSize = cb; }
        return TRUE;
    case SPI_GETICONTITLELOGFONT:
        if (param) memset(param, 0, sizeof(LOGFONTW));
        return TRUE;
    default:
        return FALSE;
    }
}

} // extern "C"

namespace sys {

uint32_t ticks()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

} // namespace sys
