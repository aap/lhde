# Theme files (step 4 of the merge)

Status: **draft for review**, 2026-10-28, written by the wux session from what both sessions agreed
in `~/fun/longhorn/THEME-FORMAT-wux.md` (proposal, the lux session's comments, the reply) and aap's
`~/fun/longhorn/themes.txt` (the theme list). Replaces `data/themes.ini`, `data/wallpapers.ini`,
`settings.ini`'s `[override:NAME]`/`[override]` sections, the presets in `~/.config/longhorn/themes/`
and the `flxp_*` keys. Open points are marked **OPEN**.

## 1. Two layers
**Components**, each chosen independently and referenced by **name**:

| kind | where (user data dir first, then `$PREFIX/share/lhde`, then the build tree's `data/`) | what it is |
| --- | --- | --- |
| style | `styles/NAME.msstyles` (or `styles/NAME/*.msstyles`), shellstyle.dll beside it | the visual style: controls, classic decorations, menus, start panel |
| scheme | `styles/NAME.scheme` | a classic Appearance scheme (Windows Standard, Brick, ...) for `style = classic` |
| frames | `frames/NAME/` (`theme.ini` + pixels or a cache) | luxwm's frame set: glass, or a non-composited reskin (vision buttons) |
| pack | `packs/NAME/` (`shell.lua` + pixels) | luxshell's Avalon bars, per build and family: `4051-slate`, `4074-slate`, `4074-aero`, `5112-aero` |
| navbar | `navbar/NAME.ini` | navigation-bar layout family: `4051`, `4074`, `vision` (later `5048`, `5112`) |
| icons | `icons/NAME.theme` | an icon set recipe (keys -> `resources/<build>/shell32.dll#id`) |
| wallpaper | `wallpapers/[GROUP/]NAME.ext` | one `/` allowed: the build group (`4074/Bliss`) |
| fonts | `fonts/NAME/` | per build (`4074`, `5112`), then fontconfig |
| resources | `resources/BUILD/` | the build's DLLs (shell32, imageres, explorer, browseui, Avalon) |

Names: `[a-z0-9._-]+` (wallpapers: plus one `/`). Importers name by build (`aero5048`, `aero5112`,
`aero-patch`, `jade4074`, `slate4051`); a hand-dropped file keeps its stem unless that collides.
A path (absolute or `~/`) is accepted in the user's own theme files only.

**Theme files** tie them together, like Windows `.theme` files: shipped in
`$PREFIX/share/lhde/themes/NAME.theme` (source: `data/themes/`), the user's in
`~/.config/lhde/themes/NAME.theme`. A bare style in `styles/` is also a theme (section 5).

## 2. The file
```ini
[Theme]
name     = Jade (4074)          ; display title
base     = jade4074             ; optional: inherit every key not set here
build    = 4074                 ; optional: the build whose resources/fonts are the default (else from the style)

[VisualStyle]
style    = jade4074             ; styles/; "classic" = no visual style
color    = Default              ; scheme name inside the file; absent = the style's first
size     = Normal               ; absent = the style's first
scheme   = brick                ; only with style = classic: styles/brick.scheme
shellstyle = auto               ; auto = the one beside the style for this colour

[Glass]
frames         = jade4074       ; composited frame set; none = no glass (classic decorations)
frames_classic = none           ; non-composited frame set; none = the style's own decorations (default)
composition    = auto           ; auto | on | off
animations     = 4074plus       ; 4074plus | 4074 | 5048 | invented | none

[Shell]
taskbar  = avalon               ; avalon (luxshell) | classic (flxp-taskbar) | none
start    = flxp                 ; flxp | lux: who answers the Start button and Super
pack     = 4074-aero            ; packs/, for taskbar = avalon
sidebar  = full                 ; full | minimized | pinned | none

[Explorer]
chrome   = longhorn             ; auto | xp | longhorn: the Explorer's own look
navbar   = 4074                 ; 4051 | 4074 | vision | none (later 5048, 5112): a layout family
navbar_preset = medium          ; family-specific size: 4074 small|medium|large|xlarge; vision small|big
taskband = 0                    ; the Longhorn task band

[Desktop]
wallpaper = 4074/Bliss          ; wallpapers/; none = colour only
mode      = fill                ; fill | fit | center | tile | stretch
color     = 58 110 165          ; background colour
icons     = longhorn4074        ; icons/

[Fonts]
fonts     = 4074                ; fonts/, then fontconfig
smoothing = standard            ; standard | cleartype | off

[Cursors]                       ; reserved
[Sounds]                        ; reserved
```
Not in a theme (per user, `settings.ini` / `flxp.ini`): the current theme name, sidebar stacking,
width, autohide, locked, rows, clock layout; the default folder view and per-folder views, Folder
Options, window sizes, terminal, file associations, bookmarks.

Who draws a navbar family is the resolver's business, not the theme's: today `4051`/`4074` are flxp's
own header and `vision` is luxnav; when luxnav reproduces 4051/4074 pixel for pixel (flxp's header is
the reference), the resolver switches and no theme file changes. `composition = on` with `frames = none`:
the compositor runs (shadows, animations), windows keep classic frames; nobody should expect glass.

## 3. Values and inheritance
- **absent** = inherit from `base`, else the style-derived default (section 5); **`none`** = switched off.
- `base` chains, at most 8 deep, a cycle is an error.
- Editing in Display Properties or luxshell's flyout never writes a shipped file: the first change writes
  `~/.config/lhde/themes/NAME-modified.theme` with `base = NAME`, only the changed keys and
  `name = TITLE (modified)`, and selects it; "Save As" renames it. Every theme key changed anywhere goes
  there, so the dialog and the flyout agree.

## 4. The resolver (`lhtheme/`, the lux side writes it, everybody links it)
`lhtheme_resolve(name)` returns one flat record `section.key = value` with inheritance, defaults and
fallbacks applied, and for every key where it came from (file, base, default, fallback + reason).
luxwm and flxp link it, luxshell through a Lua binding. The root property `_LH_THEME_RESOLVED` carries
that record as `section.key=value` lines plus `theme=NAME` and `serial=` (the `_LH_SETTINGS_SERIAL`
it was resolved at); a client resolves itself when the property is missing or stale.

## 5. Defaults from the style (a bare style, or absent keys)
| the style (wuxtheme: `ut_format`, classes, version info) | chrome | navbar | taskbar | pack | frames | icons |
| --- | --- | --- | --- | --- | --- | --- |
| v3 (XP format) | xp | none | classic | - | none | xp |
| v4 with CompositedWindow, 4051 | longhorn | 4051 | avalon if `<build>-<family>` exists, else classic | `4051-slate` | none | longhorn4074 |
| v4 with CompositedWindow, 4074 | longhorn | 4074 | as above | `4074-slate` or `4074-aero` by family (slate -> slate, jade/aero -> aero) | the frame set named like the style, else none | longhorn4074 |
| v4 with DWMWindow/uDWM (5048, 5112) | longhorn | vision (aap: Vista falls back to luxnav with the patched aero resources) | classic | - | the frame set named like the style | longhorn4074 |
| classic | xp | none | classic | - | none | xp |
`start` defaults to `flxp`, `composition` to `auto`, `animations` to the build's own, fonts and resources
to the build.

## 6. Fallbacks (never fail, look plainer, report it)
frames missing or no compositor -> `frames_classic`, else the style's decorations; pack missing ->
taskbar classic; navbar `vision` without luxnav -> `4074`; a navbar family whose art the style lacks ->
drawn buttons; icons missing -> `xp` if `resources/xp` exists, else the built-in Wine set; resources
missing -> drawn art. Each substitution is in the resolved record's "came from".

## 7. Runtime
`_LH_THEME` = the theme name. Whoever changes it writes `settings.ini [theme] name` first, then the
property, then `_LH_SETTINGS_CHANGED`. `_FLXP_WALLPAPER` is retired: picking a picture is an Apply into the
modified copy, and flxp-desktop reads the resolved record like everyone else.

## 8. The shipped themes (aap's themes.txt)
| theme | style | glass / classic frames | composition | pack | navbar | wallpaper |
| --- | --- | --- | --- | --- | --- | --- |
| `slate4051` | slate4051 | - / slate4051 | off | 4051-slate | 4051 | 4051 field |
| `slate4074` | slate4074 | - / slate4074 | off | 4074-slate | 4074 (4 sizes) | 4074/Bliss (leaves) |
| `jade4074` | jade4074 | - / jade4074 | off | 4074-aero | 4074 | 4074/Bliss |
| `aero4074` | aero-patch | aero-patch / jade4074 | auto | 4074-aero | 4074 | Aero Enmeshed |
| `aerovision` | aero-patch | aerovision / jade4074 | auto | 4074-aerovision (jade for now) | vision (small, big) | chadDesktop demo |
| `slatevision` (TODO) | slate4074 | - / slatevision (Aero caption buttons) | off | 4074-slate | vision | cherry blossom |
| `metalvision` (?) | jade4074 | - / metalvision | off | 4074-aero | vision | ? |
| `basic5048` / `aero5048` | aero5048 | aero5048 / jade4074 | off / on (`aero5048: base = basic5048`) | - (classic bar) | vision | 5048's own |
| `basic5112` / `aero5112` | aero5112 | aero5112 / jade4074 | off / on | - | vision | 5112's own |
| `luna`, `luna-olive`, `luna-silver` | luna-xp | - / luna4051-* | off | - | none (XP chrome) | Bliss (XP) |
| any dropped XP style | itself | - | off | - | none | - |
Later: plex (various versions), plex vision.

## 9. Open points
1. **OPEN: the `-xp` backports.** Today flxp loads Jade's/Slate's XP backports instead of the real v4
   styles (`flxp_msstyles`), because the backports carry hand-tuned values (rebar text colours, edges).
   wuxtheme renders the v4 files themselves (v4 Slate parts are pixel-identical to the backport's), so the
   proposal is: drop the per-consumer split, every component uses the theme's one style, and the few
   tuned differences become fixes in wuxtheme or are accepted. Needs a side-by-side check of the explorer
   under v4 jade4074/slate4074 first (wux side).
2. **OPEN: the Luna themes** name `luna4051.msstyles` (the 4051 copy, v4) for luxwm and `luna-xp` for flxp.
   With the XP ISO import, the real XP Luna becomes `styles/luna` for everyone (one style, see 1).
3. **OPEN: the library's name.** `lhtheme` today. `luxtheme` would read as a sibling renderer of `wuxtheme`
   (it is a resolver, not a uxtheme). Proposal: one small desktop-wide library `liblhde` (`lhde/` or `lhtheme/`
   renamed) holding what every component shares: the data dirs (`lhde_data`), this resolver and the
   settings store, with the `lhde_` prefix that `lhde_data`, `lhde-import` and `lhde-session` already use.
4. The `navbar_preset` value list per family is luxnav's/flxp's to keep in sync; the resolver only passes it on.
