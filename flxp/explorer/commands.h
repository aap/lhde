// Command and control ids of the explorer frame, shared with the desktop
// (which uses the verb and view ids of the context menus).
#pragma once

// the `view` the menus get for the Thumbnails view (an LVS_ICON list; the
// other views pass their LVS_* type)
enum { VIEW_THUMBNAILS = 0x100 };
// with another view: the Longhorn hosts' Thumbnails toggle is on (it shows in
// the Icons view; browser::view_code, the views file's "details+thumbnails")
enum { VIEW_THUMBS_FLAG = 0x200 };
// the Longhorn hosts' Tiles view (an LVS_ICON list in LV_VIEW_TILE) and Hide File
// Names in their Icons view (the views file's "tiles@48", "icons@96+nonames")
enum { VIEW_TILES = 0x400, VIEW_NONAMES_FLAG = 0x800 };
// view codes may carry the zoom (icon px) above ZOOM_SHIFT (browser::view_code)
enum { ZOOM_SHIFT = 16, ZOOM_VIEW = 0xffff };

enum {
    ID_BACK = 100, ID_FORWARD, ID_UP, ID_SEARCH, ID_FOLDERS, ID_VIEWS,
    ID_FILTER, ID_DETAILS_TOGGLE, ID_FILTER_CLEAR,   // the Longhorn 4051 bar (ID_FILTER: the zoom's magnifier)
    ID_STOP,                                         // its red X: stop loading (thumbnails)
    ID_FILE_CLOSE = 200, ID_EDIT_SELECTALL, ID_EDIT_INVERT,
    ID_VIEW_STATUSBAR = 300, ID_VIEW_FOLDERS, ID_VIEW_ICONS, ID_VIEW_LIST, ID_VIEW_DETAILS,
    ID_SORT_NAME, ID_SORT_SIZE, ID_SORT_TYPE, ID_SORT_DATE, ID_VIEW_HIDDEN, ID_VIEW_REFRESH,
    ID_VIEW_THUMBNAILS,               // the explorer's Thumbnails view (not on the desktop)
    ID_VIEW_TASKPANE = 320,           // Longhorn: the task band under the command bar (taskpane.h)
    ID_VIEW_SMALLICONS,               // the explorer's View menu only (Ctrl+5; 2000's Small Icons)
    ID_VIEW_PLACES_ICONS,             // View > Explorer Bar > Icons in the Pane (places_icons, the groups look)
    ID_VIEW_PREVIEW,                  // 4051's View > Preview: the task band expanded (else compact)
    ID_VIEW_DEFAULT,                  // Ctrl+0: the default view (the folder's own forgotten)
    ID_VIEW_BIGICONS,                 // Ctrl+3: big icons with thumbnails (XP frame: the Thumbnails view)
    ID_VIEW_TILES,                    // the Longhorn views (browser::ViewCommand): Tiles, Hide File Names,
    ID_VIEW_HIDENAMES,                // the Large and Medium Icons stops (ID_VIEW_SMALLICONS: Small)
    ID_VIEW_LARGEICONS,
    ID_VIEW_MEDIUMICONS,
    ID_VIEW_HEADER_FIRST = 330,       // View > Header: header::MENU_IDS ids (sizes, elements)
    ID_VIEW_ZOOM_FIRST = 340,         // View > Zoom: browser::ZOOM_MENU_IDS ids (in, out, the levels)
    // (ID_VIEW_ZOOM_FIRST .. +12)
    ID_HELP_ABOUT = 400, ID_TB_LARGE = 410, ID_TB_SMALL, ID_TB_ADDRESS,
    ID_TOOLS_FOLDEROPTIONS = 420,     // Tools > Folder Options (dialogs::folder_options)
    // item and background verbs (context menus and the File/Edit menus)
    ID_OPEN = 500, ID_EXPLORE, ID_OPENWITH_CHOOSE, ID_TERMINAL, ID_ADD_BOOKMARK, ID_CUT, ID_COPY, ID_PASTE,
    ID_DELETE, ID_RENAME, ID_PROPERTIES, ID_NEW_FOLDER, ID_NEW_TEXT, ID_EXPAND,
    ID_COPY_PATH,                     // Copy as Path: the paths as text on the CLIPBOARD (Vista's Shift+right-click verb)
    ID_WALL_STRETCH = 520, ID_WALL_CENTER, ID_WALL_TILE, ID_WALL_FILL,   // Set as Desktop Background
    ID_BM_ADD = 600, ID_BM_ORGANIZE, ID_BM_FIRST = 700, ID_BM_LAST = 799,
    ID_OPENWITH_FIRST = 800, ID_OPENWITH_LAST = 899,
    // the desktop's own (Arrange Icons By, Properties)
    ID_DESK_AUTOARRANGE = 900, ID_DESK_ALIGN, ID_DESK_PROPERTIES, ID_DESK_BACKGROUND, ID_DESK_PASTELINK,
    IDC_REBAR = 1000, IDC_TOOLBAR, IDC_ADDRESS, IDC_TREE, IDC_LIST, IDC_STATUS, IDC_MENUBAND, IDC_BRAND, IDC_BANDSITE, IDC_BANDCLOSE,
    IDC_HEADER, IDC_SEARCH, IDC_LHTOOLBAR, IDC_PLACES,
};
