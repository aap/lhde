/* wux.h: the one include of an lhde application written against wux (docs/APPLICATIONS.md).

   It gives the Win32 surface wux implements, as <windows.h> and its companions declare it (Wine's headers, vendored:
   windows.h, commctrl.h, commdlg.h, uxtheme.h with vsstyle.h, shellapi.h), and wux's own additions (wux/w32/flxp_shell.h:
   the desktop and dock window styles, root properties, file drag and drop, the X selections, PNG writing, MIME data on the
   clipboard). Compile with apps/app.mk's flags (16-bit WCHAR, the shim's CRT redirections); nothing else is needed.

   The entry point is an ordinary main(): call WuxInit() once before any other wux call, then create windows and run the
   message loop (GetMessageW / TranslateMessage / DispatchMessageW, or DialogBoxParamW for a dialog-only program).
   WuxInit loads the fonts (the imported ones, else fontconfig's) and the session's visual style; from then on the program
   follows the session theme by itself (WM_THEMECHANGED, the system colours and metrics), as every flxp program does.

   Resources (dialog templates, menus, string tables, icons) come from a .res compiled by windres from the program's .rc
   and embedded in the binary: WUX_RESOURCES(name, "apps/NAME/NAME.res") at file scope, then
   WuxLoadResources(name##_start, name##_end - name##_start) gives the HINSTANCE to pass to DialogBoxParamW, LoadIconW,
   LoadStringW. */
#ifndef WUX_H
#define WUX_H

#ifdef __cplusplus
#ifndef NOMINMAX
#define NOMINMAX            /* min and max are <algorithm>'s in C++ */
#endif
extern "C" {
#endif

#include <stddef.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <shellapi.h>
#include "w32/flxp_shell.h"

/* The fonts, the session theme and the shim: once, before any other call. Exits with a message when there is no display. */
void WINAPI WuxInit(void);

/* A .res image in memory (copied) as a module: the HINSTANCE for the resource functions. NULL on a damaged image. */
HINSTANCE WINAPI WuxLoadResources(const void *data, size_t size);

#ifdef __cplusplus
}
#define WUX_RES_EXTERN_ extern "C"
#else
#define WUX_RES_EXTERN_ extern
#endif

#ifdef __cplusplus
#include <string>
/* UTF-8 (the shell layer's strings, file names) to the API's UTF-16 and back: MultiByteToWideChar with CP_UTF8 */
inline std::u16string WuxWide(const std::string &s)
{
    std::u16string w(s.size() + 1, 0);
    int n = s.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), (WCHAR *)&w[0], (int)w.size());
    w.resize(n > 0 ? (size_t)n : 0);
    return w;
}
inline std::string WuxUtf8(const WCHAR *w, int len = -1)
{
    if (!w || !len || (len < 0 && !*w)) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? (size_t)n : 0, 0);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
    if (len < 0 && !s.empty() && !s.back()) s.pop_back();   /* the terminator counted with len -1 */
    return s;
}
/* A window's text as UTF-8 */
inline std::string WuxWindowText(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::u16string w((size_t)n + 1, 0);
    GetWindowTextW(h, (WCHAR *)&w[0], n + 1);
    return WuxUtf8((const WCHAR *)w.c_str());
}
#endif

/* Embeds FILE (a path from the top of the tree, as the assembler sees it) as NAME_start .. NAME_end. File scope. */
#define WUX_RESOURCES(name, file) \
    __asm__(".section .rodata\n.balign 4\n.global " #name "_start\n.global " #name "_end\n" #name "_start:\n" \
            ".incbin \"" file "\"\n" #name "_end:\n.previous\n"); \
    WUX_RES_EXTERN_ const unsigned char name##_start[], name##_end[]

#endif
