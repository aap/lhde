# Theme files

The theme model of lhde: components chosen independently and referenced by name, tied together by Windows-style `.theme` files,
resolved by liblhde for every program. Sections 1 to 8 are the format as it is implemented (liblhde/theme.c, `lhde-theme`).
How the format was arrived at (the open points, aap's decisions, the work split between the sessions and their status reports
of 2026-10-28, formerly sections 9 to 15 of this file) is in docs/history/THEME-FORMAT-status.md.

Decisions in force (from that history):
- The `-xp` backports are gone: every component renders the theme's one style natively through wuxtheme.
- Luna may come from XP (`styles/luna`, from the Server 2003 ISO) or from a Longhorn build (`luna4051`, `luna4074`); the shipped
  `luna*` themes name `luna` and fall back to `luna4051`.
- XP-family resources (`resources/xp`, `resources/2k`) come from the Server 2003 and Windows 2000 ISOs; flxp reads XP and 2000
  art by resource id from them, never from a source tree.
- Themes and Appearance are apart, as in Windows: a theme file is never written by a settings change. Every change goes into the
  current layer (`settings.ini [current]`, shown as "NAME (Modified)", section 3); Save As writes a theme file; selecting
  another theme drops the layer.
- Frame set names are kept as the importer and the frame regression know them (`jade4074-classic`, `slate4074-classic`,
  `luna4051-*`, `aero-patch`, ...).

## 1. Two layers
**Components**, each chosen independently and referenced by **name**:

| kind | where (relative names, looked up in `~/.config/lhde`, the Windows tree's `index/`, `$PREFIX/share/lhde`, the build tree's `data/`: docs/LAYOUT.md "The three trees") | what it is |
| --- | --- | --- |
| style | `styles/NAME.msstyles` (or `styles/NAME/*.msstyles`), shellstyle.dll beside it | the visual style: controls, classic decorations, menus, start panel |
| scheme | `styles/NAME.scheme` | a classic Appearance scheme (Windows Standard, Brick, ...) for `style = classic` |
| frames | `frames/NAME/` (`theme.ini` shipped + pixels in `lhde-windows/generated/frames/NAME`) | luxwm's frame set: glass, or a non-composited reskin (vision buttons) |
| pack | `packs/NAME/` (`shell.lua` shipped; pixels in `lhde-windows/generated/packs/<build>`) | luxshell's Avalon bars, per build and family: `4051-slate`, `4074-slate`, `4074-aero`, `5112-aero` |
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
`~/.config/lhde/themes/NAME.theme`. A bare style in `styles/` is also a theme (section 5) for the resolver and `lhde-theme`;
Display Properties lists theme files only and offers the styles on its Appearance tab ("Windows and buttons"), as Windows
keeps Themes and the visual style apart (2026-10-28).

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

