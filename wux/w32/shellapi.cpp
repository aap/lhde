// shell32's file drop for Win32 programs: DragAcceptFiles, WM_DROPFILES, DragQueryFile, DragQueryPoint, DragFinish,
// over wux's X drops (flxp_shell.h: every toplevel is an XDND target and gets WM_FLXP_DRAGOVER / WM_FLXP_DROP). A
// window that accepts files (WS_EX_ACCEPTFILES, which DragAcceptFiles sets) answers a drag of files over it or over its
// children with a copy (DefWindowProc, defwnd.cpp) and, on the drop, is posted WM_DROPFILES with an HDROP: a
// GlobalAlloc'd DROPFILES as on Windows (the drop point in the window's client coordinates, the paths as UTF-16 after
// it, double-NUL terminated), which DragFinish frees.
#include <string.h>
#include "user.h"
extern "C" {
#include "shellapi.h"
}

using namespace w32;

namespace {
struct DropFiles { DWORD pFiles; POINT pt; BOOL fNC; BOOL fWide; };   // shlobj.h's DROPFILES
}

namespace w32 {

HDROP make_hdrop(HWND h, int x, int y, const char* const* paths, unsigned count)
{
    std::u16string list;
    for (unsigned i = 0; i < count; i++) { list += utf16_of(paths[i]); list += u'\0'; }
    list += u'\0';
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DropFiles) + list.size() * sizeof(char16_t));
    DropFiles* d = g ? (DropFiles*)GlobalLock(g) : nullptr;
    if (!d) return nullptr;
    POINT pt = { x, y };
    ScreenToClient(h, &pt);
    d->pFiles = sizeof(DropFiles);
    d->pt = pt;
    d->fNC = FALSE;
    d->fWide = TRUE;
    memcpy(d + 1, list.data(), list.size() * sizeof(char16_t));
    GlobalUnlock(g);
    return (HDROP)g;
}

} // namespace w32

extern "C" {

void WINAPI DragAcceptFiles(HWND h, BOOL accept)
{
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    SetWindowLongW(h, GWL_EXSTYLE, accept ? (ex | WS_EX_ACCEPTFILES) : (ex & ~WS_EX_ACCEPTFILES));
}

UINT WINAPI DragQueryFileW(HDROP hdrop, UINT index, LPWSTR buf, UINT cch)
{
    const DropFiles* d = hdrop ? (const DropFiles*)GlobalLock((HGLOBAL)hdrop) : nullptr;
    if (!d) return 0;
    const char16_t* p = (const char16_t*)((const BYTE*)d + d->pFiles);
    UINT n = 0, ret = 0;
    for (; *p; n++) {
        size_t len = std::char_traits<char16_t>::length(p);
        if (n == index) {
            ret = (UINT)len;
            if (buf && cch) {
                size_t k = len < cch - 1 ? len : cch - 1;
                memcpy(buf, p, k * sizeof(WCHAR));
                buf[k] = 0;
                ret = (UINT)k;
            }
            break;
        }
        p += len + 1;
    }
    GlobalUnlock((HGLOBAL)hdrop);
    return index == 0xFFFFFFFF ? n : ret;
}

UINT WINAPI DragQueryFileA(HDROP hdrop, UINT index, LPSTR buf, UINT cch)
{
    if (index == 0xFFFFFFFF) return DragQueryFileW(hdrop, index, nullptr, 0);
    UINT len = DragQueryFileW(hdrop, index, nullptr, 0);
    std::u16string w(len + 1, 0);
    DragQueryFileW(hdrop, index, (LPWSTR)&w[0], len + 1);
    w.resize(len);
    std::string s = utf8_of(w);
    if (!buf || !cch) return (UINT)s.size();
    size_t k = s.size() < cch - 1 ? s.size() : cch - 1;
    memcpy(buf, s.data(), k);
    buf[k] = 0;
    return (UINT)k;
}

BOOL WINAPI DragQueryPoint(HDROP hdrop, POINT* pt)
{
    const DropFiles* d = hdrop ? (const DropFiles*)GlobalLock((HGLOBAL)hdrop) : nullptr;
    if (!d) return FALSE;
    if (pt) *pt = d->pt;
    BOOL client = !d->fNC;
    GlobalUnlock((HGLOBAL)hdrop);
    return client;
}

void WINAPI DragFinish(HDROP hdrop) { if (hdrop) GlobalFree((HGLOBAL)hdrop); }

} // extern "C"
