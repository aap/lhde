# luxwm: a Longhorn-style window manager for X11

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, lhwm/README.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhde/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

A fork of [berry](https://github.com/JLErvin/berry) (MIT, Joshua L Ervin; see LICENSE), keeping its
reparent-free design: every client gets a separate decoration window stacked directly beneath it.
luxwm turns that decoration window into a 32-bit ARGB surface and paints the Longhorn 4074
`CompositedWindow` parts into it with cairo (`frame.c`), so a compositor such as picom shows the
desktop blurred through the plate: glass frames on every window, no compositor code of our own yet.

What changed from berry:
- `frame.c`/`frame.h`: theme loader (`themes/<name>/theme.ini` + PNGs), 9-slice painter for the plate
  (active/inactive halves of LGFRAME), the edge overlay, the four-state caption button sprites with the
  blue/orange glow plates, caption text with TextGlow (pangocairo), and the hit tester (buttons,
  caption, edges, corners).
- geometry: asymmetric frame margins from the theme (`margins()`), replacing berry's border/title
  arithmetic; `_NET_FRAME_EXTENTS` follows.
- input: button hover/pressed states with glows, press-and-release-inside semantics, edge and corner
  resize with proper cursors, double-click on the caption to maximise (berry's monocle), minimise =
  berry's hide.
- cooperating clients: `_MOTIF_WM_HINTS` with decorations off, or an `_LHWM_SELF_FRAMED` property,
  means no decoration window (the glassdemo / shell programs draw their own frame).
- EWMH (see "EWMH and the shell" below): window states, stacking list, struts and work area, client
  placement; existing windows are adopted on startup.
- bug fix inherited from berry: events of extension types (>= 64) indexed past the handler table.

Build (lhde): the top-level `make` builds `build/bin/luxwm` and `build/bin/luxwmc` (luxwm/luxwm.mk; x11, xext, xfixes, xinerama,
fontconfig, xft, cairo, cairo-xlib, pangocairo and the tree's libwuxtheme; berry's configure and the `.o` symlink are gone). Run:
`luxwm [-d] [-t THEME]` (or `LHWM_THEME=THEME`), THEME a name from data/themes.ini (`aero-patch`, `slate4074`, ...), an alias
(`aero`) or a frame set; it only counts while the display has no `_LH_THEME` yet ("EWMH and the shell"). `berryc` still works as
`luxwmc` for the IPC commands; `luxwmc quit` ends luxwm cleanly. (x11: `./configure && make`, the binary `.o/lhwm`.) `tests/frametest.c` renders the painter offline
(`build/luxwm/tests/frametest`, built by make;
`./frametest THEMEDIR out.png` for the four-state strip, `./frametest THEMEDIR out.png W H TITLE FOCUSED [MAXIMIZED [HOVER [ICON.png]]]`
for one frame with the window shape applied; `FRAMETEST_EXT="l r t b [flags]"` extends it, `FRAMETEST_FADE=t` renders a frame of the M5b
maximise crossfade, `FRAMETEST_FONTS="DIR[;DIR]"` registers font directories as the registry's `fonts =` does, `FRAMETEST_SOURCE=dir|lib`
picks the PNG cache or the style through libuxtheme; THEMEDIR may also be a theme spec `DIR|STYLE.msstyles|COLOR|SIZE` or `|STYLE.msstyles|COLOR|SIZE`). `luxwm/tests/regress.sh` (`make check-luxwm`, part of `make check`) renders the
whole set (every frame set in data/frames: strip, normal, unfocused, maximised, `ext60` = `0 0 60 0`, and the Explorer case
`ext46`/`ext46u`/`ext46max` = `0 0 46 0` with `NO_TITLE`; 640x480, plus frametest's margin/shape output) and compares their SHA-256
with a baseline made on this machine (`build/luxwm/regress/baseline.sha256`, written by the first run; `--rebaseline` after an
intended change; `--baseline-from-x11 DIR` takes it from the x11 lab's own build, which proved the import byte-identical: 212
renders). No reference images are in the repository; the x11 tree's tests/ref stays in the research workspace. It skips without
the user's styles (tools/lhde-import). It renders from the source luxwm uses (libuxtheme when built in, else the PNG
caches; `--source dir|lib` forces one; the x11 script's `--compare-sources` is not carried over).

Themes: `themes/aero` (the patched 4074 Aero plates) and `themes/jade` (the shipped 4074 Jade plates, blended as build 4066 shows them),
each with its maximised plates (M5b); `theme.ini` holds the 9-slice insets, button layout, font and colours.

Known gaps (next): `_NET_WM_STATE_ABOVE`/`STICKY`/`SHADED`, `_NET_WM_ALLOWED_ACTIONS`,
per-monitor work areas in `_NET_WORKAREA` (one rect for the whole root; maximise does use the monitor's own
strut-free rect). Window icons (`_NET_WM_ICON`, `WM_HINTS`, theme default) and theme fonts: "Icons, maximised caption text, fonts".

## EWMH and the shell (milestone M2 of `SHELL-DESIGN.md`)

What `luxshell` (the taskbar/sidebar) relies on, all in `wm.c`:

- `_NET_CLIENT_LIST` in creation order (berry wrote its per-workspace focus stack; each client now has a creation
  sequence number; after a WM restart the old list's order is kept), `_NET_CLIENT_LIST_STACKING` bottom-to-top,
  taken from the real X stacking order (`XQueryTree`) after every raise/restack/map/unmap/workspace switch and
  written only when it changed.
- `_NET_WM_STATE` is written from the client's flags by `ewmh_update_state()` (only on change; atoms luxwm does not
  own, such as `SKIP_TASKBAR`, are kept): `HIDDEN` while minimised (also `WM_STATE` Iconic), `MAXIMIZED_VERT` +
  `MAXIMIZED_HORZ` while maximised, `DEMANDS_ATTENTION` (client message or the `WM_HINTS` urgency hint; cleared when
  the window gets focus), `FOCUSED` on the active window, `FULLSCREEN`, `BELOW`. States requested before mapping
  (maximised, fullscreen, below, attention) are applied at map time.
- `_NET_WM_STATE` client messages (remove/add/toggle) for `HIDDEN` (minimise/restore), `MAXIMIZED_VERT`/`HORZ`
  (handled together: maximise/restore), `DEMANDS_ATTENTION`, `BELOW`, `FULLSCREEN`; `WM_CHANGE_STATE` Iconic
  (`XIconifyWindow`) minimises.
- Minimised is its own flag (`c->minimized`), separate from berry's "hidden" (parked right of the screen, which is
  how other workspaces are hidden): a minimised window is unmapped where it is, client and frame (ICCCM IconicState; see
  "Animations"), and counts as hidden; workspace switches, config reloads and send-to-workspace leave minimised
  windows alone; focusing one (Alt+Tab, `_NET_ACTIVE_WINDOW`) restores it. Hide/show move the window without
  dropping the maximised state, and a window maximised while minimised is fitted to the work area when shown.
- `_NET_ACTIVE_WINDOW` messages switch to the window's workspace, restore, raise and focus it. When the focused
  window is minimised or closed, focus goes to the most recently focused other window that is not minimised, or
  `_NET_ACTIVE_WINDOW` becomes None. `switch_ws` no longer discards the event queue (berry's `XSync(dpy, True)`
  dropped a pager's `_NET_ACTIVE_WINDOW` sent right after `_NET_CURRENT_DESKTOP`, e.g. `xdotool windowactivate`).
- Focus and the desktop (2026-10-25): luxwm follows the real X input focus, not only the focus it sets itself. An unmanaged
  window can take the focus: flxp-desktop (`_NET_WM_WINDOW_TYPE_DESKTOP`) calls `XSetInputFocus` on itself on every click (its
  context menu then grabs), and a dock may focus itself for its menus. luxwm selects `FocusChangeMask` on the clients and the
  root; on every `FocusIn`/`FocusOut` (except `NotifyGrab`/`NotifyUngrab` and detail `NotifyPointer`) `focus_sync()` asks
  `XGetInputFocus` and walks up to the top-level: on the focused client (or a window inside it) the frame is active; on a
  window luxwm does not manage, the root, `PointerRoot` or None, `focus_set_active(false)`: the frame is repainted inactive,
  `_NET_ACTIVE_WINDOW` becomes None and `_NET_WM_STATE_FOCUSED` goes, while `f_client` stays the last focused window
  (`f_active` false; `client_active(c)` is what every paint and `FOCUSED` use). Focus inside an override-redirect top-level
  (menus, tooltips) or on another managed client changes nothing. A click on a client or its frame (`FOCUS_ON_CLICK`) focuses
  it whenever it does not really hold the focus (before, `c != f_client` alone: after a click on the desktop the window looked
  focused to luxwm and could not get the focus back). A click on the bare root (no desktop window) gives the focus to luxwm's
  `nofocus` window and makes the window inactive the same way. While inactive: Alt+Tab (`cycle`) first reactivates the last
  focused window, the window key actions (close, kill, maximise, minimise) do nothing (Alt+F4 on the desktop closes nothing),
  and closing or minimising that window from elsewhere moves "last focused" on without taking the focus from the desktop.
  Test stand-in for the desktop: `tests/desktopwin.c` (ARGB desktop window that focuses itself on a click and pops a grabbing
  override-redirect menu on a right click).
- Docks: unmanaged windows (`MANAGE_DOCK` is false) are tracked from `MapNotify` on the root and the unmanaged-type
  path of `manage_new_window` (which also covers windows adopted at startup), with `PropertyChangeMask` on them;
  `UnmapNotify`/`DestroyNotify` drop them. Their `_NET_WM_STRUT_PARTIAL` (fallback `_NET_WM_STRUT`, whole edge)
  gives the work area: `strut_area()` cuts every strut that overlaps a rectangle out of it (then the configured
  gaps). Per edge the work area takes the largest strut, never the sum: struts are distances from the screen edge, so a
  block stacked above the taskbar already includes the taskbar's height in its own strut (luxshell's pinned sidebar above the
  classic taskbar: strut = block height + the bar's height over the sidebar's columns, beside the bar's own over 0..1279). A partial
  strut that covers only part of the edge still cuts the whole rect (EWMH: the work area excludes every strut).
  `tests/strutdock.c` maps a dock with a bottom strut for tests (2026-10-25, 1280x800: taskbar 30 alone: 1280x770;
  + block strut 200 over 1131..1279: 1280x600; + block strut 170 overlapping the taskbar: 1280x630; + strut 15: 1280x770). `_NET_WORKAREA` is that rect over the root, once per desktop (all equal). Dock-type windows are raised
  above the clients after every restack unless they carry `_NET_WM_STATE_BELOW` (luxshell's sidebar does). `docks_raise()` raises them as a group
  in the order the server has them (`XQueryTree`), not in map order, so a dock orders itself against another dock with
  `XRaiseWindow`/`XLowerWindow`/a `ConfigureRequest` with `stack_mode` (applied with `XConfigureWindow`, then the group raise);
  `tests/dockorder.c` (raise|lower|above|below) checks it. The exception is EWMH's: the focused window, when it is fullscreen,
  goes above the docks at the end of every group raise (and when it gets the focus or enters fullscreen); when it loses the focus
  or leaves fullscreen the next group raise puts the docks back on top. A dock that raises itself (luxshell's bars do on every
  client-list change) goes through a `ConfigureRequest`, so the rule holds whoever raises. lux's `[sidebar] stacking` switches the sidebar between
  `_NET_WM_STATE_ABOVE` (raised over the taskbar) and `_NET_WM_STATE_BELOW` (lowered under the clients, no strut) by rewriting
  the property of the mapped dock; `dock_property()` re-reads it and restacks at once, while `_NET_WORKAREA` and the refit of
  maximised windows wait until the event queue is drained (`workarea_dirty`, in `run()`), so a burst of dock changes (Win+S:
  state, strut, geometry and stacking of the sidebar plus the taskbar's width) refits the windows once, against the final struts.
- Reparenting: a managed window that another client reparents away from the root (XEmbed tray icons docked by luxshell) is released (lists, decoration, button grabs, focus; the window itself is untouched), and `MapRequest`s for windows whose parent is not the root are ignored.
- The work area of the window's monitor is used by maximise (the frame fills it exactly; msstyles themes keep only the
  caption there, the borders go), snap left/right, centring and smart placement;
  maximised windows are refitted when the work area changes (shell started/stopped, strut changed).
- New windows: dialogs (`_NET_WM_WINDOW_TYPE_DIALOG`, and transients, which EWMH treats as dialogs) are centred on
  their parent, or on the work area without one; `USPosition`, or `PPosition` other than 0,0, in `WM_NORMAL_HINTS`
  is honoured with the window gravity (NorthWest: the frame's top-left lands on the requested point, SouthEast: its
  bottom-right, Static: the client stays put); everything else gets smart placement. The frame is then clamped into
  the work area (shrunk if larger), independently of `EDGE_LOCK`. `EDGE_LOCK`, when on, also locks moves and
  resizes to the work area. Adopted windows (WM restart) keep their place (frame put back around the client),
  minimised ones are minimised again, and maximised ones get their restore geometry back from
  `_LHWM_RESTORE_GEOMETRY` (written on maximise).
- `ConfigureNotify` of root children no longer re-queries the monitors (only the root's own); a `ConfigureRequest`
  from a hidden window keeps it off screen (size only).
- Session theme: root `_LH_THEME` (UTF8_STRING) is a theme name liblhde resolves (docs/THEME-FORMAT.md: the user's and the
  shipped theme files, aliases, bare styles; the current layer). luxwm resolves it itself (`lh_theme_apply`: the frame set,
  the classic frames, the style, the fonts, the composition-off rule, the animations) and follows a change live (below,
  "Maximised mode": runtime theme switching; `_LH_SETTINGS_CHANGED` and a republished `_LH_THEME_RESOLVED` apply the record
  again when it differs). At startup the root's value wins over `-t`/`LHWM_THEME`, which luxwm publishes only when
  `_LH_THEME` is missing; an alias written to `_LH_THEME` is replaced by the theme's name; a name that is no theme is logged
  (`luxwm: _LH_THEME NAME is no theme; keeping CURRENT`) and nothing changes.
- Start menu key (M6, `SHELL-DESIGN.md` 6.3): the key action `start` (defaults `Super_L start`, `Super_R start`, added to a
  user keys file that binds neither Super key) grabs the bare Super key; when it is released with no other key or button press
  in between, luxwm sends the client message `_LHSHELL_START` to the root (SubstructureNotify|SubstructureRedirect, data[0] =
  the key time), which luxshell answers by toggling its start menu. While Super is held the keyboard is luxwm's, so the Mod4
  bindings still arrive here; unbound Super+key combinations are swallowed. `Super_L none` in the keys file disables it.
- Default keys (2026-10-25): `Mod4+d` = show desktop, `Mod4+e` = `flxp` (falls back to `rox` when flxp is not on PATH), `Mod4+r` =
  `shell run`, `Mod4+s` = `shell sidebar-stacking toggle` (lux's sidebar under the windows ⇄ on top, instantly). A user keys file
  keeps its own bindings; `Mod4+d`, `Mod4+r` and `Mod4+s` are added to it only where that key is unbound there. Actions:
  `showdesktop`, `sysmenu` (the window menu, default Alt+Space, added to a user keys file where unbound: "The window menu"), `shell CMD` (sets the root property `_LHSHELL_COMMAND` = CMD, UTF8_STRING; luxshell's runtime.lua accepts `run` =
  lux's own Run box, `sidebar-stacking toggle|top|bottom`, `taskbar KIND`, `sidebar-with-classic 0|1`, `settings`, `desktop N|next|prev` = switch the virtual desktop, N 0-based, through the EWMH `_NET_CURRENT_DESKTOP` request). Every `exec` child does `chdir($HOME)` first (luxwm itself runs in its own directory).
  **Show desktop** (2026-10-27: Windows' Win+D) = EWMH `_NET_SHOWING_DESKTOP` (in `_NET_SUPPORTED`, root CARDINAL, 0 at start,
  deleted on quit). On (the key, or a `_NET_SHOWING_DESKTOP` client message with data[0] = 1 from a pager or taskbar): the visible
  windows of the current desktop are really minimised (iconic, task buttons stay; bottom first, so the focus is not handed down the
  stack) and luxwm remembers their ids, their order and the active one; they stay minimised. Off (the key again, or data[0] = 0)
  restores exactly that set in its old stacking order and refocuses the old active window, but only while nothing has changed in
  between: a window restored (taskbar click, `_NET_WM_STATE`, Alt+Tab), activated (`_NET_ACTIVE_WINDOW`), newly mapped (MapRequest)
  or closed/withdrawn ends the mode on the spot without restoring anything (`showing_changed()`; `_NET_SHOWING_DESKTOP` goes to 0),
  so the next Win+D is a new "show desktop" that minimises whatever is visible then. A desktop switch ends the mode the same way.
  Both directions are instant, without any per-window animation (2026-10-27): the whole set is unmapped / mapped in one batch under one
  `XGrabServer`, `anim_classic` (XP caption flight) is skipped (`sd_instant`), and with picom `_LHWM_ANIMATE` is deleted on the clients and
  frames of the set for the transition and put back 400 ms later (`anim_until`, `anim_expire`), so no picom hide/show rule matches. A single
  window's minimise (button, Super+Down) keeps its animation.
  `_NET_SHOWING_DESKTOP` is 1 exactly while the remembered set is minimised and unchanged. Until 2026-10-27 an activation or a new
  window restored the whole set ("only minimised temporarily"). `showing_desktop()`. Lab (`:3`, xterms A, B, C, xdotool):
  `key super+d` twice: `C A B` (C active) -> all minimised, 1 -> `C A B`, C active, 0; Win+D, `windowactivate A`, Win+D, Win+D: A
  alone comes back and is the only one minimised and restored by the second pair (C and B stay minimised, the property 1 / 0 / 1 / 0);
  Win+D, a new xterm D mapped (0 at once), Win+D minimises only D, Win+D brings back only D; Win+D, D closed (0), Win+D with nothing
  visible does nothing.
- **Alt+Tab** (2026-10-27, as Windows'; key action `cycle`, defaults `Mod1+Tab`, `Mod4+Tab` and their `Shift+` forms, the Shift ones
  added to a user keys file where unbound; `cycle_run()`): the press grabs the keyboard until the binding's modifier (Alt, Super) is
  released. Candidates: the current desktop's windows in stacking order (top first), then its minimised ones. The first Tab selects
  the window under the active one (Shift+Tab the last candidate; from the desktop, i.e. nothing active, the top window); each further
  Tab / Shift+Tab steps forward / back, wrapping. The selected window is previewed: client and frame raised over the other windows,
  without focus (frames keep their active/inactive look), and the previously previewed one goes back to its original place (the
  desktop's stacking from before the first Tab, taken with `XQueryTree`, is restacked and the candidate put on top of it). A minimised
  candidate is not shown (the original order stays). Releasing the modifier restores the original stacking, then focuses the
  candidate as any activation does (restored if minimised, put on top, raised), so every other window keeps its place: `C B A` (C
  active), Alt+Tab+Tab = `A C B`, not `A B C`. Esc while the modifier is held puts the original stacking back and leaves the focus
  where it was. A modifier already released when the grab starts (xdotool `key alt+Tab`) makes it one step. Other events (Expose,
  maps, property changes) are handled meanwhile; a candidate that goes away is skipped. Lab (`:3`, xdotool `keydown alt`, `key Tab`,
  `keyup alt`, `xwininfo -root -tree` order, `getwindowfocus`): `C B A`/C -> 1 Tab `B C A`/C -> 2 Tab `A C B`/C -> release `A C B`/A;
  Tab + Esc: `C A B`/A during the preview, `A C B`/A after; Shift+Tab: `B A C`/A, release `B A C`/B; `key alt+Tab`: `A B C`/A; with C
  minimised (`A B C(min)`): Tab `B A C`, Tab (C, not shown) `A B C`, release: C restored, `C A B`/C. Super+Tab the same (`xdotool keydown
  super keydown Tab keyup Tab`, a second Tab, `keyup super`: `B A C` -> preview `A B C`, `B A C`... -> `C B A`/C); the Super release
  ends the cycle and sends no start menu, a bare Super afterwards does (`_LHSHELL_START`). (A separate `xdotool key Tab` while
  `keydown super` is held releases Super first: one step.)
- **Restack requests** (2026-10-27, `_NET_RESTACK_WINDOW` in `_NET_SUPPORTED`; CLIENT-PROTOCOL.md "Requests to the wux side", the
  drag-and-drop raise): a client message on a managed client window, data.l[0] source indication, l[1] sibling, l[2] detail.
  `client_restack()` moves the window in luxwm's stack list and restacks client and frame together (`client_raise` of the top window),
  without touching the focus or `_NET_ACTIVE_WINDOW`: Above with no sibling raises to the top, Below with no sibling lowers to the
  bottom of the managed windows, with a managed sibling on the same desktop directly above / below it; TopIf/BottomIf are taken as
  Above/Below (no occlusion test), Opposite as Above unless the window is on top already. Minimised windows, windows of other
  desktops, BELOW windows and unknown siblings are ignored (logged with `-d`). Client messages are processed whatever pointer grab
  another client holds, so a drag source (flxp's XDND drag, which keeps its active pointer grab) can raise the window under the pointer
  after its ~500 ms hover. Lab (`:3`, xterms, A focused; a python-xlib client holding an active pointer grab on the root while it sends
  source 2, Above, no sibling for B): `A C B` -> `B A C`, focus and `_NET_ACTIVE_WINDOW` stay A; Below/no sibling `A C B`; B Above C
  `A B C`; B Below A `A B C`.
  Flxp has no way to open its Run dialog from outside (only the start panel item); it would need e.g. `flxp-taskbar --run` or
  a `_FLXP_COMMAND run` root message, then `Mod4+r exec flxp-taskbar --run` here.
- Clean exit (`luxwmc quit`, berry's `quit` IPC command; luxshell's default "log off" outside the tty8 session, 6.2): the frames
  are destroyed, clients stay mapped where they are, windows parked off screen on other workspaces are moved back, and
  minimised windows stay unmapped with WM_STATE Iconic (berry used to set every client Withdrawn on exit, so a restarted
  luxwm lost the minimised ones; now they are adopted minimised again). Deleted from the root: `_NET_SUPPORTED`,
  `_NET_SHOWING_DESKTOP`, `_NET_SUPPORTING_WM_CHECK`, `_NET_WORKAREA`, `_LHWM_THEME`, `_LHWM_COMPOSITED`, `_LHWM_FRAME_METRICS`,
  `_LHWM_MAXIMIZED_MODE` and berry's status properties; kept: `_LH_THEME` (the session's) and
  `_NET_CLIENT_LIST`, `_NET_CLIENT_LIST_STACKING`, `_NET_ACTIVE_WINDOW` (a restarted luxwm restores order and focus from
  them). Exit status 0, "luxwm: clean exit" on stderr. Tested on the lab: three clients stay viewable at their positions, a
  minimised xterm keeps WM_STATE Iconic and comes back minimised after `lab.sh restart-wm`.

## The window menu (2026-10-26, `menu.c`, `menu.h`)

XP's system menu, drawn by luxwm. **Opened by** Alt+Space (key action `sysmenu`, default `Mod1+space sysmenu`; added to a user keys
file where Alt+Space is unbound there), a right click on the caption (on the button release, like `WM_CONTEXTMENU`, at the pointer;
it flips left/up at the monitor edge) and a click on the caption icon (`frame_icon_rect()`, new in frame.c and used by nothing that
paints; a double click on the icon, the second press within 500 ms, closes the window as XP's). Alt+Space and the icon put it under
the caption at the frame's left border. Items, mnemonics and greying as `mnsys.c` `xxxSetSysMenu`: **R**estore (greyed unless
maximised), **M**ove, **S**ize (greyed while maximised; Size also for a window whose size hints fix its size), Mi**n**imize,
Ma**x**imize (greyed while maximised), Move to **D**esktop (luxwm's own, below: "Virtual desktops"), separator, **C**lose (the
default item, bold) with `Alt+F4` right-aligned. Opened from the
keyboard the first item is selected (greyed or not, as `MNFindNextValidItem`, which skips only separators) and the mnemonics are
underlined (XP's keyboard cues; a key press shows them in a mouse-opened menu too). Keys: Up/Down/Tab (wrapping, separators skipped),
Home/End, Enter (on a greyed item nothing happens), a mnemonic (the only item with it is chosen at once; a greyed one is only
selected; several: the next one), Esc, Alt or F10 close it. Mouse: hover selects (greyed items too, as XP), a release over an enabled
item chooses it (also the release of the button that opened it, dragged onto an item), a click outside closes it (the click is not
passed on). Modal like the drags: pointer and keyboard grabbed, Expose/MapRequest/ConfigureRequest of other windows handled meanwhile.
**Submenus** (`item.sub`) cascade as XP's: a 4 x 7 arrow in the right margin; hovering the item for MenuShowDelay (400 ms), a click on
it, Right, Enter or its mnemonic opens the submenu beside the item (3 px overlap, left of the parent when it does not fit); Left or
Esc closes only the submenu, the pointer back over another item of the parent (or a press there) closes it and the parent takes
over, a click outside both, Alt or F10 closes everything. The submenu shares the parent's keyboard grab and hands the pointer grab
back when it closes.

**Rendering** (`wmmenu_popup`, an override-redirect ARGB window with `_NET_WM_WINDOW_TYPE_POPUP_MENU`, `_LHWM_MENU` and
WM_CLASS `lhwm-menu`), from the frame theme's visual style through libuxtheme:
- styles with a `Menu` class (`POPUPITEM` present: the 4074 Slate, Jade and Aero-patch styles, also used for the DWM themes aero and
  jade; Vista's aero5048/aero5112): `POPUPBORDERS` over the whole menu (else `POPUPBACKGROUND` and a 1 px BTNSHADOW line),
  `POPUPGUTTER` when the style has one (Vista), `POPUPITEM` in state 2 (hot) / 4 (disabled hot) under the selected item, `SEPARATOR` /
  `POPUPSEPARATOR` at its true height plus its content margins, the text colours of `POPUPITEM` states 1-4 (TEXTCOLOR), item height =
  text height + `POPUPITEM` content margins, Vista's `SYSTEMRESTORE`/`MINIMIZE`/`MAXIMIZE`/`CLOSE` glyphs in the gutter (the 4074
  styles have none, and the 4074 menus show none);
- styles without one (XP's Luna, Royale, Zune, Noir, Embedded): XP's own drawing (`mndraw.c`) with the style's `[SysMetrics]`
  colours over XP's Windows Standard scheme: FLATMENUS = 1 px BTNSHADOW outline, MENU inside, MENUHILIGHT bar framed in HIGHLIGHT
  with HIGHLIGHTTEXT, greyed text GRAYTEXT; without FLATMENUS the raised 3D edge, a HIGHLIGHT bar and embossed greyed text on a
  BTNFACE menu; the separator a BDR_SUNKENOUTER etch (BTNSHADOW over BTNHIGHLIGHT) at the middle of a 9 px item; the system menu
  bitmaps (XP's Marlett glyphs for Restore, Minimize, Maximize, Close) drawn in the 13 px check column in the text colour;
- the font is `[SysMetrics] MENUFONT` (Slate/Luna Tahoma 8 = 11 px, Jade/Aero Segoe UI 9 = 12 px; `Quality:ClearType-Natural`: no
  hinting); item height and baseline use GDI's tmHeight / tmAscent (unhinted pango metrics rounded), as frame.c's captions;
- without the library: the DWM themes use the 4074 Jade values (menu #f4f4f4, border #4e5358, text #00025c, highlight #c5e7f6, the
  theme.ini `font`), msstyles themes XP's Windows Standard classic menu with Tahoma 11 px.
Geometry (XP's `mncomput.c`, and measured on the 4074 Slate menu of `lh4074-195235.png`: identical): 1 px border + 2 px inside, text
19 px into the item (22 px from the border: MNLEFTMARGIN + the 13 px check column + MNXSPACE), separator 8 px (Slate's part) / 9 px
(XP), accelerator right-aligned 17 px before the inner edge. The drop shadow is XP's 4 px right/bottom shadow (alpha 0.44, 0.33,
0.17, 0.055 outwards, starting 5 px in, as measured on the 4074 menu): real alpha with a compositor (`picom-glass.conf` excludes
`_LHWM_MENU@` from picom's own shadow and blur), otherwise blended over a copy of the screen taken before the map, as XP does.
Measured against the 4074 Slate menu: border, plate, highlight and separator colours, item pitch (19), text start (x 22) and text
rows (cap height 6..13) are identical; the glyph advances differ by up to 5 px over a word (FreeType vs GDI ClearType, as in the
captions) and the reference's ClearType fringes are BGR-ordered while luxwm renders RGB like its captions.
`compare_wmmenu.png`: the 4074 Slate reference | luxwm on slate4074 (Size selected with the keyboard) | jade4074 | luna-xp | flxp's
classic menus for the classic layout. `tests/menutest.c` renders the menu offline (`cc -I.. $(../tools/uxtheme-flags.sh | sed -n 1p)
menutest.c ../frame.c ../frame_ut.c ../menu.c -o menutest $(pkg-config --cflags --libs cairo cairo-xlib pangocairo x11 fontconfig)
-lXext -lm $(../tools/uxtheme-flags.sh | sed -n 2p)`; `./menutest THEMESPEC out.png [SEL [MAXIMIZED [KEYBOARD]]]`, `MENUTEST_FONTS`).

**API** (`menu.h`, meant for lux-style context menus too): `struct wmmenu_item { text ("&Move"), accel, flags (WMMENU_DISABLED,
SEPARATOR, DEFAULT, CHECKED), glyph, id, cb(arg, id), arg }`; `wmmenu_popup(env, x, y, items, n, flags, button)` runs the menu
(flags `WMMENU_KEYBOARD`, `WMMENU_FLIP`; `button`: the button still held) and returns the chosen index after calling its `cb`, or
`WMMENU_CANCEL` / `WMMENU_DBLCLICK`; `struct wmmenu_env` carries the display, ARGB visual, theme, compositor flag, the monitor
rectangle, a dispatcher for other windows' events and the double-click rectangle. wm.c's `wmmenu_open(client, x, y, flags, button,
from_icon)` builds the system menu; `sysmenu_command()` is its callback (it looks the window up again: a window that went away
while the menu was open is left alone).

**Keyboard Move and Size** (`drag_run(..., keyboard)`, also `_NET_WM_MOVERESIZE` directions 10 and 9), as `movesize.c`: the pointer
goes to the middle of the caption (Move) or of the window (Size), cursor fleur. Move: arrows move by half a caption button
(`max(SM_CXSIZE / 2, 1)`: msstyles CAPTIONBARHEIGHT / 2, DWM caption height / 2 = 13 px), Ctrl+arrow 1 px. Size starts with no edge
(WMSZ_KEYSIZE): the first Left/Right picks the left/right edge, the first Up/Down the top/bottom edge (two of them a corner) and
puts the pointer on it without changing anything; further arrows move it by the same step (at least one size-hint increment).
Two arrows held together (Right and Down) step diagonally, in Move as in Size (luxwm's own; X repeats only the last key held).
**The mouse in Size mode** (luxwm's own, not Windows'): the pointer picks the edges it moves towards once it is 12 px (3 x
`DRAG_THRESHOLD`) from where the mode started, both edges of the corner when the move is within about 27 degrees of the diagonal
(the shorter axis at least half the longer), one edge otherwise; a pointer that reaches a border or corner of the frame picks that
one. The pointer is then put on the picked edge (corner) and the edge follows it. **Shift** toggles between the edge and the
corner next to it (the end nearer the pointer) and back to the edge picked first, putting the pointer on the new edge or corner;
the size carries on from there. In both modes mouse motion takes over;
Enter accepts and puts the pointer back where it was before the mode (ptRestore), a click accepts and leaves it, Esc (or a
`_NET_WM_MOVERESIZE` cancel) puts the window back to its original rectangle (and the pointer back). Size hints, sync requests and the
flicker-free resize path are the mouse drag's. Lab (`:3`, aero): Alt+Space, M, Right x3, Down x2, Enter: +39/+26, pointer restored;
Alt+Space, S, Right x3, Down x2, Enter on an xterm (inc 6x13): +24/+13; Left/Up corner + Esc: unchanged; Size, mouse +15/+1: right edge, pointer on it; Shift: bottom-right corner,
pointer there, +30/+40 grows both (+30/+39); Shift: the right edge again, +20/+50 widens only; Size, mouse +14/+10: the corner.

## Snap (from Windows 7, not Longhorn)

Windows 7's Aero Snap, added because aap asked for it; neither XP nor the Longhorn builds had it. A **move** drag (the caption, the
move modifier with its button, `_NET_WM_MOVERESIZE` 8) whose pointer reaches the left or right edge of the work area (the window's
monitor minus struts, as maximise uses) snaps the window to that half of the work area; one that reaches the top edge maximises it.
A **resize** drag of the top or bottom border whose pointer reaches that edge of the work area stretches the window to the work
area's full height, keeping its columns (7's vertical maximise; corners do not). The pointer has to touch the edge, not the frame,
so an ordinary drag that pushes the frame against or past an edge does not snap; and nothing happens until the button is released:
while the pointer is at an edge an outline of the target rectangle is shown (4 px of XP's checkered drag-rectangle pattern, an
override-redirect window `lhwm-snap`: luxwm's choice, 7 drew a glass sheet), and moving away from the edge before the release drops
it. Escape cancels the drag and the snap. Keyboard Move and Size do not snap.

A snapped window (`c->snap`, `_NET_WM_STATE_MAXIMIZED_VERT` while it spans the full height) remembers the rectangle it had before
(`c->snap_prev`); a maximised one its restore rectangle as always. **Dragging either away** by the caption gives it that size back at
once, placed under the pointer at the same fraction of its width (as 7), and the drag continues; dropping it at the top again
maximises with the pre-snap rectangle as the restore geometry. A resize that changes a snapped window's height ends the snap; a
width-only resize keeps it. `luxwmc snap_left`, `snap_right` (IPC) go through the same code.
Lab (`:17`, slate4074, xterm 400x238 at 300,200): drag to x 0 → left half (636 px frame, size hints), `MAXIMIZED_VERT`; dragged away →
400x238 again under the pointer; to y 0 → maximised; dragged away → 400x238; to x 1279 and back before the release → no snap;
bottom border to y 799 → full height at the same x; dragged away → 400x238.

## Virtual desktops

Ten desktops (`_NET_NUMBER_OF_DESKTOPS`), switched by the key action `workspace N` (defaults Mod4+1..4 = desktops 0..3), by
`_NET_CURRENT_DESKTOP` requests (luxshell, any pager) and `_LHSHELL_COMMAND desktop`. A window changes desktop by:
- **a desktop key while it is moved**: during a move (the window menu's Move, a caption or modifier drag, `_NET_WM_MOVERESIZE`
  8/10), a key bound to `workspace N` takes the window along to desktop N and switches there; the move goes on;
- **the window menu's Move to Desktop**: a submenu with Desktop 1..10 (desktop 0..9; mnemonics 1..9 and 0), the window's own one
  checked; choosing another moves the window there and switches to it;
- **`_NET_WM_DESKTOP` client messages** (EWMH, data.l[0] = the desktop): the window moves there and the current desktop stays, as a
  pager expects; 0xFFFFFFFF (all desktops) is not supported and ignored.
None of this is Windows': XP had no virtual desktops, Longhorn neither. Lab (`:17`): Alt+Space, D, 3 → the window and the session
on desktop 2; Alt+Space, M, Right, Super+1, Enter → both back on 0; `xdotool set_desktop_for_window W 4` → the window on 4, the
current desktop still 0; a caption drag with Super+1 pressed on desktop 4 → window and session on 0; the submenu by mouse: open
after the hover delay, closed when the pointer goes back to Move, a click on Desktop 2 moves the window.

## Live-editing a theme (2026-10-26)

`luxwmc reload-theme` (or the root ClientMessage `_LHWM_RELOAD`, or any write of a root property `_LHWM_RELOAD`, or `_LH_THEME` set
to the name already in use, which used to be a no-op) makes luxwm re-read the current theme in place (`theme_reload()`): the
`_LH_THEME` entry (or preset) is looked up again, then `theme_select()` loads the same spec with `theme_force` set, i.e. theme.ini and
its PNGs, or the `.msstyles` through libuxtheme, anew; every client is rethemed as on a switch (`client_retheme`: the client
rectangle is kept and the frame placed around it with the new margins, `_NET_FRAME_EXTENTS`, restore geometry and maximised frames
follow), frames and header pixmaps are repainted (new theme generation), `_LHWM_FRAME_METRICS` and `_LHWM_COMPOSITED` are announced
again. Log: `luxwm: theme reloaded (_LHWM_RELOAD message|_LHWM_RELOAD property|_LH_THEME set to the theme in use): SPEC`; a theme that
does not load any more keeps the running one. luxwm's own alias rewrite of `_LH_THEME` is counted (`lh_theme_selfwrite`) so it does
not come back as a reload. lux has the same for its side (`_LHSHELL_COMMAND reload`); `.session/theme-reload.sh` sends both and
`.session/theme-watch.sh` runs it on every save (`../USAGE.md` "Live-editing a theme"). Lab (`:3`, jade4074): `margins = 27 9 9 9` ->
`40 9 9 9` and `title_x` 28 -> 60 in `themes/jade/theme.ini`, `luxwmc reload-theme`: the xterm kept 319,248 490x264, its
`_NET_FRAME_EXTENTS` became `9, 9, 40, 9` and the caption grew; restoring the file and setting `_LH_THEME` to `jade4074` again
brought `9, 9, 27, 9` back; `xprop -root -f _LHWM_RELOAD 32c -set _LHWM_RELOAD 1` reloads too; slate4074 (msstyles through the
library) reloads the same way. `tests/regress.sh`: 160 identical (frame.c only gained `frame_icon_rect`).

## Window shape (both modes)

`frame_paint()` paints into an image surface, copies it to the decoration window and derives an X Shape
bounding region from its alpha: in the top and bottom 24 rows, leading/trailing pixels with alpha below
`shape_alpha` (theme.ini; default 32 for DWM themes, 1 for msstyles) are cut out. This fixed the "pixels sticking
out" at the DWM corners: the plate is rounded (its corner pixels have alpha 0..0x30), but the window was a full
rectangle, so picom blurred (and shadowed) the whole rectangle and the square corner showed as a blurred notch
around the rounded highlight (`compare_dwm_corners.png`: real 4074 DWM | before | after, 16x, over a test
checkerboard). The texture slicing itself was exact (corners are 1:1 at integer offsets). Without a compositor the
shape is what makes the rounded caption corners of the classic themes transparent at all.

### Pill and mesh slicing

The `ChamferBox01` mesh in `desksrv.dll` (D3DXSTREAM 101) has no `MeshTextureCoords` at all (168 vertices, x/y in
+-15, front face flat at +-14.505, a 0.495-unit chamfer rim), so it gives no UV information; the 9-slice stays
measured from the plate texture. In `plate_active.png` the pill is a low-alpha well at x=701..793, y=2..14, ending
at the dark line x=792 (the highlight line is x=799, 7 px further). The 1:1 right 100 px of `plate_insets` keep it
unstretched. The gap came from `button_right = 8`: the original (4074-dwm.png, Documents window) ends the button
sprites 5 px before the frame edge (sprite col 41, the bright end line, sits 7 px from it; the dark line of the
texture lies 9 px from it). aero uses `button_right = 5`; jade has `button_right = 6`, `button_gap = 0` since 2026-10-24 (fitted to
4066, "Glass accuracy"). In the original the dividers of
min/max/close are 27/27/42 px apart (-104/-77/-50/-8 from the frame's right edge, measured on the bright line at w-2); our sprites
are 25/25/40, so each cell is widened with `button_gap = 2` (default 0): sprite cols 0..3 stay at the cell's left, col 4 is
repeated over the gap, the rest sits at the right end; `button_rect` (draw and `frame_hit`) uses the cell. See `compare_pill2.png`.
The Explorer plate (STREAM 484, below) has its pill at exactly the same place (x=701..793 at rows 2..5, narrowing to 787..792 at
row 11; checked row by row against 483), so the same 100 px right inset and button layout apply.
Re-verified 2026-10-27 (after `cell_paint`'s slice-edge fix): frametest at 500, 1000 and 1280 px wide and 607 high (the texture's
height, so every row is 1:1) with the plate alone (copies of `themes/aero` and `themes/aerovision` with `glass_alpha_mode = linear`,
`overlay = 0`, `shape_alpha = 0`, buttons pushed off the frame), active and inactive, lib and dir source: the right 100 columns equal
the texture's columns 701..800 exactly, **0 differing pixels** in all 24 renders (a 1-column offset gives ~35000); with the shipped
theme.ini (squared glass, overlay, buttons, title) the right 100 columns of 500/1000/1280 px wide frames are identical to each other
(0 differ), for aero and aerovision.

## Classic msstyles mode (`mode = msstyles`)

The non-composited Longhorn/XP look, drawn the way uxtheme's nctheme.cpp does it (see docs/history/CLASSIC-DECO-NOTES.md),
from a decompiled visual style. Since 2026-10-24 the style itself is read live through libuxtheme when luxwm has it ("libuxtheme source"
below); the generated directories are then caches (the fallback without the library, and the tuning keys). Themes are generated, not
hand-made:

    tools/msstyles2theme.py ../../res/4074/slate_longhorn.ini ../../res/4074/slate.msstyles themes/slate4074-classic
    tools/msstyles2theme.py ../../res/4074/jade_longhorn.ini  ../../res/4074/jade.msstyles  themes/jade4074-classic
    tools/msstyles2theme.py ../../res/4051/slate_longhorn.ini ../../res/4051/slate.msstyles themes/slate4051-classic

The converter also reads a `.msstyles` file directly, XP format (PACKTHEM_VERSION 3: `THEMES_INI` + `<VARIANT>_INI` text
resources, bitmaps named after their files) and Longhorn-compiled (version 4: `VMAP` + `VARIANT` decompiled with the style's own
`STBL01` schema, as `../../tools/lhstyle.py` does); `--color`/`--size` pick the variant. Checked: the 4074 and 4051 Slate styles
converted from the PE files give exactly the shipped `slate4074-classic`/`slate4051-classic` directories (only the comment line
naming the source differs). See "XP visual styles (Luna)" below.

Run with `LHWM_THEME=themes/slate4074-classic` (lab: `LAB_THEME=slate4074-classic`). Screenshots:
`screenshot_classic_slate.png`, `screenshot_classic_jade.png`; reference | ours crops (offline renders of the same
window size composited over the reference screenshot): `compare_classic_slate.png` (4074 Slate Task Manager,
inactive, and the active "Active Window" sample), `compare_classic_jade.png` (4074 Jade, same),
`compare_classic_slate4051.png` (4051 Explorer: caption buttons and bottom corners).

The converter resolves properties like uxtheme (state section, part section, `[Window]`, `[globals]`) and splits every
IMAGEFILE strip into `<part>_<state>.png` (`<part>@<n>_<state>.png` for IMAGEFILE1..5, which for IMAGEGLYPH buttons
are the glyphs). Bitmaps: a 32-bit DIB with any alpha outside {0,255} is premultiplied (un-premultiplied for PNG); one
with both 0 and 255 uses its alpha as a mask (Longhorn: the 4074 glyph strips are black where alpha is 0 and the
caption corners are alpha 0; XP's rule of ignoring such alpha would draw black boxes); otherwise TRANSPARENT parts
key out TRANSPARENTCOLOR (magenta).

theme.ini keys (all written by the converter):
- `mode = msstyles`; `border_width` ([sysmetrics] SIZINGBORDERWIDTH, system default 1 when absent);
  `caption_height` (CAPTIONBARHEIGHT); `small_caption_height`; `caption_font`, `small_caption_font` (pango
  description; Windows' lfHeight = -MulDiv(pt, 96, 72) px, so Tahoma 8 = 11px, Segoe UI 9 = 12px, with a fallback
  family); `caption_text`, `caption_text_inactive` ([sysmetrics] CAPTIONTEXT / INACTIVECAPTIONTEXT);
  `text_antialias` (subpixel|gray|none), `text_hinting` (full|medium|slight|none; none for Quality:ClearType-Natural),
  optional `text_dy` (baseline correction), `shape_alpha`.
- per part (`caption maxcaption smallcaption frameleft frameright framebottom close max min restore help smallclose`):
  `.images` (IMAGECOUNT), `.variants` (`@n:WxH` single-image size of IMAGEFILE / IMAGEFILEn),
  `.sizing_margins` and `.content_margins` (left right top bottom), `.caption_margins`, `.sizing` (stretch|tile),
  `.transparent`, `.content_alignment`, `.shadow.<1 active|2 inactive> = none|single|continuous dx dy #rrggbb`.

Geometry and drawing (frame.c `ms_paint`, `ms_btn_rect`, `theme_margins_ex`):
- cnBorders = 3 + border_width on all sides (4 px for these styles), caption = cnBorders + caption_height + 1
  (24 px); CAPTION over the full width, FRAMELEFT/RIGHT (cnBorders wide, from the caption to the bottom frame) and
  FRAMEBOTTOM (full width) stretched into their rects; frame state 1 active / 2 inactive.
- Nine-grid exactly as ninegrid2.cpp: 32.32 fixed-point nearest neighbour starting half a step in, corners 1:1 while
  a middle remains, otherwise the corners share the size in proportion to their margins (the 29 px caption bitmap
  with margins 20/8 is shrunk into 24 rows that way).
- Buttons: (MulDiv(caption_height, close.w, close.h) - 4) x (caption_height - 4) = 15x15, close 2 px left of the
  right border, then max/restore and min 2 px apart, top = cnBorders + 2. Strip index = framestate * (images / 2) +
  {normal, hot, pushed, disabled}: the Longhorn 10-image strips have 5 per frame state (active 1-5, inactive 6-10; the
  5th, a deep red/blue "default" look, is unused - nothing in nctheme selects it), 4051's 8-image strips are XP's
  layout. The glyph is the largest IMAGEFILEn fitting the CONTENTMARGINS rect (9x9 here), true size, centred.
- Icon (the client's `_NET_WM_ICON` or `WM_HINTS` pixmap, else the theme's `default_icon`, XP's application icon): 16x16 at cnBorders + 2; caption text from icon.right (or cnBorders when
  there is no icon) + CAPTIONMARGINS.left to the first button - CAPTIONMARGINS.right, end-ellipsised; top =
  cnBorders + (caption_height + 1 - tmHeight) / 2 with GDI's tmAscent/tmDescent (unhinted metrics rounded), baseline
  tmAscent below. TEXTSHADOWTYPE CONTINUOUS is comctl32's DrawShadowText reimplemented: every touched pixel of the
  text counts as 255, a 4-wide box to the right, then the row and four rows above (bottom-up DIB) / 4, blended at
  (-4 + dx, -8 + dy) from a buffer with the text at (5,5). SINGLE is one offset copy.
- Maximised (berry's monocle, full screen): MAXCAPTION, no side/bottom frames, the borders lie off-screen: frame
  margins become 0/0/0 and caption_height + 1 (20 px), buttons and text keep their offsets (2 px from the screen
  edge). `_NET_FRAME_EXTENTS` follows; moving/resizing a maximised window restores the normal margins.
  The MAXCAPTION bitmap is nine-gridded into exactly the visible band, `w x (caption_height + 1)` at 0,0: nctheme.cpp's
  `NcPaintCaption` insets the background rect by cnBorders on top, left and right when `fFullMaxed` (and `CreateCompositeRgn`
  does the same for the region). Until 2026-10-24 luxwm drew it over the whole off-screen caption rect (`-cnBorders`, `w + 2
  cnBorders` wide, `cnBorders + caption_height + 1` high), so the bitmap's edge rows and columns, which the MAXCAPTION
  bitmaps carry at their outer edge (Slate: dark outline + highlight in rows/columns 0..1; Luna Silver's MAXCAPTION is 26 rows,
  exactly caption_height + 1), were pushed off-screen or squeezed and its inner line sat inset (flxp report). No maximised msstyles
  window exists in `../../screenshots/`, so this follows the uxtheme source. frametest: the maximised renders of the three
  classic themes change, nothing else.
- Window shape: the caption's alpha-0 corners, taken 1:1 from the caption bitmap (uxtheme keeps the margin areas of
  the region unscaled; the drawn pixels come from the shrunk image, so row 3 of the 4074 corner differs - matches
  the screenshots). Frames are opaque; the client area is left transparent (covered by the client), so the mode looks
  the same with and without picom.
- frame_hit: buttons exactly their rects, borders cnBorders wide, corners one caption height along each edge.

Measured against the references: frame borders, caption bitmaps, caption corners and all caption buttons are
pixel-identical (4074 Slate Task Manager: 0 of 4928 border pixels and 0 button pixels differ; 4074 Jade Task
Manager the same; 4051 Explorer borders, bottom corners and buttons: 0 differ). Remaining differences, all in the
caption text:
- glyph rasterisation: FreeType + cairo subpixel AA versus GDI ClearType; same font files (Tahoma), same positions,
  but individual stems differ by a fraction of a pixel and ClearType's colour fringes are stronger; occasionally an
  advance width rounds differently (+-1 px after a few words).
- Jade asks for Segoe UI 9. Until 2026-10-24 it was not available here and the `Segoe UI,Tahoma 12px` fallback (Tahoma) was taken
  to match the screenshots; but 4074 ships Segoe UI 0.7 (`WINDOWS/Fonts/seguibk.ttf`), which the registry's `fonts =` now loads, and
  the Jade Task Manager caption fits it better than Tahoma (`docs/history/THEMES.md` "Fonts", `compare_fonts.png`). The pixel counts above were
  measured with Tahoma.
- The Display Properties "Active Window" sample is not a real window: its text sits 1 px further right (and 1 px
  higher for Jade) and a light band lies under its caption; real windows (Task Manager) match exactly in position.
- No icon from the client: XP draws the default application icon and indents the text by it; since 2026-10-24 luxwm does the same
  with the theme's `default_icon` (shell32 #3); a theme without one starts the text at cnBorders + CAPTIONMARGINS.left, like a window
  without WS_SYSMENU.
- Not implemented: SMALLCAPTION/tool windows, MINCAPTION, help button, disabled max/min (no X11 equivalent of
  WS_MAXIMIZEBOX is consulted), DPI-scaled image selection (96 dpi assumed).
- `_LHWM_FRAME_FLAGS` `NO_TITLE` / `NO_ICON` are honoured here too (CLIENT-PROTOCOL.md "Additions 2026-10-24"): no caption
  text / no icon (the text then starts at cnBorders + CAPTIONMARGINS.left, as without an icon). Everything else about extended
  frames in this mode is the header pixmap (below, "Extended frames").

### XP visual styles (Luna)

`themes/luna-blue`, `themes/luna-olive`, `themes/luna-silver` are XP's Luna in its three colour schemes (NormalColor,
HomeStead, Metallic; NormalSize), generated from the copy of `luna.msstyles` that Longhorn 4051 ships (Longhorn-compiled;
4042's differs only in its version resource):

    W=/u/aap/fun/longhorn/wim; F=$W/4074/3/WINDOWS/Fonts
    for c in NormalColor:blue HomeStead:olive Metallic:silver; do
      tools/msstyles2theme.py $W/4051/2/WINDOWS/Resources/Themes/Luna/luna.msstyles themes/luna-${c#*:} --color ${c%:*} \
        --text-antialias none --font-file $F/trebuc.ttf --font-file $F/trebucbd.ttf; done

The same command on XP's own file (PACKTHEM_VERSION 3, e.g. `~/.wine/drive_c/windows/resources/themes/Luna/luna.msstyles`)
gives the same theme.ini; its button bitmaps differ in the antialiased corner pixels only (XP stores straight alpha and
premultiplies on load, `themeldr/utils.cpp` `PreMultiplyAlpha`; the Longhorn copy is stored premultiplied).

What differs from the Longhorn styles, all handled by the existing code: CAPTIONBARHEIGHT 25 and no SIZINGBORDERWIDTH (system
default 1), so cnBorders 4 and a 30 px caption; 8-image button strips (4 per frame state, XP's layout: normal, hot, pushed,
disabled), 21x21 buttons with 13x13 glyphs (IMAGEFILE3 of five sizes); CAPTION/MAXCAPTION 66x29 with SIZINGMARGINS 28 35 9 17;
FRAMELEFT/RIGHT bitmaps 5 px wide drawn into the 4 px border; `TEXTSHADOWTYPE = SINGLE` (1,1) on the active caption only. Two
new pieces:
- `font_files` (theme.ini): Luna's CAPTIONFONT is Trebuchet MS 10 bold (13 px), not installed here. luxwm adds the listed TTFs as
  fontconfig application fonts at theme load (together with the registry's `fonts =`, the set replaced per theme load; "Icons,
  maximised caption text, fonts") and drops pango's default font map so the next layout sees them; the converter writes it from `--font-file`, or finds the font in the style's own
  `WINDOWS/Fonts` (4051 has no Trebuchet; the 4074 files are used).
- text rendering: XP's default "Standard" font smoothing leaves these captions bi-level, so Luna uses `text_antialias = none`
  with full hinting (FreeType's mono rasteriser then gives GDI's 2 px stems; grey or subpixel AA rendered the bold face visibly
  thinner).

Comparison (`compare_luna.png`; reference: `/u/aap/src/flxp/screenshots/luna_exp_smallTB_folder.png`, a real XP SP3 Explorer
window, 800x596 at 17,17; ours rendered offline at the same size and laid over it, the icon being the reference's own icon
pixels): left/right/bottom borders, bottom corners, the caption bitmap including both rounded top corners: 0 pixels differ
(2248 + 2248 + 3200 border, 16800 caption pixels between title and buttons, 660 in the corner/icon block). Buttons: 48 of 2400
pixels differ, all of them the antialiased corner pixels of the three buttons (alpha 15/143/239 corners blended differently;
the XP-format conversion gives the same count). Caption text: same position and baseline, glyph shapes differ (FreeType mono
hinting of Trebuchet vs GDI; 488 of 4200 pixels in the text box, 639 with subpixel AA). No olive/silver reference was at hand;
`compare_luna.png` shows ours, active and inactive. In the lab (`screenshot_luna_extendframe.png`): `xprop -root -f _LH_THEME
8u -set _LH_THEME luna`.

## libuxtheme source (2026-10-24)

luxwm reads the `.msstyles` itself through the shared visual-style library (`~/src/flxp/src/uxtheme`, `~/.local/include/uxtheme/ut.h`,
`libuxtheme.so.1`; `docs/history/THEMES.md` "One uxtheme for everything", step 1). The theme directories under `themes/` are now caches: the
fallback for an luxwm built without the library, and the place for tuning keys. A registry entry whose `msstyles` names any v3 (XP) or
v4 (Longhorn-compiled) style gets classic frames without running `tools/msstyles2theme.py` (`[luna-xp]` in `../themes.ini`: the real XP
`luna.msstyles`, copied from the Wine prefix to `resources/msstyles/luna-xp.msstyles`, no directory at all; likewise `[royale]`, `[zune]`,
`[embedded]`).

**Which style.** `theme_load` takes a theme spec: a directory, `DIR|STYLE.msstyles|COLOR|SIZE`, or `|STYLE.msstyles|COLOR|SIZE` (no
directory: msstyles mode, every tuning key at its default). The style is the spec's, else the directory's own theme.ini `msstyles` /
`color` / `size` (written by the converter, added by hand to `aero` and `jade`; relative paths are against the directory). wm.c builds the
spec from the registry entry: `frame` + the entry's `msstyles`/`color`/`size`, or `|msstyles|color|size` for an entry without `frame`.
A `frame_classic` directory, or a `fallback_theme`, is loaded alone with its own theme.ini style (aero4074's `jade4074-classic` is Jade's
classic frame, not aero.msstyles'). `_LH_THEME` switching therefore reloads through the library. `_LHWM_THEME` publishes the spec (flxp
only checks that it is non-empty).

**Source choice.** With the library and a style: the library (`ut_open`). If the style does not open, the directory's PNGs (logged),
or failure for a spec without a directory. `LHWM_THEME_SOURCE=dir` (luxwm) / `FRAMETEST_SOURCE=dir` (frametest) forces the PNG caches,
`=lib` the library. The log says which: `luxwm: theme SPEC (msstyles, /path/luna.msstyles (NormalColor, NormalSize, v3)) margins ...`
or `(msstyles, PNG cache)`, and at startup `luxwm: libuxtheme built in: visual styles are read live`.

**What comes from the library** (`frame_ut.c`):
- msstyles mode, drawn live with `ut_draw_background` at paint time (Wine's DrawThemeBackground over flxp's GDI: nine-grid with XP's
  ninegrid2.cpp rounding, margins, TRUESIZE, TRANSPARENT/TransparentBlt, uxtheme's alpha rules): CAPTION, MAXCAPTION, FRAMELEFT,
  FRAMERIGHT, FRAMEBOTTOM in their frame states. Each part is rendered into a w x h image and painted OVER at its place, the way
  `ms_draw` paints its nine-grid image, so the compositing is the same.
- the caption buttons (CLOSE/MAX/MIN/RESTOREBUTTON, IMAGEGLYPH with CONTENTMARGINS): their IMAGEFILE and IMAGEFILE1..5 strips come from
  `ut_get_bitmap` (uxtheme's alpha rules; TRANSPARENTCOLOR keyed out of an image without alpha when the part is TRANSPARENT) and go
  through frame.c's nine-grid and glyph rule. The library's DrawThemeBackground cannot be used for them: its draw.c (Wine) selects
  the glyph image for, and centres it in, the whole button rect, while XP's imagefile.cpp (`DrawBackground`, lines 1529-1545:
  `GetBackgroundContentRect`, then `SelectCorrectImageFile(&rc)`) uses the content rect. In a 21x21 Luna button that is the 19x19
  glyph instead of the 13x13 (Slate 4074: 13x13 instead of 9x9), visibly too large (first try: every caption differed only there).
  frame.c's rule is XP's, so the library is used as the bitmap reader here (README above: "the largest IMAGEFILEn fitting the
  CONTENTMARGINS rect"). Parts without content margins would be drawn live.
- metadata for frame.c's geometry, text and shape: IMAGECOUNT, the true size (`ut_part_size` TS_TRUE: the button aspect),
  SIZINGMARGINS (shape rows), CONTENTMARGINS, CAPTIONMARGINS (CAPTION's for parts without their own), SIZINGTYPE, TEXTSHADOWTYPE/
  OFFSET/COLOR per caption state; the caption's active image at its true size for the window shape's corner rows (`frame_shape_rects`).
- [SysMetrics]: SIZINGBORDERWIDTH (default 1), CAPTIONBARHEIGHT, CAPTIONFONT (`ut_get_font`, as the pango description the converter
  writes: `FACE[,Tahoma][ Bold][ Italic] PXpx`), CAPTIONTEXT, INACTIVECAPTIONTEXT (`ut_get_color`).
- defaults the converter used to bake in, unless theme.ini sets them: `text_hinting = none` for a `Quality:ClearType-Natural`
  CAPTIONFONT, `text_antialias = none` for an XP (v3) style (XP's "Standard" smoothing).
- DWM mode: the CompositedWindow PNGSTREAMs (`ut_get_stream`, decoded with cairo's PNG reader from memory) for every texture file
  name: `plate_active/inactive.png` = LGFRAME (483) top/bottom half, `plate_explorer_*` = LGFRAMEEXPLORER (484), `plate_max_*` =
  LGFRAMEMAX (493), `plate_max_explorer_*` = LGFRAMEMAXEXPLORER (494), `overlay.png` = LGFRAMEOVERLAY (486, grey RGB: white at alpha =
  grey, as the extracted LA file), `close/max/min/restore.png` = LGCLOSEBTN/LGMAXBTN/LGMINBTN/LGRESTOREBTN (487, 489-491),
  `glow_close.png`/`glow_btn.png` = LGCLOSEBTNOVERLAY/LGBTNOVERLAY (488, 492), `captionfog.png` = CAPTIONFOG, `default_icon.png` =
  DEFAULTICON (506), `shadow.png` = LGFRAMESHADOW (485). Every one of them is pixel-identical to the extracted file (checked for aero and
  jade), and alpha-less streams are made ARGB32 like the files. A name the style lacks falls back to the file.
- the registry's `fonts` directories are also given to `ut_add_font_dir` (the library's own text drawing; luxwm draws text itself).

**What stays in theme.ini / frame.c:** text (pango, the GDI metrics emulation, TST_SINGLE / TST_CONTINUOUS shadows), geometry
(cnBorders, button placement, hit testing), the window shape, maximised / extended-frame logic; tuning keys `text_antialias`,
`text_hinting`, `text_dy`, `font_files`, `default_icon` (classic: shell32 #3), `shape_alpha`, `text_max_*`, `glow_max`; and for DWM
themes every inset and tuning key (`plate_insets`, `margins`, `explorer_*`, `max_*`, `extend_rows`, `glass_alpha(_mode)`, `fog`,
`overlay`, `button_*`, `buttons`, `font`, `text_*`, `glow`, `title_x`, `icon_*`, `fallback_theme`); the presence of `plate_explorer` /
`plate_max` / `plate_max_explorer` still switches those plates on. In library mode the style-derived theme.ini keys (`border_width`,
`caption_*`, `<part>.*`) are not used. Theme directories with only a theme.ini work (checked: aero, jade, luna-silver, slate4074-classic,
jade4074-classic reduced to theme.ini render byte-identically to `tests/ref`).

**One case kept from frame.c:** a part drawn taller than its bitmap whose SIZINGMARGINS cover every row (Luna Silver's MAXCAPTION: 26
rows, margins 9 + 17) has no middle to stretch. Only luxwm asks for that (a maximised caption continued into a header, `ext46max`); XP
draws MAXCAPTION exactly CAPTIONBARHEIGHT + 1 high and its ninegrid2.cpp would stretch a zero-row source, Wine's draw.c leaves the rows
empty (a black band). `fut_ms_draw` then renders the part at its own height and repeats source row `top` in between, as frame.c's
`ng_map` does.

**Build detection.** `tools/uxtheme-flags.sh` prints the compiler and linker flags: `pkg-config uxtheme`, else `include/uxtheme/ut.h` +
`lib/libuxtheme.so` under `$UXTHEME_PREFIX`, `~/.local`, `/usr/local`, `/usr` (`-DHAVE_UXTHEME -I.../include`, `-L.../lib
-Wl,-rpath,.../lib -luxtheme`); nothing with `UXTHEME=no`. `configure` substitutes them into `Config.mk` (`@ut_cflags@`, `@ut_libs@`;
`--without-uxtheme`), `tests/regress.sh` uses the same script. Without the library `frame_ut.c` compiles to stubs and luxwm needs the
theme directories as before (checked: builds and links without `-luxtheme`, 128 identical).

**Verification** (`tests/regress.sh`; `compare_lib_vs_dir.png`):
- `--compare-sources`: all 8 theme directories x 16 files, dir (PNG cache) vs lib: **128 of 128 byte-identical** (aero and jade from the
  streams, the six classic/Luna themes drawn through the library).
- On the way there two differences turned up, and in both the old renders were wrong. The glyph selection (above) was wrong on the
  library's side. Luna's `max.png` and `ext46max.png` (blue, olive, silver) were wrong on the cache's side: luna.msstyles defines
  `[Window.MaxCaption] SizingType` twice, `stretch` then `Tile` (XP's NormalBlue.ini, and the 4051 VARIANT records alike). XP's
  uxtheme takes the first: the parser appends properties in source order (`CThemeLoader::AddData`) and `CRenderObj::GetValueIndex`
  returns the first match; Wine's `MSSTYLES_AddProperty` keeps the first too, and so does the library. The converter's ini dict kept
  the last (TILE). The 3 px middle of the 66 px bitmap was therefore tiled instead of stretched across the maximised caption: small
  gradient differences of up to a few levels over the whole width, rows 2..25. `msstyles2theme.py` now keeps the first (only Luna's
  `maxcaption.sizing` changes, every PNG of all six classic directories stays byte-identical), the classic directories were regenerated
  from the PE files (now with `msstyles`/`color`/`size`), and `tests/ref` was regenerated for exactly those 6 files.
  Afterwards: lib 128 identical, dir 128 identical, no-library build 128 identical.
- `[luna-xp]` (XP's own v3 luna.msstyles, no directory) against the XP SP3 reference (`/u/aap/src/flxp/screenshots/
  luna_exp_smallTB_folder.png`, 800x596 at 17,17, the icon being the reference's own pixels, as `compare_luna.png`):
  `compare_luna_xp.png`. Left/right/bottom borders 0 of 2264/2264/3168 pixels differ, the caption bitmap 0 of 18190 (+ 0 of 1077 right
  end and 0 of 660 corner/icon), buttons 48 of 1323 (the antialiased button corners, as with the 4051 copy), title text 488 of 2750
  (FreeType mono vs GDI glyphs). Exactly the 4051-copy numbers. luna-xp and luna-blue differ from each other only in 36 (active) / 27 (inactive)
  corner pixels of the buttons (+-1 level: XP stores straight alpha and premultiplies on load, the 4051 copy is stored premultiplied). The HomeStead and
  Metallic schemes of the same file load the same way.
- XP styles from `XP_Themes.zip` (2026-10-25; `docs/history/THEMES.md` "Microsoft's later XP styles"): `[royale]`, `[zune]`, `[embedded]` have no directory and load like `[luna-xp]` (whose style is now the copy `resources/msstyles/luna-xp.msstyles`).
  `compare_xp_styles.png`: frametest renders (active, inactive, maximised) of all of them plus Royale Noir's NormalColor scheme (= Royale),
  and the Luna caption next to the XP SP3 screenshot; no reference screenshots of the other four exist here. All four open through the
  library (v3), margins 4/4/30/4, borders identical to `ut-dump --render`; on the lab (`:4`) `_LH_THEME = royale` / `zune` /
  `embedded` switch live (`_LHWM_THEME = |.../resources/msstyles/royale.msstyles|NormalColor|NormalSize`). The theme directories'
  `theme.ini` now name `resources/msstyles/*` and `resources/fonts/4074` and `tests/regress.sh` uses `resources/fonts/4074`: still 144
  of 144 identical (lib and dir source).
- Lab (`:3`, picom xrender, an xterm): `_LH_THEME` aero4074 -> luna-xp -> slate4074 -> luna-olive -> jade4074-classic -> luna-xp
  switches live; the log shows each style opened through the library, `_LHWM_FRAME_METRICS` follows (luna-xp: caption 30, borders 4).

Library gaps found (for the flxp side; worked around here):
- `DrawThemeBackground` places IMAGEGLYPH glyphs in the whole rect, not the content rect (above), and there is no way to draw only the
  background, so luxwm reads the button bitmaps instead.
- `ut_get_font` reports weight 400 for XP's `CaptionFont = Trebuchet MS, 10, bold` (v3); the v4 `Tahoma Bold, 8` is 700.
  frame_ut.c reads the flags from `ut_get_string`.
- `ut_get_bitmap` does not say whether the image counted as having alpha (an opaque result could be either). frame_ut.c treats an
  all-opaque result as "no alpha" for TRANSPARENTCOLOR keying.
- `ut_add_font_dir` cannot remove a directory (luxwm replaces its fontconfig set per theme, the library's only grows).
- A part with zero-height middle stretched taller (above) leaves rows empty.

## Classic fallback: a theme's own msstyles without composition (2026-10-27)

aap: "non-composited decoration should be drawn straight from msstyles through our uxtheme; I would like to toggle aero on and off in
5048 and 5112 easily". The classic frame of a DWM (glass) theme, used while no compositor runs or with `composition = off`, is chosen in
`theme_select()`:
- registry `frame_classic` naming a directory: that theme, as before (aero-patch, aerovision, jade4074: `jade4074-classic`);
- registry `frame_classic` **empty, missing or `style`** (lux's resolved view turns `style` into an absolute `.../style`, which counts
  too unless such a directory exists; a preset's `[Frames] frame_classic = style` likewise): the theme's **own visual style in msstyles
  mode**, the spec `|STYLE|COLOR|SIZE` from the entry's `msstyles`/`color`/`size` (`struct theme` now keeps the resolved style:
  `style`, `style_color`, `style_size`), drawn live through libuxtheme like `[luna-xp]` (CAPTION/MAXCAPTION/FRAME* with
  `ut_draw_background`, the buttons through frame.c's glyph rule, [SysMetrics] font and colours; no directory, so every tuning key
  at its default and no default icon). If that does not load (a style without a Window class, luxwm built without the library) the
  frame directory's theme.ini `fallback_theme` is the last resort;
- `_LHWM_THEME` set directly (debugging, no registry entry): theme.ini `fallback_theme`, where `style` means the own style too.
Log: `luxwm: theme aero5048: frame ..., classic (the theme's own msstyles)` and `luxwm: no compositor: DIR|STYLE|... -> |STYLE|COLOR|SIZE
(the theme's own msstyles)`; `_LHWM_THEME` then publishes the `|STYLE|...` spec, `_LHWM_COMPOSITED` 0.

`[aero5048]` and `[aero5112]` now say `frame_classic = style`. Both styles have the whole Window class (`ut-dump FILE --list Window`:
CAPTION, MAXCAPTION, MINCAPTION, SMALLCAPTION, FRAMELEFT/RIGHT/BOTTOM, MIN/MAX/CLOSE/RESTORE/HELPBUTTON, ...): 5048 CAPTIONBARHEIGHT 18
and no SIZINGBORDERWIDTH (margins 4/4/23/4), 5112 CAPTIONBARHEIGHT 19, SIZINGBORDERWIDTH 4 (margins 7/7/27/7), both CAPTIONFONT Segoe UI 9
ClearType-Natural. Their theme.ini `fallback_theme` stays `jade4074-classic` (the last resort). aero-patch's style has a Window class too
(CAPTION with SIZINGMARGINS 100/200), but its entry keeps `frame_classic = jade4074-classic` (not asked for; `frame_classic = style`
would switch it). Lab (`:3`, picom xrender; `_LH_THEME` aero5048 / aero5112, picom stopped with the display-filtered kill loop and
started again with `lab.sh restart-picom`): glass ⇄ the style's own classic frame both ways, `_LHWM_COMPOSITED` 1 ⇄ 0;
`compare_aero_classic_fallback.png` (5048 glass / 5048 classic / 5112 glass / 5112 classic, an xterm). aero-patch still falls back
to `jade4074-classic`. `tests/regress.sh` renders the classic fallbacks of every registry entry whose `frame_classic` is `style` or
missing as `<theme>.classic/` (lib source only, the usual 8 renders x png+txt): `tests/ref/aero5048.classic`, `tests/ref/aero5112.classic`
(32 new files). Regression after this round (lib source): 201 identical (169 + the 32 new) and the 11 known stale aero5112 files; frame.c
only gained the registry/preset `style` value and the `style` fields, every existing render is byte-identical; `--compare-sources`: 0 differ.

## Windows Classic frames (`style = classic`)

docs/THEME-FORMAT.md sections 2a and 14: with `VisualStyle.style = classic` (Display Properties' "Windows Classic style", or
`lhde-theme layer-set VisualStyle.style classic`) there is no visual style; the look is Windows' classic one from the record's
`Colors.*` and `Metrics.*` (a scheme `styles/NAME.scheme`, the current layer; without either the built-in Windows Standard). Until
now luxwm kept the previous theme's frames ("no frame set and no visual style"). `lh_theme_apply` now turns a record whose
`style.kind` is `classic` (and any theme with neither a frame set nor a visual style) into the spec
`|classic|smoothing=...;activecaption=R G B;...;borderwidth=N;captionwidth=N;captionheight=N;captionfont=...;menufont=...`
(every `Colors.*` the record has and those five metrics); `frames_classic` and `Glass.frames` do not count then (the user chose no
visual style: a frame set would draw one). `theme_load` gives it to `classic_load` (frame.c): `struct theme` keeps `classic = 1`,
`cl[31]` (COLOR_ index order, Windows Standard for what is missing), `cl_capw`, the caption font in `ms_font` and the menu font in
`cl_menufont`; the mode is `THEME_MSSTYLES` with `t->ut == NULL`, so margins, the shape (a plain rectangle), the hit test, the
window menu's icon cell, `_LHWM_FRAME_METRICS` (text colours `captiontext` / `inactivecaptiontext`) and the size-hint gap fill
(`btnface`) take the msstyles geometry. Log: `luxwm: theme slate4074: Windows Classic frames (style classic, scheme brick)`.

`classic_paint` is user32's non-client painting (Windows 2000 / XP `xxxDrawCaptionBar`, `DrawEdge`, `DrawFrame`,
`DrawFrameControl(DFC_CAPTION)`, `xxxDrawCaptionTemp`, GDI's `GradientFill`), for a sizable overlapped window with
`WS_EX_WINDOWEDGE`. It does not draw the pieces itself: it paints into an ARGB image of the frame's size with libwuxtheme's classic
helpers (`<wuxtheme/ut.h>`: `ut_draw_edge`, `ut_draw_frame_control`, `ut_draw_caption`, which run Wine's user32 `DrawEdge` /
`DrawFrameControl` and wux's GDI with the frame's colour table installed as the system colours) and composites it; Display
Properties' classic sample (flxp/settings) draws its windows with the same helpers and the same geometry, so there is one
implementation of the classic art. luxwm keeps the geometry (cnBorders, the button rectangles, maximised), the hit test, the shape
and the caption icon (cairo):
- frame: `ut_draw_edge(EDGE_RAISED, BF_RECT | BF_ADJUST)` (outer `3dlight` / `3ddkshadow`, inner `btnhighlight` / `btnshadow`;
  right and bottom are drawn first, so they own the corners), then the sizing border, `borderwidth` px of `activeborder` /
  `inactiveborder`, then 1 px `btnface` (DrawFrame: plain fills): cnBorders = 3 + `borderwidth` on every side (the msstyles
  formula). Maximised: the borders lie off-screen.
- caption: `captionheight` rows (SM_CYSIZE), then a 1 px `btnface` separator (SM_CYCAPTION = `captionheight` + 1). Buttons
  (`captionwidth` - 2) x (`captionheight` - 4), 2 px below the caption top; close 2 px from the right, 2 px gap, max and min
  touching (min/max/close, restore while maximised); `ut_draw_frame_control(DFC_CAPTION)`: `DrawPushButton` with `BF_SOFT`
  (raised: `btnhighlight`, `3dlight` / `3ddkshadow`, `btnshadow`; pushed: `EDGE_SUNKEN`, `3ddkshadow`, `btnshadow` /
  `btnhighlight`, `3dlight`), face `btnface`, glyph `btntext`. Classic caption buttons have no hot state (`hover` is ignored).
- glyphs: Marlett's `r` `0` `1` `2` (wux's GDI maps a symbol font's characters to U+F000 + the byte, as Windows does) at a cell of
  e = min(w, h) of the button's inside (within the 2 px push-button edge), centred there, `lfHeight` -e (Marlett's cell is its
  em); pushed: 1 px down and right (uitools.c.patch: Wine sized the glyph from the whole button, 2 px too big). At e = 10 (the
  16 x 14 button) that is the X 8 x 7, the box 9 x 9 with a 2-row top, the bar 6 x 2 of 4051/XP. The face is the first imported
  `fonts/<build>/marlett.ttf` (`cl_marlett`, registered as a single file with `ut_add_font_dir`, so a theme whose fonts lack it,
  classic's `xp`, still has it), else fontconfig's.
- caption fill: the whole bar first in `gradientactivecaption` (the brush around the buttons), then `ut_draw_caption` over the
  icon slot and text area: the slot (a `captionheight` square, solid `activecaption`; luxwm draws the SM_CXSMICON =
  (`captionwidth` - 2) & ~1 icon in it, 1 px right of centre), then the text area (icon slot .. min button - 2) as
  `GradientFill(GRADIENT_FILL_RECT_H)` from `activecaption` to `gradientactivecaption` (GDI's 8.24 fixed point, truncated, now
  wux's own GradientFill; solid when the two colours are equal); inactive: `inactivecaption` / `gradientinactivecaption` /
  `inactivecaptiontext`.
- text: `captionfont` (`Tahoma, -11, bold`; points: -MulDiv(pt, 96, 72)) as a LOGFONT (`cl_face`, `cl_px`, `cl_bold`,
  `cl_italic`), drawn by `DrawText` (`DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS`) 2 px into the text area: GDI's hinted
  TrueType advances and vertical centring, "..." when it does not fit. `Fonts.smoothing` (`cl_smooth`, `ut_set_font_smoothing`):
  `off` bi-level, `standard` grey where the font's gasp table allows it (Tahoma stays bi-level at 11 px and is smoothed at 19 and
  24 px, as XP's "Standard" does), `cleartype` drawn as standard. wux's font lookup has no per-glyph fallback.
- the window menu (menu.c) takes `menu`, `menutext`, `graytext`, `highlight`, `highlighttext`, `btnface`, `btnshadow`,
  `btnhighlight`, `3ddkshadow`, `3dlight` and `menufont`.

Measured against Longhorn 4051's Windows Classic (`~/fun/longhorn/screenshots/lh4051-193915.png`, the Computer window at
109,33, 800 wide, Windows Standard): edge rows 33/34, band 35/36, caption 37-54 (18), separator 55; buttons 16 x 14 at y 39, close
x 887-902, max 869-884, min 853-868; icon slot 113-130, gradient 131-850 (`activecaption` 10 36 106 to 166 202 240), text at
x 133, baseline 50. `frametest '|classic|' out.png 800 300 Computer 1 0 none ICON` against the screenshot's rows 33-55 (all 800
columns, the icon slot left out, `FRAMETEST_FONTS` = the imported 4074 fonts): 0 of 18400 pixels differ, the Marlett glyphs
included. The default icon (no client icon): the first of
`packs/xp/shell32/16/3.png`, `packs/4074/shell32/16/3.png`, `packs/2k/shell32/16/3.png` that exists (and its 32 px size).

`tests/regress.sh` renders `classic/` (the spec `|classic|`: the built-in Windows Standard, no data needed), `classic.xl/` (Extra
Large Fonts metrics: 24/24, Tahoma -19 bold) and `classic.hc/` (High Contrast Black's colours, borderwidth 6, 31/31, Tahoma -24 bold),
the usual 8 renders x png+txt each. Display Properties' classic sample (the Appearance tab with "Windows Classic style") draws its
inactive, active and message-box windows with the same helpers and the same construction (edge, `borderwidth` ring, `btnface` ring,
caption, buttons); only its window sizes are its own.

## Maximised mode, the desktop layer, running without a compositor (M5a, `SHELL-DESIGN.md`)

- `_LHWM_MAXIMIZED_MODE` (root, CARDINAL x 10, one per desktop like `_NET_WORKAREA`): 1 while the desktop's active
  window (current desktop: the focused one; others: the most recently focused one that is not minimised) is maximised or
  fullscreen, or, with no such window, any non-minimised window there is. Recomputed after every event, written only on
  change, deleted on a clean exit. luxshell fades its black layers and its desktop layer on it (`lh.maxmode()`; the
  EWMH rule is its fallback when the `_NET_SUPPORTING_WM_CHECK` window is not luxwm's).
- DWM themes maximised: the frame fills the work area, margins `0 0 m_t 0` (`_NET_FRAME_EXTENTS` follows), so the
  client fills the work area below the caption. The plate is 9-sliced as `w x (h + bottom margin)` (bottom border below
  the window, side borders under the client); the caption keeps its height, text and button layout pixel-identical to
  the normal frame (the aero pill and buttons sit in the side margin, so the borders cannot be pushed off-window without
  clipping the close button). The two top corners are squared (`square_corners()`: inside a 6x6 square each pixel comes
  from the straight edge next to it, mitred along the diagonal) on the plate and on the overlay, and the window shape is
  the full rectangle. No resize edges while maximised (`frame_hit_ex`: buttons, caption, nothing else). Non-maximised
  frames are unchanged (frametest output byte-identical before/after for aero, jade, jade4074-classic, slate4074-classic).
- **M5b (2026-10-24): maximised plates and the crossfade** (`SHELL-DESIGN.md` "M5b"). A maximised DWM frame now draws 4074's
  `LGFRAMEMAX` plate (msstyles STREAM 493; `MAXEXPLORER`, 494, under an Explorer-style header, i.e. `_LHWM_EXTEND_FRAME` top > 0)
  instead of the squared normal plate; geometry as above (client fills the work area below the caption, margins `0 0 m_t 0`, window
  shape the full rectangle). Theme keys: `plate_max = ACTIVE.png [INACTIVE.png]`, `plate_max_explorer = ...` (one file: both states),
  `max_insets` / `max_explorer_insets` (top right bottom left; defaults: `plate_insets`, then `max_insets`), `max_explorer_rows = r0 r1
  line` (as `explorer_rows`: rows [0, r0) 1:1, band [r0, r1) stretched to the header height - 2, lines [line, line + 2) on its last two
  rows, body from line + 2; default `2, max_explorer_insets top - 2, same`), `max_button_right` / `max_button_top` (button offsets while
  maximised; default `button_right` / `button_top`), and `overlay = 0` (no `overlay.png` at all; default 1). Without `plate_max` the M5a
  squared plate is drawn as before. The maximised plate gets the glass weighting like the others (aero: opaque anyway) but no overlay
  (LGFRAMEOVERLAY belongs to the normal plate; the reference shows no edge highlight) and no fog.
  - Textures (`themes/aero|jade/plate_max_{active,inactive}.png`, `plate_max_explorer_{active,inactive}.png`, the halves of the PNG,
    active on top): **aero** 493/494 are 787x1198 **opaque** RGB greys (no alpha channel; 599-row halves; the same art as 5048's uDWM
    BITMAP 1002, COMPOSITION.md 15.4). LGFRAMEMAX: caption rows 0..24 (the sheen: a horizontal gradient 10 -> 55 -> 0 grey with light
    rays), light/dark lines at rows 25/26, body from row 27 (gradient 0 -> 55 -> 0), 1 px edges at x 0/1, 785/786 and rows 597/598;
    inactive half lighter (54..67). MAXEXPLORER: the same with the band rays over rows 0..77, lines 78/79, body from 80. **No caption-button
    pill** in either (checked at the 4x zoom of the right end). desksrv 4074 loads them as PNG 122/123 (byte-identical built-ins) with
    texture margins **(0, 27) (0, 0)** and **(0, 71) (0, 0)** (left, top) (right, bottom) at `0x6f733560`/`0x6f733578` (4066:
    `0x6cc43588`/`0x6cc435a0`, the calls at `0x6cc52e13..0x6cc52f02` push PNG 0x7a/0x7b), so the whole width stretches; the second
    margin set of those calls, (12, 8) (12, 0), is presumably the plate's outset around the window rectangle and is not modelled. aero
    theme.ini: `max_insets = 27 0 0 0`, `max_explorer_insets = 80 0 0 0`, `max_explorer_rows = 0 78 78` (measured: the lines are at 78/79,
    7 rows below desksrv's 71, the same offset as the Explorer plate's 73 vs 80). **jade**: 4074's 493 and 494 are byte-identical to its
    483 (the flat 801x1214 normal plate; COMPOSITION.md 18.1), extracted the same way; `max_insets = 27 9 9 9`, `max_explorer_insets =
    73 9 9 9`, corners squared as in M5a. Build 4066 has its own 787x1198 Jade LGFRAMEMAX (`res/4066/jade.msstyles/STREAM_491_1033.png`,
    = 492): the same two flat colours (112,144,112) / (139,160,139) at alpha 213, square corners, but split 607/591 rows (active rows
    0..606), not halves; not used (COMPOSITION.md 18.1's "(125,152,125)" for it is the mean of both halves, not a pixel value).
  - Buttons: `screenshots/aero_max.png` (1920x1080, work area 0..1769, sidebar from 1770) has the same sprites ending with close's bright
    column at x 1766, 3 px further right than on a normal frame; a fit of the min/max/close strip over the reference (gap 0..2, right
    0..7, top 0..3) is best at gap 2, **right 2**, top 2 (rmse 48 on the video-compressed shot; next best 57): aero `max_button_right = 2`.
    The caption text: until 2026-10-24 `text_active` (dark with the white glow) on the dark plate; now `text_max_active` (white, no glow),
    see "Icons, maximised caption text, fonts".
  - **Crossfade** (wm.c `max_transition`, `xfade_weight`, `xfade_tick`). **Off by default since 2026-10-25** (user decision; lux's bars
    are made instant too): config.h `MAX_FADE_MS` (default 0) is its duration, and 0 switches the plate in the same paint as the geometry
    change (`client_geom_max`), with no timer and no `maxfade` ticks (lab: the caption pixel is at its final value in the first shot).
    A non-zero value (`-DMAX_FADE_MS=400`, or config.h) restores the fade described here: with a compositor and a DWM theme that has
    `plate_max`, a maximise or restore of a mapped window on the current desktop starts a 400 ms fade instead of picom's geometry animation (since
    2026-10-25 the default style has no geometry animation at all, see "One step" below). The frame jumps to the new rectangle at once (as before)
    and its plate fades there: on maximise from the normal plate (squared, maximised geometry) to the maximised plate, on restore from the
    maximised plate (stretched over the restored frame) to the normal one, linear over 400 ms like luxshell's black layers (runtime.lua
    `lh.animate(..., 400)` without an easing = linear; the Animator2 0.3/0.4 curve is not used there either), repainted on a 25 ms grid by
    the main loop's poll timeout (next to `anim_expire`). A second maximise/restore during a fade reverses it from where it is; a drag
    that un-maximises ends it. `_LHWM_MAXIMIZED_MODE` is still written first (`max_transition` starts with `maxmode_update()`).
    frame.c: `frame_paint_ex3`/`frame_paint_cr3` take the weight `maxfade` (0 normal .. 1 maximised, < 0 = by state; the old entry
    points pass -1). In between it paints a cross-dissolve `(1 - t) normal + t maximised` (premultiplied, `OPERATOR_ADD` in a group, then
    over the background) rather than "maximised over normal with alpha t": identical for aero's opaque plate, and Jade's translucent
    plate ends at its own alpha instead of jumping from max-over-normal (alpha 0.97) to 0.835 at the end; the overlay fades out with
    `1 - t`. The three layers of a fade (normal plate, maximised plate, masked overlay) are rendered once per fade into w x h images
    (`fade_layer`, keyed by size, focus, state, extension and theme load) and freed by the next paint outside a fade: a fade frame of the
    1130x770 extended Explorer window costs 8..10 ms of cairo (25..37 ms uncached), the restore frames 2 ms. `tests/frametest`:
    `FRAMETEST_FADE=t` renders a fade frame.
  - **One step (2026-10-25, user report "maximisation jumps in two steps, black fade wrong, only when composited").** The geometry
    change was several steps: maximise moved frame and client to the work-area corner at the old size (`client_move_absolute`), resized
    them with the *normal* margins and painted the frame (`client_resize_absolute` → `draw_text`, 8..37 ms of cairo for a big plate),
    then re-placed the client into the maximised margins (9 px jump) and painted again; restore moved first at the maximised size, then
    resized. `_LHWM_MAXIMIZED_MODE` went out before all that, so luxshell's bars started fading one or two frames before the window
    moved, and the crossfade clock started before the paint (on a restore the plate finished 60 ms after the bars). Now
    `client_geom_max()` does the whole change in one batch (frame and client `XMoveResizeWindow` to their final rectangles with the
    final margins, one paint) and, with a compositor, the server is grabbed from the mode write in `max_transition` to the end of that
    paint, so picom and luxshell see mode, geometry and painted plate together; the crossfade clock starts at the ungrab and one 25 ms
    tick in, like `lh.animate`'s first frame. `ANIM_4074PLUS` no longer writes `_LHWM_ANIMATE = 8` (no picom geometry animation at
    all; `ANIM_INVENTED` keeps 2 for themes without `plate_max`). Lab (`:3`, xrender, luxshell, aero4074; 20 ms root grabs + a
    property/ConfigureNotify tracer): before, the tracer showed `client (9,27,424,264), frame (0,0,442,300), client (9,27,1081,734),
    frame (0,0,1099,770)` and 7..13 ms later `client (0,27,1099,743)`; after, `frame (0,0,1099,770), client (0,27,1099,743)` in the same
    millisecond, followed by the docks' restack. In the shots the window changes once (first composited frame 80..140 ms after the
    request: lab latency) and the bars and the plate change in that same frame and reach their end values together at ~440 ms (taskbar
    mean 151 → 75 / caption pixel 181 → 55 on maximise, the reverse on restore). Not solved: picom 13 xrender on Xvfb often composes
    one or more frames right after the change with the resized window, and the windows it covers, missing (wallpaper; also seen with
    the old binary in 6/6 xterm runs). It still shows in about half of the runs after the change, mostly on restore, almost never
    without luxshell (1 of 8), never with `--log-level debug` (timing), and never on a plain resize; picom glx on Xvfb (llvmpipe) does
    not repaint the fades at all there, so the real-hardware glx behaviour is unchecked.
  - Lab check (`:4`, Xvfb + picom xrender, luxshell; `/tmp/.../m5b/tools/series.py` sends the `_NET_WM_STATE` toggle, traces root
    PropertyNotify and the client's ConfigureNotify, and takes `import -window root` shots every 50 ms): xterm maximise: `_LHWM_MAXIMIZED_MODE
    = 1` at 19 ms, the client's ConfigureNotify at 20..27 ms (mode first); restore: mode 0 at 2..9 ms, ConfigureNotify 7..20 ms. Caption
    pixel on maximise (565,12): 37, 39, 42, 46, 47, 49, 53 at 141..441 ms, 55 (final) from 490 ms; on restore (500,132): 92 (91 ms), 97,
    102, 107, 114, 116, 122 (441 ms), 126 (final) from 491 ms, while luxshell's taskbar mean rises 80 -> 177 and the sidebar 12.5 -> 35.7
    over the same 91..491 ms: frame and bars fade in step. On maximise picom presents nothing for the first ~140 ms (the known lab
    latency, M5a), and luxshell's desktop layer still turns black at once (luxshell's `desktop_black`, not luxwm). With the extended
    1130x770 window the lab manages 8 of the 16 frames on maximise (the X server is busy: full-window uploads of 30..80 ms while picom
    re-blurs the screen), all 16 on restore. `compare_max_plate.png`: `aero_max.png` header (scaled 0.64, and the button end at 2x) /
    before (M5a squared normal plate over luxshell's black desktop) / after (MAXEXPLORER, `max_button_right 2`); header grey across the
    width at y 20, ref vs after at 5/15/25/.../95 %: 6/6, 16/29, 27/31, 37/45, 49/54, 54/55, 50/54, 41/51, 35/37, 16/16, 7/5 (before:
    114 ... 23 ... 102).
  - Lab artefact, not new: an extended (ARGB) client that is maximised or restored on picom xrender/Xvfb sometimes shows its header
    black in picom's output while the decoration window holds the right pixels (checked with `GetImage` on it); restarting picom shows the
    plate. The old (M5a) binary does the same.
- Reference: the only maximised window in `../../screenshots/` is the self-framed Explorer in `lh4074-181424.png`
  (Slate): its top corners are square and flush with the screen edge at 0,0 (nothing clipped), the caption sits on
  black, and its caption buttons end 3 px before the work area's right edge (the sidebar).
- msstyles maximised (MAXCAPTION): verified the shape is one full rectangle and restore brings the rounded caption
  corners back. SMALLCAPTION rules are still open.
- Shadows: luxwm draws no `shadow.png`; picom does (Jade 4066 fit, COMPOSITION.md 18.1: LGFRAMESHADOW is a 9-grid outset left 40,
  top 20, right 19, bottom 38 around the frame; picom's own shadow stands in for it). Each decoration window carries `_LHWM_MAXIMIZED` (0/1) and
  `picom-glass.conf` excludes `_LHWM_MAXIMIZED@ = 1` from shadows. The `@` (top-level) form matters: picom 13 does not
  re-read the client-window form when the property changes. The client keeps its shadow (it falls under the docks,
  `clip-shadow-above`).
- Desktop windows (`_NET_WM_WINDOW_TYPE_DESKTOP`) are never managed; `docks_raise()` lowers them to the bottom after
  every restack (checked after `restart-wm`). Their order among themselves is defined (it used to be whoever was lowered
  last, so flxp-desktop and luxshell's desktop layer swapped places with start order): higher `_LHWM_DESKTOP_LAYER`
  (CARDINAL, default 0, read as signed) above lower, equal layers in map order with the first-mapped one lowest
  (`CLIENT-PROTOCOL.md` "Desktop windows"). A desktop window that lowers itself (flxp-desktop does on map) is put back
  into that order. Tested on Xvfb with three desktop windows: map order A,B,C stays A<B<C after C lowers itself; C with
  layer -1 goes under A; A with layer 5 goes on top; B unmapped and mapped again stays between (its new map is later
  than A's and C's but the layers decide).
- WM restart: the previous `_NET_ACTIVE_WINDOW` and `_NET_CLIENT_LIST_STACKING` are put back after adoption, so a
  maximised active window stays active and on top (before, the last adopted window took focus).
- Compositor detection: XFixes selection events on `_NET_WM_CM_S<screen>` (linked with `-lXfixes`). A DWM theme is
  replaced by its classic frame while no compositor owns the selection, and comes back when one appears (logged to stderr).
  The classic frame is the registry's `frame_classic` (M6) when the theme came from `_LH_THEME` (empty or `style`: the theme's own
  msstyles, "Classic fallback" above), else the theme.ini `fallback_theme` key (aero, jade: `jade4074-classic`; a bare name is a
  sibling of the theme directory). A registry entry
  with `composition = off` uses `frame_classic` with a compositor too (jade4074-classic). The lab's `start` launches luxwm
  before picom, so the log shows the fallback and the switch back.
- Runtime theme switching (M6): a change of root `_LH_THEME` (`xprop -root -f _LH_THEME 8u -set _LH_THEME slate4074`,
  `session/lab.sh theme NAME`, luxshell's settings) looks the name up in the registry (`theme_registry_lookup` in frame.c, re-read
  on every switch, so edits to themes.ini apply at the next one) and loads `frame` or `frame_classic` as above.
  `_LHWM_THEME` (root, STRING) is the derived theme directory in use; writing it directly
  (`xprop -root -f _LHWM_THEME 8s -set _LHWM_THEME slate4074-classic`, a name or a path) still loads that theme (or its
  theme.ini fallback) for debugging, without changing `_LH_THEME`. Every `_LH_THEME` or `_LHWM_THEME` switch rewrites
  `_LHWM_COMPOSITED` and `_LHWM_FRAME_METRICS` even when unchanged, so clients get one PropertyNotify of each per switch
  (`composited_publish()`, flag `theme_announce`); an unknown name is logged and ignored. Verified on the lab with
  `xprop -spy`: aero4074 → slate4074 → jade → aero4074 gives `_LHWM_THEME`, `_LHWM_FRAME_METRICS`, `_LHWM_COMPOSITED` on
  each switch (also jade4074 → aero4074, where neither value changes), `jade` is rewritten to `jade4074`; with picom
  stopped aero4074 comes up as jade4074-classic with `_LHWM_COMPOSITED` 0, and glass returns with picom.
  A switch keeps every client rectangle where it was (new margins around it), shifts the restore geometry the
  same way, refits maximised frames, reloads icons at the new `icon_size` and sends the client an Expose (after
  unredirect/redirect clients kept stale contents otherwise).
- Open issue (lab, picom 13 xrender on Xvfb): with luxshell's desktop layer mapped, activating a maximised window that
  lay under another one sometimes leaves its client area showing the wallpaper in picom's output (the window's own
  contents are correct; the original luxwm did not show it). Excluding the desktop layer from `blur-background` made it
  worse, so `picom-glass.conf` no longer excludes it; not seen without luxshell or with the layer typed as a dock. Needs a
  look on the glx backend before chasing further.

## Icons, maximised caption text, fonts (2026-10-24)

**Window icons.** User report: icons only "very occasionally", in the captions and in luxshell's task buttons. Checked on the lab with
xterm, xclock, xcalc, xeyes, xfe (`WM_HINTS` icon pixmap + mask, no `_NET_WM_ICON`), yad, pavucontrol (GTK 3, `_NET_WM_ICON` 16 / 48
px, set before the map), pcmanfm-qt (Qt, `_NET_WM_ICON` 128 px...), zenity (GTK 4, client-side decorations, no icon at all), xmessage
and `examples/extendframe` (none). Root causes:
- luxwm read only `_NET_WM_ICON`, never `WM_HINTS`, so every X toolkit program without EWMH icons (xterm, xclock, Xt/Motif/FOX) had
  none, and a client without any icon showed nothing (no default icon). The user's own windows are mostly flxp's, which sets neither
  property (no `_NET_WM_ICON`/`XSetWMHints` icon anywhere in `~/src/flxp`), so only the odd GTK/Qt window had one.
- `_NET_WM_ICON` was re-read on PropertyNotify, but not when deleted, and a `WM_HINTS` change was used only for the urgency hint.
- The size pick was "nearest to 16" (a 15 px image beat a 32 px one); drawing scaled it with BEST at every paint.
Fixed (`client_set_icon`, `client_icon`, frame.c `frame_icon_from_netwm`, `frame_icon_from_pixmap`, `theme_default_icon`):
`_NET_WM_ICON` (read up to 16M items) → the smallest image at least the icon size (`icon_size`, msstyles 16), else the largest,
scaled once to the size with `CAIRO_FILTER_BEST` (aspect kept, centred); else `WM_HINTS` `icon_pixmap` + `icon_mask` (depth 1: black
on white as the classic X icon box; other depths through the default visual; depth 32 taken as premultiplied ARGB; errors from a
pixmap the client freed meanwhile are swallowed), scaled the same way; else the generic application icon (`app_icon`: shell32 #3,
what Windows draws for a window without an icon, from the first imported pack of 4074, 4051, 5112, 4042, `packs/BUILD/shell32/16|32/3.png`;
urxvt and xterm get it instead of the DWM default's flag); else the theme's default icon. PropertyNotify: `_NET_WM_ICON`
set, changed or deleted re-reads it; `WM_HINTS` re-reads the icon when its pixmap or mask id changed (urgency updates do not). Theme
switches re-read at the new size (as before). `LHWM_DEBUG=1` logs `icon 0x...: client | application | theme default | none`.
Default icons, theme key `default_icon = FILE [FILE...]` (relative to the theme directory; the nearest size at or above is used),
else `default_icon.png`: aero/jade `default_icon.png` = the CompositedWindow `DEFAULTICON` part (msstyles STREAM 506, a 20x17
Windows flag; aero from `res/aero/aero.msstyles/`, jade from `res/4074/jade.msstyles/`), drawn 1:1 centred on the 16 px cell (icons
at most 4 px larger than the cell are not scaled); the classic and Luna themes `default_icon = packs/4074/shell32/16/3.png
packs/4074/shell32/32/3.png` (the 4074 shell32 application icon from luxshell's `shellpack.sh`, XP's default window icon;
`tools/msstyles2theme.py` writes the line). The caption text of a client without an icon now starts after the default icon (XP's
indentation). luxshell got the same fixes (below, and `SHELL-DESIGN.md` M6 status). Lab check: `../lhshell/compare_icons.png`
(taskbar before/after, a live `_NET_WM_ICON` set and delete with xprop on xmessage, aero and jade4074-classic captions).

**Maximised caption text** (`text_max_active`, `text_max_inactive`, `glow_max`; default: `text_active`, `text_inactive`, `glow`;
`glow_max = none` drops the TextGlow). The msstyles: `CompositedWindow` has no text-colour property at all, in 4074 Jade, the patched
Aero and 4093 (every part is a single `PNGSTREAM`/`D3DXSTREAM`, `tools/lhstyle.py ... COMPOSITEDWINDOW`; COMPOSITION.md 19). The
maximised look lives elsewhere in the style: Explorer's header title and breadcrumbs turn white (255,255,255) from navy (0,2,92) with
the white glow dropped, inactive maximised text stays grey (98,100,105), and `screenshots/aero_max.png` shows the white title on the dark
LGFRAMEMAX header. aero and jade: `text_max_active = #ffffff`, `text_max_inactive = #626469`, `glow_max = none`. The text colour and the
glow alpha are interpolated with the M5b crossfade weight (the plate's), so they fade with the plate in both directions. Jade's
LGFRAMEMAX is its normal green plate, so the white title sits on green there (as instructed; the data has nothing Jade-specific).
msstyles themes: the keys apply to MAXCAPTION, but no shipped style defines a MAXCAPTION text colour (`Window.MAXCAPTION(ACTIVE)` has only
`TEXTSHADOWCOLOR = 0 0 0`, `TEXTSHADOWTYPE = CONTINUOUS` in 4074 Jade and Slate and 4051 Slate; the converter already writes that shadow
as `maxcaption.shadow.1`), so they keep CAPTIONTEXT.

**Fonts** (`docs/history/THEMES.md` "Fonts"): the registry's `fonts = DIR[;DIR]` (frame.c `theme_registry_lookup` → `frame_fonts_set`) plus the
theme.ini `font_files` (now files or directories) are registered at every theme load as the process's fontconfig application fonts,
replacing the previous set (`FcConfigAppFontClear`, then `FcConfigAppFontAddDir`/`AddFile` on the current config, then pango's default
font map is dropped; only when the set changed). `LHWM_DEBUG=1` logs the resolved face per theme load, e.g. `caption font "Segoe UI 9" ->
"Segoe UI 9" (/u/aap/fun/longhorn/wim/4074/3/WINDOWS/Fonts/seguibk.ttf)`; jade4074-classic's `Segoe UI,Tahoma 12px` now resolves to
Segoe UI too, Luna to Trebuchet MS Bold. Before, "Segoe UI 9" (aero, jade) was DejaVu Sans. `compare_fonts.png`.

**Regression** (`tests/regress.sh` against the M5b reference, before regenerating it): 113 identical, 15 changed: aero, jade and
jade4074-classic `strip`, `normal`, `unfocused`, `max`, `ext60` (`.png` only): Segoe UI 0.7 instead of DejaVu Sans / Tahoma
(`FRAMETEST_FONTS`), and aero/jade `max` also the white maximised title without glow. All `.txt` (margins, shape, hit columns), all
NO_TITLE renders (`ext46*`) and every Slate and Luna render are byte-identical. The frame.c changes alone (icons, keys, font plumbing)
gave 128 identical; the two new theme keys alone changed only aero/jade `max.png`. `tests/ref` was regenerated afterwards.

## Extended frames (the Explorer tall caption, `CLIENT-PROTOCOL.md`)

A cooperating ARGB client sets `_LHWM_EXTEND_FRAME` (`left right top bottom`, client pixels) and `_LHWM_FRAME_FLAGS`
(`NO_TITLE`, `NO_ICON`, `NO_FOG`). luxwm then paints the plate further in under the client, and the client leaves that part
transparent. The reference client is `../examples/extendframe.c`, and `compare_extend.png` shows it next to the 4074 Documents window.

- `frame_paint_ex`/`frame_paint_cr_ex` take a `struct frame_ext`. NULL, or all zero with no flags, is the old code path,
  untouched: frametest output stays byte-identical for aero, jade and the three classic themes, normal and maximised,
  focused and not.
  `plate_ext` paints the plate on a 7x7 grid: per axis, outer edge | band (stretched by the extension) | inner lines |
  middle | inner lines | band | outer edge. The inner dark/light lines move in by the extension. The caption rows that stretch
  come from the theme key `extend_rows` (aero: `15 25`, below the baked-in button pill). `fog_band` paints `captionfog.png`
  over the caption row and the extended top unless `NO_FOG`. Maximised: the same, on the `w x (h + bottom margin)` plate,
  then `square_corners` as in M5a.
- Only while composited (compositor present and a DWM theme). Otherwise the frame is the normal one with title and icon,
  and the client paints its header opaque. Root `_LHWM_COMPOSITED` (0/1) and `_LHWM_FRAME_METRICS` (`caption_h border_l
  border_r border_b text_rgb glow_rgb inactive_text_rgb 0`, since the second round of 2026-10-24 followed by `max_caption_h
  max_border_l max_border_r max_border_b border_t max_border_t`, since 2026-10-25 by `max_text_rgb max_inactive_text_rgb max_glow_rgb 0`
  (`text_max_active`, `text_max_inactive`, `glow_max` of the current frame theme, 0xFFFFFFFF = no glow, for flxp's Explorer header that
  flips its text when zoomed; aero4074 `... 0xffffff 0x626469 0xffffffff 0`): 18 items) are written on change by `composited_publish()`, called from
  `theme_select()` (compositor appeared/gone, runtime theme switch) and deleted on exit. Extended clients are repainted
  when `_LHWM_COMPOSITED` changes. The client properties are read at map time and on every PropertyNotify, deletion included.
- `_NET_WM_MOVERESIZE` (in `_NET_SUPPORTED`): `drag_run()` is the edge/corner drag of the frame (`frame_resize_drag` calls
  it), and moves go through `drag_move_step()`, which the caption/Super drag loop uses too. Directions 0..7 resize (ignored
  while maximised), 8 moves, 9/10 size/move with the keyboard (Windows' keyboard Size and Move modes, "The window menu"), 11 cancels. Escape also cancels a keyboard drag. Tested on
  the lab with xdotool: header drag (+100,+90), double-click maximise and restore through `_NET_WM_STATE`, right-edge
  resize (+120 px), cancel mid-drag (window back), keyboard move (arrows, Return), and Escape. berry's old
  `NetWMMoveResize` slot is `_NET_MOVERESIZE_WINDOW` and still works.
- `tests/frametest`: `FRAMETEST_EXT="l r t b [flags]"` renders an extended frame (`-1 -1 -1 -1` = sheet of glass). For an
  msstyles theme it now renders what the header pixmap is cut from (caption band continued by `t`), so the classic `ext` renders
  changed on 2026-10-24. Regression set after that day's changes (strip, normal, unfocused, maximised, `0 0 60 0` extended for
  aero, jade, jade4074-classic, slate4074-classic, slate4051-classic: 25 renders): 19 byte-identical (every non-maximised,
  non-extended render and all of aero/jade); the 3 classic maximised (MAXCAPTION fix) and 3 classic extended renders changed on
  purpose. The same 5 renders exist for luna-blue, luna-olive, luna-silver.
- Without composition (msstyles themes, with or without picom), 2026-10-24: the decoration window keeps the normal frame (the
  extension does not change it; `NO_TITLE`/`NO_ICON` apply), and `header_update()` (wm.c, called at the end of every
  `draw_text`) publishes `_LHWM_HEADER_PIXMAP` on the client: a pixmap of the client's depth and visual (created on the root, so
  a client destroyed meanwhile cannot make `XCreatePixmap` fail; a BadDrawable there killed the lab luxwm once), client width x
  (top frame margin + header rows), painted by `frame_header_image()` (frame.c): the whole frame painted with the extension, in
  which `ms_paint` nine-grids the CAPTION (maximised: MAXCAPTION) bitmap over `caption + extension` rows and starts the side
  frames below it, title and icon left out, cropped to the client's columns and rows 0..band. Its last row lies against the
  bottom of the client's header. Rendered again only when a key over size, focus, maximised state, extension, theme generation
  (bumped by every theme load) and, with `CLIENT_CAPTION`, button hover/press changes; new pixmap, property replaced, old one
  freed. Deleted when composited, when the extension goes, on unmanage and on a clean exit. `examples/extendframe` copies it.
- `CLIENT_CAPTION` (bit 3, experimental): `margins_for()` cuts the top margin to `theme_caption_border()` (msstyles cnBorders,
  DWM the side border `m_l`, maximised 0), so the client covers the caption; geometry, `_NET_FRAME_EXTENTS`, drags and theme
  switches all go through it. A new window keeps the frame size of a normal one and its client grows by the caption; toggling the
  flag at runtime keeps the frame and resizes the client; adoption after a WM restart puts the frame around the client as it is.
  `_LHWM_CAPTION_RECT` (client coordinates, the union of the button hit rectangles from `frame_buttons_rect()`, clipped to the
  client) is written on change and on every maximise/restore. The decoration window under it is unchanged, so the buttons and their hit-testing stay luxwm's
  and work through the client's input-shape hole. Composited, the extension's top is measured from the client's top as before,
  and the plate band is stretched from the normal caption bottom to there. Tested on the lab (Luna and aero4074, `extendframe
  -c`): hover lights the button in the header pixmap, the max/restore button maximises and restores through the hole (the
  maximised header is MAXCAPTION, the caption rect follows to x 1062), a press in the client's caption zone moves the window via
  `_NET_WM_MOVERESIZE`. Screenshots: `screenshot_luna_extendframe.png` (Luna: plain extended window with the header pixmap,
  inactive, and the `-c` window, active), `screenshot_aero_clientcaption.png` (the same two composited).
- Second round (2026-10-24, flxp requests; CLIENT-PROTOCOL.md "Second round"):
  - CLIENT_CAPTION height drift over theme switches: `client_retheme` kept the client *window* rectangle (caption rows included),
    the flag-toggle path the frame; a client toggling the flag per theme drifted by the difference of the themes' `caption_h -
    border_t` (flxp: 418, 398, 416, 390, 408; reproduced exactly with xclock and xprop). Now the switch keeps the logical rectangle
    (frame minus the normal margins, `theme_margins_ex` of old and new theme) and only then places the window with `margins()`.
    Lab: `extendframe -c` over 11 switches among aero4074/jade4074/slate4074/luna is 438/438/440/446 rows high per theme every time,
    its bottom edge fixed; the xclock toggle sequence alternates 418 (flag on, aero/jade) and 400 (off) with the logical rectangle fixed.
  - `_LHWM_FRAME_METRICS` items 8..13 (maximised margins, `border_t`, `max_border_t`). `_LHWM_CAPTION_RECT` is republished on every
    maximise/restore (`cap_rect_mono`), after `_NET_WM_STATE` (`ewmh_update_state` moved before the paint in `client_apply_max`,
    `client_unmaximize`, `client_unmax_margins`); `header_hold` keeps the intermediate not-yet-maximised paint in `client_apply_max`
    from publishing. extendframe -c: caption zone 18 -> 27 rows on aero when maximised (`_LHWM_EXTEND_FRAME` top 78 -> 87).
  - `client_hit()` (wrapping `frame_hit_ex` for all three call sites): with CLIENT_CAPTION, not maximised, every border row above
    the client resizes (DWM rows 6..8 were a move strip); clients can also send `_NET_WM_MOVERESIZE` direction 1 from their header
    (extendframe -c: top 4 rows).
  - `_LHWM_FRAME_FLAGS` bit 4 `NO_HEADER_PIXMAP` (`FRAME_NO_HEADER_PIXMAP`, masked out of the painter's flags): `header_update` deletes
    the property and frees the pixmap when set, renders again when cleared. The opaque-client paint hole is off for CLIENT_CAPTION
    clients (the buttons lie under them; extendframe -c -n shows them through a bounding-shape hole).
  - `tests/regress.sh`: 128 identical, 0 changed (frame.c only got the enum value; `frame_hit_ex` is untouched); `tests/ref` not
    regenerated.
- Fixed on the way: luxwm selected PropertyChangeMask on a new client only after mapping it, but publishes `_LHWM_CAPTION_RECT`
  while setting it up; a client answering at once (extendframe -c sets a taller `_LHWM_EXTEND_FRAME`) was missed, and the header
  pixmap stayed short by the caption zone. PropertyChangeMask is now selected before the client's properties are read.
- Explorer plate (2026-10-24, COMPOSITION.md 15): see "Explorer plate" below; with it the caption band of an extended aero/jade
  frame is no longer the normal plate's rows 15..25 stretched.
- picom (`../picom-glass.conf`): `"_LHWM_EXTEND_FRAME@"` in `blur-background-exclude` and `shadow-exclude`. `picom
  --log-level debug` confirms it: `c2_match_once ... result = 1, client = 0x00e00002, pattern = _LHWM_EXTEND_FRAME@[0]`,
  then `Blur background disabled by blur-background-exclude`.

### Explorer plate (`plate_explorer`, `explorer_insets`, `explorer_rows`)

4074's desksrv has a separate part for Explorer windows, `LgFrameExplorer` (theme STREAM 484, desksrv's built-in 112), 9-gridded with
texture margins (9, 73) (100, 9) instead of LgFrame's (9, 27) (100, 9) (COMPOSITION.md 15.1). `themes/aero/plate_explorer_active.png`
and `plate_explorer_inactive.png` are the two 801x607 halves of `aero.msstyles` STREAM 484 (`res/aero/aero.msstyles/STREAM_484_1033.png`,
the same file the normal plates are the 483 halves of; active top, inactive bottom, so there is a real inactive part). Jade's STREAM
484 (`res/4074/jade.msstyles/`) is byte-identical to its 483, a flat green plate; it is extracted the same way so jade uses the same
code path, and its renders look like before apart from `glass_alpha`.

Measured on the aero texture: the 73-row top band (rows 0..72) is the tall caption gradient (alpha rising from ~75 to ~150 in the
middle columns, the pill in rows 2..14 of the right 100 columns, the same place as in 483); the gradient goes on to row 77, the
inner light/dark lines are rows **78/79** and the texture's client area starts at row 80; left/right/bottom are the normal plate's
9/100/9 px. theme.ini:

    plate_explorer = plate_explorer_active.png plate_explorer_inactive.png   # one file: used for both states
    explorer_insets = 73 100 9 9        # top right bottom left (default: plate_insets)
    explorer_rows = 15 71 78            # band rows [15, 71) stretched, inner lines at rows 78/79 (default: extend_rows start, top-2, top-2)

It is used for an extended frame whose top reaches into the client (`_LHWM_EXTEND_FRAME` top > 0, not the sheet of glass), composited
or in frametest; everything else (normal frames, left/right/bottom-only extensions, the sheet, msstyles themes) is unchanged. With
header height H = caption (27) + top, `plate_explorer()` (frame.c) maps the rows as: 0..14 1:1 (pill), band 15..70 stretched to
15..H-3, the texture's lines 78/79 onto rows H-2/H-1 (right above the client's content, like the normal plate's 25/26), then the body
from row 80 and the bottom edge as `plate_ext`. **A 46 px extension (H = 73) copies rows 0..70 1:1** (checked: frametest `ext46`
against the texture, max difference 1 level in the 1:1 columns); other heights stretch only the band's middle rows 15..70 (60 px:
56 rows into 60; 10 px: 56 into 22). Columns as `plate_ext` (9 and 100 px kept, the middle stretched with the window width, as
desksrv's 9-grid does). The texture's own lines at 78/79 are where the reference has its lines too: the 4074 Documents window has a
77-row header (frame top y=74, light/dark lines at y=149/150, menu bar from 151), i.e. desksrv's 9-grid squeezes rows 73..79 into
73..76 there; a client that wants that exact geometry extends by 50.

The caption fog is now off by default (theme key `fog = 1` re-enables it; clients' `NO_FOG` is then moot): the fit below shows that the
reference has none (COMPOSITION.md 15), and with the thinner glass the fog dominated the error. The shipped aero theme uses
`glass_alpha_mode = square` with `glass_alpha = 1.0` (the parameter-free 5048 equation, header rmse 5.6) instead of the 0.5 factor (6.3);
jade used it too until it was fitted to 4066 (straight alpha, "Glass accuracy").

Regression (`tests/regress.sh` against `tests/ref/`, generated before these changes; 8 themes x 8 renders x png+txt = 128 files): 107
identical, 21 changed, all aero/jade: `strip`, `normal`, `unfocused`, `max` (glass_alpha; `normal.txt`/`unfocused.txt` of aero: shape 8 ->
7 bands), `ext60`, `ext46`, `ext46u`, `ext46max` (Explorer plate + glass_alpha; aero `ext46.txt`, `ext60.txt` and jade `ext60.txt` shape).
Every classic and Luna render is byte-identical, and aero with `glass_alpha = 1.0` gives byte-identical `strip`/`normal`/`unfocused`/`max`.

Regression after M5b and the Jade fit (2026-10-24, against the reference above, before regenerating it): 117 identical, 11 changed:
aero `max.png`, `ext46max.png` (maximised plate, `max_button_right`); jade `strip.png`, `strip.txt` (hit columns 49/27/29 -> 47/25/27:
`button_gap 0`), `normal.png`, `unfocused.png`, `max.png`, `ext60.png`, `ext46.png`, `ext46u.png`, `ext46max.png` (straight alpha, no
overlay, buttons; max also the maximised-plate path). Every classic and Luna render and every non-maximised aero render is byte-identical;
the frame.c changes alone (before the theme.ini keys) changed nothing (128 identical). `tests/ref` was regenerated afterwards.

Comparison `compare_explorer_plate.png` (lab on Xvfb, picom xrender, wallpaper stretched as on the reference, `examples/extendframe`
inactive with its frame at the reference's 245,74, 810 px wide, extension 50): reference / before (normal plate, rows 15..25
stretched) / after (Explorer plate, glass_alpha 0.5, fog) / after with `NO_FOG`.

### Aero vision (`themes/aerovision`, `header_mode`, `deco_h`, `caption_border`, `button_files`; 2026-10-27)

The experimental frame for the concept-art navigation bar (`NAVBAR.md`, `../luxnav/`, `../examples/navdemo.c`;
CLIENT-PROTOCOL.md "Vision header"). `themes/aerovision` started as a copy of `themes/aero` with aap's tuned values (plates, insets,
`button_right = 5`, `button_gap = 0`, glass) and adds:
- `header_mode = crop` (frame.c `crop_rows`, used by `plate_explorer` and the MAXEXPLORER branch of `plate_max`): the Explorer
  plate's rows are copied 1:1 down to the header's last two rows instead of stretching the band [15, 71); the texture's inner line
  pair (rows 78/79: light, then black) goes on the header's last two rows as before. aap's idea ("we never scale the tall titlebar
  plate vertically ... redraw a 1px black frame on top and use the inside for the client area"; what 4074 apparently tried). A
  header taller than the band (more than 80 rows) repeats the band's last row.
- `deco_h = 15`: the frame's decoration ends on row 15, right under the buttons. `frame_hit_ex` and `frame_buttons_rect` clip the
  buttons' hit slop to it (so `_LHWM_CAPTION_RECT` ends there too); wm.c publishes it as `_LHWM_FRAME_METRICS` field 18
  (`METRICS_N` 19; other themes: `caption_h`).
- `caption_border = 2` (`theme_caption_border`): a `CLIENT_CAPTION` client starts on frame row 2, so it can draw a small title with
  the baseline on row 15 (with the default side border, 9, its top 3..4 rows of glyphs would lie above the client).
- `button_files = vision_close.png vision_max.png vision_min.png vision_restore.png`, `button_frame_h = 13`: 13-row caption buttons
  (rows 2..14, ending with the plate's pill and the deco). `tools/build_aerovision.py` makes them from the aero sprites by dropping
  sprite row 11 (the light line above the dark bottom line; `--drop-row N` to try another) and keeping the widths, which fill the
  pill exactly (27 + 25 + 43). The key exists because `close.png` etc. are replaced by the style's LGCLOSEBTN/... streams in library
  mode (`fut_png`); names outside that map always come from the directory.
- `nav.ini`: not luxwm's: luxnav's tuning (presets `[preset small]` / `[preset big]` with the variant, orb size, hand placement,
  fonts, colours, glow; `NAVBAR.md` "Presets"), read by luxnav clients from the directory of `_LHWM_THEME` and again on every
  reload, so `.session/theme-reload.sh` / `.session/theme-watch.sh` (which watches `nav.ini` too) live-update the header. The header height is
  the client's (`_LHWM_EXTEND_FRAME` top); `header_mode = crop` handles any height (checked on the lab: small 47 rows, light/black
  line on frame rows 47/48; big 69 rows, lines on 69/70, body from 71; up to 80 rows the texture's band is copied 1:1).

Everything else (normal windows, maximised, no compositor: `fallback_theme = jade4074-classic`) is aero's, apart from the 13-row
buttons. Regression (`tests/regress.sh`, lib source): 149 identical and the 11 known-stale aero5112 files (`ext46`, `ext46u`,
`ext60`, `normal`, `strip`, `unfocused` png/txt: stale before this change, reason unknown, `tests/ref` not regenerated); aerovision's 16
standard renders plus `vision49`/`vision49max` (new in regress.sh, only for `header_mode = crop` themes: the navdemo header,
`FRAMETEST_EXT="0 0 22 0 9"`) were added to `tests/ref/aerovision` as new files (169 identical with them). `--compare-sources`:
dir and lib identical for every theme, aerovision included.

## Glass accuracy

Two fits, one per theme (2026-10-24): **aero** is fitted to the patched 4074 screenshot `screenshots/4074-dwm.png` (its textures are the
5048 art of the aero zip, COMPOSITION.md 15; everything below up to "Jade" is that fit, unchanged), **jade** to build 4066's
`screenshots/4066-MIL-Effects.png` (the shipped Jade plate through desksrv's fixed-function path, COMPOSITION.md 18; "Jade" below).

Measured against `screenshots/4074-dwm.png` (wallpaper = `mock/assets/wallpaper/longhorn.jpg` stretched to 1280x1024) and `themes/aero`:

- **Blur.** SS_GLASSSHADER = 3x3 taps at 1.5 px = Gaussian sigma ~1.2 px. Per-row affine fit of the Documents glass (y 100..148) against
  the wallpaper blurred by sigma 0..8: rmse is minimal at sigma 1.2 (14.0 vs 15.0 unblurred, 15.7 at 3, 18.9 at 5, 23.8 at 8; the
  14 floor is JPEG/scale mismatch, the bare desktop beside the window gives the same). The old settings were far too strong:
  gaussian 10x5 = sigma ~4.5, dual_kawase strength 6 = sigma > 8 (kawase strength 1/2/3 measure sigma ~1.9/3/6 on the lab).
  Now: glx `blur-strength = 1` (the mildest level), xrender lab `--blur-size 5 --blur-deviation 1.2` (measures sigma 1.25).
- **Alpha.** `plate_active/inactive.png` are straight alpha: 99.4% of the translucent pixels have rgb > alpha (teal 118,145,153 at alpha 60),
  and over the Documents frame body (plate rows 29..75) the straight blend `a*rgb+(1-a)*bg` matches the screenshot best (rmse 18.8; the
  "premultiplied" shader form `rgb+(1-a)*bg` saturates, rmse 97; double-darkened 36). So cairo's straight-alpha load is right; no
  `premultiplied` theme key was added and frame.c is unchanged.
- **Shadow.** `shadow.png` (860x664, plate 801x607 sits at +41,+19, the hole under the plate is transparent like picom's clipped shadow):
  peak alpha 0.53 at the plate edge, half value 17 px out on the left and bottom, 10..90% spread ~26 px (sigma ~10), but only a faint
  tail on the right (<0.05) and top (<0.12): a plate-sized rect shifted ~15 px left and ~13 px down. picom: radius 22, opacity 0.52,
  offsets -33/-6 reproduce the bottom profile within 3% (0.50/0.48/0.45/0.40/0.34 against 0.48/0.47/0.44/0.40/0.35 at d = 1,4,7,10,13).
- **Double shadow.** luxwm sets `_LHWM_DECORATED` (CARDINAL 1) on every client that has a decoration window (before mapping; removed
  when the decoration goes), and `picom-glass.conf` excludes `"_LHWM_DECORATED@"` from shadows (picom `--log-level debug`: "Shadow disabled
  by shadow-exclude" for the clients, the decoration windows keep theirs). `compare_glass.png` shows reference / before / after.
- **Glass weight (`glass_alpha`, `glass_alpha_mode`; 2026-10-24, COMPOSITION.md 15.3).** The reference glass is about half as opaque as
  the plate textures say. `glass_alpha` (default 1.0; aero and jade were 0.5 at first, now both 1.0: aero squared, jade straight) multiplies the plate's alpha; `glass_alpha_mode = square` (default
  `linear`) squares it first (a -> a^2, the 5048 `BlurUnder_Glass` equation with premultiplied textures; meant with glass_alpha 1.0). Only the
  plate (normal and Explorer) is weighted: the overlay, buttons, glows, fog and text keep their alpha. It is applied when the plate is
  composited (`plate_draw()`: `cairo_paint_with_alpha`, or the plate masked by its own alpha for `square`), not to the loaded textures,
  because the plate's raw alpha is also the overlay's mask; the window-shape threshold (`shape_alpha`) is mapped through the same factor
  so the corner cut stays where it was (it moves by at most 1 px in 4 corner rows: aero 8 -> 7 shape bands, jade's 60 px extended frame
  5 -> 4).
  Fit (Documents window of `4074-dwm.png`, inactive, header rows 2..72 at x 560..900, rmse in levels 0..255; wallpaper stretched to
  1280x1024 and blurred sigma 1.2, 6 levels on bare desktop; the lab screenshots give the same numbers as offline frametest renders
  composited over that background):

  | | texture model (484 inactive rows 1:1) | luxwm, fog (extendframe default) | luxwm, `NO_FOG` |
  | --- | --- | --- | --- |
  | before: normal plate, rows 15..25 stretched | - | 52.5 | 25.7 |
  | Explorer plate, glass_alpha 1.0 | 30.3 | 55.1 | 29.2 |
  | Explorer plate, **glass_alpha 0.5** (aero/jade default) | 6.4 | 37.4 | **6.3** |
  | Explorer plate, `glass_alpha_mode = square` | 5.2 | 31.0 | 5.6 |
  | best free factor | 4.3 (x0.40) | | |

  So the ~7 levels of COMPOSITION.md 15.3 are confirmed (6.3) for a client with `NO_FOG`; the fog adds ~30. The squared alpha is slightly
  better here (5.2/5.6) than 15.3 reported (10.6; that fit's exact setup was lost with /tmp, this one is in the scratchpad `fit/`); both
  are within a level or two of each other, and the free-factor optimum is 0.40.
- **Remaining differences.** The fog (above). Glx dual_kawase cannot go below sigma ~1.9. The shadow top tail is weaker than the original (same-size rect).
- **Jade (fitted to 4066, COMPOSITION.md 18.1/18.6).** The 4066 Explorer window (frame 810x600 at 33,51, 77-row header, client from
  42,128) shows one flat plate colour, **(104,138,121)**, over the flat (58,110,165) wallpaper: the theme's (112,144,112) at alpha 213/255 =
  0.835 blended straight, no squared alpha, no 0.5 factor, no overlay, no fog, buttons min 27 + max 25 + close 43 abutting, 2 px below
  the top, ending 6 px before the right edge. `themes/jade/theme.ini`: `glass_alpha = 1.0`, `glass_alpha_mode = square` removed,
  `overlay = 0` (new key: `overlay.png` is not loaded), `fog = 0` (as before), `button_gap = 0`, `button_right = 6`, `button_top = 2`.
  Check (scratchpad `m5b/tools/fit66.py`): frametest at 810x600 with `FRAMETEST_EXT="0 0 50 0 1"` composited straight over the flat
  wallpaper, against every frame pixel of the screenshot that lies over flat wallpaper (outside the client and the button strip, 72198
  px): before mean error 15.2 levels (our (96,133,129) vs (104,138,121)), **after 99.95 % within 1 level, our value (104,138,122)**; the
  caption-button strip (x 742..836, y 53..66): rmse 19.3 / max 136 before, **0.57 / max 1** after. The 34 pixels off by more than 1 are
  the four corners: 4066's texture has 2-row rounded corners top and bottom, 4074's (ours) a 4-row top corner and square bottom corners
  (COMPOSITION.md 18.1: the textures differ only in the corner antialiasing); switching to the 4066 texture would fix them, not done
  (the theme is 4074 Jade). `compare_jade_4066.png`: screenshot / before / after (half size, plus 4x zooms of the button corner and the
  bottom-left corner; the client area of the renders is pasted from the screenshot). The drop shadow (texture outsets left 40, top 20,
  right 19, bottom 38) is picom's here. picom's blur stays for both themes: the real shader blurs 3x3 taps at 1.5 px when ps_2_0 is
  present, and the 4066 screenshot machine evidently had none (un-blending recovers the ClearType fringes of the icon text under the frame).

## Performance (interactive resize)

User report: resizing was very sluggish on real hardware with picom glx. Profiled on the lab (`LHWM_PROF=1 LAB_WMARGS= session/lab.sh
restart-wm`: one stderr line per drag with the step and frame_paint timings; each step is followed by an `XSync` only while
profiling, so the step time includes the X server's work). Test: one xdotool run drags the right frame edge 100 x 3 px out and back
(200 motion events, ~4.5 ms apart) on xterm (442x274 frame) and on `examples/extendframe` (806x536, ARGB + extended frame).

| | before | after |
| --- | --- | --- |
| xterm: steps applied in the ~930 ms drag | 13 | 40..49 |
| xterm: frame paints during the drag | 145 | = steps (40..49) |
| xterm: one frame paint (cairo / upload) | 1.8 / 4.0 ms | 1.0 / 2.4..4.6 ms |
| xterm: luxwm busy per drag (paints) | ~850 ms of 952 | ~170..240 ms |
| extendframe: steps / paints | 4 / 49 | 23..28 / same |
| extendframe: one paint (cairo / upload) | 7.1 / 15.2 ms | 7..8 / 13..19 ms (full paint, see below) |

The shape (`frame_shape_rects`) measured 0.00..0.04 ms per paint and is only sent when it changes, so it was left alone (the
suggested per-theme precomputed corner cuts would save nothing measurable).

What made it slow, and the fixes:
- **Expose storms.** Every resize of the decoration window (ForgetGravity, background pixel) exposes it, the shape change exposes it
  again, and each Expose did a full `frame_paint`: 145 paints for 13 resize steps. Now an Expose is ignored when its serial is older
  than the first upload request of the last paint (`frame_upload_serial`: the paint that followed the resize covers it); newer ones
  only mark the frame dirty, and `paint_dirty()` repaints once when the event queue is empty (main loop, and inside the drag loop
  before it blocks).
- **Unthrottled steps.** `drag_run` coalesced motion already, but applied every batch at once. Steps are now applied at most every
  `DRAG_INTERVAL` ms (config.h, 16); a position held back is applied when the interval is over (poll with a timeout on the X
  connection) or when the drag ends, so the final geometry is exact. Super/Alt drags (berry's loop in `handle_button_press`) now
  coalesce motion too.
- **Painting what nobody sees.** The plate under an opaque client (depth != 32, no `_LHWM_EXTEND_FRAME`) is covered by the client.
  `frame_paint_ex2` takes that hole (client rectangle, kept clear of the 24 top/bottom rows the shape scan reads), clips cairo to the
  complement (pixel-aligned, so the band pixels are identical: checked for aero, jade, jade4074-classic, slate4074-classic, normal and
  maximised, 0 pixels differ) and uploads only the four bands with plain `XPutImage`. ARGB clients and extended frames still get the
  whole image through cairo-xlib (MIT-SHM; a plain full-size `XPutImage` measured slower, 28 vs 17 ms).
- Double-click on the caption (flxp report: maximises, but a second double-click does not restore). Cause: the first press of the
  second double-click runs berry's move loop (`handle_button_press`), and any MotionNotify while the button is down, even 1 px of
  jitter, called `drag_move_step` → `client_move_absolute` → `client_unmax_margins`, which drops the maximised state (without
  restoring the size); the second press then saw a non-maximised window and maximised it again, writing the full-screen size as
  the restore geometry. xdotool's jitter-free double-clicks never showed it; `mousedown, +1 px, mouseup, mousedown, mouseup`
  reproduced it on the lab, slate4074 and aero alike. `frame_hit_ex` while maximised was correct. Fix: a press only becomes a move
  once the pointer leaves a `DRAG_THRESHOLD` (config.h, 4 px = SM_CXDRAG) square, in berry's loop and in `drag_run` (caption
  moves by `_NET_WM_MOVERESIZE`, so a jittery double-click in an extended header behaves the same; resizes are unaffected).
  After the fix: jittery and clean double-clicks maximise and restore to the old geometry; a real drag still moves by the full
  pointer delta (+60/+45 checked).
- Kept: client and frame are resized together in every step (no timer-driven client resize: with the steps capped at 60/s the
  frame and client never disagree, and a lagging client would show the plate's glass through the gap).
- `tests/frametest` output is byte-identical before/after (aero, jade, the three classic themes; strip, normal, unfocused,
  maximised and extended frames: 25 renders, 0 differ).

### Resize without flicker

User report (real hardware, picom glx): "resizing the window makes the content flicker; keep the old content until the resized one
is ready". Measured on the lab (:3, picom xrender, root set to a solid green): an XTest drag of the right edge by 300 px in 1 s
(250 motion events), the root grabbed every 20 ms (70 shots, ~50 of them during the drag), each shot classified on two rows of
the client: client colour up to the frame's right edge, then the border, then the desktop; a shot is bad when anything else shows
(the desktop or a stale border inside the client's area, the frame missing a band, client and frame of different widths). Test
clients painting solid magenta: xterm, `examples/extendframe` (ARGB, white body), a GTK 3 and a Qt 5 window (PyGObject, PyQt5),
also slowed down by 15 / 40 ms per paint (a busy real application). A second probe connection reads back, on every
ConfigureNotify, what a compositor could show at that instant: the frame's right border (painted at its current size?) and the
client's new strip (painted yet?).

What flickered:
- **The client's new strip.** GTK 3 and Qt 5 toplevels have background None (and NorthWest bit gravity already). Under Composite a
  None window's newly added area is a copy of whatever the screen showed there: the old frame border, the desktop. Until the client
  has painted the strip (every step: 55..61 of 61 probes), picom shows that: the stale border repeated or the wallpaper inside the
  window, at the right edge of a growing window. With a client that keeps up it is rare in the grabs (0..1 bad shots); a client that
  needs 15..40 ms per paint shows it in nearly every frame (47..54 of 70). xterm (Xt) has a background pixel, its own colour: no
  strip artefact at all. This is the flicker of the report.
- **The frame** was resized, then painted on the CPU, then uploaded; the decoration window had ForgetGravity and a background pixel
  of 0, so between the resize and the upload it is fully transparent. In the lab the X server mostly carried resize and upload out
  together (0..4 of 122 probes caught the frame unpainted at its new size); with the decoration window resized before the client
  it was 17..29 of 61, i.e. it depends on where Xlib's buffer happens to be flushed. Even with the upload staged, without a grab
  picom caught the client resized but the frame's right band not yet copied (the right border missing: 5 shots in 8 drags).
- `extendframe` (ForgetGravity, ARGB) loses its whole content on every resize; it repaints fast enough that the lab never showed it,
  a slower ForgetGravity client would blink entirely transparent (the glass plate showing through).

What luxwm does now (wm.c `drag_client_begin` .. `drag_client_end`, `geom_paint`, frame.c `frame_pre_upload`):
- **Order of a step.** The frame is painted at the new size first and uploaded into an offscreen pixmap; then, under a short
  `XGrabServer`, client and decoration window are resized (client first; a left/top edge is one `XMoveResizeWindow` each, no
  longer a move then a resize), the shape set and the pixmap copied into the frame (the four bands, or all of it for ARGB/extended
  clients). Upload first, then `XSync`, so the grab covers only those few small requests. This applies to every
  `client_resize_absolute`, not only drags.
- **Decoration window:** NorthWest bit gravity and no background (`CWBackPixmap` None instead of `CWBackPixel` 0): a resize keeps
  the old frame pixels instead of clearing the window.
- **Client bit gravity:** a client with ForgetGravity (`extendframe`, plain Xlib programs) gets NorthWest for the duration of the
  drag, so its old pixels stay; afterwards its own value again and, as it only got Expose events for the new areas, one synthetic
  full Expose. Xt, GTK and Qt toplevels already use NorthWest (xlogo/xclock/xeyes, too: their ForgetGravity windows are children,
  which luxwm does not touch).
- **Client background:** a background of None is the strip flicker, so the remedy is the opposite of clearing it: when the drag
  starts luxwm reads the client's right-edge pixel; the first step that grows the client by 4 px or more tells None (the strip is a
  copy of the screen: two points of it equal the screen pixels read just before the resize, both inside the server grab) from a
  background pixel; a None client then has its own edge colour as background pixel while the drag lasts and None again afterwards.
  Clients with a background pixel are left alone: X cannot read a background back, so luxwm could not restore it. Only opaque
  TrueColor clients in the root's pixel format (an ARGB client's new area is transparent, the glass shows through, which is fine).
  Background None during the drag, as for the frame, was measured too: no change for GTK/Qt (already None) and nothing better for
  xterm, so it is not done.
- **`_NET_WM_SYNC_REQUEST`** (EWMH basic frame sync, in `_NET_SUPPORTED`): for a client listing it in `WM_PROTOCOLS` with a
  `_NET_WM_SYNC_REQUEST_COUNTER`, each step that changes its size is preceded by the request with the next counter value, and the
  next step waits (XSync alarm) until the client has set the counter, at most `SYNC_WAIT` (50) ms, besides `DRAG_INTERVAL`. GTK and Qt
  answer it (the xterm here does not list it); a client needing 40 ms per paint now gets 26 steps per second instead of 61 resizes it
  cannot draw. It does not by itself remove the strip (the client still paints after the resize; picom has no frame sync).

Bad shots of 70 per 1 s drag (three drags each; probes: client strip found unpainted right after a resize, of ~61):

| client | before | after | strip probe before / after |
| --- | --- | --- | --- |
| xterm | 1, 0, 0 | 0, 0, 0 | 0 / 0 |
| extendframe (ARGB) | 0, 0, 0 | 0, 0, 0 | 1 / 0 |
| GTK 3 | 0, 1, 0 | 0, 0, 0 | 55 / 1 |
| GTK 3, 15 ms per paint | 51, 50, 51 | 1, 0, 0 | 61 / 2 |
| GTK 3, 40 ms per paint | 54, 54, 53 | 2, 4, 2 | 61 / 2 |
| Qt 5 | 1, 1, 1 | 0, 0, 0 | 61 / 3 |
| Qt 5, 15 ms per paint | 51, 47, 51 | 1, 1, 1 | 61 / 2 |

The remaining bad shots are the first step (the 1 px before the background is known) and the frame of the button press/release.
Frame unpainted at its current size: 0 of ~120 probes per client. Responsiveness: the same number of steps as before (xterm 61..62,
extendframe 38..39 per 1 s drag, `LHWM_PROF`), the step time unchanged (xterm ~1..2.5 ms, extendframe ~20 ms, the server's ARGB
upload); the staging costs one server-side copy of the bands. `tests/regress.sh`: 144 identical (painting is unchanged).

Caveats: a client that sets its own background during the drag gets None at the end of it. A None client whose right edge is not
its background colour (a scrollbar, a coloured sidebar) shows that colour in the growing strip, still better than the copied
desktop. A pixel-background client whose strip happens to equal the screen at both probe points would be taken for None (it is then
given its edge colour and None afterwards); with two points on the glass border that needs a uniform border and a matching colour.
Real hardware may differ from the lab (glx, vsync, and what the screen pixmap holds where picom flips): the strip is the part a
window manager can influence; the client's own repaint time remains, only a compositor with frame sync could hide it completely.

## Size hints (WM_NORMAL_HINTS, 2026-10-25)

User report: "un-painted areas in windows that don't use the full client rect (urxvt at arbitrary sizes)". berry, and luxwm until now,
ignored `WM_NORMAL_HINTS`, so terminals (urxvt, xterm, emacs, mlterm...) got sizes that are not a whole number of character cells and
left a strip below the last row / right of the last column that they never repaint as cells; with NorthWest bit gravity during drags
("Resize without flicker") that strip could keep old pixels. luxwm now does what ICCCM 4.1.2.3 asks, in every path that sizes a client.

- **Reading** (`client_read_hints`): `XGetWMNormalHints` at map time and on every PropertyNotify of `WM_NORMAL_HINTS` (urxvt and xterm
  rewrite it on a font change). Set, changed or deleted: re-read, then the window is refitted (maximised: re-laid out in the work area;
  otherwise resized to its current frame, which rounds it). ICCCM's defaults: no `PBaseSize` → the base is `PMinSize`, no `PMinSize` →
  the minimum is the base; a maximum below the minimum is raised to it. `luxwm -d` logs `hints 0x...: base, inc, min, max, aspect`.
- **Rounding** (`client_hint_size`, as dwm's applysizehints): aspect first (`PAspect`; on the size minus the base when a base other
  than the minimum is given, else on the whole size; the side that is too long is reduced), then `base + n * inc` rounded **down**, then
  the minimum and maximum. Never larger than asked, except for the minimum.
- **Where the client goes** (`client_area`): inside the frame margins at the hinted size. Not maximised, the frame is fitted around it
  (`frame_fit`: frame = client + margins), so a normal frame has no gap; maximised, the frame stays the work area and the client may be
  smaller by the remainder (never larger: the minimum is capped at the work area then). Fullscreen ignores the hints (as dwm does).
- **Paths:** interactive resize, edge and corner drags and `_NET_WM_MOVERESIZE` 0..7 (`drag_resize_step`: the frame size is fitted
  before it is applied; a left/top drag keeps the right/bottom edge exactly and moves the left/top edge by whole cells); the throttled
  `_NET_WM_SYNC_REQUEST` steps are the same steps; keyboard resize (`_NET_WM_MOVERESIZE` 9, `luxwmc window_resize`,
  `window_resize_absolute`) and every other `client_resize_absolute` (ConfigureRequests, the work-area clamp of new windows, snap
  left/right: snap right then keeps the right edge on the work area's); maximise/restore (`client_geom_max`: restore fits the restore
  rectangle); new windows (the initial size, and `CLIENT_CAPTION`'s growth by the caption, go through `client_refresh`, i.e. are fitted
  once; min/max/aspect apply there too); theme switches (`client_retheme` fits the frame; the client's size is normally kept exactly,
  so nothing changes).
- **Maximised remainder.** The X11 way, not Windows' (Windows gives the client the whole area and terminals paint the rest with their
  background): the frame fills the work area, the client is `base + n * inc`, and the frame paints what lies under the remainder (right
  and bottom). DWM themes paint the plate under the client anyway; the hole of "Performance" is now the client's real rectangle, so the
  plate under the remainder is painted and uploaded: the maximised plate's own client-area texture shows there (aero: the dark
  LGFRAMEMAX body, jade: its green glass). msstyles frames leave the client area transparent, so `frame_paint_ex4` (new in frame.c: a
  `client` rect; `ms_gap_fill`) fills the free part of the client area with the frame border's colour (FRAMELEFT drawn in its frame
  state, sampled in the middle: slate4074 a dark grey; button-face grey without the part), as if the border went on around the client.
  `_NET_FRAME_EXTENTS` stays true: it is the frame around the client as it lies, so a maximised urxvt reads `0 4 27 2` (left, right,
  top, bottom) on aero and `0 4 20 9` on slate4074; it is written only when it changes.
- **Fixed size** (`client_fixed`): minimum and maximum equal (a win32 window without `WS_THICKFRAME`: wux writes min == max == its
  size, following every resize it makes itself, and `_MOTIF_WM_HINTS` without resize and maximize; flxp's dialogs, property sheets
  and message boxes). In every frame mode its edges and corners are a plain border (`client_hit`: `HTBORDER`, no size cursor; a drag
  there neither sizes nor moves), Alt+drag resize, `_NET_WM_MOVERESIZE` 0..7 and the window menu's Size do nothing, maximise is off
  (button, double click, Super+Up, the menu's Maximize greyed) and its maximise button is drawn disabled (msstyles: the strip's
  disabled image; classic: `DFCS_INACTIVE`; DWM sprites have none, so the inactive one at 45 %; `FRAME_MAX_DISABLED`, lhwm's own bit
  above the client's `_LHWM_FRAME_FLAGS`).
- **ConfigureRequest** (berry bug, fixed on the way): only the fields in the request's `value_mask` are applied. The others hold the
  geometry the window had when it sent the request, so a client that asked for a size before luxwm had placed it was moved back there
  (`emacs -Q` came up with its client at 0,0 and the frame off-screen). The requested size is fitted like any other.
- **Not refitted, on purpose:** a `CLIENT_CAPTION` toggle at runtime and a theme switch of a `CLIENT_CAPTION` client keep the frame (the
  client grows/shrinks by the caption; rounding that down at every toggle drifted by one row per toggle in the lab), so a hinted
  `CLIENT_CAPTION` client may keep a remainder on the plate until its next resize. No cooperating client sets increments.

Verified on the lab (`:3`, picom xrender, aero4074; `compare_sizehints.png`: before = the previous binary, after = this one):
- urxvt 80x24 (base 16x4, inc 6x13): corner drag +101/+57 → 592x368 = 96x28 cells (asked 597x373); left-edge drag -47 → 634x368, the
  right edge stays at x 991; drags of +3/+9 (less than a cell) change nothing, +9/+20 add a column and a row; keyboard size
  (`_NET_WM_MOVERESIZE` 9, Right x3, Down) +30 px → +5 columns, +10 px → no row; `luxwmc window_resize 17 23` → +2 columns, +1 row.
  Maximised: 1276x771 (210x59 cells) in the 1280x800 work area, extents `0 4 27 2`; before: 1280x773. A font change (OSC 710) re-reads
  the new increments (11x22, then 8x15) and the window keeps its 109x27 cells. Before, the same 625x352 drag left 3 px / 10 px over
  (101x26 cells + remainder).
- xterm 70x20 (base 4x4, inc 6x13): drag +23/+31 → 442x290 (73x22); maximised 1276x771 = 212x59, extents `0 4 27 2`; slate4074
  maximised: extents `0 4 20 9`, the remainder filled dark grey (no transparent strip); theme switches aero → slate → luna → aero keep
  442x290.
- Clients without increments are unaffected: xclock (USSize/USPosition only) right-edge drag +37 → exactly 200x151 (from 163), maximised
  1280x773; `examples/extendframe` (no WM_NORMAL_HINTS at all) drag +33 → exactly 673x420.
- emacs 31 (GTK 3, `-Q`): base 40x83, inc 8x17; maximised 1280x763 (155x40), its bottom 10 px on the plate. GTK clients publish
  increments but paint their whole window anyway (emacs, and GTK terminals such as gnome-terminal / xfce4-terminal, which set
  `PResizeInc` from the cell size; none of those is installed here), so for them the rounding only changes the size.
- The lab never showed urxvt's strip as garbage, before or after (Xvfb + xrender: the server fills a growing urxvt's strip with its
  background pixel, and urxvt clears it on shrink); what the user saw on real hardware could not be reproduced, so the shots show the
  geometry. `tests/regress.sh`: 144 identical (frametest passes no client rect, so the msstyles fill never runs there).

## Animations (config.h `ANIMATE`)

Minimise and restore animate the same way from every path: frame button, keybinding (`minimize`), `_NET_WM_STATE_HIDDEN` and
`WM_CHANGE_STATE` (minimise), `_NET_ACTIVE_WINDOW` and `_NET_WM_STATE` remove-hidden (restore), because all of them go through
`client_minimize` / `client_restore`. Maximise and restore go through `max_transition()`.

- **Minimise unmaps.** The client and its frame are unmapped where they are (WM_STATE Iconic, `_NET_WM_STATE_HIDDEN`), no longer moved
  off screen; that is what lets picom see a "hide" and, on restore (mapped again), a "show". luxwm's own UnmapNotify events are
  counted (`unmap_ignore`; the client's and the root's copy), a minimised client that is destroyed is dropped on DestroyNotify,
  and a restart adopts unmapped windows whose WM_STATE is Iconic (they stay minimised and unmapped).
- **With picom** (`../picom-glass.conf`, window rules; 2026-10-24, following COMPOSITION.md 14): luxwm sets `_LHWM_ANIMATE` (CARDINAL) on
  every client and decoration window before mapping them; its value is the animation style (runtime since 2026-10-25, next item; config.h
  `ANIMATE_STYLE` is the default), and each value has its own rule set.
- **Runtime style (2026-10-25, `SETTINGS.md` "Who writes what").** `settings_load()` reads `[theme] animations = 4074plus | 4074 |
  invented | 5048 | none` from `~/.config/lhde/settings.ini` (`$XDG_CONFIG_HOME/lhde/settings.ini`; a number 0/1/3/5/7
  is accepted too) at startup and again on the root ClientMessage `_LH_SETTINGS_CHANGED` (format 32, data[0] the writer's serial, sent
  with SubstructureNotifyMask). While a user preset is selected (`_LH_THEME` = a preset name, "EWMH and the shell") its own
  `[Composition] animations` wins over the file's (log `from preset NAME`). `LHWM_ANIMATIONS=<name>` overrides both (for the lab); a missing file or key, or an unknown name
  (logged), gives config.h's `ANIMATE_STYLE`. The log says which: `luxwm: animations 4074 (_LHWM_ANIMATE 1) from .../settings.ini`.
  Values: 4074plus = 7, 4074 = 1, invented = 3, 5048 = 5, none = 0. A change rewrites `_LHWM_ANIMATE` on every client and decoration
  window at once (a window in a running ANIM_INVENTED geometry animation gets it when that ends), so picom's rules switch live; **none**
  deletes the property (no rule matches; picom still logs hide/show, which end at once without a rule) and also turns off luxwm's own XP
  caption flight without a compositor. `composition` is not luxwm's (lux/picom). Lab check (picom `--log-level debug`, a python-xlib sender
  for the message): all five clients 7 → `animations = 4074` + message → 1 on every client, and minimising xterm then logs
  `c2_match_once ... result = 1, client = 0x00c0000c, pattern = _LHWM_ANIMATE@[0] = 1`; `none` → the property is gone on every window;
  `LHWM_ANIMATIONS=5048` at restart → 5 ("from LHWM_ANIMATIONS").
  Docks, desktop and notification windows are excluded in the rules.
  The three styles in one line each (details below): **`ANIM_4074PLUS`** (default) = 4074's 350 ms fade + gentle centre zoom (0.95 -> 1) on open, the invented
  grow on restore from minimised and shrink on minimise, close = picom's default fade, maximise/restore = luxwm's one-step move + plate
  crossfade only; **`ANIM_4074`** = 4074's fade + flattened door swing on open/restore, everything else instant; **`ANIM_INVENTED`** =
  the invented minimise/restore, default fade on open/close, picom's geometry animation on maximise/restore.
  - `ANIM_4074PLUS` (7, **default**; changed 2026-10-25 at the user's request): window **open** (first map, picom `open`) = 4074's
    350 ms fade with the gentle 0.95 -> 1 zoom about the centre (the first swing stand-in, from before the door-swing experiment); **restore from
    minimised** (`show`) = the invented 250 ms grow + fade in from 15 % at the window's horizontal centre on the bottom edge of its
    monitor (ease-out, the reverse of minimise); **minimise** (`hide`) = the invented 250 ms shrink + fade out towards it (ease-in);
    close keeps picom's default fade; **maximise/restore**: luxwm's crossfade only, **no picom animation** (no geometry rule, luxwm never
    writes another value):
    the window goes to its new rectangle in one step and its plate switches with it (`MAX_FADE_MS` 0, the default since 2026-10-25; with 400, DWM themes with `plate_max` crossfade the plate over 400 ms in step with
    luxshell's black layers, "Maximised mode" M5b). Until 2026-10-25 open/restore used the door swing and maximise/restore set
    `_LHWM_ANIMATE = 8` for picom's geometry animation (only for themes without `plate_max`, i.e. never with the registry themes,
    whose classic frames run with `composition = off`).
  - `ANIM_4074` (1): what 4074's desksrv does. Only a hidden -> visible transition animates (window **open**, and **restore from
    minimised**, which is the same `'DWM!' 0xcc` message there): opacity 0 -> 1 over 350 ms on a timeline with acceleration 0.3 /
    deceleration 0.4, as `cubic-bezier(0.35, 0, 0.575, 1)` (within 0.01 of the Avalon curve; COMPOSITION.md 14.5 suggested
    (0.3, 0, 0.6, 1), off by 0.026), blur and shadow fading with it. 4074 also swings the window in in 3D (300 ms, decel 0.3: from 0.25
    window-widths behind, turned 35 deg about its top edge); picom has no perspective, so the swing is flattened **head-on**: scale-x
    0.93 -> 1 about the window's centre (picom scales about the top-left corner, `offset-x` compensates) and scale-y 0.762 (0.93 *
    cos 35 deg) -> 1 anchored at the top edge (`offset-y` 0), on the swing's curve (`cubic-bezier(0.225, 0.2, 0.725, 1)`, within 0.016).
    The real swing (perspective hinge, bottom edge coming out of the screen) needs the phase-2 compositor. Client and frame scale alike
    and stay registered to within a pixel or two, while nearly transparent. Minimise and close (`hide`, `close`) are **instant**: their rule has
    no timing function, so picom ends it at once (no default fade either); maximise/restore are instant too (no geometry rule, and
    luxwm no longer sets the value 2).
  - `ANIM_5048` (5): the April 2005 uDWM variant: one 266 ms curve `p = sqrt(t / 0.266 s)` (`cubic-bezier(0, 0.55, 0.95, 1)`, within
    0.016) for opacity and pose; the pose (pitched and pushed 0.75 back on open, tilted/twisted from below-left on restore) is a scale
    0.9 -> 1 here. Minimise, close, maximise instant, as in 5048.
  - `ANIM_INVENTED` (3; not Longhorn, kept for comparison): the earlier luxwm rules. "hide": both windows shrink to 15 % towards the
    window's horizontal centre on the bottom edge of its monitor, fading out (opacity, blur and shadow), 250 ms, ease-in
    `cubic-bezier(0.4, 0, 1, 1)`; "show" the reverse with ease-out `cubic-bezier(0, 0, 0.2, 1)`; open/close keep picom's default fade.
    For maximise/restore luxwm sets `_LHWM_ANIMATE = 2` on client and frame right before the geometry changes and back to 3 after 400 ms
    (timer in the main loop), and the rule for `= 2` adds picom's `geometry-change` preset (250 ms), so ordinary moves, resizes and
    workspace switches are never animated.
  Checked 2026-10-25 with `picom --log-level debug` on the lab (default style 7): "Starting animation hide" for client and frame on
  minimise (same millisecond), "Starting animation show" for both on restore (same millisecond; the shot series shows the window
  growing from the bottom edge, translucent), "Starting animation open" for a new xterm and its frame. The frame's "open" starts one
  picom frame (30..50 ms on the lab) after the client's: the decoration window is created at the MapRequest, so picom imports it one
  round later (painting it before the map did not change that); the same holds for every style's open. Maximise/restore: no
  `_LHWM_ANIMATE` change and no animation. Earlier check (style 1): `_LHWM_ANIMATE@[0] = 1` matches client and frame; "Starting
  animation open" for a new xterm, "Starting animation hide" for client and frame on minimise with "Releasing pixmap" in the same
  millisecond (instant), "Starting animation show" on restore, "Starting animation close" on close; a caption double-click maximise
  and restore start no animation and `_LHWM_ANIMATE` stays 1.
- **Without a compositor** luxwm draws XP's animation itself (`anim_classic`), on the root with the server grabbed, from the frame
  rectangle to the taskbar button (`_NET_WM_ICON_GEOMETRY`, written by luxshell; fallback a 160x24 rectangle on the bottom-left of
  the work area) and back on restore, and from the old to the new rectangle on maximise/restore (XP does that too). Reference:
  `nt5src/.../windows/core/ntuser/kernel/minmax.c`. XP only uses `IDANI_CAPTION`: `xxxDrawAnimatedRects` (line 1051) calls
  `xxxAnimateCaption` (line 567), which flies the active caption bar: `cy = CYCAPTION - 1` rows, rectangles taller than a caption
  inset by the window borders, the strip drawn once at the larger width and cut from the left to the current width, left/top/width
  moved linearly with the elapsed time, `start + MultDiv(delta, elapsed, CMS_ANIMATION)` with `CMS_ANIMATION = 250` (line 18,
  735..739), `UserSleep(1)` between frames, the screen under the strip saved and restored every frame. The XOR wireframe
  (`AnimateFrame`/`DrawWireFrame`, white pen with R2_XORPEN, lines 829..1040) is Win95 code that XP compiles out
  ("DISABLE OLD ANIMATION FOR M7", `#if 0`). luxwm does the caption flight by default (caption rows of the frame painted focused, inside
  the borders, over black; save-under with `XCopyArea` and `IncludeInferiors`) with a 10 ms sleep per frame, which gives 25..26
  frames in ~255 ms (XP's frame count is whatever its 10..16 ms tick allows). `ANIMATE_WIREFRAME true` draws XOR rectangles instead
  (3 px, `GXxor` with white ^ black, same timing). Each frame is `XSync`ed; before/after screenshots of a minimise + restore are
  identical (only the sidebar clock differs) for both styles. `LHWM_ANIM_NOGRAB=1` skips the server grab (testing only, so a screen
  recorder can watch).
- `_LHWM_MAXIMIZED_MODE` is written at the start of `max_transition()`, i.e. in the same handler before any ConfigureWindow, repaint or
  animation, and on restore it is cleared before the restore geometry (also when a drag un-maximises: `client_unmax_margins`). A tracer
  that selects root PropertyNotify and the client's ConfigureNotify sees, in server order: mode = 1, then the client's ConfigureNotify
  events (maximise); mode = 0, then the ConfigureNotify events (restore), with and without picom. A 50 ms screenshot series on the lab
  shows luxshell's black layer up in the frame before the window has grown, and gone before it shrinks.