## 2a. The classic look: `[Colors]`, `[Metrics]`, schemes (2026-10-28)
With `style = classic` (no visual style) the look is Windows' classic one: system colours and non-client metrics. A theme
file (or the current layer, section 3) may set them directly; `scheme = NAME` names a classic Appearance scheme,
`styles/NAME.scheme`, which gives every key the files leave out. Without a scheme the built-in classic colours apply (wux's
table = XP's "Windows Standard"), so "Windows Classic style" works without any import.
```ini
[Colors]                        ; the COLOR_ constant's name without COLOR_ (case-insensitive), "R G B"
activecaption = 10 36 106       ; scrollbar background activecaption inactivecaption menu window windowframe menutext windowtext
                                ; captiontext activeborder inactiveborder appworkspace highlight highlighttext btnface btnshadow
                                ; graytext btntext inactivecaptiontext btnhighlight 3ddkshadow 3dlight infotext infobk
                                ; alternatebtnface hotlight gradientactivecaption gradientinactivecaption menuhilight menubar
[Metrics]                       ; NONCLIENTMETRICS in pixels, fonts as "Face, size[, bold][, italic]" (size: points; negative:
                                ; pixels, LOGFONT's lfHeight, as the importer writes them)
borderwidth = 1                 ; scrollwidth scrollheight captionwidth captionheight smcaptionwidth smcaptionheight
captionfont = Tahoma, 8, bold   ; menuwidth menuheight; captionfont smcaptionfont menufont statusfont messagefont icontitlefont
```
A scheme file `styles/NAME.scheme` has the same `[Colors]` and `[Metrics]`, a `[Scheme] name = Title` (and `sizes = Normal, Large
Fonts, Extra Large Fonts`), and per font size `[Metrics:SIZE]` (and `[Colors:SIZE]` when the colours differ): `VisualStyle.size`
picks one (XP's "Font size": Normal, Large Fonts, Extra Large Fonts). Schemes are the user's own data: `tools/lhde-schemes`
imports XP's and Windows 2000's from the install media's default hive (`HIVEDEF.INF`, `Control Panel\Appearance\Schemes`),
run by lhde-import for the `xp` and `2k` sources. Resolution: theme files, then the current layer, then the scheme's keys for
what is still absent (source `scheme`); a scheme named but not imported falls back to `none` (the built-in colours).
Only the classic look reads them: a visual style brings its own `[SysMetrics]`. With the classic look `Colors.background` is
the desktop colour (COLOR_BACKGROUND) under the wallpaper; `Desktop.color` when the scheme and the layer have none.

## 3. Values and inheritance
- **absent** = inherit from `base`, else the style-derived default (section 5); **`none`** = switched off.
- `base` chains, at most 8 deep, a cycle is an error.
- **The current layer** (2026-10-28; Windows' "NAME (Modified)"): editing in Display Properties never writes a theme file.
  `settings.ini [current]` holds the changed theme keys as `Section.key = value` plus `theme = NAME`, the theme they belong to;
  liblhde's resolver applies them on top of that theme's files (source `current`; the derived key `current` counts them), so
  `lhde_theme_resolve(NAME)` of the session theme, the published `_LH_THEME_RESOLVED`, luxwm, luxshell and flxp all see the
  same look. A key whose value the theme gives anyway is not kept. Selecting another theme clears the layer
  (`lhde_theme_select`, `lhde-theme set`); "Save As" writes `~/.config/lhde/themes/FILE.theme` (`base = NAME` and the layer's
  keys, or a copy of the user's own theme file plus the keys; `name =` the title given), selects it and clears the layer;
  Delete removes a user theme file (the selected one falls back to its base). API: `lhde_current_set_key`, `lhde_current_set`,
  `lhde_current_keys`, `lhde_current_clear`, `lhde_current_theme`, `lhde_theme_select`, `lhde_theme_save_as`,
  `lhde_theme_delete`, `lhde_theme_resolve_flags(NAME, LHDE_RESOLVE_FILES)` (the files only), `lhde_theme_resolve_with`
  (a preview of a layer not stored), `lhde_theme_resolve_current`.
- Migration: existing `NAME-modified.theme` files stay what they are, ordinary user themes (their `base = NAME` and `name =
  TITLE (modified)`); nothing converts them. Delete them in Display Properties, or keep them under a better name with Save As.
- `lhde_theme_set_key` (the earlier model: the first change writes `NAME-modified.theme` with `base = NAME`, only the changed
  keys, `name = TITLE (modified)`, and selects it) still works for its callers (`lhde-theme set-key`); Display Properties and
  luxshell's flyout no longer use it (section 15).

## 4. The resolver (liblhde, `<lhde/theme.h>`)
`lhde_theme_resolve(name)` returns one flat record `section.key = value` with inheritance, defaults and
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
current layer (section 3), and flxp-desktop reads the resolved record like everyone else. A change of the current layer is a
settings.ini write: `_LH_SETTINGS_CHANGED`, and every client resolves the session theme again (its record now has the layer).

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

