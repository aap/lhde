// The file Open/Save dialog in two looks, chosen like the explorer's frame
// (filedlg= in flxp.ini, else the session theme's Explorer.chrome, luxwm):
// - XP: comdlg32's "Explorer-style" GetOpenFileName / GetSaveFileName
//   (filenew.cpp) re-created on this toolkit against the src/shell namespace.
//   Places bar, Look in combo, Back/Up/New Folder/Views toolbar, the file
//   list, File name (with auto-append completion) and Files of type.
// - Longhorn: the pre-reset Longhorn Save File concept and the 4074 Open
//   dialog on the explorer's browsing widget (src/explorer/browser.h): the
//   header (Back/Forward, breadcrumb, search) on luxwm's glass or the theme's
//   navigation bar, the 4074 command bar, the collapsible groups of places,
//   the list grouped by type, a details band for Save, Hide Explorer.
// Used by the shim's GetOpenFileNameW (src/w32/comdlg.cpp), the
// flxp-filedialog CLI and the xdg-desktop-portal backend.
//
// The caller must have initialised the shim (w32::init()). run() is modal and
// re-entrant per thread like DialogBox.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace filedlg {

enum class Mode { Open, Save, Folder };

struct Filter {
    std::string name;                      // "Images"
    std::vector<std::string> patterns;     // globs, matched case-insensitively ("*.png"); "*" / "*.*" = all
    std::vector<std::string> mime_types;   // "image/png", "image/*" (by name, shared-mime-info globs)
};

struct Options {
    Mode mode = Mode::Open;
    std::string title;                // "" = "Open" / "Save As" / "Select Folder"
    std::string accept_label;         // "" = "&Open" / "&Save" / "&Select"
    std::string initial_dir;          // "" = the directory of default_name, else $HOME
    std::string default_name;         // the File name box; may carry a directory ("/tmp/a.txt")
    std::vector<Filter> filters;      // empty = all files
    int filter_index = 0;             // initially selected filter
    bool multiple = false;            // Open: several files
    std::string default_ext;          // Save: appended ("txt") when the typed name has no extension
    bool overwrite_prompt = true;     // Save: "x already exists. Do you want to replace it?"
    bool file_must_exist = true;      // Open
    bool path_must_exist = true;
    bool create_prompt = false;       // Open: offer to create a missing file (OFN_CREATEPROMPT)
    bool show_read_only = false;      // "Open as read-only" box
    bool read_only = false;           // its initial state
    bool show_hidden = false;         // dot files (Ctrl+H toggles)
    bool no_change_dir = true;        // the process cwd is never changed (OFN_NOCHANGEDIR is implied)
    uint64_t parent_window = 0;       // native (X11) id of a foreign owner window: transient-for + centred over it
    bool modal = true;                // _NET_WM_STATE_MODAL towards that owner
    std::string app_location;         // the calling application's folder ("" = none): the Longhorn look's "App Location" place
    enum class Style { Auto, XP, Longhorn };
    Style style = Style::Auto;        // Auto: filedlg= (flxp.ini / FLXP_FILEDLG), else the explorer's frame style
};

// The look run() would use for o (Auto resolved as described above).
bool longhorn_look(const Options& o);

struct Result {
    bool ok = false;                  // false = cancelled
    std::vector<std::string> paths;   // absolute paths
    int filter_index = -1;            // the filter in effect when accepted (-1: none / a typed pattern)
    bool read_only = false;
    std::string folder;               // the folder shown last (also on cancel)
};

// owner: an HWND of this process (or null). Blocks until the dialog closes.
Result run(void* owner, const Options& o);

// A message box in the XP layout (the shim's MessageBoxW is a stub).
// type: MB_OK / MB_YESNO | MB_ICONEXCLAMATION / MB_ICONINFORMATION / ...; returns IDOK/IDYES/IDNO.
int message_box(void* owner, const std::string& title, const std::string& text, unsigned type);

// Glob matching as the dialog does it (case-insensitive, "*.*" matches names without a dot).
bool matches(const Filter& f, const std::string& file_name);

} // namespace filedlg
