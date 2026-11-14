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
| `flxp.ini` | flxp's own preferences (`[flxp]` key = value: views, header, thumbnails, dialog sizes, ...); written with every key empty (= automatic) and its comment on first run; any key also as `FLXP_<KEY>` in the environment for one run. Nothing the desktop shares: flxp reads the keys below from settings.ini, the theme's from the session theme (`_LH_THEME`, without a session `[theme] name`; `--theme` / `FLXP_THEME` gives one run a fixed visual style) |
| `keys` | luxwm's key bindings, one per line (`Mod4+Return exec urxvt`; LUXWM.md "Running and testing") |
| `quicklaunch/` | the Quick Launch `.desktop` files: luxshell's tile, flxp-taskbar's toolbar (`[quicklaunch] dir` names another folder) |
| `luxpad.ini` | luxpad's choices: `[luxpad]` font, font_size, font_weight, font_italic, wrap, statusbar (docs/USAGE.md "luxpad") |
| anything else | a shipped file overridden under its own relative name (`navbar/4074.ini`, `frames/jade4074/theme.ini`, `packs/4074-aero/shell.lua`): this directory is the first tree of every data lookup (docs/LAYOUT.md "The three trees") |

**What a program remembers on its own**: `${XDG_STATE_HOME:-~/.local/state}/lhde/`. Losing it costs nothing but convenience.

| file | what |
| --- | --- |
| `luxshell-state.ini` | the tile order (`[sidebar] tiles`), the mode the minimized sidebar returns to (`[sidebar] restore`); `$LHSHELL_STATE` names another file |
| `luxshell-mfu.ini` | launch counts (`[mfu]` desktop id = count last-launch-epoch) |
| `luxshell-notifications` | the notification history (tab-separated, newest last, at most 100) |
| `luxshell-icons/<size>/` | icons converted to square PNGs (ImageMagick) |
| `flxp-views` | per folder, one line: `PATH<tab>VIEW` (`icons@48`, `details`, `tiles@48+nonames`, ...), and `<tab>columns=LAYOUT` when the folder's Details columns differ from the default (VIEW may then be empty) |
| `flxp-columns` | the default Details columns, one LAYOUT: every column in order, a hidden one with `-`, each with its width (`name:180 size:96 type:120 modified:120 -description:180 tags:120 -comment:150 -color:100`); a column it does not name is hidden |
| `flxp-view-sizes` | the size each view had last |
| `flxp-tags` | the tags last added (Add Tag..., the Properties sheet), one a line, the latest first, at most 100: the tag boxes offer them |
| `flxp-desktop-positions` | the desktop's icon positions |
| `flxp-mfu`, `flxp-run-history` | the start panel's launch counts, the Run box's history |
| `luxpad.ini` | luxpad's window (`[window]` x, y, width, height, maximized) and last search (`[search]` find, replace, match_case, whole_word, down) |

lhde never puts Windows files in either: they are `~/.local/share/lhde-windows` (lhde-import), the shipped text is
`$PREFIX/share/lhde` (make install). Which program opens a type is not kept here either but in the freedesktop files every
program shares ("Associations" below).

Environment, honoured by every lhde program and script: `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `LHDE_SETTINGS` (settings.ini
alone), `LHDE_WINDOWS` (another Windows tree), `LHDE_DATA` (one flat data directory instead of the Windows tree: tests, scratch
imports), `XDG_DATA_HOME` (where `lhde-windows` is).

## settings.ini

An ini file (`[section]`, `key = value`, `;` or `#` comments at the start of a line or after whitespace; of a key given twice the
last one counts, for every reader: liblhde's `lhde_ini`, which flxp's `shell::store` wraps, and luxshell's). Every writer keeps the
comments, the order and the keys it does not know: it reads the whole file, changes its keys and writes it back atomically
(through a symlink; liblhde `lhde_settings_set`, luxshell `lh.settings_set`, flxp `shell::store`, `lhde-theme setting SEC KEY
[VALUE]`).

