# Shared desktop settings

One settings store for the whole desktop: luxwm, luxshell and the flxp programs read and write the same files, through liblhde
(`<lhde/settings.h>`). The theme keys are not settings: they live in theme files (docs/THEME-FORMAT.md) and the current layer.
The live state of a session is on the root window (`_LH_THEME`, `_LH_THEME_RESOLVED`, `_LHWM_*`, `_LHSHELL_*`).

## Files

Two directories, one rule each. liblhde gives every path (`lhde_config_file`, `lhde_state_file`; luxshell `lh.config_file`,
`lh.state_file`; flxp `shell::settings_path`, `shell::state_file`); `lhde-theme dirs` lists both. No other directory is read.

**What the user chose**: `${XDG_CONFIG_HOME:-~/.config}/lhde/`

| file | what |
| --- | --- |
| `settings.ini` | the selected theme, its current layer and the per-user keys (below); `$LHDE_SETTINGS` names another file |
| `themes/NAME.theme` | the user's theme files (docs/THEME-FORMAT.md), written by Save As (Display Properties, luxshell's flyout, `lhde-theme save-as`) |
| `flxp.ini` | flxp's own preferences (`[flxp]` key = value: views, header, thumbnails, the terminal, dialog sizes, ...); written with every key empty (= automatic) and its comment on first run; any key also as `FLXP_<KEY>` in the environment for one run. Nothing the desktop shares: flxp reads the keys below from settings.ini, the theme's from the session theme |
| `keys` | luxwm's key bindings, one per line (`Mod4+Return exec urxvt`; LUXWM.md "Running and testing") |
| `quicklaunch/` | the Quick Launch tile's `.desktop` files (`[quicklaunch] dir` names another folder) |
| anything else | a shipped file overridden under its own relative name (`navbar/4074.ini`, `frames/jade4074/theme.ini`, `packs/4074-aero/shell.lua`): this directory is the first tree of every data lookup (docs/LAYOUT.md "The three trees") |

**What a program remembers on its own**: `${XDG_STATE_HOME:-~/.local/state}/lhde/`. Losing it costs nothing but convenience.

| file | what |
| --- | --- |
| `luxshell-state.ini` | the tile order (`[sidebar] tiles`); `$LHSHELL_STATE` names another file |
| `luxshell-mfu.ini` | launch counts (`[mfu]` desktop id = count last-launch-epoch) |
| `luxshell-notifications` | the notification history (tab-separated, newest last, at most 100) |
| `luxshell-icons/<size>/` | icons converted to square PNGs (ImageMagick) |
| `flxp-views`, `flxp-view-sizes`, `flxp-columns` | the view remembered per folder, the size each view had last, the Details columns |
| `flxp-desktop-positions` | the desktop's icon positions |
| `flxp-mfu`, `flxp-run-history` | the start panel's launch counts, the Run box's history |

lhde never puts Windows files in either: they are `~/.local/share/lhde-windows` (lhde-import), the shipped text is
`$PREFIX/share/lhde` (make install).

Environment, honoured by every lhde program and script: `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `LHDE_SETTINGS` (settings.ini
alone), `LHDE_WINDOWS` (another Windows tree), `LHDE_DATA` (one flat data directory instead of the Windows tree: tests, scratch
imports), `XDG_DATA_HOME` (where `lhde-windows` is).

## settings.ini

An ini file (`[section]`, `key = value`, `;` or `#` comments at the start of a line or after whitespace). Every writer keeps the
comments, the order and the keys it does not know: it reads the whole file, changes its keys and writes it back atomically
(through a symlink; liblhde `lhde_settings_set`, luxshell `lh.settings_set`, flxp `shell::store`, `lhde-theme setting SEC KEY
[VALUE]`).

```
[theme]       name = aero4074            ; the selected theme: a theme file's name, an alias or a bare style (mirrors _LH_THEME)
[current]     theme = aero4074           ; the current layer, Windows' "NAME (Modified)": the theme it belongs to
              Section.key = value        ; and the theme keys changed but not saved (Desktop.wallpaper = 2k/blulac16, ...)
[desktop]     wallpaper = none           ; the picture off in every theme (luxshell's "Show the wallpaper"); absent: the theme's
              desktop_black = 0          ; 1: maximised mode also blacks out the desktop layer (luxshell)
[shell]       max_fade_ms = 0            ; the bars' black layers in maximised mode: 0 at once, else a fade in ms (luxshell)
[taskbar]     autohide = 0 ; rows = 1    ; 0 | 1; 1..3 (the top-edge drag)
              start_menu = panel         ; the classic bar's Start button: panel (XP's start panel) | classic (the popup menu) (flxp-taskbar)
[sidebar]     mode = full                ; full | minimized | pinned (absent: the theme's Shell.sidebar)
              width = 150                ; 100..600 (the sizer drag)
              stacking = top             ; top: above the windows, with its strut | bottom: under them, part of the desktop (Win+S)
              with_classic = 0           ; 1: the sidebar stays with the classic taskbar
              yield_bottom = auto        ; auto: ends above another dock's bottom strut | 0
              locked = 0                 ; 1: Lock the Sidebar (no width drag)
[clock]       pinned = 1 ; pinned_pack = 4074-aero ; layout = default   ; the clock tile; pinned holds for the pack it was chosen in
[quicklaunch] dir = ~/some/dir           ; the Quick Launch tile's folder (default ~/.config/lhde/quicklaunch/)
[start]       pinned = a.desktop;b.desktop ; flxp's start panel: the pinned programs (desktop ids, terminal, explorer, commands)
[commands]    logoff = ... ; standby ; shutdown ; restart ; lock ; picom ; taskbar ; filemanager ; run ; search   ; luxshell, flxp's start panel (logoff, shutdown)
[fonts]       smoothing = standard       ; off | standard | cleartype (flxp, luxwm's menus; luxshell uses fontconfig's)
[effects]     menu_shadows = 1           ; XP's drop shadow under menus (wux's popup menus, luxwm's window menu)
              keyboard_cues = 0          ; 0: mnemonics underlined only after Alt (XP's default); 1: always
```

