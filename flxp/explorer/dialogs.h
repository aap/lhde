// The explorer's small dialogs, built as in-memory DLGTEMPLATEs and run by
// the vendored Wine dialog manager (the comdlg32 .res route needs a resource
// file; these are ours, so they are laid out here in dialog units).
#pragma once
#include <string>
#include <vector>
#include "w32/w32.h"
extern "C" {
#include "commctrl.h"
}

namespace dialogs {

// XP's "Open With" (shell32 openwith.cpp): recommended and other programs,
// "Always use the selected program to open this kind of file".
struct OpenWith { std::string app_id; bool always = false; };
bool choose_program(HWND owner, const std::string& file_name, const std::string& mime,
                    HICON file_icon, HIMAGELIST small, int app_image, bool always_default, OpenWith* out);

bool confirm_delete(HWND owner, const std::vector<std::string>& names, HICON icon);   // Yes = true
void message(HWND owner, const std::string& title, const std::string& text, bool error = true);

// Organize Bookmarks: rename, remove, reorder; saves the GTK bookmarks file on OK.
bool organize_bookmarks(HWND owner, HIMAGELIST small, int folder_image);

// File/folder properties (a General page in a property sheet).
void properties(HWND owner, const std::string& path, const std::string& display_name, const std::string& type_name,
                HICON icon, HIMAGELIST small, int app_image);

// Folder Options (Tools menu): XP's General and View pages reduced to flxp's settings.
// View codes as browser::view_code (LVS_*, VIEW_THUMBNAILS, VIEW_THUMBS_FLAG, the zoom
// above ZOOM_SHIFT). The buttons act at once through the callbacks (as XP's do, after a
// question); OK and Apply call applied() with the page values in the struct.
struct FolderOptions {
    bool longhorn = false;             // the Longhorn frame: icon size and the thumbnails toggle
    int default_view = 0;              // in: the default now; out: as chosen
    int builtin_default = 0;           // Restore Defaults
    int current_view = 0;              // the folder's (Apply to All Folders)
    std::vector<int> zoom_levels;      // the Icons view's sizes (0 = the default size first)
    bool remember = true, hidden = false;
    void (*apply_all)(FolderOptions&) = nullptr;   // the current view is the default, every folder's own forgotten
    void (*reset_all)(FolderOptions&) = nullptr;   // the built-in default again, every folder's own forgotten
    void (*applied)(FolderOptions&) = nullptr;     // OK / Apply
};
void folder_options(HWND owner, FolderOptions& o);

} // namespace dialogs
