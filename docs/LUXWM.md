# luxwm: a Longhorn-style window manager for X11

A fork of [berry](https://github.com/JLErvin/berry) (MIT, Joshua L Ervin; see luxwm/LICENSE) that keeps its reparent-free design:
every client gets a separate decoration window stacked directly beneath it. luxwm makes that window a 32-bit ARGB surface and paints
the frame into it with cairo. With a compositor (picom with `session/picom-glass.conf`) a DWM theme draws the glass frame of 4074 and
later (plate, edge overlay, caption buttons) with the desktop blurred through it; without one the frame is the classic one, drawn
live from the visual style through libwuxtheme, or Windows Classic. The window manager's name stays "lhwm" (`_NET_WM_NAME`, the
WM_CLASS of its own windows `lhwm-menu`, `lhwm-snap`); `make install` adds `lhwm` and `lhwmc` symlinks. luxwm/README.md lists the
sources. Reference screenshots named below lie in `~/fun/longhorn/screenshots`, the `compare_*.png` crops in `~/fun/longhorn/x11`
(both outside the repository).

What luxwm adds to berry:
- `frame.c`: the frame-set loader (theme.ini + PNGs, or the style through libwuxtheme), the 9-slice plate painter (active/inactive
  halves of LGFRAME), the edge overlay, four-state caption button sprites with their blue/orange glow plates, caption text with
  TextGlow (pangocairo), the msstyles and Windows Classic painters, and the hit tester (buttons, caption, edges, corners).
