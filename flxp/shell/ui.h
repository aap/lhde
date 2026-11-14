// ui.h: the shell layer's win32 part (flxp/shell/ui/, compiled on wux and linked with it: libflxpshell.a, flxp's
// programs; not the native toolkit tests). What a program shows of the shell: the message box, a shell icon as an HICON,
// the Properties sheet, Default Programs. Include it after the win32 headers (wux/wux.h, or w32/w32.h inside flxp); shell/shell.h does.
#pragma once
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max      // windef.h's, before the library headers
#include <functional>
#include <string>
#include <vector>
#pragma pop_macro("max")
#pragma pop_macro("min")

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
// An image in _NET_WM_ICON's layout (shell::app_icon's, px x px) as an HICON; null when empty. The caller destroys it.
HICON icon_from_argb(const std::vector<unsigned long>& im);
// A program's icon by its desktop file id (shell::App::id: the Open With list, Properties' Opens with), at px: the shared
// lookup (shell::app_icon) with the desktop file's Icon= first (a program's own icon), then the class map by the id's name
// (xterm.desktop: cmd), then the generic application icon
HICON app_hicon(const std::string& desktop_id, int px);
// A program's own window icon from an icon key (WM_SETICON ICON_SMALL 16 px and ICON_BIG 32 px, so _NET_WM_ICON):
// flxp-settings "display", luxpad "notepad", luxshot "image"; false (nothing set) when no icon set has the key
bool set_window_icon_key(HWND w, const std::string& key);

// The Properties sheet of files and folders (XP's file Properties; flxp's Properties verb, luxattr), titled "NAME
// Properties" ("a.txt, b.txt, ... Properties" for several), modal to owner (null: a window of its own). Two pages:
//   General   the icon, the name, the type (with the MIME type), Opens with (Change... when change_program is set), the
//             location, size ("1.29 KB (1,331 bytes)"), what a folder contains, the three dates with seconds, the
//             permissions and owner, a link's target; for several items XP's "N Files, M Folders", "All of type X" /
//             "Multiple Types", "All in DIR" / "Various Folders" and the total size.
//   Metadata  the registry's stored properties (shell/details.h): Description (one line), Tags (the files' tags in a list,
//             one a row, a tag some of the files have in grey; a box with the known tags in its drop-down, Add or Enter
//             adding what is typed to every file, Remove or Delete taking the selected ones from the files that have
//             them), Color (a row of swatches, None first), Comment (several lines), Run as (a command, Browse... for
//             the program); Advanced >> lists every user.* attribute with its value (luxfs's Author, Source, Build, Notes,
//             To do among them), edited in place (F2, a double click), New Attribute..., Remove. Several files: a shared
//             value shows, else "(several values)", and what is typed goes to every file.
// OK writes and closes, Apply writes and stays, Cancel drops; every value through details::write (as the Explorer writes
// it); an error (read-only, not yours, no user attributes) in a message box, the sheet staying. True when something was
// written.
struct PropertiesOptions {
    int page = 0;                     // the page shown first: 0 General (XP's), 1 Metadata
    std::string name;                 // one item: its name as the caller shows it (else the file's name)
    std::string type;                 // one item: its type as the caller shows it (else shell::type_name)
    std::string icon_key;             // one item: its icon key (else shell::icon_key of the name)
    std::string window_icon;          // the sheet's own window icon, an icon key (luxattr: "settings"); none when empty
    // General's Change... beside "Opens with" (one file, not a folder), shown when set: called with the sheet's page as the
    // owner and the file's MIME type; true when the default program changed (the page shows the new one)
    std::function<bool(HWND page, const std::string& path, const std::string& mime)> change_program;
};
bool properties(HWND owner, const std::vector<std::string>& paths, const PropertiesOptions& o = PropertiesOptions());
// Default Programs (shell/roles.h; XP's Set Program Access and Defaults): the roles in a list, the selected one's question
// with "Use my current ..." and its installed programs as radio buttons. OK and Apply write settings.ini [programs] and
// apply the roles (the role file) at once; Cancel drops what was not applied. role: the role selected first (its id;
// "" the first). Modal to owner (null: a window of its own). True when something was applied.
bool default_programs(HWND owner, const std::string& role = "");

// A one-line edit's paste keeps every line of the clipboard, joined by spaces (the edit control alone keeps the first)
void one_line_edit(HWND edit);

} // namespace shell