luxshell writes the defaults of `[desktop] desktop_black`, `[taskbar] autohide rows`, `[sidebar] width with_classic stacking`,
`[clock] layout` and `[shell] max_fade_ms` when they are missing. `[desktop] wallpaper = none` is the one per-user key the
theme resolver applies (below the current layer; source `settings` in `lhde-theme resolve --why`); every other theme key is
the theme's. The current layer is written by Display Properties, Set as Desktop Background, luxshell's flyout and
`_LHSHELL_COMMAND taskbar` (liblhde `lhde_current_set` / `lhde_current_set_key`; a key the theme gives anyway is dropped);
selecting another theme removes it (`lhde_theme_select`), Save As turns it into a theme file (docs/THEME-FORMAT.md section 3).

## Who writes what

| writer | what |
| --- | --- |
| Display Properties (flxp-settings: Themes, Desktop, Appearance, Effects) | `[theme] name`, the current layer, `[desktop] wallpaper`, `[sidebar] mode with_classic stacking`, `[fonts]`, `[effects]`, theme files (Save As, Delete) |
| luxshell (the settings flyout, its menus, drags, `_LHSHELL_COMMAND`) | `[theme] name`, the current layer, `[desktop] wallpaper`, `[taskbar]`, `[sidebar]`, `[clock]`, theme files (Save As) |
| flxp-desktop, the Explorer | Set as Desktop Background: the current layer's `Desktop.wallpaper` / `Desktop.mode` (and a `[desktop] wallpaper = none` cleared) |
| lhde-session | `_LH_THEME` only (`theme NAME`; luxshell then writes the file); without a luxshell, `taskbar KIND` through `lhde-theme set-key`, `set` and `setting` |
| luxwm | nothing; it reads `[fonts] smoothing` and `[effects]` for its window menu, the theme through `_LH_THEME` |
| flxp-taskbar, the start panel | nothing; they read `[taskbar] start_menu`, `[start] pinned`, `[commands] logoff shutdown` |

## Change notification

A writer saves the file, then sends a ClientMessage `_LH_SETTINGS_CHANGED` (format 32, data[0] = a monotonically increasing
serial, data[1] = 0 or a section hint atom) to the root with `SubstructureNotifyMask`. The serial lives on the root as
`_LH_SETTINGS_SERIAL` (CARDINAL[1], the last serial sent): a writer reads it, adds one (masked to 31 bits), writes the property
back, then sends the message with that value (flxp's `shell::store::notify`, luxshell's `S.notify`). The hint is the section
name's atom (`theme`, `desktop`, `taskbar`, `current`, ...) when exactly one section changed, else 0; readers may use it to skip
work but must not rely on it. Readers reload the file on that message (and may also watch it with inotify).

luxshell's serial is never below the highest it has seen from anyone or its clock seed (so it keeps increasing over restarts);
several keys set while one event is handled go out as one message. Every message (its own included) makes luxshell read the
file again and apply the keys whose value differs from what runs.

## The session theme on the root

- `_LH_THEME` (UTF8_STRING): the selected theme's name, as the user chose it (a theme file, an alias, a bare style). Whoever
  changes it writes `settings.ini [theme] name` first, then the property, then sends `_LH_SETTINGS_CHANGED`. luxshell replaces an
  alias by the theme's name; nobody rewrites another theme's name.
- `_LH_THEME_RESOLVED` (UTF8_STRING): the resolved record of that theme, `theme=NAME`, `serial=` (the `_LH_SETTINGS_SERIAL` it was
  resolved at), then `Section.key=value` lines with the current layer applied (docs/THEME-FORMAT.md section 4). luxshell and
  luxwm write it when it does not describe the current record yet; for its own switches luxshell writes it before `_LH_THEME`.
  A reader uses it only when its `theme=` equals `_LH_THEME`, else resolves the name itself through liblhde, and watches it for a
  republished record (luxwm, flxp's `lookup_session_theme`, the examples).
- A name that resolves to nothing: every reader keeps its current theme and logs the name.

## Conflicts

Last writer wins per key; writers never drop keys they do not know. When settings.ini and the root disagree at startup, the
root wins for the running session (it is what is live), and the first writer brings the file in line.
