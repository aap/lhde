// imm32: no input method. Every window has no input context.
#include "w32/w32.h"
extern "C" {
#include "imm.h"
}

extern "C" {

HIMC WINAPI ImmGetContext(HWND) { return nullptr; }
BOOL WINAPI ImmReleaseContext(HWND, HIMC) { return TRUE; }
LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD) { return 0; }
BOOL WINAPI ImmSetCompositionFontW(HIMC himc, LPLOGFONTW) { return himc != nullptr; }
BOOL WINAPI ImmSetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM) { return himc != nullptr; }
BOOL WINAPI ImmNotifyIME(HIMC, DWORD, DWORD, DWORD) { return FALSE; }
BOOL WINAPI ImmGetOpenStatus(HIMC) { return FALSE; }
BOOL WINAPI ImmSetOpenStatus(HIMC, BOOL) { return FALSE; }
BOOL WINAPI ImmIsIME(HKL) { return FALSE; }
HWND WINAPI ImmGetDefaultIMEWnd(HWND) { return nullptr; }
HIMC WINAPI ImmAssociateContext(HWND, HIMC) { return nullptr; }

} // extern "C"
