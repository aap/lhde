// ui.h: the shell layer's win32 part (flxp/shell/ui/, compiled on wux and linked with it: libflxpshell.a, flxp's
// programs; not the native toolkit tests). What a program shows of the shell: the message box, a shell icon as an HICON.
// Include it after the win32 headers (wux/wux.h, or w32/w32.h inside flxp); shell/shell.h does.
#pragma once
#include <string>

namespace shell {

// A message box (wux's MessageBoxW: the icon, the text and the buttons of `type`, MB_OK | MB_ICONEXCLAMATION, MB_YESNO |
// MB_ICONQUESTION, ...) with UTF-8 strings; returns IDOK, IDYES, IDNO, IDCANCEL, ... owner may be null.
int message_box(HWND owner, const std::string& title, const std::string& text, unsigned type);

// An icon theme's source (shell/icons.h: an .ico or image file, or "pe:FILE#ID", a resource of a PE file read as data) as
// a HICON / HBITMAP of cx x cy (LoadImageW's type and flags; a theme of depth 4, the Windows 2000 look, adds LR_VGACOLOR to
// icons). Null when the source is missing. The caller destroys it.
HANDLE load_image(const std::string& src, UINT type, int cx, int cy, UINT flags = 0);
// The icon of an icon key (shell::icon_key, "folder", "document", ...) at px, "document"'s when the key has none; null
// when neither loads. The caller destroys it (DestroyIcon).
HICON icon_for_key(const std::string& key, int px);
// The icon the Explorer shows for a file or folder (its icon key by name and type) at px.
HICON icon_for(const std::string& path, int px);

} // namespace shell
