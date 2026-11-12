// shell.h: the one include for lhde's shell facilities (docs/APPLICATIONS.md), what an application shares with the
// Explorer without borrowing its internals. C++, namespace shell; linked from build/lib/libflxpshell.a (apps/app.mk's
// WUX_APP_LINK). Include it after <wux/wux.h>. Every header named here is public; the rest of flxp/ is not.
//
//   util.h        trim, lower, iequal, exists, is_dir, is_file, read_file, program_name (the warnings' prefix)
//   config.h      XDG directories (config_home, data_home, user_dir("PICTURES", "Pictures")), flxp.ini settings, URIs,
//                 shell quoting, dir_of / base_of, write_file_atomic, make_dirs
//   settings.h    lhde's settings store, settings.ini (shell::store::get / set, the _LH_SETTINGS_CHANGED notification)
//   lhtheme.h     the session theme's record (shell::session_theme(), theme_setting): what the theme says beyond the
//                 visual style, which wux already follows
//   lhdata.h      liblhde's data directories: the user's imported Windows files by relative name
//   namespace.h   the shell namespace (folder_for, Item, Folder::enumerate), type names, size and date formats
//   mime.h        MIME types by name and content (shared-mime-info)
//   apps.h        launching: open_files (a double click), spawn (a program, detached), the .desktop applications,
//                 the terminal
//   details.h     the property registry and the annotations: details::property("description" | "tags" | "comment"),
//                 details::write (as the Explorer writes user.lux.desc / user.xdg.tags / user.xdg.comment),
//                 read_stored, read_attr, list_attrs (every user.* name on a file), add_tags, infotip; the
//                 Properties formats (size_and_bytes, long_date, thousands)
//   fileops.h     Trash, rename, new folder; fileops_job.h the copy/move/delete engine
//   dirwatch.h    a directory followed with inotify
//   thumbs.h      freedesktop thumbnails (made and cached; gdi::Bitmap results)
//   icons.h       the icon theme: which file or resource an icon key names
//   bookmarks.h   the GTK bookmarks
//   xclip.h       files and text on the X CLIPBOARD (the clipboard API in wux.h goes through it)
//   ui.h          the win32 part: message_box (MessageBoxW with UTF-8 strings), load_image / icon_for_key / icon_for
//                 (an icon theme's source or a file's icon as an HICON), properties (the Properties sheet: General,
//                 Summary)
//   filedlg.h     the Open/Save dialog itself (filedlg::run). GetOpenFileNameW / GetSaveFileNameW reach the same dialog;
//                 either needs $(WUX_APP_FILEDIALOG) on the link line (apps/app.mk)
#pragma once
#include "shell/util.h"
#include "shell/config.h"
#include "shell/settings.h"
#include "shell/lhtheme.h"
#include "shell/lhdata.h"
#include "shell/namespace.h"
#include "shell/mime.h"
#include "shell/apps.h"
#include "shell/details.h"
#include "shell/fileops.h"
#include "shell/fileops_job.h"
#include "shell/dirwatch.h"
#include "shell/thumbs.h"
#include "shell/icons.h"
#include "shell/bookmarks.h"
#include "shell/xclip.h"
#include "shell/ui.h"
#include "filedlg/filedlg.h"
