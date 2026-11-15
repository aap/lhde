# Bugs and measurable gaps

One line per concrete item (two at most), dated when it was recorded, grouped by area. A fixed item leaves in the commit that
fixes it; an item found wrong or decided against leaves with a one-line reason in the commit message. Longer things are a plan
file (plan/*.md); wishes are plan/IDEAS.md. "(check)" marks an item that may already be done.

## Explorer (flxp)
- 2026-11-16 aap: in Details only the area around an unselected row's text counts as the file (hover, click), a selected one's whole row
  does. Not reproduced headless (aerovision, jade4074, slate4051 Details: hover and click take every column; detailsrow.sh); case needed.
- 2026-10-31 XP's Customize Toolbar dialog (View > Toolbars > Customize...: buttons, text options) is not there; only Icon Options is.
- 2026-11-13 After luna -> jade4074 -> classic the menu band's items sit 1 px off a fresh classic start's (File..Help, x 8..241;
  themeswitch.sh allows 0.5 %); a fresh classic Explorer draws a black 1 px line left of the Go strip that a switched one does not.
- 2026-11-13 The classic icon set (2k) has no Go glyph, so a classic Explorer has no Go button; XP's has one (WinXP-193211.png).
- 2026-10-31 The Filter box's text sits 2 px higher and 1 px further left than 4074's (Jade, 4074-dwm.png: cap top 8 px below the
  box's top, 5 px in; ours 6 and 4): 4074's box is not a plain Edit with a client edge (1 px rounded border).
- 2026-11-15 Shell.start_icon: the Explorer's brand band changes only at its next theme change, not live as the taskbar does.
- 2026-11-01 5048 and 5112: Display Properties still offers Small/Big/Concept header sizes for them; the Vista era has none.
- 2026-11-09 While a filter is on, 4051's band still says "N items in this folder" for the items shown (the status bar: "N of M").
- 2026-11-09 The panes do not follow an attribute changed by another program: changed_on_disk() refreshes the rows, not the Details
  rollout / 4051 band of the selection (setfattr in a terminal: old value until the selection changes).
- 2026-11-01 The 4051 band's invitation colour and the Choose Details dialog's layout are unmeasured choices.
- 2026-11-01 The property registry's defaults are not in a shipped data file with user overrides (the shape is there, no loading).
- 2026-11-15 Network folder: readdir, folder_for's stat and the tree's listing of the parent still run on the UI thread (0.8 s for
  2000 entries in slowpath.sh at 50 ms per 128-entry reply); a hung server still hangs the window. A worker readdir in batches would not.
- 2026-11-15 Network folder: Tiles and the details leave out a picture's Dimensions, the infotip shows only the stored properties a
  column read; the lister could read the header (and the xattrs) for the items on screen.
- 2026-11-15 Network folder: what the user asks for still reads synchronously (Add Tag's selection_tags reads every selected
  file's xattrs, Open's mime_type stats and sniffs, Properties); 500 selected files = 500 round trips before the dialog.
- 2026-11-15 Network folder: the task band shows "Add a description" for the moment (0.1-0.3 s) before the lister has read the
  selection's stored properties; an edit begun then starts empty.
- 2026-11-15 The network thumbnail budget is per Loader: thumbview's and the two panes' loaders (preview.cpp) each have one, so a
  folder may read up to three budgets whole.
- 2026-11-14 Properties sheet, General page: the "Opens with" program's icon is drawn over the start of its name ("[icon]pad" for
  luxpad; lab, luna and jade4074, luxattr --general).
- 2026-11-13 Properties sheet: a binary value shows escaped (\xNN) in the Advanced list, but an edit of it is written back as text.
- 2026-11-13 Properties sheet: the Advanced list's in-place editor is one line, so a value of several lines (notes) cannot be edited.
- 2026-11-13 Properties sheet: several files with different comments show an empty Comment without the "(several values)" cue.
- 2026-11-13 Thumbnails the memory bounds refuse: a progressive JPEG above about 53 MP, an interlaced PNG, BMP or ICO above 25 MP;
  the JPEG's EXIF thumbnail (160 px) or a row-by-row read of the PNG's interlace passes would give them one.
- 2026-11-14 A thumbnailer child's memory is not bounded (posix_spawn has no rlimit action: a prlimit wrapper or fork+setrlimit).
- 2026-11-14 Thumbnailer entries take the type from the name only (a PDF without .pdf gets no thumbnail).
- 2026-11-14 lhde-postscript renders onto Ghostscript's default page when the file names no size (ImageMagick PS without
  setpagedevice is cropped to letter).
- 2026-11-14 A thumbnailer timeout leaves the failure marker until the file changes, also when the machine was only busy.
- 2026-11-13 The thumbnail disk cache (~/.cache/thumbnails) is never cleaned: drop large/ and normal/ entries of gone or
  months-unopened files.
- 2026-11-14 Folder Options' File Types: New Action's Browse... and Create New Extension's "Advanced >>" (collapsed) are missing.
- 2026-11-14 File Types: the rows show our icon set's file icons, not the icon theme's per-type icons; the measurements are
  ReactOS's template fitted to our 227 x 215 pages, not XP's.
- 2026-11-14 File Types: a Restore leaves empty [Default Applications] / [Added Associations] headers in mimeapps.list (GLib too).
- 2026-11-14 Default Programs: ten radio slots, so a role with more than nine programs shows the first nine (prefer= order).
- 2026-11-14 Default Programs: the terminal ([commands] terminal) is no role yet (candidates from Categories=TerminalEmulator).
- 2026-11-14 Default Programs: XP's "Enable access to this program" boxes and its Computer Manufacturer / Microsoft / Non-Microsoft
  configurations are not there; the layout is ours (XP's lives in Add or Remove Programs as one list of groups).
- 2026-11-14 With PREFIX=~/.local the role file and make install's lhde-mimeapps.list are one path (check that neither clobbers).
- 2026-11-01 Naming leftovers: 105 flxp functions and globals with external linkage nothing else references (nm: header::size,
  browser::zoom_at, shell::thumbs::md5_hex, ...): make them file-local or remove them, component by component.

## Desktop and bars (flxp-taskbar, the start panel)
- 2026-11-13 flxp-taskbar: the grippers of an unlocked bar do not drag (Quick Launch and the task band cannot move or resize).
- 2026-11-13 The tray chevron counts only an icon's changes as use (a click goes to the program); XP also counted clicks.
- 2026-11-13 The tray offers the default 24-bit visual, so an ARGB icon with a transparent surround shows it black
  (luxshell/tests/trayicon; luxshell's tray is 32-bit).
- 2026-10-31 Start panel, styled: XP's pinned items have a second line (the program's description) that ours lack.
- 2026-11-15 The Avalon packs' Start buttons keep their flag (bitmaps); a pack key for a separate flag image (as 4042's
  start_compose) would let them follow Shell.start_icon.

## Dialogs and wux
- 2026-11-01 Appearance's style list shows a DisplayName outside Latin-1 as '?' (zxbiyao's 诛仙主题之碧瑶版(绿色) as
  ????????(??)): style_label goes through narrow strings.
- 2026-10-31 Keyboard cues: Wine's buttons, statics and tabs draw mnemonics without asking WM_QUERYUISTATE, so dialog labels stay
  underlined; XP hides them until Alt.
- 2026-10-31 Resize flicker (a lead): wux windows use ForgetGravity, so a glass window's transparent background shows before the
  repaint; try NorthWest gravity and a background of None under picom.
- 2026-11-01 4074's About box is Segoe UI 8 under Jade (4074-dwm.png); ours should name Segoe UI in its template under the
  Longhorn styles (MS Shell Dlg stays Tahoma).
- 2026-11-13 A dropped menu under Jade has a 1 px white line inside its outline where 4074's is the body's 245 grey
  (lh4074-083401.png: the Edit menu's outline at x=78, 245 from x=79).
- 2026-11-14 Ctrl+digit posts WM_CHAR of the digit (Ctrl+1 gives 0x31); Windows gives none for Ctrl+1..9.
- 2026-11-01 flxp-filedialog: a right click right after the pointer arrives opens the context menu and closes it at once (Xvfb,
  xdotool's burst press+release past a synchronous grab, as menus.sh works around) (check with a real mouse).

## luxshell
- 2026-11-13 The taskbar view's tiles (taskbar_tiles) cannot be removed by the user.
- 2026-11-14 Font linking's table (share/fonts/fontlink.ini) is not loaded by luxshell, luxnav or luxwm's ut_ text: call
  ut_load_font_links(lhde_data_find("fonts/fontlink.ini")) at startup beside ut_add_font_dir.

## luxwm
- 2026-11-01 luxwm does not reap the programs it starts (`flxp <defunct>` zombies under :1's luxwm, from Run or a key binding):
  it ignores SIGCHLD only when a config file was found; a SIGCHLD handler with waitpid(-1, WNOHANG).
- 2026-11-01 msstyles window shape: only leading/trailing alpha-0 runs of a caption row are cut (chaninja, dzbio, mxmqj, octane keep
  black inside); XP's region is every opaque pixel of CAPTION (GetThemeBackgroundRegion): several rectangles per row.
- 2026-11-13 Click to focus grabs buttons 1-3 on every client always (GrabModeSync, ReplayPointer): a round trip per click, also
  on the focused window; if that ever shows, grab only on unfocused windows (dwm's way).
- 2026-11-12 The Alt+Tab switcher's geometry is ReactOS appswitch.c's, unmeasured: VM shots of XP's and 4074's (classic and
  Luna/Jade, 3 and 9 windows) to compare (docs/LUXWM.md "Task switcher").
- 2026-11-14 No help button (?) for a window with WS_EX_CONTEXTHELP, and no disabled minimize button for one with maximize only.
- 2026-11-14 A message box and a property sheet show an icon in the caption; XP's have none (WS_EX_DLGMODALFRAME / DS_MODALFRAME):
  wux has to say so (no _NET_WM_ICON is not enough: luxwm then draws its default).

## Themes and the import
- 2026-11-01 RealAero 0.3 moved the Explorer plates' header lines up 3 rows (76/77, not 79/80): realaero's explorer_rows /
  max_explorer_rows fit pre-0.1, so under 0.3 the header loses its lines (values per release, or fit to 0.3).
- 2026-11-01 lhdebaml cannot decode RealAero 0.3's patched WCPClient.dll desktopaurora.baml (34720 bytes, "BAML 2004: bad record
  0x0 at 0x84b8"); BetaWiki says the package's BAML is in the correct format.
- 2026-11-01 Aero patches other than RealAero (twiwmtb-aero) borrow realaero's frame geometry: no caption-button pill, the white
  maximised caption text on light glass; fit when a 4074 screenshot with that style exists.
- 2026-10-31 Aero vision's resources resolve into the research workspace ([elsewhere] in explain); the import should own them (check).
- 2026-10-31 The ReactOS styles' start panel user pane is a blank white band (no StartPanel UserPane art?): compare with ReactOS.
- 2026-11-01 lhde-import annotates every source's files author = microsoft (lhdetree.py's xput), ReactOS's and the Pexels photos
  too: a source ini key (author = reactos, ...) would say whose they are.

## Catalogue
- 2026-11-15 lhde-cat: relations cannot be confirmed from the command line; resolve applies every conflicting op, not the winner only.

## FreeBSD
- 2026-11-14 The battery is the sysctls' sum: no model, capacity or per-battery rows in the power flyout (ACPIIO_BATT_GET_BIX /
  GET_BST on /dev/acpi, group operator, would give them).
- 2026-11-14 The touchpad item is unverified on a real X server (Xorg's log on soma shows the "SynPS/2 Synaptics TouchPad").
- 2026-11-14 FreeBSD 15's inotify may give no IN_ATTRIB for a watched folder's entries, so changed attribute columns wait for F5;
  a per-file watch or a ctime poll of the shown rows (check).
- 2026-11-15 tests/headless/slowfs.c (the slow-path shim) is unbuilt and untested on FreeBSD (extattr_*, statfs's f_fstypename).

## Tests
- 2026-11-14 The headless tests' luxshell runs `pactl subscribe`, which autospawns pulseaudio where it is installed but not running:
  it writes ~/.config/pulse into the scratch HOME and lingers; set PULSE_SERVER to nothing or stop it.
- 2026-11-15 slowpath.sh covers the Icons view sorted by Name only: the once-only re-sort of a size or date sort and the Details
  columns filling in on a network folder have no test.

## Repository
- 2026-11-14 Publish the free pack: a GitHub release tagged `resources` with lhde-resources-free-3.zip (FREE_PACK_URL's default,
  FREE_PACK_SHA256 its hash).
