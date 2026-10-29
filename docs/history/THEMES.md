# Themes: one setting for the window manager, the shell and the clients

> **Superseded in step 4** (2026-10-28) by theme files: docs/THEME-FORMAT.md (format, resolver, status in section 12). The
> registry `data/themes.ini` and `data/wallpapers.ini` are gone; liblhde generates a compatible `themes.ini` for flxp. What
> follows is the registry era, kept as history.

> **Imported from the Longhorn X11 lab** (`~/fun/longhorn/x11` at d353b69, THEMES.md) into lhde in step 3 of docs/LAYOUT.md. Program and path names are updated to lhde (lhwm → luxwm, lhwmc → luxwmc, lhshell → luxshell, lhnav → luxnav; x11/... and lhwm/themes, lhshell/themes, lhshell/assets → the lhde tree and data directories, see docs/LAYOUT.md and tools/lhde-import); the X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager name "lhwm", luxnav's C API (`lhnav_*`, `lhnav.h`), configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and setting values (`header_engine = lhnav`, `classic_start_menu = lhshell`) keep their names. Dated sections are history: they describe the x11 tree as it was (its compare_*.png and screenshots stay in the research workspace).

For the authors of clients that want to follow the Longhorn lab's theme, flxp first (its uxtheme loads real
`.msstyles`). Windows has one setting (the visual style with its colour scheme and size) and one broadcast
(`WM_THEMECHANGED`). Here the setting is a root window property holding a theme *name*, the names are defined in
one registry file, and the broadcast is the X PropertyNotify on that property. Design: `SHELL-DESIGN.md` 6.1.

## The registry: `data/themes.ini`

Plain ini. One section per theme name, `key = value` lines.

```ini
[jade4074]
title = Jade (4074)
fonts = ../resources/fonts/4074
msstyles = ../resources/msstyles/jade4074.msstyles
color = Default
size = Normal
frame = data/frames/jade4074
frame_classic = data/frames/jade4074-classic
shell = data/packs/4074-aero
composition = auto
aliases = jade
```

| Key | Meaning | Who reads it |
| --- | --- | --- |
| `msstyles` | the visual style file (a real PE `.msstyles`) | uxtheme clients: flxp's `FLXP_THEME` |
| `color`, `size` | colour scheme and size name inside it. The 4074 and 4051 `.theme` files say `ColorStyle=Default`, `Size=Normal` (not Luna's `NormalColor`/`NormalSize`) | `FLXP_THEME_COLOR`, `FLXP_THEME_SIZE` |
| `frame` | luxwm frame theme directory: the DWM glass frame for `composition = auto` themes, the msstyles frame otherwise | luxwm |
| `frame_classic` | luxwm frame theme without a compositor, or always with `composition = off`. Empty, missing or `style` (default since 2026-10-27): the entry's own `msstyles` in msstyles mode, its Window class drawn live through libuxtheme (`docs/LUXWM.md` "Classic fallback"); if the style has no Window class, the frame directory's theme.ini `fallback_theme` | luxwm |
| `shell` | lhshell theme pack directory (taskbar and sidebar imagery) | lhshell |
| `startpanel` | optional: a start panel other than the pack's own, by name from `data/packs/startpanels.lua` (`luna-blue`, `luna-olive`, `luna-silver`: XP's start panel, `assets/startpanel-luna-*` from `shellpack.sh`). The Luna themes use it: Slate bars with Luna's start menu | lhshell |
| `wallpaper` | no longer used by the shipped entries: a theme's wallpaper is in `data/wallpapers.ini` ("Wallpapers" below). A `wallpaper` key in a section still wins over that file (for a local experiment) | lux, `lhwall.sh`; flxp-desktop reads it from the section |
| `composition` | `auto`: glass while a compositor runs, classic frames otherwise. `off`: always classic, like Longhorn's `USESCOMPOSITION=FALSE` | luxwm; clients see the result in `_LHWM_COMPOSITED`. luxshell also starts (auto) or stops (off) the display's picom when the theme changes (USAGE.md "Compositor rule") |
| `aliases` | other names that select this section (the old luxwm theme directory names: `aero`, `jade`, `slate4074-classic`, ...) | everyone who resolves a name |
| `title` | display name for theme lists (settings UI) | lhshell |
| `fonts` | font directories (or font files), `;` separated: registered as fontconfig application fonts while the theme is active ("Fonts" below) | luxwm, luxshell; flxp may use it |

Rules:
- Paths are absolute or relative to the registry file.
- Lists (`aliases`) are separated by spaces.
- Comments are lines starting with `;` or `#`, or a `;` / `#` that follows whitespace at the end of a line.
- Unknown keys are ignored, so a section can carry keys for one consumer only.
- A name is looked up as a section name first, then in every section's `aliases`. luxwm and luxshell also accept a path and
  look up its last component (`themes/aero` selects `aero-patch`).

Shipped names (paths under `../resources/`, "Resources" below):

