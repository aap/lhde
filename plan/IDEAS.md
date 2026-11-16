# Ideas

Visions and wishes, one line each, with the date and who said it (aap unless marked). Nothing here is agreed work: an idea
becomes a plan file (plan/NAME.md, with a status line) when work on it starts, and leaves this list then.

## Themes and the look
- 2026-10-31 aap: the aurora example (examples/aurorademo.c) in the Longhorn flxp as an option: the PDC 2003 4050 demo's last
  piece; decide its extracted folder colour table (Aurora.c_rgColorMap) then.
- 2026-10-31 aap: the Slate vision and Metal vision themes (share/themes/*.theme.todo).
- 2026-10-31 aap: a graphical theme installer and a desktop with nothing imported: plan/THEMING.md.
- 2026-11-13 aap: the colour label as an icon overlay badge, XP's idiom (a dot of 8-10 px with a 1 px dark edge at the icon's
  bottom right in every view, scaled with it); the Details column stays for sorting; a hex field in the Metadata tab later.
- 2026-11-16 Firefox: a GTK theme from the visual style for what lhde-firefox's userChrome.css cannot reach (menus, context
  menus, popups, form widgets); lhde-firefox install run on a theme switch from luxshell's lh-theme handler (closed profiles).
- 2026-11-01 survey: E's theming as a concept: one theme skins WM, shell and every app, an app adds or overrides a few parts; a
  runnable one-file example per widget.

## Explorer and metadata
- 2026-11-01 aap: 4074's (i) icon in the band's top right to flip between details and tasks (4051 is the target; undecided).
- 2026-11-09 aap: saved filters (the Filter box's text kept by name, 4051's "All Photos" queries).
- 2026-11-01 aap: tasks as data (a task table with experiences and per-selection names; the research's TASKS-SUMMARY):
  plan/METADATA.md step 3.
- 2026-11-01 aap: luxfs (~/src/luxfs, the xattr tools): move it into lhde or keep it separate.
- 2026-11-01 survey: Haiku's lesson: People, Mail and bookmarks are attributed files that Tracker and Find query; aim the
  catalogue at that (data as attributed files), not only at speed.
- 2026-11-15 aap: photo query views, bulk tagging and a static gallery per folder or query: plan/CATALOGUE-REMOTE-PHOTOS.md step 5.
- 2026-11-15 aap: "My machines": a My Network Places window over machines.ini (each machine a computer icon, its roots as folders)
  and a machines tile in luxshell: plan/CATALOGUE-REMOTE-PHOTOS.md.
- 2026-11-15 aap: ZFS admin in wux (pools, datasets, snapshots, scrub, used/avail) in XP's Disk Management idiom, local or over the
  ssh helper; more wux front ends for OS work (mounts, services, users) the same way.

## The shell (luxshell, flxp-taskbar)
- 2026-10-31 aap: a centred window-buttons mode for the plex and concept taskbars.
- 2026-10-31 aap: 4074's own Avalon context menu look as an option ([effects] menus = avalon; lh4074-091929: white plate, 1 px
  #919191 frame and shadow, 18 px highlight bar) drawn from the painter's layout.
- 2026-11-13 aap: tray items for network/wifi and bluetooth beside Volume, Brightness and Touchpad.
- 2026-11-14 aap: KDE Connect over kdeconnectd's D-Bus without KDE: a phone tile (battery, ring), its notifications mirrored into
  the notification tile, send-to-phone in flxp's context menu ("the sidebar tiles will almost write themselves").
- 2026-11-01 survey: a launcher with search (E's "everything": apps, files, settings, calculator, clipboard history;
  flxp-startmenu's search box is the seed).
- 2026-11-01 survey: a volume mixer, a clipboard manager, a screen lock and a logout/suspend menu.

## Programs
- 2026-11-12 aap: luxpad++ on SciTE: Scintilla's win32 variant on wux's GDI and the SciTEWin frontend (needs RegisterClipboardFormat,
  more GDI, CreateProcess/CreatePipe shims); the test of wux's drawing at volume. Scintilla and Lexilla are not in ~/othersrc yet.
- 2026-11-01 aap: a more elaborate calendar to supplement the sidebar's.
- 2026-11-14 aap: ood, a binary editor after xvi32 with the focus on octal, for PDPs: 8-bit bytes, 16/18/36-bit words, encodings
  (larsb's pdp10-its-disassembler), ascii, FIODEC, flexowriter, squoze/radix50, sixbit.
- 2026-11-14 aap: a greeter that is XP's Welcome screen (logonui.exe's art), on wux as a LightDM greeter (liblightdm-gobject) or a
  greetd one, once the display manager path (docs/BUILDING.md) is in use.
- 2026-11-01 survey: a themed terminal; Haiku's Expander (archives), ActivityMonitor (Task Manager) and DiskProbe as models.

## Platforms
- 2026-11-14 aap: lhde on Fedora (soma dual-boots it), a second Linux distribution for the out-of-the-box test.
- 2026-11-14 aap: a Wayland port experiment, on Fedora (wux's X layer is wux/sys/x11.cpp; luxwm is X11 through and through).