- geometry: asymmetric frame margins from the theme (`margins()`) instead of berry's border/title arithmetic; `_NET_FRAME_EXTENTS`.
- input: hover/pressed button states, press-and-release-inside semantics, edge and corner resizing with cursors, caption double-click
  maximises (berry's monocle), minimise unmaps.
- cooperating clients: `_MOTIF_WM_HINTS` with decorations off, or an `_LHWM_SELF_FRAMED` property, means no decoration window.
- EWMH (below), the window menu, snap, the session theme, animations; existing windows are adopted at startup.
- a berry bug fixed: events of extension types (>= 64) indexed past the handler table.

## Running and testing

Build: the top-level `make` builds `build/bin/luxwm` and `build/bin/luxwmc` (`luxwm/luxwm.mk`: x11, xext, xfixes, xinerama,
fontconfig, xft, cairo, cairo-xlib, pangocairo, liblhde and the tree's libwuxtheme, compiled with `-DHAVE_UXTHEME`).

    luxwm [-d] [-t THEME] [-c AUTOSTART] [-f FONT] [-h] [-v]

- `-t THEME` (or `LHWM_THEME=THEME`): a theme name liblhde resolves (docs/THEME-FORMAT.md: a theme file, a bare style), an alias
  (`aero`, `jade`) or a path whose last component is one; default `aero-patch` (aero4074). It counts only while the root has no
  `_LH_THEME` yet ("Themes and frame sets").
- `-d` logs what luxwm does (`hints 0x...`, refused restacks, ...); `-c` berry's autostart script, default
  `~/.config/lhde/luxwm-autostart`, run only when it exists; `-f` berry's font.

Environment: `LHWM_THEME` (as `-t`), `LHWM_THEME_SOURCE=dir|lib` (frame-set PNG caches or the style through the library, "libuxtheme
source"), `LHWM_ANIMATIONS=NAME` ("Animations"), `LHWM_DEBUG=1` (icon source per window, the resolved caption font per theme load),
`LHWM_PROF=1` (one stderr line per drag with step and paint timings; each step is `XSync`ed while profiling), `LHWM_ANIM_NOGRAB=1`
(the classic animation without a server grab, so a screen recorder can watch; testing only).

`luxwmc` is berry's `berryc`: the IPC commands of `luxwm/luxwmc.1` (`window_move`, `window_resize`, `switch_workspace`,
`send_to_workspace`, `fullscreen`, `snap_left`, `snap_right`, `move_mask`, `resize_button`, ...), plus `reload-theme` ("Live-editing a
theme") and `quit` (clean exit, "EWMH and the shell").

**Keys** come from `~/.config/lhde/keys` (lines `MODS+Key action [arg]`, MODS of `Mod4`/`Super`, `Mod1`/`Alt`, `Control`/`Ctrl`,
`Shift`; `#` comments). Actions: `exec CMD`, `close`, `kill`, `cycle`, `maximize`, `minimize`, `sysmenu`, `workspace N`, `showdesktop`,
`shell CMD`, `start`, `none`. Without the file the defaults: `Mod4+Return` urxvt, `Mod4+t` xterm, `Mod4+e` flxp (rox when flxp is not on
PATH), `Mod4+r shell run`, `Mod4+d showdesktop`, `Mod4+s shell sidebar-stacking toggle`, `Mod4+q` and `Mod1+F4` close, `Mod1+space
sysmenu`, `Mod1+Tab`/`Mod4+Tab` and their `Shift+` forms `cycle`, `Mod4+Up` maximize, `Mod4+Down` minimize, `Mod4+1..4` `workspace
0..3`, `Print shell screenshot`, `Mod1+Print shell screenshot_window`, `Shift+Print shell screenshot_region`, `Super_L`/`Super_R
start`. A user file keeps its own bindings; `Mod4+d`, `Mod4+r`, `Mod4+s`, `Mod1+space`, the two `Shift+Tab` bindings and the three
`Print` bindings are added where that key is unbound there, and `Super_L`/`Super_R start` when the file binds neither Super key (`Super_L
none` disables it). `shell CMD` sets the root property `_LHSHELL_COMMAND` = CMD (UTF8_STRING); luxshell's runtime.lua accepts `run`
(lux's Run box), `sidebar-stacking toggle|top|bottom`, `taskbar KIND`, `sidebar-with-classic 0|1`, `settings`, `desktop N|next|prev`
(N 0-based, through `_NET_CURRENT_DESKTOP`), `reload`, and runs any other word as the session command of that name (settings.ini
`[commands]`, docs/SETTINGS.md): `screenshot`, `screenshot_window`, `screenshot_region` start luxshot. Every `exec` child does
`chdir($HOME)` first.

**Mouse**: Super+drag (button 1) moves a window from anywhere on it (berry's `move_mask` / `move_button`). berry's Alt+drag resize is
off (it took Alt+clicks from every client; the window menu's Size sizes with the mouse); `luxwmc resize_button N` turns a modifier
resize drag on again (with `resize_mask`, default Alt).

**Tests.** `build/luxwm/tests/frametest` renders the painter offline: `frametest THEMEDIR out.png` the four-state strip, `frametest
THEMEDIR out.png W H TITLE FOCUSED [MAXIMIZED [HOVER [ICON.png]]]` one frame with the window shape applied; it prints margins,
hit-test probes and the shape. THEMEDIR may be a theme spec (`DIR|STYLE.msstyles|COLOR|SIZE`, `|STYLE.msstyles|COLOR|SIZE`,
`|classic|...`). `FRAMETEST_EXT="l r t b [flags]"` extends the frame (`-1 -1 -1 -1` = sheet of glass), `FRAMETEST_FADE=t` renders the
maximise crossfade at weight t, `FRAMETEST_FONTS="DIR[;DIR]"` registers font directories as a theme's `path.fonts` does,
`FRAMETEST_SOURCE=dir|lib` picks the PNG cache or the library. `build/luxwm/tests/menutest THEMESPEC out.png [SEL [MAXIMIZED
[KEYBOARD]]]` renders the window menu (`MENUTEST_FONTS` likewise). `luxwm/tests/regress.sh` (`make check-luxwm`, part of `make check`)
renders, for every frame set in `share/frames`, the strip, `normal`, `unfocused`, `max`, `ext60` (`0 0 60 0`) and the Explorer cases
`ext46`/`ext46u`/`ext46max` (`0 0 46 0` with `NO_TITLE`) at 640x480 with frametest's text output; `vision49`/`vision49max` for
`header_mode = crop` sets; `NAME.classic/` for the glass sets whose style has a non-composited look of its own (aero5048, aero5112);
and `classic/`, `classic.xl/` (Extra Large metrics 24/24, Tahoma -19 bold), `classic.hc/` (High Contrast Black colours, borderwidth
6, 31/31, Tahoma -24 bold) for Windows Classic. It compares their SHA-256 with a baseline made on this machine
(`build/luxwm/regress/baseline.sha256`, written by the first run, `LUXWM_BASELINE=` another file; `--rebaseline` after an intended
change; `--source dir|lib`). No reference images are in the repository; it skips without the user's imported styles.
Lab helpers in `luxwm/tests`: `desktopwin` (an ARGB `_NET_WM_WINDOW_TYPE_DESKTOP` window that focuses itself on a click and pops a
grabbing override-redirect menu on a right click), `strutdock` (a dock with a bottom `_NET_WM_STRUT_PARTIAL`), `dockorder`
(`raise|lower|above|below` between docks).

## Themes and frame sets

**The session theme** is the root's `_LH_THEME` (UTF8_STRING), a theme name liblhde resolves (docs/THEME-FORMAT.md: the user's and
the shipped theme files, aliases, bare styles, the current layer). luxwm resolves it itself (`lh_theme_apply`) and maps the record:
- `VisualStyle.style = classic`, or neither a frame set nor a visual style: Windows Classic frames ("Windows Classic frames"), always,
  composited or not;
- `[Glass] frames = SET`: the glass set with the theme's style, spec `share/frames/SET|STYLE|COLOR|SIZE`; its classic frame (no
  compositor, or `[Glass] composition = 0`) is `frames_classic`, or with `frames_classic = none` the style's own ("Classic fallback");
- `frames = none`: the classic set `frames_classic` with the style, or the style alone (`|STYLE|COLOR|SIZE`), always.

It also takes the fonts (`path.fonts`), `Fonts.smoothing` (the window menu) and `Glass.animations` ("Animations"). At startup the root's
value wins over `-t`/`LHWM_THEME`, which luxwm publishes only when `_LH_THEME` is missing; an alias written to `_LH_THEME` is replaced
by the theme's name; a name that is no theme is logged (`luxwm: _LH_THEME NAME is no theme; keeping CURRENT`) and nothing changes.
luxwm writes `_LH_THEME_RESOLVED` (the serialized record) when the published one describes another record (luxshell writes the
same), and re-applies the theme when its record changes: on `_LH_SETTINGS_CHANGED` (root ClientMessage, format 32, data[0] the writer's
serial, sent with SubstructureNotifyMask; liblhde's settings are reloaded), a republished `_LH_THEME_RESOLVED`, a `NAME-modified.theme`
written. A change of `_LH_THEME` (`lhde-theme set`, `session/lab.sh theme NAME`, Display Properties, `xprop -root -f _LH_THEME 8u -set
_LH_THEME slate4074`) switches live; the name already in use set again is a reload ("Live-editing a theme").

**`_LHWM_THEME`** (root, STRING) publishes the theme spec in use (flxp only checks that it is non-empty; luxnav reads `nav.ini` from
its directory). Writing it directly (`xprop -root -f _LHWM_THEME 8s -set _LHWM_THEME slate4074-classic`, a name or a path; a bare
name is a sibling of the current set's directory) loads that set, or without a compositor its theme.ini `fallback_theme`, for
debugging, without changing `_LH_THEME`.

**A switch** keeps every client's logical rectangle (the frame minus the old theme's normal margins; the new margins go around it),
shifts the restore geometry the same way, refits maximised frames, reloads icons at the new `icon_size`, sends each client an Expose
(after an unredirect/redirect clients keep stale contents otherwise), and rewrites `_LHWM_COMPOSITED` and `_LHWM_FRAME_METRICS` even when
unchanged, so clients get one PropertyNotify of each per switch (`composited_publish()`, `theme_announce`). Log: `luxwm: theme SPEC
(msstyles|dwm, SOURCE) margins t=.. r=.. b=.. l=.. font=...`.

**Frame sets** are `share/frames/NAME/theme.ini` (and `nav.ini` for aerovision): the tuning keys and, as `msstyles`/`color`/`size`, the
style they belong to. Their PNG caches lie in the Windows tree (`generated/frames/NAME`, written by `lhde-import`: the DWM sets'
textures from the style's streams, the classic sets through `tools/msstyles2theme.py`), found through liblhde. Two modes, `mode = dwm`
(glass) and `mode = msstyles` (classic). Shipped: `aero-patch` (the patched 4074 Aero plates), `jade4074` (4074's Jade plates, blended
as build 4066 shows them; no shipped theme uses it), `aero5048`, `aero5112`, `aerovision` (glass); `slate4074-classic`,
`jade4074-classic`, `slate4051-classic`, `luna4051-blue|olive|silver` (classic).

**Theme specs** (`wm_theme_load`): a directory; `DIR|STYLE.msstyles|COLOR|SIZE` (the style given explicitly); `|STYLE.msstyles|COLOR|SIZE`
(no directory: msstyles mode, every tuning key at its default); `|classic|KEY=VALUE;...` (Windows Classic). Without a style in the
spec the directory's own theme.ini `msstyles`/`color`/`size` count (relative to the data directories).

**Compositor detection**: XFixes selection events on `_NET_WM_CM_S<screen>`. A DWM theme is replaced by its classic frame while no
compositor owns the selection and comes back when one appears (logged); `session/lab.sh start` launches luxwm before picom, so the
log shows the fallback and the switch back. Root `_LHWM_COMPOSITED` (CARDINAL 0/1) is 1 while a DWM frame is drawn composited.

## EWMH and the shell

What luxshell and flxp rely on, all in `wm.c`:
- `_NET_CLIENT_LIST` in creation order (a per-client sequence number; after a WM restart the old list's order is kept);
  `_NET_CLIENT_LIST_STACKING` bottom-to-top from the real X stacking order (`XQueryTree`) after every raise, restack, map, unmap and
  desktop switch, written only when it changed.
- `_NET_WM_STATE` written from the client's flags (`ewmh_update_state()`, only on change; atoms luxwm does not own, such as
  `SKIP_TASKBAR`, `ABOVE`, `STICKY`, are kept): `HIDDEN` while minimised (also `WM_STATE` Iconic), `MAXIMIZED_VERT` + `MAXIMIZED_HORZ`
  while maximised (`MAXIMIZED_VERT` alone while snapped to the full height), `DEMANDS_ATTENTION` (client message or the `WM_HINTS`
  urgency hint; cleared when the window gets the focus), `FOCUSED` on the active window, `FULLSCREEN`, `BELOW`. States requested before
  the map (maximised, fullscreen, below, attention) apply at map time.
- `_NET_WM_STATE` client messages (remove/add/toggle) for `HIDDEN` (minimise/restore), `MAXIMIZED_VERT`/`HORZ` (together:
  maximise/restore), `DEMANDS_ATTENTION`, `BELOW`, `FULLSCREEN`; `WM_CHANGE_STATE` Iconic (`XIconifyWindow`) minimises.
- Minimised is its own flag (`c->minimized`), apart from berry's "hidden" (parked right of the screen, how other desktops are hidden):
  a minimised window is unmapped where it is, client and frame (ICCCM IconicState; "Animations"); desktop switches, config reloads and
  send-to-desktop leave it alone; focusing it (Alt+Tab, `_NET_ACTIVE_WINDOW`) restores it. Hide/show keep the maximised state; a window
  maximised while minimised is fitted to the work area when shown.
- `_NET_ACTIVE_WINDOW` messages switch to the window's desktop, restore, raise and focus it. When the focused window is minimised or
  closed, the focus goes to the most recently focused other window that is not minimised, else `_NET_ACTIVE_WINDOW` becomes None.
  Desktop switches do not discard the event queue (berry's `XSync(dpy, True)` dropped a pager's `_NET_ACTIVE_WINDOW` sent right after
  `_NET_CURRENT_DESKTOP`).
- Docks: unmanaged windows (`MANAGE_DOCK` false) are tracked from `MapNotify` on the root and from `manage_new_window` (which covers
  windows adopted at startup), with `PropertyChangeMask`; `UnmapNotify`/`DestroyNotify` drop them. Their `_NET_WM_STRUT_PARTIAL`
  (fallback `_NET_WM_STRUT`, the whole edge) make the work area: `strut_area()` cuts every strut that overlaps a rectangle out of it
  (then the configured gaps). Per edge the largest strut counts, never the sum: struts are distances from the screen edge, so a block
  stacked above the taskbar includes the taskbar in its own strut (lux's pinned sidebar above the classic taskbar: block height + the
  bar's height over the sidebar's columns). A partial strut cuts the whole rectangle (EWMH: the work area excludes every strut).
  Example, 1280x800: taskbar 30 alone gives 1280x770; plus a block strut of 200 over x 1131..1279, 1280x600; a block strut of 170
  overlapping the taskbar instead, 1280x630. `_NET_WORKAREA` is that rectangle over the root, once per desktop (all equal).
- Dock stacking: dock windows are raised above the clients after every restack unless they carry `_NET_WM_STATE_BELOW` (lux's sidebar
  under the windows). `docks_raise()` raises them as a group in the server's order (`XQueryTree`), so a dock orders itself against
  another with `XRaiseWindow`/`XLowerWindow` or a `ConfigureRequest` with `stack_mode` (applied, then the group raise). EWMH's
  exception: the focused window, when fullscreen, goes above the docks at the end of every group raise (and when it gets the focus or
  enters fullscreen); once it loses the focus or leaves fullscreen the next group raise puts the docks back on top. lux's `[sidebar]
  stacking` switches the sidebar between `_NET_WM_STATE_ABOVE` and `_NET_WM_STATE_BELOW` (lowered under the clients, no strut) by
  rewriting the mapped dock's property; `dock_property()` restacks at once, while `_NET_WORKAREA` and the refit of maximised windows
  wait until the event queue is drained (`workarea_dirty`), so a burst of dock changes (Win+S: state, strut, geometry and stacking of
  the sidebar plus the taskbar's width) refits the windows once, against the final struts.
- Reparenting: a managed window that another client reparents away from the root (XEmbed tray icons docked by luxshell) is released
  (lists, decoration, grabs, focus; the window itself untouched); `MapRequest`s for windows whose parent is not the root are ignored.
- The work area of the window's monitor is used by maximise (the frame fills it), snap, centring and smart placement; maximised
  windows are refitted when the work area changes.
- New windows: dialogs (`_NET_WM_WINDOW_TYPE_DIALOG`, and transients, which EWMH treats as dialogs) are centred on their parent, or on
  the work area without one; `USPosition`, or `PPosition` other than 0,0, is honoured with the window gravity (NorthWest: the frame's
  top-left on the point, SouthEast: its bottom-right, Static: the client stays put); everything else gets smart placement. The frame is
  then clamped into the work area (shrunk if larger), whatever `EDGE_LOCK` says; `EDGE_LOCK`, when on, also locks moves and resizes to
  it. `ConfigureNotify` of root children does not re-query the monitors (only the root's own); a `ConfigureRequest` from a window on a
  hidden desktop keeps it off screen (size only).
- Adoption (WM restart): windows keep their place (the frame put back around the client), Iconic ones are minimised again (unmapped),
  maximised ones get their restore geometry from `_LHWM_RESTORE_GEOMETRY` (CARDINAL x 4 on the client, written on maximise), and the
  previous `_NET_ACTIVE_WINDOW` and `_NET_CLIENT_LIST_STACKING` are put back, so the active window stays active and on top.
- `_NET_MOVERESIZE_WINDOW` (berry's `NetWMMoveResize` slot) and `_NET_WM_MOVERESIZE` ("Extended frames") work; `_NET_WM_SYNC_REQUEST`
  ("Resize without flicker"); `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_WM_DESKTOP` ("Virtual desktops").
- **Start menu key** (`SHELL-DESIGN.md` 6.3): the action `start` grabs the bare Super key; released with no other key or button press in
  between, luxwm sends `_LHSHELL_START` to the root (SubstructureNotify|SubstructureRedirect, data[0] = the key time) and luxshell
  toggles its start menu. While Super is held the keyboard is luxwm's, so the Mod4 bindings still arrive; unbound Super+key
  combinations are swallowed.
- **Clean exit** (`luxwmc quit`; luxshell's "log off" outside the tty8 session): the frames are destroyed, clients stay mapped where they
  are, windows parked off screen on other desktops are moved back, minimised windows stay unmapped with WM_STATE Iconic (a restarted
  luxwm adopts them minimised). Deleted from the root: `_NET_SUPPORTED`, `_NET_SHOWING_DESKTOP`, `_NET_SUPPORTING_WM_CHECK`,
  `_NET_WORKAREA`, `_LHWM_THEME`, `_LHWM_COMPOSITED`, `_LHWM_FRAME_METRICS`, `_LHWM_MAXIMIZED_MODE` and berry's status properties; kept:
  `_LH_THEME` and `_NET_CLIENT_LIST`, `_NET_CLIENT_LIST_STACKING`, `_NET_ACTIVE_WINDOW` (a restarted luxwm restores order and focus from
  them). Exit status 0, `luxwm: clean exit` on stderr.

### Focus and the desktop

luxwm follows the real X input focus, not only the focus it sets. An unmanaged window can take it: flxp-desktop
(`_NET_WM_WINDOW_TYPE_DESKTOP`) calls `XSetInputFocus` on itself on every click (its context menu then grabs), and a dock may focus
itself for its menus. luxwm selects `FocusChangeMask` on the clients and the root; on every `FocusIn`/`FocusOut` (except
`NotifyGrab`/`NotifyUngrab` and detail `NotifyPointer`) `focus_sync()` asks `XGetInputFocus` and walks up to the top-level. On the
focused client (or a window inside it) the frame is active; on a window luxwm does not manage, the root, `PointerRoot` or None,
`focus_set_active(false)`: the frame is repainted inactive, `_NET_ACTIVE_WINDOW` becomes None and `_NET_WM_STATE_FOCUSED` goes, while
`f_client` stays the last focused window (`f_active` false; `client_active(c)` is what painting and `FOCUSED` use). Focus inside an
override-redirect top-level (menus, tooltips) or on another managed client changes nothing. A click on a client or its frame
(`FOCUS_ON_CLICK`) focuses it whenever it does not really hold the focus. A click on the bare root (no desktop window) gives the focus
to luxwm's `nofocus` window and makes the window inactive the same way. While inactive, Alt+Tab first reactivates the last focused
window, the window key actions (close, kill, maximise, minimise) do nothing (Alt+F4 on the desktop closes nothing), and closing or
minimising that window from elsewhere moves "last focused" on without taking the focus from the desktop.

### Show desktop

Windows' Win+D, as EWMH `_NET_SHOWING_DESKTOP` (root CARDINAL, in `_NET_SUPPORTED`, 0 at start, deleted on quit; `showing_desktop()`).
On (the key, or a `_NET_SHOWING_DESKTOP` client message with data[0] = 1): the visible windows of the current desktop are really
minimised (iconic, task buttons stay; bottom first, so the focus is not handed down the stack) and luxwm remembers their ids, their
order and the active one. Off (the key again, or data[0] = 0) restores exactly that set in its old stacking order and refocuses the old
active window, but only while nothing has changed: a window restored (taskbar click, `_NET_WM_STATE`, Alt+Tab), activated
(`_NET_ACTIVE_WINDOW`), newly mapped or closed/withdrawn, or a desktop switch, ends the mode on the spot without restoring anything
(`showing_changed()`, the property goes to 0), so the next Win+D minimises whatever is visible then. `_NET_SHOWING_DESKTOP` is 1
exactly while the remembered set is minimised and unchanged. Both directions are instant: the set is unmapped / mapped in one batch
under one `XGrabServer`, the XP caption flight is skipped, and with picom `_LHWM_ANIMATE` is deleted on the set's clients and frames
for the transition and put back 400 ms later (`anim_until`, `anim_expire`), so no picom hide/show rule matches. A single window's
minimise keeps its animation.

### Alt+Tab

As Windows' (`cycle`, `cycle_run()`): the press grabs the keyboard until the binding's modifier (Alt, Super) is released. Candidates:
the current desktop's windows in stacking order (top first), then its minimised ones. The first Tab selects the window under the
active one (Shift+Tab the last candidate; with nothing active the top window); each further Tab / Shift+Tab steps forward / back,
wrapping. The selected window is previewed: client and frame raised over the others without focus (frames keep their look), and the
previous preview goes back to its place (the stacking from before the first Tab is restacked with the candidate on top). A minimised
candidate is not shown. Releasing the modifier restores the original stacking, then activates the candidate (restored if minimised,
raised), so every other window keeps its place: `C B A` (C active), Alt+Tab+Tab gives `A C B`, not `A B C`. Esc while the modifier is
held puts the original stacking back and leaves the focus where it was. A modifier already released when the grab starts (xdotool
`key alt+Tab`) makes it one step. Other events (Expose, maps, property changes) are handled meanwhile; a candidate that goes away is
skipped. A Super release that ends a cycle sends no start menu.

### Restack requests

`_NET_RESTACK_WINDOW` (in `_NET_SUPPORTED`; CLIENT-PROTOCOL.md "Raising a window during a drag", the drag-and-drop raise): a client message on
a managed client window, data.l[0] source indication, l[1] sibling, l[2] detail. `client_restack()` moves the window in luxwm's stack
list and restacks client and frame together, without touching the focus or `_NET_ACTIVE_WINDOW`: Above with no sibling raises to the
top, Below with no sibling lowers to the bottom of the managed windows, with a managed sibling on the same desktop directly above /
below it; TopIf/BottomIf count as Above/Below (no occlusion test), Opposite as Above unless the window is on top already. Minimised
windows, windows of other desktops, BELOW windows and unknown siblings are ignored (logged with `-d`). Client messages are processed
whatever pointer grab another client holds, so a drag source (flxp's XDND drag keeps its active pointer grab) can raise the window
under the pointer after its ~500 ms hover.

## The window menu

XP's system menu, drawn by luxwm (`menu.c`, `menu.h`). **Opened by** Alt+Space (`sysmenu`), a right click on the caption (on the
release, like `WM_CONTEXTMENU`, at the pointer; it flips left/up at the monitor edge), a click on the caption icon (`wm_frame_icon_rect()`;
a double click there, the second press within 500 ms, closes the window as XP's), and a `_GTK_SHOW_WINDOW_MENU` client message
(CLIENT-PROTOCOL.md: luxshell's task buttons, GTK's client-side headers) at the point it names, also for a minimised window. Alt+Space
and the icon put it under the caption at the frame's left border. Items, mnemonics and greying as `mnsys.c` `xxxSetSysMenu`:
**R**estore (greyed unless maximised or minimised; the default item while minimised), **M**ove and **S**ize (greyed while maximised or
minimised; Size also for a fixed-size window), Mi**n**imize (greyed while minimised), Ma**x**imize (greyed while maximised, or for a
fixed-size window), Move to **D**esktop (luxwm's own, "Virtual desktops"), separator, **C**lose (the default item, bold, unless
minimised) with `Alt+F4` right-aligned.

Opened from the keyboard the first item is selected (greyed or not, as `MNFindNextValidItem`, which skips only separators) and the
mnemonics are underlined (XP's keyboard cues; a key press shows them in a mouse-opened menu too; always with settings.ini `[effects]
keyboard_cues = 1`). Keys: Up/Down/Tab (wrapping, separators skipped), Home/End, Enter (nothing on a greyed item), a mnemonic (the
only item with it is chosen at once, a greyed one only selected; several: the next one), Esc, Alt or F10 close. Mouse: hover selects
(greyed items too, as XP), a release over an enabled item chooses it (also the release of the button that opened it, dragged onto an
item), a click outside closes it and is not passed on. Modal like the drags: pointer and keyboard grabbed, Expose/MapRequest/
ConfigureRequest of other windows handled meanwhile. **Submenus** cascade as XP's: a 4 x 7 arrow in the right margin; hovering the item
for MenuShowDelay (400 ms), a click on it, Right, Enter or its mnemonic opens it beside the item (3 px overlap, left of the parent when
it does not fit); Left or Esc closes only the submenu, the pointer back over another parent item (or a press there) closes it and the
parent takes over, a click outside both, Alt or F10 close everything. The submenu shares the parent's keyboard grab and hands the
pointer grab back when it closes.

**Rendering** (`wm_menu_popup`: an override-redirect ARGB window, `_NET_WM_WINDOW_TYPE_POPUP_MENU`, `_LHWM_MENU`, WM_CLASS `lhwm-menu`):
the pixels are wuxtheme's popup menu painter (`ut_menu_layout` / `ut_menu_draw` in `<wuxtheme/ut.h>`, over `wuxtheme/menu.cpp`), the
code that draws every user32 menu of the wux side (flxp's context menus, menu bars, the start menu cascades), so the window menu and an
Explorer context menu are the same pixels. luxwm keeps the geometry on screen, the shadow and the input. The painter gets the frame
theme's visual style (`flat = -1`, the style's MENUFONT) or, for Windows Classic, the scheme's colours and `Metrics.menufont`:
- styles with a `Menu` class (`POPUPITEM` present: the 4074 Slate, Jade and Aero-patch styles, also under the DWM themes; Vista's
  aero5048/aero5112): `POPUPBORDERS` over the whole menu (else `POPUPBACKGROUND` and a 1 px BTNSHADOW line), `POPUPGUTTER` when the style
  has one, `POPUPITEM` state 2 (hot) / 4 (disabled hot) under the selected item, `SEPARATOR` / `POPUPSEPARATOR` at its true height plus
  its content margins, the text colours of `POPUPITEM` states 1-4, item height = text height + `POPUPITEM` content margins, `CHECK` /
  `POPUPCHECK` for check marks (states 1, 2) and radio bullets (3, 4) centred in the check column (the 4074 bullet of
  `lh4074-195235.png` is Slate's state 3), `FLYOUTARROW` / `POPUPSUBMENU` with its right edge 6 px before the item's end, Vista's
  `SYSTEMRESTORE`/`MINIMIZE`/`MAXIMIZE`/`CLOSE` glyphs in the gutter (the 4074 styles have none, and their menus show none);
- styles without one (XP's Luna, Royale, Zune, Noir, Embedded): XP's own drawing (`mndraw.c`) with the style's `[SysMetrics]` colours
  over Windows Standard: FLATMENUS = 1 px BTNSHADOW outline, MENU inside, MENUHILIGHT bar framed in HIGHLIGHT with HIGHLIGHTTEXT,
  greyed text GRAYTEXT; without FLATMENUS the raised 3D edge, a HIGHLIGHT bar and embossed greyed text on BTNFACE; the separator a
  BDR_SUNKENOUTER etch at the middle of a 9 px item; the system menu bitmaps (Marlett glyphs for Restore, Minimize, Maximize, Close) in
  the 13 px check column in the text colour; check marks and bullets from Marlett (`a`, `h`); the submenu arrow XP's 4x7 (its right
  column 6 px before the end, 8 px from the outline, as `WinXP-193221.png`);
- the font is `[SysMetrics] MENUFONT` (Slate/Luna Tahoma 8 = 11 px, Jade/Aero Segoe UI 9 = 12 px) in wux's GDI text (FreeType with the
  v35 interpreter, hinted integer advances as GDI's), item height and baseline from its tmHeight / tmAscent. Smoothing is the
  system's, as flxp's menus have it: settings.ini `[fonts] smoothing`, else the theme's `Fonts.smoothing`; only when neither says, the
  captions' (ClearType for DWM themes and sets with `text_antialias = subpixel`, none for Luna's);
- without a visual style (a DWM theme whose style cannot be opened): Windows Standard colours and Tahoma 8.

Geometry (XP's `mncomput.c`, identical to the 4074 Slate menu of `lh4074-195235.png`): 1 px border + 2 px inside, text 19 px into the
item (22 px from the border: MNLEFTMARGIN + the 13 px check column + MNXSPACE), separator 8 px (Slate's part) / 9 px (XP), accelerators
right-aligned 17 px before the inner edge in one column after the widest text plus two average character widths. The drop shadow is
XP's 5 px right/bottom shadow (`ut_menu_shadow_alpha`: alpha 113, 84, 43, 14, 3 outwards, starting 5 px in, with XP's rounded corners;
the 4074 menus show the same levels): real alpha with a compositor (`picom-glass.conf` gives `_LHWM_MENU@` no picom shadow or blur),
otherwise blended over a copy of the screen taken before the map, as XP does; none with settings.ini `[effects] menu_shadows = 0`.
Against the 4074 Slate menu, border, plate, highlight, separator, bullet and arrow colours and positions, item pitch (19), text start
(x 22) and text rows are identical; the reference's ClearType fringes are BGR-ordered, the painter's RGB.

**API** (`menu.h`, meant for lux-style context menus too): `struct wm_menu_item { text ("&Move"), accel, flags (WMMENU_DISABLED,
SEPARATOR, DEFAULT, CHECKED), glyph, id, cb(arg, id), arg, sub, nsub }`; `wm_menu_popup(env, x, y, items, n, flags, button)` runs the
menu (flags `WMMENU_KEYBOARD`, `WMMENU_FLIP`; `button`: the button still held) and returns the chosen index after calling its `cb`, or
`WMMENU_CANCEL` / `WMMENU_DBLCLICK`; `struct wm_menu_env` carries the display, ARGB visual, theme, compositor flag, monitor rectangle, a
dispatcher for other windows' events and the double-click rectangle. wm.c's `wm_menu_open(client, x, y, flags, button, from_icon)`
builds the system menu; `sysmenu_command()`, its callback, looks the window up again (one that went away while the menu was open is
left alone).

**Keyboard Move and Size** (`drag_run(..., keyboard)`, also `_NET_WM_MOVERESIZE` directions 10 and 9), as `movesize.c`: the pointer goes
to the middle of the caption (Move) or of the window (Size), cursor fleur. Move: arrows move by half a caption button (`max(SM_CXSIZE /
2, 1)`: msstyles CAPTIONBARHEIGHT / 2, DWM caption height / 2 = 13 px), Ctrl+arrow 1 px. Size starts with no edge (WMSZ_KEYSIZE): the
first Left/Right picks the left/right edge, the first Up/Down the top/bottom edge (two of them a corner) and puts the pointer on it
without changing anything; further arrows move it by the same step (at least one size-hint increment). Two arrows held together step
diagonally, in Move as in Size (luxwm's own; X repeats only the last key held). **The mouse in Size mode** (luxwm's own): the pointer
picks the edges it moves towards once it is 20 px (5 x `DRAG_THRESHOLD`) from where the mode started, both edges of the corner when the
move is within about 23 degrees of the diagonal (the shorter axis at least 0.4 of the longer), one edge otherwise; a pointer that
reaches a border or corner of the frame picks that one. The pointer is then put on the picked edge (corner) and the edge follows it.
**Shift** toggles between the edge and the corner next to it (the end nearer the pointer) and back, putting the pointer on the new edge
or corner. A pointer that slides along the one edge it sizes to its end (the frame's corner hit area, or past it) takes that corner;
after Shift went back to the edge the pointer has to leave the corner first. A corner never goes back to an edge by itself. In both
modes mouse motion takes over; Enter accepts and puts the pointer back where it was before the mode (ptRestore), a click accepts and
leaves it, Esc (or a `_NET_WM_MOVERESIZE` cancel) puts the window back to its original rectangle (and the pointer back). Size hints,
sync requests and the flicker-free resize path are the mouse drag's. Keyboard Move and Size do not snap.

## Snap

Windows 7's Aero Snap, at aap's request (neither XP nor the Longhorn builds had it). A **move** drag (the caption, the move modifier,
`_NET_WM_MOVERESIZE` 8) whose pointer reaches the left or right edge of the work area (the window's monitor minus struts) snaps the
window to that half; reaching the top edge maximises it. A **resize** drag of the top or bottom border whose pointer reaches that edge
stretches the window to the work area's full height, keeping its columns (7's vertical maximise; corners do not). The pointer has to
touch the edge, not the frame, so pushing the frame against or past an edge does not snap; nothing happens until the release: while
the pointer is at an edge an outline of the target is shown (4 px of XP's checkered drag-rectangle pattern, an override-redirect window
`lhwm-snap`: a choice, 7 drew a glass sheet), and moving away before the release drops it. Escape cancels drag and snap.

A snapped window (`c->snap`: `SNAP_LEFT`, `SNAP_RIGHT`, `SNAP_VERT`; `_NET_WM_STATE_MAXIMIZED_VERT` while it spans the full height)
remembers its rectangle from before (`c->snap_prev`), a maximised one its restore rectangle. **Dragging either away** by the caption
gives it that size back at once, under the pointer at the same fraction of its width (as 7), and the drag goes on; dropping it at the
top again maximises with the pre-snap rectangle as restore geometry. A resize that changes a snapped window's height ends the snap; a
width-only resize keeps it. `luxwmc snap_left`, `snap_right` go through the same code (snap right keeps the right edge on the work
area's after the size-hint rounding).

## Virtual desktops

Ten desktops (`_NET_NUMBER_OF_DESKTOPS`), switched by `workspace N` (defaults Mod4+1..4 = desktops 0..3), by `_NET_CURRENT_DESKTOP`
requests (luxshell, any pager) and `_LHSHELL_COMMAND desktop`. A window changes desktop by:
- **a desktop key while it is moved**: during a move (the window menu's Move, a caption or modifier drag, `_NET_WM_MOVERESIZE` 8/10) a key
  bound to `workspace N` takes the window to desktop N and switches there; the move goes on;
- **the window menu's Move to Desktop**: a submenu Desktop 1..10 (desktops 0..9; mnemonics 1..9 and 0), the window's own one checked;
  choosing another moves the window there and switches to it;
- **`_NET_WM_DESKTOP` client messages** (data.l[0] = the desktop): the window moves there and the current desktop stays, as a pager
  expects; 0xFFFFFFFF (all desktops) is not supported and ignored.

None of this is Windows': XP had no virtual desktops, Longhorn neither.

## Live-editing a theme

`luxwmc reload-theme` (the root ClientMessage `_LHWM_RELOAD`), any write of a root property `_LHWM_RELOAD` (`xprop -root -f
_LHWM_RELOAD 32c -set _LHWM_RELOAD 1`), or `_LH_THEME` set to the name already in use makes luxwm re-read the current theme in place
(`theme_reload()`): the theme is resolved again, then `theme_select()` loads the same spec with `theme_force` set, i.e. theme.ini and
its PNGs, or the `.msstyles` through the library, anew. Every client is rethemed as on a switch (the client rectangle kept, the frame
placed around it with the new margins; `_NET_FRAME_EXTENTS`, restore geometry and maximised frames follow), frames and header pixmaps
are repainted (a new theme generation), `_LHWM_FRAME_METRICS` and `_LHWM_COMPOSITED` are announced again. Log: `luxwm: theme reloaded
(_LHWM_RELOAD message|_LHWM_RELOAD property|_LH_THEME set to the theme in use): SPEC`; a theme that no longer loads keeps the running
one. luxwm's own alias rewrite of `_LH_THEME` is counted (`lh_theme_selfwrite`) so it does not come back as a reload. lux has the same
(`_LHSHELL_COMMAND reload`); `session/theme-reload.sh` sends both and `session/theme-watch.sh` runs it on every save (docs/USAGE.md
"Live-editing a theme").

## Window shape

`wm_frame_paint_ex4()` paints into an image surface, copies it to the decoration window and derives an X Shape bounding region from its
alpha: in the top and bottom 24 rows, leading/trailing pixels with alpha below `shape_alpha` (theme.ini; default 32 for DWM sets, 1 for
msstyles) are cut out. The DWM plate is rounded (its corner pixels have alpha 0..0x30); a full rectangle would let picom blur and shadow
the square corner as a notch around the rounded highlight (`compare_dwm_corners.png`). Without a compositor the shape is what makes the
rounded caption corners of the classic frames transparent at all. The shape is sent only when it changes (0.00..0.04 ms per paint).

### Pill and mesh slicing

The `ChamferBox01` mesh in `desksrv.dll` (D3DXSTREAM 101) has no `MeshTextureCoords` (168 vertices, x/y in +-15, front face flat at
+-14.505, a 0.495-unit chamfer rim), so it gives no UV information; the 9-slice is measured from the plate texture. In the active plate
the button pill is a low-alpha well at x=701..793, y=2..14, ending at the dark line x=792 (the highlight line is x=799). The 1:1 right
100 px of `plate_insets` keep it unstretched: frametest at 500, 1000 and 1280 px wide and 607 high (every row 1:1), plate alone, active
and inactive, both sources: the right 100 columns equal the texture's columns 701..800 with 0 differing pixels. The original
(`4074-dwm.png`, Documents window) ends the button sprites 5 px before the frame edge (sprite col 41, the bright end line, 7 px from
it; the texture's dark line 9 px): aero-patch `button_right = 5`; jade4074 `button_right = 6` (fitted to 4066, "Glass accuracy"). In
the original the min/max/close dividers are 27/27/42 px apart (-104/-77/-50/-8 from the frame's right edge on the bright line at w-2)
while the 4074 sprites are 25/25/40; `button_gap` (default 0) widens each cell: sprite cols 0..3 stay at the cell's left, col 4 is
repeated over the gap, the rest sits at the right end; `button_rect` (drawing and `wm_frame_hit`) uses the cell. The shipped sets use
`button_gap = 0`. The Explorer plate (STREAM 484) has its pill at the same place (x=701..793 at rows 2..5, narrowing to 787..792 at row
11), so the same inset and button layout apply.

## Classic msstyles mode (`mode = msstyles`)

The non-composited Longhorn/XP look, drawn as uxtheme's nctheme.cpp does it (docs/history/CLASSIC-DECO-NOTES.md), from the visual
style read live through the library ("libuxtheme source"); the frame set's PNG cache is the fallback and its theme.ini holds the tuning
keys. `tools/msstyles2theme.py` makes a set from a `.msstyles` file, XP format (PACKTHEM_VERSION 3: `THEMES_INI` + `<VARIANT>_INI` text
resources, bitmaps named after their files) or Longhorn-compiled (version 4: `VMAP` + `VARIANT` decompiled with the style's own `STBL01`
schema); `--color`/`--size` pick the variant (usage in its header). It resolves properties like uxtheme (state section, part section,
`[Window]`, `[globals]`; of a property defined twice the first, as uxtheme's `CRenderObj::GetValueIndex` and Wine's
`MSSTYLES_AddProperty`: luna.msstyles's `[Window.MaxCaption] SizingType` is `stretch`, then `Tile`) and splits every IMAGEFILE strip
into `<part>_<state>.png` (`<part>@<n>_<state>.png` for IMAGEFILE1..5, the glyphs of IMAGEGLYPH buttons). Bitmaps: a 32-bit DIB with
any alpha outside {0,255} is premultiplied (un-premultiplied for PNG); one with both 0 and 255 uses its alpha as a mask (the 4074 glyph
strips are black where alpha is 0 and the caption corners alpha 0; XP's rule of ignoring such alpha would draw black boxes); otherwise
TRANSPARENT parts key out TRANSPARENTCOLOR (magenta). The 4074 and 4051 Slate sets converted from the PE files equal those built from
the decompiled styles.

theme.ini keys (written by the converter; with the library only the tuning keys are used):
- `mode = msstyles`; `msstyles`, `color`, `size`; `border_width` ([sysmetrics] SIZINGBORDERWIDTH, system default 1);
  `caption_height` (CAPTIONBARHEIGHT); `small_caption_height`; `caption_font`, `small_caption_font` (pango description; Windows'
  lfHeight = -MulDiv(pt, 96, 72) px, so Tahoma 8 = 11px, Segoe UI 9 = 12px, with a fallback family); `caption_text`,
  `caption_text_inactive`; tuning: `text_antialias` (subpixel|gray|none), `text_hinting` (full|medium|slight|none),
  `text_dy` (baseline correction), `shape_alpha`, `font_files`, `default_icon`, `text_max_*`.
- per part (`caption maxcaption smallcaption frameleft frameright framebottom close max min restore help smallclose`): `.images`
  (IMAGECOUNT), `.variants` (`@n:WxH`), `.sizing_margins`, `.content_margins` (left right top bottom), `.caption_margins`, `.sizing`
  (stretch|tile), `.transparent`, `.content_alignment`, `.shadow.<1 active|2 inactive> = none|single|continuous dx dy #rrggbb`.

Geometry and drawing (frame.c `ms_paint`, `ms_btn_rect`, `wm_theme_margins_ex`):
- cnBorders = 3 + border_width on all sides (4 px for the Longhorn styles), caption = cnBorders + caption_height + 1 (24 px); CAPTION over
  the full width, FRAMELEFT/RIGHT (cnBorders wide, from the caption to the bottom frame) and FRAMEBOTTOM (full width) stretched into
  their rects; frame state 1 active / 2 inactive.
- Nine-grid as ninegrid2.cpp: 32.32 fixed-point nearest neighbour starting half a step in, corners 1:1 while a middle remains, otherwise
  the corners share the size in proportion to their margins (the 29 px Slate caption with margins 20/8 is shrunk into 24 rows that way).
  A part drawn taller than its bitmap whose SIZINGMARGINS cover every row (Luna Silver's 26-row MAXCAPTION, margins 9 + 17; only luxwm
  asks for that, in a maximised caption continued into a header) repeats source row `top` in between (`ng_map`, `wm_fut_ms_draw`).
- Buttons: (MulDiv(caption_height, close.w, close.h) - 4) x (caption_height - 4) = 15x15, close 2 px left of the right border, then
  max/restore and min 2 px apart, top = cnBorders + 2. Strip index = framestate * (images / 2) + {normal, hot, pushed, disabled}: the
  Longhorn 10-image strips have 5 per frame state (the 5th, a deep red/blue "default" look, is unused: nothing in nctheme selects it);
  4051's and XP's 8-image strips 4. The glyph is the largest IMAGEFILEn fitting the CONTENTMARGINS rect (9x9 for Slate), true size,
  centred (XP's imagefile.cpp `DrawBackground`: `GetBackgroundContentRect`, then `SelectCorrectImageFile(&rc)`).
- Icon (the client's, else the application icon, else the set's `default_icon`; "Icons, maximised caption text, fonts"): 16x16 at
  cnBorders + 2. Caption text from icon.right (cnBorders without an icon, or with `NO_ICON`) + CAPTIONMARGINS.left to the first button -
  CAPTIONMARGINS.right, end-ellipsised; top = cnBorders + (caption_height + 1 - tmHeight) / 2 with GDI's tmAscent/tmDescent (unhinted
  metrics rounded), baseline tmAscent below. TEXTSHADOWTYPE CONTINUOUS is comctl32's DrawShadowText: every touched text pixel counts
  as 255, a 4-wide box to the right, then the row and four rows above (bottom-up DIB) / 4, blended at (-4 + dx, -8 + dy) from a buffer
  with the text at (5,5). SINGLE is one offset copy.
- Maximised: MAXCAPTION, no side or bottom frames, the borders off-screen: margins 0/0/0 and caption_height + 1 (20 px for Slate),
  buttons and text at their offsets (2 px from the screen edge). The MAXCAPTION bitmap is nine-gridded into exactly the visible band, `w
  x (caption_height + 1)` at 0,0, as nctheme.cpp's `NcPaintCaption` insets the background rect by cnBorders on top, left and right when
  `fFullMaxed` (and `CreateCompositeRgn` the region); the bitmaps carry their outline at the outer edge (Slate rows/columns 0..1). The
  window shape is one rectangle; restore brings the rounded corners back.
- Window shape: the caption's alpha-0 corners, taken 1:1 from the caption bitmap at its true size (uxtheme keeps the margin areas of the
  region unscaled; the drawn pixels come from the shrunk image, so row 3 of the 4074 corner differs, as in the screenshots). A
  caption-band row painted transparent across the whole width is left out of the region, as `GetThemeBackgroundRegion` does (the
  ReactOS styles' CAPTION has three transparent top rows; Watercolor Lite's tiled CAPTION is shorter than the band): the window starts
  below them, as on XP (`2k3-205316.png`). Frames are opaque and the client area left transparent, so the mode looks the same with and
  without picom.
- `wm_frame_hit`: buttons exactly their rects, borders cnBorders wide, corners one caption height along each edge.
- `_LHWM_FRAME_FLAGS` `NO_TITLE` / `NO_ICON` apply here too; everything else about extended frames in this mode is the header pixmap
  ("Extended frames").

Against the references, frame borders, caption bitmaps, caption corners and all caption buttons are pixel-identical (4074 Slate and Jade
Task Manager: 0 of 4928 border pixels and 0 button pixels differ; 4051 Explorer borders, bottom corners and buttons: 0). What differs is
the caption text: FreeType + cairo subpixel AA against GDI ClearType, same font files and positions, stems off by a fraction of a pixel,
ClearType's fringes stronger, now and then an advance rounding by 1 px after a few words. Jade asks for Segoe UI 9: 4074 ships Segoe UI
0.7 (`seguibk.ttf`), which the theme's fonts load, and the Jade Task Manager caption fits it better than Tahoma (`docs/history/THEMES.md`
"Fonts"). The Display Properties "Active Window" sample is not a real window (its text 1 px further right, 1 px higher for Jade, a light
band under its caption); real windows match in position. Not implemented: SMALLCAPTION/tool windows, MINCAPTION, the help button, a
disabled min button (no X11 equivalent of WS_MINIMIZEBOX is consulted; max is disabled for fixed-size windows, "Size hints"),
DPI-scaled image selection (96 dpi assumed).

### XP visual styles (Luna)

`luna4051-blue`, `luna4051-olive`, `luna4051-silver` are XP's Luna in its three colour schemes (NormalColor, HomeStead, Metallic;
NormalSize) from the copy of `luna.msstyles` that 4051 ships (Longhorn-compiled; 4042's differs only in its version resource), made with
`--text-antialias none` and the 4074 Trebuchet files as `--font-file`. XP's own file (PACKTHEM_VERSION 3) gives the same theme.ini; its
button bitmaps differ in the antialiased corner pixels only (+-1 level: XP stores straight alpha and premultiplies on load,
`themeldr/utils.cpp` `PreMultiplyAlpha`; the 4051 copy is stored premultiplied). What differs from the Longhorn styles, all handled by
the same code: CAPTIONBARHEIGHT 25 and no SIZINGBORDERWIDTH (system default 1), so cnBorders 4 and a 30 px caption; 8-image button
strips; 21x21 buttons with 13x13 glyphs (IMAGEFILE3 of five sizes); CAPTION/MAXCAPTION 66x29 with SIZINGMARGINS 28 35 9 17;
FRAMELEFT/RIGHT bitmaps 5 px wide drawn into the 4 px border; `TEXTSHADOWTYPE = SINGLE` (1,1) on the active caption only. Two keys:
- `font_files` (files or directories): Luna's CAPTIONFONT is Trebuchet MS 10 bold (13 px); luxwm registers them as application fonts at
  theme load ("Fonts"); the converter writes it from `--font-file`, or finds the font in the style's own `WINDOWS/Fonts`.
- `text_antialias = none` with full hinting: XP's default "Standard" smoothing leaves these captions bi-level (FreeType's mono
  rasteriser then gives GDI's 2 px stems; grey or subpixel AA renders the bold face visibly thinner). The library default for a v3
  style is the same.

Against a real XP SP3 Explorer window (`luna_exp_smallTB_folder.png`, 800x596; `compare_luna.png`, `compare_luna_xp.png`): borders,
bottom corners and the caption bitmap including both rounded top corners: 0 pixels differ; buttons 48 pixels, all antialiased button
corners; caption text: same position and baseline, glyph shapes differ (FreeType mono hinting of Trebuchet against GDI). XP's later
styles (Royale, Royale Noir, Zune, Embedded) load the same way as bare styles, borders identical to `ut-dump --render`; no reference
screenshots of them are at hand.

## libuxtheme source

luxwm reads the `.msstyles` itself through libwuxtheme (`<wuxtheme/ut.h>`, `build/lib/libwuxtheme.so.1`; `docs/history/THEMES.md` "One
uxtheme for everything"); a frame set's PNG directory is a cache and the place of the tuning keys. A style with no frame set at all (a
bare style, a theme with `frames_classic = none`) gets classic frames from the style alone. Built without `HAVE_UXTHEME`, `frame_ut.c`
compiles to stubs and luxwm needs the PNG caches.

**Source choice.** With a style: the library (`ut_open`). If the style does not open, the directory's PNGs (logged), or failure for a
spec without a directory. `LHWM_THEME_SOURCE=dir` (luxwm) / `FRAMETEST_SOURCE=dir` (frametest) force the PNG caches, `=lib` the library.
Logged: `luxwm: libuxtheme built in: visual styles are read live` at startup, then per theme `(msstyles, /path/x.msstyles (NormalColor,
NormalSize, v3))` or `(msstyles, PNG cache)`.

**What comes from the library** (`frame_ut.c`):
- msstyles mode, drawn with `ut_draw_background` at paint time (Wine's DrawThemeBackground over wux's GDI: nine-grid with XP's
  ninegrid2.cpp rounding, margins, TRUESIZE, TRANSPARENT/TransparentBlt, uxtheme's alpha rules): CAPTION, MAXCAPTION, FRAMELEFT,
  FRAMERIGHT, FRAMEBOTTOM in their frame states, each rendered into a w x h image and painted OVER at its place, as the PNG path does.
- the caption buttons (CLOSE/MAX/MIN/RESTOREBUTTON, IMAGEGLYPH with CONTENTMARGINS): their IMAGEFILE and IMAGEFILE1..5 strips from
  `ut_get_bitmap` (uxtheme's alpha rules; TRANSPARENTCOLOR keyed out of an image without alpha when the part is TRANSPARENT) through
  frame.c's nine-grid and glyph rule. DrawThemeBackground is not used for them: Wine's draw.c selects and centres the glyph for the
  whole button rect, XP's imagefile.cpp for the content rect (in a 21x21 Luna button the 19x19 glyph instead of the 13x13).
- metadata: IMAGECOUNT, the true size (`ut_part_size` TS_TRUE: the button aspect), SIZINGMARGINS (shape rows), CONTENTMARGINS,
  CAPTIONMARGINS (CAPTION's for parts without their own), SIZINGTYPE, TEXTSHADOWTYPE/OFFSET/COLOR per caption state; the caption's
  active image at its true size for the shape's corner rows (`wm_frame_shape_rects`).
- [SysMetrics]: SIZINGBORDERWIDTH (default 1), CAPTIONBARHEIGHT, CAPTIONFONT (`ut_get_font`, as a pango description
  `FACE[,Tahoma][ Bold][ Italic] PXpx`), CAPTIONTEXT, INACTIVECAPTIONTEXT (`ut_get_color`).
- defaults unless theme.ini sets them: `text_hinting = none` for a `Quality:ClearType-Natural` CAPTIONFONT, `text_antialias = none` for
  an XP (v3) style.
- DWM mode: the CompositedWindow PNGSTREAMs (`ut_get_stream`, decoded with cairo's PNG reader) for every texture file name:
  `plate_active/inactive.png` = LGFRAME (483) top/bottom half, `plate_explorer_*` = LGFRAMEEXPLORER (484), `plate_max_*` = LGFRAMEMAX
  (493), `plate_max_explorer_*` = LGFRAMEMAXEXPLORER (494), `overlay.png` = LGFRAMEOVERLAY (486, grey RGB: white at alpha = grey),
  `close/max/min/restore.png` = LGCLOSEBTN/LGMAXBTN/LGMINBTN/LGRESTOREBTN (487, 489-491), `glow_close.png`/`glow_btn.png` =
  LGCLOSEBTNOVERLAY/LGBTNOVERLAY (488, 492), `captionfog.png` = CAPTIONFOG, `default_icon.png` = DEFAULTICON (506), `shadow.png` =
  LGFRAMESHADOW (485). Each is pixel-identical to the extracted file; alpha-less streams are made ARGB32 like the files. A name outside
  that map (`button_files`) comes from the directory.
- the theme's font directories also go to `ut_add_font_dir` (the library's own text drawing; luxwm draws captions itself).

**What stays in theme.ini / frame.c:** text (pango, the GDI metrics emulation, TST_SINGLE / TST_CONTINUOUS shadows), geometry
(cnBorders, button placement, hit testing), the window shape, maximised and extended-frame logic, the tuning keys (`text_antialias`,
`text_hinting`, `text_dy`, `font_files`, `default_icon`, `shape_alpha`, `text_max_*`, `glow_max`; for DWM sets every inset and tuning key:
`plate_insets`, `margins`, `explorer_*`, `max_*`, `extend_rows`, `glass_alpha(_mode)`, `fog`, `overlay`, `button_*`, `buttons`, `font`,
`text_*`, `glow`, `title_x`, `icon_*`, `fallback_theme`, `header_mode`, `deco_h`, `caption_border`); the presence of `plate_explorer` /
`plate_max` / `plate_max_explorer` switches those plates on. The style-derived keys (`border_width`, `caption_*`, `<part>.*`) are not
used with the library; a set with only a theme.ini works. Both sources render byte-identically for every set.

Library gaps (worked around here): DrawThemeBackground places IMAGEGLYPH glyphs in the whole rect, and nothing draws only the background,
so the button bitmaps are read instead; `ut_get_font` reports weight 400 for XP's `CaptionFont = Trebuchet MS, 10, bold` (v3; v4's
`Tahoma Bold, 8` is 700), so frame_ut.c reads the flags from `ut_get_string`; `ut_get_bitmap` does not say whether the image counted as
having alpha (an all-opaque result is taken as "no alpha" for TRANSPARENTCOLOR keying); `ut_add_font_dir` cannot remove a directory
(luxwm's fontconfig set is replaced per theme, the library's only grows); a part with a zero-height middle stretched taller leaves rows
empty (black) in Wine's draw.c, hence frame.c's row repeat.

## Classic fallback

The classic frame of a DWM theme, used while no compositor runs or with `[Glass] composition = 0`, is chosen in `theme_select()`:
- `frames_classic` naming a set: that set (aero4074, aerovision: `jade4074-classic`);
- `frames_classic = none` (empty or `style` likewise): the theme's **own visual style in msstyles mode**, the spec `|STYLE|COLOR|SIZE`
  (`struct theme` keeps the resolved `style`, `style_color`, `style_size`), drawn live like any bare style (no directory: every tuning
  key at its default, no default icon). If that does not load (a style without a Window class) the glass set's theme.ini
  `fallback_theme` is the last resort;
- `_LHWM_THEME` set directly: the set's `fallback_theme`, where `style` means the own style too.

Log: `luxwm: no compositor: DIR|STYLE|... -> |STYLE|COLOR|SIZE (the theme's own msstyles)` (or `composition = 0: ...`);
`_LHWM_THEME` then publishes the `|STYLE|...` spec, `_LHWM_COMPOSITED` 0. basic5048 / aero5048 and basic5112 / aero5112 use it: both
styles have the whole Window class (CAPTION, MAXCAPTION, MINCAPTION, SMALLCAPTION, FRAMELEFT/RIGHT/BOTTOM, MIN/MAX/CLOSE/RESTORE/
HELPBUTTON): 5048 CAPTIONBARHEIGHT 18, no SIZINGBORDERWIDTH (margins 4/4/23/4), 5112 CAPTIONBARHEIGHT 19, SIZINGBORDERWIDTH 4 (margins
7/7/27/7), both CAPTIONFONT Segoe UI 9 ClearType-Natural (`compare_aero_classic_fallback.png`). aero-patch's style has a Window class
too (CAPTION with SIZINGMARGINS 100/200), but aero4074 keeps `frames_classic = jade4074-classic`.

## Windows Classic frames (`style = classic`)

docs/THEME-FORMAT.md sections 2a and 14: with `VisualStyle.style = classic` (Display Properties' "Windows Classic style", or `lhde-theme
layer-set VisualStyle.style classic`) there is no visual style; the look is Windows' classic one from the record's `Colors.*` and
`Metrics.*` (a scheme `styles/NAME.scheme`, the current layer; without either the built-in Windows Standard). `lh_theme_apply` turns such a
record (and any theme with neither a frame set nor a visual style) into the spec `|classic|smoothing=...;activecaption=R G B;...;
borderwidth=N;captionwidth=N;captionheight=N;captionfont=...;menufont=...` (every `Colors.*` the record has and those five metrics);
`frames_classic` and `Glass.frames` do not count then (the user chose no visual style). `classic_load` (frame.c) keeps `classic = 1`,
`cl[31]` (COLOR_ index order, Windows Standard for what is missing), `cl_capw`, the caption font in `ms_font` and the menu font in
`cl_menufont`; the mode is `THEME_MSSTYLES` with `t->ut == NULL`, so margins, the shape (a plain rectangle), the hit test, the window
menu's icon cell, `_LHWM_FRAME_METRICS` (text colours `captiontext` / `inactivecaptiontext`) and the size-hint gap fill (`btnface`) take
the msstyles geometry. Log: `luxwm: theme slate4074: Windows Classic frames (style classic, scheme brick)`.

`classic_paint` is user32's non-client painting (Windows 2000 / XP `xxxDrawCaptionBar`, `DrawEdge`, `DrawFrame`,
`DrawFrameControl(DFC_CAPTION)`, `xxxDrawCaptionTemp`, GDI's `GradientFill`) for a sizable overlapped window with `WS_EX_WINDOWEDGE`. It
paints into an ARGB image of the frame's size with libwuxtheme's classic helpers (`ut_draw_edge`, `ut_draw_frame_control`,
`ut_draw_caption`: Wine's user32 `DrawEdge` / `DrawFrameControl` and wux's GDI with the frame's colour table as the system colours) and
composites it; Display Properties' classic sample (the Appearance tab with "Windows Classic style") draws its inactive, active and
message-box windows with the same helpers and construction, so there is one implementation of the classic art. luxwm keeps the geometry
(cnBorders, the button rectangles, maximised), the hit test, the shape and the caption icon (cairo):
- frame: `ut_draw_edge(EDGE_RAISED, BF_RECT | BF_ADJUST)` (outer `3dlight` / `3ddkshadow`, inner `btnhighlight` / `btnshadow`; right
  and bottom drawn first, so they own the corners), then the sizing border, `borderwidth` px of `activeborder` / `inactiveborder`, then
  1 px `btnface`: cnBorders = 3 + `borderwidth` on every side. Maximised: the borders lie off-screen.
- caption: `captionheight` rows (SM_CYSIZE), then a 1 px `btnface` separator (SM_CYCAPTION = `captionheight` + 1). Buttons
  (`captionwidth` - 2) x (`captionheight` - 4), 2 px below the caption top; close 2 px from the right, 2 px gap, max and min touching
  (restore while maximised); `ut_draw_frame_control(DFC_CAPTION)`: `DrawPushButton` with `BF_SOFT` (raised: `btnhighlight`, `3dlight` /
  `3ddkshadow`, `btnshadow`; pushed: `EDGE_SUNKEN`, `3ddkshadow`, `btnshadow` / `btnhighlight`, `3dlight`), face `btnface`, glyph
  `btntext`; no hot state (`hover` is ignored). A fixed-size window's max button is `DFCS_INACTIVE`.
- glyphs: Marlett's `r` `0` `1` `2` (wux's GDI maps a symbol font's characters to U+F000 + the byte, as Windows does) in a cell of e =
  min(w, h) of the button's inside (within the 2 px push-button edge), centred, `lfHeight` -e (Marlett's cell is its em); pushed: 1 px
  down and right. At e = 10 (the 16 x 14 button) that is the X 8 x 7, the box 9 x 9 with a 2-row top, the bar 6 x 2 of 4051/XP. The face
  is the first imported `fonts/<build>/marlett.ttf` (`cl_marlett`, registered as a single file with `ut_add_font_dir`, so a theme whose
  fonts lack it still has it), else fontconfig's.
- caption fill: the whole bar in `gradientactivecaption` (the brush around the buttons), then `ut_draw_caption` over the icon slot and
  text area: the slot (a `captionheight` square, solid `activecaption`; luxwm draws the SM_CXSMICON = (`captionwidth` - 2) & ~1 icon in
  it, 1 px right of centre), then the text area (icon slot .. min button - 2) as `GradientFill(GRADIENT_FILL_RECT_H)` from
  `activecaption` to `gradientactivecaption` (GDI's 8.24 fixed point, truncated; solid when the two are equal); inactive:
  `inactivecaption` / `gradientinactivecaption` / `inactivecaptiontext`.
- text: `captionfont` (`Tahoma, -11, bold`; points: -MulDiv(pt, 96, 72)) as a LOGFONT (`cl_face`, `cl_px`, `cl_bold`, `cl_italic`),
  drawn by `DrawText` (`DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS`) 2 px into the text area: GDI's hinted TrueType advances and
  vertical centring. `Fonts.smoothing` (`cl_smooth`, `ut_set_font_smoothing`): `off` bi-level, `standard` grey where the font's gasp
  table allows it (Tahoma stays bi-level at 11 px and is smoothed at 19 and 24 px, as XP's "Standard"), `cleartype` drawn as standard.
  wux's font lookup has no per-glyph fallback.
- the default icon: the first existing of `packs/xp/shell32/16/3.png`, `packs/4074/shell32/16/3.png`, `packs/2k/shell32/16/3.png` (and
  its 32 px size).
- the window menu takes `menu`, `menutext`, `graytext`, `highlight`, `highlighttext`, `btnface`, `btnshadow`, `btnhighlight`,
  `3ddkshadow`, `3dlight` and `menufont`.

Measured against Longhorn 4051's Windows Classic (`lh4051-193915.png`, the Computer window at 109,33, 800 wide, Windows Standard): edge
rows 33/34, band 35/36, caption 37-54 (18), separator 55; buttons 16 x 14 at y 39, close x 887-902, max 869-884, min 853-868; icon slot
113-130, gradient 131-850 (`activecaption` 10 36 106 to 166 202 240), text at x 133, baseline 50. `frametest '|classic|' out.png 800 300
Computer 1 0 none ICON` against the screenshot's rows 33-55 (all 800 columns, the icon slot left out, the imported 4074 fonts): 0 of
18400 pixels differ, the Marlett glyphs included.

## Maximised mode, the desktop layer, running without a compositor

- `_LHWM_MAXIMIZED_MODE` (root, CARDINAL x 10, one per desktop like `_NET_WORKAREA`): 1 while the desktop's active window (current
  desktop: the focused one; others: the most recently focused one that is not minimised) is maximised or fullscreen, or, with no such
  window, any non-minimised window there is. Recomputed after every event, written only on change, deleted on a clean exit. luxshell
  fades its black layers and its desktop layer on it (`lh.maxmode()`; the EWMH rule is its fallback when the `_NET_SUPPORTING_WM_CHECK`
  window is not luxwm's). It is written at the start of `max_transition()`, before any ConfigureWindow, repaint or animation, and on
  restore cleared before the restore geometry (also when a drag un-maximises: `client_unmax_margins`), so a tracer of root
  PropertyNotify and the client's ConfigureNotify sees the mode first, with and without picom.
- DWM sets maximised: the frame fills the work area, margins `0 0 m_t 0` (`_NET_FRAME_EXTENTS` follows), the client fills the work area
  below the caption; no resize edges (`wm_frame_hit_ex`: buttons and caption only), window shape the full rectangle. The caption keeps its
  height, text and button layout (the aero pill and buttons sit in the side margin, so the borders cannot be pushed off-window without
  clipping the close button). With `plate_max` the frame draws 4074's `LGFRAMEMAX` plate (STREAM 493; `LGFRAMEMAXEXPLORER`, 494, under an
  Explorer-style header, i.e. `_LHWM_EXTEND_FRAME` top > 0). Without it the normal plate is 9-sliced as `w x (h + bottom margin)` (bottom
  border below the window, side borders under the client) with the two top corners squared (`square_corners()`: inside a 6x6 square each
  pixel comes from the straight edge next to it, mitred along the diagonal) on plate and overlay.
- Theme keys: `plate_max = ACTIVE.png [INACTIVE.png]`, `plate_max_explorer = ...` (one file: both states), `max_insets` /
  `max_explorer_insets` (top right bottom left; defaults `plate_insets`, then `max_insets`), `max_explorer_rows = r0 r1 line` (as
  `explorer_rows`: rows [0, r0) 1:1, band [r0, r1) stretched to the header height - 2, lines [line, line + 2) on its last two rows, body
  from line + 2; default `2, max_explorer_insets top - 2, same`), `max_button_right` / `max_button_top` (default `button_right` /
  `button_top`), `overlay = 0` (no `overlay.png` at all; default 1). The maximised plate gets the glass weighting like the others but no
  overlay (LGFRAMEOVERLAY belongs to the normal plate; the reference shows no edge highlight) and no fog.
- Textures: **aero** 493/494 are 787x1198 opaque RGB greys (599-row halves, active on top; the art of 5048's uDWM BITMAP 1002,
  COMPOSITION.md 15.4). LGFRAMEMAX: caption rows 0..24 (a horizontal sheen 10 -> 55 -> 0 grey with light rays), light/dark lines at rows
  25/26, body from 27, 1 px edges at x 0/1, 785/786 and rows 597/598; inactive lighter (54..67). MAXEXPLORER: the band over rows 0..77,
  lines 78/79, body from 80. No caption-button pill in either. desksrv 4074 loads them as PNG 122/123 with texture margins (0, 27) (0, 0)
  and (0, 71) (0, 0) (left, top) (right, bottom) at `0x6f733560`/`0x6f733578` (4066: `0x6cc43588`/`0x6cc435a0`), so the whole width
  stretches; the second margin set of those calls, (12, 8) (12, 0), presumably the plate's outset around the window, is not modelled.
  aero-patch: `max_insets = 27 0 0 0`, `max_explorer_insets = 80 0 0 0`, `max_explorer_rows = 0 78 78` (the lines lie 7 rows below
  desksrv's 71, the offset of the Explorer plate's 73 vs 80). **jade4074**: 4074's 493 and 494 are byte-identical to its 483 (the flat
  801x1214 normal plate; COMPOSITION.md 18.1): `max_insets = 27 9 9 9`, `max_explorer_insets = 73 9 9 9`, corners squared. Build 4066's
  own Jade LGFRAMEMAX (787x1198, the flat colours (112,144,112) / (139,160,139) at alpha 213, square corners, split 607/591) is not used.
- Buttons maximised: `aero_max.png` (1920x1080, work area 0..1769) has the sprites end with close's bright column at x 1766, 3 px further
  right than on a normal frame; a fit over gap 0..2, right 0..7, top 0..3 is best at gap 2, right 2, top 2: aero-patch `max_button_right
  = 2`. Caption text maximised: "Maximised caption text".
- **One step.** A maximise or restore is one geometry change: `client_geom_max()` moves and resizes frame and client to their final
  rectangles with the final margins and paints once (restore fits the restore rectangle); with a compositor the server is grabbed from
  the mode write in `max_transition` to the end of that paint, so picom and luxshell see mode, geometry and painted plate together. No
  picom geometry animation runs for it, except with `ANIM_INVENTED` ("Animations").
- **Crossfade** (`max_transition`, `xfade_weight`, `xfade_tick`), off by default: config.h `MAX_FADE_MS` is its duration, 0 (the
  default, aap's choice; lux's bars are instant too) switches the plate in the geometry change's own paint, with no timer. With a
  non-zero value (`-DMAX_FADE_MS=400`), a compositor and a set with `plate_max`, a maximise or restore of a mapped window on the current
  desktop fades the plate at its new rectangle: from the normal plate (squared, maximised geometry) to the maximised one, or back (the
  maximised plate stretched over the restored frame), linear like luxshell's black layers (`lh.animate(..., 400)`), repainted on a 25 ms
  grid by the main loop's poll timeout, its clock starting at the ungrab and one tick in. A second maximise/restore reverses it from
  where it is; a drag that un-maximises ends it. frame.c's `wm_frame_paint_ex3`/`wm_frame_paint_cr3` take the weight `maxfade` (0 normal .. 1
  maximised, < 0 by state) and paint a cross-dissolve `(1 - t) normal + t maximised` (premultiplied, `OPERATOR_ADD` in a group, then over
  the background), so Jade's translucent plate ends at its own alpha; the overlay fades with `1 - t`, the caption text colour and glow
  with t. The three layers (normal plate, maximised plate, masked overlay) are rendered once per fade (`fade_layer`, keyed by size,
  focus, state, extension and theme load): 8..10 ms of cairo per frame for a 1130x770 extended window.
- Reference: the only maximised window in the screenshots is the self-framed Explorer of `lh4074-181424.png` (Slate): top corners square
  and flush with the screen edge, the caption on black, caption buttons ending 3 px before the work area's right edge.
- Shadows: luxwm draws no `shadow.png`; picom's shadow stands in for LGFRAMESHADOW (a 9-grid outset left 40, top 20, right 19, bottom 38,
  COMPOSITION.md 18.1). Each decoration window carries `_LHWM_MAXIMIZED` (CARDINAL 0/1) and `picom-glass.conf` gives `_LHWM_MAXIMIZED@ =
  1` no shadow; the `@` (top-level) form matters, picom 13 does not re-read the client-window form when the property changes. The client
  keeps its shadow (it falls under the docks, `clip-shadow-above`).
- Desktop windows (`_NET_WM_WINDOW_TYPE_DESKTOP`) are never managed; `docks_raise()` lowers them to the bottom after every restack. Among
  themselves higher `_LHWM_DESKTOP_LAYER` (CARDINAL, default 0, signed) lies above lower, equal layers in map order with the first-mapped
  lowest (CLIENT-PROTOCOL.md "Desktop windows"); a desktop window that lowers itself (flxp-desktop does on map) is put back into that
  order.
- Without a compositor: classic frames ("Classic fallback", "Themes and frame sets"), XP's caption flight ("Animations"), the header
  pixmap for extended clients ("Extended frames").

## Icons, maximised caption text, fonts

### Window icons

`client_set_icon`, `client_icon`, frame.c `wm_frame_icon_from_netwm`, `wm_frame_icon_from_pixmap`, `wm_theme_default_icon`. The caption icon is:
1. `_NET_WM_ICON` (read up to 16M items): the smallest image at least the icon size (`icon_size`, msstyles 16), else the largest, scaled
   once to the size with `CAIRO_FILTER_BEST` (aspect kept, centred);
2. else `WM_HINTS` `icon_pixmap` + `icon_mask` (depth 1: black on white as the classic X icon box; other depths through the default
   visual; depth 32 taken as premultiplied ARGB; errors from a pixmap the client freed meanwhile swallowed), scaled the same way;
3. else the generic application icon (`app_icon`: shell32 #3, what Windows draws for a window without an icon, from the first imported
   pack of 4074, 4051, 5112, 4042, `packs/BUILD/shell32/16|32/3.png`); urxvt and xterm get it;
4. else the set's default icon: `default_icon = FILE [FILE...]` (relative to the set's directory, or with a `/` to the data
   directories; the nearest size at or above is used), else `default_icon.png`. The DWM sets' `default_icon.png` is the CompositedWindow
   `DEFAULTICON` part (STREAM 506, a 20x17 Windows flag), drawn 1:1 centred on the 16 px cell (icons at most 4 px larger than the cell are
   not scaled); the classic sets say `default_icon = packs/4074/shell32/16/3.png packs/4074/shell32/32/3.png` (the converter writes it).

PropertyNotify: `_NET_WM_ICON` set, changed or deleted re-reads it; `WM_HINTS` re-reads the icon when its pixmap or mask id changed
(urgency updates do not). Theme switches re-read at the new size. The caption text of a client without an icon starts after the
default icon (XP's indentation). `LHWM_DEBUG=1` logs `icon 0x...: client | application | theme default | none`.

### Maximised caption text

`text_max_active`, `text_max_inactive`, `glow_max` (defaults `text_active`, `text_inactive`, `glow`; `glow_max = none` drops the
TextGlow). `CompositedWindow` has no text colour at all in 4074 Jade, the patched Aero and 4093 (every part is a single
`PNGSTREAM`/`D3DXSTREAM`; COMPOSITION.md 19). The maximised look lives elsewhere in the style: Explorer's header title and breadcrumbs
turn white (255,255,255) from navy (0,2,92) with the white glow dropped, inactive maximised text stays grey (98,100,105), and
`aero_max.png` shows the white title on the dark LGFRAMEMAX header. The DWM sets: `text_max_active = #ffffff`,
`text_max_inactive = #626469`, `glow_max = none`; Jade's LGFRAMEMAX is its normal green plate, so the white title sits on green there (the data has nothing
Jade-specific). With the crossfade on, text colour and glow alpha follow the plate's weight. msstyles sets: the keys apply to
MAXCAPTION, but no shipped style defines a MAXCAPTION text colour (`Window.MAXCAPTION(ACTIVE)` has only `TEXTSHADOWCOLOR = 0 0 0`,
`TEXTSHADOWTYPE = CONTINUOUS` in 4074 Jade and Slate and 4051 Slate, written as `maxcaption.shadow.1`), so they keep CAPTIONTEXT.

### Fonts

The theme's font directories (`path.fonts`, from `[Fonts] fonts`; frame.c `wm_frame_fonts_set`) plus the set's `font_files` (files or
directories) are registered at every theme load as the process's fontconfig application fonts, replacing the previous set
(`FcConfigAppFontClear`, then `FcConfigAppFontAddDir`/`AddFile`, then pango's default font map is dropped; only when the set changed).
`LHWM_DEBUG=1` logs the resolved face per load, e.g. `caption font "Segoe UI 9" -> "Segoe UI 9" (.../Fonts/seguibk.ttf)`;
jade4074-classic's `Segoe UI,Tahoma 12px` resolves to Segoe UI, Luna to Trebuchet MS Bold (`compare_fonts.png`).

## Extended frames (the Explorer tall caption, `CLIENT-PROTOCOL.md`)

A cooperating ARGB client sets `_LHWM_EXTEND_FRAME` (`left right top bottom`, client pixels) and `_LHWM_FRAME_FLAGS` (`NO_TITLE` 1,
`NO_ICON` 2, `NO_FOG` 4, `CLIENT_CAPTION` 8, `NO_HEADER_PIXMAP` 16). Composited, luxwm paints the plate further in under the client and
the client leaves that part transparent. The reference client is `examples/extendframe.c` (`compare_extend.png`: next to the 4074
Documents window). The client properties are read at map time and on every PropertyNotify, deletion included (PropertyChangeMask is
selected before they are first read).

- `wm_frame_paint_ex4`/`wm_frame_paint_cr3` take a `struct frame_ext`; NULL, or all zero with no flags, is the plain path. `plate_ext` paints
  the plate on a 7x7 grid: per axis outer edge | band (stretched by the extension) | inner lines | middle | inner lines | band | outer
  edge, so the inner dark/light lines move in by the extension. The caption rows that stretch are `extend_rows` (aero-patch `15 25`,
  below the pill). With `fog = 1`, `fog_band` paints `captionfog.png` over the caption row and the extended top unless `NO_FOG`.
  Maximised: the same on the `w x (h + bottom margin)` plate, then `square_corners`. An extended top > 0 uses the Explorer plate when
  the set has one (below).
- Only while composited (a compositor and a DWM set). Otherwise the frame is the normal one with title and icon, and the client paints
  its header opaque. Root `_LHWM_COMPOSITED` (0/1) and `_LHWM_FRAME_METRICS` (CARDINAL[19]: `caption_h border_l border_r border_b
  text_rgb glow_rgb inactive_text_rgb 0 max_caption_h max_border_l max_border_r max_border_b border_t max_border_t max_text_rgb
  max_inactive_text_rgb max_glow_rgb 0 deco_h`; `max_glow_rgb` 0xFFFFFFFF = no glow, for msstyles MAXCAPTION's text shadow, else
  CAPTION's; `deco_h` the set's `deco_h`, else `caption_h`; aero4074 `... 0xffffff 0x626469 0xffffffff 0 27`) are written on change by
  `composited_publish()` (from `theme_select()`: compositor appeared/gone, theme switch) and deleted on exit. Extended clients are
  repainted when `_LHWM_COMPOSITED` changes. `picom-glass.conf` gives `_LHWM_EXTEND_FRAME@` clients no blur and no shadow.
- `_NET_WM_MOVERESIZE` (in `_NET_SUPPORTED`): `drag_run()` is the frame's edge/corner drag, moves go through `drag_move_step()` (the
  caption/Super drag loop uses it too). Directions 0..7 resize (ignored while maximised or for a fixed size), 8 moves, 9/10 size/move
  with the keyboard ("The window menu"), 11 cancels; Escape cancels too.
- **Header pixmap** (without composition: classic sets, with or without picom): the decoration window keeps the normal frame (the
  extension does not change it; `NO_TITLE`/`NO_ICON` apply), and `header_update()` (end of every `draw_text`) publishes
  `_LHWM_HEADER_PIXMAP` on the client: a pixmap of the client's depth and visual (created on the root, so a client destroyed meanwhile
  cannot make `XCreatePixmap` fail), client width x (top frame margin + header rows), painted by `wm_frame_header_image()`: the whole frame
  painted with the extension, `ms_paint` nine-gridding the CAPTION (maximised: MAXCAPTION) bitmap over `caption + extension` rows and
  starting the side frames below it, title and icon left out, cropped to the client's columns and rows 0..band; its last row lies
  against the bottom of the client's header. Rendered again only when size, focus, maximised state, extension, theme generation or, with
  `CLIENT_CAPTION`, button hover/press change (new pixmap, property replaced, old one freed). Deleted when composited, when the
  extension goes, on unmanage, on a clean exit, and while `NO_HEADER_PIXMAP` is set (`FRAME_NO_HEADER_PIXMAP`, wm.c's only).
  `examples/extendframe` copies it.
- **`CLIENT_CAPTION`** (experimental): `margins_for()` cuts the top margin to `wm_theme_caption_border()` (msstyles cnBorders, DWM the side
  border `m_l` or the set's `caption_border`, maximised 0), so the client covers the caption; geometry, `_NET_FRAME_EXTENTS`, drags and
  theme switches go through it. A new window keeps a normal frame's size and its client grows by the caption; toggling the flag at
  runtime keeps the frame and resizes the client; adoption puts the frame around the client as it is. A theme switch keeps the logical
  rectangle (frame minus the normal margins), so a client toggling the flag per theme does not drift. `_LHWM_CAPTION_RECT` (client
  coordinates, the union of the button hit rectangles from `wm_frame_buttons_rect()`, clipped to the client) is written on change and on
  every maximise/restore, after `_NET_WM_STATE` (`header_hold` keeps the intermediate paint in `client_apply_max` from publishing). The
  decoration window under the client is unchanged, so buttons and hit-testing stay luxwm's and work through the client's input-shape
  hole. Composited, the extension's top counts from the client's top, and the plate band is stretched from the normal caption bottom to
  there. `client_hit()`: not maximised, every border row above the client resizes; clients can also send `_NET_WM_MOVERESIZE` direction 1
  from their header. The opaque-client paint hole ("Performance") is off for these clients.

### Explorer plate (`plate_explorer`, `explorer_insets`, `explorer_rows`)

4074's desksrv has a separate part for Explorer windows, `LgFrameExplorer` (STREAM 484, desksrv's built-in 112), 9-gridded with texture
margins (9, 73) (100, 9) instead of LgFrame's (9, 27) (100, 9) (COMPOSITION.md 15.1). aero-patch's `plate_explorer_active.png` /
`plate_explorer_inactive.png` are the two 801x607 halves of `aero.msstyles` STREAM 484 (active top). Jade's 484 is byte-identical to its
483, a flat green plate, used the same way.

On the aero texture the 73-row top band (rows 0..72) is the tall caption gradient (alpha rising from ~75 to ~150 in the middle columns,
the pill in rows 2..14 of the right 100 columns); the gradient goes on to row 77, the inner light/dark lines are rows **78/79** and the
texture's client area starts at row 80; left/right/bottom are the normal plate's 9/100/9 px. theme.ini:

    plate_explorer = plate_explorer_active.png plate_explorer_inactive.png   # one file: used for both states
    explorer_insets = 73 100 9 9        # top right bottom left (default: plate_insets)
    explorer_rows = 15 71 78            # band rows [15, 71) stretched, inner lines at rows 78/79 (default: extend_rows start, top-2, top-2)

It is used for an extended frame whose top reaches into the client (`_LHWM_EXTEND_FRAME` top > 0, not the sheet of glass), composited or
in frametest. With header height H = caption (27) + top, `plate_explorer()` maps the rows: 0..14 1:1 (pill), band 15..70 stretched to
15..H-3, the texture's lines 78/79 onto rows H-2/H-1 (right above the client's content), then the body from row 80 and the bottom edge as
`plate_ext`. A 46 px extension (H = 73) copies rows 0..70 1:1 (frametest `ext46` against the texture: at most 1 level apart in the 1:1
columns); other heights stretch only the band's middle rows. Columns as `plate_ext` (9 and 100 px kept, the middle stretched with the
window width, as desksrv's 9-grid). The 4074 Documents window has a 77-row header (frame top y=74, lines at y=149/150, menu bar from 151),
i.e. desksrv's 9-grid squeezes rows 73..79 into 73..76 there; a client that wants that geometry extends by 50.

The caption fog is off by default (`fog = 1` re-enables it; clients' `NO_FOG` is then moot): the reference has none (COMPOSITION.md
15), and with the thinner glass the fog dominated the error ("Glass accuracy").

### Aero vision (`header_mode`, `deco_h`, `caption_border`, `button_files`)

The experimental frame set `aerovision` for the concept-art navigation bar (docs/NAVBAR.md, `luxnav/`, `examples/navdemo.c`;
CLIENT-PROTOCOL.md "Vision header"; a "vision" theme, i.e. taste, not a build): aero-patch's plates and glass with:
- `header_mode = crop` (frame.c `crop_rows`, used by `plate_explorer` and the MAXEXPLORER branch of `plate_max`): the Explorer plate's
  rows are copied 1:1 down to the header's last two rows instead of stretching the band [15, 71); the texture's line pair (rows 78/79:
  light, then black) goes on the header's last two rows. aap's idea ("we never scale the tall titlebar plate vertically ... redraw a 1px
  black frame on top and use the inside for the client area"). A header taller than the band (more than 80 rows) repeats the band's last
  row; any client header height works (small 47 rows: lines on frame rows 47/48; big 69: lines 69/70, body from 71).
- `deco_h = 15`: the decoration ends on row 15, under the buttons. `wm_frame_hit_ex` and `wm_frame_buttons_rect` clip the buttons' hit slop to
  it (so `_LHWM_CAPTION_RECT` ends there too); `_LHWM_FRAME_METRICS` field 18.
- `caption_border = 2` (`wm_theme_caption_border`): a `CLIENT_CAPTION` client starts on frame row 2, so it can draw a small title with the
  baseline on row 15 (with the side border, 9, its top 3..4 glyph rows would lie above the client).
- `button_files = vision_close.png vision_max.png vision_min.png vision_restore.png`, `button_frame_h = 13`: 13-row caption buttons
  (rows 2..14, ending with the pill and the deco), made by `tools/build_aerovision.py` from the aero sprites by dropping sprite row 11
  (the light line above the dark bottom line; `--drop-row N` to try another), widths kept (27 + 25 + 43, filling the pill). The key exists
  because `close.png` etc. are the style's LGCLOSEBTN/... streams in library mode; names outside that map come from the directory.
- `nav.ini` is not luxwm's: luxnav's tuning (`[preset small]` / `[preset big]`: variant, orb size, hand placement, fonts, colours, glow;
  NAVBAR.md "Presets"), read by luxnav clients from the directory of `_LHWM_THEME` on every reload, so `session/theme-reload.sh` /
  `theme-watch.sh` (which watches `nav.ini` too) live-update the header. The header height is the client's (`_LHWM_EXTEND_FRAME` top).

Everything else (normal windows, maximised, the classic frame `jade4074-classic`) is aero's, apart from the 13-row buttons.

## Glass accuracy

**aero** (aero-patch) is fitted to the patched 4074 screenshot `4074-dwm.png` (its textures are the 5048 art of the aero zip,
COMPOSITION.md 15; wallpaper `longhorn.jpg` stretched to 1280x1024), **jade** (jade4074) to build 4066's `4066-MIL-Effects.png` (the
shipped Jade plate through desksrv's fixed-function path, COMPOSITION.md 18).

- **Blur.** SS_GLASSSHADER = 3x3 taps at 1.5 px = Gaussian sigma ~1.2 px. A per-row affine fit of the Documents glass (y 100..148)
  against the wallpaper blurred by sigma 0..8 is best at 1.2 (rmse 14.0 vs 15.0 unblurred, 15.7 at 3, 18.9 at 5, 23.8 at 8; the 14 floor
  is JPEG/scale mismatch). picom: glx `blur-strength = 1` (the mildest dual_kawase level, sigma ~1.9; it cannot go lower), the xrender
  lab `--blur-method gaussian --blur-size 5 --blur-deviation 1.2` (measures sigma 1.25).
- **Alpha.** The plate textures are straight alpha: 99.4% of the translucent pixels have rgb > alpha (teal 118,145,153 at alpha 60), and
  over the Documents frame body (plate rows 29..75) the straight blend `a*rgb+(1-a)*bg` matches best (rmse 18.8; the premultiplied form
  `rgb+(1-a)*bg` saturates, 97). cairo's straight-alpha load is right.
- **Shadow.** `shadow.png` (860x664, the 801x607 plate at +41,+19, the hole under the plate transparent): peak alpha 0.53 at the plate
  edge, half value 17 px out on the left and bottom, 10..90% spread ~26 px (sigma ~10), only a faint tail on the right (<0.05) and top
  (<0.12): a plate-sized rect shifted ~15 px left and ~13 px down. picom radius 22, opacity 0.52, offsets -33/-6 reproduce the bottom
  profile within 3%; the top tail is weaker than the original.
- **Double shadow.** luxwm sets `_LHWM_DECORATED` (CARDINAL 1) on every client that has a decoration window (before mapping; removed when
  the decoration goes); `picom-glass.conf` gives `_LHWM_DECORATED@` no shadow, the decoration windows keep theirs (`compare_glass.png`).
- **Glass weight** (`glass_alpha`, `glass_alpha_mode`; COMPOSITION.md 15.3). The reference glass is about half as opaque as the textures
  say. `glass_alpha` (default 1.0) multiplies the plate's alpha; `glass_alpha_mode = square` (default `linear`) squares it first (a ->
  a^2, the 5048 `BlurUnder_Glass` equation with premultiplied textures; meant with 1.0). Only the plate (normal, Explorer, maximised) is
  weighted; overlay, buttons, glows, fog and text keep their alpha. It applies when the plate is composited (`plate_draw()`:
  `cairo_paint_with_alpha`, or the plate masked by its own alpha for `square`), not to the loaded textures, whose raw alpha is also the
  overlay's mask; `shape_alpha` is mapped through the same factor so the corner cut stays put. Fit (inactive Documents header rows 2..72
  at x 560..900 over the wallpaper blurred sigma 1.2; rmse in levels): Explorer plate with `NO_FOG`, glass_alpha 1.0: 29.2; 0.5: 6.3;
  `square`: 5.6 (the texture model alone 5.2; best free factor 0.40: 4.3); the fog adds ~30. aero-patch ships `square` with 1.0.
- **Jade** (COMPOSITION.md 18.1/18.6). The 4066 Explorer window (frame 810x600 at 33,51, 77-row header) shows one flat plate colour,
  (104,138,121), over the flat (58,110,165) wallpaper: the theme's (112,144,112) at alpha 213/255 = 0.835 blended straight, no squared
  alpha, no factor, no overlay, no fog, buttons min 27 + max 25 + close 43 abutting, 2 px below the top, ending 6 px before the right
  edge. jade4074: `glass_alpha = 1.0` (linear), `overlay = 0`, `fog = 0`, `button_gap = 0`, `button_right = 6`, `button_top = 2`. frametest
  at 810x600 with `FRAMETEST_EXT="0 0 50 0 1"` blended over the flat wallpaper, against every frame pixel of the screenshot over flat
  wallpaper (72198 px): 99.95% within 1 level ((104,138,122)); the button strip (x 742..836, y 53..66): rmse 0.57, max 1. The 34 pixels
  off by more are the four corners: 4066's texture has 2-row rounded corners top and bottom, 4074's (ours, the theme is 4074 Jade) a
  4-row top corner and square bottom corners (`compare_jade_4066.png`). The 4066 screenshot machine evidently had no ps_2_0 blur (the
  icon text's ClearType fringes survive under the frame); picom's blur stays for both sets.

## Performance (interactive resize)

What keeps drags fast (with `LHWM_PROF=1`: an xterm right-edge drag of 200 motion events applies 40..49 steps with as many paints, one
paint 1.0 ms cairo + 2.4..4.6 ms upload; an ARGB extended window 7..8 ms cairo + 13..19 ms upload):
- **Expose coalescing.** Each resize of the decoration window, and each shape change, exposes it. An Expose whose serial is older than
  the first upload request of the last paint is ignored (`wm_frame_upload_serial`: that paint covers it); newer ones mark the frame dirty
  and `paint_dirty()` repaints once when the event queue is empty (main loop, and in the drag loop before it blocks).
- **Throttled steps.** `drag_run` coalesces motion and applies a step at most every `DRAG_INTERVAL` ms (config.h, 16); a position held
  back is applied when the interval is over (poll with a timeout on the X connection) or when the drag ends, so the final geometry is
  exact. Modifier drags (berry's loop in `handle_button_press`) coalesce motion too. Client and frame are resized together in every step
  (a lagging client would show the glass through the gap).
- **The hidden plate is not painted.** Under an opaque client (depth != 32, no `_LHWM_EXTEND_FRAME`, no `CLIENT_CAPTION`) the plate is
  covered: `frame_paint_ex2` takes that hole (the client's real rectangle, kept clear of the 24 top/bottom rows the shape scan reads),
  clips cairo to the complement (pixel-aligned, so the band pixels equal a full paint) and uploads only the four bands with plain
  `XPutImage`. ARGB clients and extended frames get the whole image through cairo-xlib (MIT-SHM; a plain full-size `XPutImage` measured
  slower, 28 vs 17 ms).
- **Drag threshold.** A press becomes a move only once the pointer leaves a `DRAG_THRESHOLD` (config.h, 4 px = SM_CXDRAG) square, in
  berry's loop and in `drag_run` (caption moves by `_NET_WM_MOVERESIZE` too); so 1 px of jitter in a double-click does not un-maximise
  the window between the clicks. A real drag still moves by the full pointer delta.

### Resize without flicker

Under Composite a window's newly added area shows whatever it holds until it is painted. What luxwm does about it (wm.c
`drag_client_begin` .. `drag_client_end`, `geom_paint`, frame.c `wm_frame_pre_upload`):
- **Order of a step.** The frame is painted at the new size first and uploaded into an offscreen pixmap; then, under a short
  `XGrabServer`, client and decoration window are resized (client first; a left/top edge is one `XMoveResizeWindow` each), the shape
  set and the pixmap copied into the frame (the four bands, or all of it for ARGB/extended clients). Upload, then `XSync`, so the grab
  covers only those few small requests. This applies to every `client_resize_absolute`, not only drags. Without it picom could catch the
  frame unpainted at its new size, or the client resized and the frame's band not yet copied.
- **Decoration window:** NorthWest bit gravity and no background (`CWBackPixmap` None): a resize keeps the old frame pixels instead of
  clearing the window.
- **Client bit gravity:** a client with ForgetGravity (`extendframe`, plain Xlib programs) gets NorthWest for the drag, so its old pixels
  stay; afterwards its own value again and one synthetic full Expose (it only got Expose events for the new areas). Xt, GTK and Qt
  toplevels use NorthWest already (the ForgetGravity windows of xlogo/xclock/xeyes are children, which luxwm does not touch).
- **Client background:** GTK 3 and Qt 5 toplevels have background None, so the strip a resize adds is a copy of the screen there (the
  old frame border, the desktop) until the client paints it; a client needing 15..40 ms per paint would show it in nearly every frame.
  When a drag starts luxwm reads the client's right-edge pixel; the first step that grows the client by 4 px or more tells None (the
  strip is a copy of the screen: two points of it equal the screen pixels read just before the resize, both inside the server grab) from
  a background pixel; a None client then has its own edge colour as background pixel for the drag and None again afterwards. Clients
  with a background pixel are left alone (X cannot read a background back). Only opaque TrueColor clients in the root's pixel format (an
  ARGB client's new area is transparent, the glass shows through).
- **`_NET_WM_SYNC_REQUEST`** (EWMH basic frame sync, in `_NET_SUPPORTED`): for a client listing it in `WM_PROTOCOLS` with a
  `_NET_WM_SYNC_REQUEST_COUNTER`, each step that changes its size is preceded by the request with the next counter value, and the next
  step waits (XSync alarm) until the client has set the counter, at most `SYNC_WAIT` (50) ms, besides `DRAG_INTERVAL`. GTK and Qt
  answer it; a client needing 40 ms per paint gets ~26 steps per second instead of 61 resizes it cannot draw. It does not by itself
  remove the strip (picom has no frame sync).

On the lab (1 s drags, 70 root grabs each) GTK 3 and Qt 5 clients, also slowed to 15 / 40 ms per paint, show 0..4 bad frames (the first
step, before the background is known, and the press/release frame); xterm and `extendframe` none. Caveats: a client that sets its own
background during the drag gets None at the end of it. A None client whose right edge is not its background colour (a scrollbar, a
coloured sidebar) shows that colour in the growing strip. A pixel-background client whose strip happens to equal the screen at both
probe points is taken for None. The client's own repaint time remains; only a compositor with frame sync could hide it completely.

## Size hints

ICCCM 4.1.2.3 `WM_NORMAL_HINTS`, in every path that sizes a client, so terminals (urxvt, xterm, emacs, mlterm) get whole character
cells and no strip they never repaint.
- **Reading** (`client_read_hints`): `XGetWMNormalHints` at map time and on every PropertyNotify of `WM_NORMAL_HINTS` (urxvt and xterm
  rewrite it on a font change). Set, changed or deleted: re-read, then refitted (maximised: re-laid out in the work area; otherwise
  resized to its current frame, which rounds it). ICCCM's defaults: no `PBaseSize` → the base is `PMinSize`, no `PMinSize` → the minimum
  is the base; a maximum below the minimum is raised to it. `-d` logs `hints 0x...: base, inc, min, max, aspect`.
- **Rounding** (`client_hint_size`, as dwm's applysizehints): aspect first (`PAspect`; on the size minus the base when a base other than
  the minimum is given, else on the whole size; the side that is too long is reduced), then `base + n * inc` rounded **down**, then the
  minimum and maximum. Never larger than asked, except for the minimum.
- **Where the client goes** (`client_area`): inside the frame margins at the hinted size. Not maximised, the frame is fitted around it
  (`frame_fit`: frame = client + margins); maximised, the frame stays the work area and the client may be smaller by the remainder (the
  minimum is capped at the work area). Fullscreen ignores the hints.
- **Paths:** edge and corner drags and `_NET_WM_MOVERESIZE` 0..7 (`drag_resize_step`: the frame size is fitted before it is applied; a
  left/top drag keeps the right/bottom edge and moves the left/top edge by whole cells), the sync-throttled steps, keyboard Size
  (`_NET_WM_MOVERESIZE` 9), `luxwmc window_resize` / `window_resize_absolute` and every other `client_resize_absolute`
  (ConfigureRequests, the work-area clamp of new windows, snap), maximise/restore, new windows (the initial size, and `CLIENT_CAPTION`'s
  growth, through `client_refresh`), theme switches (`client_retheme` fits the frame).
- **Maximised remainder.** The X11 way, not Windows' (Windows gives the client the whole area and terminals paint the rest): the frame
  fills the work area, the client is `base + n * inc`, and the frame paints what lies under the remainder (right and bottom). DWM sets
  paint the plate there (the opaque-client hole is the client's real rectangle): aero's dark LGFRAMEMAX body, jade's green glass.
  msstyles frames leave the client area transparent, so `wm_frame_paint_ex4` (its `client` rect; `ms_gap_fill`) fills the free part with the
  border's colour (FRAMELEFT drawn in its frame state, sampled in the middle; button-face grey without the part; Windows Classic
  `btnface`), as if the border went on around the client. `_NET_FRAME_EXTENTS` is the frame around the client as it lies (a maximised
  urxvt: `0 4 27 2` on aero4074, `0 4 20 9` on slate4074), written only when it changes.
- **Fixed size** (`client_fixed`: minimum and maximum equal; wux writes min == max == its size for a win32 window without
  `WS_THICKFRAME`, following every resize it makes itself, plus `_MOTIF_WM_HINTS` without resize and maximize: flxp's dialogs, property
  sheets, message boxes). In every frame mode its edges and corners are a plain border (`client_hit`: `HTBORDER`, no size cursor; a drag
  there neither sizes nor moves); a modifier resize drag, `_NET_WM_MOVERESIZE` 0..7 and the window menu's Size do nothing; maximise is off
  (button, double click, Super+Up, the menu's Maximize greyed) and the maximise button is drawn disabled (msstyles: the strip's disabled
  image; classic: `DFCS_INACTIVE`; DWM sprites have none, so the inactive one at 45 %; `FRAME_MAX_DISABLED`, luxwm's own bit above the
  client's `_LHWM_FRAME_FLAGS`).
- **ConfigureRequest:** only the fields in the request's `value_mask` are applied (berry applied all, so a client that asked for a size
  before it was placed was moved back to 0,0); the requested size is fitted like any other.
- **Not refitted, on purpose:** a `CLIENT_CAPTION` toggle and a theme switch of a `CLIENT_CAPTION` client keep the frame (the client
  changes by the caption; rounding that at every toggle drifts by a row), so such a client may keep a remainder on the plate until its
  next resize. No cooperating client sets increments.

GTK clients publish increments but paint their whole window anyway (emacs; GTK terminals set `PResizeInc` from the cell size), so for
them the rounding only changes the size. Clients without increments (xclock, `extendframe`) keep exact sizes.

## Animations (config.h `ANIMATE`)

Minimise and restore animate the same way from every path (frame button, `minimize`, `_NET_WM_STATE_HIDDEN` and `WM_CHANGE_STATE`;
`_NET_ACTIVE_WINDOW` and `_NET_WM_STATE` remove-hidden), because all of them go through `client_minimize` / `client_restore`; maximise
and restore go through `max_transition()`.

- **Minimise unmaps.** Client and frame are unmapped where they are (WM_STATE Iconic, `_NET_WM_STATE_HIDDEN`), which lets picom see a
  "hide" and, when mapped again, a "show". luxwm's own UnmapNotify events are counted (`unmap_ignore`; the client's and the root's
  copy); a minimised client that is destroyed is dropped on DestroyNotify.
- **The style** is the theme's `[Glass] animations` (`Glass.animations`: `4074plus | 4074 | 5048 | invented | none`, or the number
  7/1/5/3/0), re-read on every theme switch and `_LH_SETTINGS_CHANGED`; `LHWM_ANIMATIONS=NAME` overrides it (for the lab); without either,
  or for an unknown name (logged), config.h's `ANIMATE_STYLE` (`ANIM_4074PLUS`). Log: `luxwm: animations 4074 (_LHWM_ANIMATE 1) from
  theme NAME|LHWM_ANIMATIONS|config.h`.
- **With picom** (`picom-glass.conf` window rules, COMPOSITION.md 14): luxwm sets `_LHWM_ANIMATE` (CARDINAL, the style's number) on every
  client and decoration window before mapping them, and each value has its own rule set. A change rewrites it on every window at once
  (one in a running `ANIM_INVENTED` geometry animation gets it when that ends), so picom's rules switch live; `none` deletes the property
  (no rule matches; picom's hide/show then end at once) and also turns off luxwm's caption flight without a compositor. Docks, desktop
  and notification windows are excluded in the rules. `composition` is not luxwm's (lux/picom).
  - `ANIM_4074PLUS` (7, default): window **open** (picom `open`) = 4074's 350 ms fade with a gentle 0.95 -> 1 zoom about the centre;
    **restore from minimised** (`show`) = a 250 ms grow + fade in from 15 % at the window's horizontal centre on the bottom edge of its
    monitor (ease-out); **minimise** (`hide`) = the 250 ms shrink + fade out towards it (ease-in); close = picom's default fade;
    **maximise/restore** = luxwm's one-step change (and the plate crossfade when `MAX_FADE_MS` is set), no picom animation.
  - `ANIM_4074` (1): what 4074's desksrv does. Only a hidden -> visible transition animates (**open**, and **restore from minimised**,
    the same `'DWM!' 0xcc` message there): opacity 0 -> 1 over 350 ms on a timeline with acceleration 0.3 / deceleration 0.4, as
    `cubic-bezier(0.35, 0, 0.575, 1)` (within 0.01 of the Avalon curve), blur and shadow fading with it. 4074 also swings the window in
    in 3D (300 ms, decel 0.3: from 0.25 window-widths behind, turned 35 deg about its top edge); picom has no perspective, so the swing is
    flattened head-on: scale-x 0.93 -> 1 about the centre (picom scales about the top-left, `offset-x` compensates) and scale-y 0.762
    (0.93 * cos 35 deg) -> 1 anchored at the top edge, on `cubic-bezier(0.225, 0.2, 0.725, 1)` (within 0.016). Client and frame stay
    registered to within a pixel or two while nearly transparent; the real swing needs a compositor of our own. Minimise and close are
    instant (their rule has no timing function), maximise/restore too.
  - `ANIM_5048` (5): the April 2005 uDWM variant: one 266 ms curve `p = sqrt(t / 0.266 s)` (`cubic-bezier(0, 0.55, 0.95, 1)`, within
    0.016) for opacity and pose; the pose (pitched and pushed 0.75 back on open, tilted/twisted from below-left on restore) is a scale
    0.9 -> 1 here. Minimise, close, maximise instant, as in 5048.
  - `ANIM_INVENTED` (3; not Longhorn, kept for comparison): "hide" shrinks both windows to 15 % towards the window's horizontal centre on
    the bottom edge of its monitor, fading out (opacity, blur, shadow), 250 ms ease-in `cubic-bezier(0.4, 0, 1, 1)`; "show" the reverse,
    ease-out `cubic-bezier(0, 0, 0.2, 1)`; open/close picom's default fade. For maximise/restore luxwm sets `_LHWM_ANIMATE = 2` on client
    and frame right before the geometry changes and back to 3 after 400 ms (`anim_expire`), and the rule for `= 2` adds picom's
    `geometry-change` preset (250 ms), so ordinary moves, resizes and desktop switches are never animated.

  picom starts client and frame animations in the same millisecond, except "open": the decoration window is created at the MapRequest,
  so picom imports it one frame (30..50 ms on the lab) after the client.
- **Without a compositor** luxwm draws XP's animation itself (`anim_classic`), on the root with the server grabbed, from the frame
  rectangle to the taskbar button (`_NET_WM_ICON_GEOMETRY`, written by luxshell; fallback a 160x24 rectangle at the bottom-left of the
  work area) and back on restore, and from the old to the new rectangle on maximise/restore (XP does that too). Reference:
  `nt5src/.../windows/core/ntuser/kernel/minmax.c`. XP uses only `IDANI_CAPTION`: `xxxDrawAnimatedRects` (line 1051) calls
  `xxxAnimateCaption` (line 567), which flies the active caption bar: `cy = CYCAPTION - 1` rows, rectangles taller than a caption inset by
  the window borders, the strip drawn once at the larger width and cut from the left to the current width, left/top/width moved linearly
  with the elapsed time, `start + MultDiv(delta, elapsed, CMS_ANIMATION)` with `CMS_ANIMATION = 250`, `UserSleep(1)` between frames, the
  screen under the strip saved and restored every frame. The XOR wireframe (`AnimateFrame`/`DrawWireFrame`) is Win95 code XP compiles out
  ("DISABLE OLD ANIMATION FOR M7", `#if 0`). luxwm flies the caption (the frame's caption rows painted focused, inside the borders, over
  black; save-under with `XCopyArea` and `IncludeInferiors`) with a 10 ms sleep per frame: 25..26 frames in ~255 ms. `ANIMATE_WIREFRAME
  true` draws XOR rectangles instead (3 px, `GXxor` with white ^ black, same timing). Each frame is `XSync`ed; the screen afterwards is
  what it was.

## Known gaps

- `_NET_WM_STATE_ABOVE`/`STICKY`/`SHADED` on clients, `_NET_WM_ALLOWED_ACTIONS`, per-monitor work areas in `_NET_WORKAREA` (one rect for
  the whole root; maximise uses the monitor's own strut-free rect), `_NET_WM_DESKTOP` 0xFFFFFFFF.
- msstyles: SMALLCAPTION rules (tool windows), MINCAPTION, the help button.
- flxp's Run dialog cannot be opened from outside (only from the start panel); it would need e.g. `flxp-taskbar --run` or a
  `_FLXP_COMMAND run` root message, then `Mod4+r exec flxp-taskbar --run`.
- Lab artefacts (picom 13 xrender on Xvfb, unchecked on glx): one or more composed frames right after a maximise/restore sometimes show
  the resized window, and the windows it covers, missing (wallpaper), mostly on restore and mostly with luxshell running; an extended
  ARGB client that is maximised or restored sometimes shows its header black in picom's output while the decoration window holds the
  right pixels (a picom restart shows the plate); with luxshell's desktop layer mapped, activating a maximised window that lay under
  another sometimes leaves its client area showing the wallpaper (excluding the layer from `blur-background` made it worse, so
  `picom-glass.conf` does not). picom glx on Xvfb (llvmpipe) does not repaint the crossfade at all.