```
[theme]       name = aero4074            ; the selected theme: a theme file's name, an alias or a bare style (mirrors _LH_THEME)
[current]     theme = aero4074           ; the current layer, Windows' "NAME (Modified)": the theme it belongs to
              Section.key = value        ; and the theme keys changed but not saved (Desktop.wallpaper = 2k/blulac16, ...)
[desktop]     wallpaper = none           ; the picture off in every theme (luxshell's "Show the wallpaper"); absent: the theme's
              black = 0                  ; 1: maximised mode also blacks out the desktop layer (luxshell)
              max_fade_ms = 0            ; the bars' black layers in maximised mode: 0 at once, else a fade in ms (luxshell)
[taskbar]     autohide = 0 ; rows = 1    ; 0 | 1; 1..3 (the top-edge drag)
              start_menu = panel         ; the classic bar's Start button: panel (XP's start panel) | classic (the popup menu) (flxp-taskbar)
              quicklaunch = 1            ; the classic bar's Quick Launch toolbar (its menu's Toolbars > Quick Launch) (flxp-taskbar)
              locked = 1                 ; the classic bar's Lock the Taskbar; 0: the bands' grippers show (flxp-taskbar)
              hide_inactive = 1          ; the classic bar's chevron hides the icons unchanged for an hour (XP's Hide inactive icons)
[sidebar]     mode = full                ; full | minimized | pinned (absent: the theme's Shell.sidebar; Super+H: minimized ⇄ back)
              width = 150                ; 100..600 (the sizer drag)
              stacking = top             ; top: above the windows, with its strut | bottom: under them, part of the desktop (Win+S)
              with_classic = 0           ; 1: the sidebar stays with the classic taskbar
              yield_bottom = 1           ; 1: ends above another dock's bottom strut | 0
              locked = 0                 ; 1: Lock the Sidebar (no width drag)
              animate = 0                ; 0: hiding (minimized) and showing it is instant, as 4074's | 1: the sidebar slides, the taskbar follows
[clock]       pinned = 1 ; pack = 4074-aero ; layout = default   ; the clock tile; pinned holds for the pack it was chosen in
[quicklaunch] dir = ~/some/dir           ; the Quick Launch folder: luxshell's tile and flxp-taskbar's toolbar (default ~/.config/lhde/quicklaunch/)
[start]       pinned = a.desktop;b.desktop ; flxp's start panel: the pinned programs (desktop ids, terminal, explorer, commands)
[commands]    logoff = ... ; standby ; shutdown ; restart ; lock ; picom ; taskbar ; filemanager ; search   ; luxshell, flxp's start panel (logoff, shutdown)
              run = dmenu_run            ; Super+R runs this instead of the start menu's Run dialog (the default: unset)
              terminal = cmd             ; the terminal (below); default: the first of cmd, urxvt, xterm, x-terminal-emulator on PATH
              screenshot = luxshot --screen ; screenshot_window = luxshot --window ; screenshot_region = luxshot --region   ; luxwm's Super+Shift+F/W/S (luxshell; the defaults)
[fonts]       smoothing = standard       ; off | standard | cleartype (flxp, luxwm's menus; luxshell uses fontconfig's)
[effects]     menu_shadows = 1           ; XP's drop shadow under menus (wux's popup menus, luxwm's window menu, luxshell's menus)
              keyboard_cues = 0          ; 0: mnemonics underlined only after Alt (XP's default); 1: always
```

**The terminal**, `[commands] terminal`, is a `/bin/sh` command line; `%d` in it is the directory, quoted. Every terminal of the
desktop is this one: luxwm's Super+Return (the `terminal` key action), flxp's Open Terminal Here (run in the folder, `%d` that
folder), the start panel's pinned `terminal`, a `Terminal=true` program (`TERMINAL -e PROGRAM ARGS...`, from flxp and luxshell),
luxshell's Quick Launch Command Prompt and Task Manager fallback (`top`), lhde-session's first terminal (liblhde
`lhde_terminal_command`; `lhde-theme terminal` prints it, `lhde-theme terminal --run [DIR]` runs it). Without the key the first
of `cmd` (a script of the user's own: one that looks like cmd.exe), `urxvt`, `xterm`, `x-terminal-emulator` on PATH, `cmd`
first by aap's choice. The window that starts gets cmd.exe's icon through the class map (docs/THEME-FORMAT.md section 9) when its
class is one of the terminals there; a terminal of another class is added in `~/.config/lhde/icons/apps.ini`. flxp.ini has no
`terminal` key any more (settings.ini's is the desktop's); one left there is not read.

**Switches** are `1` or `0` in every file lhde reads (settings.ini, flxp.ini, theme files) and in the `FLXP_<KEY>` /
`LHSHELL_*` environment; `on`, `off`, `yes`, `no`, `true`, `false` are not accepted: such a value is reported once on stderr
and taken as unset (the default). One parser each: flxp's `shell::flag_value` (`setting_flag`, `store::flag`), luxshell's
`S.flag_value` (`S.flag`). `auto` is a value only where it chooses something: the theme's `Glass.composition` (auto: a
compositor when the theme has glass frames), `VisualStyle.shellstyle`, `Explorer.chrome`, `Explorer.navbar_preset`; in
flxp.ini an empty value is automatic for every key.

luxshell writes the defaults of `[desktop] black max_fade_ms`, `[taskbar] autohide rows`, `[sidebar] width with_classic stacking animate`
and `[clock] layout` when they are missing. `[desktop] wallpaper = none` is the one per-user key the
theme resolver applies (below the current layer; source `settings` in `lhde-theme resolve --why`); every other theme key is
the theme's. The current layer is written by Display Properties, Set as Desktop Background, luxshell's flyout and
`_LHSHELL_COMMAND taskbar` (liblhde `lhde_current_set` / `lhde_current_set_key`; a key the theme gives anyway is dropped);
selecting another theme removes it (`lhde_theme_select`), Save As turns it into a theme file (docs/THEME-FORMAT.md section 3).

## Who writes what

