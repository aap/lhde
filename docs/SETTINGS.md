# Shared desktop settings (2026-10-25)

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, SETTINGS.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

## lhde (step 4, 2026-10-28): `~/.config/lhde`
This section replaces the paths below; the dated sections after it are history (`[override:NAME]`, presets, the registry).

- `${XDG_CONFIG_HOME:-~/.config}/lhde/settings.ini` (`$LHDE_SETTINGS` overrides it): `[theme] name` (the selected theme) and the
  per-user keys that are not part of a theme: `[taskbar] autohide rows classic_start_menu`, `[sidebar] mode width stacking
  with_classic yield_bottom locked`, `[clock]`, `[start] pinned mfu_count userpane`, `[desktop] desktop_black` and `wallpaper = none`
  (the flyout's "Show the wallpaper" off), `[shell] max_fade_ms`, `[fonts] smoothing`, `[commands]`. Written by liblhde's store
  (`lhde_settings_set`, luxshell's `lh.settings_set`, `lhde-theme setting`): comments, order and unknown keys kept, atomic, through
  a symlink. The serial (`_LH_SETTINGS_SERIAL`, `_LH_SETTINGS_CHANGED`) stays the X clients' business, unchanged.
- `~/.config/lhde/themes/NAME.theme`: the user's theme files (docs/THEME-FORMAT.md), written by Save As (and `lhde-theme save-as`).
- `settings.ini [current]` (2026-10-28): the current layer, Windows' "NAME (Modified)": `theme = NAME` and the changed theme keys
  as `Section.key = value` (`VisualStyle.style = classic`, `Desktop.wallpaper = 2k/blulac16`, `Colors.activecaption = 255 0 0`).
  Display Properties and Set as Desktop Background write it (liblhde `lhde_current_set` / `lhde_current_set_key`; a key the theme
  gives anyway is dropped); the resolver applies it to that theme, so every client sees it; selecting another theme removes the
  section (`lhde_theme_select`), Save As turns it into a theme file. luxshell's flyout and `_LHSHELL_COMMAND taskbar` write it too
  (`lh.current_set_key`, 2026-10-28); only `lhde-theme set-key` still writes `NAME-modified.theme` (`lhde_theme_set_key`).
- **Two user directories, one rule each** (2026-10-29). What the user chose lives in `~/.config/lhde/` (`$XDG_CONFIG_HOME/lhde`):
  `settings.ini`, `themes/`, `flxp.ini` (flxp's own preferences: views, dialog sizes, the terminal, ... any key also as
  `FLXP_<KEY>` for one run), `keys` (luxwm's key bindings), and any shipped file overridden under its own name. What a program
  remembers on its own lives in `~/.local/state/lhde/` (`$XDG_STATE_HOME/lhde`): `luxshell-state.ini` (the tile order),
  `luxshell-mfu.ini` (launch counts), `luxshell-notifications` (the notification history), `flxp-views` (the view remembered
  per folder), `flxp-view-sizes` (the size each of the Explorer's views had last), `flxp-desktop-positions` (icon positions),
  `flxp-mfu` and `flxp-run-history` (the start panel's launch counts and the Run box's history).
  Losing the state directory costs nothing but convenience. liblhde gives the paths (`lhde_config_file`, `lhde_state_file`;
  luxshell `lh.config_file`, `lh.state_file`; flxp `shell::settings_path`, `shell::state_file`) and `lhde-theme dirs` lists both.
  No lhde program reads `~/.config/flxp`, `~/.config/lhshell`, `~/.config/lhwm` or `~/.config/longhorn` any more.
- `~/.config/lhde/` is also the first tree of every data lookup (docs/LAYOUT.md "The three trees", 2026-10-28): a copy of a
  shipped file under its own relative name overrides the shipped one (`~/.config/lhde/navbar/4074.ini`,
  `~/.config/lhde/frames/jade4074/theme.ini`, `~/.config/lhde/packs/4074-aero/shell.lua`), so nothing under `$PREFIX` needs
  editing. lhde never puts Windows files here: those are `~/.local/share/lhde-windows` (lhde-import), the shipped text is
  `$PREFIX/share/lhde` (make install). `lhde-theme explain` names the tree of every file a theme uses (`[user]` for these).
- Environment, honoured by every lhde program and script: `XDG_CONFIG_HOME` (this tree), `LHDE_SETTINGS` (settings.ini alone),
  `LHDE_WINDOWS` (another Windows tree), `LHDE_DATA` (one flat data directory instead of the Windows tree: tests, scratch
  imports), `XDG_DATA_HOME` (where `lhde-windows` and an old `lhde` are looked for).

**Migration** (`lhde_settings_migrate`; run by `lhde-session start`, luxshell at startup, `lhde-theme migrate`; once: nothing
happens when `lhde/settings.ini` exists; a report goes to `lhde/migration.log`):
1. `longhorn/themes/NAME.theme` (presets) -> `lhde/themes/<name>.theme` (file name lower case, spaces -> `-`, `-custom` when it
   would shadow a shipped theme): `[VisualStyle] msstyles` -> `style` (a data-directory path becomes the style's name), `[Frames]
   frame/frame_classic` -> `[Glass] frames/frames_classic` (`style` -> none), `[Composition]` -> `[Glass]` (follow/auto dropped),
   `[Shell] shell` -> `pack` (old pack names mapped), `[Desktop] icons longhorn` -> `longhorn4074`, `[Explorer] frame` -> `chrome`;
   the base through the aliases (`aero-patch` -> `aero4074`).
2. `settings.ini` is copied with its comments; `[theme] name` follows aliases and renamed presets; each `[override:NAME]` becomes
   `NAME-modified.theme` (keys mapped: taskbar -> Shell.taskbar, icons -> Desktop.icons, frame ->
   Explorer.chrome, composition -> Glass.composition (follow = auto), animations, wallpaper, mode; keys equal to the theme's own
   are dropped, no file when nothing is left) and `[theme] name` moves to it when it was the selected theme; `[override]` (all
   themes) goes into the selected theme's modified copy; the global fallbacks `[theme] composition`, `[theme] animations`,
   `[taskbar] kind`, `[start] menu`, `[desktop] icons`, `[explorer] frame` are removed (the theme decides; the report names a
   value that differed); an empty `[desktop] wallpaper` is removed.
3. `lhshell/state.ini`: user-facing keys an old lux left there move into settings.ini (lux's rules below), the original is kept
   as `state.ini.pre-lhde`.
4. `longhorn/settings.ini` is kept as `settings.ini.pre-lhde`.
5. Every run, idempotent (2026-10-29): the files that were scattered over `~/.config/{flxp,lhshell,lhwm,longhorn}` move into the
   two directories above: `flxp/flxp.ini` -> `lhde/flxp.ini`, where the keys that duplicated theme keys (`icons`, `frame`,
   `taskbar_kind`, `wallpaper`, `wallpaper_mode`, `themes_ini`), `nt5src` and the path overrides the imported Windows tree makes
   redundant (`font_path`, `longhorn_system32`, `longhorn_wim`, `explorer_resources`, `theme_path`) are removed and reported;
   `flxp/views`, `flxp/desktop-positions`, `~/.local/share/flxp/{mfu,run-history}`, `lhshell/state.ini`, `lhshell/mfu.ini` -> the state directory; `lhwm/keys` ->
   `lhde/keys`; a `longhorn/settings.ini` symlink left by an earlier migration is removed; an emptied old directory is removed,
   one still holding backups (`*.pre-lhde`, `*.pre-settings`) is left and named. A file that reappears in an old place (an older
   program) is moved by the next run unless the new one exists. Run by `lhde-session start`, luxshell and luxwm at startup, and
   the flxp programs when they first read their settings; the report is appended to `lhde/migration.log`.
6. Every run, idempotent: legacy sections in `lhde/settings.ini` itself (left by a program from before the theme files) are
   converted as in 2.: `[override:NAME]` -> `NAME-modified.theme`, `[override]` -> the selected theme's modified copy, and a
   `[desktop] wallpaper = PATH` (+ `mode`; the old Set as Desktop Background for every theme) -> the selected theme's current
   layer (`[current] Desktop.wallpaper / Desktop.mode`). The resolver applies none of them; of settings.ini's own keys it
   applies only `[desktop] wallpaper = none`.

Result on a copy of aap's config (2026-10-28): the four presets (Aero lotus, MyAero, MyLuna, MySlate4051) became
`aero-lotus`, `myaero`, `myluna`, `myslate4051`; `[override:aero4074] icons = xp` -> `aero4074-modified.theme` (Desktop.icons =
xp); `[override:aero5112]` -> `aero5112-modified.theme` (Glass.composition = off; taskbar classic and frame longhorn are 5112's
own now); `[override:aero5048]` left nothing (its values are basic5048's defaults); `[theme] name = aerovision` kept;
`[theme] composition/animations`, `[taskbar] kind`, `[start] menu`, `[desktop] icons` removed; state.ini had only runtime state.

One settings store for the whole desktop so luxwm, lux (the Lua/cairo Avalon-style shell, formerly
"lhshell") and the flxp programs (explorer, flxp-taskbar, flxp-desktop, the Display Properties
dialog, written with the "wux" win32 layer) read and write the same configuration. It complements
the theme registry (`data/themes.ini`, docs/history/THEMES.md) and the live root properties (`_LH_THEME`,
`_LHWM_*`, `_LHSHELL_*`).

## Files
`$XDG_CONFIG_HOME/longhorn/` (default `~/.config/longhorn/`):

- `settings.ini` — the current configuration (ini, `key = value`, `;`/`#` comments). Sections:
  ```
  [theme]      name = jade4074            ; a themes.ini entry or a preset (below); mirrored to _LH_THEME
               composition = follow        ; follow (themes.ini `composition`) | on | off
               animations = 4074plus       ; 4074plus | 4074 | invented | 5048 | none  (luxwm, runtime)
  [desktop]    wallpaper = /path.jpg       ; empty = the theme's (data/wallpapers.ini); none = no image (solid #3a6ea5); mode = fill | fit | center | tile | stretch
               icons = longhorn            ; icon set for flxp/flxp-desktop: longhorn | xp | 2k
               desktop_black = 0           ; lux: 1 = maximised mode also blacks out the desktop (M5a)
  [shell]      max_fade_ms = 0             ; lux: the bars' black layers in maximised mode, 0 = instant, else a fade (ms)
  [taskbar]    kind = avalon               ; avalon | classic | none ; autohide = 0 ; rows = 1
               classic_start_menu = flxp   ; flxp | flxp-classic
  [sidebar]    mode = full                 ; full | minimized | pinned ; width = 150 ; with_classic = 0 ; yield_bottom = auto
               stacking = top              ; lux: top (above the windows, strut, full height) | bottom (under the windows like the desktop, no strut, ends at the full-width taskbar); Win+S toggles
  [clock]      pinned = 1 ; pinned_pack = aero ; layout = default
  [quicklaunch] dir = ~/some/dir         ; lux: the Quick Launch tile's folder of .desktop files (default ~/.config/lhde/quicklaunch/)
  [start]      mfu_count = 6 ; userpane = 1 ; lux
  [start]      pinned = a.desktop;b.desktop ; menu = flxp   ; flxp | lux (provider when both exist)
  [commands]   logoff = ... ; standby = ... ; shutdown = ... ; restart = ... ; lock = ... ; picom = ... ; taskbar = ... ; run = ...
  [fonts]      smoothing = standard        ; off | standard | cleartype (flxp, luxwm's menus); luxshell uses fontconfig's
  [effects]    menu_shadows = 1            ; 0 | 1: XP's drop shadow under menus (wux's popup menus, luxwm's window menu)
  ```
  Unknown keys are kept by every writer (read, modify, write back the whole file preserving
  comments and order).
- `themes/NAME.theme` — user presets ("custom configs"), the Windows `.theme` idea: a theme name
  plus overrides. Any `.theme` here is a selectable theme next to the registry entries, and
  `_LH_THEME` may carry its name (lookup order: registry section, then `themes/NAME.theme`).
  ```
  [Theme]        name = My Jade ; base = jade4074
  [VisualStyle]  msstyles = ... ; color = ... ; size = ...     ; optional overrides of the base
  [Composition]  composition = on ; animations = 4074
  [Desktop]      wallpaper = ... ; mode = fill ; icons = xp
  [Frames]       frame = ... ; frame_classic = ...             ; luxwm theme dirs (rare)
  [Shell]        shell = aero ; startpanel = luna-blue         ; lux packs
  [Fonts]        fonts = ...
  ```
  Everything not overridden comes from the base registry entry. "Save as…" in the settings UI
  writes one of these from the current state.
- Program-private state stays private: lux's `~/.local/state/lhde/luxshell-state.ini` keeps only runtime
  state (tile order, launch counts, window positions) and migrates its user-facing keys to
  `settings.ini` once; flxp keeps `flxp.ini` for its own options; luxwm has none.

## Change notification
A writer saves the file, then sends a ClientMessage `_LH_SETTINGS_CHANGED` (format 32, data[0] =
a monotonically increasing serial, data[1] = 0 or a section hint atom) to the root with
`SubstructureNotifyMask`. The serial lives on the root as `_LH_SETTINGS_SERIAL` (CARDINAL[1], the last serial sent): a
writer reads it, adds one (masked to 31 bits), writes the property back, then sends the message with that value (flxp's
`shell::store::notify`, lux's `S.notify`; adopted by lux 2026-10-25). The hint is the section name's atom (`theme`, `desktop`,
`taskbar`, ...) when exactly one section changed, else 0; readers may use it to skip work but must not rely on it (a theme
change from the dialog usually also touches `[desktop] wallpaper`, so its hint is 0). Readers reload the file on that message (and may also inotify-watch it).
Theme changes are additionally published through `_LH_THEME` as today, so a reader that only
cares about the theme needs nothing new. Order for a theme change: write `settings.ini`, set
`_LH_THEME`, send `_LH_SETTINGS_CHANGED`.

lux's messages: data[0] is `_LH_SETTINGS_SERIAL` + 1, and never below the highest serial it has seen from anyone or its clock
seed (so it keeps increasing over lux restarts), data[1] the section atom when one section changed, else 0; several keys set while one
event is handled go out as one message. Every message (its own included) makes lux read the file again and apply the keys whose
value differs from what runs (its own then differ in nothing).

## Resolved theme: `_LH_THEME_RESOLVED` (added 2026-10-25)
So that luxwm and flxp can follow a preset without parsing `themes/*.theme` themselves, lux publishes the theme in effect as a
root property, on every theme change and whenever a key that feeds it changes:

- `_LH_THEME_RESOLVED`, UTF8_STRING: `key=value;key=value;...`, in this order: `name` (the selected name: a registry name or a
  preset's file name), `base` (the registry entry it builds on; = `name` for a registry theme), `title`, then `msstyles`,
  `color`, `size`, `frame`, `frame_classic`, `shell`, `fonts`, `wallpaper`, `icons`, `composition`, `animations`, `mode`,
  `startpanel`. A `;` or `\` inside a value is escaped with `\` (`fonts` lists directories separated by `;`, so it appears as
  `fonts=/a\;/b`). Paths are absolute. Empty value = not set (e.g. `animations=`: the program's own default).
- Values are the effective ones: a preset's own key, else settings.ini (`[desktop] wallpaper/mode/icons`, `[theme]
  animations`), else the registry entry (`icons` falls back to the entry's `flxp_icons`). `wallpaper=none` means no image.
  `wallpaper` / `mode` follow the order of docs/history/THEMES.md "Wallpapers" (2026-10-25): `[desktop] wallpaper = none`, `[override:NAME]
  wallpaper`/`mode`, the preset's `[Desktop]`, `[desktop] wallpaper`/`mode`, a themes.ini `wallpaper` key (none ships), then
  `data/wallpapers.ini [NAME]` and `[default]`. The property's format did not change; registry themes now always carry a
  `wallpaper=` (from wallpapers.ini) where before it came from their themes.ini key. Paths are under
  `/u/aap/fun/longhorn/resources/` (docs/history/THEMES.md "Resources").
- `_FLXP_WALLPAPER` (2026-10-25, lux, interim): flxp-desktop takes a registry theme's wallpaper from the section's `wallpaper` key,
  which no section has any more, and reads `_LH_THEME_RESOLVED` only for presets. So that it needs no change, lux also writes the
  effective wallpaper to flxp-desktop's own root property `_FLXP_WALLPAPER` (`MODE PATH`, or `none`; UTF8_STRING) whenever it
  publishes the resolved view and the value differs. flxp-desktop watches that property and lets it beat its other sources, as for
  "Set as Desktop Background" (which writes the same file to `[desktop] wallpaper`, so lux then publishes the same value). Checked
  on the lab: flxp-desktop follows zune -> slate4051 live. Side effect: a `wallpaper =` in `flxp.ini` no longer beats the theme's
  while lux runs. Once flxp-desktop reads `wallpaper=`/`mode=` from `_LH_THEME_RESOLVED` for every theme (or reads
  `wallpapers.ini`), lux can stop writing `_FLXP_WALLPAPER`.
  `composition` is the theme's frame mode in the registry vocabulary, `auto` | `off` (a preset's `on` is written `auto`), not
  the `[theme] composition` compositor switch.
- `_LH_THEME` holds the **selected name**, a registry name or a preset's file name (section "Preset names on `_LH_THEME`"
  below; until 2026-10-25 lux wrote a preset's `base` there, which is no longer done). For its own switches lux writes
  `_LH_THEME_RESOLVED` first, then `_LH_THEME`, then sends `_LH_SETTINGS_CHANGED`. When somebody else writes a preset name to
  `_LH_THEME` (flxp-settings, a picker, `xprop`), the order is the other way round: `_LH_THEME` first, `_LH_THEME_RESOLVED`
  follows when lux has resolved it. A reader therefore uses `_LH_THEME_RESOLVED` only when its `name` equals the current
  `_LH_THEME`, otherwise it resolves the name itself (next section), and it also watches `_LH_THEME_RESOLVED` so it sees the
  republished view (luxwm does both).
- At startup lux takes the root's `_LH_THEME` as it is. Only for a session from before the decision (root = a preset's base,
  `_LH_THEME_RESOLVED` and `[theme] name` = that preset) it selects the preset again and puts its name on the root.
- Readers: luxwm (frames from `msstyles`/`color`/`size`/`frame`/`frame_classic`, `fonts`, `composition`; the preset file's own
  `animations`), flxp (`lookup_session_theme`: `msstyles`/`color`/`size`, `icons`, `wallpaper`/`mode`; preset names in its
  theme lists).

## Notes for the flxp team (2026-10-25: resources/, XP styles, wallpapers.ini)
- **Wallpaper and icons: use the resolved view.** `_LH_THEME_RESOLVED` carries the effective `wallpaper=`, `mode=` and `icons=` for
  registry themes as well as presets. flxp-desktop should read them there (when `name=` equals `_LH_THEME`) instead of the registry
  section's `wallpaper` / `flxp_icons`; the themes.ini `wallpaper` keys are gone (docs/history/THEMES.md "Wallpapers"). Until then lux keeps
  `_FLXP_WALLPAPER` in step (above). Without lux running, a reader that wants the theme's wallpaper reads `data/wallpapers.ini`
  (`[NAME]` then the entry's aliases then `[default]`, `file` relative to `[default] root`).
- **`[sidebar] stacking` in Display Properties** (redefined 2026-10-25). The key is shared and now says whether the sidebar is
  over or under the windows: `top` = above the windows (`_NET_WM_STATE_ABOVE`), with its strut, full height at the corner, the
  taskbar shortened (4074); `bottom` = part of the desktop: under the windows (`_NET_WM_STATE_BELOW`), **no strut** (maximised
  windows cover it), the taskbar spans the full width over it and the sidebar ends at its top (M4). Win+S (luxwm's default
  `Mod4+s shell sidebar-stacking toggle`) flips it at once and lux saves it. lux's settings flyout offers it as **"Sidebar on top
  of windows" / "Sidebar under windows (desktop)"** (`luxshell/settings.lua`); the dialog's Appearance tab (next to the taskbar
  kind) should use the same two labels and values. A change written by the dialog with `_LH_SETTINGS_CHANGED` is applied live
  (checked on the lab). It is a global key, not per theme.
- **`[sidebar] layer` is removed** (it was above|below, the sidebar over or under the windows, which `stacking` now covers).
  Readers should ignore it and writers should not write it; lux folds an existing one into `stacking` (`below` → `bottom`,
  `above` dropped) and deletes the key, also when a writer brings it back.
- **`[override:NAME]` and aliases.** `[aero4074]` is now `[aero-patch]` with `aero4074` as an alias. lux and `lhwall.sh` read
  `[override:aero-patch]` first, then `[override:aero4074]`, so an override written before keeps working; `store::theme_value`
  should do the same (or the dialog could rename the section once). New keys there: `wallpaper`, `mode` ("this theme only").
- **flxp_msstyles everywhere.** The registry's styles now live in `resources/msstyles/` under build-specific names
  (`jade4074.msstyles`), which `loadable_msstyles` cannot map to an XP backport by name; every Longhorn-format entry therefore sets
  `flxp_msstyles` (relative to the registry, `../resources/xp-backport/...`).
- **Styles to test in flxp:** `royale`, `zune`, `embedded`
  and `luna-xp`: all v3, so explorer, flxp-taskbar and the start panel load them directly (the lab showed flxp-taskbar with Royale,
  Noir, Zune and Embedded bars). Their `Shell/<scheme>/ShellStyle.dll` (task pane) is in `resources/xp-themes/<Style>/Shell/`.
  Library gaps: `plex4015.msstyles` (4015, early v4) loads with truncated class names (`indow` for `Window`), and `aero5048`'s
  scheme is `NormalColor`, not `Default` (the registry now says so).

## lux's migration from `~/.config/lhshell/state.ini` (2026-10-25)
The first lux with the shared store moves the user-facing keys of its old state.ini into settings.ini: `[shell] theme` ->
`[theme] name`, `[shell] composition` -> `[theme] composition`, `[shell] wallpaper = 0` -> `[desktop] wallpaper = none` (`1`
is the default and is dropped), `[shell] desktop_black` -> `[desktop] desktop_black`, `[taskbar] enabled` -> `[taskbar] kind`;
`[taskbar] autohide / rows / classic_start_menu`, `[sidebar] mode / width / stacking / with_classic / yield_bottom` (an old `layer =
below` becomes `stacking = bottom`), `[clock]
pinned / pinned_pack / layout`, `[start] pinned / mfu_count / userpane` and all of `[commands]` keep their names. A key that
settings.ini already has is not overwritten. The keys are then removed from state.ini (sections left empty go too), which keeps
`[sidebar] tiles` (the tile order) and anything unknown; the original is kept once as `state.ini.pre-settings`. Then lux
writes its missing defaults (marked in USAGE.md) and sends one `_LH_SETTINGS_CHANGED`. `lhde-session` and `lhde-session`
read and write settings.ini now (a preset in `[theme] name` gives luxwm its base).

## Who writes what
- The **Display Properties dialog** (flxp; tabs Themes / Desktop / Appearance / Effects, Apply does
  not close) is the full settings UI: every key above, presets (open/save), and icons. It is the
  Control Panel ▸ Display item of flxp-taskbar's start panel and of lux's start menu.
- **lux's settings flyout** stays the quick switch (theme and presets, Save as preset, composition, taskbar kind and autohide,
  start menu, sidebar mode, wallpaper on/off). lux writes `themes/NAME.theme` on Save as preset and publishes
  `_LH_THEME_RESOLVED`.
- **luxwm** reads `[theme] animations` (runtime: it sets the `_LHWM_ANIMATE` value picom's rules key
  on, so switching needs no rebuild; a selected preset's own `[Composition] animations` wins over it); the compositor switch
  `[theme] composition` is lux's (lux starts/stops picom, luxwm follows `_NET_WM_CM_S<screen>`); everything else it takes from
  the registry and `_LH_THEME`, resolving preset names itself.
- **flxp programs** read `[desktop] icons`, `[fonts]`, `[desktop] wallpaper` (flxp-desktop), and the
  theme through the registry as before.

## Conflicts
Last writer wins per key; writers never drop keys they do not know. If `settings.ini` and the root
properties disagree at startup, the root properties win for the running session (they reflect
what is live), and the first writer brings the file back in line.

## Preset names on `_LH_THEME` (decision 2026-10-25, after flxp-settings landed)
`_LH_THEME` carries the **selected name as the user chose it**: a registry section or a preset
(`themes/NAME.theme`). Every reader resolves a name the same way: registry section first, then
the preset file (base section + overrides). flxp does this already (`lookup_session_theme`);
luxwm and lux must too. lux additionally publishes `_LH_THEME_RESOLVED` as a convenience and must
NOT rewrite `_LH_THEME` to the preset's base any more (that confused flxp). A reader that finds
neither a section nor a preset keeps its current theme and logs the name.

The resolution rule, the same in every reader:
1. `NAME` is a registry section, or listed in a section's `aliases` (luxwm and lux also accept a theme directory path whose last
   component is one): that entry. Writers may replace an alias by the section name on `_LH_THEME`; nobody replaces a preset.
2. Else `NAME` (no `/`, not starting with `.`) names `$XDG_CONFIG_HOME/longhorn/themes/NAME.theme` (default
   `~/.config/longhorn/themes/`): the `[Theme] base` registry entry with the preset's non-empty keys merged over it. Paths in
   the preset are absolute, `~/...`, or relative to the preset file's directory; `fonts` is a `;` list, each entry resolved
   alike. `[Composition] composition = off` means the classic frame (registry `composition = off`); `on`/`auto` mean the
   composited frame while a compositor runs. Sections and keys are matched case-insensitively.
3. A reader may take the values from `_LH_THEME_RESOLVED` instead of the file when its `name=` is `NAME` (lux's view: the
   same merge, plus settings.ini fallbacks for `wallpaper`/`mode`/`icons`/`animations`). luxwm prefers it; flxp currently reads
   the file (see "Status" below).
4. Neither: keep the current theme and log the name.

Status (2026-10-25): luxwm resolves presets on `_LH_THEME` at startup and on change (log `luxwm: theme NAME: preset (base BASE)
from SOURCE: ...`), re-applies when `_LH_THEME_RESOLVED` for the selected preset is republished, publishes `_LHWM_THEME` as
before (the derived spec) and never rewrites a preset name. lux writes the selected name (preset or registry) to `_LH_THEME`
and does not write back a preset name somebody else set (no bounce). Tested end to end with flxp-settings on the lab
(`lhshell/compare_settings_sync.png`, docs/USAGE.md "Settings and presets").

Key audit against flxp-settings (flxp bf79f78, `src/settings/settings.cpp` `apply()`; the dialog writes only what changed):

| dialog control | settings.ini | values written | read by (ours) |
| --- | --- | --- | --- |
| Themes: theme | `[theme] name` (+ `_LH_THEME`) | registry name or preset file name | lux (`reload_settings`, `_LH_THEME`), luxwm (`_LH_THEME`) |
| Appearance: composition | `[override:NAME] composition` or the preset (was `[theme] composition`) | follow, on, off | lux (starts/stops picom, also at startup; luxwm follows the compositor) |
| Appearance: animations | `[theme] animations` | 4074plus, 4074, invented, 5048, none | luxwm `settings_load`, lux (`_LH_THEME_RESOLVED`) |
| Appearance: taskbar | `[override:NAME] taskbar` or the preset (since the per-theme change; was `[taskbar] kind`) | avalon, classic, none | lux (live switch, per-theme chain) |
| Appearance: start menu | `[override:NAME] start` or the preset (was `[start] menu`) | flxp, lux | lux (provider check, classic bar's menu; per-theme chain) |
| Appearance: explorer frame | `[explorer] frame` | (empty), xp, longhorn | flxp only |
| Desktop: background | `[desktop] wallpaper` | path or empty (= the theme's) | lux (feh; `none` is lux's own value) |
| Desktop: position | `[desktop] mode` | fill, fit, center, tile, stretch | lux (feh modes; `fit` = `--bg-max`) |
| Desktop: icons | `[desktop] icons` | longhorn, xp, 2k | lux (`_LH_THEME_RESOLVED` only), flxp |
| Effects: smoothing | `[fonts] smoothing` | off, standard, cleartype | flxp, luxwm's menus (luxwm's captions: the frame set; luxshell: fontconfig) |
| Effects: menu shadows | `[effects] menu_shadows` | 1 (default), 0 | flxp's programs when they start (SPI_SETDROPSHADOW), luxwm's window menu when it opens |
| Save As | `themes/NAME.theme` | `[Theme] name/base`, `[Composition] composition` (unless follow) + `animations`, `[Desktop] wallpaper/mode` (if set) + `icons` | lux `S.resolve`, luxwm `theme_preset_lookup` |

`[start] pinned` is not written by the dialog; both sides read it as a `;` list (lux: desktop ids; flxp also takes
`terminal`, `explorer` and command lines, which lux skips).

## Per-theme settings (added 2026-10-25, flxp)
The Appearance values (taskbar kind, start menu, composition, animations, explorer frame) and the icon set belong to the
theme: switching themes brings the theme's own values without touching any global key. For theme NAME (registry section or
preset) the value of KEY is the first set of:
1. `settings.ini [override:NAME] KEY` - the user's override of a registry theme (keys `taskbar`, `start`, `composition`,
   `animations`, `icons`, `frame`; the values as in the dialog: `taskbar = avalon|classic|none`, `start = flxp|lux`,
   `composition = follow|on|off`, `animations = ...`, `icons = longhorn|xp|2k`, `frame = auto|xp|longhorn`);
2. the preset's own key when NAME is a preset: `[Shell] taskbar`, `[Shell] start`, `[Composition] composition`,
   `[Composition] animations`, `[Desktop] icons`, `[Explorer] frame` (new: `[Shell] taskbar/start`, `[Explorer] frame`);
3. the registry entry (the preset's base): `taskbar`, `start`, `animations`, `flxp_icons`, `flxp_frame` (docs/history/THEMES.md
   "Per-theme shell settings"); `composition` has no registry default here;
4. the global key as before: `[taskbar] kind`, `[start] menu`, `[theme] composition`, `[theme] animations`, `[desktop] icons`,
   `[explorer] frame` - the fallback for themes that set nothing (lux keeps working with it);
5. the built-in default (avalon, flxp, follow, 4074plus, xp, auto).
Display Properties shows the effective value for the theme selected on its Themes tab and stores a change for that theme
only: into the preset when a preset is selected, else into `[override:NAME]`; a value equal to what the theme gives without
it (3-5) removes the key (and an emptied `[override:NAME]` section). It no longer writes the global keys of these settings.
Save As writes only the keys that differ from the base (3-5). A flxp.ini key of the same meaning (`icons`, `frame`,
`taskbar_kind`, `start_menu`) would beat every theme; the dialog blanks it when it stores the value for the theme.
`[override:NAME]` sections are kept by every writer like any unknown section. Readers to follow: lux (`[taskbar] kind`,
`[start] menu`, `[theme] composition` through this chain; `_LH_THEME_RESOLVED`'s `icons`/`animations` likewise), luxwm
(`animations`). Note the order differs from the `_LH_THEME_RESOLVED` rule above (preset > settings.ini > registry): the
registry default now comes before the global key.

## Per-theme overrides: `[override:NAME]` (found 2026-10-25 in flxp-settings' output)
flxp's Display Properties writes the Appearance choices **per theme** into sections named
`[override:<theme name>]` with keys `taskbar = avalon|classic|none`, `start = flxp|lux`,
`icons = longhorn|xp|2k`, `frame = auto|...` (observed in aap's settings.ini), not into the global
`[taskbar] kind` / `[start] menu` / `[desktop] icons`. Resolution rule, now part of the spec: for
the current theme (registry or preset name), a key in `[override:<name>]` wins over the global
section; the global key is the default for themes without an override. Readers (lux, luxwm, flxp)
apply this when the theme changes and on `_LH_SETTINGS_CHANGED`. Key mapping: `taskbar` ↔
`[taskbar] kind`, `start` ↔ `[start] menu`, `icons` ↔ `[desktop] icons`, `frame` ↔ flxp's
`[explorer] frame`. The dialog should keep writing the global key when the user did not ask for a
per-theme value (today it writes only the override, which lux did not read: that is why "the
taskbar setting had no effect").

Status (2026-10-25, lux): lux follows the full chain of "Per-theme settings" above (`[override:NAME]` > the preset's own key >
the registry entry > the global key > default; session.lua `S.theme_value`, the same table as flxp's `theme_keys`) for
`taskbar`, `start`, `composition`, `icons` and `animations` (the last two in `_LH_THEME_RESOLVED`), at startup, on every theme
change and on `_LH_SETTINGS_CHANGED`. Its settings flyout and `_LHSHELL_COMMAND taskbar KIND` store like the dialog
(`S.set_theme_value`): `[override:NAME]` for a registry theme, the preset's own key for a preset, the key removed when it equals
the theme's default (3-5); they no longer write the global keys, because the registry's `taskbar`/`start` (set on every entry)
come before them and a global write would have no visible effect. The compositor rule is applied at startup as well (also for
`follow`), and `lhde-session` starts picom only when the resolved theme wants one (`lhwall.sh lhcomp_wanted`).

Note for flxp: with the registry before the global key, writing the global key "when the user did not ask for a per-theme
value" would not change anything visible for registry themes, so lux does not do it and the dialog need not either. What would
help: (a) when the dialog removes an override because the value equals the theme default, it already does the right thing;
(b) `shell::store::notify` with hint `theme` after `set_theme_values` (it does); (c) if the dialog ever offers "for all themes",
that should write the global key **and** remove the matching `[override:*]` keys and preset keys, otherwise the registry
defaults still win; (d) flxp-taskbar / flxp-desktop should resolve `icons` through the same chain (override first).

## "All themes": `[override]` (added 2026-10-25, flxp)
Answer to (c) above. Writing only the global key cannot work for registry themes (their `taskbar`/`start`/... come first),
so an explicit "for all themes" choice gets its own layer: `settings.ini [override] KEY` (same keys and values as
`[override:NAME]`). The final chain for theme NAME and KEY, first one set:
1. `[override:NAME] KEY`
2. the preset's own key (NAME a preset)
3. `[override] KEY` - the user's choice for all themes
4. the registry entry (a preset: its base's)
5. the global key (`[taskbar] kind`, `[start] menu`, `[desktop] icons`, `[explorer] frame`, `[theme] composition`,
   `[theme] animations`)
6. the built-in default (avalon, flxp, follow, 4074plus, xp, auto).
Writers, for "all themes" = VALUE: set `[override] KEY = VALUE` and the global key = VALUE (for readers that know only
the global key), remove KEY from every `[override:NAME]` section and every preset's own key. "Back to each theme's
default": remove `[override] KEY` and the per-theme keys (the global key stays). "This theme only" as before; the "theme's
default" a value is compared with (to drop it) is now 3-6. flxp's Display Properties offers the three as a scope combo
per value and shows which layer a value comes from. With no session theme a reader uses 3, then 5. Readers to follow:
lux (`S.theme_value`: one more lookup between the preset and the registry), luxwm (`animations`). flxp-taskbar and
flxp-desktop resolve `taskbar`, `start`, `icons` through this chain (shell::setting -> store::value_for, the session theme).