| Name | msstyles | Frames | Shell pack |
| --- | --- | --- | --- |
| `aero-patch` (aliases `aero4074`, `aero`) | `msstyles/aero-patch.msstyles`, the longhorn.ms Aero patch style for 4074 (not a Microsoft file; `res/aero/aero.msstyles/` is its extracted resources) | `aero` glass, `jade4074-classic` without composition | aero |
| `aerovision` ("Aero vision", experimental, `NAVBAR.md`) | the same aero-patch style, fonts `fonts/4074` | `aerovision` glass (aero plus the vision header: 15 px deco, Explorer plate cropped not stretched, 13-row buttons, `caption_border = 2`; `docs/LUXWM.md` "Aero vision"), `jade4074-classic` without composition; wallpaper 4051's Bliss (the concept's sky) | aero |
| `jade4074` (alias `jade`) | `msstyles/jade4074.msstyles` | `jade` glass, `jade4074-classic` without composition | aero |
| `jade4074-classic` | the same Jade style | always `jade4074-classic` (`composition = off`) | aero |
| `slate4074` (alias `slate4074-classic`) | `msstyles/slate4074.msstyles` | `slate4074-classic` | slate |
| `slate4051` (alias `slate4051-classic`) | `msstyles/slate4051.msstyles` | `slate4051-classic` | slate4051 |
| `luna` (aliases `luna-blue`, `luna4051`) | `msstyles/luna4051.msstyles` (4051's Longhorn-compiled Luna), `color = NormalColor`, `size = NormalSize` | `luna-blue` | slate (stand-in), `startpanel = luna-blue` |
| `luna-olive` | the same, `color = HomeStead` | `luna-olive` | slate, `startpanel = luna-olive` |
| `luna-silver` | the same, `color = Metallic` | `luna-silver` | slate, `startpanel = luna-silver` |
| `luna-xp` | `msstyles/luna-xp.msstyles` (XP SP3's own v3 Luna), `NormalColor` | live from the style (no directory) | slate, `startpanel = luna-blue` |
| `royale` (aliases `mce`, `energy-blue`) | `msstyles/royale.msstyles`, `NormalColor` | live from the style | slate (stand-in) |
| `zune` | `msstyles/zune.msstyles`, `NormalColor` | live from the style | slate (stand-in) |
| `noir` (alias `royale-noir`) | `msstyles/noir.msstyles`, `color = Metallic` (the file's NormalColor is plain Royale) | live from the style | slate (stand-in) |
| `embedded` | `msstyles/embedded.msstyles`, `NormalColor` | live from the style | slate (stand-in) |
| `aero5048` | `msstyles/aero5048.msstyles`, `color = NormalColor` (its only scheme; was `Default`, which the library replaced with a warning), fonts `fonts/5048` | `aero5048` glass; without composition 5048's own Window class (`frame_classic = style`) | aero |
| `aero5112` | `msstyles/aero5112.msstyles` (Vista 5112's, a v4 Longhorn-format style libuxtheme reads), `NormalColor`, fonts `fonts/5112` (Segoe UI 0.89, four faces) | `aero5112` glass (DWMWindow streams, COMPOSITION.md 21); without composition 5112's own Window class (`frame_classic = style`) | aero5112 (5112 Start pill, bar, task buttons) |

`[aero4074]` was renamed `[aero-patch]` on 2026-10-25 (the honest name: it is the community patch style, not 4074's own);
`aero4074` stays an alias, so `_LH_THEME = aero4074`, `LAB_THEME=aero4074`, presets with `base = aero4074` and settings.ini
`[theme] name = aero4074` keep working (luxwm and lux write back `aero-patch`). A settings.ini `[override:aero4074]` written before
still applies: lux and `lhwall.sh` look up `[override:NAME]` for the section name first, then for each alias. flxp's
`store::theme_value` reads only `[override:<section>]` (note for the flxp team below).

## Resources (`../resources/`, 2026-10-25)
Every asset the registry and `wallpapers.ini` name is under `/u/aap/fun/longhorn/resources/` (index: `resources/README.md`), and the
registry names them relative to itself (`../resources/...`): `msstyles/<name>.msstyles` (symlinks into the extracted builds, the
`XP_Themes.zip` styles in `xp-themes/`, a copy of the Wine prefix's XP Luna), `fonts/4074` and `fonts/5048`, `system32/4074` and
`system32/4051` (`flxp_longhorn_system32`, `luxshell/shellpack.sh`), `xp-backport/` (flxp's `flxp_msstyles`) and `wallpapers/`.
`frame`, `frame_classic` and `shell` still name our own `data/frames/*` and `data/packs/*` directories. The luxwm theme
directories' own `theme.ini` (`msstyles =`, `font_files =`) and `luxwm/tests/regress.sh` (`FRAMETEST_FONTS`) point there too.
lux publishes the paths with the directory made canonical but the file name kept (`/u/aap/fun/longhorn/resources/msstyles/
jade4074.msstyles`, not the `wim/` file the link points to); luxwm publishes `_LHWM_THEME` with the registry's relative form
(`.../x11/../resources/...`).

Because flxp finds an XP backport by the style's file name (`jade.msstyles` -> `xp-backport/jade/jade.msstyles`) and the names
under `resources/msstyles/` carry the build (`jade4074.msstyles`), every Longhorn-format entry names its backport in
`flxp_msstyles` (Jade's for aero-patch/jade4074/jade4074-classic, Slate's for slate4074/slate4051, `luna-xp.msstyles` for the Luna
entries). aero5048 has no backport of its own and takes Jade's (2026-10-25), like aero-patch; aero5112 still has none.

## Microsoft's later XP styles (`royale`, `zune`, `noir`, `embedded`; 2026-10-25)
From `XP_Themes.zip` (unpacked into `resources/xp-themes/`): Royale (Media Center Edition 2005), Zune (2006), Royale Noir and Embedded
(2008). All are XP-format (PACKTHEM_VERSION 3), one `.msstyles` each with the scheme list inside (`ut-dump FILE`): Royale NormalColor
("default blue color scheme"), Zune NormalColor, Royale Noir NormalColor ("Royale 2004") and Metallic ("Royale Noir 2004"), Embedded
NormalColor; sizes NormalSize, LargeFonts, ExtraLargeFonts. A v3 style needs nothing besides the file (no `.mui`, no scheme
directory): libuxtheme and flxp's Wine uxtheme read the schemes from its resources. Each style also has a `Shell/<scheme>/ShellStyle.dll`
(explorer's task pane), which nothing of ours reads.

The entries have no `frame` directory: luxwm draws the frame live through libuxtheme (as `[luna-xp]`), `composition = off`. lux has no
pack for them; `shell = data/packs/4074-slate` stands in (it only matters with `taskbar = avalon` or the sidebar). `taskbar = classic`
and `start = flxp`: flxp-taskbar and its start panel load the style itself (its uxtheme reads v3), so the bar is Royale's, Zune's,
...; `flxp_frame = xp`, `flxp_icons = xp`. `fonts = ../resources/fonts/4074` gives Trebuchet MS (all four captions) and Tahoma;
Franklin Gothic Medium (their start button text) is not in 4074's Fonts.

luxwm result (frametest through the library, `lhwm/compare_xp_styles.png`, and on the lab with `_LH_THEME` switched live): caption,
borders and buttons of all four load and draw from the styles; margins 4/4/30/4 like Luna; caption fonts Trebuchet MS Bold 13 px
(Royale, Noir) and 11 px (Zune and Embedded ask for 8 pt). Royale's left border is the style's own 4 px column, pixel-identical
to `ut-dump --render Window 7 1`. No XP screenshots of these styles exist locally (`~/src/flxp/screenshots`, `~/src/flxp/doc`), so only
Luna has a reference row in the image; the others are checked against the library's own rendering only. `plex4015.msstyles` (build
4015's Plex) is in `resources/msstyles/` but has no entry: libuxtheme reads that early Longhorn format's class names with their first
letters cut off (`indow`, `artPanel`, `crollBar`), so there is no `Window.CAPTION` (gap for the flxp side).

## Wallpapers (`data/wallpapers.ini`, 2026-10-25)
A theme's wallpaper is chosen in one file, `wallpapers.ini` next to the registry (`LH_WALLPAPERS_INI` overrides the path):
`[default] root = ../resources/wallpapers` (relative to the file), `file`, `mode`; one `[NAME]` section per registry entry with
`file` (relative to `root`, absolute, or `~/...`) and `mode` (fill | fit | center | tile | stretch). A section is looked up by the
registry name, then by the entry's aliases; a preset uses its base's section. The effective wallpaper, first set:

1. settings.ini `[desktop] wallpaper = none` (no image, solid #3a6ea5);
2. settings.ini `[override:NAME] wallpaper` (and `mode`), NAME the selected theme or preset (a registry entry's aliases too);
3. the preset's own `[Desktop] wallpaper` (and `mode`);
4. settings.ini `[desktop] wallpaper` (with `[desktop] mode`);
5. a `wallpaper` key in the themes.ini section (none ships; kept so an experiment does not need wallpapers.ini);
6. `wallpapers.ini [NAME]`;
7. `wallpapers.ini [default]`.

The mode goes with the layer that gave the file; a preset's `[Desktop] mode` also applies to the theme's file; else `[desktop]
mode`, else fill. (Before, `[desktop] mode`, which lux writes as `fill` by default, applied to the theme's wallpaper too.) lux
(`session.lua S.theme_wallpaper`, `runtime.lua effective`) and `lhwall.sh` (the session's first root background) follow this
order, and lux publishes the result as `wallpaper=` / `mode=` in `_LH_THEME_RESOLVED` as before. Choices and why: the comments in
`wallpapers.ini`; what each build ships: `resources/wallpapers/README.md`.

### luxshell packs (`shell =`)

A pack is `data/packs/<name>/shell.lua` returning one table (bar sizes, imageres ids, tiles, menus, start panel). These ship (and `aero5112`, data/packs/5112-aero):

| Pack | Used by | Look |
| --- | --- | --- |
| `aero` | aero-patch, jade4074, jade4074-classic | Aero_*Resources (imageres 1167-1201): glass bars, 30 px rows, nearest-neighbour scaling |
| `aerovision` | aerovision | the `aero` table with `taskbar_text = "glow"` (below) |
| `slate` | slate4074, the Luna themes | Slate_*Resources (the 1000-1162 ids): the 4074 look, 30 px rows, nearest-neighbour scaling, pinned clock |
| `slate4051` | slate4051 | the `slate` table with 4051's differences: bilinear scaling, 29 px bar, `imageres = "imageres-4051"` (the 4051 imageres.dll images that differ, from `shellpack.sh`), clock tile in the list (`clock_pinned = false`), tiles tray + clock |

Keys added for this (2026-10-24): `filter = "nearest" | "bilinear"` is the pack's image scaling filter. Longhorn's
HackImages are stretched to their element size; 4074 (and the builds after ~4060) did it nearest-neighbour, which gives the
blocky start flag, 4051 bilinear. luxshell applies it to stretched nine-grid pieces and to magnified images (a node's own
`filter` wins; shrinking, such as the clock's 260 px layers, stays smooth). Default `bilinear`. `imageres = DIR` (assets
directory searched before `assets/imageres`), `clock_pinned = false` (the `[clock] pinned` default), `taskbar.bg_filter`
(the bar background's own filter). A pack may `dofile` another and change a few keys, as `slate4051` does.

`taskbar_dy = N` (default 0; `aero` and `slate` set 1, `aero5112` and `slate4051` leave 0 (4051's pill covers the bar's top row, `lhshell/compare_4051_start_top.png`); a Longhorn-packs tweak) draws the bar's content (Start button, task buttons, toggle, tray, clock) N px lower inside the unchanged 30 px surface, clipped at the bottom, while the background stays in place: the 4074 bar's bare last row is covered by the Start pill and the background's top highlight row is kept. Struts, height and hit-testing are unchanged and follow the shifted nodes (`lhshell/compare_taskbar_dy.png`).

`taskbar_text = "plain" | "glow"` (2026-10-27; default `plain`, `aero` sets it explicitly, `aerovision` has `glow`): the text on the
taskbar, the task buttons' titles and the minimized sidebar's taskbar clock. `plain` is the 4074 look, white (the clock with its 1,1
black shadow); `glow` is Aero vision's text on glass, as luxnav draws its title and breadcrumb: black text over a white halo, the
glyphs' GDI coverage blurred (two box passes of 4 px) times 2.5. `taskbar_glow = { color = "#ffffff", text = "#000000", size = 4,
strength = 2.5 }` tunes it. `clock_pack = "NAME"`: the pack whose `[clock] pinned` choice this one shares (`aerovision`: `aero`).

Start button keys (2026-10-25): `start_dy = N` (default 0; was 1 in `aero` and `slate` until `taskbar_dy` replaced it) moves the whole Start button N px down in
its slot. The 4074 originals draw the 29 px pill on rows 0..28 of the 30 px bar and leave the bar's last row bare below it
(measured on `4074-dwm.png` and `lh4074-181533.png`: row 29 is the bar's colour); 1 puts the pill's bottom line on the bar's
bottom row. `start_extend_bottom = N` (default 0, unused by the shipped packs) instead makes the button N px taller with the
image's bottom row kept 1:1 (the extra rows repeat inside the pill). `start_alpha = A` (default 1; `aero` 0.75) multiplies the
alpha of the 1179/1180/1181 images (the node's opacity, flag included) so the glass bar shows through the pill; 0.55 washes the
flag out too much. `slate` keeps 1.

The Luna entries are Windows XP's visual style. The `msstyles` named is the copy Longhorn 4051 ships, which is
Longhorn-compiled (PACKTHEM_VERSION 4) and has Luna's real scheme names (`NormalColor`, `HomeStead`, `Metallic`; sizes
`NormalSize`, `LargeFonts`, `ExtraLargeFonts`). For flxp, whose uxtheme reads only version 3, the sections carry
`flxp_msstyles = /u/aap/.wine/drive_c/windows/resources/themes/Luna/luna.msstyles`, a real XP Luna with the same scheme names.
luxwm's frames come from `tools/msstyles2theme.py` (it reads both formats; `docs/LUXWM.md` "XP visual styles (Luna)"). All
three are `composition = off`: `_LHWM_COMPOSITED` stays 0, and clients with `_LHWM_EXTEND_FRAME` get `_LHWM_HEADER_PIXMAP`.

## Fonts (`fonts =`)

The msstyles ask for Segoe UI (Jade/Aero `CAPTIONFONT = Segoe UI, 9`, the shell packs `Segoe UI 9`), Slate for Tahoma, Luna for
Trebuchet MS. None of them needs to be installed: `fonts = DIR[;DIR...]` names directories (or single files) that luxwm and luxshell
register as fontconfig *application* fonts when the theme becomes active (`FcConfigAppFontAddDir` / `AddFile` on the current
configuration, then a fresh pango font map so the next layout resolves against them; luxshell's text is libuxtheme's GDI text
since 2026-10-27 and also hands the directories to `ut_add_font_dir`, which looks there first). The set is replaced on every
switch (`FcConfigAppFontClear` first), so a theme without the directory gets the stock fallback again (libuxtheme only adds
directories: in luxshell one of an earlier theme stays known to it). Paths are absolute or relative to
the registry; `;` separates entries (a `;` after whitespace would start a comment).

Shipped: every entry but aero5048 and aero5112 (`resources/fonts/5112` -> `wim/5112/1/Windows/Fonts`, Segoe UI 0.89: `segoeui.ttf`,
`segoeuib.ttf`, `segoeuii.ttf`, `segoeuiz.ttf`) has `fonts = ../resources/fonts/4074` (a link to `wim/4074/3/WINDOWS/Fonts`). That directory has
Segoe UI version 0.7 (`seguibk.ttf` Regular, `seguibd.ttf` Bold; byte-identical in 4093 and in `resources/fonts/5048/`, which can be listed
instead or as well), Tahoma and Trebuchet MS. 4051's own Fonts directory is not extracted, so slate4051 and the Luna entries use
4074's (Luna's theme frames also load Trebuchet through their theme.ini `font_files`). Where a family is installed on the system as
well (Tahoma in `~/.fonts`), fontconfig may pick either copy.

What it changes:
- Without `fonts =` (the state before 2026-10-24) "Segoe UI 9" fell back to **DejaVu Sans** (the DWM captions, luxshell) and the
  classic converter's `Segoe UI,Tahoma 12px` to **Tahoma**. Leaving the entry out reproduces that.
- The earlier note "a stock 4074 shows Tahoma" (luxwm README, classic msstyles mode) does not hold: 4074 ships Segoe UI 0.7, and the
  Jade Task Manager caption in `screenshots/lh4074-181714.png` fits it better than Tahoma (text box rmse 21.4 vs 24.8 levels, ink
  width 123 px = Segoe UI 0.7 vs 124 Tahoma, ink pixel count 267 vs 264 Segoe UI / 307 Tahoma). Segoe UI 0.7's advance widths are
  close to Tahoma's, which is why the two were mistaken. `lhwm/compare_fonts.png`: reference / Tahoma / Segoe UI 0.7, and the aero
  caption and luxshell taskbar before (DejaVu Sans) and after.
- Check inside the processes: `LHWM_DEBUG=1` makes luxwm log `caption font "Segoe UI 9" -> "Segoe UI 9" (.../seguibk.ttf)` on every
  theme load; `LHSHELL_DEBUG=1` (or any change of the set) makes luxshell log `fonts DIR: "Segoe UI 9" -> Segoe UI (.../seguibk.ttf)`.
  `fc-match` in a shell does not see application fonts.
- flxp: the same directories would give its uxtheme the style's own faces; it reads the key if it wants to (unknown keys are ignored).

## The two root properties

| Property | Type | Meaning |
| --- | --- | --- |
| `_LH_THEME` | UTF8_STRING | The session's theme: a section name of the registry, or the name of a user preset (`$XDG_CONFIG_HOME/longhorn/themes/NAME.theme`, SETTINGS.md). This is the setting. Anybody may write it; everybody follows it, resolving a name that is not a section as a preset (registry first, then the preset file: its `[Theme] base` entry with the preset's overrides; SETTINGS.md "Preset names on `_LH_THEME`"). An unknown name is ignored (the current theme stays). |
| `_LH_THEMES_INI` | UTF8_STRING | Absolute path of the registry. Read it from here, do not hardcode the path. |
| `_LH_THEME_RESOLVED` | UTF8_STRING | (2026-10-25, written by lux) The theme in effect after a user preset's overrides and settings.ini: `name=...;base=...;title=...;msstyles=...;color=...;size=...;frame=...;frame_classic=...;shell=...;fonts=...;wallpaper=...;icons=...;composition=...;animations=...;mode=...;startpanel=...`, `;` and `\` in values escaped with `\`. With a preset selected both `_LH_THEME` and this property's `name` hold the preset's name (since 2026-10-25; before, lux wrote the `base` to `_LH_THEME`). Use it only when its `name` equals `_LH_THEME` (it is written after `_LH_THEME` when somebody other than lux selected the preset); luxwm does, flxp reads the preset file. Format and rules: SETTINGS.md "Resolved theme". |

Who writes them:
- Whoever starts first publishes. lux (formerly luxshell) owns the persisted value (`~/.config/longhorn/settings.ini`, `[theme] name`,
  SETTINGS.md; before 2026-10-25 `~/.config/lhshell/state.ini [shell] theme`): if
  `_LH_THEME` is missing when it starts, it publishes the persisted name. luxwm publishes its `-t` / `LHWM_THEME` only if
  `_LH_THEME` is missing when it starts. Both publish `_LH_THEMES_INI` if it is missing and they found the file themselves
  (luxwm: `LH_THEMES_INI`, else `../themes.ini` next to its binary; luxshell: `LH_THEMES_INI`, else `../themes.ini` from its
  working directory `x11/lhshell`). An `LH_THEMES_INI` in the environment is used but not published.
- Switching: luxshell's settings (from the start menu, M6 step 2), `session/lab.sh theme NAME`, `lhde-session theme NAME`,
  or by hand: `xprop -root -f _LH_THEME 8u -set _LH_THEME slate4074`.
- luxwm and luxshell replace an alias with the registry name (`jade` becomes `jade4074`), so a client normally sees a
  section name or a preset name. Resolve aliases anyway if you can: the replacement is a second PropertyNotify. A preset name
  is never replaced (nobody writes its base back).
- Nobody deletes them. luxwm leaves both on the root when it exits, so a restarted luxwm and a later client find the same theme.

## What luxwm publishes on a switch

| Property | Type | Meaning |
| --- | --- | --- |
| `_LHWM_THEME` | STRING | The frame theme directory in use (derived from `frame` / `frame_classic`). Read-only for clients; writing it still switches luxwm's frames, for debugging, without touching `_LH_THEME`. |
| `_LHWM_COMPOSITED` | CARDINAL 0/1 | 1 = glass frames: a compositor runs, the theme has `composition = auto`, and its `frame` is a DWM theme. The `DwmIsCompositionEnabled` of the lab. Missing = 0 (luxwm not running). |
| `_LHWM_FRAME_METRICS` | CARDINAL[8] | `caption_h border_l border_r border_b text_rgb glow_rgb inactive_text_rgb 0` of the frame now in use (`docs/CLIENT-PROTOCOL.md`). |

On every change of `_LH_THEME`, luxwm rewrites `_LHWM_COMPOSITED` and `_LHWM_FRAME_METRICS` even when their values
stay the same (aero-patch to jade4074 changes neither), so a client watching only those two still gets one PropertyNotify of
each per switch. The order is `_LH_THEME` (from whoever set it), `_LHWM_THEME`, `_LHWM_FRAME_METRICS`, `_LHWM_COMPOSITED`.
`_LHWM_COMPOSITED` also changes on its own when a compositor starts or stops (`_NET_WM_CM_S<screen>`).

## What a client does on a change (the `WM_THEMECHANGED` handler)

1. `XSelectInput(dpy, root, PropertyChangeMask)` before reading anything, so no change slips between the read and the
   selection.
2. Treat a PropertyNotify on the root for `_LH_THEME`, `_LHWM_COMPOSITED` or `_LHWM_FRAME_METRICS` as `WM_THEMECHANGED`.
   Several arrive per switch. Coalesce them: handle the change once the event queue is empty (`XPending` = 0) or on a 0 ms
   timer. They arrive in one burst.
3. If `_LH_THEME` changed: read `_LH_THEMES_INI`, look the name up, and reload the visual style from `msstyles` + `color` +
   `size`. For flxp this is the `FLXP_THEME`, `FLXP_THEME_COLOR`, `FLXP_THEME_SIZE` triple applied at runtime, then
   `WM_THEMECHANGED` to every window. If the name is not in the registry, keep the current style.
4. Re-read `_LHWM_COMPOSITED` and repaint the extended header transparent (1: glass) or opaque (0). Since 2026-10-24 keep
   `_LHWM_EXTEND_FRAME` / `_LHWM_FRAME_FLAGS` set in both cases: while it is 0, luxwm draws a normal frame (honouring `NO_TITLE`
   / `NO_ICON`) and gives the client the theme's caption band as `_LHWM_HEADER_PIXMAP` to put under its opaque header
   (`docs/CLIENT-PROTOCOL.md` "Additions 2026-10-24"). Deleting them while it is 0 (the earlier rule) still works: a normal frame
   with title and icon, and no pixmap. This is `DwmExtendFrameIntoClientArea` called again after `WM_DWMCOMPOSITIONCHANGED`.
5. Re-read `_LHWM_FRAME_METRICS` if the header layout follows the caption height or colours.

`examples/extendframe.c` does steps 1 to 4 and is the reference. It shows the theme name and the msstyles it would load
in its window. It does not coalesce: it repaints on each of the events, which is fine for a demo. Its rule 6 and `docs/CLIENT-PROTOCOL.md` "Theme changes" say the same in protocol terms.

## The composition flag, in short

- `composition = auto` themes (aero-patch, jade4074, aero5048, aero5112): glass frames while a compositor runs, `frame_classic` otherwise
  (for aero5048/aero5112 their own msstyles frame: `frame_classic = style`).
  `_LHWM_COMPOSITED` follows both the theme and the compositor.
- `composition = off` themes (jade4074-classic, slate4074, slate4051, luna, luna-olive, luna-silver, luna-xp, royale, zune, noir, embedded): always the classic msstyles frame, and
  `_LHWM_COMPOSITED` is 0 whether or not picom runs. A client then draws everything opaque, as on 4074 without DWM.
- Do not decide glass from the theme name. Decide it from `_LHWM_COMPOSITED`, which is the only thing that says whether the
  frame under your window is glass right now.

## Things flxp needs beyond the above

- `color = Default` / `size = Normal` are the names in the shipped 4074/4051 styles. A uxtheme that only knows Luna's
  `NormalColor` should fall back to the style's first colour scheme when the name is not found.
- The aero-patch entry's `msstyles` is the patched Aero PE file (`resources/msstyles/aero-patch.msstyles` -> `/u/aap/fun/longhorn/aero.msstyles`).
  `res/aero/aero.msstyles` is a directory of its extracted resources, not something uxtheme can load.
- Without composition, aero-patch falls back to the Jade classic frame (`frame_classic = data/frames/jade4074-classic`) while
  clients keep the Aero msstyles. That is what Longhorn did too, but frame and client controls then differ in colour. aero5048 and
  aero5112 (since 2026-10-27, `frame_classic = style`) draw their own style's Window class instead, so frame and controls match;
  `frame_classic = style` on aero-patch would do the same with the patch style's Window class.
- The wallpaper is luxshell's job only while luxshell draws the desktop. When flxp-desktop owns the desktop (second-display
  session), it paints its own. Since 2026-10-25 the registry has no `wallpaper` keys (`wallpapers.ini`, "Wallpapers" above), so
  flxp-desktop's lookup of the section's `wallpaper` finds nothing; lux bridges that by also writing the effective wallpaper to
  flxp-desktop's `_FLXP_WALLPAPER` (SETTINGS.md "Resolved theme"). The lasting fix on the flxp side: take `wallpaper=` / `mode=`
  from `_LH_THEME_RESOLVED` (registry themes too, not only presets), or read `wallpapers.ini` with the order above.
- Writing `_LH_THEME` from flxp (a theme picker) is allowed. Write the registry name; luxwm and lux follow, and lux
  persists it. A user preset's name (`~/.config/longhorn/themes/NAME.theme`, SETTINGS.md) may be written too and stays on the
  root as written: luxwm and flxp resolve it themselves, lux applies it and publishes `_LH_THEME_RESOLVED`. A picker should list
  the presets next to the registry entries (flxp-settings, flxp's start panel and lux's settings flyout do).

## flxp's own keys (`flxp_*`)

flxp follows `_LH_THEME` (explorer, flxp-desktop, the file dialog; `theme_follow = 0` in `flxp.ini` or `--theme` opts out). Its
uxtheme reads only XP-format styles (`PACKTHEM_VERSION` 3), not Longhorn's compiled ones (version 4: all `msstyles` above), so for
those it loads the XP backport of the same name (`../themes/xp-backport/<name>/<name>.msstyles` next to this file) or the
section's `flxp_msstyles` (set on every Longhorn-format entry since 2026-10-25, "Resources" above). Any other `flxp.ini` key can be given per theme as `flxp_<key>`
(`flxp_icons`, `flxp_longhorn_system32`, `flxp_explorer_resources` = the Avalon `Microsoft.Windows.Client.dll` with the Back/Forward
framelets, `flxp_menubar_style`, `flxp_menubar_msstyles`, ...); a value set in `flxp.ini` wins. flxp-desktop shows the section's
`wallpaper` (fill) unless `flxp.ini` names one; no section has one any more ("Wallpapers"), lux's `_FLXP_WALLPAPER` stands in.
`flxp_frame = xp | longhorn` picks the explorer's look for the theme (flxp's `frame = auto`): `xp` for the Luna entries (the XP
toolbar and Address bar under luxwm's Luna frame), `longhorn` for the Longhorn ones (the 4074 header: glass, or without composition
the theme's navigation bar); followed live on a switch. `flxp_navbar_msstyles` names the Longhorn style whose navigation-bar
bitmaps flxp uses without composition (default: the section's `msstyles`).

## One uxtheme for everything (design, 2026-10-24)

Today three readers exist: luxwm's offline converter (`tools/msstyles2theme.py`, XP and
Longhorn-compiled formats → theme dirs), luxshell's `shellpack.sh` (via `tools/lhstyle.py`, Longhorn
format → PNG packs), and flxp's Wine-derived uxtheme (XP format only, live). aap wants one place
that reads every relevant `.msstyles` so all three use the same style:

- **Formats to cover:** XP `PACKTHEM_VERSION 3` (text ini resources: luna.msstyles from real XP);
  Longhorn-compiled v4 (4042–4074: binary `VARIANT` property records, `PNGSTREAM`/`D3DXSTREAM`/
  `BAMLSTREAM`/`IMAGEDATA`, the Sidebar/CompositedWindow classes; this is also what the 4051
  copy of Luna is); 5048 `aero.msstyles` (v4 variant, no CompositedWindow). Vista+ out of scope.
- **Where:** the Wine uxtheme port in flxp is the most complete and Windows-faithful renderer
  (nine-grid, margins, true-size, font/colour properties, `DrawThemeBackground`), so it becomes
  the shared library: split out of flxp into its own repo (`~/src/uxtheme` or inside flxp but
  installed as `libuxtheme.so` + headers), with a plain C API that does not need the win32 shim:
  `ut_open(path, color, size)`, `ut_part_size`, `ut_draw_background(theme, class, part, state,
  cairo_t*/ARGB buffer, rect)`, `ut_get_{int,color,margins,font,position,enum,bool}`, and raw
  access `ut_get_stream(class, part, prop)` for PNGSTREAM/D3DXSTREAM/BAMLSTREAM/IMAGEDATA so
  luxwm can fetch the composited plates and luxshell the Sidebar blobs. It also exposes
  `ut_list_classes/parts/states` for tooling.
- **Who does what:** the flxp side owns the library and its XP reader; the Longhorn v4 reader is
  specified by us (`docs/MSSTYLES-FORMAT.md`, written from `tools/lhstyle.py` and the converter)
  and implemented in the library; `msstyles2theme.py` and `shellpack.sh` then become thin
  wrappers around the library's CLI (or are kept only for offline packs), and `themes.ini` keeps
  pointing at the `.msstyles` files, which are then loaded live by luxwm and luxshell on
  `_LH_THEME` changes.
- **Transition:** luxwm's `frame.c` msstyles mode already mirrors uxtheme's rules (CLASSIC-DECO-
  NOTES.md); when the library exists, frame.c calls `ut_draw_background` for the caption/border
  parts and keeps only the DWM plate code. luxshell replaces its asset packs with live part
  lookups. Byte-identical regression (`luxwm/tests/regress.sh`) guards the switch.

### Status 2026-10-24 evening: libuxtheme exists
The flxp side built it: `~/src/flxp/src/uxtheme/` (`ut.h` C API, `ut.cpp` over Wine's msstyles/draw/
property, `v4.cpp` the Longhorn-compiled reader written from MSSTYLES-FORMAT.md, `nowin.cpp`,
`ut-dump`), installed as `~/.local/lib/libuxtheme.so.1` + `~/.local/include/uxtheme/ut.h`
(`make uxtheme` / `make install` in flxp, PREFIX default /usr/local). It reads v3 and v4 styles,
draws parts into premultiplied ARGB (`ut_draw_background`, header-only cairo helper
`ut_draw_background_cairo`), draws text, returns raw streams (`ut_get_stream`: PNGSTREAM/
D3DXSTREAM/BAMLSTREAM/IMAGEDATA) and bitmaps, and takes font dirs (`ut_add_font_dir`).
Adoption plan on our side (next tasks, regression-guarded by `luxwm/tests/regress.sh`):
1. luxwm msstyles mode: `frame.c` draws caption/borders/buttons via `ut_draw_background` from the
   `.msstyles` named in `themes.ini` (`msstyles`/`color`/`size`), text via its own pango path
   (keep the GDI-shadow emulation), DWM plates via `ut_get_stream` on the CompositedWindow parts
   → the generated `themes/*-classic` dirs become optional caches; theme switching loads the style
   live.
2. luxshell: the Sidebar class imagery via `ut_get_stream`/`ut_get_bitmap` instead of
   `assets/sidebar-*` packs; imageres stays a PE resource read (not a style).
3. `msstyles2theme.py` / `shellpack.sh`: keep as offline tools, validated against the library
   (`ut-dump`).

**Step 1 done (luxwm, 2026-10-24 late; `docs/LUXWM.md` "libuxtheme source").** luxwm links libuxtheme when
`(gone: the top-level build links libwuxtheme)` finds it (`configure --without-uxtheme` leaves it out) and loads the style live: a registry entry's
`msstyles`/`color`/`size` go with its `frame` directory as a theme spec (`DIR|STYLE|COLOR|SIZE`), a `frame_classic` directory names its
own style in its theme.ini (`msstyles =`, written by the converter). Classic frames: CAPTION/MAXCAPTION/FRAMELEFT/RIGHT/BOTTOM drawn with
`ut_draw_background`, the caption buttons from `ut_get_bitmap` through frame.c's XP glyph rule (the library centres IMAGEGLYPH glyphs in
the whole rect, XP in the content rect), metrics/font/colours from [SysMetrics]; text, shadows, geometry stay in frame.c. DWM themes:
every plate, button, overlay, fog and default-icon texture from the CompositedWindow PNGSTREAMs; theme.ini keeps the insets and tuning
keys. The `themes/*-classic` and `themes/luna-*` directories are now caches (no-library fallback, tuning keys); `frame` is optional for
an entry with `msstyles`. New `[luna-xp]`: XP's own v3 luna.msstyles from `~/.wine`, no directory, frames straight from the style
(`lhwm/compare_luna_xp.png`: borders and caption bitmap 0 pixels off the XP SP3 screenshot, as the 4051 copy). Regression: dir vs lib
128/128 byte-identical (`luxwm/tests/regress.sh --compare-sources`, `lhwm/compare_lib_vs_dir.png`); the reference changed only for Luna's
6 maximised renders, a converter bug the library exposed (luna.msstyles gives MAXCAPTION `SizingType` twice, XP/Wine/the library take
the first, STRETCH; the converter took the last, TILE). Library gaps for the flxp side are listed in the README section.

## Per-theme shell settings (added 2026-10-25, flxp)
`taskbar` (avalon | classic | none), `start` (lux | flxp: the start menu provider), `animations` (4074plus | 4074 | invented |
5048 | none; no entry sets it yet) and flxp's `flxp_icons` / `flxp_frame` are **per-theme defaults**: what the theme brings
unless the user overrides it for that theme. The Luna entries say `taskbar = classic`, `start = flxp`, `flxp_icons = xp`
(with `flxp_frame = xp`: the wux taskbar and the XP explorer); the Longhorn entries `taskbar = avalon`, `start = lux` (the lux
combo). `composition` keeps its frame-mode meaning (auto | off); the compositor switch has no registry default ("follow").
The effective value for a theme is: settings.ini `[override:NAME]` / the preset's own key > this registry key > the global
settings.ini key (`[taskbar] kind`, `[start] menu`, `[theme] animations`, `[desktop] icons`, `[explorer] frame`) > the
built-in default (SETTINGS.md "Per-theme settings"). lux and luxwm may read `taskbar`, `start` and `animations` here.

**flxp and the v4 styles (2026-10-26, flxp).** flxp's uxtheme reads Longhorn's compiled (version 4) msstyles too now, so a
section without `flxp_msstyles` loads its own `msstyles` (version 3 or 4); the XP backport by file name is only a fallback for
a file it cannot read, and `flxp_msstyles` stays the explicit override (the 4074/4051 entries keep their backports). Changed
here (flxp keys only): `[aero5048]` no longer has `flxp_msstyles` (it pointed at Jade's backport; flxp now shows the 5048
assets), `[aero5048]` and `[aero5112]` have `flxp_frame = xp` (the XP explorer for these builds for now), `flxp_icons =
longhorn`, `flxp_longhorn_system32 = ../resources/system32/4074`. flxp also registers a section's `fonts` directories
(5112's Segoe UI). Both entries say `taskbar = avalon`: with lux running flxp-taskbar is then menu-only; set `taskbar =
classic` (not ours to change) if flxp's rebar-style bar with 5048/5112's TaskBar parts is wanted.

## Request for the wux side: text measuring and fonts in libuxtheme (2026-10-27)
Decision (aap, 2026-10-27): all text on the desktop converges on libuxtheme's GDI-faithful text
path (wux's FreeType/GDI code, already behind `ut_draw_text`). luxnav moves first (no pango/cairo
in luxnav, so flxp can link it directly), lux's text and luxwm's captions follow later. What
libuxtheme needs for that:
1. `int ut_text_extent(ut_theme *t, const char *cls, int part, int state, const char *utf8,
   unsigned dt_flags, const ut_rect *bounds, ut_rect *extent)` — GetThemeTextExtent (advance-based
   rectangle, honouring DT_SINGLELINE/DT_END_ELLIPSIS/DT_WORDBREAK), so clients can fit text.
2. A font override for drawing/measuring without a theme part: e.g. `ut_draw_text_font(...,
   const ut_font *f, uint32_t rgb, ...)` and `ut_text_extent_font(...)`, since luxnav/lux use
   fonts from their own config (nav.ini `crumb_font`) as well as theme fonts.
3. Optional, later: `ut_draw_text` with an alpha-only (mask) output so callers can build the
   white glow behind text on glass from the same glyphs.
Until (1) exists, luxnav measures by rendering into a scratch buffer and taking the advance from
the ink box plus the font's space width; the switch is a one-function change.
Answer (wux, 2026-10-27): all three exist now in `src/uxtheme/ut.h` (flxp commit "libuxtheme: text extent..."):
`ut_text_extent`, `ut_draw_text_font` / `ut_text_extent_font` (a `ut_font`, colour `0xRRGGBB`), `ut_get_text_metrics`
(a font or a part's font), and `ut_draw_text_mask` (8-bit coverage, max'ed into the caller's mask, same glyphs as the draw
calls). Details and measured examples: flxp NOTES.md "libuxtheme: text measuring, font overrides, text masks".