| writer | what |
| --- | --- |
| Display Properties (flxp-settings: Themes, Desktop, Appearance, Effects) | `[theme] name`, the current layer, `[desktop] wallpaper`, `[sidebar] mode with_classic stacking animate`, `[fonts]`, `[effects]`, theme files (Save As, Delete) |
| luxshell (the settings flyout, its menus, drags, `_LHSHELL_COMMAND`) | `[theme] name`, the current layer, `[desktop] wallpaper`, `[taskbar]`, `[sidebar]`, `[clock]`, theme files (Save As) |
| flxp-desktop, the Explorer | Set as Desktop Background: the current layer's `Desktop.wallpaper` / `Desktop.mode` (and a `[desktop] wallpaper = none` cleared); outside lhde's files: the associations ("Associations" below) |
| lhde-session | `_LH_THEME` only (`theme NAME`; luxshell then writes the file); without a luxshell, `taskbar KIND` through `lhde-theme set-key`, `set` and `setting` |
| luxwm | nothing; it reads `[fonts] smoothing` and `[effects]` for its window menu, the theme through `_LH_THEME` |
| flxp-taskbar, the start panel | `[taskbar] quicklaunch locked` (the taskbar menu); they read `[taskbar] start_menu hide_inactive`, `[quicklaunch] dir`, `[start] pinned`, `[commands] logoff shutdown terminal` |

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

## Associations

Which program opens a file type is not an lhde setting: it lives in the freedesktop files every program reads (the MIME
Applications Associations spec), so flxp, GIO programs (Firefox, GTK), xdg-open and lhde's own tools agree. The files, most
important first (`$desktop` each entry of `XDG_CURRENT_DESKTOP`, lower-cased; lhde-session sets `XDG_CURRENT_DESKTOP=lhde` and
`DESKTOP_SESSION=lhde`):

| file | layer |
| --- | --- |
| `~/.config/$desktop-mimeapps.list`, `~/.config/mimeapps.list` | the user's choices; flxp (Open With, Folder Options' File Types, Properties' Change...), `lhde-mime default` and GLib write `mimeapps.list` |
| `$XDG_CONFIG_DIRS/$desktop-mimeapps.list`, `.../mimeapps.list` | the administrator's |
| `~/.local/share/applications/$desktop-mimeapps.list`, `mimeapps.list`, `defaults.list` | the user's too (the old place) |
| `$XDG_DATA_DIRS/applications/$desktop-mimeapps.list`, `mimeapps.list`, `defaults.list` | in data-directory order: lhde's `lhde-mimeapps.list` (installed into `$PREFIX/share/applications`, from `share/applications/`), the distribution's |

In each directory the desktop-specific files come before `mimeapps.list`, and `defaults.list` (the deprecated name) last. So the
user's choice beats lhde's defaults, and lhde's beat the distribution's when lhde's data directory comes first in
`XDG_DATA_DIRS` (`/usr/local/share` before `/usr/share`, the default) or is the data home (`PREFIX=~/.local`). Without
`XDG_CURRENT_DESKTOP=lhde` (another session) lhde's file is not read.

lhde's defaults: `inode/directory` = `flxp.desktop`, `text/plain` = `luxpad.desktop`. A desktop-specific file holds
`[Default Applications]` only: its Added and Removed Associations are not read (the spec; GLib warns about them, `lhde-mime
check` reports them), so taking a program off a type is done in `mimeapps.list`.

A type's program (GLib's order, which flxp's shell layer follows): the first installed, not removed `[Default Applications]`
entry for the type in the files above; else its first claimant (`[Added Associations]` in the same order, then the desktop
files that name the type in `MimeType=`, by data directory); else the same for its nearest parent type (`text/x-log` ->
`text/plain` -> `application/octet-stream`). GIO and xdg-open read the claimants from each directory's `mimeinfo.cache`
(update-desktop-database), not from the desktop files: a directory without one claims nothing for them (`make install` runs
update-desktop-database on `$PREFIX/share/applications`). xdg-open knows no desktop called lhde and takes xdg-utils' generic
path, which reads the same files in the same order but only for the exact type and without Removed Associations; it needs no
hint. `lhde-mime explain TYPE` shows all three verdicts (docs/USAGE.md "File types and programs").

What lhde writes, all of it the user's: `~/.config/mimeapps.list` (flxp's Open With with "Always use", Properties' Change...,
Folder Options' File Types Change... and Restore, `lhde-mime default` and `reset`: a type's `[Default Applications]` line, the
program first in its `[Added Associations]`, off its `[Removed Associations]`; Restore and reset remove the type's lines from
all three groups); copies of a program's desktop file in `~/.local/share/applications/ID` marked `X-Lhde-Override=true` and
`X-Lhde-Original=PATH` (File Types' Edit File Type: the program's actions; Restore deletes the copy, and a desktop file there
without the mark is never written); `~/.local/share/mime/packages/lhde-extensions.xml` and what update-mime-database compiles
from it in `~/.local/share/mime` (File Types' New and Delete: the user's own extensions).
