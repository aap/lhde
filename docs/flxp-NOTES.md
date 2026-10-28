> **flxp's NOTES.md, imported into lhde unchanged** (2026-10-28, from flxp
> e22c298; to be consolidated into docs/ARCHITECTURE etc. later). Read its paths
> through the new layout: `src/{gdi,sys,win,res,w32}` and `vendor/wine` are now
> `wux/...` (`wux/vendor/wine`), `src/uxtheme` is `wuxtheme/` (libwuxtheme,
> `<wuxtheme/ut.h>`), `src/{shell,explorer,desktop,taskbar,filedlg,settings}`
> and `res/` are `flxp/...`, `rc/` is split into `wux/rc` (Wine's comctl32 and
> comdlg32 resources) and `flxp/rc` (flxp's own dialogs), `src/test`,
> `src/w32test` and `src/test/headless` are `tests/{toolkit,w32,headless}`.
> Programs build into `build/bin` (`build/explorer/explorer` is `build/bin/flxp`),
> libraries into `build/lib`, tests into `build/tests`; the data directory is
> `$PREFIX/share/lhde` (share/flxp is still read as a fallback); LHNAV_DIR is
> LUXNAV_DIR, empty by default. The `doc/*.png` images and `screenshots/` it
> refers to were not imported (Microsoft art, see docs/LAYOUT.md "Asset
> policy"): they stay in the flxp archive (~/src/flxp). The research notes
> `doc/concepts.md`, `doc/4051-explorer-research.md`, `doc/demo-resources.md`
> moved to the research workspace (~/fun/longhorn/research-from-flxp/).
> nt5src is no longer read (2026-10-28): the XP/2000 art comes by resource id
> from lhde-import's resources/xp and resources/2k ("XP and 2000 art by resource
> id" at the end); the icon recipes are data/icons/*.theme. Older sections that
> name nt5src files describe that earlier state; nt5src stays a reference for humans.

# flxp

A unix-native file explorer in the Windows 2000–7 mould, on a custom toolkit
that feels like win32 without being win32. Target look: XP/2003 classic and
visual styles (uxtheme/msstyles), with Longhorn themes as the fun goal.

## Running and installing

    make explorer && build/explorer/explorer [DIR]     uninstalled, from anywhere
    make install [PREFIX=/usr/local]                   bin/flxp, bin/flxp-* helpers,
                                                       share/flxp/res, share/applications/flxp.desktop
    (make install-icons is gone: the xp/2k art is read from resources/xp, resources/2k)
    make uninstall [PREFIX=...]
    make desktop && build/desktop/flxp-desktop         the desktop (wallpaper + icons), see "The desktop"
    make taskbar && build/taskbar/flxp-taskbar         the taskbar, see "The taskbar"

`flxp [--theme NAME|PATH] [--classic] [--icons NAME] [--small-toolbar]
[--frame xp|longhorn|auto] [--longhorn] [--geometry WxH+X+Y] [--info] [DIR|FILE...]`: folders open in a window
(default the current directory, not Windows' Documents; further folders in new processes), files open with their
default application (src/shell/apps), so `flxp foo.png` works. `--info`
prints what was found (settings file, data dir, theme, icon theme, fonts).
Options are applied as FLXP_<KEY> for the run, so they beat the ini.

Settings: `~/.config/flxp/flxp.ini` (shell::setting; written as a commented
template on first run; empty = automatic; env FLXP_<KEY> overrides):
`font_path` (extra font dirs), `icons` (xp|2k|longhorn|wine|path), `nt5src`,
`longhorn_system32`, `theme` (luna|name|.msstyles|classic), `theme_color`,
`theme_size`, `theme_path`, `theme_follow`, `toolbar_icons` (large|small), `geometry`,
`terminal`, `frame` (xp|longhorn|auto, see "The Longhorn header"),
`thumbnail_size` (see "Thumbnails view"), `places_pane` (on|off, see "The
places pane"), `folders` (0|1, the Folders tree at start, remembered),
`font_smoothing` (off|standard|cleartype, see "Font smoothing"), and the
desktop's `wallpaper`, `wallpaper_mode` (`desktop_fade_ms` deprecated, ignored). Test-only env hooks stay env-only: FLXP_DEBUG, FLXP_BACK,
FLXP_ADDRESS_TEXT, FLXP_INACTIVE; `--quit-after MS` (or a numeric second
argument) closes the window.

Auto-detection (src/shell/runtime.cpp, run before w32::init):
- Fonts: font_path dirs go first (gdi::add_font_dir). Tahoma, Microsoft
  Sans Serif, Marlett (Trebuchet MS, Verdana) that neither font_path nor
  fontconfig has are taken from Wine's font dirs ($WINEPREFIX or
  ~/.wine/drive_c/windows/Fonts, /usr/share/wine/fonts, ...: Wine ships
  tahoma/marlett lookalikes); anything still missing is fontconfig's
  substitute (DejaVu Sans here), with one warning if Tahoma is missing.
  Real fonts are not installed by us (licensing): put tahoma.ttf,
  tahomabd.ttf, micross.ttf, marlett.ttf (and trebuc*.ttf, verdana*.ttf for
  Luna) from a Windows install in ~/.fonts, or point font_path at its Fonts.
- Icons: default xp; a theme whose folder/document icons are missing falls
  back to wine with one warning; missing toolbar strips are dropped
  (comctl32's images). In .theme files $FLXP_<KEY> expands to the setting,
  and a relative base is relative to the .theme file.
- Theme: default luna if <dir>/Luna/luna.msstyles exists in theme_path,
  ~/.local/share/flxp/themes, <data>/themes, ~/.wine/.../resources/themes,
  ~/.themes; else classic. The resolved path is exported as FLXP_THEME
  (+_COLOR/_SIZE) for the shim's init_theme and child processes.

Data: shell::data_path(rel) searches $FLXP_DATA, the compiled FLXP_DATADIR
(Makefile DATADIR = $(PREFIX)/share/flxp, kept in build/shell/datadir so
config.o rebuilds when it changes), <exe>/../share/flxp, <exe>/../.. and
<exe>/.. (source tree). Only res/icons is read at run time; the rc/*.res
dialog templates and stdres.res are .incbin'd into the binary.

## Decisions

- Light C++: namespaces instead of the flat win32 name list, stdint types,
  no exceptions/RTTI, otherwise C-ish. `std::string`/`std::vector` allowed
  where they remove drudgery.
- Namespaces: `gdi` (surfaces, DCs, regions, later fonts/text), `sys`
  (platform: X11 now, Wayland/Plan 9 later), `win` (window tree, messages,
  painting, input), later `theme` (uxtheme port) and `ctl` (controls).
- Win32 model kept on purpose: a tree of windows with integer rects, one
  wndproc per window, WM_PAINT driven by invalidation, capture/focus/timers.
  Message numbers and style bits use the win32 values so code seeded from
  Wine keeps working.
- One native (X11) window per toplevel; children are virtual and painted
  into the toplevel's 32bpp backbuffer. Frames are the window manager's job
  for now; client-side (themed) frames later.
- Controls and the theme engine are seeded from Wine (LGPL): comctl32,
  user32 controls, uxtheme. They stay C and compile against a small
  `extern "C"` compat header that maps the win32 names they use onto the
  namespaced API; new code uses the namespaced API directly.
- Theme files: read real .msstyles (PE resources) directly, so XP and
  Longhorn themes load unmodified; also an unpacked ini+bmp directory form.
- Text: FreeType, configured for GDI-identical hinting (Wine's freetype.c is
  the reference), 96 DPI integer world.

## Layout

    src/gdi      geom color region bitmap dc font (FreeType, GDI-faithful) bmp png
    src/sys      sys.h x11.cpp                  platform backend
    src/win      msg.h window.h window.cpp      window tree, messages, painting, input
    src/res      res.h res.cpp                  PE / .res resource reader
    src/w32      the win32 shim: gdi*.cpp image.cpp syscolor.cpp kernel.cpp crt.cpp
                 debug.cpp usp10.cpp imm.cpp user.cpp defwnd.cpp sysparams.cpp theme.cpp
                 dwmapi.cpp + flxp_dwmapi.h (lhwm glass frames, see below)
                 flxpshell.cpp + flxp_shell.h (DESKTOP-type windows, root properties)
                 comdlg.cpp GetOpenFileName/GetSaveFileName on src/filedlg
    src/filedlg  the XP Open/Save dialog (library), flxp-filedialog (CLI),
                 flxp-portal (xdg-desktop-portal FileChooser backend); see "File dialog"
                 include/   stub private headers the vendored Wine code expects
                 stdres.res Wine's OEM bitmaps, standard icons and cursors
    vendor/wine  Wine sources (see MANIFEST for tags; <file>.patch is the diff
                 against the pristine tag, every edit commented "flxp:"):
                 comctl32 v6 controls (10.0), user32 scroll/uitools/text/menu
                 (7.0), dialog/defdlg (6.0), uxtheme (master 840a939ee52)
    rc/          build.sh compiles Wine .rc files with mingw windres (strips #msgctxt#)
    src/test     native toolkit tests (build/<name>)
    src/w32test  shim-level tests (build/w32test/<name>); link with unresolved
                 symbols ignored so partial work can be exercised
    src/explorer explorer.cpp (the frame), header (Longhorn header), menuband,
                 dialogs; shared with the desktop: verbs (context menus and item
                 operations behind a Host), uiicons (theme icons as image lists),
                 commands.h (command ids)
    src/desktop  flxp-desktop: desktop.cpp, wallpaper (BMP/PNG/JPEG, layout modes)
    src/taskbar  flxp-taskbar: taskbar.cpp (bar, start button, tray, clock), taskband.cpp,
                 startmenu.cpp (menu, Run, session commands), xwm.cpp (X thread: EWMH, struts, XEmbed tray)
    src/uxtheme  libuxtheme: ut.h (C API), ut.cpp, v4.{h,cpp} (Longhorn-compiled
                 reader), nowin.cpp (.so only), ut-dump.cpp; see "libuxtheme"
    doc/         milestone screenshots

## Building and running

    make            native tests
    make vendor     vendored C + shim objects
    make w32test    shim tests; e.g. build/w32test/controls [quit_ms]
                    build/w32test/glassframe [quit_ms] (glass header demo, needs lhwm)
                    build/w32test/dialog rc/comdlg32.res CHOOSE_FONT [quit_ms]
                    build/w32test/dlgkeys [quit_ms]: z-order, AdjustWindowRectEx and
                    client size checks at start; then e.g. xdotool key Tab / alt+c /
                    alt+a / alt+n, xdotool windowactivate <owner>: focus, active
                    window, check and apply events are logged to stderr
    Environment: FLXP_FONT_PATH=dir:dir (extra font dirs, e.g. the 4074 Fonts dir
    for real Tahoma), FLXP_THEME=/path/to/file.msstyles [FLXP_THEME_COLOR, _SIZE],
    WINEDEBUG=+listbox,+combo,... (Wine's debug channels work in the vendored code).
    Headless testing: Xvfb :99 + xdotool + import, see the scratch scripts.
    Explorer: see "Running and installing" (flxp.ini keys, options).
    FLXP_DEBUG=1 (band/button geometry on stderr). Test hooks for
    screenshot comparisons: FLXP_BACK=<dir> (visit first, so Back is enabled),
    FLXP_ADDRESS_TEXT=<text> (address shown), FLXP_INACTIVE=1 (draw as inactive).
    terminal= (Open Terminal Here; default urxvt; %d = the quoted directory, the
    command runs with cwd = that directory). build/assoctest exercises the association model
    from the command line (mime, exec, open, setdefault, terminal, trash, bookmarks).

## How the shim works

Vendored Wine C is compiled against Wine's public headers (-fshort-wchar, so
WCHAR is 16-bit) and links against src/w32, which implements the ~400 win32
functions those files use on top of gdi/win/sys. HWND is a win::Window*, HDC
wraps a gdi::DC with GDI state, HINSTANCE is a res::Module*. Wine-private
hooks (WIN_GetPtr, user_api, DIALOGINFO) are provided by src/w32/include.
Everything else (handles, registry, atoms, clipboard, TEB for the inline
GetLastError) is process-local and in memory.

## Icon themes

(Superseded 2026-10-28: the recipes are lhde's data/icons/*.theme, read by
resource id from resources/<build>; see "XP and 2000 art by resource id".)
`FLXP_ICONS=wine|xp|2k|longhorn` selects a theme file in res/icons/*.theme
(demo.sh defaults to xp). A theme maps icon keys to .ico files or to icon
resources in a PE (`pe:shell32.dll#4`), gives a preferred colour depth (4 =
the 16-colour frames, i.e. the Windows 2000 look) and a toolbar strip with
button indices. The real Microsoft files are not in the repo: xp and 2k read
the XP SP1 source tree (`FLXP_NT5SRC`), longhorn reads the 4074 system32
(`FLXP_LONGHORN_SYSTEM32`). Toolbar icon size: View > Toolbars > Large/Small
Icons, or `FLXP_TOOLBAR_ICONS=small`; themes carry both strips. 32-bit strips
keep their alpha, 8-bit ones are colour-keyed on the top-left pixel.
doc/icons-*.png, doc/toolbars-xp-2k.png, doc/toolbar-sizes-xp.png.

## Status (2026-10-23)

- Milestone 1 done: Wine's comdlg32 dialog templates (Font, Colour, Open)
  run modally with the classic look. doc/milestone1-classic-dialogs.png
- Milestone 2 done: the same under the Longhorn 4074 Slate XP backport
  (.msstyles, packthem v3) through uxtheme. doc/milestone2-slate-theme.png
- All of comctl32 (wine-10.0) is vendored and links; COM headers are generated
  with widl built out of tree in ~/othersrc/wine-build (make -C tools/widl).
- Menus (wine-7.0 user32/menu.c) work: bar in the frame area, popups, submenus,
  keyboard (Alt/F10), context menus. doc/menus.png
- src/shell: the explorer's namespace model (filesystem + virtual Desktop and
  My Computer, natural sort, type names, Explorer-style size/date formatting).
- src/w32test/comctl.cpp: a complete common-controls demo (explorer-like window
  plus a tab page with progress, trackbar, up-down, date picker, month calendar,
  hotkey, IP address). doc/comctl-*.png
- src/explorer/menuband: XP's menu is a toolbar band in the rebar (browseui's
  CMenuBand); ours tracks popups under the buttons, switches menus on hover
  and arrow keys through a WH_MSGFILTER hook (the shim now supports those),
  Alt/F10/mnemonics. The system menu bar is not used by the explorer.
- Toolbar metrics follow XP's comctl32 (nt5src shell/comctl32/v6/toolbar.cpp)
  and browseui/itbar.cpp: mixed buttons autosize (vendor patch), 7x6 padding,
  list gap 4, the 30px toolbar centred in a 38px band. doc/toolbar-vs-xp.png
- Themes can be named: FLXP_THEME=luna looks in ~/.wine/.../resources/themes
  and the backport dir (FLXP_THEME_PATH adds dirs). doc/explorer-luna.png
- src/explorer: the explorer frame (rebar with standard toolbar and address
  band, Folders tree, file list in details/list/icons, status bar, menus,
  history with Back/Forward dropdowns, splitter).
  `./demo.sh explorer` / `./demo.sh texplorer`. doc/explorer-*.png
- Not yet: WM-independent frames (the frame stays with fvwm for now), real X
  cursors beyond the standard shapes, packthem v4 (not needed: XP-era styles
  are the target), drag-drop, real copy progress/undo, the X clipboard for
  Cut/Copy, My Computer drive details, the XP task pane.
- File associations, item/background context menus, Open With, Bookmarks,
  Trash, rename, new folder: see "Associations, context menus, bookmarks".
- win32 dialog behaviour (src/w32test/dlgkeys checks all of it, driven with
  xdotool; the log says ok/WRONG):
  - Child z-order: win::create links a new child at the bottom, as win32 does,
    so GW_CHILD/GW_HWNDNEXT run in creation order and Tab order and mnemonic
    search follow the dialog template. Sibling clipping follows WS_CLIPSIBLINGS
    (win::visible_region): without it a child draws over the siblings above
    it; children paint top first, so the controls inside a group box (created
    before them, so above them) paint over it. A window that must stay above
    later siblings is raised, as propsheet.c does with its pages
    (w32test/comctl does the same for its tab pages).
  - Alt+key keeps the key's text, so TranslateMessage posts WM_SYSCHAR:
    IsDialogMessage's mnemonics (DIALOG_IsAccelerator) work, and DefWindowProc
    sends a child's WM_SYSCHAR on as SC_KEYMENU to the toplevel (as it does for
    Alt alone), so Alt+F opens the explorer's menu band directly.
  - AdjustWindowRectEx adds nothing for caption and frame of a WM-framed style
    (overlapped, or a popup with WS_CAPTION or WS_THICKFRAME): the WM draws them
    outside our window. The menu bar and WS_EX_CLIENTEDGE still count;
    children and other popups keep the win32 sizes (nc_calc_size uses
    w32::adjust_frame_rect). Dialogs now get exactly the template's client size.
  - FocusIn for a disabled toplevel activates its enabled owned WM-framed
    window instead (the modal dialog, also while the dialog manager has not
    shown it yet) and asks the WM to follow (sys::activate_window:
    _NET_ACTIVE_WINDOW + XSetInputFocus when viewable); the owner gets no
    WM_SETFOCUS. Tested without a WM and under fluxbox (windowactivate and
    clicking the owner).
  - Known: GetDlgItemTextA/SendMessageA(WM_GETTEXT) return UTF-16 (no A/W
    conversion). win::create treats WS_POPUP|WS_BORDER at creation as
    WM-decorated, AdjustWindowRectEx does not (tooltips add WS_BORDER only
    after creation, so they are unaffected).

## References

    ~/othersrc/wine            Wine (dlls/comctl32, dlls/user32, dlls/uxtheme,
                               dlls/win32u/freetype.c, dlls/aero.msstyles)
    ~/lib/nt5src               XP SP1 / 2003 source, read as a spec
                               (shell/themes/themedir/*.txt, tmschema.h)
    ~/fun/longhorn             real msstyles from 4051/4074 to test against

## Explorer frame vs XP: measurements and how they are met (2026-10-23)

Oracle: screenshots/{luna,class}_exp_{large,small}TB_folder.png (XP SP3), compared
pixel by pixel with ours rendered at the same client origin and size (client
792x562; Luna large at (38,54), Luna small (21,47), classic large (24,40),
classic small (17,33) - classic's caption is 18 rows plus a 1-row line, the
client starts after it). Rows 54.. below are Luna-large screen coordinates.

- Rebar: no border; rows menu 54..75 (22, the brand band sets it), etch 76/77,
  toolbar 78..115 (38), etch 116/117, address 118..139 (22 = combo height).
  Themed bands start after Rebar.Band ContentMargins (Luna 2,0,0,0); XP's
  _BandWidth adds left+right per band, rows add top+bottom (rebar.c). The
  between-band etch runs 1px into the row etch below. Theme background is drawn
  over the whole client first, then RP_BAND per band incl. margins.
- Band x offsets beyond the margin (measured, not in the SP1 source): toolbar
  band +4 under a theme (RBBIM_HEADERSIZE 4), menu band +1 in classic.
- Menu band: unthemed list toolbar, 1px bitmap slot -> buttons text+16 x 19,
  centred in the 22 row. Text 3DSHADOW when the window is inactive (mnbase.cpp).
- Brand: shell32 brand_sml.bmp frame 0 (22x22) centred on its top-left colour,
  band RBBS_FIXEDSIZE 38 wide; XP keeps it at the end of row 0.
- Toolbar (Luna): 24px glyph buttons 38 tall; Back 72, Forward 52, Up 32,
  separators 6 (browseui CX_SEPARATOR), Search 72, Folders 74, Views 45.
  Unexplained but reproduced (toolbar.c): a themed split button without text
  is 7px wider with its image 2px right of centre and its arrow 1px left; a
  themed split button with text is 1px wider. Whole-drop-down arrow: XP's
  Marlett glyph sits 2px left of the arrow area, 1px low; classic split
  arrows 2px right, 1px low of Wine's. Checked-button dither starts 0xAAAA.
  Disabled 32bpp images: XP's gray (54r+183g+19b)>>8 at constant alpha (157
  fits SP3 best; SP1 says 150); Wine multiplied by alpha twice.
- Address: label "A&ddress" in the Rebar theme font/TextColor (unthemed: caption
  font, normal weight), text at band x+4, top = centre - h/2 - 1 (XP _DrawBand).
  ComboBoxEx: image at the item rect's left (+3 in the list), edit at
  2+1+icon+4; edit margins min(avecharwidth/2, max negative A/C) (XP edit).
  Themed combo button: full height inside BorderSize, SM_CXVSCROLL wide (Luna
  17; GetSystemMetrics now follows the theme's scroll bar size). Go button: one
  list button (20px shell32 tb_go_def_20, alpha corners as on SP3) in the
  subclassed ComboBoxEx at cx-cxGo, combo narrowed by cxGo+2 (address.cpp).
- uxtheme: nine-grid parts smaller than their margins split the margins in
  proportion and shrink the pieces (ninegrid2.cpp) - the toolbar separator line
  lands at +2 of 6; glyph/truesize centring is (dst-src)/2. DT_VCENTER and
  prefix underlines (full advance) as Windows in text.c.
- Folders bar (browbs.cpp): band site 220 wide with the Rebar background, band
  starts after the 2px margin (vertical: top), title "Folders" in the menu font
  at x 6, header 22 with an etch at its bottom, X (browtb.bmp cell 1, navy on
  SP3) at right-w-1, tree without client edge, splitter 4. List: no client
  edge, XP widths 180/96/120/120, HDF_SORTUP + selected column set.
- Residuals: glyph edge pixels of a few icons (1-2 levels); the small disabled
  Forward glyph is 1px lower on SP3 (asset difference); the X button is 4px
  further left in two of the four screenshots (Luna large, classic small) for
  no visible reason; list header (XP SP3: 20 tall, text +12, sorted column
  tint 247,247,247) not done yet.

## Associations, context menus, bookmarks (2026-10-23)

src/shell, no win32 (reusable by the tree, a Properties dialog, tools):
- mime.{h,cpp}: shared-mime-info. globs2 from $XDG_DATA_HOME/mime and every
  $XDG_DATA_DIRS/mime (merged; __NOGLOBS__ honoured), literal names first,
  then highest weight / longest pattern, case-sensitive matches beat
  case-insensitive ones (*.c vs *.C); aliases, subclasses (+ implicit text/plain,
  application/octet-stream parents), generic-icons -> our icon keys, the
  <type>.xml comment ("PNG image"). Sniffing is a short built-in signature list
  (PNG/JPEG/GIF/PDF/zip/gz/xz/ELF/#!/...) plus a text/binary check, used only
  when no glob matches; the full magic file is not parsed. Empty files are
  text/plain (opens in the editor rather than nowhere). namespace.cpp now falls
  back to the mime comment and icon for extensions its own table lacks.
- apps.{h,cpp}: .desktop files ([Desktop Entry] only: Name, Exec, Icon,
  Terminal, NoDisplay, Hidden, TryExec, Path, MimeType) from
  $XDG_DATA_HOME/applications then $XDG_DATA_DIRS/*/applications (first id
  wins; subdir ids use '-'). mimeapps.list in spec order ($XDG_CONFIG_HOME,
  $XDG_CONFIG_DIRS, $XDG_DATA_HOME/applications, $XDG_DATA_DIRS/applications,
  then legacy defaults.list): [Default Applications], [Added Associations],
  [Removed Associations]. default_app walks the type then its ancestors;
  apps_for lists default, added, MimeType= matches. set_default_app edits
  $XDG_CONFIG_HOME/mimeapps.list line by line (other entries and comments kept):
  sets the default, puts the app first in Added, drops it from Removed (as GLib).
  Exec: string escapes, then "..." quoting; %f/%u one process per file, %F/%U
  one for all (URIs for %u/%U), %i %c %k, deprecated codes dropped; no field
  code -> files appended. Programs start directly (fork, setsid, fork; fds
  closed; stdin /dev/null) in the file's directory (or Path=); exec failures come
  back through a CLOEXEC pipe. Terminal=true apps run as `<terminal> -e argv`.
  Executable ELF files (incl. PIE, detected by PT_INTERP) run themselves;
  scripts open with their associated app (the editor), not executed.
- fileops.{h,cpp}: Trash per the freedesktop spec ($XDG_DATA_HOME/Trash, or
  $topdir/.Trash/$uid / .Trash-$uid on other filesystems; info file reserved with
  O_EXCL, name.2.ext on collisions, relative Path= for topdir trashes); rename;
  "New Folder (2)" / "New Text Document.txt"; paste and drop (wrappers over
  fileops::run, see "File operations"); file_info.
- bookmarks.{h,cpp}: the GTK file chooser's $XDG_CONFIG_HOME/gtk-3.0/bookmarks
  (read fallback ~/.gtk-bookmarks): `file:///percent-encoded/path Label` lines,
  remote URIs kept as they are; written atomically (through a symlink if the file
  is one).
- config.{h,cpp}: XDG dirs, flxp.ini, URIs, shell quoting.

src/explorer:
- Item menu (list right-click, Shift+F10, Menu key; positioned at the click or
  the focused item): Open (default, bold), Explore (folders: navigate and show
  the Folders bar - one window, so Open also navigates), Open With > (default
  first, other associations, Choose Program...), Open Terminal Here (folder, or
  a file's directory), Add to Bookmarks (folders), Cut, Copy, Paste (folders),
  Delete, Rename, Properties. Multi-selection: Open opens all (grouped per
  default app, so %F apps get one process), Open With applies to the files of
  the first file's type. Virtual items (My Computer, drives list) get Open/Explore.
- Folders tree right-click: Expand/Collapse (default), Explore, Open, then the
  same file verbs; the item gets the drop highlight while the menu is up, like XP.
- Background menu: View >, Arrange Icons By >, Refresh, Paste, New > (Folder,
  Text Document; then label edit), Open Terminal Here, Add to Bookmarks,
  Properties. The File menu is the selection's (or the folder's) menu, as in XP.
- Keys: Enter opens, Delete trashes (confirm), F2 renames (LVS_EDITLABELS /
  TVS_EDITLABELS; the namespace roots, /, Home cannot be renamed), Alt+Enter
  Properties, Ctrl+X/C/V, Backspace up. Cut items are ghosted (LVIS_CUT).
  Cut/Copy use an internal clipboard (not the X CLIPBOARD /
  x-special/gnome-copied-files yet).
- Dialogs: rc/explorer.rc (windres -> rc/explorer.res, `make` rebuilds it,
  embedded with .incbin in dialogs.cpp, loaded with res::load_memory): Open With
  (XP layout: Recommended / Other Programs tree, "Always use the selected
  program..."), Confirm File Delete, a message box (MessageBoxW in the shim is
  still a stderr stub), Organize Bookmarks (move up/down, remove, rename via the
  Name field; saved on OK), Properties (a real PropertySheet with a General page:
  type + mime, opens with + Change..., location, size, folder contents, times,
  permissions/owner, link target).
- Bookmarks menu where XP has Favorites (Favorites is gone): Add to Bookmarks
  (current folder), Organize Bookmarks..., then the entries with folder icons
  (HBMMENU_CALLBACK + WM_MEASUREITEM/WM_DRAWITEM; every item gets a callback
  bitmap because Wine's menu.c does not widen plain items by the text offset).
- Status bar: one selected file shows "Type: X   Opens with: App".
- Flat menus: the shim now stores SPI_SETFLATMENU; uxtheme sets it from the
  theme, so Luna popups get the 1px flat frame and classic keeps 3D edges.
  doc/contextmenu-luna.png, doc/contextmenu-classic.png,
  doc/contextmenu-background-luna.png, doc/openwith-luna.png, doc/bookmarks-luna.png.
- Not yet: application icons (Icon= needs PNG/SVG decoding and icon-theme
  lookup; apps show the generic program icon), localized Name[xx], desktop-
  specific mimeapps lists, Send To / Create Shortcut, permanent Shift+Delete,
  restoring from Trash, a Trash folder in the namespace, bookmarks in the tree.
  Dialogs are placed by the window manager (DS_CENTER is not honoured under fvwm).

## Glass frames: lhwm's DwmExtendFrameIntoClientArea (2026-10-23)

The Longhorn 4074 tall glass header: the window manager's frame plate extends
into the client and the client paints that band transparent, drawing title,
icon and breadcrumbs on top. Protocol: ~/fun/longhorn/x11/lhwm/CLIENT-PROTOCOL.md
(reference client x11/examples/extendframe.c). Layers:

- sys (x11.cpp): toplevels can be ARGB (create_window(..., argb) or set_argb,
  which recreates the X window: 32-bit TrueColor visual from XMatchVisualInfo,
  own colormap, border/background pixel 0). present() then XPutImage's the
  backbuffer as a depth-32 ZPixmap of that visual (premultiplied BGRA is the
  visual's layout); opaque windows keep the old default-visual path untouched.
  set_frame_extension/set_frame_flags write _LHWM_EXTEND_FRAME (CARDINAL[4]
  l r t b, null deletes) and _LHWM_FRAME_FLAGS (deleted when 0) and re-put them
  when the X window is recreated. The root is watched (PropertyChangeMask):
  _LHWM_COMPOSITED changing value -> EventKind::CompositionChanged,
  _LHWM_THEME/_LHWM_FRAME_METRICS -> FrameThemeChanged; frame_info() caches
  them. begin_move_resize = XUngrabPointer + _NET_WM_MOVERESIZE (source 1),
  set_maximized/is_maximized = _NET_WM_STATE MAXIMIZED_VERT|HORZ. Toplevels
  now carry WM_CLASS <program>, "Flxp" (for picom/WM rules).
- gdi: Bitmap::has_alpha marks an alpha-tracking (premultiplied) surface; the
  ARGB toplevel's backbuffer has it. On such surfaces blit_alpha (icons,
  DrawIconEx) and blend_mask (all text) do src-over that also accumulates
  destination alpha; opaque surfaces keep the old code paths bit for bit.
  Solid fills, BitBlt, gradients etc. are unchanged: they write alpha 255 (or
  copy the source's alpha), i.e. opaque on glass - unlike Windows, where GDI
  writes alpha 0 and black text turns transparent. AlphaBlend was already a
  premultiplied src-over with destination alpha. clear_rect = transparent black.
  glow_mask/glow_text: Longhorn TextGlow - the coverage mask dilated and box-
  blurred twice (radius about 3 px), gain 2, composited in the glow colour
  under where the text goes. FontDesc::quality (from lfQuality: ANTIALIASED/
  CLEARTYPE* = grey, NONANTIALIASED = mono, else the global setting).
- win: set_argb/is_argb, set_frame_extension/set_frame_flags/frame_flags,
  composited() (= 32-bit visual && _LHWM_COMPOSITED), frame_metrics(),
  begin_move_resize(w, HT*) (HTCAPTION move, HTLEFT..HTBOTTOMRIGHT/HTSIZE
  resize directions, MOVE_KEYBOARD/SIZE_KEYBOARD), set_maximized. The root
  events are posted to every toplevel as WM_DWMCOMPOSITIONCHANGED (0x031E) and
  WM_DWMCOLORIZATIONCOLORCHANGED (0x0320). Caption behaviour: a hit-test answer
  of HTCAPTION inside the client already became WM_NCLBUTTONDOWN; nonclient
  double-clicks on HTCAPTION are now generated without CS_DBLCLKS (win32 does
  that for all NC areas; only the caption here, so the scrollbar paths do not
  change), within the usual 500 ms/4 px. def_proc and the shim's DefWindowProc
  follow Wine's NC_HandleNCLButtonDown/DblClk: WM_NCLBUTTONDOWN HTCAPTION ->
  WM_SYSCOMMAND SC_MOVE+HTCAPTION, HT sizing codes -> SC_SIZE+WMSZ_*, double-
  click -> SC_MAXIMIZE/SC_RESTORE (WS_MAXIMIZEBOX); NC_HandleSysCommand hands
  SC_MOVE/SC_SIZE (also keyboard, low bits 0) and SC_MAXIMIZE/SC_RESTORE to the
  WM for WM-framed toplevels. begin_move_resize clears the button's key state
  (the WM gets the release) and releases capture. Only WM-framed toplevels;
  self-framed windows can call win::begin_move_resize too once they exist.
- w32 dwmapi (vendor/wine/include/dwmapi.h, Wine 840a939ee52, in MANIFEST):
  DwmExtendFrameIntoClientArea (makes the toplevel ARGB - call it in
  WM_CREATE, before mapping; all-zero margins remove the extension, negative =
  sheet of glass; unlike Windows it also succeeds while not composited, since
  lhwm ignores the property until it composites again), DwmIsCompositionEnabled,
  DwmSetWindowAttribute (unknown attributes accepted and ignored),
  DwmGetWindowAttribute (NCRENDERING_ENABLED, EXTENDED_FRAME_BOUNDS = window
  rect, the flxp flags), DwmDefWindowProc (FALSE), DwmFlush, DwmEnableComposition,
  DwmEnableBlurBehindWindow (no-ops), DwmGetColorizationColor (E_NOTIMPL: lhwm
  publishes no tint). src/w32/flxp_dwmapi.h: DWMWA_FLXP_FRAME_FLAGS
  (0x4c480001) with FLXP_DWMFF_NO_TITLE/NO_ICON/NO_FOG, FlxpDwmGetFrameMetrics
  (_LHWM_FRAME_METRICS: caption height, borders, text/glow/inactive colours),
  FlxpDwmEnableAlpha, FlxpClearRect, FlxpDrawGlowTextW (DrawTextW with a glow:
  the text is laid out by DrawTextW into a scratch DIB, its coverage blurred
  onto the target, then drawn normally).
- Demo src/w32test/glassframe.cpp = extendframe.c in win32: 640x420, 60 px
  header, NO_TITLE, Tahoma 15pt title + 9pt breadcrumb with glow, Wine's
  mydocs.ico (FLXP_GLASS_ICON overrides), HTCAPTION in the header, HTRIGHT on
  the right 6 px, GradientFill Slate header when not composited, repaints on
  both DWM messages, Escape/q quits. Tested in the headless lab
  (`LAB=:94 ./lab.sh start`, aero theme, picom xrender): move, double-click
  maximise/restore, right-edge resize, runtime theme switch to
  slate4074-classic and back (header turns opaque and back to glass).
  doc/glassframe-lhwm.png (ours), doc/glassframe-extendframe.png (reference,
  same position), doc/glassframe-slate.png (lhwm non-composited),
  doc/glassframe-plain.png (no WM). The explorer renders pixel-identically
  before/after (Luna and classic, old explorer objects relinked).
- Explorer (done: see "The Longhorn header"): create the frame with DwmExtendFrameIntoClientArea (top =
  header height) + NO_TITLE in WM_CREATE; draw the header (folder title, icon,
  breadcrumb/address, search box) in its own band window above the rebar,
  clearing to transparent with FlxpClearRect and drawing with
  FlxpDrawGlowTextW/DrawIconEx while composited, Slate gradient otherwise;
  answer HTCAPTION for the empty parts of the band (children over glass must
  answer HTTRANSPARENT where they want caption behaviour), repaint on
  WM_DWMCOMPOSITIONCHANGED/WM_DWMCOLORIZATIONCOLORCHANGED; take colours from
  FlxpDwmGetFrameMetrics. Controls placed on glass must paint opaque
  backgrounds or use AlphaBlend/DrawIconEx for their content: plain GDI on glass
  is opaque, and the caret XOR ignores alpha.
- lhwm notes: double-clicking lhwm's own caption maximises but does not
  restore a maximised window (extendframe too; restore works from the client
  header via _NET_WM_STATE). Maximised, the plate's header/body dividing line
  stays inset by the normal border widths (8 px each side) instead of running
  edge to edge.

## File dialog: XP's Open/Save, the comdlg32 API, a CLI and a portal (2026-10-23)

src/filedlg/filedlg.{h,cpp} re-creates comdlg32's Explorer-style dialog (XP SP1
filenew.cpp, CFileOpenBrowser) on the toolkit against src/shell, without
shell32 COM. Template: rc/filedlg.rc = XP's NEWFILEOPENV2ORD from the public
SDK's fileopen.dlg (370x237 DLU, the layout apps copied for custom templates;
Wine's cdlg_xx template differs in details), compiled by `make rc/filedlg.res`
and embedded with .incbin. Control ids are comdlg32's (stc4/cmb2 Look in,
stc1 = toolbar slot, ctl1 places bar, lst1 view placeholder, stc3/cmb13 File
name with edt1 hidden, stc2/cmb1 Files of type, chx1, IDOK/IDCANCEL/pshHelp).

    filedlg::Options { mode Open|Save|Folder, title, accept_label ("_Open" or "&Open"),
                       initial_dir, default_name (may carry a directory), filters
                       {name, globs, mime types}, filter_index, multiple, default_ext,
                       overwrite_prompt, file_must_exist, path_must_exist, create_prompt,
                       show_read_only/read_only, show_hidden, parent_window (X id), modal }
    filedlg::Result run(void* owner_hwnd, const Options&)   -> ok, paths, filter_index, read_only, folder
    filedlg::message_box(owner, title, text, MB_*)          (the shim's MessageBoxW is a stub)

What it does, following XP:
- Places bar: My Recent Documents (~/.local/share/recently-used.xbel, newest
  first, existing files only; a virtual folder "::recent"), Desktop (the
  namespace root), Home (XP: My Documents), My Computer, then the GTK
  bookmarks (shell/bookmarks.h) that are local folders. XP's five items fill the
  bar; ours are sized the same way, further bookmarks appear when the dialog is
  made taller. A toolbar with SetWindowTheme "Placesbar" (Placesbar::Toolbar
  background and button parts under Luna; classic: COLOR_BTNSHADOW, white
  text, edges in highlight / 3D dark shadow as XP's TB_SETCOLORSCHEME). Items
  are custom drawn (32px icon, name word-wrapped in two lines: Wine's toolbar
  draws one text row) in buttons as wide as the bar (TB_SETBUTTONWIDTH).
- Look in: owner-drawn combo as PaintDriveLine/MeasureDriveItems (indent
  10px per level, "W" height + 3, open-folder icon for the current one):
  Desktop, its children (My Computer + drives, Home, ~/Desktop), the current
  folder's ancestry inserted under its deepest listed ancestor.
- Toolbar in stc1's rect: Back (HIST_BACK; history), Up One Level, Create New
  Folder (new folder + label edit), View Menu (BTNS_WHOLEDROPDOWN: Icons, List,
  Details); tooltips with XP's strings. Images: XP's comctl32 viewsm/histsm
  (icon theme keys filedlg_view / filedlg_hist in res/icons/xp.theme), else
  Wine's IDB_VIEW/HIST_SMALL_COLOR. The 2000 strips are RLE4 bitmaps our loader
  does not decode, so the 2k theme uses Wine's.
- View: list view over lst1 (List by default as XP; details with Name / Size /
  Type / Date Modified and header sorting; icons), folders first, the theme's
  file icons, hidden files ghosted when shown (Ctrl+H, the GTK convention), F2 /
  slow click rename, Delete to the Trash (confirmed), right-click: item menu
  (Select/Open, Rename, Delete) or background menu (View, Arrange Icons By,
  Refresh, Show Hidden Files, New Folder). Double-click opens a folder or
  accepts a file. Going up selects the folder you came from.
- File name: selection goes into the box (several files quoted, as XP);
  typed names are resolved against the folder (~ expanded): a folder
  navigates, a wildcard ("*.c", "src/*.h") filters the view, "a" "b" is a
  multi-selection, otherwise Open requires the file (File not found / Path
  does not exist messages with XP's wording, OFN_CREATEPROMPT offers creation),
  Save appends default_ext when there is no extension, refuses read-only files
  and asks "x already exists. Do you want to replace it?" (Yes/No). Save turns
  the button into Open while a folder is selected (XP). Completion is XP's
  auto-append (inline, the appended part selected; folders get a "/").
- Folder mode (not in XP's GetOpenFileName; Vista's FOS_PICKFOLDERS
  semantics): files hidden, "Folder name:", Select takes the typed or selected
  folder, or the current one when the box is empty.
- Keys: Enter accepts, Esc cancels, Backspace (in the view) and Alt+Up go up,
  Alt+Left back, F5 refresh, Ctrl+H hidden files, Ctrl+A select all (multi),
  F2, Delete, Alt+letter mnemonics of the template.
- Layout: XP's ReAdjustDialog (no Help and no read-only box: the bottom rows
  move down by the Help button's height + gap, the view grows); resizable
  (XP's OFN_ENABLESIZING behaviour: the view and places bar grow, the bottom rows
  and buttons follow), minimum = template size. Centred over the owner (a
  foreign X window for the portal), WM_TRANSIENT_FOR + _NET_WM_WINDOW_TYPE_DIALOG
  + _NET_WM_STATE_MODAL (sys::set_transient_for, new in src/sys).
  doc/filedlg-luna.png, doc/filedlg-classic.png.

src/w32/comdlg.cpp: GetOpenFileNameW/A, GetSaveFileNameW/A, CommDlgExtendedError.
lpstrFilter double-NUL lists (';'-separated globs), nFilterIndex (1-based; 0 =
lpstrCustomFilter), lpstrFile in/out with nFileOffset/nFileExtension and
FNERR_BUFFERTOOSMALL (required size in the first WORDs), lpstrFileTitle,
lpstrInitialDir, lpstrTitle, lpstrDefExt (+OFN_EXTENSIONDIFFERENT), Flags
ALLOWMULTISELECT (Explorer format "dir\0name\0name\0\0", names relative to
their common folder), FILEMUSTEXIST, PATHMUSTEXIST, OVERWRITEPROMPT,
CREATEPROMPT, HIDEREADONLY/READONLY, FORCESHOWHIDDEN, NOCHANGEDIR (without it
the cwd becomes the chosen folder, as Windows does). A versions convert through
UTF-8 (the shim's ANSI code page). Not supported: hooks (lpfnHook is never
called), custom templates, OFN_SHAREAWARE / NODEREFERENCELINKS. filedlg::run is
a weak reference, so programs that do not link $(FILEDLGLIB) get
CDERR_INITIALIZATION instead of a link error. build/w32test/filedlgtest
[open|save|multi|ansi] [dir] exercises it with an owner window.

flxp-filedialog (build/filedlg; `./demo.sh filedlg ...`): --open | --save |
--directory, --multiple, --title, --accept-label, --filename, --directory-start
(default: cwd), --filter 'Name|*.png *.jpg image/webp' (repeatable; tokens with
'/' are MIME types, matched by name through shared-mime-info including
subclasses, "image/*" works; the name ends at the last '|'), --current-filter
N, --default-ext, --no-confirm-overwrite, --show-hidden, --parent x11:HEX,
--no-modal, --null, --separator, --print-filter, --uri. Prints the paths one per
line; exit 0 chosen, 1 cancelled, 2 usage. Fonts/theme/icons come from flxp.ini
through shell/runtime (FLXP_THEME=classic etc. override). Test hooks:
FLXP_FILEDLG_VIEW=list|details|icons, FLXP_FILEDLG_QUIT_MS=<ms> (cancel after).

flxp-portal (build/filedlg; libdbus-1 - there is no libsystemd/sd-bus here):
org.freedesktop.impl.portal.FileChooser on org.freedesktop.impl.portal.desktop.flxp,
/org/freedesktop/portal/desktop. OpenFile (accept_label, modal, multiple,
directory, filters a(sa(us)), current_filter, current_folder ay), SaveFile
(+ current_name, current_file), SaveFiles (a folder chooser; uris = folder +
each name of `files`). Results: uris (file://, percent-encoded), current_filter
(the filter in effect, as sent), writable=false for OpenFile, an empty choices
a(ss) when choices were asked for (choices are not shown). response 0 / 1
cancelled / 2 error. Each request runs flxp-filedialog (next to flxp-portal,
else $FLXP_FILEDIALOG, else $PATH) as a child process; the handle is exported
as org.freedesktop.impl.portal.Request while it runs and Close kills the dialog
(SIGTERM) and answers 1. Requests run side by side. The service exits after 5
idle minutes (FLXP_PORTAL_PERSIST=1 keeps it; -v / FLXP_PORTAL_DEBUG=1 logs).
Tested on a private bus (dbus-run-session) with gdbus calls, and end to end
through /usr/libexec/xdg-desktop-portal 1.22.1 (XDG_DESKTOP_PORTAL_DIR pointing
at a test dir with flxp.portal and a portals.conf) with a GIO client calling
org.freedesktop.portal.FileChooser and receiving Request.Response.

Installing the portal (not done; your config is untouched):

    make filedlg && sudo make install install-portal      # PREFIX=/usr/local
      -> bin/flxp-filedialog, bin/flxp-portal (install copies build/*/flxp-*),
         /usr/share/xdg-desktop-portal/portals/flxp.portal (PORTALDIR=),
         $(PREFIX)/share/dbus-1/services/org.freedesktop.impl.portal.desktop.flxp.service
    # (the XP icons come from lhde-import's resources/xp; no install-icons any more)
    # ~/.config/xdg-desktop-portal/portals.conf (res/portal/portals.conf.example):
    #   [preferred]
    #   org.freedesktop.impl.portal.FileChooser=flxp      (was: lxqt)
    # then restart the frontend: pkill -f /usr/libexec/xdg-desktop-portal (D-Bus
    # starts it again on the next request).

Without installing: copy res/portal/flxp.portal to
/usr/share/xdg-desktop-portal/portals/ (that one file needs root: the frontend
only reads its own datadir or $XDG_DESKTOP_PORTAL_DIR) and put the service file
with Exec=/u/aap/src/flxp/build/filedlg/flxp-portal into
~/.local/share/dbus-1/services/. The activated service inherits the bus's
activation environment: DISPLAY (and XAUTHORITY) must be in it
(dbus-update-activation-environment DISPLAY XAUTHORITY in the X session
start-up, which most setups already do). Firefox needs
widget.use-xdg-desktop-portal.file-picker = 1 (already the case when it uses
the lxqt chooser).

Toolkit issues found on the way, since fixed in src/win and src/w32 (see
Status, "win32 dialog behaviour"); the dialog's workarounds are gone except
order_children, which still slots the view and the other controls created
after the template into the tab order.
- MessageBoxW is still a stderr stub (filedlg::message_box covers the dialog's
  needs). sys::set_window_rect does not update NativeWindow::rect until the
  ConfigureNotify arrives.

Missing compared with XP's dialog: Thumbnails and Tiles views, the full shell
context menus (Open With, Send To, Properties...), drag and drop, the File name
MRU list in the combo's drop-down and the auto-complete drop-down (completion
is inline only), My Network Places, hooks/templates for applications,
OFN_ENABLEINCLUDENOTIFY-style filtering, the portal's choices (extra combo/check
boxes) and the Help button. File types for MIME filters are matched by name
only (no content sniffing).

## The Longhorn header: frame=longhorn (2026-10-23)

`frame=` in flxp.ini (or `--frame`, `--longhorn`): `xp` is the old look
(the window manager's frame, nothing above the rebar), `longhorn` the 4074
Explorer header, `auto` (the default, also for an empty value) is longhorn
whenever lhwm runs at start-up (the root has `_LHWM_COMPOSITED` = 1,
`_LHWM_FRAME_METRICS` or `_LHWM_THEME`), glass or not, else xp. So under lhwm
the explorer always comes up with the header - on glass with a DWM theme and
a compositor, opaque in the colours of lhwm's caption with a classic/msstyles
theme (see "Opaque header") - and everywhere else (fvwm, no WM) it looks as
before; `frame=xp` keeps XP under lhwm too, `frame=longhorn` forces the header
(opaque Slate where there is no lhwm). The choice is made once: a later
composition or theme change only repaints (glass <-> opaque, live on
WM_DWMCOMPOSITIONCHANGED / WM_DWMCOLORIZATIONCOLORCHANGED), it does not add or
remove the header.

src/explorer/header.{h,cpp}, a child of the frame above the rebar:
- WM_CREATE: DwmExtendFrameIntoClientArea(top = header height) and
  DWMWA_FLXP_FRAME_FLAGS = NO_TITLE (lhwm draws no caption text).
- Height: 4074-dwm.png has 76 rows from the frame's top edge to the client
  contents (frame y=75, contents from y=151); lhwm's caption is outside the
  client, so the header is 76 - caption_h client rows (aero 27 -> 49,
  slate4074-classic 24 -> 52; FlxpDwmGetFrameMetrics, 27 without lhwm),
  recomputed on WM_DWMCOLORIZATIONCOLORCHANGED (re-extends and relayouts).
  The contents start at y=151 as in the reference.
- Contents, measured on the Documents window of 4074-dwm.png (client left
  x=254), all relative to the contents' top C and that left edge:
  - title: Segoe UI 13pt (h17, ANTIALIASED) when the font can be found (the
    4074 Fonts' seguibk.ttf via font_path), else Tahoma h17 (heavier). Ours:
    ink x 331..414 against 331..414, rows 102..113 against 100..111: the
    title would reach 2 rows into lhwm's caption (4074 draws the header from
    the frame's top edge), so its capitals start at the client's first row.
  - breadcrumb: Segoe UI 9pt (h12): first segment ink x 343, rows 128..136,
    exactly the reference's; "Documents" 59 px against 57 (4074 lays out with
    fractional Avalon metrics). Separator: a 4x7 triangle (50,50,50) with a
    1 px white outline (its 4-neighbour dilation, as the reference pixels),
    24 px after the text, the next segment 23 px after it. Segments are the
    folder's namespace chain without the Desktop root (4074 starts at
    Computer); leading ones collapse into "..." (goes to the last hidden one)
    when they do not fit; a click navigates (hover: see "The breadcrumb as 4074 builds it").
  - text colour navy (5,6,62) with a white TextGlow (FlxpDrawGlowTextW, 2 px)
    on glass, (0,2,92) without glow on Slate (lh4074-181415.png).
  - Back/Forward: 4074's "framelet" buttons (see below) at x 2; then, as in
    4074 (no folder icon), the title at x 75 and the breadcrumb at 88: ink x
    331 and 343 against 331 and 343 in 4074-dwm.png (2026-10-24).
  - right: a vertical divider (glass: a white line fading out at both ends;
    Slate: dark with a light line left of it) and a search box (EDIT, cue
    "Search", 160 px, 8 px from the right edge). 4074 has magnifier and
    favourites buttons there; the box is ours.
- Slate (DwmIsCompositionEnabled FALSE): a horizontal gradient sampled from
  lh4074-181415.png row 124 (216,225,245) -> (228,236,255), the light band
  (241,246,255) 94..52 px left of the divider, falling to (194,202,220) at it,
  then (192,200,218) -> (159,166,187) -> (207,214,232) at the right edge, and
  the dark (56,58,63) line under the header.
- Caption behaviour: the header answers HTTRANSPARENT except over a crumb,
  the frame's WM_NCHITTEST turns the rest of the header into HTCAPTION, so the
  toolkit's WM-framed path moves (drag) and maximises/restores (double-click).
  Tested in the lab: drag +40/+30, maximise, restore, runtime theme switch
  aero -> slate4074-classic -> aero (glass -> Slate with a 52 row header ->
  glass). doc/explorer-longhorn-glass.png, doc/explorer-longhorn-slate.png
  (slate msstyles, longhorn icons, lhshell bars).
- Back/Forward buttons (header.cpp "navigation buttons"): 4074's own images
  are the .NET resources framelet_{back,forward}_{rest,hover,pressed,
  disabled}.png of WINDOWS/Microsoft.NET/Avalon/Microsoft.Windows.Client.dll
  (back 38x54, forward 27x54, RGBA: a round blue glass button on a pale
  translucent plate; the forward plate continues the back plate's right
  edge; hover/pressed add the history drop-down under the button; forward
  disabled is only the plate, as in the 4074 screenshots). They are read at
  run time from `longhorn_system32`/../Microsoft.NET/Avalon (nothing is
  copied into the repo): src/shell/netres reads System.Resources .resources
  streams (0xBEEFCACE, reader v2, ByteArray/Stream entries) out of any file,
  src/gdi/png decodes PNG (libpng simplified API; libpng is now in PKGS).
  Without them the header draws lookalikes of the same size (blue disc with a
  vertical gradient, dark rim, gloss, white arrow, the plate, the drop-down;
  4x4 supersampled). Placement: the back circle (rows 6..41 of the image) is
  centred 37 rows above the contents in 4074 (circle x 261..293 of
  4074-dwm.png = client x 7..39); with lhwm's caption outside the client that
  would cut 5 rows off the top, so the image starts at max(H - 60, -5): the
  circle starts at the client's first row and the drop-down (rows 43..53)
  ends at the header's last. AlphaBlend'ed on glass and Slate alike. States:
  disabled without history (no hover, clicks eaten so the window does not
  drag from a button), hover (the whole button incl. its drop-down), pressed
  while the button is down and the pointer over it (captured); a click on the
  circle sends ID_BACK/ID_FORWARD, a press on the drop-down opens the history
  menu (the toolbar's, now explorer.cpp history_menu) under it at once, as a
  split button does. Hit test: circles (r 17.5 / 11.5) and, when enabled, the
  drop-down rectangles answer HTCLIENT, the rest stays caption.
  doc/explorer-longhorn-glass.png (back hovered).
- Opaque header: superseded by "The opaque header: navigation bar and lhwm's
  pixmap" below (caption_1.png is no longer read). The original notes:
  (lh4051-225158.png under Slate, lh4051-232843.png under
  4051's Luna): without composition 4051's header band simply continues the
  window's caption - Slate's grey horizontal gradient, Luna's blue - with a
  white bold title, a black breadcrumb, the round Back button, a divider and
  the search/favourites buttons at the right, and a dark line before the
  body. lhwm publishes no caption colour in _LHWM_FRAME_METRICS (only text
  colours), but its theme directory (_LHWM_THEME, new shim call
  FlxpDwmGetFrameThemePath): for a `mode = msstyles` theme the header reads
  caption_1.png (the active caption image) and theme.ini's
  caption.sizing_margins/border_width, takes the row just above the image's
  bottom margin and stretches it horizontally as lhwm stretches the caption
  over the window (client x + border_width), one colour per column; a 1 px
  line 55 % towards black under it; the divider a light and a dark soft
  line. Title: Tahoma Bold 16 px in the caption text colour (crText), crumbs
  black, as both 4051 shots. DWM themes without glass and no lhwm keep the
  4074 Slate gradient. Re-read on every composition/theme change; tested by
  switching _LHWM_THEME at run time slate4051-classic -> aero ->
  slate4051-classic (opaque -> glass -> opaque). doc/explorer-longhorn-opaque.png
  (slate4051-classic). lhwm has no 4051 Luna theme yet; the Luna reference's
  caption title is drawn by 4051 in the header only (lhwm's msstyles mode
  still draws its own caption text, NO_TITLE is a DWM-mode flag).
  Future: a unified path could have lhwm hand the client a pre-rendered
  header background pixmap (caption continued over the header rows, any
  theme mode), instead of every client re-deriving it from theme files.
- The 4051 command bar (src/explorer/lhbar.{h,cpp} + explorer.cpp lhtool):
  in longhorn mode the XP toolbar band and the Address band are hidden and the
  brand (throbber) band is not created; the rebar has one row: the menu band,
  then (no RBBS_BREAK, 30 px band header as the gap measured after Help) a
  small list toolbar as in lh4051-*.png: a red-crossed page (Delete the
  selection; the icon is shell32's browser strip #206 cell 2, in 4051 the
  meaning of that button is a guess), Search (focuses the header's search box),
  Folders (toggle), Views (whole drop-down), a magnifier drop-down (Filter:
  Filter by Name..., Clear Filter, Show Hidden Files) and a details-view
  button (toggles Details/Icons, shown checked in Details). No Back/Forward,
  no Up. Icons: the 16 px shell strips of longhorn_system32's shell32.dll
  (#216 normal, #217 hot = XP's tb_sh_def_16/tb_sh_hot_16 layout, identical
  in 4051 and 4074: delete 10, search 17, details 23, views 32, folders 43),
  else the icon theme's small strip, else comctl32's std/view strips. Gaps
  are disabled empty buttons with a fixed cx (a flat separator would draw its
  etch). Background: the rebar is subclassed (WM_ERASEBKGND; the transparent
  toolbars forward their erase to it) and painted with Longhorn's per-folder
  type Rebar.BACKGROUND strip from slate.msstyles: Default/Document/Music/
  Picture(=Video)/Contact/Game = bitmaps 194/196/197/198/199/200 in 4074,
  177/179/180/181/182/183 in 4051 (same pixels), 838x8 with sizing margins
  1 1 1 0: per column, row 0 is the highlight row, rows 1.. the body (row 3
  taken), first/last column the side borders. The Document strip is exactly
  the blue of lh4051-225221.png (edge (36,67,142), centre (85,115,187),
  highlight 24 % white). Button frames: ExplorerMenu::Toolbar.BUTTON (4074
  bitmap 370, 4051 340; 20x115, five 23-row images: 1 hot, 2 pressed, 3 the
  checked look), nine-grid margins 5, AlphaBlend'ed; text white
  (ExplorerMenu::Toolbar TEXTCOLOR), inactive window: 45 % towards the bar's
  centre colour. The toolbars are unthemed (SetWindowTheme "") and custom
  drawn (TBCDRF_NOEDGES|NOBACKGROUND|NOOFFSET); checked state is ours, not
  TBSTATE_CHECKED (which gets the classic dither). Vendor patch (toolbar.c,
  flxp:): the whole-drop-down arrow uses the custom-draw clrText (default
  clrBtnText, so XP mode is unchanged) - white on the bar.
  Settings (flxp.ini, all optional): `menubar_style` = default | document |
  music | picture | video | contact | game (the slate strip; auto document) |
  gradient (drawn) | theme (no colouring: the visual style's own rebar, as
  4051's bar under Luna, lh4051-232843.png, where the blue is only the task
  pane below); `menubar_gradient = #edge #centre [#right]` (drawn gradient,
  auto #24438e #5572bb, used when style=gradient, when the msstyles is not
  found, or when set without a style); `menubar_text = #rrggbb` (auto white);
  `menubar_msstyles` = a Longhorn slate.msstyles (auto
  longhorn_system32/../Resources/Themes/Slate/slate.msstyles; 4051 or 4074,
  told apart by the resource ids). The msstyles is opened as a data module
  (LoadLibraryEx + LoadImage): our uxtheme cannot parse Longhorn's compiled
  (binary) theme ini, so the part's bitmaps are addressed by id.
  View > Toolbars > Large/Small Icons are grayed in this mode.
  doc/explorer-longhorn-toolbar.png (Search hot, tooltip).
  Seen in the lab only: picom (xrender, Xvfb) sometimes shows stale wallpaper
  over parts of an ARGB window after a tooltip and a menu were shown; the
  window's own contents are right (import -window) and the pre-change
  binary does the same, so it is a compositor artefact.
- The Address band is hidden (RB_SHOWBAND) while the header shows the path;
  View > Toolbars > Address Bar becomes a real toggle in this mode only (XP
  mode keeps the old grayed item).
- Search box: filters the list as you type (names containing the text, ASCII
  case-insensitive): the explorer keeps all_items (the enumeration) and items
  (what the list shows); refresh() enumerates and applies the filter, so sort,
  status bar and verbs work on the filtered list. Enter keeps the filter and
  focuses the list, Escape clears it; navigating to another folder clears it.
- XP mode is pixel-identical before/after (Luna and classic, the
  scratchpad cmp.sh/diff.py comparison against the pre-change binary).
- Codex (1 call) on 2x header crops: title 4 px low (since 2), separator
  outline (fixed), glow too wide (3 -> 2 px).
- lhwm wishes: a way for a client to own the caption rows too (4074's title
  and Back button sit across them), e.g. a flag that makes lhwm draw only the
  caption buttons and leaves the rest of the caption band to the client
  through the same transparent-client trick; then the header could match the
  reference exactly.

## Menus end on external events: WM_CANCELMODE (2026-10-23)

A popup menu (context menus, the menu band's menus, any TrackPopupMenu) used
to stay open while the window was moved or resized, after it lost activation
and after a click into another application: nothing grabs the pointer (as on
Windows, other applications keep their clicks), and nothing told the menu
loop. Windows ends modes with WM_CANCELMODE, which the system sends on
deactivation, on moves/sizes from outside and when the input is taken away;
DefWindowProc then ends the menu (MENU_EndMenu) and releases the capture.
Now the same:
- win (window.cpp cancel_modes): WM_CANCELMODE is sent to the capture window
  (a context menu's popup holds the capture in menu mode; a pressed button,
  a drag) and to the toplevel concerned (a menu-band menu's owner) when
  - a WM-framed toplevel gets a ConfigureNotify that moves or sizes it and was
    not caused by our own SetWindowPos (that already set the rect),
  - a toplevel loses the focus (FocusOut, also mode NotifyGrab: the WM's key
    bindings, Alt+Tab), before WM_KILLFOCUS/WM_ACTIVATE(0),
  - another client grabs the pointer while it is over one of our windows
    (LeaveNotify mode NotifyGrab -> new sys::EventKind::GrabbedElsewhere: the
    WM's Super+drag move/resize, a launcher); for a popup the message goes to
    its owner's toplevel.
- shim (defwnd.cpp): WM_CANCELMODE calls MENU_EndMenu(h) whenever a menu is
  active, as Wine's DefWindowProc (MENU_EndMenu itself checks that h is the
  menu's popup or owner; the old test matched the popup only), then releases
  the capture if h has it. Wine's MENU_TrackMenu then sees fEndMenu and the
  posted WM_CANCELMODE and returns; the menu band's track loop (WH_MSGFILTER
  hook untouched) gets no switch request and ends, resetting its button.
- Tested (scratchpad cm.sh on plain Xvfb, explorer frame=xp): context menu,
  then xdotool windowmove / windowsize / windowfocus on an xmessage: the
  menu goes each time (the old binary keeps it and even stacks a second
  one). Under lhwm (lab, aero): context menu + Super+Up (maximise), File menu
  + activating another window, context menu + Super+drag: all closed. XP mode
  pixel-identical, dlgkeys all ok.
- Not done: a click on the root window or on an unmanaged window without a WM
  does not deactivate us, so a menu stays then (Windows would have the same
  click land in the same thread only if it were ours); a pointer grab would
  catch it but would also eat the click.

## The desktop: flxp-desktop (2026-10-23)

`make desktop` -> build/desktop/flxp-desktop, installed as bin/flxp-desktop.
`flxp-desktop [--wallpaper FILE] [--wallpaper-mode MODE] [--theme ...]
[--icons ...] [--quit-after MS]`. As in XP, where the desktop is Explorer's
DefView list view in icon mode over the Desktop folder with the wallpaper as
its background:
- One toplevel covering the screen (the whole root; no per-monitor windows
  yet), CreateWindowEx(WS_EX_FLXP_DESKTOP, ..., WS_POPUP): new in the toolkit,
  win::create makes such a toplevel managed (not override-redirect) and
  undecorated, sys::set_window_type(Desktop) sets _NET_WM_WINDOW_TYPE_DESKTOP,
  _NET_WM_STATE BELOW+STICKY+SKIP_TASKBAR+SKIP_PAGER, _NET_WM_DESKTOP
  0xFFFFFFFF and Motif "no decorations", lowers it whenever it is mapped (so it
  is at the bottom without a WM too) and focuses it on a button press (lhwm
  does not manage DESKTOP windows, so nobody else would). Title "Program
  Manager" as XP's.
- Wallpaper (src/desktop/wallpaper): `wallpaper` = BMP (gdi::decode_bmp), PNG
  (libpng simplified API) or JPEG (libjpeg); `wallpaper_mode` = stretch
  (default, XP's), center, tile, fill (cover, crop centred); box-filtered when
  shrinking, bilinear when growing. No file: XP's plain blue (58,110,165). It
  is composed once into a screen-size DIB and blitted in WM_ERASEBKGND, which
  also serves the list: with LVM_SETBKCOLOR CLR_NONE Wine's list view sends its
  WM_ERASEBKGND to the parent with its own DC (the list covers the window at
  0,0, so the coordinates agree).
- The list: LVS_ICON without LVS_AUTOARRANGE, LVS_EDITLABELS, the icon title
  font (SPI_GETICONTITLELOGFONT), the theme's 32 px icons (uiicons), text
  white on CLR_NONE, LVS_EX_TRANSPARENTSHADOWTEXT. Items: My Documents ($HOME),
  My Computer (::computer), Recycle Bin ($XDG_DATA_HOME/Trash/files) as
  virtual items (Open/Explore only), then ~/Desktop (sorted).
- Wine changes (listview.c, commctrl.c, .patch files updated):
  - DrawShadowText is XP's (nt5src comctl32/v6 blur.cpp) instead of Wine's
    offset-text stub: coverage of the text drawn into a buffer with a 5 px
    margin, every touched pixel opaque, box blur 4 wide x 5 high (sum of 5 rows
    / 4, clamped), premultiplied black, AlphaBlend'ed at (left - 4 + dx,
    top - 8 + dy), text on top. XP's buffer is bottom-up and blurs upwards in
    memory (= each row takes the rows above it): done explicitly on a top-down
    buffer. The result is a soft shadow about (0.5, 1) px off the text for the
    (2, 2) the list view passes.
  - Icon-view labels drawn transparent (not highlighted) on a CLR_NONE list
    with LVS_EX_TRANSPARENTSHADOWTEXT go through DrawShadowText(white, black,
    2, 2), XP's lvicon.c rule (XP enables it with the ListviewShadow setting,
    Vista with that ex-style); selected labels on such a list get their
    highlight box (Wine only filled the text cells).
  - LVM_SETITEMPOSITION computes the item metrics first when there are none:
    the first paint after DeleteAllItems arranged every icon and threw away
    positions set before it.
- Positions: free (no auto-arrange); every icon's top-left is kept by name in
  $XDG_CONFIG_HOME/flxp/desktop-positions ("x y name" lines), written at once
  on every change and re-read on every refill (the file is the truth). New
  items take the first free cell of a column-major grid from the work area's
  top-left (LVM_GETITEMSPACING), as XP fills the desktop. Dragging icons
  (LVN_BEGINDRAG) moves the selection with the pointer and saves; positions are
  kept inside the work area. Background menu > Arrange Icons By > Name/Size/
  Type/Modified sorts and lays out once (XP), Auto Arrange keeps it laid out,
  Align to Grid snaps.
- Work area: SPI_GETWORKAREA and GetMonitorInfo's rcWork now return the
  window manager's _NET_WORKAREA (for _NET_CURRENT_DESKTOP), else the screen;
  the desktop lays out inside it and re-clamps on changes (lhshell's taskbar
  and sidebar struts: 1131x770 in the lab).
- Root properties: sys/win/shim watch_root_property (FlxpWatchRootProperty):
  changes are posted to every toplevel as WM_FLXP_ROOTPROPERTY (0x03F0, wp =
  the id); FlxpGetRootCardinals reads one. The desktop watches _NET_WORKAREA
  (and _FLXP_WALLPAPER, _LH_SETTINGS_CHANGED).
- Maximised mode (removed 2026-10-25): the desktop used to fade to black while
  _LHWM_MAXIMIZED_MODE was 1 (lux's old M5a desktop layer). M5b (x11/SHELL-DESIGN.md
  "M5b, note for flxp-desktop") replaced that: the desktop never fades, only the
  maximised window's plate and the bars switch. flxp-desktop no longer watches the
  property; `desktop_fade_ms` is deprecated and ignored (default 0).
- Opening: double-click/Enter opens through verbs (files with their default
  application, e.g. a PNG opened the system's Chromium in the test); folders
  and the virtual items start a new explorer process (`flxp DIR`: the
  installed flxp next to flxp-desktop, else build/explorer/explorer, else PATH;
  the Trash folder is created if missing). Not in-process: the explorer keeps
  its state per process (and itself starts a process for every further
  folder), so this keeps one model, needs no refactor of explorer.cpp's
  globals, and the desktop survives an explorer crash.
- Menus: the item and background menus are the explorer's (src/explorer/
  verbs, now shared through a Host: open_folder, changed, begin_rename,
  clip_changed, trashed, updated, background_properties); the desktop adds
  Auto Arrange / Align to Grid to Arrange Icons By, and its background
  Properties is a Display Properties stub (a message pointing at flxp.ini).
  Keys: Enter, Delete (Trash), F2, F5, Ctrl+A/X/C/V, Alt+Enter, Shift+F10 and
  the menu key, arrows/type-ahead (the list view's). Cut/Copy stay
  per-process (no X CLIPBOARD yet): no copying between desktop and explorer.
  ~/Desktop is polled every 2 s (mtime) and refilled on changes.
- Stacking with lhshell (lab, lhwm + picom + lhshell): both are DESKTOP
  windows; lhwm lowers DESKTOP windows after every restack and their mutual
  order is just who was lowered last. Observed: flxp-desktop started after
  lhshell ends up below lhshell-desktop; lhshell maps its layer only in
  maximised mode (black, lowered before mapping), so either order looks right
  today (both black then), but an Aurora on lhshell's layer would be hidden
  under ours or cover our icons depending on start order. lhwm could keep a
  defined order (e.g. by _NET_WM_PID start time or a _LHWM_DESKTOP_LAYER
  hint), or lhshell's layer should be off when flxp-desktop runs.
- Tested on plain Xvfb (no WM; doc/desktop-luna.png with the background menu)
  and under lhwm + picom + lhshell with the Longhorn wallpaper (fill),
  longhorn icons and an explorer in longhorn mode (doc/desktop-longhorn.png).
- Not yet: per-monitor desktops (Xinerama/RandR), a real Display Properties,
  drag and drop between desktop and explorer, the X clipboard, icon labels
  wrapping to more than XP's two lines when focused, a full Trash folder icon.

## Thumbnails view (2026-10-24)

View > Thumbnails (also the Views button and the background menu's View; XP's
order Thumbnails, Icons, List, Details - Tiles is left out: Wine's listview has
no LV_VIEW_TILE). As XP's defview (dvthumbnail.cpp _SetThumbview):
- The list stays LVS_ICON; its LVSIL_NORMAL image list becomes one of
  `thumbnail_size` squares (flxp.ini, default 96 = XP's; 32..256), the icon
  spacing cxIcon/4 + size + 1 + 20 across (125 at 96; the height stays the
  automatic one) and LVS_EX_BORDERSELECT is set. Leaving the view resets the
  spacing (-1,-1), the ex-style and the 32 px list.
- LVS_EX_BORDERSELECT in the vendored listview.c (flxp:, XP lvicon.c): a 1 px
  outline over the square's edge in the outline colour (LVM_SET/GETOUTLINECOLOR,
  default COLOR_BTNFACE - Luna's beige frame), a selected item a 3 px frame
  outside the square instead (COLOR_HIGHLIGHT with the focus, COLOR_BTNFACE
  without, SHOWSELALWAYS), the image itself not tinted; the label starts 4 px
  under the square (XP g_cyLabelSpace; Wine had 2, which the frame would touch).
- Squares (src/explorer/thumbview): pictures fitted (never enlarged) and
  centred on the list's background colour (XP CalculateAspectRatio), opaque;
  other files their 48 px icon centred (XP CreateDefaultThumbnail uses
  SHIL_EXTRALARGE); folders the folder icon at full size with up to four
  pictures from inside in a 2x2 layout (folderthumb.cpp: 40 px cells at
  x 7/49, y 11/52 for 96, scaled for other sizes), first by name. Items show
  the default square at once; finished thumbnails replace it as they arrive.
  Within a folder the squares are kept by path and mtime (sorting, filtering
  and F5 reuse them; F5 asks again for files whose mtime changed); navigating
  drops them and cancels pending work.
- Worker threads (shell::thumbs::Loader, up to 4 by CPU count) do the reading,
  decoding and scaling; the UI thread only composes squares. The win layer
  got cross-thread posting: PostMessageW/win::post from another thread queue
  under a mutex and write to a pipe (sys::wake) that sys::wait_event selects on
  next to the X connection; the UI thread moves those messages into its queue
  before looking for messages (destroyed windows are skipped). The loader
  posts one thumbview::WM_THUMBS (WM_APP+0x40) per batch; the frame takes all
  finished results then. Measured: 300 JPEGs of 3000x2000 thumbnailed in
  ~2.5 s with 4 threads, the list scrolling meanwhile.

The cache (src/shell/thumbs, no win32), the freedesktop thumbnail spec 0.9,
shared with GNOME/KDE/etc.:
- $XDG_CACHE_HOME/thumbnails (else ~/.cache/thumbnails)/normal (128) or
  large (256, when thumbnail_size > 128)/<md5 of the file URI>.png. The URI is
  shell::uri_from_path (GLib's escaping); md5 is a small RFC 1321 copy.
- A cached thumbnail is used when its tEXt Thumb::URI equals the URI and
  Thumb::MTime the file's mtime (whoever wrote it); a current `large` one also
  serves `normal`. Otherwise the picture is decoded, fitted into 128/256 (not
  enlarged) and saved as 8-bit RGBA with Thumb::URI, Thumb::MTime, Thumb::Size,
  Thumb::Image::Width/Height and Software=flxp, written to a temporary name in
  the same directory, chmod 0600, renamed (directories 0700). Files inside the
  thumbnail directory are not thumbnailed.
- Failures (unreadable/undecodable) get the spec's marker in
  thumbnails/fail/flxp-1.0/<md5>.png (a 1x1 PNG with URI and MTime), so they
  are not decoded again until they change. No size limit and no clean-up.
- Formats: by extension .png .jpg .jpeg .jpe .jfif .bmp .dib .ico .cur. No SVG
  (no SVG renderer here), no GIF/WebP/TIFF yet.

Decoders, now shared (src/gdi/image): gdi::decode_image/load_image for PNG
(gdi/png), JPEG (libjpeg, now in PKGS: with a size hint it decodes at 1/2,
1/4 or 1/8 by DCT scaling, never below the hint; the EXIF orientation of APP1
is applied, all eight cases), BMP (gdi/bmp, which now also reads BI_RLE8 and
BI_RLE4, bottom-up, skipped pixels transparent) and ICO/CUR (the largest,
deepest frame: PNG frames and DIB+AND-mask frames; the shim's icon loader uses
the same gdi::decode_icon_image, so PNG-compressed icon frames load there too,
where it used to print a FIXME). gdi::resample (area average shrinking,
bilinear growing, alpha-weighted) and gdi::fit_size. gdi/png also writes PNGs
with text chunks (write_png) and reads tEXt/iTXt (read_png_text). The
desktop's wallpaper loader keeps its own copy for now (src/desktop is being
worked on elsewhere); it can switch to gdi::load_image.
doc/thumbnails-luna.png.

## Following the session theme: _LH_THEME (2026-10-24)

The Longhorn lab has one theme setting, the root property `_LH_THEME` (a
section of the registry whose path is `_LH_THEMES_INI`, x11/themes.ini;
format and rules in x11/THEMES.md). flxp is a client of it - the explorer,
flxp-desktop and flxp-filedialog alike, since it lives in shell/ and the shim:
- src/shell/lhtheme.{h,cpp}: the registry (sections, aliases, a path's last
  component, `;`/`#` comments, paths relative to the file), the session theme
  of the process (`shell::session_theme()`), `theme_setting(key)` = flxp.ini /
  FLXP_<KEY>, else the section's `flxp_<key>`, else the default.
- Start-up (shell::setup_theme): unless `theme_follow = 0` or a theme was given
  for the run (`--theme`/`--classic`, or FLXP_THEME in the environment that a
  following parent did not export itself - it marks its export with
  FLXP_THEME_FOLLOWED, so child windows follow too), `_LH_THEME` is read over a
  short X connection of its own and its style replaces `theme=`.
- The style: our uxtheme reads XP-format visual styles only (PACKTHEM_VERSION
  3, a TEXTFILE ini); every Longhorn msstyles in the registry is the compiled
  version-4 format (VARIANT/VMAP/RMAP/CMAP resources). So the section's
  `flxp_msstyles` is used if given, else the msstyles itself if it is version 3,
  else an XP backport of the same name: `<registry>/../themes/xp-backport/
  <name>/<name>.msstyles` (the user's 4074 Jade and Slate backports) or the
  theme directories. aero4074 has `flxp_msstyles` = Jade's backport (there is
  no Aero one; the patched aero.msstyles is version 4); slate4051 gets the 4074
  Slate backport. No loadable style: a warning, the current style stays.
- Colour/size: `color = Default`, `size = Normal` are not in the backports
  (NormalSize): vendor patch (msstyles.c, flxp:) - a colour or size the style
  does not have selects its first one instead of failing.
- Run time: win::on_root_property_change(prop, fn) - fn runs once after a burst
  of changes, when the pending X events are handled (THEMES.md step 2); the
  shim (w32/theme.cpp) registers it for `_LH_THEME` and `_LH_THEMES_INI`. It
  looks the name up (unknown: a warning, nothing changes; the same section, e.g.
  the alias rewritten by lhwm: nothing), resets the system colours to classic,
  writes the ThemeManager key and calls UXTHEME_ReloadTheme (vendor patch,
  system.c flxp:): UXTHEME_LoadTheme with the metrics applied again (Wine skips
  them when the key already names the theme: `flxp_reloading`) - the new
  [SysMetrics] colours, fonts and sizes - and WM_THEMECHANGED posted to every
  window, as ApplyTheme does; controls reopen their HTHEME there. If the style
  is the same (aero4074 <-> jade4074 both load Jade) WM_THEMECHANGED is still
  posted (flxp_* keys, lhwm's frame). Then every toplevel is redrawn
  (RDW_INVALIDATE|ERASE|FRAME|ALLCHILDREN). FLXP_THEME* in the environment
  follow, so new windows start in the new style.
- WM_DWMCOMPOSITIONCHANGED / WM_DWMCOLORIZATIONCOLORCHANGED are now posted once
  per burst (not again while one is queued for the window).
- Explorer: WM_THEMECHANGED reloads the header's framelets, the 4051 command bar
  (lhbar::theme_changed: menubar_* again) and the Folders band title font (the
  menu font of the new style) and repaints; the header's colours follow lhwm's
  frame as before (WM_DWM*). Not followed live: the icon theme (start-up only).
- flxp-desktop: the wallpaper is `wallpaper=` (flxp.ini or FLXP_WALLPAPER), else
  the section's `wallpaper` (fill, as lhshell's feh); on a switch only flxp.ini's
  beats the theme's (second-display.sh's FLXP_WALLPAPER is a start-up default).
- flxp-specific keys added to x11/themes.ini (documented in x11/THEMES.md "flxp's
  own keys"): `flxp_msstyles` (aero4074), `flxp_icons = longhorn`,
  `flxp_longhorn_system32` (4074's, 4051's for slate4051), `flxp_menubar_style =
  document`; also usable: `flxp_explorer_resources` (new setting
  `explorer_resources`: the Avalon Microsoft.Windows.Client.dll with the
  framelets), `flxp_menubar_msstyles`, `flxp_menubar_gradient`, ... Prefixed so
  that a later lhshell `icons` key cannot clash.
- `--info` prints the session theme and where the framelets come from.
- Tested in the lab (lhwm + picom xrender, explorer + flxp-desktop running,
  nothing restarted): aero4074 -> slate4074 -> jade4074 -> slate4051 -> aero4074
  (doc/theme-switch-*.png; slate4051 changes the wallpaper to ascent.jpg). An
  explorer started under aero4074 and switched to slate4074 matches one started
  under slate4074 except the tree's horizontal scroll thumb (its content width
  is recomputed lazily): list, tree, header, toolbar, rebar, status bar, scroll
  bars and the window text colour (Jade 0,2,92 -> Slate black) follow; and the
  other way round. The file dialog switches as well. XP mode pixel-identical.

## Header polish and the caption seam (2026-10-24)

- What the user saw on the real display: flxp-desktop on PATH is the installed
  /usr/local/bin/flxp-desktop, and it opens folders with the installed
  /usr/local/bin/flxp - both from 2026-10-23 22:06, i.e. before the glass
  header (4c1c8e9), the framelets (cba9e5c) and the 4051 command bar were
  committed. That build shows the header with a folder icon and the XP Luna
  toolbar (Back/Forward from Luna's strip) under it (reproduced in the lab).
  `sudo make install` brings the current explorer. `--info` now prints the
  session theme and where the framelets come from.
- Framelet sources: Microsoft.Windows.Client.dll has exactly one framelet_* set
  (8 PNGs); the 4051 and 4074 DLLs have pixel-identical copies and no other
  4074 binary (msstyles, Avalon assemblies, I386) has 38x54/27x54 images, so
  there are no per-theme variants to select (unlike the taskbar resources).
  The DLL is found through `explorer_resources` (new; flxp.ini or the theme's
  `flxp_explorer_resources`), else `longhorn_system32`/../Microsoft.NET/Avalon;
  when neither has it a warning names both settings and the lookalikes are
  drawn.
- Disabled look: 4074-dwm.png's disabled Back button is grey and translucent
  on the glass (about grey 170 at 62 % from solving its pixels against the
  wallpaper beside it), not the blue of framelet_back_disabled.png: Avalon's
  disabled rendering. The disabled images are now converted so (luminance x
  0.62, alpha x 0.62), on glass and opaque headers alike.
- Placement: the image (54 rows; plate rows 0..42, circle 6..41, history
  drop-down 43..53) used to start 5 rows above the client to keep 4074's circle
  centre 37 rows above the contents; lhwm's caption is not ours, so the plate's
  top was cut flat there. Now the whole image is in the client from its first
  row, and the header is at least 54 rows (aero: 76 - 27 = 49 -> 54, so the
  contents start 5 rows lower than 4074's y=151; the title/breadcrumb keep their
  distance to the contents, ink rows 105..116 / 133..141 against 100..111 /
  128..136). The hover drop-down is whole. doc/header-buttons-before-after.png
  (aero, 3x: before with the cut plate and the icon, after), doc/explorer-
  longhorn-glass.png (hover with the drop-down).
  For the exact 4074 position lhwm would have to let the client paint into the
  caption rows: e.g. a frame flag (FLXP_DWMFF/_LHWM_FRAME_FLAGS bit
  "CLIENT_CAPTION") with which lhwm maps the client over the caption band too
  (or extends _NET_FRAME_EXTENTS' top to 0 and keeps only its buttons as a
  small input-only/override child), drawing the plate under it as for the
  extension, the caption buttons on top, and leaving hit-testing of the rest to
  the client (it already does _NET_WM_MOVERESIZE). Then the header would be 76
  rows from the frame's top edge as in 4074.
- The seam: in the lab (aero, picom xrender + gaussian blur, picom-glass.conf)
  the boundary between lhwm's caption and our header has no step: rows 101/102
  (the caption's last and the client's first) differ by 1-2 per channel in
  ours exactly as in extendframe at the same position (doc/seam-ours-2x.png,
  doc/seam-extendframe-2x.png; Codex, one call: "no seam, border, shadow or
  brightness step" at the top or along the left edge). Our window carries
  _LHWM_EXTEND_FRAME, so the picom rule matches (no shadow, no blur of its
  own); it is ARGB with background/border pixel 0; the header child starts at
  client row 0 and clears its whole rectangle to transparent; the rebar starts
  below it. The same holds for the stale installed build. Not reproducible
  here: the glx backend (Xvfb's software GLX does not refresh ARGB textures).
  One thing on our side that could show only with glx + dual_kawase +
  use-damage is a re-blur seam: every repaint of the header is damage, and a
  blur limited to a damaged rectangle that ends at the caption can differ from
  the caption's earlier blur. The header now invalidates only the crumb or the
  buttons that change on hover (it repainted all of it on every hover change).
  If the user still sees it: test with `use-damage = false` in picom; if it
  goes, it is picom's damage/blur handling (or lhwm's: the plate under the
  client is painted by the dec window, whose blur picom computes; with the
  client excluded from blur the caption and header are one blurred surface).

## The places pane (2026-10-24)

src/explorer/places.{h,cpp}: with the Folders tree off, its slot (the same
width, `tree_width`, and the same 4 px splitter) shows a pane of places and
bookmarks, as XP shows its task pane there; the Folders button and View >
Explorer Bar > Folders toggle between the two, as XP's do. `places_pane=off`
gives the old behaviour (nothing there). The window is created hidden, so the
default views (tree on) are pixel-identical.
- Contents: "Places" - Desktop (::desktop), Home, Documents and Pictures
  (XDG user dirs from $XDG_CONFIG_HOME/user-dirs.dirs, else ~/Documents,
  ~/Pictures; only when they exist), My Computer, Trash ($XDG_DATA_HOME/Trash/
  files, created on first use as the desktop does); "Bookmarks" - the GTK
  bookmarks file (shell/bookmarks; remote ones greyed), then the action
  "Bookmark this folder". The file is polled every second (mtime, size, inode),
  so bookmarks added anywhere (the Bookmarks menu, Organize Bookmarks, the
  item menu, GTK applications) appear; the explorer's own menu commands
  refresh it at once. A click opens the place in this window; right-click:
  Open, Open in New Window, for bookmarks Rename (in place: an edit over the
  name, Enter/focus loss keeps, Escape cancels; a name equal to the folder's
  own clears the label) and Remove, and Add Current Folder to Bookmarks
  (greyed for virtual folders or when present). The pane scrolls (scroll bar,
  wheel). Drag and drop is not there (the explorer has none yet).
- XP look, from the shellstyle sources (themes/themedir/shellstyle/*/
  style.uix: the "sectionss" styles, i.e. Other Places/Details) and measured on
  screenshots/explorer_xp_1.png (File and Folder Tasks), which agree:
  - pane: vertical gradient TASKPANE_BACKGROUND over the visible height (blue
    123,162,231 -> 99,117,214; homestead 204,217,173 -> 165,189,132; metallic
    196,200,212 -> 177,179,200), a white line left and at the bottom;
  - sections 12 px inside the pane (after the line), the first 12 px from the
    top, 15 px apart; header 25 rows (2 px border + the 23 px chevron):
    headersection.bmp stretched as a nine-grid with 3 px corners (its magenta
    corners transparent, so the top corners are round), title Tahoma 8 bold
    at +12 in NS_EXPANDO_BUTTON (33,93,198), centred in the 23 rows (ink rows
    and x match the screenshot to the pixel; the title turns 66,142,255 under
    the pointer); chevron up.bmp/down.bmp (32 bpp with alpha, hot variants
    under the pointer) 3 px from the right, 2 px down - placement found by
    matching the bitmap against the screenshot;
  - the box: 1 px white border (left, right, bottom), fill 214,223,247
    (homestead 246,246,236, metallic 240,241,245), padding 12/10, rows 16 px
    (icon) 4 px apart, the name 6 px after the icon (Tahoma 8, NS_TASK_BUTTON;
    underlined and 66,142,255 under the pointer, hand cursor), long names wrap
    under themselves with the icon beside the first line;
  - a click on the header folds the section (XP animates, we do not).
  The bitmaps are read at run time from `nt5src`
  (Source/XPSP1/NT/shell/themes/themedir/shellstyle/<blue|homestead|metallic|
  classic>/), chosen by GetCurrentThemeName (luna.msstyles + its colour
  name); without them the header is a drawn white-to-blue gradient with cut
  corners and the chevron a drawn disc with two chevrons. Other .msstyles
  (not Luna) use their ExplorerBar class (EBP_NORMALGROUPHEAD/BACKGROUND/
  COLLAPSE/EXPAND, the gradient and text colours) when they define it.
  Classic: the classic style.uix (window background, btnface headers with
  btntext titles, window-coloured boxes with btnface borders, windowtext
  names, the classic chevron bitmaps keyed on magenta), icon title font.
  WM_THEMECHANGED re-reads all of it (live theme switches).
- frame=longhorn (until 2026-10-26, replaced by the groups look, see "The
  user's 2026-10-26 notes"): 4051's pane (lh4051-232843.png): white, a "Filter by" box
  (EDIT with cue banner, client edge = the 127,157,185 frame of the reference)
  at (12, 34), 22 high, about the reference's width (pane - 47), then plain
  links in 86,115,186 every 20.5 px starting 12 px under it, underlined under
  the pointer; no icons and no section titles (the reference has none), a
  10 px gap between places and bookmarks; every bookmark shows (the "more"
  that folded a long list went on 2026-10-25: 4051's "more" folds further WinFS
  views, lh4051-194216.png). The box
  filters the list as the header's search box does (the same filter; Enter
  goes to the list, Escape empties it; navigating clears both).
doc/places-luna.png (with the Thumbnails view), doc/places-longhorn.png.

## The opaque header: navigation bar and lhwm's pixmap (2026-10-24)

What 4074 really draws without composition (~/fun/longhorn/COMPOSITION.md 16):
browseui's navigation bar, a rebar at the top of the client themed with
`Navbar::Rebar.BACKGROUND` (Inactive-/Max-/MaxInactiveNavbar by focus and
maximise state), `Rebar.SPLITTER` over its last 4 rows, the travel/favorites
AnimationButtons, the NavbarBase gripper and the Navbar.TITLE/breadcrumb fonts
and colours, under an ordinary caption with no text or icon. The header now
picks, while `_LHWM_COMPOSITED` is 0 (header.cpp choose_mode):
1. the session theme's navigation bar (src/explorer/navbar.{h,cpp}) when its
   Longhorn msstyles is a known build. Our uxtheme cannot read the compiled
   version-4 styles, so the msstyles (`navbar_msstyles`, flxp.ini or the
   theme's `flxp_navbar_msstyles`, else the registry section's `msstyles`) is
   opened as a data module and the bitmaps are taken by id, with one table per
   build told apart by bitmap sizes: Slate 4074 (band 53, Back 38x49), Jade
   4074 and the patched Aero (same ids; band 58, Inactive/Max variants of the
   buttons, title and crumbs), Slate 4051 (band 54, no splitter, tiled dot
   gripper, black crumbs). Longhorn stores these bitmaps premultiplied (the
   splitter's white row is 128,128,128 at alpha 128): taken as they are.
   Background nine-gridded with its SIZINGMARGINS over the client width (the
   max variants: Slate whole-image, Jade 1 184 1 1), splitter state 0 over rows
   H-4..H-1 (margins 3 3 2 1), gripper, Back at (2,0) and Forward at (40,0)
   in the DISABLED/NORMAL/HOT/PRESSED/SPLITPRESSED images, Favorites at
   (cw-46, 0 or 5), the breadcrumb arrow glyph (6 states) between crumbs, and
   a drawn magnifier (no image of it was found in 4074's files: grey glass for
   4074, 4051's blue lens with an orange handle). The search box hides behind
   the magnifier: a click (or Search on the command bar, header::open_search)
   puts it left of the gripper; it goes again when it loses focus empty. The
   star opens the Bookmarks menu. Height = the band's (header::height_for_caption).
   Focus: WM_ACTIVATE -> header::set_active; maximised: IsZoomed, which now
   answers _NET_WM_STATE for WM-framed toplevels (was always FALSE).
2. else lhwm's `_LHWM_HEADER_PIXMAP` (Luna, or styles without a Navbar): the
   caption band as the frame draws it, continued down; we copy its last H rows
   under the glass layout (framelets, title in the caption text colour,
   inactive text colour when inactive, black crumbs, a soft divider in the band
   colour, the search box), as 4051 under Luna. lhwm re-renders it on focus,
   size and theme changes and replaces the property: sys watches
   PropertyNotify on toplevels (PropertyChangeMask, not on popups) ->
   EventKind::WindowPropertyChanged -> WM_FLXP_DWMHEADERCHANGED (0x03F1) to the
   toplevel; FlxpDwmGetHeaderBitmap copies the pixmap (XGetImage with an error
   handler: lhwm frees the old one right after replacing the property).
3. else (no lhwm) the 4074 Slate gradient as before.
`header_background = navbar|pixmap|gradient` (default auto) forces one.
`_LHWM_EXTEND_FRAME` and `_LHWM_FRAME_FLAGS` = NO_TITLE|NO_ICON stay set in both
states (CLIENT-PROTOCOL.md "Additions 2026-10-24"); lhwm now honours them in
msstyles mode, so its caption carries no title or icon over our header.
Measured against the references (client coordinates, our window in the lab,
the reference windows located by template-matching the theme's bitmaps):
- Slate 4074 active (lh4074-181608.png "Computer"): background, splitter
  rows 49..52 and the dark last row equal per channel at all sampled columns;
  Back (2,0) and Favorites (cw-46,0) exact; title "Computer" ink x 83..164 rows
  4..18 against 83..165 rows 4..18; crumbs from x 83 rows 31; separators 15/14
  px blank before/after as the reference. Inactive ("Music" window) within 2
  levels (the other window's width is estimated).
- Jade 4074 active/inactive (181415/181440): background equal; title x 77,
  rows 6..22; separator gaps 23/21 as measured; inactive title/crumbs
  (98,100,105), inactive buttons and star.
- Slate 4051 (lh4051-225158.png "Local Disk"): background equal, title ink x
  81..166 rows 3..15 exactly, crumbs black from row 30, the dot gripper's rows
  4,5,11,12,...,46,47 at x cw-93..cw-92 exactly.
- XP mode pixel-identical (Luna and classic, against the previous binary), the
  glass header too (aero4074 + picom, whole screen equal). Live switches aero4074
  -> slate4074 -> jade4074-classic -> slate4051 -> aero4074 change the band and
  its height (49->54 glass, 53, 58, 54) without a restart.
doc/header-opaque-slate4074.png, doc/header-opaque-inactive.png (Slate 4074 and
Jade inactive), doc/header-opaque-luna.png (frame=longhorn forced under luna:
the pixmap, active above the inactive one).
Not done: NavBarSize < 2 (the Small* buttons), hover/pressed looks of the
magnifier beyond a brighter blend, drag of the splitter.

## libuxtheme: one msstyles reader/renderer (2026-10-24)

The shared visual-style library x11/THEMES.md "One uxtheme for everything"
asks for: flxp, lhwm (frames) and lhshell (taskbar/sidebar art) read the same
.msstyles through it. It is the vendored Wine uxtheme, unchanged in how it
draws, with a plain C API that needs no window system and no win32 at the
call site.

- Layout: src/uxtheme/ut.h (the API, installed as include/uxtheme/ut.h),
  ut.cpp (API over msstyles.c/draw.c/property.c), v4.{h,cpp} (the
  Longhorn-compiled reader; format summary at the top of v4.h, full spec
  x11/MSSTYLES-FORMAT.md), nowin.cpp (the eight user32 calls uxtheme makes,
  answered as if there were no windows: only in the .so), ut-dump.cpp, and
  exports.map (the .so exports ut_* only).
- What is in it (Makefile UTOBJS): vendored uxtheme msstyles/draw/property/
  stylemap/uxini, user32 text/uitools (DrawText, DrawEdge), the shim's
  window-free part (gdi, gdi_draw, gdi_text, image, crt, debug, syscolor,
  kernel, usp10), src/gdi, src/res, ut.cpp, v4.cpp. build/uxtheme/
  libuxtheme.a is those objects as they always were: every flxp program links
  it instead of listing them (W32OBJS), so flxp and the library are the same
  code. libuxtheme.so.1 = the same sources -fPIC + nowin.cpp, linked
  --no-undefined against freetype, fontconfig, libpng, libjpeg.
- Build/install: `make uxtheme` (also part of `make install`): bin/ut-dump,
  lib/libuxtheme.so{,.1}, include/uxtheme/ut.h. ut-dump has rpath
  $ORIGIN:$ORIGIN/../lib, so it runs from build/uxtheme and installed.
- API: ut_open(path, color, size) (NULL/unknown = the first, as the flxp
  msstyles.c patch does) / ut_close, ut_format (3 or 4), ut_color/ut_size,
  ut_list_colors/sizes/classes/parts/states/props, ut_part/ut_state/ut_prop
  (names -> ids: the file's own STBL schema for v4, Wine's stylemap tables
  for v3; decimal numbers accepted) and the reverse *_name, ut_get_int/bool/
  enum/color/margins/rect/position/font/intlist/string (uxtheme's lookup:
  state, part, class, base class, globals; class "sysmetrics" = [SysMetrics]),
  ut_get_stream (raw bytes: PNGSTREAM/D3DXSTREAM/BAMLSTREAM, 4042's IMAGEDATA
  BLOB, and the RT_BITMAP of any FILENAME property in either format),
  ut_get_resource (any resource), ut_get_bitmap (FILENAME through
  MSSTYLES_LoadBitmap's alpha rules, or a PNG stream, as premultiplied ARGB),
  ut_part_size, ut_content_rect, ut_draw_background / ut_draw_text into a
  premultiplied ARGB buffer (a shim DC over a wrapped gdi::Bitmap with
  has_alpha; the caller's pixels, no copy), ut_add_font_dir, and
  ut_draw_background_cairo (header-only under UT_WITH_CAIRO: cairo is not a
  dependency of the library). One global lock; any thread may call: a
  thread without the fake TEB gets one on its first call (w32::ensure_teb,
  kernel.cpp: Wine's inline GetLastError/__TRY read %gs).
- ut-dump FILE [--color C --size S] [--list [CLASS] | --get CLASS PART STATE
  PROP | --render CLASS PART STATE WxH out.png | --size-of CLASS PART STATE
  [min|true|draw] | --extract-streams DIR]. --list prints every property as
  ini text (a decompiler for both formats); names or numbers everywhere.
  UT_FONT_PATH adds a font directory for text.
- Vendor patches (msstyles.c/.h, stylemap.c; .patch files regenerated):
  MSSTYLES_OpenThemeFileClass (OpenThemeClass on any theme file, not only
  the active one), MSSTYLES_ParseThemeFile (parse without applying metrics),
  the v4 branch (version 4 -> FLXP_V4_Open picks the VMAP variant; the
  records go through the same THEME_CLASS/PARTSTATE/PROPERTY model as ini
  text; the [SysMetrics] handling is factored into parse_sysmetric and shared;
  BCMAP base classes become `overrides` in front of globals), v4 images are
  premultiplied already (prepare_alpha only zeroes alpha-0 pixels), a
  negative font size is a pixel height (v4 LOGFONTs), MSSTYLES_GetNextLong
  parses negative numbers (Wine skipped the minus sign: Luna's caption button
  Offset = -25, 5 read as 25, 5; XP reads them), MSSTYLES_LookupEnum takes
  numbers (v4 enums), reverse name lookups for the listings.
- v4 values as text: INT/SIZE/ENUM decimal, BOOL TRUE/FALSE, COLOR "r, g, b",
  MARGINS/RECT/POSITION comma lists, INTLIST the values without the count,
  STRING the UTF-16, inline FONT (LOGFONTW) "Face, -<px>[, bold][, italic]",
  string-table values as they are ("Tahoma Bold, 8" -> "Tahoma, 8, bold"),
  FILENAME and streams "#<resource id>" (LoadImage reads "#123" as an id),
  other inline blobs "*<n>". STBL01 and STBL02 are merged (Luna's
  AvalonButton); an id defined twice keeps its first name.
- Regression (worktrees at HEAD with and without these changes, Xvfb):
  explorer under Luna, explorer under the Slate backport, w32test/controls
  under Luna and under the Slate backport: pixel-identical.
- v4 validation: ut-dump --render of Button.PUSHBUTTON (NORMAL, HOT),
  Button.CHECKBOX, Window.CAPTION(ACTIVE), Rebar, Rebar.BAND, Toolbar.BUTTON
  (HOT), ScrollBar.ARROWBTN, ScrollBar.THUMBBTNVERT from 4074 Slate are
  pixel-identical to the same parts from the XP backport slate.msstyles;
  w32test/controls under the v4 Slate is identical to it under the backport;
  the explorer differs only in the menu/toolbar/address rows, where the
  backport changed values (Rebar TEXTCOLOR 0 0 0 vs 255 255 255, edge
  colours). doc/v4-slate.png = the explorer with FLXP_THEME = 4074
  slate.msstyles. Jade 4074, Jade 4066, aero 5048, Luna 4051 (all 9
  variants), Slate 4042 open, list and draw; the MSSTYLES-FORMAT.md
  checklist values (Slate VMAP/CMAP/Window.CAPTION/sysmetrics/RMAP, Jade
  LOGFONT -12 and TRANSITIONDURATIONS 37,6,.., CompositedWindow LGFRAME
  801x1214 alpha 213) come out as documented.
- Not done: the session-theme lookup (shell/lhtheme.cpp) still prefers the
  XP backports for v4 styles (they carry tuned values); FLXP_THEME=<v4
  file> or `theme =` uses the v4 style directly. Real Longhorn uxtheme
  semantics that are inferred (BCMAP order, duplicate records first-wins,
  binary alpha) follow MSSTYLES-FORMAT.md section 9. Text drawing needs the
  fonts (fontconfig/ut_add_font_dir); sysmetrics are never applied by the
  library (nothing global changes in the host process except the GS base).

## libuxtheme and the 5048 aero (2026-10-25)

Checked aero 5048 (wim/5048/2/Windows/Resources/Themes/aero/aero.msstyles,
v4, colour NormalColor, sizes Normal/LargeFonts/ExtraLargeFonts, 88 classes)
against the 4074 aero: ut-dump --list has no unnamed class/part/state/prop,
and every [Class.PART(STATE)] section of the file renders through
ut-dump --render without error (only uxtheme's own FIXMEs for ExplorerBar's
VERTGRADIENT, which Wine draws as a plain two-colour fill, and Progress'
RADIALGRADIENT, never drawn since the parts are image files). Button,
Window caption/frame/buttons, ScrollBar, Rebar, Toolbar, TaskBar, Start::Button,
StartPanel, Header, ListView, Tab, Menu (BARBACKGROUND/POPUP*/SYSTEM*),
Status, Progress, TreeView render like 4074's aero (grey silver caption,
dark glass Start orb, navy 0,2,92 text). w32test/controls with
FLXP_THEME=<5048 aero>: pixel-for-pixel the 4074 aero's look, doc/uxtheme-5048.png.
Differences from 4074 (all handled by the generic reader; the part and
property names come from the file's STBL, so renumbered MENU/STARTPANEL parts
resolve by name): no CompositedWindow/Sidebar/AnimationButton/... classes
(nothing for lhwm's glass frames: the compositor art is not in the file),
new classes Listbox, Static, Wizard05, TaskDialog, MonthCal, DatePicker,
TravelBand, Travel::Toolbar; Menu parts BARBACKGROUND/POPUPGUTTER/... instead
of BAR/CHECK; images chosen by DPI or size (IMAGEFILE1..5 + MINDPIn/MINSIZEn,
IMAGESELECTTYPE: Button.CHECKBOX/RADIOBUTTON, Tab.BODY, ScrollBar.ARROWBTN,
...), which Wine's UXTHEME_SelectImage already does. The one gap was the
API: ut_get_bitmap/ut_get_stream/ut_get_string(IMAGEFILE) found nothing on
parts that only have IMAGEFILE1..7; find_prop now answers IMAGEFILE with the
96-dpi pick (highest MINDPIn <= 96, else IMAGEFILE1). Not supported: the
OFFSETTYPE/UNIFORMSIZING/TRUESIZESCALINGTYPE/SOURCESHRINK/ALPHATHRESHOLD
properties are parsed and listed but uxtheme ignores them (DPI is always
96); the new classes are not used by any flxp control; no compositor frame.
Suggested themes.ini entry (not added): [aero5048] msstyles =
.../wim/5048/2/Windows/Resources/Themes/aero/aero.msstyles, color =
NormalColor, size = Normal (LargeFonts, ExtraLargeFonts), fonts =
/u/aap/fun/longhorn/fonts/5048, flxp_frame = xp (no glass art).

## Per-theme frame style: flxp_frame (2026-10-24)

themes.ini has XP's Luna (luna, luna-olive, luna-silver: lhwm's Luna frames from
msstyles2theme.py, `flxp_msstyles` = the real XP Luna). Under those the
explorer is the XP explorer. `frame = auto` (the default) now asks, in order:
flxp.ini/FLXP_FRAME `xp`/`longhorn`; the session theme's `flxp_frame`
(added to x11/themes.ini: `xp` for the three Luna entries, `longhorn` for
aero4074, jade4074, jade4074-classic, slate4074, slate4051; THEMES.md "flxp's own
keys"); else longhorn whenever lhwm runs (as before), else xp
(explorer.cpp want_longhorn). It is followed live: WM_THEMECHANGED calls
set_frame_mode, which
- to XP: deletes the 4051 command bar band (lhtool::destroy, lhbar::detach
  unsubclasses the rebar, menuband custom draw off), destroys the header
  (header::destroy), removes the frame extension and flags (lhwm draws its own
  caption title and icon again), shows the Standard Buttons and Address bands,
  restores the View > Toolbars items, adds the brand band, clears a header
  filter, places::set_longhorn(false) (the XP task-pane look, no Filter box);
- to Longhorn: the reverse, with the header told the folder and history.
The window stays ARGB once it was; a window started in XP mode becomes ARGB
when it first extends (the X window is recreated and mapped again: lhwm
re-frames it, a few pixels of jump). Tested in the lab (picom, aero4074 start):
aero4074 -> luna (XP bars, Luna caption with title) -> aero4074 (glass header)
-> luna-silver -> slate4074 (navigation bar); and a luna start -> aero4074 ->
jade4074-classic. NO_TITLE/NO_ICON are now honoured by lhwm in msstyles mode:
under slate/jade its caption above our header is empty. XP mode pixel-identical.

## Glass header over the caption rows: CLIENT_CAPTION (2026-10-24)

lhwm's experimental `_LHWM_FRAME_FLAGS` bit 3 (CLIENT-PROTOCOL.md "Additions
2026-10-24") is usable and used on glass (`client_caption = 0` in flxp.ini or
the theme turns it off): lhwm grows the client upwards to its top border (DWM
frames: the side border, 9 px; `_NET_FRAME_EXTENTS` top 9), paints the plate
under it as for the extension, keeps only its caption buttons and publishes
their rectangle as `_LHWM_CAPTION_RECT` (aero: 701,0 99x10 on an 800 px client).
flxp: FLXP_DWMFF_CLIENT_CAPTION (0x8) with NO_TITLE|NO_ICON while composited;
the header is then 4074's 76 rows from the frame's top edge less the border
(67), so the contents start 76 rows below the frame's top as in 4074-dwm.png
(y=151 against the frame's 75); the title and crumbs keep their distance to
the contents (title caps C-51..C-40, crumbs C-23..), and the Back/Forward
images move to 4074's place (circle centre 37 rows above the contents: image
top = H-60 = 7) instead of starting at the client's first row. The caption
buttons' rectangle is cut out of the input shape (new sys::set_input_hole =
XShape ShapeInput, FlxpDwmSetInputHole; FlxpDwmGetCaptionRect and
WM_FLXP_DWMCAPTIONRECTCHANGED 0x03F2 from the property), re-cut on every
resize, so clicks there reach lhwm's buttons; the rest of the header stays
HTCAPTION through the frame's WM_NCHITTEST (_NET_WM_MOVERESIZE / maximise).
Without composition the flag is cleared (the navigation bar sits under an
ordinary caption, COMPOSITION.md 16.4) and lhwm shrinks the client again.
Tested in the lab (aero4074, picom): maximise through the hole, restore with
lhwm's restore button, drag from the header, aero -> slate4074 -> aero -> luna
-> aero. doc/header-client-caption.png.
lhwm asks: (1) the client's height drifts across theme switches (418 -> 398 ->
416 -> 390 -> 408 rows here): growing uses the old theme's caption height,
shrinking the new one's; lhwm should undo exactly what it added. (2) A
maximised window's caption rect is 1175,0 105x19 (taller): fine for us, but
_LHWM_FRAME_METRICS has no maximised top border, so the header keeps 67 rows
when maximised. (3) The top-edge resize only works in the 9 border rows.

## Header diagnostics and lhwm's active window (2026-10-24)

Report: "the DWM decoration uses Luna Back/Forward buttons" (aero4074). What can
put other Back/Forward buttons into the header, and what was done:
- The framelets not found: then lookalikes are drawn (a blue disc with a white
  arrow). The user's flxp.ini has longhorn_system32 = 4074's, and `flxp --info`
  on the real :1 resolves Microsoft.Windows.Client.dll from it, so not there; but
  the resolution is now more robust (header.cpp framelet_candidates): flxp.ini /
  FLXP_* `explorer_resources`, then next to its `longhorn_system32`; the session
  theme's flxp_explorer_resources / flxp_longhorn_system32; then the well-known
  places <registry dir>/../wim, ~/fun/longhorn/wim and /u/aap/fun/longhorn/wim,
  `*/*/WINDOWS/Microsoft.NET/Avalon/Microsoft.Windows.Client.dll` (4074 first, then
  the newest build). The first file with all eight images wins (an incomplete
  one no longer stops the search). Drawn: one stderr line naming flxp.ini.
- The opaque navigation bar instead of glass: without composition the header is
  the theme's navigation bar, whose travel buttons are the msstyles' round
  Back/Forward images (aero4074 without a compositor = lhwm's jade4074-classic
  frame + aero.msstyles' Jade-table buttons). On the real :1 lhshell starts and
  stops picom per theme (lhshell.log "compositor: starting picom ... --backend
  glx"), so `_LHWM_COMPOSITED` flips some time after the theme switch, and glass
  only comes when picom actually runs. That path already refits on
  WM_DWMCOMPOSITIONCHANGED / WM_DWMCOLORIZATIONCOLORCHANGED (also when the
  composited bit changes inside a burst of _LHWM_THEME changes: sys re-reads all
  three properties, so the THEME event can carry the change; the explorer treats
  both messages the same). New: the header checks its mode on every paint
  (choose_mode against the current composition); a wrong one is corrected at once
  and the frame is told to refit (height, extension, CLIENT_CAPTION), so a missed
  or late flip can no longer leave the navigation bar on glass or v.v.
- The XP explorer (frame resolved to xp: Longhorn's shell32 toolbar strip has
  XP's green Back/Forward circles): frame=auto follows the theme's flxp_frame
  (longhorn for every Longhorn entry); new: with no flxp_frame, an explorer that
  started before lhwm (xp) takes the header as soon as lhwm's properties appear
  (WM_DWM*), never the other way (an lhwm restart deletes them for a moment).
Lab reproductions with the user's flxp.ini (aero4074 + picom; aero4074 without
picom; explorer before lhwm; luna -> aero4074 -> luna-silver -> slate4074 ->
aero4074; slate4074 -> aero4074 with picom started 1 s later, as lhshell does):
glass + 4074 framelets in every glass case, the Jade navigation bar without picom.
`flxp --info` now also opens the display and prints:
    framelets    <dll> (<the setting or place it came from>) | drawn lookalikes (tried: ...)
    frame        longhorn: the 4074 header (session theme aero4074: flxp_frame=longhorn)
    composited   yes (_LHWM_COMPOSITED 1, lhwm theme .../lhwm/themes/aero)
    header       glass (...) | navigation bar, Jade 4074 table, <msstyles> | lhwm's pixmap, else the gradient (why)
(without DISPLAY: "display (none ...)").

Active/inactive: the header used our focus (WM_ACTIVATE), lhwm draws the frame
active for the root `_NET_ACTIVE_WINDOW` (wm.c ewmh_set_focus). They differ
e.g. after a click on flxp-desktop (it takes the focus itself; lhwm does not
manage DESKTOP windows and keeps the explorer's frame active): measured with
the previous binary under slate4074, the frame stayed active and the
navigation bar went inactive. Now:
- sys::root_window (a WINDOW root property), win::wm_active(w) (1 / 0 / -1 when
  the WM publishes no _NET_ACTIVE_WINDOW), shim FlxpDwmIsWindowActive(hwnd,
  &active) (S_FALSE + GetActiveWindow() without one); also sys/win
  set_root_string, FlxpGetRootString / FlxpSetRootString (flxp_shell.h).
- explorer.cpp sync_active(): FlxpWatchRootProperty("_NET_ACTIVE_WINDOW");
  WM_FLXP_ROOTPROPERTY for it, WM_ACTIVATE (which keeps the window's own state in
  g.focus_active, the fallback without an EWMH WM), start-up, WM_THEMECHANGED,
  WM_DWM*, set_frame_mode -> header::set_active and menuband::on_activate (the
  menu bar's inactive text and the 4051 command bar follow the frame as well).
  header::set_active repaints in every mode (glass too) and marks lhwm's pixmap
  stale (its inactive variant arrives as WM_FLXP_DWMHEADERCHANGED).
- Tested (lab, flxp-desktop + two explorers, probe.sh: lhwm caption pixel and
  header pixel per window against _NET_ACTIVE_WINDOW): slate4074 (navigation
  bar), aero4074 without picom (Jade bar), luna with frame=longhorn (pixmap),
  aero4074 glass; activation by xdotool windowactivate, by clicks into the
  windows, by a click on the desktop, after theme switches: caption and header
  always the same state. XP mode pixel-identical (Luna, classic).

## The wallpaper: who owns it, Set as Desktop Background, _FLXP_WALLPAPER (2026-10-24)

Who paints the wallpaper: flxp-desktop, a full-screen DESKTOP window, paints its
own composed DIB (src/desktop/wallpaper: BMP/PNG/JPEG, stretch/center/tile/fill)
and covers the root window. lhshell's session scripts also set a root wallpaper
with feh (xinitrc.lab `feh --bg-fill longhorn.jpg`, lab.sh likewise) - that one is
only visible while flxp-desktop does not run (and lhshell's own desktop layer is
off then, LHSHELL_DESKTOP=0). So under flxp-desktop, feh, `xsetroot` etc. change
nothing visible; the desktop's choice, first match (desktop.cpp make_wallpaper):
1. the root property `_FLXP_WALLPAPER` (UTF8_STRING "MODE PATH", MODE = stretch |
   center | tile | fill; a bare path = the default mode): an explicit choice for
   this X session, by flxp or any tool (`xprop -root -f _FLXP_WALLPAPER 8u -set
   _FLXP_WALLPAPER "fill /path/pic.jpg"`); flxp-desktop watches it and recomposes
   at once; it also wins over a theme switch and over a restarted desktop's
   start-up settings;
2. `wallpaper=` / `wallpaper_mode=` (flxp.ini, or FLXP_WALLPAPER* at start-up -
   xinitrc.lab/second-display.sh export the Longhorn wallpaper only when flxp.ini
   has none; after a theme switch only flxp.ini counts);
3. the session theme's `wallpaper` (themes.ini; fill);
4. after a switch to a theme without one, the start-up wallpaper; else XP's blue.
Setting it (src/explorer/verbs, shared by the explorer and the desktop):
- the item menu of a single local BMP, PNG or JPEG (by signature, what the
  desktop decodes; GIF is not) has "Set as Desktop Background" > Stretch /
  Center / Tile / Fill (XP's shimgvw verb, with the Display Properties
  positions; ids ID_WALL_*), in the explorer's list (and File menu) and on the
  desktop;
- verbs::set_wallpaper(path, mode): shell::write_setting (new in shell/config:
  rewrites the first `key =` line in place, else appends; comments, order and
  every other line kept; creates the file with [flxp] if missing) for wallpaper
  and wallpaper_mode, then FlxpSetRootString("_FLXP_WALLPAPER", "MODE PATH");
- the desktop's background menu has "Desktop Background..." before Properties:
  the file dialog (GetOpenFileNameW: flxp-desktop now links src/filedlg) on
  "Pictures (*.bmp;*.jpg;*.jpeg;*.png)" / All Files, starting in the current
  wallpaper's folder (else ~/Pictures, else ~); the choice keeps the current
  position (stretch if none); not a picture -> a message.
Tested headless (lab :97, lhwm slate4074, flxp-desktop + explorer, scratch HOME
with the user's flxp.ini): explorer > red.png > Center (centred on blue; flxp.ini
gained exactly the two lines, the rest byte-identical); a switch to slate4051
(whose theme wallpaper is ascent.jpg) kept it; `xprop -set _FLXP_WALLPAPER "tile
.../tile.bmp"` tiled at once; Desktop Background... (GIF hidden by the filter)
-> plasma.jpg, tiled (mode kept); the desktop's own item menu on ~/Desktop/
red.png > Fill; anim.gif has no such item. XP mode pixel-identical.

## The taskbar: flxp-taskbar (2026-10-24)

The classic (XP-style, uxtheme-drawn) taskbar; lhshell keeps the Avalon taskbar and the sidebar (SHELL-DESIGN.md "M8").
`make taskbar` -> build/taskbar/flxp-taskbar, installed as bin/flxp-taskbar (`make install`, `install-taskbar`).
`flxp-taskbar [--edge bottom|top|left|right] [--theme NAME|PATH] [--classic] [--no-tray] [--start-menu-only] [--quit-after MS]`.
doc/taskbar-luna.png (lhwm, Luna: three windows, two tray icons, the start menu with Programs > Accessories),
doc/taskbar-slate.png (the same session switched live to slate4074; xclock demanding attention).

What it is (XP SP1 explorer read as the spec: tray.cpp, taskband.cpp, traynot.cpp, trayclok.cpp):
- One toplevel `Shell_TrayWnd`, WS_POPUP | WS_EX_FLXP_DOCK (new, flxp_shell.h): win::create makes it managed but
  undecorated like the desktop, sys::WindowType::Dock sets `_NET_WM_WINDOW_TYPE_DOCK`, STATE ABOVE+STICKY+SKIP_TASKBAR+
  SKIP_PAGER, `_NET_WM_DESKTOP` 0xFFFFFFFF, Motif no decorations and WM_HINTS input=False (no WM focuses it).
  `_NET_WM_STRUT` + `_NET_WM_STRUT_PARTIAL` for its edge: lhwm's `_NET_WORKAREA` becomes 1280x770 on the 1280x800 lab,
  maximised windows fit above it and flxp-desktop moves its icons into the new work area (checked with a 150 px bar).
- Edge: `taskbar_edge` (flxp.ini, FLXP_TASKBAR_EDGE or `--edge`; per theme `flxp_taskbar_edge`). Thickness
  `taskbar_height` (horizontal) / `taskbar_width` (vertical), default 30 themed, 28 classic, 100 vertical.
- Drawing, all through uxtheme of the session theme (`_LH_THEME`, followed live through WM_THEMECHANGED: themes,
  fonts, flag, layout and struts are redone): `TaskBar` BACKGROUNDBOTTOM/TOP/LEFT/RIGHT by edge; the start button is a
  child with SetWindowTheme "Start" -> `Start::Button` PUSHBUTTON normal/hot/pressed (pressed while the menu is open),
  the flag is explorer's IDB_START (`start.bmp`, 25x20 32-bit) / IDB_STARTCLASSIC from the nt5src tree (`nt5src`
  setting; without it four coloured squares), text "start" (classic "Start") in the part's font with its
  TEXTSHADOWOFFSET/COLOR through DrawShadowText; the task band (below); the notification area `TrayNotifyWnd` with
  SetWindowTheme "TrayNotifyHoriz"/"TrayNotifyVert" -> `TrayNotify` BACKGROUND, content margins from the part; the clock
  `TrayClockWClass` with "TrayNotify" -> `TrayNotify::Clock` CLP_TIME. Children paint their background with
  DrawThemeParentBackground (the bar answers WM_ERASEBKGND with its background in the child's coordinates).
- Classic (no visual style, `--classic`): COLOR_3DFACE with the raised edge on the screen's inside only (the XP bar
  reaches 2 px past the screen), DrawFrameControl start button with the bold caption font, raised task buttons, the
  checked one sunken with the 3DHILIGHT dither and bold text, 3 px between buttons (cxButtonSpacing), the tray
  BDR_SUNKENOUTER, clock in COLOR_BTNTEXT.

Metrics against XP (96 dpi):
- Bar 30 px themed (XP Luna), 28 visible classic (2000/XP classic). Longhorn 4051's Slate bar is 28/29 rows: set
  `flxp_taskbar_height = 28` in the slate4051 section of x11/themes.ini if wanted (not done: x11 is read-only here).
- Start button: Button_GetIdealSize = content margins + flag + text extent; Luna 96 x 30 (XP about 97-99: the font
  is Franklin Gothic Medium Italic from the 4074 Fonts dir, metrics slightly off), classic 4 + flag 16 + 3 + text + 4
  (56 x 22 at (2, 4) with Tahoma Bold; 2000/XP: about 54).
- Task band from `start.cx + SM_CXFRAME + 1` (tray.cpp) to 3 px before the tray. Buttons (taskband.cpp _GetIdealWidth):
  width = band / n, at most SM_CXMINIMIZED (160), at least 1.8 x (SM_CYSIZE + 2 SM_CXEDGE), remainder pixels to the
  first buttons; themed the bar's full thickness at y 0 (since 2026-10-25, below), 22 classic. Icon 8 px in
  (5 classic), text 3 px after it, ellipsis (4051 measured). No grouping or scrolling yet: past the minimum, buttons
  run off the band's end (phase 2: glomming by WM_CLASS, TaskBandGroupMenu, scroll buttons).
- Task button look (XP _HandleCustomDraw): the toolbar is custom drawn (Wine's toolbar takes neither CDRF_NEWFONT nor
  themed text colours): TaskBand::Toolbar TP_BUTTON normal/hot/pressed/checked/hotchecked, `TaskBand.FLASHBUTTON`
  while `_NET_WM_STATE_DEMANDS_ATTENTION` or the urgency hint (seven blinks at 500 ms, then lit until cleared);
  text in the part's FONT (Tahoma 8, so the active button is not bold, as on 4051), classic bold when checked.
- Tray: icons in 22 px cells (16 px icons; TB_SETPADDING 2 in traynot.cpp), clock width = the larger of "11:59 AM00"
  and "23:59 PM00"-style extents (trayclok.cpp's "00" slop), centred.

Window list (xwm.cpp, plain Xlib, its own connection on its own thread; XInitThreads at start; requests from the UI
thread are queued and the thread woken through a pipe; changes reach the UI as WM_APP+1 via PostMessage from the
thread):
- `_NET_CLIENT_LIST` order (lhwm: creation order), filtered: `_NET_WM_WINDOW_TYPE` DOCK, DESKTOP, TOOLBAR, MENU,
  SPLASH, UTILITY, NOTIFICATION, DROPDOWN/POPUP_MENU, TOOLTIP, COMBO, DND; `SKIP_TASKBAR`; windows with WM_TRANSIENT_FOR
  (owned windows, as XP); only the current desktop's (`_NET_WM_DESKTOP` vs `_NET_CURRENT_DESKTOP`, sticky ones always).
- Per client PropertyChangeMask: title `_NET_WM_NAME` / WM_NAME, icon `_NET_WM_ICON` (smallest of >= 16 px, box
  filtered to 16x16) else the WM_HINTS icon pixmap + mask (xterm, xclock), `_NET_WM_STATE` (HIDDEN, MAXIMIZED_*,
  DEMANDS_ATTENTION), WM_HINTS urgency. Root: `_NET_ACTIVE_WINDOW` (the checked button), `_NET_CURRENT_DESKTOP`.
- Click: the active, not minimised window is minimised (XIconifyWindow = WM_CHANGE_STATE), any other gets
  `_NET_ACTIVE_WINDOW` (source 2; lhwm restores minimised ones). Middle click: `_NET_CLOSE_WINDOW`. Right click: Restore,
  Minimize, Maximize (`_NET_WM_STATE` MAXIMIZED_VERT+HORZ), Close (default item). Tooltips: the full title
  (TBN_GETINFOTIP). Every button's screen rectangle goes to the client's `_NET_WM_ICON_GEOMETRY` (lhwm's minimise
  animation target).
- Without a window manager (plain Xvfb) the band is empty; the start menu, Run and tray still work (tested).
- Gap: flxp's own windows publish no `_NET_WM_ICON` yet (the toolkit sets none), so the explorer's button shows the
  default application icon; lhwm draws its theme default the same way.

System tray (freedesktop System Tray Protocol 0.3 + XEmbed):
- Owns `_NET_SYSTEM_TRAY_S<screen>` (a timestamp from a zero-length property append), announces MANAGER on the root,
  sets `_NET_SYSTEM_TRAY_ORIENTATION` (0 horizontal, 1 vertical), `_NET_SYSTEM_TRAY_VISUAL` = the default visual (no
  ARGB icons: they would not show the background without a compositor) and `_NET_SYSTEM_TRAY_ICON_SIZE` 16.
- Already owned: a message on stderr, no icons, and the X thread checks every 2 s and claims it once it is free;
  `tray_takeover = 1` takes it from another tray, never while lhshell runs (2026-10-25, below). Losing it
  (SelectionClear) gives the icons back to the root and waits again. `tray = 0` / `--no-tray`: none.
- SYSTEM_TRAY_REQUEST_DOCK: the icon window is added to the save-set, reparented into a container child of the bar
  (an X window of the tray thread's connection), sent XEMBED_EMBEDDED_NOTIFY, mapped according to `_XEMBED_INFO`'s
  XEMBED_MAPPED (followed on PropertyNotify), dropped on DestroyNotify or a reparent elsewhere. The container's
  background pixmap is the bar + TrayNotify background rendered for exactly that rectangle; icons of the default depth
  get ParentRelative and are cleared with exposures, so they draw over the theme's background (doc/taskbar-luna.png).
- On exit (and on SIGTERM/SIGINT/SIGHUP) the icons are reparented to the root first: the save-set alone would put them
  into the bar's own window (the nearest ancestor the tray connection did not create), which dies with the toolkit's
  connection; a restarted taskbar's MANAGER message makes them dock again (tested with build/traytest).
- build/traytest [COLOR [SECONDS]] (src/test/traytest.cpp): a minimal XEmbed client (a disc on the tray background;
  button 1 hides it for 2 s through XEMBED_MAPPED, button 3 quits; re-docks on MANAGER). No real tray application is
  installed here (no nm-applet, pasystray, stalonetray).
- Not yet: the chevron (TrayNotifyHoriz::Button, hiding inactive icons), balloon messages
  (`_NET_SYSTEM_TRAY_MESSAGE_DATA`), XEMBED focus messages.

Clock: HH:MM from the C library's locale (LC_TIME: T_FMT, or T_FMT_AMPM when the locale uses AM/PM, seconds removed,
no leading zero in 12-hour times as XP's "h:mm tt"), re-armed on the minute; tooltip = the long date (`%A, %B %e, %Y`,
`clock_tip_format`); `clock_format` overrides the time; `clock_date = 1` (per theme `flxp_clock_date`) adds the short
date as 4051's sidebar clock does ("7:22 AM, Oct 23"); `clock = 0` hides it, and so does a root
`_LHSHELL_CLOCK_VISIBLE` = 1 (watched: lhshell's sidebar shows a clock, COMPOSITION.md 11 `_ToggleClock`).

Start menu (phase 1, startmenu.cpp): opened on the button press (as XP) and by lhwm's `_LHSHELL_START` root message
(the Super key; `start_key = 0` turns that off, e.g. while lhshell also answers it). A Wine popup menu (TrackPopupMenu
with the keyboard: the dock takes the input focus for the menu and gives it back to the active window when the menu is
dismissed): Programs (the .desktop applications, shell::all_apps without NoDisplay, in submenus by their main
Categories: Accessories = Utility, Development, Education (+Science), Games, Graphics, Internet = Network, Multimedia =
AudioVideo/Audio/Video, Office, System Tools = System/Settings, Other; started with shell::launch), My Documents ($HOME
in a new explorer: flxp next to the binary, ../explorer/explorer, else PATH), My Computer (`flxp ::computer`),
Settings > Display Properties (a stub message), Settings > Theme (the sections of the session's themes.ini with their
`title`s; choosing one writes root `_LH_THEME`, which lhwm, lhshell and every flxp program follow), Run..., Log Off
<user>..., Shut Down.... `start_menu = lhshell` instead sends `_LHSHELL_START` with the button rectangle in data[1..4]
(SHELL-DESIGN M8's anchor rect) and leaves the menu to lhshell's StartPanel. shell::App has `categories` now
(Categories=).
- Run (shell32's RunFileDlg layout: 347x151 client, icon, prompt, Open: combo, OK/Cancel/Browse...): a directory opens in
  the explorer, an existing non-executable file with its default application (shell::open_files), a URL with xdg-open,
  anything else runs as `/bin/sh -c` in $HOME; history (26 entries, XP's RunMRU) in $XDG_CONFIG_HOME/flxp/run-history.
  Browse... is disabled (phase 2: the file dialog).
- Log Off (after an OK/Cancel question): `logoff_command` if set; else, when the window manager is lhwm/berry
  (`_NET_SUPPORTING_WM_CHECK` name), `lhwmc quit` (lhwmc next to the WM's executable from `_NET_WM_PID`, else PATH) and
  the taskbar exits; else the taskbar just exits (it is the session's shell then). Shut Down: confirmation, then
  `shutdown_command` (default `loginctl poweroff`; never pressed in the tests).
- Since 2026-10-25 this menu is `start_menu = classic` (or the old `own`); the default is XP's start panel (below,
  "The start panel"). Phase 2: the classic menu's large icons and banner; Search, Help, Documents (recent files), Taskbar
  properties, the taskbar's own context menu (lock, toolbars, cascade/tile, Task Manager), quick launch, grouping,
  autohide (2.1.3 timers), sizing/moving the bar by dragging, multi-monitor.

Vendor fix found on the way (msstyles.c, flxp:, patch regenerated): MSSTYLES_GetNextToken trimmed trailing blanks by
looking at the character *after* the token, so when a value ended in a blank (the line's \r) its last token lost a
letter: every theme FONT whose last word was an attribute lost it ("Tahoma, 8, Bold" not bold; Luna's sysmetrics
CaptionFont "Trebuchet MS, 10, bold"; Start::Button's "Franklin Gothic Medium, 14, italic"). Now `ut-dump --get
Start::Button 1 1 FONT` says italic and SPI_GETNONCLIENTMETRICS' caption font is bold under Luna. Anything drawn with
such a theme font in the explorer or the dialogs may change accordingly (it is now what XP draws).

Running it in the second-display session (x11/second-display.sh, USAGE.md): the taskbar needs DISPLAY and, for the
start menu's programs, the session's environment, as flxp-desktop:
`DISPLAY=:1 setsid flxp-taskbar >"$XDG_RUNTIME_DIR/lhwm-lab:1/flxp-taskbar.log" 2>&1 &` (or build/taskbar/flxp-taskbar).
lhshell has no switch for its taskbar alone: `second-display.sh shell off` stops lhshell entirely (taskbar, sidebar,
its tray, toasts), which is what was tested here (lab: lhwm + flxp-desktop + flxp-taskbar, no lhshell). With lhshell
running, both bars would take the bottom edge and lhshell's tray owns the selection first.
What the lhwm/lhshell side needs (SHELL-DESIGN M8, "What lhshell needs for this"):
- lhshell: `LHSHELL_TASKBAR=0` (and/or `[taskbar] enabled = 0`) like LHSHELL_DESKTOP: no Avalon taskbar surface, no
  taskbar strut, no `_NET_WM_ICON_GEOMETRY` writes, no tray selection (or give it up when another tray asks: it should
  not take `_NET_SYSTEM_TRAY_S0` while flxp-taskbar holds it); the sidebar yields to a foreign bottom strut (read
  `_NET_WORKAREA`); `_LHSHELL_CLOCK_VISIBLE` = 1 on the root while its sidebar shows the clock (we hide ours then);
  `_LHSHELL_START` should carry the anchor rect in data[1..4] when sent by us (`start_menu = lhshell`) and lhshell must
  not toggle its own menu on lhwm's Super-key message while we answer it (or we set `start_key = 0`). USAGE.md: a
  `second-display.sh taskbar on|off` like `desktop on|off`, restarting lhshell with LHSHELL_TASKBAR=0.
- lhwm: nothing is missing for phase 1 (EWMH covers the list, states, struts, activation, iconify, close; it already
  reads `_NET_WM_ICON_GEOMETRY`). `_LHWM_MAXIMIZED_MODE` matters only for the Avalon bar's black layer: the classic
  TaskBar parts of Slate/Jade/Aero have no composited or black variant (SHELL-DESIGN 2.1.1, M8 "Black layers when
  maximised: the classic taskbar has none"), so flxp-taskbar ignores it. Nice to have later: `_NET_WM_ALLOWED_ACTIONS`
  (to grey Maximize for fixed-size windows) and `_NET_WM_STATE_ABOVE` honoured for docks.

Tests (lab :82 = Xvfb 1280x800 + lhwm luna, no picom; plain Xvfb :81): luna and slate4074 live switches (and back),
classic with `--classic`, all four edges (work areas 0,30 / 100,0 / 0,0 1180 wide), activate/minimise/restore by
clicks, window menu Maximize, middle-click close, Run (xlogo started, history written), start menu by mouse, keyboard
and the Super key, tray dock/hide/show/undock and re-dock after a restart, flashing. Codex: three calls (one
comparison with 4051's Slate bar: ~75 %, differences explained by 4051's 28-row bar, its flag-only start button and the
4074 vs 4051 Slate bitmaps; one aborted by a broken pipe; one knowledge-based look at the Luna bar, which pointed at
the missing italic start text - the msstyles fix above).

## Header buttons from the theme, in every mode; navbar through libuxtheme (2026-10-25)

Report: "with the aero glass theme I see luna buttons". On glass the header drew
the Avalon framelets (Microsoft.Windows.Client.dll, round glossy blue) for every
theme. 4074's composited header is the same browseui navbar in the
`Composited` sub-app without a background (COMPOSITION.md 16.5): its travel
and favorites buttons are the style's own AnimationButtons. Checked with
ut-dump on aero.msstyles, 4074 Jade, 4074 Slate, 4051 Slate (and 4051 Luna):
`TravelBack::`/`TravelFwd::`/`Favorites::AnimationButton.ABBACKGROUND`
with states DISABLED NORMAL HOT PRESSED SPLITPRESSED (favorites without
SPLITPRESSED), `Inactive*`/`Max*`/`MaxInactive*` variants in aero/Jade only,
CUSTOMSPLITRECT (the drop-down: Slate 4074 0,38,38,49, others 0,44,..,54); no
`Composited::` variant of the buttons exists (only `Composited::Static.TEXT`
and `CompositedBreadcrumbBar::Toolbar`). Rendered: aero = Jade (grey glass,
green when hot or pressed, the same images as 4074-dwm.png's header), Slate =
blue, Luna 4051 = Luna blue with a lavender arrow (lh4051-232843.png).
- navbar.cpp now reads everything through libuxtheme (ut_open, ut_get_bitmap
  of IMAGEFILE / GLYPHIMAGEFILE, ut_get_margins, ut_get_font/color, the
  SIZINGTYPE enum): no resource ids. A sub-app class must be in the file and
  the part must have properties of its own (else uxtheme answers from the
  plain class: `Rebar`'s IMAGEFILE for a missing `Rebar.SPLITTER`); a missing
  Inactive/Max variant takes the active one. Band: `{,Inactive,Max,
  MaxInactive}Navbar::Rebar.BACKGROUND` + SIZINGMARGINS, `Rebar.SPLITTER`,
  `NavbarBase::Rebar.GRIPPER` else `Rebar.GRIPPER` (TILE = 2: 4051's dots);
  arrows = GLYPHIMAGEFILE of `{..}BB::Toolbar` (Jade/aero) or
  `BreadcrumbBarToolbar::Toolbar` `.SPLITBUTTONHORZARROW`; title font and
  colours `Navbar.TITLE` / `*NavTitle::Navbar`, crumb colours from the BB /
  BreadcrumbBarToolbar TEXTCOLOR. Slate's face "Tahoma Bold" (weight 400 in
  the file) is asked for as Tahoma 700. Height = Back image + 4 with a
  splitter. Only the placements stay measured: one layout per family (Slate
  4074 when the Back image is < 52 rows, Jade 4074 with a splitter, else
  Slate 4051), so any Longhorn style with these classes works.
- Skin.buttons (travel + favorites found) is separate from Skin.valid (the
  whole bar). With buttons, glass and lhwm's pixmap mode draw them too
  (header.cpp themed_free/nb_buttons): Back/Forward at the framelets' place
  (nav_top), hover/pressed/disabled/split-pressed from the parts, the
  variant by focus and maximise; on the right the bar's gripper (not 4051's
  dots: the soft line stays), the magnifier and the star, as in
  4074-dwm.png; the search box hides behind the magnifier as in the
  navigation bar (a click or Search opens it, it goes when it loses focus
  empty). The Avalon framelets remain the fallback for styles without
  TravelBack (XP styles) and for `header_buttons = framelets` (flxp.ini or
  the theme).
- `flxp --info` prints `buttons  the theme's TravelBack/... <msstyles>
  (<layout>), in every mode` (or "on glass and lhwm's pixmap" for Luna, or
  the framelets and why); the header line names the layout instead of a table.
- Regression (HEAD worktree vs new, lab Xvfb + lhwm): navigation bar under
  slate4074, jade4074, slate4051: the header pixel-identical (the 195
  differing pixels are the command bar's activation state at shot time,
  outside the header); XP mode (luna, classic) identical.
- Tested: aero4074 and jade4074 with picom (theme buttons, hot Back green,
  hot star green, magnifier opens the search box and filters), luna with
  frame=longhorn (Luna's buttons on lhwm's pixmap). doc/header-buttons-aero.png,
  doc/header-buttons-jade.png (Back hot after a crumb click; star hot).

## The start panel (2026-10-25)

XP's two-column start panel is flxp-taskbar's default start menu now (`start_menu = panel`; `classic` = the popup
menu above, `lhshell` = lhshell's). src/taskbar/startpanel.cpp; the spec is explorer/desktop2 of XP SP1 (deskhost,
userpane, sfthost/proglist, specfldr, moreprog, logoff). doc/startpanel-luna.png (hot item), doc/startpanel-slate.png
(slate4074 through lhwm's `_LH_THEME`).

Window and modality: one popup toplevel `DV2ControlHost` (WS_POPUP: override-redirect), self-drawn, no child
controls. It runs its own loop like a menu: the dock takes the keyboard (xwm::focus_own), the panel takes the win
capture and an X pointer grab (new `sys::grab_pointer`, owner_events: our windows - the flyout menus, the taskbar -
get their events, clicks into other clients come to the panel outside its rectangle). The grab is needed because
lhwm does not refocus the already active window on a click, so the dock's FocusOut (WM_CANCELMODE) never came; the
toolkit's LeaveNotify/NotifyGrab of our own grab is no longer taken for another client's (x11.cpp `own_grab`).
Closes on Escape, Super, a click outside (also the start button: it toggles), a command, and WM_CANCELMODE once the
dock had the focus (before that it is our own Run dialog giving the focus to the dock). Dismissed: the keyboard goes
back to the previously active window. Positioned above the start button at the screen's left (below it for a top
bar, beside it for left/right bars), clamped to the screen.

Layout (96 dpi; StartPanel DEFAULTPANESIZE 380x440 and the parts' DEFAULTPANESIZEs, the same in Luna, Slate, Jade):
- 380 wide; user pane 64 (USERPANE), two columns of 190, log off pane 40 (LOGOFF). Column height = max(336, what the
  items need): with 2 pinned + 6 MFU items it is 367, the panel 471 high.
- User pane (userpane.cpp): the picture 48x48 (aspect kept) inside USERPICTURE, framed size = 48 + its
  CONTENTMARGINS (8,6,8,6 -> 62x62), centred vertically; the name 2 SM_CYEDGE after the frame in the USERPANE FONT
  (Franklin Gothic Medium 19 px) with its TEXTSHADOWOFFSET/COLOR through DrawShadowText, vertically centred. Name =
  the passwd GECOS full name, else the login. Picture: ~/.face, ~/.face.icon, `user_picture =`, else XP's chess tile
  from nt5src (windows/advcore/duser/directui/test/app/logonui/chess.bmp); none: name only. USERPICTURE=FALSE in a
  theme hides it.
- Left column (PROGLIST background, CONTENTMARGINS 6,9,4,5): tiles of 32 + 4 SM_CYEDGE = 40 px (sfthost
  _ComputeTileMetrics), icon at 2 px, text after icon + 2 edges + 2. Pinned items bold, a PROGLISTSEPARATOR tile
  (part height + 6), then the most frequently used programs (6, XP's default), normal weight.
- All Programs (moreprog.cpp): the bottom 30 px of the left column (MOREPROGRAMS background, CONTENTMARGINS
  8,8,0,6), text in the part's FONT (Tahoma bold) indented 3 edges + 32 to line up with the list text, then
  MOREPROGRAMSARROW normal/hot (non-themed: a drawn triangle). Opens the classic menu's Programs (by category) as a
  flyout at the column edge, growing up from the button, on click, Enter, Right or after hovering
  SPI_GETMENUSHOWDELAY.
- Right column (PLACESLIST background, CONTENTMARGINS 4,9,6,5 Luna / 5,9,5,5 Slate): 24 px icons, tiles 32; My
  Documents ($HOME, as the classic menu), My Pictures (XDG_PICTURES_DIR if it exists), My Computer (bold, as XP);
  PLACESLISTSEPARATOR; Control Panel (a cascade: the classic menu's Settings: the Display Properties stub and the
  session's themes); separator; Run... (the Run dialog), Search ($HOME in the explorer: no search option yet).
- Log off pane (logoff.cpp): Log Off and Turn Off Computer right-aligned, each a 24 px image from the LOGOFFBUTTONS
  strip (72x24: eject, log off, turn off; HOT state while hot) + the text in the menu font and the part's TEXTCOLOR;
  they call the classic menu's log_off / shut_down (with their questions).
- Hot item (sfthost _OnLVItemPrePaint): filled with COLOR_MENUHILIGHT, text COLOR_HIGHLIGHTTEXT (Luna blue, Slate's
  own); non-themed COLOR_HIGHLIGHT. Text colours: each part's TEXTCOLOR.
- Without the StartPanel classes (classic, `--classic`): XP's non-themed colours: COLOR_MENU lists, the user and log
  off panes in COLOR_HIGHLIGHT with HIGHLIGHTTEXT, etched separators and column divider, a raised frame, the log off
  images from desktop2's LogoffButtons(Hot).bmp (XP's non-themed resources, nt5src).

Items:
- `pinned = ` (flxp.ini / per theme flxp_pinned): `;`-separated; a .desktop id, `terminal` (the terminal setting,
  shell::open_terminal in $HOME), `explorer` ($HOME in a new explorer), or a command line (sh -c; name and icon from
  the .desktop whose Exec runs that program, else the program's name). Default `terminal;explorer`.
- MFU: `$XDG_DATA_HOME/flxp/mfu`, lines "COUNT ID", +1 for every application started from the panel or the classic
  menu (startmenu::launch -> startpanel::count_launch); the top 6 that are installed, displayable and not pinned.
- Icons: left column 32 px from the .desktop Icon= (an absolute file, else NAME.png in $XDG_DATA_HOME/icons and
  $XDG_DATA_DIRS/icons under hicolor then Adwaita, sizes 32, 48, 64, 128, 256, 24, 22 (resampled), else
  /usr/share/pixmaps/NAME.png; no SVG/XPM); otherwise the icon theme's `exe`. Right column 24 px from the icon theme
  (uiicons::load_icon, build/explorer/uiicons.o is linked into the taskbar): home, pictures, computer,
  control_panel, run, search - `pictures`, `control_panel`, `run` are new keys in res/icons/*.theme (XP:
  shell32/mypics.ico, control.ico, strun.ico; Longhorn: shell32.dll #236, #137, #25). An installed flxp needs
  `make install` for them (FLXP_DATADIR is searched before the source tree; FLXP_DATA=src tree for tests).
- Keyboard: Up/Down within a column (wrapping), Left/Right between the columns (nearest row) and along the log off
  buttons, Right also opens a cascade, Tab through all, Enter/Space runs, Escape closes. No type-to-jump.

Stubbed / not done: Control Panel is the Settings cascade (no control panel folder), Display Properties a message
box, Search opens $HOME (no search pane option in the explorer yet), no subtitles under pinned items, no "recent
documents"/My Music/Help/Printers places, no drag and drop or context menus on items, no pinning from the UI, no
rounded top corners (the window is rectangular; the USERPANE image's corner pixels show COLOR_MENU), the flyouts
are plain popup menus (not the themed MenuBand of XP's All Programs). Tested on Xvfb + lhwm (luna, slate4074) and
--classic: open/close by the button, click outside (another client), focus change, Escape, keyboard launch (MFU
count written), All Programs > Accessories launch, Control Panel cascade, Run with the Run dialog already open.

## The horizontal task band (2026-10-25)

4051/4074 Explorer has a band under the command bar (lh4051-232843.png: the folder's
big document image faded at the left, links "Make a new folder", "Search for files or
folders", "Publish this folder to...", "more"; lh4051-225221.png with a selection: the
item's icon, its name, "all properties...", the item's verbs). XP's equivalent is the
vertical shellstyle pane. It takes room, so it is optional: `taskpane = 0|1` (flxp.ini,
default 0), `--taskpane`, View > Task Band (toggling writes `taskpane=` to flxp.ini;
grey in the XP frame). Longhorn frame only (src/explorer/taskpane.{h,cpp}): a child
under the rebar, 104 rows.
- Content: left the folder's icon at 96 px faded (the selection's icon whole when one
  item is selected), the name (Segoe UI 18 px, else Tahoma) and a line (the path, or the
  item's type), then the tasks in two columns 196 px apart, 27 rows apart, 16 px theme
  icons, white 12 px links underlined on hover: nothing selected = New Folder, Search
  (the header's search box: on_command(ID_SEARCH)), Open a terminal here, Add to
  Bookmarks, folder Properties; a selection = Open, Rename, Copy, Delete, Properties.
  "more ▾" opens the background menu (or the item menu) as the right-click does.
- Background: 4051's band is the command bar's Document::Rebar.BACKGROUND strip
  stretched down (measured: the band's columns equal the strip's body row, then a
  light row (88,112,169) and a dark one (29,54,116)); with `taskpane_style = theme`
  it uses lhbar's strip (the theme's per-folder Rebar.BACKGROUND, or its gradient)
  with a light and a dark line at the bottom. Since 2026-10-25 that strip is the default
  (`taskpane_style = menubar`, "theme" is the old name): the same gradient and colours as
  the menu band above it. `taskpane_style = aurora` uses the PDC 2003
  demo's aurora bitmaps when demo_resources is found (`taskpane_aurora = DIR`, else
  next to the Longhorn images, ~/fun/longhorn/demo_resources): orange for document
  folders, purple for music, blue for the rest (frame 00 of each 40-frame loop, the
  middle rows over the width). doc/demo-resources.md catalogues docLib, search and
  chadDesktop and what they show (one Codex look at a contact sheet).
- `flxp --info` prints `task band  on|off; <background source>`.
- Not done: the aurora's animation, item previews (thumbnails) in the band, the
  "all properties..." link, per-folder-kind task lists (the demo's library tasks).
- Tested in the lab (slate4074 navigation bar): orange band in doc/, theme strip in
  flxp/ (doc/taskpane-longhorn.png), a selection switches to the item's tasks, "more"
  opens the item menu; XP mode pixel-identical (luna, classic), the Longhorn header
  unchanged with the band off.

## Font smoothing, lhwm's caption text, the task band's look, Folders off (2026-10-25)

**Font smoothing** (XP's Display Properties > Appearance > Effects > "Use the following
method to smooth edges of screen fonts"): `font_smoothing = off|standard|cleartype` in
flxp.ini (default standard, XP's default), read by shell::setup_fonts, so every program
(explorer, taskbar, desktop) gets it; gdi::set_font_smoothing / gdi::font_smoothing
(src/gdi/font.{h,cpp}); the shim's SPI_GET/SETFONTSMOOTHING and
SPI_GET/SETFONTSMOOTHINGTYPE (FE_FONTSMOOTHINGSTANDARD/CLEARTYPE) read and write it.
Standard is GDI's grayscale (GGO_GRAY4, 17 levels) wherever the font's gasp table says
GASP_DOGRAY at the ppem (Wine's get_gasp_flags; synthesized bold/italic stay gray as with
Wine's antialias_fakes); mono otherwise. The 4074 fonts' tables: Tahoma/Tahoma Bold,
Verdana, Franklin Gothic Medium, Trebuchet MS gray only above 16 ppem (and at <= 8),
Trebuchet Bold above 11 ppem, Microsoft Sans Serif above 18, Marlett never. So Tahoma 8
and MS Sans Serif 8 stay crisp; the Luna start button (Franklin Gothic Medium 14 pt,
19 ppem), Trebuchet 10 bold captions, Segoe UI and the large header title are
smoothed. lfQuality: NONANTIALIASED_QUALITY never, ANTIALIASED_QUALITY always (gasp
ignored, as before), CLEARTYPE_QUALITY/CLEARTYPE_NATURAL_QUALITY gray. **cleartype is
drawn as standard for now** (no subpixel blending in gdi yet; documented, not faked).
Pixels: XP-mode explorer luna and classic identical to before (everything there is
Tahoma 8 / MS Sans Serif 8, gasp says mono; checked with Folders on, see below); the
Luna taskbar differs only in the "start" text (34..73 x 7..23 of the bar,
doc/taskbar-start-aa.png), the clock and task buttons (Tahoma 8) unchanged. src/test/text.cpp
now turns smoothing off for its first block (it assumed the old default off).

**Header title like lhwm's caption** (src/explorer/header.cpp CaptionLook): when lhwm
runs, the header reads the theme.ini of `_LHWM_THEME` (the first `|` field) and the
colours of `_LHWM_FRAME_METRICS`, and the title and breadcrumb take lhwm's caption face
(the first family of `caption_font`/`font` that we have: "Tahoma Bold 11px",
"Segoe UI,Tahoma 12px", "Trebuchet MS,Tahoma Bold 13px", "Segoe UI 9"), its weight, the
active/inactive text colours (crText/crInactiveText; crumbs too), the msstyles
frames' `caption.shadow.1/.2` (single = one offset copy, continuous = comctl32's
DrawShadowText as lhwm's ms_shadow_text: box blur of the touched pixels at -4+dx,
-8+dy) and on glass the DWM `glow` colour (radius 2 as before), and `text_antialias = none`
(Luna) -> NONANTIALIASED_QUALITY, else ANTIALIASED_QUALITY (lhwm/pango antialiases
regardless of gasp). Sizes stay the header's (the Navbar's title_px/crumb_px, 17/12 on
glass, 16 for the pixmap caption title): at lhwm's 11-13 px the title would not be a
Longhorn header any more. Choices against the theme: the msstyles Navbar prescribes its
own title face, weight and colours (navbar::Skin title_face/title_weight/title_color,
crumb_color) - lhwm's face, weight and colours win over them; on glass lhwm's #1e1e1e /
#5a5a5a replace the measured 4074 navy (5,6,62). Not followed: lhwm's maximised
colours (text_max_*; not published in the metrics). Without lhwm (no _LHWM_THEME)
nothing changes. Lab (Xvfb + lhwm, picom for aero): doc/header-title-match.png, an
xterm's caption above our header, active and inactive, for slate4074, jade4074-classic,
luna with frame=longhorn and aero4074 glass. Re-read on every composition/frame theme
change (composition_changed -> make_fonts).

**Task band**: default background is the menu band's (lhbar's strip, `taskpane_style =
menubar`); aurora is opt-in. Icons: DrawIconEx into a zeroed DIB left the alpha byte 0,
so AlphaBlend (AC_SRC_ALPHA) added the icon to the gradient (black fringes, washed
colours); the icon is now drawn over black and over white, alpha = 255 - the largest
channel difference, colour = the black pass (premultiplied).

**Folders off by default**: the explorer starts with the places pane where the Folders
tree was; `folders = 1` in flxp.ini (or FLXP_FOLDERS=1) starts with the tree. View >
Explorer Bar > Folders and the toolbar's Folders button write `folders=` (the Explore
verb opens the tree for that window only, not remembered). XP-mode comparisons against
the reference screenshots (which have Folders open) must pass FLXP_FOLDERS=1: the lab's
xpcmp*.sh scripts do.

## Taskbar metrics, start-menu provider, tray handover (2026-10-25)

Three items from x11/todo ("flxp taskbar now") and lhshell's notes for flxp (SHELL-DESIGN M8 status, items 1-5).
doc/taskbar-vs-xp.png: 4051-vm-now.png's bar, ours under slate4051, ours under Luna before and after (2x).

Measured, and fixed:
- No XP Luna bar to measure: screenshots/luna_exp_*.png are window shots (no taskbar in them); the references are
  4051-vm-now.png (XP's explorer code with 4051's Slate parts, 1152x864) and the XP SP1 source.
- Task buttons: 4051's checked plate (TaskBand::Toolbar BUTTON state 5) starts on the bar's row 2 and ends on its last
  row with 14 rows of the flat middle: the 28-row image stretched to 30 rows at y 0, not 25 rows at y 3 as NOTES said
  (that misreading squeezed the image: Luna's plate was 21 visible rows from y 6, now 25 from y 3 with 2 below, the
  image's own transparent rows). XP agrees: the button height is the toolbar's (taskband.cpp _GetCurButtonHeight), the
  rebar and the tray get the whole client height (tray.cpp _GetWindowSizes). Now themed horizontal buttons are the
  bar's thickness at y 0 (vertical bars unchanged, 25). Icon and text after the fix, bar-relative: icon y 7..22,
  text y 11..20, the same rows as 4051; icon 8 px in, text 19 px after the button's left (4051: the same within 1 px).
- Button widths: 160 (SM_CXMINIMIZED, taskband.cpp iMax) for one or two windows; 4051's single button is 148
  (4051's own minimised width, not XP's). Band start = start.cx + SM_CXFRAME + 1 (tray.cpp), unchanged.
- Start button Luna: CONTENTMARGINS 10,24,2,4 + flag 25 + "start" = 96 x 30. XP: 97, because GDI sums the
  font's hdmx widths at 19 ppem (9+6+10+7+6 = 38 in framdit.ttf) where FreeType's hinted advances give 37. That is
  src/gdi's business (advances from hdmx when the table has the size), left to it.
- Tray and clock (traynot.cpp _SizeWindows, trayclok.cpp _CalcMinSize): left margin 19 + icons + clock (the
  theme's text extent of "11:59 AM00"), right 0, full height: matches already (Luna: 97 px with one icon, clock text
  at y 11..18). The bar's top rows are the TaskBar part's own (4051: 136, 222 grey on rows 0/1, ours identical).

Start-menu provider (SHELL-DESIGN "Start menu ownership, revised"; xwm.cpp do_menu_provide/start_message,
taskbar.cpp start_request):
- With `start_menu = panel` or `classic` the X thread owns `_LH_STARTMENU_S<screen>` with an InputOnly window
  (timestamp from a property append); `start_menu = lhshell` releases it (re-checked on WM_THEMECHANGED, the setting
  may be per theme), exit destroys it. Owned by someone else (another provider): left alone, logged; SelectionClear:
  we stop being the provider.
- `_LHSHELL_START` on the owner window: l[0] bit 0 toggle, l[1..4] the anchor rectangle. Toggle closes an open menu
  (startpanel::close, startmenu::close = EndMenu), else opens it; flags 0 opens or leaves it open. With an anchor that
  is not our own button the panel/menu opens above it, left edges aligned (below it in the screen's upper half).
- The root broadcast (lhwm's Super; no rect = toggle) is answered only while nobody owns `_LHSHELL_S<screen>` (lhshell
  forwards it to us then) and no other provider owns the menu; `start_key = 0` still ignores it.
- `--start-menu-only` / `start_menu_only = 1`: no bar, struts, tray, task buttons or `_NET_WM_ICON_GEOMETRY`; the
  Shell_TrayWnd is a 1x1 override-redirect window off the screen (the menus' owner and keyboard target): our start
  panel next to lhshell's Avalon taskbar. Without an anchor it opens at the bottom-left.
- Tested (Xvfb :84 lhwm luna, :85 lhwm slate4074 + lhshell avalon, a small Xlib sender): anchored open/toggle, root
  toggle, flags 0 twice, root ignored while a fake `_LHSHELL_S0` exists while the owner messages still work, release
  with start_menu = lhshell; with lhshell: its Avalon Start button and Super open our panel above its button
  ("forwarded to provider ... anchor 0,738 83x29" in its log), Super again closes it, Escape closes it (keyboard).

Tray (lhshell's notes 4 and 5):
- Never taken from lhshell: while `_LHSHELL_S<screen>` is owned and the tray is someone else's we wait, whatever
  `tray_takeover` says (lhshell hands it over itself before starting us as its classic bar); free: claimed (2 s poll).
- The takeover race: a ReparentNotify of an icon is checked against the server (XQueryTree); an icon still in our
  container stays; one that the previous tray moved to the root within 3 s of our docking is taken back once.
- Hand-back (exit, SIGTERM/SIGINT/SIGHUP, SelectionClear): no more events, unmapped, background None (not our
  ParentRelative), reparented to the root, out of the save-set. Tested with build/traytest: after each signal the icon
  is an unmapped child of the root, a restart docks it again; a foreign owner taking the selection gets it back the
  same way and we reclaim it 2 s after it quits; with a fake lhshell selection and `FLXP_TRAY_TAKEOVER=1` we waited
  until the tray was free.
- Not fixable here (note 6): icons that lhshell created 32-bit keep that depth and show black where transparent in
  our tray (no compositing); we announce the default visual, so icons created for us are fine.

## Display Properties and the shared settings store (2026-10-25)

The Longhorn session's one settings store (x11/SETTINGS.md): `$XDG_CONFIG_HOME/longhorn/settings.ini`, user presets
in `themes/NAME.theme`, the `_LH_SETTINGS_CHANGED` root message. flxp writes it from a Display Properties dialog and
reads it in every program.

**Store** (src/shell/settings.{h,cpp}, `shell::store`): `Ini` keeps the file line by line (set() rewrites a value in
place keeping indentation and a trailing `; comment`, else appends to the section, else adds the section; unknown
keys, comments and order survive); `get(section, key)` (cached, re-read when the file's mtime/size change);
`set(edits)` read-modify-write; `set_shared(edits)` the same plus flxp.ini for a mapped key flxp.ini sets (it would win
otherwise: the dialog and Set as Desktop Background rewrite it there too); presets: `preset_names/read_preset/
delete_preset/preset_path`; `registry_path()` (`_LH_THEMES_INI`, `$LH_THEMES_INI`, flxp.ini `themes_ini`),
`theme_list()` (registry sections by `title`, then presets by `[Theme] name`); `notify(hint)`: `_LH_SETTINGS_SERIAL`
(root CARDINAL) + 1, then the ClientMessage to the root with SubstructureNotifyMask, data[0] the serial, data[1] the
hint atom (`theme`, `desktop`, ... or 0 for several sections); `apply_theme(name)`: settings.ini `[theme] name`,
`_LH_THEME`, the message - the documented order; `launch_settings(tab)`, `lux_running()`.

**Precedence** for a flxp program: `FLXP_<KEY>` (options for the run) > flxp.ini > settings.ini > the session theme
(`flxp_<key>`, `wallpaper`) > the default. shell::file_setting/setting fall back to settings.ini for the mapped keys
(config.cpp's weak `store_value_for`, so flxp-portal links without the store), so readers changed nothing:

| flxp key | settings.ini | read by |
| --- | --- | --- |
| icons | [desktop] icons | every program, start-up only |
| wallpaper, wallpaper_mode | [desktop] wallpaper, mode (fill fit center tile stretch; `fit` new in src/desktop/wallpaper) | flxp-desktop, live |
| font_smoothing | [fonts] smoothing | every program, live |
| pinned | [start] pinned | start panel (when built) |
| start_menu | [start] menu (flxp -> our default, lux -> lhshell) | flxp-taskbar (provider, re-checked on theme changes) |
| taskbar_kind | [taskbar] kind | flxp-taskbar at start-up: `none`, or `avalon` while lux owns `_LHSHELL_S<screen>`, = --start-menu-only (unless start_menu_only is set) |
| frame | [explorer] frame (a flxp key of its own in settings.ini) | the explorer, new windows |

Live: sys/x11 reports a root ClientMessage of a watched type like a root property change (watching
`_LH_SETTINGS_CHANGED` selects SubstructureNotifyMask on the root), so `win::on_root_property_change` /
`FlxpWatchRootProperty` take it. The shim (w32/theme.cpp) re-reads font_smoothing and repaints everything when it
changed (the glyph caches are keyed by it); flxp-desktop recomposes the wallpaper (after a switch only flxp.ini /
settings.ini count, as before; `_FLXP_WALLPAPER` still wins: the dialog sets or clears it with the wallpaper). The
theme as before through `_LH_THEME`.

**Presets as themes**: lookup_session_theme falls back to `themes/NAME.theme`: the `[Theme] base` registry section's
keys with the preset's overrides ([VisualStyle] msstyles/color/size, [Composition], [Desktop] wallpaper/mode/icons ->
`wallpaper`, `wallpaper_mode`, `flxp_icons`, [Frames], [Shell], [Fonts]); paths relative to the preset file. So
`_LH_THEME = My Luna` restyles flxp. lhwm does not know presets yet ("_LH_THEME My Luna is not in themes.ini": it keeps
its frames) - its side of SETTINGS.md.

**flxp-settings** (src/settings, rc/settings.rc; `make settings`; also `flxp --settings [--tab ...]`):
`flxp-settings [--tab themes|desktop|appearance|effects]`. A Wine propsheet with OK / Cancel / Apply. Nothing is
written before OK or Apply (so Cancel reverts whatever Apply did not commit); Apply writes only what differs from the
committed state, sets `_LH_THEME` for a theme change, sends one `_LH_SETTINGS_CHANGED`, and stays open. The dialog
follows the theme like every flxp window, so applying a theme restyles it in place. Initial values: the theme from
`_LH_THEME` (the live session wins, SETTINGS.md "Conflicts"), the rest as the readers see them (flxp.ini > settings.ini
> theme).
- Themes: combo of the registry titles and the presets ("(custom)"); a sample drawn with libuxtheme from the selected
  style (the original msstyles, Longhorn-compiled ones too, else flxp's loadable one): the theme's background, an
  active window's caption, frame and caption buttons, a scroll bar and a default push button. Choosing a theme resets
  the background to the theme's (Windows does the same; the Desktop tab can change it before Apply). Save As... writes
  `themes/<name>.theme` from the current state ([Theme] name/base - a preset's base and overrides carried over -,
  [Composition] composition unless follow, animations, [Desktop] wallpaper/mode if one is set, icons) and selects it;
  Delete (presets only) asks, deletes, selects the base.
- Desktop: a thumbnail (the screen composed at full size, scaled), Background (edit; empty = the theme's; Browse...
  through our file dialog, pictures filter; Theme's clears it), Position (fill fit stretch center tile), icon set
  (longhorn xp 2k; programs started afterwards).
- Appearance: explorer frame (automatic xp longhorn), taskbar kind (avalon classic none), start menu (flxp lux),
  composition (follow on off), animations (4074plus 4074 invented 5048 none). Written for lux/lhwm; flxp itself acts on
  frame (new windows), kind and menu (next taskbar start).
- Effects: "Use the following method to smooth edges of screen fonts" + Standard/ClearType (off when unchecked).
- Wired: flxp-taskbar's Control Panel > Display Properties... (start panel and classic menu; the Theme > quick list
  stays), flxp-desktop's background Properties. For lux's start menu: run `flxp-settings` (optionally `--tab desktop`);
  shell::store::settings_program() finds it next to the calling binary, in the build tree, or on $PATH.

Tested (lab :96, lhwm slate4074, flxp-desktop + explorer + flxp-taskbar, scratch HOME with the user's flxp.ini): Slate ->
Windows XP (Luna) by Apply: lhwm's frames, the explorer (header -> XP mode), the desktop (luna's wallpaper; flxp.ini's
wallpaper line blanked) and the dialog itself restyled, dialog open, Apply greyed again; Save As "My Luna" -> preset
written, selected, applied (`_LH_THEME = My Luna`, flxp follows, lhwm keeps luna); Desktop > Browse > longhorn.jpg >
Apply: the desktop changed at once (settings.ini + `_FLXP_WALLPAPER`); Effects off/on: the taskbar's start text changed
live (409 px); the start panel's Control Panel > Display Properties... started flxp-settings. doc/settings-themes.png
(Jade 4074 no composition), doc/settings-desktop.png.

Not implemented: lhwm/lux reading presets, animations and composition (their side; we only write the keys); live icon
set switching; cleartype is drawn as standard (see "Font smoothing"); `[start] pinned` editing, `[taskbar] autohide/
rows`, `[sidebar]`, `[clock]`, `[commands]` (kept, not shown); the sample's caption text uses the Window class's text
colour (black where XP's caption is white); no live preview before Apply.

## Maximised header colours, task band preview, icon sizes, the Luna start button (2026-10-25)

**Header title when maximised** (todo "black/white flip when maximized"). lhwm already publishes what we asked for:
`_LHWM_FRAME_METRICS` has 18 items since 2026-10-25, 14 `max_text_rgb`, 15 `max_inactive_text_rgb`, 16 `max_glow_rgb`
(`0xFFFFFFFF` = no glow), 17 reserved (x11/lhwm/CLIENT-PROTOCOL.md; aero4074/jade: white, #626469, no glow). sys reads up to
18 items (8 and 14 still accepted: `have_max` needs 17); FLXP_DWM_FRAME_METRICS gained bMaxValid, bMaxGlowNone, crMaxText,
crMaxInactiveText, crMaxGlow. The header (CaptionLook) takes theme.ini's `text_max_active`, `text_max_inactive`,
`glow_max` and `maxcaption.shadow.1/.2` (msstyles MAXCAPTION's shadow, drawn by lhwm on its maximised caption), the
metrics' 14..16 win; without either the maximised look is the normal one. While IsZoomed the title and crumbs use them
(glow on glass as well). `_NET_WM_STATE` changes now reach the toplevel as WM_FLXP_DWMSTATECHANGED (0x03F3; sys
WindowState), the header repaints (buttons: the navigation bar's MAX/MAX_INACTIVE variants as before). Lab: the lhwm
binary in x11/lhwm was older than its source (14 items); a build of the current source (output in /tmp/make/lhwm)
publishes 18. doc/header-maximised.png (2x, aero4074 + picom): an xterm's maximised caption (lhwm's own white text), our
header maximised active (white, no glow) and inactive (grey), restored active/inactive (dark with the white glow).
Nothing more is needed from lhwm.

**Task band preview**: one picture selected -> its thumbnail (shell/thumbs Loader, one thread, the 128 px cache fitted
into 96, never enlarged) in place of the big icon, centred in the 96 px box, async (WM_APP+0x31 from the worker);
folders and non-pictures keep their icon. The band keeps its 104 rows (no jump when the selection changes); the demo's
128 px preview would need ~150. doc/taskpane-preview.png.

**Icon sizes**. What the files have: 4074 shell32 (the longhorn theme): 16/24/32/48 for nearly every icon, 256 px
(4, 8 and 32 bpp) for #1 document and #4 folder, #2 only 16/32; no 64/96/128. imageres.dll in 4074 has only PNGFILE
resources (no icon groups), not used. XP's .ico files: up to 48.
- w32 best_icon (flxp change of CURSORICON_FindBestIcon): the exact size, else the smallest larger one, else the
  largest; Wine/Windows took the nearest (a 64 request from 48/256 blew up the 48; ties went to the first entry).
  Shrinking a 32-bit icon (make_icon) averages the covered source pixels (alpha-weighted box filter) instead of
  nearest-pixel sampling; enlarging is unchanged.
- `LR_FLXP_NATIVESIZE` (flxp_shell.h): LoadImage keeps the chosen image's own size; uiicons::native_size(key, want).
- Desktop: `desktop_icon_size = 32|48` (XP's Effects > "Use large icons"; uiicons::init(large_px), the grid uses it).
- Thumbnails view, non-picture tiles: 48 as XP, 64 (at most two thirds of the tile, 96 for tiles >= 144) when the theme
  has a larger image (Longhorn's document); folders at the tile size now come from the 256 px folder, shrunk.
- Task band: the folder icon at 96 from the 256 px image (the band has no room for 128). Start panel 32/48 unchanged.
- doc/icons-sizes.png: the desktop at 48 and the Thumbnails view with the band, longhorn icons.
XP mode (luna, classic, xp icons, Folders open) pixel-identical to HEAD.

**Luna start button bottom**: Start::Button is 99x33, sizing margins 6 52 13 14: a 31-row green body over 2 rows of
(50,141,197)/(42,110,226), lighter than the bar's own bottom rows (30,80,196)/(25,65,165). Squeezed into the 30-row bar
those 2 rows showed as a blue line under the button. The themed button is now at least the image's own height (33) from
the bar's top and clipped by the bar (Luna: the last visible row is the body's (50,134,50)); 4051-vm-now.png's Slate
button matches the unsqueezed image best (rows 1..29). XP's own code sizes it max(ideal, task button height) (tray.cpp
_AlignStartButton), which does not explain this; the image's design does. Vertical bars unchanged. Width stays 96 (XP 97:
GDI's hdmx advances at 19 ppem; using hdmx in src/gdi is not a one-liner, left). doc/taskbar-start.png (3x): 4051, ours
slate4051, ours Luna.

**Themes, presets, no wallpaper** (lux/lhwm author's notes):
- The start panel's/menu's Theme cascade lists the registry entries, a separator, then the user presets
  (shell::store::theme_list); choosing one goes through shell::store::apply_theme (settings.ini, `_LH_THEME`, notify).
- `_LH_THEME_RESOLVED` (lhwm/lux: the registry section a preset in `_LH_THEME` resolves to) wins for a preset's base
  section when `_LH_THEME` still holds that preset; our own resolution is the fallback, and a preset file we cannot read
  is followed with the resolved section. A preset is never reported as an unknown session theme (`preset X: no base
  theme`). w32 theme watches `_LH_THEME_RESOLVED` too and re-applies when the base changes.
- `wallpaper = none` (settings.ini [desktop], flxp.ini, or `_FLXP_WALLPAPER` "none"): flxp-desktop paints only the
  background colour (#3a6ea5), the theme's wallpaper is not used.

## Views: the icon mix-up, Ctrl+1..5, views remembered per folder (2026-10-25)

"Changing the view sometimes shows wrong icons, F5 fixes them": in Thumbnails
every item's iImage is an index into the thumbnail image list (thumbview's
squares, set per item and as thumbnails arrive). Leaving the view only put the
32 px list back as LVSIL_NORMAL, so Icons/List/Details showed whatever icon
had the thumbnail's index in the small/large lists (uiicons keeps the two in
step, so it was never the small/large pair) until F5 refilled the list.
set_view now gives every item its icon index back (by lParam -> g.items)
right after thumbview::leave, before the new view paints; selection and
scroll position stay.

Ctrl+1..5 (anywhere in the window; XP has no such keys, the View menu shows
them as accelerator text): 1 Thumbnails, 2 Icons, 3 List, 4 Details, 5 Small
Icons - the fifth is 2000's LVS_SMALLICON (XP's fifth, Tiles, does not exist
in Wine's listview), now also in the View menu between Icons and List (not in
the Views drop-down or the context menu, which the desktop shares).

Views are remembered per folder (`remember_views = 1`, the default; 0 turns
it off): every view change from the menus, the Views button, the 4051 bar or
the keys stores `path<TAB>view` (thumbnails|icons|smallicons|list|details) in
$XDG_CONFIG_HOME/flxp/views (rewritten whole, atomically); navigating to
another folder applies its stored view before the list is filled (no double
fill: set_view(view, navigating) leaves the refill to refresh()). A folder
with no stored view keeps the current one.

"Back changes the view, Up does not" (fixed 2026-10-25): every way of changing
folder (Back, Forward and their history menus, Up and Backspace, tree, breadcrumb
segments and drop-downs, address bar, places pane, double-click/Enter, the
command line, bookmarks) already went through the one navigate() and
views::lookup; none was missing. The asymmetry was the rule "a folder with no
stored view keeps the current one": from Home (never set, Icons) into Pictures
(stored Thumbnails), Up kept Thumbnails for Home, and Back into Pictures then
"changed" it. Now each window also remembers, for the session only (not
written), the view a folder was shown in when it is left; lookup = the stored
view, else that one, else keep the current view (folders not yet seen in this
window). And the views file is re-read when its mtime/size/inode change, so a
view set in another explorer window applies here, and store() no longer writes
back a stale map over the other window's entries. FLXP_DEBUG prints `navigate
PATH view V style S` after each navigation. Tested on :91 (a unset, a/b
thumbnails): double-click b -> thumbnails, Up -> a details (was thumbnails),
Back -> b thumbnails, Back -> a details, address a/c (never seen) -> details
kept, another writer sets a = list -> Up from c shows List.

## Popups on top after a nested dialog; the task buttons; the start panel (2026-10-25)

### Combo boxes after a nested modal dialog (todo: "combo boxes don't always open")
- Repro (flxp-settings, Themes, under lhwm): Save As..., close it (Esc, its
  Cancel or the WM's X), click the Theme combo: nothing seems to happen.
- Cause: the combo did drop down (focus, CBF_FOCUSED and the capture were all
  right): its list is an override-redirect toplevel that was created, and
  first mapped, on top; on the next map X keeps its old stacking position,
  and the WM had raised the property sheet (and its frame) above it when the
  modal dialog went away. The list was mapped underneath the dialog.
- Fix (toolkit): sys::show_window maps override-redirect windows with
  XMapRaised (win32 shows a popup on top; the combo list and menus even ask
  for HWND_TOPMOST), and win::set_window_pos raises a visible unmanaged
  popup on HWND_TOP/HWND_TOPMOST without SWP_NOZORDER (it ignored z-order
  for toplevels; managed windows are left to the WM, desktop/dock windows
  never raised).
- Also: IsWindowEnabled is the window's own WS_DISABLED, as on win32 (it
  followed the parents, so every control painted grey while a modal dialog
  was up, and the Save As button stayed grey after it until repainted).
  Input still follows win::is_enabled (parents included).
- Checked: Save As + Esc / Cancel / WM close, then the combo (opens);
  flxp-filedialog --save: New Folder, then the replace prompt (No), then
  Save as type (opens); explorer Properties > Change... (Open With) twice.

### The streak across the task buttons (todo: "very visible with slate")
- 4074 Slate's TaskBand::Toolbar BUTTON is 26x28 per state with SIZINGMARGINS
  4,4,16,12: top + bottom margins are the whole image, the source middle is
  0 rows. The buttons are the bar's 30 rows, so 2 middle rows are stretched
  from nothing: Wine's nine-grid (UXTHEME_DrawImageBackground) drew nothing
  there and the bar showed through, rows 16..17 of every button (ut-dump
  --render showed it too, alpha 0). Not the rebar/toolbar or custom draw.
- XP (ninegrid2.cpp DNG_StretchCol with cySrcTileSize 0) starts that stretch
  at the row on the boundary, the top part's last row. Now draw.c (flxp:)
  uses that row (column) as a 1-pixel source middle when the margins leave
  none. Luna and 4051 Slate (non-empty middles) unchanged. doc/taskbar-slate.png.

## Per-theme settings, live icon sets, the wallpaper list, the desktop menu (2026-10-25)

**Per-theme model** (x11/SETTINGS.md "Per-theme settings", THEMES.md "Per-theme shell settings"). The Appearance values
and the icon set are the theme's: `store::theme_value(theme, key, registry)` = settings.ini `[override:NAME] key` > the
preset's own (`[Shell] taskbar/start`, `[Composition] composition/animations`, `[Desktop] icons`, `[Explorer] frame`) >
the registry entry (`taskbar`, `start`, `animations`, `flxp_icons`, `flxp_frame`) > the global settings.ini key (lux's
fallback, unchanged) > built-in (avalon, flxp, follow, 4074plus, xp, auto). Chosen over auto-created `themes/<name>.theme`
presets because a preset named like a registry section never resolves (registry first) and would list twice; one
settings.ini section per theme needs no new file and rides on the existing notify/reload. `store::set_theme_values` writes
into the preset or `[override:NAME]`, removes a key equal to the theme's own default (and an emptied section; `Ini::remove`),
and blanks a flxp.ini `icons`/`frame`/`taskbar_kind`/`start_menu` that would beat it. `store::value_for` (under
`shell::setting`) resolves those four flxp keys through the chain for the session theme, so readers changed nothing; the
explorer's frame: an explicit `auto` now skips the theme's `flxp_frame`. themes.ini gained `taskbar = classic`, `start = flxp`,
`flxp_icons = xp` for the four Luna entries and `taskbar = avalon`, `start = lux` for the six Longhorn ones.

**Display Properties**: the Appearance tab and the icon combo show the selected theme's effective values (a flxp.ini value
if one forces it) and are reloaded when another theme is chosen; Apply stores what differs for that theme only (the preset
when one is selected - so Animations works with a preset now), global keys only for wallpaper, position and smoothing.
Save As writes only keys that differ from the base. `_LH_THEME` is written once per theme change (`[theme] name` too: it
is no longer in the set() batch before apply_theme). `_LH_THEME_RESOLVED` is parsed as `name=...;base=...` (`resolved_field`;
a bare name from older lux still accepted): its `base` counts only when its `name` is the current `_LH_THEME`.

**Live icon sets**: `shell::refresh_icon_theme()` re-reads the icons setting and selects the set when it names another
(generation counter); `uiicons::sync()` reloads every image-list index in place from the new set, so list views, the tree,
the address box and the places pane keep their items and only repaint. The explorer calls it on WM_THEMECHANGED and
`_LH_SETTINGS_CHANGED` (also re-checks the frame mode and swaps the toolbar strip and its button indices), flxp-desktop on
both, the start panel when it opens; the taskbar re-checks its start-menu provider on `_LH_SETTINGS_CHANGED`. Not live: an
open Thumbnails view's own list (until the view is re-entered), the task band's big icon (next folder), file dialogs
already open. Tested on :90 (lhwm luna, desktop + explorer + dialog, scratch HOME with the user's settings.ini where
`[desktop] icons = 2k`): luna showed XP icons; Slate + Apply -> Longhorn header, Longhorn icons on the explorer and the
desktop at once, Appearance showed Avalon/lux/Longhorn header; Desktop > icons Windows 2000 + a Bliss picture + Apply ->
2k icons in both windows and the wallpaper, `[override:slate4074] icons = 2k`; back to Luna -> XP icons, XP frame;
animations on preset "My Luna" -> `[Composition] animations = 5048` in the preset, back to 4074 plus -> removed; `xprop
-spy` saw one `_LH_THEME` per change.

**Wallpaper list** (Desktop tab, XP's IDD_BACKGROUND layout): the monitor preview over a list "(None)", the theme's
background ("ascent (theme background)", value empty), the user's earlier choices (flxp.ini `wallpapers`, `;` list, 10, set
on Apply for a picture outside the directories), the current one, then every BMP/JPEG/PNG directly in `wallpaper_dirs`
(flxp.ini, `:` list; default /u/aap/cfg/wallpaper/longhorn_wallpapers, /u/aap/cfg/wallpaper/xp_wallpapers,
~/Pictures/Wallpapers when they exist), names without extensions, the theme's image icon. Selecting previews; Position is
disabled for (None) (`wallpaper = none`); Browse... adds to the list. The edit field and Theme's button are gone.
doc/settings-desktop.png, doc/settings-appearance.png (luna).

**Desktop menu** as XP's: Arrange Icons By (Name/Size/Type/Modified, Auto Arrange, Align to Grid), Refresh, Paste, Paste
Shortcut (new: a symlink per copied item, "Shortcut to NAME" on a clash), New, Open Terminal Here (flxp's one extra),
Properties = Display Properties on the Themes tab, as XP. Add to Bookmarks and Desktop Background... left the desktop's menu
(the Desktop tab and Set as Desktop Background cover it).

## Themed item selection: Explorer::ListView LISTITEM (2026-10-25)

lh4051-224221.png from the todo does not exist; lh4051-225221.png is the one
with the selection (225228/225233 show the same item selected without the
focus). Codex (one call, 2x crop): a 214x52 rounded rectangle around icon and
both text lines (7 px before the icon), 1 px peach edge (246,210,171), a pale
inner highlight, a vertical cream-to-peach gradient (248,238,208) ->
(248,213,166), the dotted focus rectangle 1 px inside the edge, text black,
icon not tinted. That is exactly the Longhorn msstyles' part: slate/jade
4051..4074 (and the Vista-era aero.msstyles) have class
`Explorer::ListView` with LISTITEM(HOT) #177, (SELECTED) #176,
(SELECTEDNOTFOCUS) #178 (grey, the unfocused screenshots), (HOTSELECTED) #179,
IMAGEFILE stretched with 4 px sizing margins; ut-dump --render of SELECTED at
214x52 reproduces the screenshot. XP's luna has only `ListView` (no LISTITEM).

- The explorer calls SetWindowTheme(list, L"Explorer", NULL), as Vista's
  Explorer does; uxtheme then opens Explorer::ListView where it exists and
  plain ListView otherwise.
- listview.c (flxp:, patch regenerated): LISTVIEW_ThemedItemState gives the
  LISS_* state when the window theme defines LVP_LISTITEM: selected with the
  focus SELECTED (HOTSELECTED under the mouse), without it SELECTEDNOTFOCUS
  (SHOWSELALWAYS), drop target SELECTED, the hot item HOT. Such an item gets
  DrawThemeBackground(LVP_LISTITEM) behind it - icon view the item box (with
  a longer focused label), details the row with full-row select else icon and
  text, list/small icons icon and text width - instead of the highlight fill:
  prepaint_setup keeps the normal text colour and draws transparent, the
  image is drawn ILD_NORMAL without a background colour (no square behind a
  masked icon), the focus rectangle sits 1 px inside the part, and in
  Thumbnails the 3 px border-select frame gives way to the part.
- Hot tracking for themed items (without LVS_EX_TRACKSELECT): mouse moves
  hit-test and invalidate the old/new nHotItem, TrackMouseEvent(TME_LEAVE),
  WM_MOUSELEAVE clears it.
- XP themes: no LISTITEM part, every path as before; checked pixel-identical
  under Luna against the previous binary (Icons, Details, List, with a
  selection and the mouse over another item). doc/selection-4051.png: 4051
  focused and unfocused, ours on slate4051 (sub2 selected, d.sh hot).

### The start panel: round corners, the user pane per theme, hover, icons (todo: flxp-startmenu)
- Round corners: SetWindowRgn now shapes a toplevel (shim -> win::set_shape ->
  sys::set_shape, XShape bounding from the region's rectangles; the region is
  the system's afterwards; child windows ignored). The panel draws its panes'
  backgrounds over black and over white once laid out (startpanel
  apply_shape): in each 12x12 corner square the pixels the parts leave less
  than half covered, contiguous from the edge, are cut row by row. So the
  corners are the theme's: Luna's and Slate's user pane, Jade's/Aero's lists
  (top) and log off pane; the classic panel stays rectangular.
- User pane per theme: its height is the USERPANE part's DEFAULTPANESIZE
  (deskhost.cpp sizes the panes from them; 64 in Luna/Slate), 0 when the
  style has no USERPANE part (4074 Jade and Aero: StartPanel 380x376) - and
  the XP backports flxp loads for them (themes/xp-backport/jade) define
  USERPANE 380x0 with USERPICTURE false. No picture, name or pane then; the
  lists start at the top.
- Hover while a flyout is open: the flyout's menu loop has the capture, so
  the panel follows the pointer through a WH_MSGFILTER hook (MSGF_MENU
  mouse moves on the panel, not on a menu over it): the item under it is
  highlighted at once; after SPI_GETMENUSHOWDELAY (400) on another item the
  flyout ends (EndMenu) and, if that item is a cascade (Control Panel, All
  Programs), it opens; back on the open item nothing happens; once closed,
  All Programs opens again after the delay (as before).
- Programs icons: the All Programs / classic Programs menus carry 16 px
  MIIM_BITMAP images: the .desktop Icon= through the panel's lookup
  (startpanel::menu_bitmap: premultiplied 32 bpp DIB; else the icon set's
  "exe"), the category submenus the icon set's "folder". menu.c (flxp:)
  draws a 32 bpp item bitmap with GdiAlphaBlend and no NOTSRCCOPY when the
  item is highlighted (Wine 7 blitted it, inverted when hot). Bitmaps live
  until the next Programs menu is built.
- Tested (lab Xvfb + lhwm): slate4074, jade4074, aero4074, luna: corners cut
  (Luna/Slate top, Jade top), no user pane in Jade/Aero; hover sequence
  All Programs -> Terminal (highlight, flyout gone after 400 ms) -> Control
  Panel (its cascade) -> All Programs (reopens). doc/startpanel-jade.png.
- Seen, not changed: flxp-taskbar ignores SIGTERM while a start-panel
  flyout is open (the menu loop does not return to the main loop).

## The breadcrumb as 4074 builds it: segments, drop-downs, the edit field (2026-10-25)

lh4074-224731.png shows the 4074 bar while a drop-down is open; at rest (the
other screenshots) it is plain text with separators, which is what we drew.
Codex (2x crop) described the construction; a second call compared ours:
- a field behind all segments, from before the first crumb to the bar's end,
  27 rows: darker than the header (#7a7a7b on 4074's grey), a light top rule
  (150), a dark bottom (60), and at its right end a 15 px drop button (white
  at the top to (203,204,218), a dark chevron).
- every crumb a split button: the name and an arrow part over the separator
  after it. Open, the segment is a sunken 23-row box: (75,75,75) edge, light
  inner top line (203,211,214), the name part (164,172,174) -> (122,130,132)
  downwards, the arrow part the other way, a dark divider, the separator's
  triangle replaced by a light 7x4 down arrow.
- its list: flush under the field, left-aligned with the segment, the folders
  beside that crumb (its parent's subfolders, here Local Disk's), the crumb
  itself bold; the last crumb shows no arrow at rest.

Ours (header.cpp "the breadcrumb bar as 4074 builds it"):
- At rest nothing changes. The field shows while the mouse is over the bar,
  a list is open or the path is being edited; the field and segments are
  drawn translucent (AlphaBlend) so they work on glass, the navigation bar
  and the pixmap alike.
- Segment hot: a light raised box with a divider before the arrow; pressed or
  open: the sunken 4074 box; the last segment's arrow part appears only when
  hot (after the name, 12 px). When the session msstyles has
  BreadcrumbBarToolbar::Toolbar (or BB::Toolbar) SPLITBUTTON and
  SPLITBUTTONDROPDOWN with its glyph (aero.msstyles: 6 states each, sizing
  margins 12,11,4,7 and 2,4,4,4) those are nine-gridded instead
  (navbar::Skin crumb_btn/crumb_drop/crumb_down, loaded with the buttons;
  4051/4074 slate and jade have none).
- Name part: click navigates (as before; no underline any more). Arrow part:
  on the press a popup of the folders beside the crumb (enumerated, sorted by
  name, at most 250, the crumb bold via SetMenuDefaultItem), picking one
  navigates. The field's drop button: the Back history menu (we keep no typed
  address history). The "..." segment works like the others (the last hidden
  folder).
- Edit mode: Ctrl+L, or a click on the field's empty part: an Edit over the
  field with the current path selected (white field); Enter navigates (~
  expanded, header_navigate beeps on a bad path), Escape or losing the focus
  leaves; navigating anywhere ends it. Ctrl+L does nothing in the XP frame.
- The bar is now HTCLIENT over the field (a control, as 4074's): the caption
  drag works on the title row and the rest of the header.
- Layout: the crumbs leave room for the last segment's arrow and the drop
  button, so a long path collapses into "..." one crumb earlier than before
  (the only change at rest).
- GradientFill(GRADIENT_FILL_RECT_V) came out bottom-to-top here (Codex's
  samples); the bar draws its vertical gradients row by row instead.
- A long drop-down is shifted up by the menu code to fit the screen (seen with
  36 folders on an 800 px screen), as Windows does.
doc/breadcrumb-4074.png: 4074, ours on slate4074 (list open on the last
segment, hot segment), ours on aero glass (theme split buttons, hot).

## Cut/Copy/Paste over the X CLIPBOARD (2026-10-25)

Cut and Copy now also put the files on the X CLIPBOARD the way GNOME/KDE file
managers do, and Paste takes files from there, so flxp windows (separate
processes) and other file managers exchange them. Done at shell level, not in
the shim's OpenClipboard/SetClipboardData (process-local, would have needed
SelectionRequest handling and a blocking conversion inside src/sys's event
loop, which other work is changing): src/shell/xclip.{h,cpp}, no win32.
- Owner: a thread with its own Display connection and a 1x1 unmapped window,
  started on the first Cut/Copy; set_files() hands it the paths through a pipe,
  it takes CLIPBOARD with a server timestamp (zero-length property append) and
  answers SelectionRequest for TARGETS, TIMESTAMP,
  x-special/gnome-copied-files ("copy"|"cut" then file URIs, newline
  separated), text/uri-list (CRLF), application/x-kde-cutselection ("1" for
  cut), UTF8_STRING/text/plain;charset=utf-8/STRING (the paths). No INCR (a
  file list is small). SelectionClear marks the clipboard lost.
- Paste: if another client owns the CLIPBOARD (or ours was lost), a second
  short-lived connection converts x-special/gnome-copied-files, else
  text/uri-list (+ the KDE cut flag), 1 s timeout each; file: URIs only
  (shell::path_from_uri). After a cut is pasted (moved) we give the clipboard
  up, as GNOME does.
- Paste is enabled for our own files or whenever another client owns the
  CLIPBOARD (verbs::can_paste; the target types are only looked at on Paste,
  which beeps if there are no files). Cut items lose their ghosted look once
  another client takes the clipboard (verbs::is_cut checks xclip::lost()).
- Tested in the lab with xclip: TARGETS/gnome/uri-list read back from an
  explorer after Ctrl+A Ctrl+C; a gnome "cut" set by xclip pasted (moved) into
  an explorer; files copied in one explorer process pasted in another.
- The desktop's Paste Shortcut takes verbs::clipboard_files() like Paste.
- Bug fixed (2026-10-25, "it was always using the previous clipboard"): a
  process that pasted another owner's files kept them in verbs' g_clip, and as
  it had never owned the CLIPBOARD, xclip::lost() never became true, so every
  later Paste in that window reused the first list until it did a Cut/Copy of
  its own. Now xclip::ours() (our last set_files is what the CLIPBOARD holds:
  owned, or the take-over still pending in the owner thread, tracked with a
  request/done counter under the lock) decides: if not ours, Paste
  (verbs::clipboard_files) always reads the selection afresh. Test:
  src/test/headless/clipboard.sh (two explorer processes, copy A paste, copy B
  paste; xclip shows the offer each time; FAIL before, PASS after).

## "All themes", the final per-theme chain, Properties greyed on the desktop (2026-10-25)

lux's author: a global key alone is pointless while the registry has defaults (`taskbar = avalon|classic` on every entry
comes before `[taskbar] kind`), so choosing the taskbar "globally" did nothing. The chain gained a layer for an explicit
"all themes" choice, between the theme's own value and the registry:

1. settings.ini `[override:NAME] KEY` (Source::Override)
2. the preset's own key when NAME is a preset (Preset)
3. settings.ini `[override] KEY`: chosen for all themes (AllThemes) - new
4. the registry entry, a preset's base's (Theme)
5. the global key (`[taskbar] kind`, `[start] menu`, `[desktop] icons`, `[explorer] frame`, `[theme] composition/animations`) (Global)
6. the built-in default (Default)

A flxp.ini key of the same meaning (`icons`, `frame`, `taskbar_kind`, `start_menu`) and `FLXP_<KEY>` still beat it all.
`store::theme_value(theme, key, registry, &source)` reports the layer; `theme_default(..., without_all)` is 3-6 or 4-6.

**Display Properties**: every per-theme value (Appearance: frame, taskbar, start menu, composition, animations; Desktop:
icons) has a scope combo next to it and a line under it saying where the value comes from ("Set for this theme", "Set in
this theme's file", "Set for all themes", "The theme's default (NAME)", "The global setting", "The built-in default",
"flxp.ini sets it for every theme", "Changed: Apply stores it"). Scopes:
- This theme only: as before (`set_theme_values`: the preset or `[override:NAME]`, the key removed when it equals 3-6).
- All themes (`store::set_all_themes_values`): `[override] KEY` and the global key = value, the key removed from every
  `[override:NAME]` and every preset, flxp.ini's key blanked. Written only when the row was touched (it rewrites every
  theme's choice). The scope shows "All themes" when the value comes from layer 3.
- Theme defaults: `[override] KEY` and the per-theme keys removed (the global key kept); the combo shows 4-6 at once.
After Apply the rows are reloaded so the source lines show the new state.

**Readers**: nothing read settings.ini or flxp.ini past the chain: flxp-taskbar's kind at start-up (`taskbar_kind`),
start-menu provider (`start_menu`, re-checked on `_LH_SETTINGS_CHANGED`), `pinned` (global `[start] pinned`, not per theme)
and its icons (`icons` via shell/icons' `theme_setting`), flxp-desktop's icons and wallpaper (`wallpaper`/`wallpaper_mode`
are global `[desktop]` keys) all go through `shell::setting`/`file_setting` -> `store::value_for` -> the chain for the
session theme. Changed: `value_for` with no session theme now also runs the chain (layer 3, then the global key).

**Desktop Properties greyed**: verbs::build_background_menu greys New, Open Terminal Here, Add to Bookmarks and Properties
when the host has no folder (`current_dir()` empty), and the desktop's folder is `~/Desktop` only when it exists - aap has
none. The desktop now enables Properties itself (it is Display Properties, no folder needed). `settings_program()` also
searches $PATH explicitly (next to the binary, the build tree's build/settings, then $PATH; the start panel and the classic
menu use the same `launch_settings`), and `make install` installs flxp-settings (install-settings; before, only when it
happened to be built).

Tested (lab :90, lhwm slate4074, scratch HOME with the user's settings.ini, `[override:slate4074] taskbar = none, icons =
2k`, `[override:aero4074] icons = xp`): flxp-taskbar started menu-only; Appearance > Taskbar Classic + All themes + Apply
-> `[override] taskbar = classic`, `[taskbar] kind = classic`, both overrides' taskbar gone, the row says "Set for all
themes"; a restarted flxp-taskbar is the bar. Desktop > icons Longhorn + All themes -> `[override] icons = longhorn`, the
per-theme icons gone; explorer, desktop and the start panel show Longhorn icons, still after `_LH_THEME = aero4074`.
Theme defaults -> `[override] icons` removed, the row "The theme's default (slate4074)". Properties: installed
(`make install PREFIX=<scratch>`, PATH without it, no ~/Desktop) and from the build tree, both start flxp-settings
(inst/bin/flxp-settings, build/desktop/../settings/flxp-settings); the installed start panel's Control Panel > Display
Properties... too.

## The Sidebar group: lux's [sidebar] per theme (2026-10-25)

Display Properties > Appearance has a "Sidebar (lux)" group (always enabled: lux's sidebar also runs next to the
classic flxp-taskbar with `with_classic`, and `stacking` is about exactly that pairing): Mode (Full / Minimized /
Pinned), Stacking (moved, see "Sidebar stacking is global" at the end), and "Show the sidebar with the classic taskbar" (`with_classic`, 1/0). `layer` is left out (the spec
names only `above`, no list of values); `width`, `yield_bottom` stay unshown. Each has the scope combo and source line
of the other Appearance values and runs through the same chain (`store::theme_keys`):

| dialog | `[override:NAME]` / `[override]` key | preset key | registry key | global key | default |
| --- | --- | --- | --- | --- | --- |
| Mode | `sidebar` | `[Shell] sidebar` | `sidebar` | `[sidebar] mode` | full |
| ~~Stacking~~ | (2026-10-26: not per theme any more, global `[sidebar] stacking`) | | | | top |
| with the classic taskbar | `sidebar_with_classic` | `[Shell] sidebar_with_classic` | `sidebar_with_classic` | `[sidebar] with_classic` | 0 |

"This theme only" writes `[override:NAME]` (or the preset), "All themes" `[override]` plus the global `[sidebar]` key,
"Theme defaults" removes them; Save As carries them into the preset's `[Shell]`. Apply sends `_LH_SETTINGS_CHANGED`
(hint `theme`) as for the taskbar. Note for lux: these keys are stored under the same per-theme layers as
`taskbar`/`start` - a reader of only `[sidebar] mode/stacking/with_classic` sees a per-theme choice only when "All themes"
was used; lux's `S.theme_value` needs the three rows above (no registry entry sets them yet). The Appearance page grew to
288 DLU high (the sheet with it). Tested on :92 (scratch HOME, the user's settings.ini, `_LH_THEME` slate4074 then
luna): Stacking -> Sidebar full height + the checkbox + Apply -> `[override:slate4074] sidebar_stacking = top,
sidebar_with_classic = 1`; Mode Minimized + All themes -> `[override] sidebar = minimized`, `[sidebar] mode = minimized`;
the rows then read "Set for this theme" / "Set for all themes". doc/settings-appearance.png (luna).

## How XP picks a folder's view (XPSP1 source, 2026-10-25)

Paths under NT/shell. "Remember each folder's view settings" is the Folder
Options > Advanced item whose value is `...\Explorer\Advanced\ClassicViewState`
(inverted: checked = 0; selfreg.inx:2389-2398, text IDS_ADV_FOLDER_ClassicViewState
shell32/ids.h:3555; the string itself is not in the tree, the wording is from
memory). There is no `fRememberViewSettings` in this source. "Remember" on =
ClassicViewState 0 = per-folder bags; off = Win95 behaviour.

Lookup when navigating inside a window (remember on):
1. basesb.cpp:1496 CBaseBrowser2::CreateViewWindow, called from
   _CreateNewShellView (basesb.cpp:1518, :1613); shbrows2.cpp:6651
   CShellBrowser2::CreateViewWindow first calls _UpdateFolderSettings(pidlPending)
   (shbrows2.cpp:1257), which fills `_fsd._fs` (the FOLDERSETTINGS passed to
   IShellView::CreateViewWindow, defview.cpp:11326-11330 takes ViewMode/fFlags from
   it; DefView does not choose the mode itself) and `_fsd._dwViewPriority`.
2. Remember on (`!_fWin95ViewState`): _FillIEThreadParam (shbrows2.cpp:1225) reads
   the folder's bag `GetPropertyBag(SHGVSPB_FOLDER)` = HKCU\...\ShellNoRoam\Bags\<n>\Shell
   keyed by a BagMRU slot per pidl (shlwapi/propbag.cpp:1984, shbrows2.cpp:11871).
   Read chain per property (propbag.cpp:2403-2437): this folder's bag, nearest
   "Inherit" bag (only written by Customize > "also apply to subfolders",
   shell32/fldrcust.cpp:710/796), user defaults, folder defaults, global defaults.
   The mode is VS_PROPSTR_MODE (_ReadSettingsFromPropertyBag, shbrows2.cpp:985-1030);
   a bag counts as a hit when WPFLAGS exists -> priority CACHEHIT (0x50, :1093).
3. No bag: v_GetDefaultSettings (shbrows2.cpp:1748-1794): ViewMode/VID/priority of
   the global default g_dfs: factory default priority CACHEMISS 0x30
   (browseui/util.cpp:207), or after "Like Current Folder"/"Apply to All Folders"
   USEASDEFAULT 0x43 (shbrows2.cpp:661-700, SetAsDefFolderSettings; also Ctrl held
   on close, :820-ish). A bag older than the default's dwDefRevCount is replaced by
   the default (shbrows2.cpp:1104-1118).
4. shdocvw/fldset.cpp:340-430 then arbitrates by priority: the folder's own
   shell-extension / folder-type view (desktop.ini, Pictures, Music: SHELLEXT 0x40)
   beats CACHEMISS but not CACHEHIT or USEASDEFAULT; INHERIT (0x20, the previous
   window view) is only reached if everything above is lower, which with remember
   on never happens (default is 0x30), DESPERATE (0x10) last. Constants:
   shell/inc/fldset.h:10-19.
   So with remember on an UNVISITED folder does NOT inherit the view of the folder
   you came from; it gets the default (Icons/Tiles, or the Apply-to-All view) or
   its folder-type view.

Remember off (ClassicViewState=1): _UpdateFolderSettings skips the bag and keeps
the current view's FOLDERSETTINGS (`GetCurrentInfo(&_fsd._fs)`, priority INHERIT,
shbrows2.cpp:1259-1290), the shell-ext priority is lowered to 0x15, under INHERIT
(fldset.cpp:395-402): every folder, visited or not, opens in the view you are in.
(The first navigation of a new window still reads the bag, :1295.) The bag is
still written on leaving; only the read is skipped.

Saving: always on leaving a folder, not only when the user changed the view.
CShellBrowser2::ReleaseShellView (shbrows2.cpp:6614-6625) -> _SaveState
(:720-754) -> _PropertyBagSaveState (:755) writes the window/frame part, then
CBaseBrowser2::ReleaseShellView calls psv->SaveViewState() (basesb.cpp:640) ->
CDefView::SaveViewState (defview.cpp:12662) -> CViewState::SaveToPropertyBag
(viewstate.cpp:632, Sync reads `_fs.ViewMode`, :1207) -> Mode, scroll, sort,
columns, icon positions. Also at window close (shbrows2.cpp:6873) and DefView
refresh (defview.cpp:10684, 10731). Skipped when !CabinetState.fSaveLocalView,
REST_NOSAVESET, or the window is invisible (:728-736). Every way of leaving is the
same ReleaseShellView: Back, Forward, Up, address bar and double-click are
indistinguishable (SBSP_* only choose history handling, basesb.cpp:3886-3912), so
a visited folder always returns in the view it was left in.

Apply to All Folders / Reset All Folders: DVID_SETASDEFAULT (defview.cpp:15002,
_SaveGlobalViewState :12706) writes the current DefView state as the stream
HKCU\...\Explorer\Streams\Defaults\<folder CLSID> and DELETES the whole
ShellNoRoam\Bags key, i.e. every per-folder view is forgotten.
DVID_RESETDEFAULT (:15014, _ResetGlobalViewState :12733) deletes Streams\Defaults
and Bags (shbrows2.cpp:10283 from the Reset All Folders button).
Bag limit: `BagMRU Size` default 400 (propbag.cpp:1967); the MRU evicts the least
recently used pidl's bag when a 401st folder is saved.

flxp compared with XP:
- visited folder (stored, or shown earlier in this window): same (flxp stores on
  change and keeps a session map of what was shown when left; XP persists on every
  leave, so a folder that was only looked at is also pinned. No visible difference
  without a default-view setting). Up/Back/forward: same in both.
- unvisited folder, remember on: DIFFERED. flxp kept the current view
  (Thumbnails from Pictures carried into a new subfolder), XP uses the default.
  Fixed: views::lookup now returns the default view (LVS_REPORT, flxp's startup
  view) for a folder with no stored and no shown view. flxp has no folder-type
  views (Pictures -> thumbnails) and no Apply-to-All, so default is a constant.
- remember off: same (lookup returns -1, the current view carries over).
- not done: BagMRU limit (flxp's views file is unbounded), Apply/Reset All Folders.

## Drag and drop: XDND 5 in the toolkit (2026-10-25)

Files are dragged between flxp windows and to and from other X programs with
XDND version 5 (freedesktop.org XDND spec), implemented once in src/sys
(x11.cpp, "drag and drop" section) so every wux program has it. Not OLE: a
lean API instead of DoDragDrop/RegisterDragDrop/IDataObject/IDropTarget.
- API (win/window.h, shim flxp_shell.h): `win::drag_files(src, paths,
  allowed, image, hotspot)` / `FlxpDragFiles(hwnd, paths, count, allowed,
  himl, images, nimages)` is DoDragDrop for local paths: called on
  LVN_BEGINDRAG/TVN_BEGINDRAG, it runs a modal loop (the app keeps painting)
  until the drop is done or cancelled and returns the DROPEFFECT_* the target
  performed. Every toplevel is a target (XdndAware set on all non-popup
  windows): the deepest window under the pointer gets WM_FLXP_DRAGOVER
  (return the effect a drop there would have, 0 = refuse), WM_FLXP_DROP (do
  it, return the effect done) and WM_FLXP_DRAGLEAVE (unhighlight; also after a
  drop), lParam = FLXPDROP / win::DropInfo (same layout, static_assert):
  target window, screen point, allowed effects, MK_SHIFT/MK_CONTROL/MK_ALT,
  the source's request, local flag, the paths, the source window if ours.
  def_proc and DefWindowProc hand the three to the parent (like
  WM_MOUSEWHEEL), so a frame answers for its controls. A disabled toplevel
  (modal dialog up) refuses.
- Source (sys::drag_begin): grabs pointer and keyboard on the toplevel, owns
  XdndSelection, sets XdndTypeList (text/uri-list,
  x-special/gnome-copied-files "copy\nURI...", UTF8_STRING,
  text/plain;charset=utf-8, STRING: the paths) and XdndActionList. The
  target is found from a snapshot of the root's mapped children (stacking
  order, the drag image skipped) descending with XTranslateCoordinates to
  the first window with XdndAware >= 3; Enter/Position/Leave/Drop as the
  spec says, one Position in flight (the newest is sent when the Status
  comes), a release waits for the last Status, Escape cancels, Shift/Ctrl
  changes re-send the position. The requested action follows the keys
  (Ctrl+Shift link, Ctrl or Shift copy, plain move). Timeouts: 2 s for a
  Status, 60 s for XdndFinished (a flxp target may ask questions). The drag
  ends inside wait_event (a Nothing event wakes the loop).
- Cursor: XP's OLE drag cursors drawn as 1-bit pixmap cursors (arrow + dotted
  rectangle for move, + a plus badge for copy, a curved-arrow badge for link,
  the "no" circle) on the active grab (XChangeActivePointerGrab).
- Drag image: the dragged items' icons (up to four, stacked diagonally) in an
  override-redirect window with an empty input shape, under the pointer at the
  icon's centre: ARGB at 60 % when a compositing manager owns _NET_WM_CM_Sn,
  else XP's no-alpha fallback, the opaque pixels dithered (every other pixel,
  bounding shape) so the target shows through.
- Target (sys translate): XdndEnter records types/actions; Position becomes
  EventKind::DragOver with the keys read from the server (XQueryPointer),
  Drop DragDrop, Leave DragLeave; win answers with drop_status/drop_finish.
  The data is fetched once per drag on the first position that needs it
  (XConvertSelection of text/uri-list, else the gnome type, 1.5 s; retried
  at later positions if it failed); from our own process the source's list
  is taken directly. file: URIs only (localhost or our host name). GTK sources
  list only the action their keys chose (Shift: move); copy and link never
  touch the source's files, so they are always allowed to our keys.
- Effects (shell::drop_effect, XP's rules plus the user's Shift): Ctrl or
  Shift copies, Ctrl+Shift links (symlink, "Shortcut to x" if taken), plain
  moves on the same filesystem (st_dev) and copies across; refused into
  itself or below, a move/link to the folder the files are in, an unwritable
  folder. The copy/move is shell::paste_items (the Paste behaviour: "Copy of
  x" when the name is taken); no progress dialog.
- Explorer: the list (a folder item takes the files and is drop-highlighted,
  LVIS_DROPHILITED; elsewhere the folder shown), the Folders tree
  (TVGN_DROPHILITE), the Longhorn breadcrumb segments (hot while over them),
  the places pane (since 2026-10-26: a place takes the files, not the Trash;
  on the Bookmarks group folders become bookmarks, files are refused - see
  "The user's 2026-10-26 notes"). Sources:
  list selection (the grabbed item first), tree nodes. After another
  process moved our files the source's folders are refreshed. Desktop: a
  folder icon takes the files; its own icons dropped on the desktop are
  repositioned (by the pointer's travel since the grab; system icons alone
  still follow the pointer as before); other files go into ~/Desktop with
  their icons at the drop point. File dialog: its list as in the explorer,
  and a source.
- Wine listview (flxp:, .patch regenerated): LVIS_DROPHILITED is drawn like a
  focused selection (CDIS_SELECTED|CDIS_DROPHILITED in custom draw, the item's
  state read with it, ILD_SELECTED icon); Wine drew nothing.
- Tested headless (src/test/headless/dnd.sh, FLXP_DEBUG_DND=1 traces the
  protocol): explorer to explorer move, Shift copy, Ctrl+Shift link, Escape;
  a GTK3 drop target (python3-gi script: gets the text/uri-list) and GTK3
  source into flxp; pcmanfm 1.3 both ways (flxp to pcmanfm moves, pcmanfm to
  flxp with Shift copies); explorer to the desktop and back, desktop icon
  repositioning; explorer to the file dialog; the tree, breadcrumb and
  places targets; doc/dnd-explorer.png (Ctrl-drag of three files over a
  folder: ghost, copy cursor, highlighted target; the cursor composited in
  from XFixes, Xvfb has no compositor so the image is the dithered one).
- Limitations: no XdndProxy, XdndDirectSave or INCR (lists over the maximum
  request size); no right-button drag menu (Copy Here/Move Here/Create
  Shortcuts Here), no spring-loaded folders, tree auto-expand or auto-scroll
  while dragging; text drops are refused; a name taken on move gets "Copy of"
  instead of XP's replace question; GTK targets ask the source to DELETE after
  a move, which is refused (file managers move the files themselves, a text
  widget must not make files vanish); icons are not repositioned inside an
  explorer list; the ARGB drag image is untested here (no compositor in Xvfb).
  The list view starts a drag only when the motion crossing the threshold is
  over it (it has no capture while the button is down).

## The shared browsing widget: browser.{h,cpp} (2026-10-25)

The explorer's browsing parts are a component now, hosted by the explorer frame
and the Longhorn file dialog (src/filedlg, see "The Longhorn file dialog"):
- src/explorer/browser.h: `struct browser::Browser` holds what the list shows
  (folder, all_items -> items through `visible()` and the filter, view, sort,
  hidden files, Back/Forward history, select-after-refresh, group view) with
  hooks for what differs between hosts (`icon`, `visible` - the dialog's file
  types, `ghosted` - cut items or hidden files, `size_shown`, `filled` - the
  status bar, `shown` - titles/address/header/tree, `view_changed`,
  `filter_cleared`, `resolve` - the dialog's ::recent) and flags
  (`remember_views` - the views file, `select_parent` - going up selects the
  folder you came from, `record_same`, `grouped`). Functions: create_list,
  fill_list, sort_feedback, apply_filter/set_filter, refresh, navigate,
  go_back/forward/up, history_menu, set_view/menu_view (Thumbnails through
  thumbview), set_grouped, selection, list_index, drag_from_list,
  drop_dest/drop_highlight, views:: (the per-folder views file); and the
  frame plumbing of a Longhorn host: want_longhorn/frame_auto (frame=, the
  theme's flxp_frame, lhwm), extend_frame/caption_hole/unextend_frame/
  header_refit (lhwm's glass under the header, CLIENT_CAPTION and its input
  hole), header_hittest (the header's empty parts are the caption),
  frame_active (_NET_ACTIVE_WINDOW through lhwm).
- What moved: explorer.cpp's fill_list, sort_feedback, the filter, refresh's
  enumeration, navigate/back/forward/up, the history menu, set_view and the
  views namespace, selection/list_index, the list's drag source and drop
  target parts, the frame-mode decision and the glass helpers; Explorer now
  derives from Browser (`g.items`, `g.folder`, `g.list` ... keep their names)
  and its old functions are one-line wrappers. The header, places pane, task
  band, 4051 command bar, thumbview and verbs were modules with callbacks
  already and are used by both hosts as they are. Behaviour: unchanged (the
  only difference: Back/Forward skip a place that cannot be opened any more
  instead of stopping there). XP mode and the Longhorn frame without a WM are
  pixel-identical to the previous build (luna and classic, Folders on and off;
  scratchpad lab/xpcmp5.sh).
- header::set_title: a fixed title instead of the folder's name (the dialog's
  "Open" / "Save File").
- places: the groups are a model now (`places::Options`: `spec` = a list of
  GroupSpec {Places | Bookmarks | Custom with the host's places, title,
  collapsed}, `new_window` - the menu's Open in New Window, `groups` - the
  groups look); set_group() replaces a Custom group's places, destroy() frees
  the statics for the next host, Callbacks::group_toggled reports a fold (the
  dialog remembers them), Callbacks::current_path the folder shown (bold).
  The groups look (the file dialog) follows the pre-reset Longhorn concepts:
  Media Player's pane (1456384117.or.45878.png: a left-to-right gradient
  240,245,255 -> 219,232,251, a 1 px line at the right, names in 35,74,139,
  hot 0,102,204 underlined, the current folder bold, the +/- expander box
  192,195,200 with a 140,155,184 glyph before each group's title in
  30,57,91, regular weight as Media Player's "Music Links") with the Save File concept's 16 px icons (image9.jpeg), the 4074
  Filter box at the top (lh4074-180309.png). Rows: headers 22 px, entries
  20 px (icon at 22, name at 42), 8 px between groups. The explorer keeps
  Places + Bookmarks in the XP and 4051 looks.
- Wine listview (flxp:, .patch regenerated): group view in the icon views
  (LVS_ICON, LVS_SMALLICON; the Thumbnails view is an icon view). Wine had
  none (every LVM_*GROUP* was a TODO). LVM_ENABLEGROUPVIEW,
  LVM_ISGROUPVIEWENABLED, LVM_INSERTGROUP (LVGF_HEADER/GROUPID/STATE),
  LVM_SET/GETGROUPINFO, LVM_GETGROUPSTATE, LVM_GETGROUPCOUNT, LVM_HASGROUP,
  LVM_REMOVEGROUP, LVM_REMOVEALLGROUPS, LVIF_GROUPID on insert/set/get.
  LISTVIEW_Arrange lays the groups out top-aligned: a header row (font height
  + 16), the group's items row by row from the left edge, 6 px after the
  group; LVGS_HIDDEN groups and the items of LVGS_COLLAPSED ones are moved
  out of sight (x = -32000); items in no group follow without a header.
  Headers are drawn before the items as in the Save File concept: the title
  in grey (114,114,114), a 1 px rule in 193,204,226 to the right (ending 36 px
  before the right edge), and for an LVGS_COLLAPSIBLE group a small triangle
  in 97,131,179 (up: expanded) 28 px from the edge; LVM_ENSUREVISIBLE of an
  item in its group's first row brings the header into view too; a
  click on a collapsible header folds the group (no notification: hosts read
  LVM_GETGROUPSTATE, browser::fill_list keeps the states by title across
  refills). Inserting or regrouping items marks the layout dirty; the next
  paint arranges. Not done: groups in the Details and List views (Wine lays
  report rows out by index everywhere), group footers, subsets, LVN_LINKCLICK,
  keyboard focus on a header, keyboard navigation skipping folded items.

## The Longhorn file dialog (2026-10-25)

The user's most wanted piece: an Open/Save dialog in the Longhorn look instead
of the GTK chooser. src/filedlg/filedlg.cpp has two looks now; both run on the
shared browsing widget (browser::Browser: the dialog's `Dlg` derives from it, so
the list, its views, sorting, history, filter and selection-after-refresh are
the explorer's code; the file-type filter is the `visible` hook, hidden files
are `ghosted`, ::recent / ::recentplaces come from `resolve`).

Which look: `filedlg = auto | xp | longhorn` in flxp.ini (FLXP_FILEDLG,
`flxp-filedialog --look`, Options::style); auto = the explorer's frame style
(browser::want_longhorn: frame=, the session theme's flxp_frame - xp for the
Luna entries, longhorn for aero4074/slate4074/jade4074/slate4051 - else
longhorn whenever lhwm runs). The Longhorn look uses the widget's singletons
(header, places pane), so a process that already shows them (the explorer)
gets the XP template; the desktop and Display Properties (Browse...) get the
Longhorn one. The XP template is unchanged: pixel-identical to the previous
build in luna and classic, Open, Save and Select Folder (scratchpad
lab/fdcmp.sh).

Layout (IDD_FILEDLG_LH in rc/filedlg.rc holds only the controls the dialog
manager needs, with comdlg32's ids - stc3/cmb13 File name, stc2/cmb1 Files of
type, chx1, IDOK, IDCANCEL, and IDC_FD_HIDE - so the accept logic is shared;
everything else is created and laid out in pixels in "the Longhorn look"):
- the explorer's header (header.h) with the dialog's title ("Open", "Save
  File", "Select Folder" or --title; header::set_title), Back/Forward with
  their history drop-downs, the breadcrumb (drop-downs, Ctrl+L edit field)
  and the search box: on lhwm's glass (DwmExtendFrameIntoClientArea with
  NO_TITLE/NO_ICON and CLIENT_CAPTION, the caption buttons' input hole, the
  empty parts are the caption: browser::extend_frame / header_hittest), the
  theme's navigation bar without composition, the Slate gradient without
  lhwm; follows composition and theme switches live (WM_DWM*, WM_THEMECHANGED,
  _NET_ACTIVE_WINDOW for the active/inactive variants);
- the 4074 command bar (lh4074-180309.png): Up a Folder, New Folder, Change
  View (drop-down) on the theme's per-folder Rebar strip with white text
  (lhbar, as the explorer's 4051 bar); View > Command Bar hides it
  (`filedlg_commandbar`);
- the browser: the places pane in the groups look (places::Options, see "The
  shared browsing widget"): Common Places (Recent Places = the folders of
  recently used files, ::recentplaces; Recent Documents = XP's ::recent;
  Desktop; Home; Documents and Pictures when the XDG dirs exist; App Location
  = the folder the caller asked for: --directory-start, the portal's
  current_folder, lpstrInitialDir, unless it is Home), Computer (My Computer
  and the drives), Bookmarks (GTK bookmarks, rename/remove, folders dropped on
  it become bookmarks), the 4074 Filter box; 155 px wide, a 4 px splitter
  (`filedlg_pane_width`); folded groups are remembered
  (`filedlg_collapsed`). The list: no client edge, the concept's 244,244,244
  background, the theme's Explorer::ListView selection (pale blue on Aero,
  4074's cream on Slate), Icons grouped by type by default (Thumbnails, Icons,
  List, Details from Change View, the list's menu or Ctrl+1..4; Show in Groups
  there too - groups apply to the icon views; `filedlg_view`,
  `filedlg_groups`); group headers fold on a click and the folded types are
  remembered (`filedlg_folded_types`);
- Save: the concept's details band (98 px; gradient 228,237,252 -> 199,214,236
  a third down -> 212,226,243 as the concept's rows, a 173,192,220 line): the
  icon of the typed name's type (follows the typing), "Name:" (the File name
  combo), "Will be saved in:" with the folder (path ellipsis), "File type:"
  (the filters); labels right-aligned in 93,110,138; then a 46 px strip
  (244,244,244) with Hide Explorer (an owner-drawn toggle: a small box with a
  triangle; Alt+H, Browse Folders = Alt+B brings the browser back) and
  Save/Cancel (84x23) at the right. Hide Explorer folds the dialog to the
  header, the band and the strip (the minimum size follows), remembered
  (`filedlg_explorer`);
- Open and Select Folder: 4074's compact rows - File name (inline
  auto-append completion as before) and Files of type (Open only), Open and
  Cancel right of the first row, the read-only box under them when asked for.
- Size: resizable, remembered (`filedlg_size`, the window without lhwm's
  CLIENT_CAPTION growth, which would otherwise add the caption's 18 rows on
  every run); default 720x560 (Open) / 720x600 (Save).
- Keys: as the XP dialog (Enter, Escape, Backspace/Alt+Up up, Alt+Left back,
  F5, Ctrl+H, Ctrl+A, F2, Delete, mnemonics) plus Alt+Right forward, Ctrl+L
  the address field, Ctrl+E/Ctrl+F/F3 the search box, Ctrl+1..4 the views;
  Enter and Escape in the search, address and Filter boxes stay there (the
  message filter dispatches them before the dialog manager would accept or
  cancel). Tab order: list, bottom controls, Hide Explorer, the places pane
  (Filter box), the header (search box; header and pane are
  WS_EX_CONTROLPARENT).
- The dialog is the verbs' host while it runs (drops on the places pane
  refresh its list; the previous host is put back: the desktop), header::
  destroy / places::destroy free the singletons when it closes.
- sys: set_argb recreates the X window when a window first becomes ARGB; it
  now puts back WM_TRANSIENT_FOR / the dialog type / MODAL and the minimum
  size set on the old window (--parent x11:HEX with the glass frame).
- What differs from the concept (image9.jpeg) and 4074: no column headers above
  the icon view (Wine's listview has none there), no Author/Keywords metadata
  in the band, the view selector is the command bar's Change View (not a
  control beside Hide Explorer), the header is lhwm's (4074's glass with the
  framelet/theme buttons, star and magnifier) rather than the concept's
  silver one, the left pane is Media Player's light blue (the concept's is
  244 grey), no task band in the dialog (4074's purple one offered printing
  and mailing, Codex judged it unrelated to choosing a file), no Encoding row.
  One Codex comparison against the concept (closeness "about 60 %", mostly the
  header and the missing column row); followed: the list background, the
  band's gradient shape, the group rules' right inset, regular group titles
  in the pane, wider buttons.
- Tests: src/test/headless/filedlg.sh (Xvfb + xdotool, no WM): in both looks
  open, multi-select, save new, the overwrite question (No keeps the dialog,
  Yes replaces), Select Folder (typed and current), a typed wildcard, a typed
  folder, default extension, the filter index, cancel; Longhorn: Hide
  Explorer (shrinks, remembered, starts hidden, accept while hidden, Browse
  Folders), Show in Groups from the list's menu (remembered), the size
  remembered. In the lab (lhwm + picom): aero4074 glass, slate4074 opaque, a
  live aero <-> slate switch with the dialog open, --parent x11:HEX of an
  xterm (transient-for, dialog type, modal, centred), group folding by
  clicking a header (remembered across runs); the portal on a private bus
  (OpenFile and SaveFile through gdbus: the Longhorn dialog under aero4074,
  uris returned). doc/filedlg-longhorn-open.png (glass, Thumbnails grouped by
  type, a selection), doc/filedlg-longhorn-save.png (glass),
  doc/filedlg-longhorn-hidden.png (Hide Explorer), doc/filedlg-longhorn-opaque.png
  (slate4074, Office files grouped by type, 4074's selection).
- Seen in the lab only: after Hide Explorer under picom the header's glow
  text shows dark fringes until the next full repaint; the window's own
  pixels are right (import -window), a compositor artefact as the one noted
  for the explorer's header.

## Header sizes, the dialog's glyph buttons, Thumbnails as a toggle, white places (2026-10-25)

From the user's new screenshots (lh4051-19*.png, lh4074-19*.png, 5048; the
reading is doc/concepts.md section 5, Codex 4 calls).

Header sizes and elements (header.h/.cpp, navbar.cpp): 4074 has Toolbar Size
(Small/Medium/Large/Extra-Large) and Toolbars (Back/Forward Buttons, Address,
Favorites) in the system menu; ours are settings with View > Header in the
explorer (enabled with the Longhorn frame) and in the dialog's View menu:
- `header_size` = small | medium | large | xlarge (also tiny = small,
  extralarge). Medium is the old header, pixel-identical (lab nbcmp6.sh: slate4074,
  jade4074, slate4051 navigation bar, aero4074 glass; the 195 differing pixels
  are the command bar's activation state, as before).
  Small (lh4074-195235.png: 59 rows from the frame's top edge): the caption keeps
  the title (extend_frame drops NO_TITLE and CLIENT_CAPTION whenever the header
  does not draw the title), one row with the theme's SmallTravelBack/SmallTravelFwd
  (navbar Skin.sback/sfwd/sfav, "Small" before the variant prefix as Jade's
  SmallInactiveTravelBack; the forward offset from ANIMATIONBUTTONRECT; styles
  without them - 4051, the framelets - draw the normal ones at two thirds),
  the breadcrumb 14 px after them centred on the circle (measured: ink 15 px,
  ours matches), the small magnifier (the drawn one at 2/3) and SmallFavorites
  25 px apart at the right of the row, no gripper. Height: the navigation bar
  button + splitter + 1 (Slate 37, = 59 - its caption), on glass/pixmap
  59 - caption, at least the button + 2.
  Large / Extra-Large (195250/195258: 108/125 rows): Medium's rows at the top,
  32/49 more below (base_height()), the magnifier and star small (Large) or
  normal (Extra-Large) at the bottom right, no gripper.
- `header_buttons`, `header_title`, `header_breadcrumb`, `header_search`,
  `header_favorites` = 1 | 0. Without the buttons the title and crumbs start at
  14 (195312); without the title (or at Small) lhwm's caption shows it; what is
  hidden on the right gives its room to the rest. The old `header_buttons =
  framelets` (the Avalon images instead of the theme's) is now
  `header_button_images`; `header_buttons=framelets` is still read as that.
- The dialog reads `filedlg_header_size` (default small: the portal dialog is
  compact) and `filedlg_header_<element>`, falling back to the explorer's keys
  (header::configure(prefix, default size)). doc/header-sizes.png (Slate
  navigation bar and aero glass, the four sizes).
- The explorer's View menu looked up its Explorer Bar and Arrange submenus by
  position (2 and 9), stale since Task Band and Small Icons were added (the
  Folders check and the sort radio silently did nothing); now by handle.

The dialog's commands (filedlg.cpp, header::set_tools): 4074's Open dialog
(lh4074-180309) and 4051's (195027) both have an XP-like toolbar of text
buttons (Up a Folder, New Folder, Change View, Zoom); the user found our copy of
it "very un-Longhorn". 4051's explorer has glyph buttons in its command row
(Views ▾, the zoom magnifier ▾), no Up (the breadcrumb), Make a new folder in the
task band. So the dialog's New Folder and Views ▾ are now glyph buttons in the
header on the breadcrumb's row, left of the magnifier and star: Longhorn
shell32's toolbar strip #216 (XP's tb_sh_def_16 layout: New Folder 31, Views 32;
the comctl32/icon theme strip without longhorn_system32), hot/pressed like the
breadcrumb's segments, the drop-down part opening the Views menu under it. Up a
Folder: the breadcrumb, Backspace/Alt+Up. The text strip stays as View >
Command Bar (`filedlg_toolbar = header | bar`; the old `filedlg_commandbar`
is not read). doc/filedlg-longhorn-small.png.
- Seen at Small on glass: the crumbs' glow text stayed on black boxes until the
  window was resized (the Medium size's CLIENT_CAPTION resize hid it); a
  RedrawWindow did not help, a 1 px resize does: the dialog does that 150 ms
  after it is shown. The cause is in the toolkit's ARGB backbuffer after
  set_argb (not found yet; open).

Thumbnails as a toggle (browser.h thumbs_toggle/thumbs, set_thumbnails,
view_code): in the Longhorn hosts Thumbnails is 4051's Preview, a check item
(View menu, the Views drop-downs - which now list Icons/List/Details and the
toggle -, the dialog's menus, Ctrl+1) that shows thumbnails in the Icons view
whatever view is chosen after it; turning it on from another view goes to
Icons. Remembered per folder like the view (views file `thumbnails` =
Icons+toggle, `details+thumbnails` etc.; the dialog's filedlg_view the same
way). XP mode keeps the Thumbnails view (verbs::thumbs_toggle = -1): XP
pixel-identical (xpcmp6.sh, fdcmp6.sh: luna/classic, Folders on/off, dialog
open/save/folder). Test: filedlg.sh checks on, kept in Details, off.

Places pane (places.cpp): the groups look on white (4051's pane and Open
dialog) with a 205,212,224 line at the right, the clickable link items kept;
no more/less in any look. Bookmarks reorder by dragging: a press moved past
SM_CXDRAG starts a folder drag of the entry (verbs::drag_items, bookmarks and
real places), so out of the pane it is an ordinary folder drag (copy with Ctrl,
etc.); over the Bookmarks group our own drag shows an insertion mark (a 2 px
line with end ticks in the hot link colour) and the drop writes the GTK
bookmarks file in the new order (shell::save_bookmarks; the pane re-reads it).
Tested in the lab with xdotool (reorder, Ctrl-drag of a bookmark into a folder
copies it). doc/places-white.png (mid-drag with the mark, after the drop).

Open: 4051's zoom slider (one continuous scale) and Hide File Names; small
picture icons in Details/List with the toggle (lh4051-194746); tooltips for
the header's glyph buttons; the ARGB repaint cause above; Codex's last look
suggested more tint behind the glass header (lhwm's) and roomier crumbs.

## The user's 2026-10-26 notes: bookmarks drop, one left pane, command bar, zoom

From ~/fun/longhorn/bugs_flxp.txt (the title-bar items wait for the 4051 research).

Bookmarks drop (places.cpp drop_here/insert_at): dropping onto a bookmark moved
the files into the bookmarked folder. Now the Bookmarks group - its header, its
entries, the gaps between them and, when it is the last group, the empty pane
under it - is a bookmark zone: dropped folders become bookmarks at the gap under
the pointer (the insertion mark that the reorder drag already had; a folder
bookmarked already moves there), our own bookmark drag reorders, anything with a
file in it is refused (the "no" cursor). Only an entry of another group
(Desktop = ~/Desktop, Home, Documents, a drive in the dialog's Computer group)
takes files, with the usual move/copy/link rules. "Bookmark this folder" and the
context menu's "Add Current Folder to Bookmarks" are gone from the pane (the
Bookmarks menu, the item menu and the task band keep theirs); the context menu
shows only over an entry. Tested headless (classic): a file on a bookmark stays,
a folder above/below a bookmark or on the header is inserted there, under the
group appended, a file on Home moves.

One left pane (places.{h,cpp}): the explorer's Longhorn frame uses the dialog's
groups look now (white, +/- expanders, link names, the current folder bold, the
Filter box) with `places::standard_spec()`: Common Places (Desktop, Home,
Documents, Pictures, Trash), Computer (`places::computer_group()`: My Computer
and the drives, shared with the dialog) and Bookmarks; the dialog keeps its own
Common Places (Recent Places/Documents, App Location). The 4051 list look (no
headers, links every 20.5 px) is deleted; `set_longhorn` switches the explorer
between the XP task pane (Places, Bookmarks: unchanged) and the groups look,
rebuilding the groups. Folded groups are remembered by the pane itself
(`Options::remember`: `places_collapsed` in the explorer, `filedlg_collapsed` in
the dialog, whose own code for it went). The previous look's plainness is a
toggle: `places_icons = 1|0` (View > Explorer Bar > Icons in the Pane in the
explorer, enabled in the Longhorn frame; View > Icons in the Pane in the
dialog's Views menu), the names then start where the icons were. XP mode:
pixel-identical apart from the removed "Bookmark this folder" (lab xpcmp7.sh,
fdcmp7.sh against ccdb3d5).
doc/places-unified.png: the explorer (slate4074) and the Open dialog with the
same pane.

Task band compact/expanded (taskpane.{h,cpp}; heights from
doc/4051-explorer-research.md section 5): 4074's band is freely resizable with
a minimum; 4051's Preview (FWF_PREVIEWPANE) switches between an expanded pane
(149-150 rows, default 150) and a compact one (71 rows: a line of text and a
row of tasks), and shows the expanded one only while 150 rows stay for the
list. Ours: the band's bottom 4 rows are a drag handle (SIZENS cursor);
expanded heights 100..400 (`taskpane_height`, default 150), at most the frame's
room less 150 (taskpane::set_room from the explorer's layout; with less room it
shows compact without changing the setting); dragged 20 rows under the minimum
it becomes compact, a compact band dragged down 20 rows expands again (to the
height it had); `taskpane_compact = 0|1`. Compact: the folder's (or
selection's) 48 px image faded, the name (with the type for one item), the
tasks side by side, more. Expanded: the big image follows the height (48..128,
142 at 150 capped to 128), the link rows tighten under 104. View > Preview
(enabled in the Longhorn frame) is checked when expanded and toggles the two
(turning it on with the band off shows the band expanded); the 4051 bar's
details button drives it (see the command bar below).
doc/taskpane-compact.png (expanded 150, then dragged up: compact).

Thumbnails in every view and the zoom (browser.{h,cpp}, thumbview, uiicons;
doc/4051-explorer-research.md section 4): in the Longhorn hosts
(`Browser::zoomable`, the explorer's Longhorn frame and the Longhorn dialog)
the Thumbnails toggle is a way of drawing the items, no longer a jump to Icons:
Icons shows the squares at the zoom, Details/List/Small Icons show 16 px
squares in the small image slot (thumbview::enter_at(px, small_slot): the
thumbnail image list in LVSIL_SMALL; folders keep their icon under 48 px, no
pictures inside; the border select frame and XP's spacing only from 48 px).
The zoom is 4051's (shell32 CIconSizeSlider): positions 0..100, size =
floor(16 * 16^(pos/100)); its ticks every 10 are the levels 16 21 27 36 48 64
84 111 147 194 256 (browser::zoom_levels, zoom_at/zoom_pos). `Browser::zoom` =
the Icons view's size (0 = the default: the large icons, with thumbnails
thumbnail_size); without thumbnails the Icons view gets the icon list at that
size (uiicons::at_size / the dialog's icons_sized: one list per size with the
same indices as small/large, loaded from the theme's sources, the large icon
stretched when the theme has nothing near) and spacing max(75, px + 43) x
px + 43 (the default spacing at the large size). Driven by Ctrl+wheel over the
list (a SetWindowSubclass on the list, browser::attach_list for the explorer's
own list), Ctrl+plus/minus, View > Zoom (Zoom In, Zoom Out, the levels as
radio items) in the explorer and the dialog, and the 4051 bar's magnifier
(below); from List/Details a zoom step goes to Icons at the size shown. The
view code carries the zoom above bit 16 (ZOOM_SHIFT); the views file and
filedlg_view write it as "icons@64", "thumbnails@147", "details+thumbnails".
XP mode (not zoomable): the Thumbnails view as before, pixel-identical
(lab xpthumb.sh: luna, Thumbnails and back to Details, against ccdb3d5).
Not done: 4051's extended tiles (metadata beside the icon from 32 px, research
section 4) and Hide File Names; Details rows stay 16 px at every zoom (4051's
Details has a size too).

The 4051 command bar's buttons (explorer.cpp lhtool), as the user read them:
- the red X (IE's Stop glyph, shell32 #206 cell 2) is **Stop**, not Delete:
  it drops the pending thumbnails (thumbview::stop, shell::thumbs::Loader::
  busy/cancel) and is enabled only while some are being made (a 250 ms timer;
  navigation itself is synchronous, so there is nothing else to stop);
- the magnifier with the drop-down (strip index 17) is the **zoom slider**:
  browser::zoom_popup, 4051's CIconSizeSlider - a 74x150 popup (no WS_BORDER:
  the toolkit hands any WS_BORDER toplevel to the window manager; the Menu
  class's POPUPBACKGROUND, a 1 px frame) with a 40x125 vertical trackbar at
  (17, 12), 0..100, ticks every 10, the big end at the top, the zoom following
  the thumb snapped to the ticks; arrows move it, Escape/Enter/a click
  elsewhere close it (a small modal loop, as a menu's);
- the last button (strip index 40, the preview pane glyph) is **View > Preview**
  (the task band expanded/compact), checked while expanded;
- the filter and hidden-files entries of the old magnifier menu are in the View
  menu: Filter by Name... (the header's search box), Clear Filter, Show Hidden
  Files (the first two greyed in the XP frame).
doc/zoom-levels.png: the slider over the Icons view at 84 px, thumbnails at
147, thumbnails at 16 in Icons, Details with 16 px thumbnails.

## Sidebar stacking is global; aliases in overrides; wallpapers.ini (2026-10-26)

Follows x11/SETTINGS.md and THEMES.md "Wallpapers" of 2026-10-25.

**Stacking.** `[sidebar] stacking` (`top` | `bottom`) is a plain global key now: lhwm's Win+S flips it, lux saves it.
Appearance has one row "Sidebar:" under the Taskbar (no scope combo, no source line) with lux's labels "Sidebar on top of
windows" / "Sidebar under windows (desktop)". `sidebar_stacking` is gone from `store::theme_keys` (and from Save As);
`store::set_sidebar_stacking(v)` writes only `[sidebar] stacking` and deletes every `sidebar_stacking` of `[override]`,
`[override:*]` (repeated sections too) and the presets' `[Shell]`; Apply calls it when the row changed (hint `sidebar`).
`store::sidebar_stacking()` reads it. The dialog subclasses its sheet (`watch_settings`) and on `_LH_SETTINGS_CHANGED`
(WM_FLXP_ROOTPROPERTY) reads the key again: the row follows a Win+S toggle, a pending change of the user stays pending.
`[sidebar] layer` is not read or written anywhere (an existing line is left for lux to delete). Mode and with_classic
unchanged. Tested on Xvfb: file `stacking = top` + stale `[override:aero4074]`/`[override]` keys, row changed, Apply ->
`stacking = bottom`, stale keys gone; changing the file and sending the message moved the open dialog's row.

**Aliases.** `store::registry_section(registry, name)` (section or alias, case-insensitive) and `store::theme_names(theme,
registry)` (section first, then its aliases). `theme_chain` looks up `[override:<section>]` then `[override:<alias>]`
(`[aero4074]` is `[aero-patch]`, alias `aero4074`: an old `[override:aero4074]` keeps working); `set_theme_values` writes
under the section's name and removes the key from the alias sections (the "rename once"). `registry_value` uses the same
lookup, a preset uses its base's. The dialog canonicalises an alias in `[theme] name` to its section, so the theme list has
one entry per section (title). `lookup_session_theme` already resolved aliases (`out.name` = the section), no change.

**Wallpapers** (src/shell/wallpapers.{h,cpp}). The themes.ini `wallpaper` keys are gone; for theme T:
`own_wallpaper` = `[override:NAME]` wallpaper/mode (section, then aliases), else a preset's `[Desktop]`;
`theme_wallpaper` = `_LH_THEME_RESOLVED`'s `wallpaper=`/`mode=` when its `name=` equals the root `_LH_THEME` (and describes
this theme: the dialog also asks for other themes), else a themes.ini `wallpaper` key (still wins over the file), else
x11/wallpapers.ini (`LH_WALLPAPERS_INI`, else next to the registry): `[section]` (a preset: its base) then its aliases, then
`[default]`; `file` under `[default] root` (absolute and ~/ as they are), mode fill|fit|center|tile|stretch; a preset's
`[Desktop] mode` also applies to the theme's file. flxp-desktop (`make_wallpaper`): `_FLXP_WALLPAPER` first, then
`wallpaper = none` (anywhere), flxp.ini/env (explicit, as before), `own_wallpaper`, settings.ini `[desktop] wallpaper`,
`theme_wallpaper`, else the start-up one; it also watches `_LH_THEME_RESOLVED`. The Desktop tab's "Bliss (theme background)"
entry and the theme sample use the same functions. Tested (Xvfb, scratch HOME, no lux): jade4074 = 4074/Bliss.bmp,
luna-silver = 5048/winxp.jpg, aero4074 = the `[aero-patch]` section's file (grass, via the alias), slate4051 = 4051/Bliss.bmp
live on `_LH_THEME` changes; a settings.ini `[desktop] wallpaper` file wins on every theme, `none` gives #3a6ea5,
`[override:aero4074] wallpaper` applies to the aero-patch session, preset P1 (base alias) uses its base's section, P2 with
`[Desktop] wallpaper` its own file. Spec gaps: wallpapers.ini `[default] file` applies when a section exists without `file`
(the file does not say); `[override] wallpaper` ("all themes") is not a layer in the spec and is not read.
Test notes: a throw-away Xvfb loses root properties when its last client exits (use `-noreset`); `xprop -set` did not set
anything here, so the scratch used a 6-line Xlib setter.

File view highlights (listview.c, flxp:, patch regenerated; research section 6):
- hovering a selected item showed white: 4051's Slate (wim/4051, the one the
  lab's slate4051 uses) has no LISTITEM(HOTSELECTED) at all, so state 6 fell
  back to the class's white BORDERFILL with the blue border (the checked
  build's #165 is a byte copy of HOT, 21 %). Now hot+selected draws SELECTED
  and HOT over it unless the theme defines HOTSELECTED itself
  (GetThemePropertyOrigin(IMAGEFILE) == PO_STATE: 4074's lighter peach, used as
  it is). Selected without the focus stays SELECTEDNOTFOCUS (grey), with or
  without the pointer.
- the dotted focus rectangle: on themed items (an LVP_LISTITEM theme) it is
  hidden until the keyboard is used in the list (XP's UISF_HIDEFOCUS rule:
  LISTVIEW_INFO.bHideFocus starts TRUE, the first non-modifier WM_KEYDOWN
  clears it; WM_UPDATEUISTATE/WM_CHANGEUISTATE with UISF_HIDEFOCUS set/clear
  it). Plain lists (Luna, classic) draw it as before.
Tested on slate4051: click (peach, no dotted edge), hover the selection
(peach + hot), focus elsewhere (grey), an arrow key (the dotted edge appears).

## The Vista-era styles: aero5048 and aero5112 (2026-10-26)

lhwm has registry entries for 5048's and 5112's aero.msstyles; flxp now loads them as they are.
- Session theme (shell/lhtheme.cpp loadable_msstyles): a section's `msstyles` is used directly
  when it is version 3 *or 4* (the v4 reader in the vendored msstyles.c); the same-named XP
  backport is only looked for when the file is neither. An explicit `flxp_msstyles` still wins:
  every 4074/4051 entry keeps its backport (tuned values). x11/themes.ini (our keys only):
  `[aero5048]` lost its `flxp_msstyles` (Jade's backport) so the 5048 assets are used;
  `[aero5048]` and `[aero5112]` got `flxp_frame = xp` (the XP explorer for now, aap),
  `flxp_icons = longhorn`, `flxp_longhorn_system32 = ../resources/system32/4074`.
- Theme fonts: set_session_theme adds the section's `fonts` directories (`;`-separated, relative
  to the registry; a file adds its directory) to the font path, once per directory, at start-up
  and on a switch. 5112's [SysMetrics] fonts are Segoe UI 9 (`fonts = ../resources/fonts/5112`):
  menus, status bars, the taskbar's clock and the start panel now use it instead of a fallback.
- Header (comctl32/header.c, flxp:, header.c.patch new): 5112's Header class background and its
  HEADERITEM images are see-through (a 1 px separator on the left, blue glows for hot/pressed);
  Wine painted nothing under them, so the list's column header was black. When the normal item's
  centre is see-through (drawn over black and over white; cached per HTHEME, reset on
  WM_THEMECHANGED) the item and the empty rest get the window colour plus part 0 under the item
  image; XP's items are opaque (Luna's keyed corners only), so XP-era styles are unchanged
  (explorer/controls/file dialog under Luna, 5048, 5112 before and after: only 5112's header
  differs).
- Start panel (taskbar/startpanel.cpp): 5112's PLACESLISTSEPARATOR/PROGLISTSEPARATOR are 216 px
  TRUESIZE lines, wider than the column: XP's uxtheme centres an oversize true-size image
  (TRUESIZESCALINGTYPE NONE), sfthost clips it to the pane's window; ours clipped it to nothing and
  it ran over the program list and out of the panel. Now clipped to the separator's rect.
  LOGOFFBUTTONS is two 24 px cells in 5112 (lock, power) instead of three (eject, log off, turn
  off): the cell count is the image width / 24, log off -> lock, turn off -> power.
- Checked with no change needed: w32test/controls under both (buttons, check/radio, group box,
  list box, combo boxes, scroll bars, edits: the parts the file has; 5112's Combobox has only
  DROPDOWNBUTTON, so XP's combo is what it defines), the XP explorer (toolbar, rebar, address band,
  ExplorerBar task pane, list view, status bar), the XP file dialog, flxp-settings (tabs, buttons,
  the sample's Window parts), flxp-desktop. Taskbar: 5112's TaskBar BACKGROUNDBOTTOM is the 30 px
  dark bar, Start::Button a 83x30 black pill (3 states, green when hot/pressed: no orb in either
  build; 5048's is the 4074-style silver bar with the green start button), TaskBand::Toolbar
  buttons, TrayNotifyHoriz::TrayNotify (same art as the bar), TrayNotify::Clock white Segoe UI;
  the bar takes the theme's part heights as for the other styles. Start panel: 5112's
  StartPanel parts (dark user pane and places list, white program list, dark log-off pane).
- Live switching (lab: lhwm, flxp-desktop, flxp-taskbar, explorer, flxp-settings running;
  aero4074 -> aero5048 -> aero5112 -> luna): every window restyles, the explorer drops the
  Longhorn header for the XP bars under 5048/5112 and stays XP under Luna, the taskbar and
  wallpaper follow. The settings dialog keeps its own selection and sample (pending state).
- Not themed / not done: menus are XP's flat menus from [SysMetrics] colours (Wine's menu.c
  does not use the Menu class; 5112's POPUP*/BAR* parts unused); the Vista-only classes
  (Navigation, CommandModule, AeroWizard, TaskDialog, BreadcrumbBar, SearchBox, PreviewPane, the
  *Composited/Max/Inactive address-band variants) have no flxp user yet; 5112's sort arrow
  (HEADERSORTARROW) is not drawn (Wine's header draws its own). With lux running and the
  registry's `taskbar = avalon` (both entries) flxp-taskbar is menu-only: the bar is lux's.
- doc/uxtheme-5112-controls.png, doc/explorer-5112.png, doc/taskbar-5112.png,
  doc/startpanel-5112.png.

General (2026-10-26 notes; research section 7):
- Status bar: off by default in the Longhorn frame (4051's Explorer default,
  CShellBrowser2::_LoadStatusBar), on in the XP frame; View > Status Bar is
  remembered per frame (`statusbar_longhorn`, `statusbar`), a frame switch
  picks the frame's.
- The window icon follows the folder (4051's v_SetIcon): browser::
  set_window_icon sends WM_SETICON with the folder's theme icon at 16 (SMALL),
  48 (BIG) and 32 (SMALL2) on every navigation (explorer and dialog). The shim
  (defwnd.cpp, flxp:) keeps a toplevel's three icons and publishes all of them
  as _NET_WM_ICON (GetIconInfo/GetDIBits, the mask when the colour has no
  alpha; sys::set_icon, put back when set_argb recreates the X window), which
  lhwm's caption (where it shows one) and flxp-taskbar read; WM_GETICON answers.
- X selections in edit controls: the shim's clipboard (sysparams.cpp) puts text
  set through SetClipboardData on the X CLIPBOARD at CloseClipboard and reads
  CF_UNICODETEXT/CF_TEXT from it (another program's or ours;
  IsClipboardFormatAvailable asks too), through FlxpSetSelectionText /
  FlxpGetSelectionText (flxp_shell.h) and shell::xclip::set_text/get_text
  (the owner thread now owns CLIPBOARD with files or text and PRIMARY with
  text; UTF8_STRING, text/plain;charset=utf-8, STRING; our files read as text
  are their paths). So Ctrl+C/Ctrl+V/Ctrl+X of every edit control (address
  bar, the breadcrumb's edit field, the search and Filter boxes, the dialog's
  file name, renames) use CLIPBOARD. Wine's edit.c (flxp:, patch regenerated):
  a selection made with the mouse (drag, double click) or the keyboard
  (Shift+arrows, Ctrl+A) is PRIMARY (not for ES_PASSWORD); the middle button
  pastes PRIMARY at the pointer (Wine pasted the clipboard), not in read-only
  controls.
- "Copy as Path" in the item menu (verbs, ID_COPY_PATH): the paths, one per
  line, as text on the CLIPBOARD (after it our files offer is gone; Paste of
  files finds none).
Tested in the lab with xclip: Ctrl+L then Ctrl+C gives the path on CLIPBOARD,
Shift+Home puts it on PRIMARY, a middle click in the Filter box pastes
xclip's PRIMARY, Ctrl+V pastes xclip's CLIPBOARD, Copy as Path; the
_NET_WM_ICON's three sizes decoded from xprop; clipboard.sh, filedlg.sh and
dnd.sh pass; XP pixel-identical (xpcmp7.sh, fdcmp7.sh).

The looking glass (header.cpp search_icon): the header's magnifier is not a
theme part (4051's Slate has no Go part; 4074's Go::Toolbar.BUTTON is fully
transparent) but shell32.dll's icon group 23 (Search), its 32x32 image (ICON
220/224), which 4051 draws at (cw-81, 11) (doc/4051-explorer-research.md
section 3, a template match against lh4051-194203). Loaded from the theme's
longhorn_system32 ("pe:<dir>/shell32.dll#23" at 32 px, premultiplied once,
dropped on a theme change): 4074's shell32 has the grey glass of the 4074
screenshots (a match by eye against lh4074-195242), 4051's the light blue lens
with the orange handle. Drawn over the old cell (30x32, scaled with it at the
Small/Large sizes); the drawn lookalikes only without the Longhorn files. The
command row's small glasses were already strip 216/217 index 17. Note: a
longhorn_system32 in flxp.ini wins over the theme's flxp_longhorn_system32,
so slate4051 shows 4074's grey glass unless the 4051 files are configured.

## The 4051 header in every theme: lower edge, one layout, small buttons (2026-10-26)

From the user's title-bar notes (~/fun/longhorn/bugs_flxp.txt "titlebar") and
doc/4051-explorer-research.md sections 1-2; decisions with the user: the
double line is ours, small-button style per theme with a setting, glass and
the small-button style independent.

The lower edge (header.cpp paint_bottom): `Rebar.SPLITTER` is no longer drawn
(it is 4074's NavBarSize drag affordance: a dark and a light row over the
bar's last four rows, which end in the bar's own dark row - three lines,
lh4074-195242.png y 168..171). The header's last row is drawn once, in every
mode, size and focus state, maximised too: the theme's `Navbar` (inactive:
`InactiveNavbar`) background's own last row, stretched with its side margins
(Slate 33,36,36, inactive 160,163,164; Jade/aero 65,75,86, inactive
118,122,125); the `Max*Navbar` variants have no dark row (Slate's ends in the
band colour, Jade's is uniform), so maximised windows take the restored
variant's row; styles without a Navbar (Luna on lhwm's pixmap, the drawn
Slate gradient without lhwm) get 4051's 33,36,36. It is drawn over the travel
buttons: their hot/pressed drop-down box (whose last row is the band's last
row) sits on the line instead of cutting it (Codex's first look). On glass it is opaque too,
and the frame is extended one row further (browser.cpp extend_frame): lhwm
ends an extension with its plate's light and dark lines (frame.c
plate_explorer: the header's last two rows), which now lie under that row and
the command bar. Measured at client x 380 (lab, our window; last four rows of
the header, top to bottom):
- slate4074 medium/small/large/xlarge: (102)(209)(163)(33,36,36) -> (163)(163)(163)(33,36,36);
  maximised (89)(199)(143)(143) -> (143)(143)(143)(33,36,36)
- slate4051: unchanged (163)x3 (33,36,36); maximised (143)x4 -> (143)x3 (33,36,36)
- jade4074-classic: (57,59,63)(240,244,253)(226,234,252)(126,207,194) -> (226,234,252)x3 (65,75,86);
  maximised (18)(5)(136)(18) -> (18,19,18)x3 (65,75,86)
- aero4074 glass: glass, glass, lhwm's light (204,222,135), lhwm's black -> glass x3, (65,75,86)
- the reference rows: lh4051-194203 x=500 y 97..100 (158)x3 (33,36,36) - the same shape.

One layout (height_for_caption, top_off/band_rows/base_height): the same rows
in every mode - glass, the navigation bar, lhwm's pixmap - under the frame's
caption: [CLIENT_CAPTION rows][the band][Large/XL rows], the band 4051's
navigation bar: Medium = the travel art's height, 54 (Jade, aero, 4051, the
framelets), except Slate 4074's short art (38x49) which keeps its measured 53;
Small 36 (lh4074-195235: 36 rows under the 24-row caption, the dark line 59
rows below the frame's top edge); Large/XL +32/+49 (Slate 85/102, others
86/103). The travel buttons are bottom-aligned in the band (btn_top =
band bottom - their height): their drop-down (CUSTOMSPLITRECT, the image's
bottom 10 rows) lies on the band's last rows, flush with the dark line, as in
4051 (research section 1: navbar row 53 = the image's last row). The title,
crumbs, gripper, magnifier and star keep the family's measured places from
the band's top (navbar.cpp layouts); with the theme's buttons the glass and
pixmap headers now use them too (and the bar's fonts: the Navbar.TITLE size,
lhwm's face), so aero/glass has Jade's layout instead of 4074-dwm.png's.
Header rows from the frame's top edge to the dark line (inclusive count),
before -> after, Medium: slate4051 78 -> 78, slate4074 77 -> 77, jade 82 ->
78, aero glass 76 -> 81 (27-row caption + 54); Small: 61/61/65/65 -> 60/60/60/63.
Button positions (client): Slate 4074 Back (2,0) -> (2,4), Forward (40,0) ->
(40,4) (the 49-row art at the bottom of the 53-row band; the text stays at its
measured rows 4..18 / 31..38); Jade/4051 (2,0)/(40,0) unchanged; aero glass
(2,7) of a 67-row client from the top border (16 rows below the frame's top) ->
(2,18) of a 72-row client = 27 rows below the frame's top, right under lhwm's
caption as on Slate/Jade ("the navigation buttons are too high in glass").
4051 medium against lh4051-194203 (client 27,47): title ink x 81, rows 3..15
("doc"; the reference's caps from row 3), crumbs rows 30..40 - as measured there.

CLIENT_CAPTION and the layout conflict: with the flag lhwm gives the client its
caption rows (from the 9-row top border); 4074-dwm.png put the title and the
Back button across them (76 rows from the frame's top). That is the conflict:
the consistent layout won. The flag stays on on glass (client_caption=0 turns
it off), the caption's rows (caption - top border = 18 on aero) stay empty
above the band, so everything is where it is without the flag; the caption
buttons' input hole is as before.

Small buttons (header_small_buttons = auto | even | uneven, also the theme's
flxp_header_small_buttons; the dialog's filedlg_header_small_buttons falls
back to it): even = the style's SmallTravelBack/SmallTravelFwd (4074 Slate
24x32, Jade/aero 27x36: the same size, as 4074 has them); uneven = the big
pair - bigger Back, smaller Forward, 4051's art and the concepts
(image9.jpeg, lh_winhec_proto_10.jpg) - made small by navbar::shrink: the
rows above the drop-down (CUSTOMSPLITRECT's top, 44 of 54) area-averaged
(box filter, premultiplied) by T/h, T = the style's small art height (Slate
4074: 32) or 36; the drop-down's rows kept 1:1 at the bottom over the scaled
plate, box-filtered across only (so the box's side lines meet the plate's:
Codex saw a join with nearest columns), except the glyph's columns (found as the
dark opaque pixels of the hot image's drop-down rows: 4051 Back cols 14..20,
Forward 9..15), which is copied unscaled - so the arrow stays crisp and flush
with the edge. 38x54 + 27x54 -> 25x36 + 18x36 (Forward at x 2 + 25), all five
states (disabled, normal, hot, pressed, split-pressed) and variants; the
framelets (no theme buttons) likewise. auto = even where the style has the
Small parts (4074 Slate, Jade, aero), else uneven (4051 Slate, Luna, the
framelets); `even` without Small parts gives uneven. The small star
without SmallFavorites (4051) is box-filtered to two thirds too (it was a
stretched AlphaBlend). Independent of glass: aero4074 shows Jade's even pair
on glass and opaque alike, header_small_buttons=uneven Jade's big pair scaled.

The header's menus open flush under its lower edge (list top = the row under
the header's dark line, as the crumb list of lh4074-193810.png): the Back/
Forward history drop-downs (were under the button image, one row up), the
crumbs' folder lists and the field's history button (were under the
breadcrumb field, in the middle of the band), the star's bookmarks (were
under the star). Measured in the lab: slate4051 Medium, the dark line client
row 53, the crumb list's and the history menu's frames from row 54; aero glass
72 rows, menu from row 72.

`flxp --info` prints the header's metrics as they would be laid out now
(header.cpp metrics_info, also without a window): `header size` - the size
setting, its rows, where they come from (under the N-row caption, or from
lhwm's top border with CLIENT_CAPTION: the caption rows left empty, then the
band) and the dark line's row from the frame's top edge, the other sizes'
rows; `travel` - Back/Forward size and place at Small and Medium, the
drop-down's rows, the small style (even/uneven, header_small_buttons and why,
how the glyph was found); `lower edge` - whose Navbar row, no splitter. E.g.
slate4051: "medium (header_size): 54 rows under the 24-row caption, the dark
line 77 rows below the frame's top edge; the others: small 36, large 86,
xlarge 103" / "small Back 25x36 at (2,0), Forward 18x36 at (27,0), the
drop-down rows 26..35, uneven ... auto: no Small travel buttons in the style".
aero4074 glass: "72 rows from lhwm's 9-row top border (CLIENT_CAPTION: the 18
caption rows stay empty, then the 54-row band), the dark line 80 rows below".

Tested (lab, scratch HOME with the user's flxp.ini; Xvfb + lhwm, picom for
aero): slate4051, slate4074, jade4074-classic, aero4074 glass at the four
sizes and maximised (the rows above), hot/pressed Back drop-downs at Small and
Medium, `header_small_buttons=uneven` on Jade and aero glass and Slate 4074,
the history and crumb menus' positions, the dialog's small header (uneven
pair on slate4051), Luna with frame=longhorn (lhwm's pixmap: the same layout,
4051's dark row, the uneven small pair from Luna's own art). XP mode
pixel-identical against HEAD (xpcmp/fdcmp: luna and classic explorer with and
without Folders, the dialog open/save/folder); the explorer's Longhorn frame
without a WM differs only in its last row (56,58,63 -> 4051's 33,36,36, 800
pixels). Codex, 2 looks: (1) 4051 Medium, Slate 4074, aero glass against
lh4051-194203 / lh4074-195242: the lower edge a clean single dark line, no
double line or gap (the hovered drop-down then cut the line: fixed, drawn on
top), 4051 button size/place right, text within 1-2 px (different strings),
~78 %; it found the Slate 4074 pair moved 4 rows down "worse than the 4074
reference" - kept: the flush drop-down is the user's request, and 4074's
place is what its splitter rows were under. (2) the small uneven pair against
image9.jpeg / lh_winhec_proto_10.jpg: proportions right ("about 80 %"), a
visible join between the unscaled drop-down and the plate (fixed: box filter
across); the rest (the concepts' longer disabled-Forward pill, the crop
offsets) are state and crop differences.
doc/header-sizes.png (slate4051, slate4074, aero4074 glass at the four sizes),
doc/explorer-longhorn-glass.png (aero4074, Back's drop-down hot on the line),
doc/explorer-longhorn-opaque.png (slate4051, the same), doc/filedlg-longhorn-
small.png (the dialog's small header, slate4051's uneven pair).

## libuxtheme: text measuring, font overrides, text masks (2026-10-27)

Asked for by the lux/lhnav side (x11/THEMES.md "Request for the wux side"): all desktop text is to
go through libuxtheme's GDI-faithful text path, lhnav first. New in src/uxtheme/ut.h:

- `ut_text_extent(t, cls, part, state, text, flags, bounds, &extent)`: GetThemeTextExtent (DrawText
  with DT_CALCRECT in the part's TMT_FONT; advance-based; DT_SINGLELINE, DT_WORDBREAK with bounds'
  width as the wrap width, DT_END_ELLIPSIS, DT_NOPREFIX). bounds NULL = unbounded.
- `ut_draw_text_font(font, 0xRRGGBB, pixels, w, h, stride, text, flags, rect)` and
  `ut_text_extent_font(font, text, flags, bounds, &extent)`: the same with an explicit `ut_font`
  (face, LOGFONT height, weight, italic/underline/strikeout), no theme needed (nav.ini fonts).
- `ut_get_text_metrics(t, cls, part, state, font_or_NULL, &m)`: height/ascent/descent/leading/
  average and max width (GetTextMetrics) of a font or a part's font, for layout.
- `ut_draw_text_mask(t, cls, part, state, font_or_NULL, mask, w, h, stride, text, flags, rect)`:
  8-bit coverage of exactly the glyphs the draw calls produce (white text rendered onto a
  transparent has_alpha scratch surface, alpha copied out, max'ed into the caller's mask), for a
  glow or shadow behind text on glass.

All share the renderer, hinting and font smoothing (gasp rules) of ut_draw_text. Checked with a C
client against libuxtheme.so: Tahoma -11 "Documents" 53x13 (ink 0..51), Segoe UI -17 87x23, Franklin
Gothic Medium -19 93x24; DT_END_ELLIPSIS in 40 px -> 36 px; DT_WORDBREAK in 80 px -> 3 lines.

## The 2026-10-27 todo items (left pane hover, auto-refresh, drag raise, hot controls, ...)

From ~/fun/longhorn/todo.txt (flxp section).

Left pane: the hovered current folder (bold) was drawn with the regular
underlined font, so it lost its bold while hovered; places.cpp now has a bold
underlined font for that case (explorer and dialog).

Hot states of the themed controls ("wux buttons have no mouse-over hover
indication", Display Properties): TrackMouseEvent's TME_HOVER was a timer
nobody turned into WM_MOUSEHOVER (the button ignored the WM_TIMER), and
TME_QUERY always said "nothing tracked". Wine's themed button sets BST_HOT
only on WM_MOUSEHOVER (it tracks TME_HOVER|TME_LEAVE with a 1 ms hover time),
so push buttons, check boxes and radio buttons never drew PBS_HOT/CBS_*HOT.
Now (src/w32/user.cpp) TME_HOVER is win32's: a timer with a TIMERPROC that
posts WM_MOUSEHOVER (client point, MK_* keys) once the pointer has rested in
the 4x4 hover rectangle for the hover time (400 ms for HOVER_DEFAULT) over the
window or with its capture, then hover tracking ends; TME_CANCEL cancels it;
TME_QUERY reports the window last tracked with TME_LEAVE/TME_NONCLIENT/TME_HOVER
as still pending. Combo boxes (CBF_HOT on the button, TME_LEAVE), tabs (their
own timer), toolbars, the header and scroll bars already had theirs.
Checked in flxp-settings under luna: OK/Cancel, the Effects check box, the
combo's button and the tabs light up while hovered and go back on leave.

Folders bar close button: the X is browseui's browtb.bmp, which only the xp
icon theme names (`close =`); with the longhorn, 2k or wine icons the button
had no image. explorer.cpp now draws the same glyph (XP's cell 1, pixel for
pixel, navy) into a 39x11 strip when the theme has none; xp unchanged.

WebP (src/gdi/image.cpp, the shared decoder): with libwebp and libwebpdemux
installed (the Makefile asks pkg-config; without them FLXP_WEBP is undefined
and nothing changes) RIFF/WEBP files decode in decode_image: lossy, lossless,
alpha (straight, as WebP stores it, BGRA = our 0xAARRGGBB), an animation's
first frame (WebPAnimDecoder, the composited canvas); for thumbnails
(max_size > 0) libwebp's rescaler decodes straight to the size that fits.
.webp is thumbnailable (thumbs::can_thumbnail), sniffed as image/webp, a
wallpaper (wallpaper::load) and in Display Properties' picture list and
Browse filter. GIF: no libgif here, so not added (Pictures with .gif keep
their icon). Checked: 16 px thumbnails in Details and 256 px in Icons for a
lossy, a lossless-with-alpha (translucent over the white) and an animated file.

## File operations (2026-10-27)

An external review (~/fun/longhorn/gpt_feedback.txt) put "trustworthy file
operations" first, and it was right. The old paste_items: a cross-filesystem
move ignored failures removing the source (remove_tree's result was dropped:
"success" with the files in both places, or half of them); "into itself"
compared path strings (a symlinked path, `..`,
a bind mount, a trailing slash all got through: a cross-fs or bind-mount move
into itself recursed); a failed copy ran remove_tree on the destination path (safe only because the
name had been free a moment before: anything appearing there meanwhile went too);
copies kept the mode bits only (no times, no xattrs, holes filled); moves onto
an existing name always made "Copy of x"; errors were one string (the last).

Now src/shell/fileops_copy.cpp + fileops_job.{h,cpp} (shell::fileops, no
win32; the header guards min/max so it can follow windows.h):
- `Result run(Op, sources, dest_dir, Options)`, Op = Copy | Move | Trash |
  Remove. Result: ok, cancelled, errors (path + errno + what; text()),
  created (where each finished top-level item is), summary() for one message
  box. Options: confirm (Conflict -> Answer), progress (Progress: bytes/items
  done and total, current), cancel (atomic<bool>*), count, rollback.
  paste_items / drop_items keep their signatures and use it (err = summary()),
  so explorer, desktop and file dialog callers are unchanged.
- Sources are (realpath of the parent, name): trailing slashes and `..`
  resolved, the last component not followed, so a symlink is the link.
  Refused when a source folder is the destination or one of its ancestors,
  by device+inode up the destination's physical ".." chain (O_PATH openat;
  the kernel's ".." crosses mounts, so a bind mount of the source below the
  destination is found). drop_effect uses the same check (fileops::is_inside)
  and compares parent folders by inode for "already there".
- Move: renameat2(RENAME_NOREPLACE) (plain rename to replace a confirmed
  file); EXDEV -> copy. The originals of a copied move are recorded (path,
  inode, size, mtime) and removed only after the whole top-level item copied
  without error, children first, each only if still the same inode/size/mtime;
  every failed removal is an error (the copy stays: both exist, nothing lost).
  A move onto itself does nothing.
- Copy: files to `name.flxp-part` (O_EXCL, .2 ... when taken) in the target
  folder, renamed into place with RENAME_NOREPLACE, so an interrupted copy
  never leaves a short file under the real name. Data: SEEK_DATA/SEEK_HOLE
  segments with copy_file_range (read/write fallback on EXDEV/EINVAL/...),
  ftruncate for a trailing hole; the source size is checked after. Symlinks
  are copied as links (never followed: `.`/`..`/loops are just links); a
  folder whose inode is already on the current copy path is ELOOP (bind-mount
  loops); fifos/devices via mknod (devices need root); sockets are an error.
  Folders are made 0700, filled, then given their metadata (so read-only
  folders copy, and their mtime is not touched by the contents going in).
- Metadata: chown (uid as root, else only the group, failures ignored),
  xattrs (all names; user.* failures other than ENOTSUP/symlink EPERM are
  errors, other namespaces best effort), chmod incl. setuid/setgid/sticky
  (retried without setuid/setgid on EPERM), then futimens/utimensat with the
  source's atime and mtime in ns (links: AT_SYMLINK_NOFOLLOW).
- Rollback: every path an item creates goes on a list in creation order (part
  files, then their final names; a replaced file is not on it: it existed);
  a failed or cancelled item removes exactly those, newest first (a read-only
  folder it made gets 0700 back first). A merge into an existing folder only
  loses what it added. Per item: a failure rolls back that item and the next
  one still goes; cancel (flag or a Cancel answer) rolls back the current item
  and stops; finished items stay (as XP).
- Name clashes: no confirm callback -> "Copy of x" / "Copy (2) of x" for the
  top-level item, for moves too (as before). With one, XP's Confirm File
  Replace / Confirm Folder Replace: Yes, Yes to All, No, No to All, Cancel;
  folders merge (entry by entry, each clash asked); a file onto a folder or
  back is an error; merging a folder into one that contains it is refused.
  A merged folder keeps its own attributes. "No" items stay at the source
  (their folder too, for a move).
- Remove (permanent delete) collects errors and never descends into another
  filesystem mounted inside. Trash goes through shell::trash per item.
- `Job(op, sources, dest, notify, ctx, ask)`: run() on a std::thread. notify
  is called on the worker (progress at most every 50 ms, a question, done):
  the UI posts itself a message from it (win::post is thread-safe, see
  "Thumbnails view") and reads progress() / question() / done() there;
  answer(a) unblocks a waiting conflict, cancel() stops (and answers Cancel);
  result() joins. `worth_a_thread(op, sources, dest)`: copies/cross-fs moves
  of > 32 items or > 8 MiB, trash/same-fs moves of > 64 items.
- Tests: `make build/fileops_test && build/fileops_test <dir> <dir on another
  filesystem>` (defaults $TMPDIR and /dev/shm; it works only in mkdtemp
  folders it makes and removes): 123 checks, pass on tmpfs->tmpfs,
  tmpfs->ext4 and ext4->tmpfs; same-filesystem dirs fail the cross-fs cases by
  design (a warning says so).

What the callers still need (verbs.cpp paste/drop, desktop, filedlg stay
synchronous; their files belong to others right now):
- a Confirm File Replace dialog (TODO: XP's IDD_CONFIRM_FILE_REPLACE layout,
  the two files' sizes and dates from Conflict) and passing confirm, so a
  move onto an existing name asks instead of making "Copy of x";
- for big operations (worth_a_thread): a Job with notify posting e.g.
  WM_APP+0x50 to the frame, a progress dialog (XP's flying-papers dialog:
  progress(), Cancel -> cancel()), the conflict dialog on question(), and on
  done() the summary in one error box plus host->changed(dirs);
- folder enumeration is still synchronous in the namespace (not touched here).
Not done: hard links inside a copied tree become separate files; ACLs only
as far as system.posix_acl_* xattrs copy; no undo.

Start menu state for lux ("lux start button does not know when wux start menu
is open"): while flxp-taskbar's start panel or classic start menu is up,
`_LH_STARTMENU_OPEN` (CARDINAL, 1 open / 0 closed) is set on the
`_LH_STARTMENU_S<screen>` owner window (when we own it) and on the root
(xwm::set_start_menu_open, from the X thread). Both menus are modal: the
panel sets 1 just before it maps and 0 as soon as its loop ends (a choice,
Escape, Super, a click outside through the pointer grab, focus loss,
_LHSHELL_START's toggle), the classic menu around TrackPopupMenu (0 before
the chosen command runs, so Run... is not "open"). The owner window gets the
current value when it is (re)created; exit (SIGTERM etc.) writes 0 to the
root. Fixed on the way: SIGTERM while the classic menu was open spun for ever
(PostQuitMessage's WM_QUIT is sticky in the shim and Wine's menu loop
dispatched it again and again): the menu loop now ends on WM_QUIT, as
Windows' does (vendor user32/menu.c, flxp:). Tested on Xvfb (panel and
classic): 1 while open, 0 after Escape, a click outside, a chosen item; a
SIGTERM with the menu open exits and clears it.
Proposal for lux (SHELL-DESIGN "Start menu ownership, revised"): a Start button
that forwards to a provider reads `_LH_STARTMENU_OPEN` on the current
`_LH_STARTMENU_S<screen>` owner (PropertyChangeMask on that window; the value
on the root is a convenience for clients that do not track the owner, and may
be stale if a provider crashed) and draws pressed while it is 1; a new owner
means "closed" until it says otherwise.

Drag hover raise ("drag&dropping across windows should temporarily bring the
currently hovered window to front after a timeout"; src/sys/x11.cpp, the XDND
source): while dragging, the pointer resting (within 8 px) for 1 s over a
toplevel that is not the topmost managed one raises it: `_NET_ACTIVE_WINDOW`
for its client (the window with WM_STATE inside the frame, else the root child
itself), source indication 2, the last input time, our window as the
requestor's active window; without a window manager (no
_NET_SUPPORTING_WM_CHECK) we XRaiseWindow it ourselves. Once per rest; the
drag image is kept above it, and 150 ms later the stacking snapshot is re-read
so the raised window gets the positions and the drop where it now covers the
one that was on top. As in Windows the hovered window stays in front after
the drop (or the cancel); override-redirect windows are never raised.
Tested on Xvfb without a WM and under lhwm (two explorers, the drop target
partly under the source): after a 1.6 s rest the target came up (lhwm made it
the active window), the drop landed in the part that had been covered.
lhwm needs nothing: it honours _NET_ACTIVE_WINDOW from any source during our
pointer and keyboard grab (client_manage_focus raises and focuses; only
`warp_pointer = true` would warp the pointer in the middle of the drag).

Auto-refresh ("automatically update the view if anything in the directory
changes"): the folder shown is followed with inotify.
- shell::DirWatch (src/shell/dirwatch.{h,cpp}): one directory, IN_CREATE,
  IN_DELETE, IN_MOVED_FROM/TO, IN_CLOSE_WRITE (not every IN_MODIFY of a copy
  in progress), IN_ATTRIB, IN_DELETE_SELF/MOVE_SELF (+ IN_ONLYDIR); take()
  reads what is pending as Changed / Gone; a queue overflow counts as
  Changed. No inotify (inotify_init1 fails) or the watch limit
  (fs.inotify.max_user_watches, ENOSPC): one line on stderr, nothing
  watched, F5 as before.
- The toolkit's fd watch: sys::watch_fd(fd, callback, user) / unwatch_fd
  (win::watch_fd/unwatch_fd forward): a blocked wait_event wakes when a
  watched fd is readable; win's message loops call sys::dispatch_fd_watches()
  next to the root-property hooks, which polls the fds and runs the callbacks
  on the main thread between messages.
- browser.{h,cpp} (explorer and file dialog): refresh() calls follow(b),
  which watches b.folder when it is a filesystem folder (`follow_disk`, on by
  default); the callback only sets a 150 ms timer on the list, so a burst (a
  copy, an unpack, a build) is one update. update(b) enumerates again and
  brings the list in line item by item: gone entries LVM_DELETEITEM'd, new
  ones inserted where the sort puts them, changed ones (size, date, type,
  icon) set again, every lParam renumbered, thumbview::fill for the
  thumbnails (cached ones by path and mtime stay, new or rewritten pictures
  are asked for); selection, focus and the scroll position are the list's
  own and stay. When the kept items' order changes (a Date sort and a file
  written) or in group view (a type's group may come or go) it refills
  instead and puts selection, focus and scroll back. It waits (300 ms
  steps) while a label is being edited or a drop is aimed at an item. The
  folder itself deleted, moved or unmounted: navigate to the nearest folder
  above it that exists. Hosts: changed_on_disk() (default filled(): the
  explorer's status bar; the dialog: nothing, its filled() would move the
  focus). ~Browser() unfollows.
- The desktop (~/Desktop): followed the same way, a change refills the icons
  150 ms later (positions are kept by name as before) with the selection and
  focus put back; the 2 s mtime poll stays for when inotify is not there.
- Not followed: the Folders tree, the places pane's drives, other folders'
  thumbnails of pictures inside sub-folders.
Tested on Xvfb/lhwm: 60 files in Details, scrolled, one selected; creating,
deleting, renaming and appending to files from a shell shows within ~0.2 s,
selection and scroll kept; the folder removed from under the window goes to
its parent; Icons with thumbnails at 256 px: a picture copied in gets its
thumbnail, the others keep theirs; a file and a folder made in ~/Desktop
appear on the desktop.
(The browser.cpp part of this went into 208a3d5 with the header work, which
was committed from the shared tree while this was in progress.)

Default view and the view keys ("default should be icon view with
thumbnails. redefine the hotkeys: Ctrl+0: default view, Ctrl+1, 2, 3:
details, icons, bigger icons"):
- A folder never shown (no stored view, not shown before in this window)
  gets the default view (XP's g_dfs, see "How XP picks a folder's view"):
  `default_view_longhorn` in the Longhorn hosts (explorer's Longhorn frame,
  the Longhorn dialog), `default_view` in the XP frame, in the views file's
  syntax ("thumbnails@48", "details", "details+thumbnails"); empty = the
  built-in one: Icons at 48 px with thumbnails in the Longhorn hosts, Details
  in the XP frame as before (XP mode stays pixel-identical: lab xpcmp9.sh,
  fdcmp9.sh against bw9; the Longhorn frame differs on purpose).
  browser::views gained parse/name_of, default_view/builtin_default/
  set_default, forget (one folder) and forget_all (XP's Bags deleted).
- Keys (explorer and dialog, browser::view_key): Ctrl+0 the default view
  and the folder's own view forgotten (it is a never-shown folder again),
  Ctrl+1 Details, Ctrl+2 Icons (48 px in the Longhorn hosts, the thumbnails
  toggle kept), Ctrl+3 big icons with thumbnails (thumbnail_size, 96; the XP
  frame's Thumbnails view); the explorer keeps Ctrl+4 List and Ctrl+5 Small
  Icons, the dialog Ctrl+4 List. Ctrl+wheel and Ctrl+plus/minus zoom as
  before. View menu: Details Ctrl+1, Icons Ctrl+2, Thumbnails Ctrl+3, List
  Ctrl+4, Small Icons Ctrl+5 and a new "Default View  Ctrl+0".
- Tools > Folder Options (dialogs::folder_options, rc/explorer.rc
  IDD_FO_GENERAL/IDD_FO_VIEW, a property sheet with Apply): General: the
  default view (View; in the Longhorn frame also Icon size - Default or a
  zoom level - and "Show thumbnails of pictures"), Restore Defaults; View:
  "Apply to All Folders" (the current folder's view becomes the default,
  every folder's own view is forgotten) and "Reset All Folders" (the
  built-in default again, every folder's view forgotten), both after XP's
  question and at once; "Remember each folder's view settings"
  (remember_views) and "Show hidden files and folders" (`show_hidden`, new:
  read at start, also written by View > Show Hidden Files). OK/Apply write
  flxp.ini (a default equal to the built-in one is written as empty).

## The vision header: lhnav (2026-10-27)

The concept art's title/navigation area (`~/fun/longhorn/x11/NAVBAR.md`, lhwm's
"Aero vision" frame) drawn by lhnav, the toolkit-independent component of the
Longhorn lab, inside flxp's header window; flxp keeps the behaviour.

Build (Makefile): `LHNAV_DIR ?= /u/aap/fun/longhorn/x11/lhnav`; when it has
lhnav.h, its lhnav.c, lhnav_px.c and lhnav_res.c are compiled (not copied) into
build/lhnav/*.o with `-DHAVE_UXTHEME -DUT_HAVE_TEXT_EXTENT -idirafter src`, i.e.
against flxp's own src/uxtheme/ut.h, archived as build/lhnav/liblhnav.a and
linked before libuxtheme.a in W32OBJS (only programs with the header pull it in);
header.o gets `-DFLXP_HAVE_LHNAV -idirafter LHNAV_DIR`. lhnav's text and art go
through the in-process libuxtheme, i.e. flxp's GDI text; libpng was linked
already. Without LHNAV_DIR the header builds without it and `flxp --info` says
"header engine flxp (lhnav unavailable: built without it ...)".

When: `header_engine = auto | lhnav | flxp` (flxp.ini or the theme's
flxp_header_engine; the dialog's filedlg_header_engine falls back to it),
default auto = lhnav when lhwm's frame theme directory (`_LHWM_THEME` up to the
first `|`) has a nav.ini. Evaluated on every refit (height_for_caption, i.e.
WM_DWMCOMPOSITIONCHANGED / WM_DWMCOLORIZATIONCOLORCHANGED / WM_THEMECHANGED),
so the header switches both ways at run time; lhwm's theme reload rewrites
_LHWM_THEME, which re-reads nav.ini (tested on a scratch copy of the aerovision
theme: `preset = small` then `lhwmc reload-theme`, extension 69 -> 47 -> 69 live).
Under aerovision lhwm switches to its `frame_classic` (jade4074-classic, no
nav.ini) when composition goes, so the opaque case is the Jade navigation bar
header as before; header_engine=lhnav forces lhnav there too (lhnav's
defaults, the title centred in the 24-row deco, on lhwm's _LHWM_HEADER_PIXMAP
with its caption buttons). `header_preset` picks a nav.ini `[preset NAME]`
(auto: nav.ini's `preset`, but `small` for the Small header size, the
dialog's default, when nav.ini has one). `flxp --info` prints "header engine"
(which, why, preset, variant, deco, tools; with a window its rows and style).
FLXP_VISION_DEBUG=1 prints every lhnav rect on layout (as NAVDEMO_DEBUG).

Delegated to lhnav (one `lhnav` per header, i.e. per process: the explorer,
the Longhorn file dialog): layout, painting and hit-testing of the travel orb,
the title (in lhwm's 15-row deco), the breadcrumb with its chevrons and "«",
the search box and the `tools` items; hover/pressed/open states; tooltips'
text (`lhnav_tip`). The style is `ut_open` of the registry's Longhorn msstyles
(navbar_msstyles, else the theme's `msstyles`, as the navigation bar and
navdemo open it; the shim itself often loads the XP backport). Metrics:
`lhnav_metrics_load_preset(nav.ini)`, then `_LHWM_FRAME_METRICS` (FlxpGetRootCardinals,
19 items: border_t 12/13, deco_h 18, the caption colours for the opaque text,
maximised: lhwm's maximised colours and no glow) and FlxpDwmGetCaptionRect.
Paint: lhnav_paint into a cleared 32-bit premultiplied DIB section, lhwm's
caption rect cleared, then AlphaBlend over the background: glass FlxpClearRect,
not composited lhwm's pixmap (blit_pixmap, split out of paint_pixmap), else the
theme's Navbar background, else navdemo's gradient.

Kept in flxp: the header window (a child at 0,0) now covers
`lhnav_extend_top()` rows - lhnav's header plus the two rows on which lhwm draws
the plate's light/dark line pair - so `header::height()` is that and the
extension equals it (`header::extend_top()`, used by browser::extend_frame; the
flxp header keeps its "+1 on glass"); CLIENT_CAPTION | NO_TITLE | NO_ICON always.
Input: WM_NCHITTEST -> HTTRANSPARENT for lhnav's CAPTION / TITLE / RESIZE_TOP /
BUTTONS / NONE (the frame's header_hittest makes them HTCAPTION, and HTTOP for
lhnav's top resize rows: `header::resize_hit`), HTCLIENT else; mouse moves,
presses, releases and leaving go to lhnav_pointer, only the changed parts are
invalidated. Back/Forward: the existing ID_BACK/ID_FORWARD callback on the
release; the big variant's BACK_MENU/FWD_MENU (on the press) and a right click
on either orb: the existing history menu, flush under the header
(lhnav_set_open meanwhile); a segment: navigate; a chevron (SEP+i): flxp's
crumb menu, 4074's siblings of segment i, it bold, under the segment
(`header_crumb_menu = children`: Vista's subfolders, the next segment bold, as
navdemo), the tooltip then says "Folders next to NAME"; "«": the collapsed
ancestors, nearest first. The address box / Ctrl+L: flxp's address EDIT
(borderless, Segoe UI 12 px, no margins) at the box's text origin with
`lhnav_set_field(ADDRESS, "", -1, 1)` (the opaque field, no text) while it is
shown; Enter/Escape/focus loss as before. Search: the search EDIT is shown over
lhnav's box only while it has the focus (`lhnav_set_field(SEARCH, "", -1, 1)`);
afterwards lhnav draws its text or "Search" (`lhnav_set_field(SEARCH, text, -1,
0)`); a `search` item opens the field (`lhnav_set_search_open`) and closes it
again when it loses the focus empty; Ctrl+E/F as before. `favorites`: the
bookmarks menu (g_cb.favorites); `views` / `newfolder`: the host's buttons -
the dialog's set_tools New Folder and Views (prepended to nav.ini's tools,
their icons through lhnav_set_icon, disabled New Folder ignores clicks), the
explorer's ID_VIEWS menu and ID_NEW_FOLDER (new Callbacks::tool in
explorer.cpp: header_tool); `bookmark:PATH` navigates. Tooltips: one comctl32
tooltip, a tool per hovered part with lhnav_tip's text, TTM_RELAYEVENT.
Title: the folder name (the dialog: "Open"/"Save"); segments: the namespace
chain as the flxp header has it (My Computer, /, ...); history from
set_history; focus from set_active (_NET_ACTIVE_WINDOW); maximised from
IsZoomed; state_changed / caption_changed re-read the metrics and ask the host
for a refit when the height changes (maximise: 49 rows small, 71 big). View >
Header's Title/Search/Favorites switches drop the title / search items /
favorites; the sizes only pick `small` (above). Drag and drop onto segments as
before.

Measured in the lab (Xvfb + lhwm aerovision + picom, solid background,
scratch HOME; flxp and navdemo on the same display, both 720 px client, frames
at x 11, the comparison per window state: each shot with that window active):
- preset `big` (nav.ini's choice): every rect lhnav reports identical to
  navdemo's (title 78,0 541x31; back 0,13 38x54; forward 38,13 27x54; menus
  3,57 35x10 / 38,57 25x10; address 73,42 455x21; search 536,42 180x21);
  _LHWM_EXTEND_FRAME 69, caption rect 627,0 93x13, _NET_FRAME_EXTENTS 9 9 2 9 as
  navdemo's; frame rows 0..70 identical outside the text columns (x 90..560),
  title ink identical (99,19..127,32); rows 71.. are the bodies.
- preset `small`: frame rows 0..48 identical outside the text columns: deco
  rows 0..14, orb rows 15..44 (26x30 + 18x30), boxes rows 21..41 (aap's
  box_h 21, address_dy 2), plate light line row 47, black 48, body from 49;
  extension 47, maximised 49 (rows 0..48 of the 1280 px maximised windows
  identical too, white text, the dark plate).
- Back hover and pressed: the orb region identical to navdemo's in the same
  states; the history list, chevron list (open look: the chevron down), "«",
  tooltips, Ctrl+L + Enter, search (EDIT while focused, lhnav's text after),
  the `concept` preset's magnifier and star (bookmarks menu), move from the
  title, top resize from the first 3 rows, maximise/restore through lhwm's
  buttons, composition off/on (vision <-> the Jade navigation bar header).
- The flxp header and XP mode are pixel-identical against a baseline build of
  HEAD: explorer and the Longhorn dialog under aero-patch with picom, aero-patch
  without, slate4074, slate4051, jade4074-classic, luna (lhwm, no nav.ini), and
  XP mode luna/classic explorer, longhorn frame without WM, the XP dialog.
- picom (xrender, Xvfb) once showed stale wallpaper over a rectangle of the
  explorer after the bookmarks menu and a tooltip; the window's own contents
  were right (import -window) and a resize cleared it: the compositor
  artefact noted in "The Longhorn header".
- Codex (1 call; concept, ours, navdemo crops): no header difference between
  ours and navdemo beyond the strings; small is the closer preset to the
  concept (47 against ~51 rows); what remains is lhnav's/the theme's: a title
  row of its own vs. the concept's single row with a Windows icon, the darker
  aero glass and the orb (~24 vs ~27 px), the 180 px search field vs the
  concept's magnifier + star (the `concept` preset has those).
doc/vision-header.png (flxp vs navdemo, big and small, 2x top crop),
doc/vision-dialog.png (the dialog's small vision header with New Folder and
Views, the Views menu open).

Open (what lhnav lacks for flxp's faithful layouts, NAVBAR.md "Missing"): the
4074 Medium/Large/XL layouts (title on its own row above the breadcrumb, right
items at the band's places), the even small travel art and flxp's uneven
shrink, the Avalon framelets as travel art (an API to hand lhnav pixels), the
field's 15 px history drop button and 4074's darker field, generic host icon
items (lhnav has fixed kinds; the dialog's two buttons map onto
`newfolder`/`views`), a disabled state for tool items, an icon before the first
segment (the concept's flag). Also: the title is lhnav's, so View > Header's
sizes do not apply in vision mode (only Small -> `small`); the opaque vision
header needs header_engine=lhnav because lhwm leaves aerovision without
composition.

## flxp finds its files through lhde_data: no path settings (2026-10-28)

flxp.ini needs no paths any more. `flxp/shell/lhdata.{h,cpp}` wraps `lhtheme/lhde_data.c` (linked into
every program and toolkit test with the shell model: `SHELLOBJS` carries `build/obj/lhtheme/lhde_data.o`;
libluxnav's copy is then not pulled; lhde_data's API is unchanged). The search order is lhde_data's:
`$LHDE_DATA` (else `${XDG_DATA_HOME:-~/.local/share}/lhde`), `<exe>/../share/lhde`, `$XDG_DATA_DIRS/lhde`,
the source tree's `data/`. Every path key of flxp.ini stays as an override; the defaults under them:

| what | where (first data directory that has it) | readers |
|------|------------------------------------------|---------|
| the registry | `_LH_THEMES_INI` (root) when published, else `$LH_THEMES_INI`, `themes_ini=`, else `themes.ini` (`lhde_registry_source`) | `shell/lhtheme.cpp` read_session_properties, `store::registry_path`, the shim's live switch (`wux/w32/theme.cpp`) |
| a registry value (`styles/...`, `resources/4074`, `fonts/4074`) | beside the registry when it is there (a resolved copy keeps absolute paths anyway), else `lhde_data_resolve` | `SessionTheme::path`, `theme_setting` (all `flxp_*` path keys), the `fonts` key |
| a style by name (`theme=`, `--theme`, FLXP_THEME) | `theme_path=`, then `styles/NAME.msstyles`, `styles/NAME/NAME.msstyles`, `styles/NAME/` holding one `.msstyles`, then the same for `NAME-xp` (`luna` = `styles/luna-xp.msstyles`, `jade4074` = `styles/jade4074-xp/jade.msstyles`), then ~/.local/share/flxp/themes, wine's, ~/.themes | `shell::find_msstyles` (the shim's `resolve_theme` now calls it) |
| a Longhorn-compiled session style without `flxp_msstyles` | `styles/NAME-xp/*.msstyles` with PACKTHEM_VERSION 3 | `loadable_msstyles` |
| `longhorn_system32` | the theme's `flxp_longhorn_system32`, else `resources/<build>` when imported | `theme_setting` (icons, header, task band, command bar, dialog glyphs, lhnav's icon_dir) |
| framelets (`explorer_resources`, `longhorn_wim`) | after the keys: `resources/<build>` of the theme, then every `resources/*`, 4074 first: `../Microsoft.NET/Avalon/Microsoft.Windows.Client.dll` beside it (the import links resources/4074 to the build's system32) or `Microsoft.Windows.Client.dll` in it | `header.cpp` framelet_candidates |
| the 4051 command bar's slate.msstyles | `<system32>/../Resources/Themes/Slate/slate.msstyles`, else `styles/slate<build>.msstyles` (the same file: cmp'd for 4051 and 4074) | `lhbar.cpp` |
| the task band's aurora | `taskpane_aurora=`, `resources/demo`, `<system32>/../../../../demo_resources` | `taskpane.cpp` (needs `docLib/`, `search/`: the import has only `aurora_strip.png`, so aurora stays off unless resources/4074 links into the workspace) |
| fonts | `font_path=`, else every `fonts/<build>` with tahoma.ttf (4074's; added unconditionally like font_path, so the real Tahoma beats a system one); the session theme's `fonts` key (`fonts/5112` = Segoe UI) as before | `setup_fonts`, `set_session_theme` |
| wallpapers | `wallpaper_dirs=`, else `~/Pictures/Wallpapers` and every `wallpapers/<build>` (listed as "Bliss (4051)"); `wallpapers.ini` beside the registry, else in the data directories, its relative `root` beside it, else `lhde_data_resolve` | Display Properties' Desktop tab, `theme_wallpaper` (desktop, settings) |

The build of a theme (`theme_build`): `flxp_longhorn_system32`'s `resources/NNNN`, else the 4 digits in the
section's name (a preset's base), in its msstyles' name, in its flxp_msstyles' directory (`jade4074-xp`), else
`fonts/NNNN`. slate4074, jade4074, aero-patch, aerovision: 4074; slate4051, luna, luna-olive, luna-silver
(luna4051): 4051; aero5048/5112: 4074 (the registry's key); luna-xp, royale, zune, noir, embedded: 4074 (fonts).
Without a session theme the digits of FLXP_THEME's file or directory name.

`flxp --info` prints the lookups after `framelets`: the data directories in order, then themes.ini,
wallpapers.ini, the theme's msstyles and flxp_msstyles, its build, its fonts, every `resources/*` (the theme's
build marked, or "not imported"), longhorn_system32 and where it came from, resources/demo, the default font
directory, the wallpaper directories, styles/, and "XP resources: not imported (nt5src= ...)". Each lookup says
which data directory answered or "not imported".

`make check` runs `build/tests/lhdatatest` (tests/toolkit/lhdatatest.cpp): a fake import in a temp directory
($LHDE_DATA) and the registry elsewhere ($LH_THEMES_INI), 30 checks (registry, theme builds, path resolution,
longhorn_system32 default and override, wallpapers.ini root, default fonts and wallpaper dirs, styles by name,
--info's report).

Remaining manual setting: `nt5src` only. The import has no XP resources (resources/ holds 4051, 4074 and demo;
wallpapers/xp has pictures only). Every nt5src reader fails soft (checked with nt5src = a missing directory under
Xvfb: luna and slate4074 explorer, dialog, taskbar draw, no crash): the xp/2k icon themes fall back to Wine's
icons with one message, toolbar strips to comctl32's, browtb.bmp to the drawn X, start.bmp to the drawn flag,
LogoffButtons to the drawn buttons, shellstyle's bitmaps to the drawn task pane, chess.bmp to the generic tile.

### Measured: no path keys against the old explicit paths

Xvfb :74 (1280x1024, `-noreset`, no window manager, no compositor: aero-patch opaque), the session theme set
with `xprop -root _LH_THEME`, scratch HOME/XDG_*, LHDE_DATA = ~/.local/share/lhde (read only),
FLXP_CLOCK_FORMAT=12:34, one fresh HOME and X server per shot. Old = the HEAD binaries before this change
(git archive of 13d3f71), the user's flxp.ini plus theme_path=~/fun/longhorn/themes/xp-backport,
longhorn_wim=~/fun/longhorn/wim, wallpaper_dirs = the old built-in list, and `_LH_THEMES_INI` = the resolved
registry (as luxwm publishes it). New = this build, the user's flxp.ini without any path key (font_path,
nt5src, longhorn_system32, theme_path, wallpapers removed), no `_LH_THEMES_INI` (the registry from the data
directory); new-nt5 = the same plus nt5src. Pixels differing (`compare -metric AE`, whole screen):

| theme | explorer | file dialog | taskbar | settings (Themes) | settings (Desktop tab) |
|-------|----------|-------------|---------|-------------------|------------------------|
| slate4074 | 0 / 0 | 0 / 0 | 469 / 0 | 0 / 0 | 1538 / 1538 |
| aero-patch (opaque) | 0 / 0 | 0 / 0 | 470 / 0 | 0 / 0 | 1590 / 1590 |
| luna (XP) | 48505 / 0 | 5907 / 0 | 470 / 0 | 0 / 0 | 2618 / 1578 |
| aero5112 | 2530 / 0 | 0 / 0 | 466 / 0 | 0 / 0 | 1609 / 1609 |

(new / new-nt5.) With nt5src kept, everything is pixel-identical except the Desktop tab's wallpaper list:
the old run lists the user's own `wallpapers=` (recent Browse... picks, a path key the new flxp.ini drops) and
the old built-in /u/aap/cfg/wallpaper directories; the new one lists the import's `wallpapers/<build>`
(now labelled with the build: "Bliss (4015)", "Bliss (4051)", ...); the theme background row is the same
(wallpapers.ini from the data directory). Without nt5src (no path key at all) the differences are all nt5src's:
the taskbar's start flag (start.bmp, drawn instead), luna's xp icons and toolbar strips (Wine's icons,
comctl32's images) in the explorer and the dialog, aero5112's XP-frame toolbar strip, luna's Desktop-tab icons.
luna's longhorn_system32 is now resources/4051 (the old flxp.ini forced 4074 for every theme); the XP frame
does not draw from it, so it changes no pixel. A missing nt5src directory fails soft (above).

The baseline binaries came from `git archive 13d3f71`; the harness (run.sh, matrix.sh) stayed in the session's
scratch directory. `make check`'s own part (tests/check.sh, LHDE_TEST_DISPLAYS=":76 :77 :78"): all passed,
lhdatatest included. check-luxwm fails before it (see the report: ~/.local/share/lhde/frames/*/ hold only
theme.ini, no PNG, so frametest stops at aero5048).

### nt5src → an XP's own binaries (for lhde-import's XP/2000 source)

From the .rc files and resource headers in nt5src (shell32/ids.h, shell/inc/tb_ids.h, browseui/resource.h,
explorer/rcids.h, explorer/desktop2/resource.h, themes/themedir/shellstyle/common.h, comctl32/v6); spot-checked
against the Longhorn 4015/4051/4074/5112 binaries (all checked ids agree; no XP binary on this machine).
Icons are RT_GROUP_ICON, bitmaps RT_BITMAP. The 2k icon theme uses the 4-bit frames of the same icons.

| nt5src file (shell/...) | binary | id | symbol / note |
|---|---|---|---|
| shell32/document.ico | shell32.dll | 1 | IDI_DOCUMENT |
| shell32/docassoc.ico | shell32.dll | 2 | IDI_DOCASSOC |
| shell32/app.ico | shell32.dll | 3 | IDI_APP |
| shell32/folder.ico | shell32.dll | 4 | IDI_FOLDER |
| shell32/folderop.ico | shell32.dll | 5 | IDI_FOLDEROPEN |
| shell32/drivehar.ico | shell32.dll | 9 | IDI_DRIVEFIXED |
| explorer/mycomp.ico | shell32.dll / explorer.exe | 16 / 100 | IDI_SERVER / ICO_MYCOMPUTER (the same file) |
| shell32/mynet.ico | shell32.dll | 18 | IDI_MYNETWORK |
| shell32/stdocs.ico | shell32.dll | 21 | IDI_STDOCS |
| shell32/sthelp.ico | shell32.dll | 24 | IDI_STHELP |
| shell32/strun.ico | shell32.dll | 25 | IDI_STRUN |
| shell32/recycler.ico | shell32.dll | 32 | IDI_RECYCLER (33 = full) |
| shell32/desktop.ico | shell32.dll | 35 | IDI_DESKTOP |
| shell32/docfind.ico | shell32.dll | 134 | IDI_DOCFIND |
| shell32/inifile.ico | shell32.dll | 151 | IDI_INIFILE |
| shell32/txtfile.ico | shell32.dll | 152 | IDI_TXTFILE |
| shell32/sysfile.ico | shell32.dll | 154 | IDI_SYSFILE |
| shell32/fave.ico | shell32.dll | 173 | IDI_FAVORITES |
| shell32/htt.ico | shell32.dll | 178 | IDI_HTTFILE |
| shell32/genvideodoc.ico | shell32.dll | 224 | IDI_AP_FILMSTRP |
| shell32/genericmusicdoc.ico | shell32.dll | 225 | IDI_AP_DMIDI |
| shell32/pics.ico | shell32.dll | 226 | IDI_AP_PICS |
| shell32/mydocs.ico | shell32.dll | 235 | IDI_MYDOCS |
| shell32/mypics.ico | shell32.dll | 236 | IDI_MYPICS |
| shell32/cdrom.ico | shell32.dll | 294 | IDI_MEDIACDROM (the drive: 12 IDI_DRIVECD) |
| shell32/control.ico | none | - | in no .rc: use 137 IDI_CPLFLD (Control Panel folder) or 36 IDI_STCPANEL |
| comctl32/v6/viewsm.bmp | comctl32.dll 6.0 (WinSxS) | 124 | IDB_VIEWTB_SMALL_COLOR (v5's 124 is an 8-bit image) |
| comctl32/v6/histsm.bmp | comctl32.dll 6.0 (WinSxS) | 130 | IDB_HISTTB_SMALL_COLOR |
| shell32/tb_sh_def_24.bmp, tb_sh_hot_24.bmp | shell32.dll | 214, 215 | IDB_TB_SH_DEF_24 / HOT_24 |
| shell32/tb_sh_def_16.bmp, tb_sh_hot_16.bmp | shell32.dll | 216, 217 | IDB_TB_SH_DEF_16 / HOT_16 (Longhorn's are wider: 768 vs 752) |
| shell32/tb_go_def_20.bmp, tb_go_hot_20.bmp | shell32.dll | 230, 231 | IDB_TB_GO_DEF_20 / HOT_20 |
| shell32/brand_sml.bmp | shell32.dll | 240 | IDB_BRAND_BITMAP_MICRO (256 colours: 245) |
| browseui/shdefhi.bmp, shhothi.bmp | browseui.dll | 276, 277 | IDB_SHSTDHICOLOR / HOTHICOLOR (2k toolbar) |
| browseui/shdef16h.bmp, shhot16h.bmp | browseui.dll | 278, 279 | IDB_SHSTDHICOLOR16 / HOTHICOLOR16 |
| browseui/browtb.bmp | browseui.dll | 545 | IDB_BROWSERTOOLBAR (Folders bar close) |
| explorer/start.bmp | explorer.exe | 143 | IDB_START (not in 4051/4074 explorer.exe) |
| explorer/startclassic.bmp | explorer.exe | 176 | IDB_STARTCLASSIC |
| explorer/desktop2/logoffbuttons.bmp | explorer.exe | 7010 | IDB_LOGOFF_NORMAL |
| explorer/desktop2/logoffbuttonshot.bmp | explorer.exe | 7011 | IDB_LOGOFF_HOT |
| themes/themedir/shellstyle/V/up.bmp, uphot, down, downhot | shellstyle.dll | 100-103 | IDB_UP, UPHOT, DOWN, DOWNHOT |
| themes/themedir/shellstyle/V/headersection.bmp | shellstyle.dll | 112 | IDB_HEADERSECTION (not in the classic one) |
| windows/advcore/duser/directui/test/app/logonui/chess.bmp | logonui.exe | 114 | IDB_USER0 (the same bytes as shell/ext/logondui/chess.bmp); the picture accounts get is the file All Users/Application Data/Microsoft/User Account Pictures/Default Pictures/chess.bmp (another rendering) |

shellstyle.dll per variant: blue = Luna.msstyles' `Shell\NormalColor\shellstyle.dll`, metallic =
`Shell\Metallic\`, homestead = `Shell\Homestead\`, classic = `%windir%\system32\shellstyle.dll`. So an XP
import needs: system32/{shell32,browseui,explorer.exe,logonui.exe}, WinSxS comctl32 6.0, Luna's three
shellstyle.dll (lhde-import's styles/ copies Luna.msstyles; its Shell/ beside it would do) and system32's
classic shellstyle.dll.

## flxp on liblhde: the session theme from theme files (2026-10-28)

docs/THEME-FORMAT.md section 13, items 1, 2, 3 and 6 (wux side). flxp no longer reads a registry: the compatibility
`themes.ini`/`wallpapers.ini`, `_LH_THEMES_INI`, `[override:NAME]`/`[override]`, the presets of `~/.config/longhorn/themes`
and every `flxp_*` key are gone from flxp's code.

**Reading the theme** (`flxp/shell/lhtheme.{h,cpp}`): `SessionTheme` is liblhde's flat record (`keys`: `Section.key` and the
derived `path.*`, `style.*`, `theme`, `file`; `why`: `lhde_theme_why`; `body`: the text without `serial=`, to compare two
records; `msstyles` = `path.style`, `classic` for `style = classic`). `session_theme_from(name, published, serial)` takes
`lhde_theme_parse(_LH_THEME_RESOLVED)` when its `theme=` is `_LH_THEME` and its `serial=` is `_LH_SETTINGS_SERIAL`, else
`lhde_theme_resolve(_LH_THEME)` (an alias, a bare style, `themes/aero` work). `read_session_theme` does the same with a short X
connection at start-up (`setup_theme`); `resolve_theme(NAME)` is any theme (the dialog's list and sample). The shim
(`wux/w32/theme.cpp` `session_theme_refresh`, also exported as `w32::session_theme_refresh`) re-reads on `_LH_THEME`,
`_LH_THEME_RESOLVED` and `_LH_SETTINGS_CHANGED` (a theme file changed: `NAME-modified.theme`); a record with the same body
is nothing; one that differs only in `Desktop.*` (not `Desktop.icons`), `path.wallpaper` or the theme's identity (`theme`,
`file`, `chain`, `Theme.name/base/aliases`: a switch to the modified copy) becomes the session theme without WM_THEMECHANGED
(flxp-desktop handles it); anything else reloads the style when it changed and sends WM_THEMECHANGED as before.
`path.fonts` is added to the font path (was the registry's `fonts`).

**The key mapping** (`theme_setting`; `FLXP_<KEY>` in the environment, a run's option, still wins everywhere):

| flxp key (old source) | now |
|---|---|
| the style, `color`, `size` (`msstyles`, `flxp_msstyles`) | `path.style`, `VisualStyle.color/size`; `style = classic`: no style; no `-xp` backport lookup for a session theme |
| `icons` (`flxp_icons`, `[override] icons`, `[desktop] icons`) | `Desktop.icons` (`longhorn4074` is passed as `longhorn` while flxp's res/icons has no file of that name); `path.icons` is in the record for the icon code |
| `frame` (`flxp_frame`, `[explorer] frame`) | `Explorer.chrome` (auto / xp / longhorn); `Explorer.navbar = none` gives xp (XP's toolbars) |
| `header_engine` (auto: luxwm's frame set has a nav.ini) | `Explorer.navbar`: vision = luxnav with `path.navbar` (data/navbar/vision.ini), 4051 / 4074 = flxp's header; flxp.ini's `header_engine` stays a developer's override; without a session theme the old auto |
| `header_preset` (`flxp_header_preset`) | `Explorer.navbar_preset` for navbar = vision (auto: the file's own preset); for 4051/4074 it is the explorer's default header size (small / medium / large / xlarge) under flxp.ini's `header_size` |
| `taskbar_kind` (`taskbar`, `[taskbar] kind`) | `Shell.taskbar` |
| `start_menu` (`start`, `[start] menu`) | `Shell.start = lux` -> lhshell; otherwise flxp.ini's `start_menu` (panel / classic, the user's) |
| `longhorn_system32` (`flxp_longhorn_system32`) | flxp.ini, else `path.resources` when it is a Longhorn build's (resources/NNNN; not xp / 2k) |
| `menubar_style` (`flxp_menubar_style`) | flxp.ini, else `document` for `Explorer.chrome = longhorn` |
| `explorer_resources` (`flxp_explorer_resources`) | flxp.ini only (no theme sets it) |
| `font_smoothing` (`[fonts] smoothing`) | settings.ini `[fonts] smoothing` (the user's, Effects tab), else `Fonts.smoothing` |
| the wallpaper (`_FLXP_WALLPAPER`, flxp.ini, `[override:NAME]`, the preset, wallpapers.ini) | `path.wallpaper` (`Desktop.wallpaper`; none = colour), `Desktop.mode`, `Desktop.color`; settings.ini `[desktop] wallpaper = none` (luxshell's "Show the wallpaper" off) above it; `--wallpaper` until the theme changes |
| the theme's build (`theme_build`) | `Theme.build`, else `path.resources`' build, else the digits of the name / style / fonts |

**flxp.ini migration** (one note on stderr per key and process, `config.cpp retired_key`): THEME-FORMAT does not make flxp.ini
keys user overrides of theme keys, so `icons`, `frame`, `taskbar_kind`, `wallpaper`, `wallpaper_mode` and `themes_ini` are
ignored in flxp.ini ("flxp: .../flxp.ini's icons = 2k is ignored: the theme's Desktop.icons decides (Display Properties)");
set the theme key in Display Properties instead. `start_menu = lhshell` is ignored the same way (`Shell.start`). The template
no longer writes them. Every other flxp.ini key is unchanged.

**The store** (`flxp/shell/settings.{h,cpp}`): `store::path()` is `lhde_settings_path()`, writes go through
`lhde_settings_set`; `value_for` maps only the per-user keys `font_smoothing` and `pinned`. `store::set_theme_keys(THEME,
{key, value}...)` writes with `lhde_theme_set_key` (THEME's own file when it is the user's, else `THEME-modified.theme`, the
following keys into the name it returned), then `apply_theme(selected)` when the name changed, else `notify("theme")`.
`store::theme_list()` is `lhde_theme_list` (group 0 the user's, 1 shipped, 2 bare styles). Removed: `theme_value`,
`theme_default`, `set_theme_values`, `set_all_themes_values`, `registry_path`, `registry_section`, `theme_names`,
`preset_*`; `lookup_session_theme`, `resolved_field`, `read_session_properties`, `loadable_msstyles`, `is_preset_name`,
`lhde_registry`, `own_wallpaper`, `wallpapers_ini`.

**Display Properties** (`flxp/settings/settings.cpp`, `flxp/rc/settings.rc`): the theme list is liblhde's (user's themes,
shipped, then bare styles marked "(style)"). Theme keys shown with the line `lhde_theme_why` gives ("Set by the theme",
"From the theme jade4074", "Set in your theme file (jade4074-modified.theme)", "The style's default", "Fallback: ..."):
Desktop.icons (Desktop tab), Explorer.chrome, Explorer.navbar (new combo), Shell.taskbar, Shell.start, Glass.composition
(auto/on/off), Glass.animations (Appearance tab), Desktop.wallpaper and Desktop.mode (Desktop tab). Apply writes the keys that
differ from what the selected theme gives with `set_theme_keys` (the first change creates and selects NAME-modified), then
the theme (once) or `_LH_SETTINGS_CHANGED`. The scope combos ("This theme only / All themes / Theme defaults") are gone.
Per user in settings.ini: `[sidebar] mode` (shown: the user's, else the theme's `Shell.sidebar`), `stacking`,
`with_classic`, `[fonts] smoothing`. The background list: (None) = `Desktop.wallpaper = none`; "NAME (theme background)" =
the base theme's picture for a NAME-modified (choosing it removes the key again); pictures in a data directory's
`wallpapers/` are written as `GROUP/STEM` (`2k/blulac16`), others as absolute paths (allowed in the user's files).
Save As (section 3): pending changes applied, then NAME-modified.theme is renamed to the new name (title lower-cased,
`[a-z0-9._-]`, spaces as `-`), another user theme is copied, a shipped theme or bare style gets a new file with `base =`
it; the new theme is selected. Delete: a user theme file; its base is selected (applied when it was in force).

**Set as Desktop Background** (`verbs::set_wallpaper` -> `shell::set_desktop_background`): `Desktop.wallpaper` + `Desktop.mode`
into the session theme's modified copy, `[desktop] wallpaper = none` removed, the theme selected and announced; no
`_FLXP_WALLPAPER`. **flxp-desktop** reads the record (`theme_wallpaper`: `path.wallpaper`, `Desktop.mode`, `Desktop.color`
under and around the picture; was the constant 58,110,165), watches `_LH_THEME`, `_LH_SETTINGS_CHANGED`,
`_LH_THEME_RESOLVED` and refreshes the record itself before it draws (`w32::session_theme_refresh`). luxshell still
writes `_FLXP_WALLPAPER` (its bridge); nothing reads it now.

**tests**: `lhdatatest` (now 55 checks) builds a fake import with theme files (the probe replaced, LHDE_TREE=0): the record,
aliases, the mapped keys per theme, the published record current vs stale serial, `wallpaper_value`, `set_theme_keys` writing
and selecting `slate4051-modified`, `theme_list`, the retired flxp.ini key, the store path, the report.
`tests/check.sh`'s ut-dump takes `styles/` from `$LHDE_DATA` (else `~/.local/share/lhde`; `LHDE_STYLES` overrides).

**Measured** (Xvfb :74-:76, luxwm from this tree, scratch HOME with copies of ~/.config/lhde and ~/.config/flxp,
LHDE_DATA = ~/.local/share/lhde): HEAD before (37ac621, compatibility registry) against the same commit with only these
changes, explorer + flxp-desktop per theme, whole screen: slate4051, slate4074, jade4074, luna: 0 pixels differ. aerovision:
3113 px (the header title: luxnav now gets `path.navbar` = vision.ini with preset small, the old code found no nav.ini in
luxwm's frame set and used luxnav's defaults); basic5112: the vision header with vision.ini's own preset (big) instead of
luxnav's defaults (same cause; set `navbar_preset = small` in the theme for the compact one). Live: `_LH_THEME` switched
among slate4051, slate4074, jade4074, luna, basic5112, aero4074, aerovision, aero5112 (picom for the aero ones) restyles
explorer, desktop and taskbar; Display Properties: navbar 4074 -> vision on jade4074 wrote `jade4074-modified.theme`
(`[Explorer] navbar = vision`), selected it, luxwm re-resolved and republished the record, the explorer switched to luxnav;
a background from the list (`2k/blulac16`), (None) (`wallpaper = none`, the theme's colour), Save As (copy of a user theme,
rename of slate4074-modified to `slate-three`), Delete (back to slate4074); Set as Desktop Background > Tile on a .bmp:
`slate4074-modified.theme` with the absolute path and `mode = tile`, the desktop followed at once. Not reproduced but seen
once on :74: after the navbar switch with an explorer under the dialog, the property sheet's margins kept stale pixels until
the dialog was moved (a missed repaint, not seen in a run without the explorer underneath).

## The -xp backports dropped: the side-by-side check (2026-10-28)

THEME-FORMAT decision 1: every component uses the theme's one style; the XP backports
(`styles/jade4074-xp`, `styles/slate4074-xp`, `luna-xp`) and `flxp_msstyles` are gone. Before
that, the same programs were rendered under the real v4 styles and under the backports that flxp
used to load instead (the old registry: jade4074 and aero-patch -> jade4074-xp, slate4074 and
slate4051 -> slate4074-xp; luna-xp.msstyles is byte-identical to the imported XP Luna, so luna
has nothing to compare). Xvfb 1280x1024, no window manager, scratch HOME, the HEAD binaries of
dcdd051, one registry with a v4 and a `-bp` section per theme (the `-bp` one adding
`flxp_msstyles`), controls/comctl with FLXP_THEME = the style. Pixels differing (v4 vs backport):

| theme | explorer (Longhorn chrome) | explorer (XP chrome) | file dialog | taskbar | start panel | settings | controls | comctl |
|-------|-----|------|---|-----|------|----|------|------|
| slate4074 | 0 | 3222 | 0 | 606 | 1115 (610 after the fix) | 0 | 0 | 1702 |
| jade4074 | 0 | 3309 | 0 | 0 | 774 (0 after the fix) | 0 | 0 | 1733 |
| aero-patch | 0 | 3309 | 0 | 0 | 774 (0 after the fix) | 0 | 0 | 1733 |
| slate4051 (backport: 4074's) | 410 | 3266 | 344 | 606 | 1115 | 32 | 1138 | 5058 |

Every difference, its cause (`ut-dump --list` of both, compared property by property) and the decision:

1. **All Programs drawn regular instead of bold** (start panel, every Longhorn style): the v4 file stores
   `StartPanel.MOREPROGRAMS` FONT as a LOGFONT with the face "Tahoma Bold" (Jade: "Segoe UI Bold") at weight
   400; Windows' font mapper matches that full name and draws the bold face, ours matches families only; the
   backport wrote "Tahoma, 8, bold". **Fixed in wuxtheme** (`v4.cpp` logfont_text: a " Bold"/" Italic" suffix
   of the face becomes the attribute, as font_text already did for the text form; b702007). Also affects
   `TaskBand.GROUPCOUNT` and the other "* Bold" faces of 4015-5112 (`ut-dump --get StartPanel 2 0 FONT`:
   "Tahoma, -11, bold", weight 700).
2. **Rebar band label** (XP chrome's "Address", comctl's rebar): slate's `Rebar` TEXTCOLOR is 255,255,255
   (Jade's 0,2,92); the backports made both 0,0,0. The v4 value is what 4074 means: its Rebar text sits on the
   dark `Document::Rebar.BACKGROUND` of the command bar (white on blue in flxp's Longhorn chrome, identical
   there). In the XP chrome the plain Rebar background is light, so slate's label is white on light grey, as
   a real comctl32 6 (DrawThemeText with the Rebar class) would draw it. **Accepted**: the backport's hand
   tuning; no theme file pairs a Longhorn style with chrome = xp (Explorer.chrome = longhorn in all of them).
3. **Rebar band edges** (XP chrome, comctl): `Rebar.BAND` EDGESHADOWCOLOR 125,125,125 (Jade 150) and no
   EDGEHIGHLIGHTCOLOR in v4; the backports set 218 and 250 for a fainter etch. **Accepted** (hand tuning; the
   style's own values are read correctly).
4. **Toolbar separators** (XP chrome, comctl): 4074's `Toolbar.SEPARATOR` image (#274) is an 8x5 32-bit
   bitmap with every pixel 0 - transparent on purpose (4074's command bar separates by spacing); the
   backports drew their own line (`id9001.bmp`, 191,191,197) and gave Jade sizing margins. **Accepted**.
5. **Slate's start button text** (taskbar, 606 px): `Start::Button` CONTENTMARGINS 7,19,2,4 in v4,
   7,19,1,1 in the backport (Jade 1,2: its start button is identical anyway). **Accepted** (hand tuning).
6. slate4051 only: its own parts (blue `Edit` BORDERCOLOR 77,124,175, its combo arrow and scroll bar
   images, 4051's start button) against the 4074 backport it used to get. Not backport tuning: the v4 4051
   style is the right one.
7. Same in both: font sizes ("-11" pixels = "8" points at 96 dpi), TRANSPARENTCOLOR on images that have
   alpha, IMAGECOUNT of the backports' padded strips, the v4-only classes (Navbar, AnimationButton, Link,
   ListView groups, Menu POPUP* parts) the backports lacked.

Result: under the v4 styles the Longhorn chrome, the file dialog, Display Properties and the controls test are
identical to the backports; the start panel is identical after the font fix; what remains are the
backports' hand-tuned rebar/separator/start-button values in the XP chrome. Screenshots: the session's
scratch directory only (`bp/shots`), no Microsoft renders in the repo.

## XP and 2000 art by resource id (2026-10-28)

nt5src is gone from flxp's code and data (no `nt5src=`, no `make install-icons`, flxp/res/icons keeps only
wine.theme). The art comes from lhde-import's `resources/xp` (Windows Server 2003 R2 x64: XP's resources)
and `resources/2k` (Windows 2000 SP4), and from the Luna import's `styles/luna/Shell/<color>/shellstyle.dll`.

- **Icon recipes** are `data/icons/{xp,2k,longhorn4074}.theme`, looked up as `icons/NAME.theme` in lhde's data
  directories (`shell::icon_theme_file`; the old name `longhorn` is longhorn4074; flxp's res/icons/wine.theme
  stays the fallback). `[theme] resources = resources/BUILD` is the base of `pe:DLL#ID` (`base =` still works
  for a user's own recipe); a relative path starting with `resources/` names another build's file (the 2k
  recipe's missing ids, longhorn4074's toolbar). `[toolbar]` sources are RT_BITMAP ids. A recipe whose
  resources are not imported is unusable: Wine's icons, with one message naming lhde-import.
- `shell::resource_file(build, file)`, `resource_src(build, file, id)`, `shellstyle_dll(style, color)`,
  `load_art("pe:PATH#ID" | file)` (flxp/shell/icons.h); `tb::xp_explorer_bitmap(id, flags)` (taskbar.h).
- Task pane (places.cpp): the style's `Shell/<color>/shellstyle.dll` beside it or in `styles/NAME/Shell/`
  (XP's own layout, and lhde-import's), the colour matched case-insensitively; another style without
  ExplorerBar parts takes Luna's NormalColor; classic takes `resources/xp/shellstyle.dll` (CLASS_SS).
- `flxp --info` lists `resources/xp` and `resources/2k` instead of the nt5src line.

The ids, verified against the imported DLLs (`bitmap` = extracted and byte-compared with the nt5src file it
replaces; all identical unless noted):

| what | source | id | check |
|------|--------|----|-------|
| toolbar strip 24 / hot | xp shell32.dll | 214 / 215 | 1128x24 32-bit, identical |
| toolbar strip 16 / hot | xp shell32.dll | 216 / 217 | 752x16, identical (4074's: 768, one more cell, so longhorn4074 keeps XP's) |
| Go button / hot | xp shell32.dll | 230 / 231 | 20x20, identical |
| brand (throbber) | xp shell32.dll | 240 | 22x1100, identical |
| 2k toolbar 20 / hot, 16 / hot | 2k browseui.dll (xp's the same) | 276 / 277, 278 / 279 | 940x20 and 753x16 8-bit, identical |
| Folders bar close | xp browseui.dll | 545 | 39x11, identical |
| start flag / classic | xp explorer.exe | 143 / 176 | 25x20 / 16x16, identical |
| Log Off buttons / hot | xp explorer.exe | 7010 / 7011 | 72x24, identical |
| task pane up, uphot, down, downhot | Luna Shell/{NormalColor,HomeStead,Metallic}/shellstyle.dll; classic: system32's | 100-103 | identical (all four colours checked) |
| task pane header | the three Luna shellstyle.dll | 112 | 186x21 8-bit, identical (not in the classic one) |
| user tile (chess) | xp logonui.exe | 114 | not imported yet: data/sources/xp.ini now lists `LOGONUI.EX_`; until a re-import the panel shows the name only |
| file dialog view/hist strips | comctl32 6.0 | 124 / 130 | not imported: resources/xp/comctl32.dll is 5.82 (4-bit Windows 2000 art); 6.0 is in the ISO's `AMD64/ASMS01.CAB` (WinSxS), which lhde-import cannot unpack yet (a cabinet inside the layout). The xp recipe leaves them out: comctl32's own images |
| icons | xp / 2k / 4074 shell32.dll | as in the recipes | RT_GROUP_ICON present for every id in xp and 4074; 2000's shell32 lacks 224, 225, 226, 235, 236, 294 (mydocs.dll et al.): the 2k recipe takes XP's, 4-bit frames |

Recipe ids corrected to the files they replace: `image` 226 (pics.ico, was 236 = mypics), `disc` 294 (cdrom.ico,
was 12 = the drive), `recent` 21 (stdocs.ico, was 20 = Programs) - in xp, 2k and longhorn4074 (the old
longhorn recipe had the same three). `control_panel` stays 137 (IDI_CPLFLD): nt5src's control.ico is in no
.rc file.

### Measured against the nt5src build

Old = dcdd051's binaries with nt5src= set; new = this change, no
nt5src, LHDE_DATA = the user's import with `icons/` from this tree (the installed `~/.local/share/lhde/icons`
are the lux side's earlier copies without [toolbar]: `make install` refreshes them; with the old copies the
icons load and the toolbar falls back to comctl32's images). Pixels differing (whole screen):

| theme | explorer (XP chrome) | task pane | Folders bar | taskbar | start panel | file dialog | settings |
|-------|----|----|----|----|----|----|----|
| luna | 0 | 0 | 0 | 0 | 4112 | 828 | 0 |
| luna-olive (homestead), luna-silver (metallic) | | 0, 0 | | | | | |
| classic + 2k icons | 297 (places pane included) | | 127 | 0 | 4457 | 814 | |
| slate4074 (longhorn4074 icons) | 0 (Longhorn chrome), 0 (XP chrome) | | | | | | |

Explained: luna's start panel = the chess tile (logonui.exe not imported) and Control Panel (137, Server
2003's icon, against nt5src's unused control.ico); luna's file dialog = the view/hist strips (comctl32 6.0
not imported). Classic + 2k = the real Windows 2000 icons where the old set had the XP icons' 4-bit frames
(the generic document #1 with the 2000 window logo, My Computer #16, Control Panel #137, Run #25, Search
#134) plus the chess tile. Everything else, including the taskbar's start flag and the task pane in all three
Luna colours, is pixel-identical. (The run used FLXP_THEME=luna.msstyles for the taskbar: with the session
theme alone flxp-taskbar draws unthemed in c7b2e82, as before this change - not an art problem.)
