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
[Metrics]                       ; NONCLIENTMETRICS in pixels, fonts as "Face, points[, bold][, italic]"
borderwidth = 1                 ; scrollwidth scrollheight captionwidth captionheight smcaptionwidth smcaptionheight
captionfont = Tahoma, 8, bold   ; menuwidth menuheight; captionfont smcaptionfont menufont statusfont messagefont icontitlefont
```
A scheme file `styles/NAME.scheme` has the same `[Colors]` and `[Metrics]`, a `[Scheme] name = Title` (and `sizes = Normal, Large
Fonts, Extra Large Fonts`), and per font size `[Metrics:SIZE]` (and `[Colors:SIZE]` when the colours differ): `VisualStyle.size`
picks one (XP's "Font size": Normal, Large Fonts, Extra Large Fonts). Schemes are the user's own data: `tools/lhde-schemes`
imports XP's and Windows 2000's from the install media's default hive (`HIVEDEF.INF`, `Control Panel\Appearance\Schemes`),
run by lhde-import for the `xp` and `2k` sources. Resolution: theme files, then the current layer, then the scheme's keys for
what is still absent (source `scheme`); a scheme named but not imported falls back to `none` (the built-in colours).
Only the classic look reads them: a visual style brings its own `[SysMetrics]`.

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
  keys, `name = TITLE (modified)`, and selects it) still works for its callers (luxshell's flyout, `lhde-theme set-key`);
  Display Properties no longer uses it.

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

## 10. Decisions (aap, 2026-10-28; recorded by the lux session)
1. **Backports dropped.** The `-xp` backports were early tests meant for real XP. Every component uses
   the theme's one style; Longhorn styles are rendered natively through wuxtheme. Differences found in
   the side-by-side check become wuxtheme fixes (wux side). `styles/*-xp`, `flxp_msstyles` and the
   `flxp_*` registry keys go away.
2. **Luna** may come from XP or from a Longhorn build; both must work. The Server 2003 ISO
   (`~/lib/windows_isos/`, `AMD64/LUNA.MS_`, `LUNA.TH_`, `BLISS.JP_`) gives the real XP Luna as `styles/luna`
   (plus its colour schemes) and `wallpapers/xp/Bliss`; the 4051/4074 copies import as `luna4051`/`luna4074`.
   The shipped `luna*` themes use `style = luna` and fall back to `luna4051` when only Longhorn was imported.
3. **The library is `liblhde`**, directory `liblhde/` (today's `lhtheme/` renamed), prefix `lhde_`, headers
   `<lhde/data.h>`, `<lhde/theme.h>`, `<lhde/settings.h>`: the data dirs, this resolver and the settings
   store. Lux side writes it; luxwm, luxshell (Lua binding), luxnav's callers and flxp link it.
4. **XP-family resources** come from the Server 2003 ISO (x64 binaries carry the same resources) and the
   Windows 2000 ISO (`5.00.2195.6717_x86fre_DTCServer_en-us.iso`) via `lhde-import`, into `resources/xp`
   and `resources/2k` (shell32, explorer incl. `IDB_START` 143, browseui, shellstyle, comctl32, the icons);
   flxp switches its nt5src readers to resource ids there (wux side).
5. Metal vision: if it is built, its plate can come from 5048's opaque "Metal" uDWM bitmap 1001 or 5112's
   `DWMMetal::DWMWindow` instead of a new drawing.

## 11. Work split for step 4
- **lux side:** `liblhde` (data, resolver with "came from", settings store incl. the migration from
  `~/.config/longhorn` + `~/.config/lhshell` to `~/.config/lhde`), `data/themes/*.theme` for section 8,
  `data/navbar/`, `data/icons/` recipes (taken over from flxp's `res/icons` with the wux side), `lhde-import`
  from ISO/WIM/directory for Longhorn builds, 5048/5112 and XP/2000, frame sets and packs manifest-first with
  a regenerable cache, luxwm and luxshell on the resolver, `_LH_THEME_RESOLVED` in the new format, a generated
  compatibility `themes.ini` until flxp has switched, then removal of `data/themes.ini`/`wallpapers.ini`.
- **wux side:** flxp, flxp-desktop, flxp-taskbar, flxp-settings and the file dialog on `liblhde`;
  the backport removal and the side-by-side check; XP resources by id; `_FLXP_WALLPAPER` retired; the
  settings dialog writing `NAME-modified.theme`.
- Order: lux side delivers `liblhde` + theme files first (with the compatibility registry so nothing breaks),
  wux side switches, lux side removes the old registry.

## 12. Status: the lux side as built (2026-10-28)
Implemented as specified unless listed under "Differences".

- **liblhde** (`liblhde/`, `<lhde/data.h>`, `<lhde/theme.h>`, `<lhde/settings.h>`; `build/lib/liblhde.{a,so.1}`, `lhde.pc`):
  `lhde_theme_resolve(NAME)` returns the flat record (`SECTION.KEY` plus derived `theme`, `file`, `chain`, `style.kind|format|family`,
  `path.style|shellstyle|frames|frames_classic|pack|navbar|icons|wallpaper|fonts|resources|scheme`) and for every key its source
  (`lhde_theme_why`: `file PATH`, `base NAME (PATH)`, `settings: [override:NAME] ...`, `default: ...`, `fallback: REASON`).
  Lookup order: the user's `~/.config/lhde/themes/NAME.theme`, `themes/NAME.theme` of the data directories, `[Theme] aliases`
  (an addition: the x11-era names, `aero-patch` -> `aero4074`), a bare style. Base chains up to 8 files, cycles and deeper chains
  are errors; a base that is missing ends the chain (reported). Style probe (section 5) through wuxtheme: `ut_format`, the classes
  `CompositedWindow`, `DWMWindow` (also `DWMGlass::DWMWindow`), `Sidebar`, the first scheme/size, the family from VERSIONINFO's
  OriginalFilename; the build from the build-qualified name (the styles' version blocks say 1.0.0.1). Kinds: v3 = xp; v4 with
  DWMWindow, or without CompositedWindow at build >= 5000 = dwm; other v4 = longhorn (4051 row below build 4060, else 4074 row;
  slate4051 has no CompositedWindow, only Sidebar). Fallbacks of section 6 incl. style `luna` -> `luna4051` -> `luna4074`, frames
  not imported (a glass set needs its pixels or lhde-import's manifest), pack missing -> taskbar classic, icons -> xp -> wine,
  wallpaper -> the build's own -> none (matched case-insensitively), fonts -> fontconfig, resources -> drawn art.
- **Settings store and migration** (docs/SETTINGS.md "lhde (step 4)"); `lhde_theme_set_key` writes `NAME-modified.theme`
  (a value equal to what the base gives removes the key).
- **`lhde-theme`**: `list`, `resolve NAME [--why|--get KEY|--shell]`, `current`, `set NAME`, `set-key`, `setting`, `style`,
  `migrate`, `registry`, `dirs`. `make check-liblhde` (`liblhde/tests/resolve.c`, 158 checks on fake files).
- **Theme files** `data/themes/`: slate4051, slate4074, jade4074, aero4074, aerovision, basic5048, aero5048, basic5112, aero5112,
  luna, luna-olive, luna-silver, noir (the tuned `color = Metallic`); `slatevision.theme.todo`, `metalvision.theme.todo` (commented,
  not listed). `data/navbar/` (vision.ini, the old frames/aerovision/nav.ini, which stays as a symlink for flxp; 4051.ini and
  4074.ini name flxp's header), `data/icons/` (longhorn4074, xp, 2k: flxp's res/icons format with `pe:shell32.dll#ID` and a
  `resources = resources/BUILD` line; icons only, the toolbar strips stay in flxp's own recipes).
- **Compatibility registry**: liblhde writes `$XDG_RUNTIME_DIR/lhde/<display>/themes.ini` + `wallpapers.ini` from the resolved
  themes (every theme file, the user's, bare styles; paths absolute; `flxp_frame`, `flxp_icons`, `flxp_header_engine`,
  `flxp_longhorn_system32`, `flxp_menubar_style`, `taskbar`, `start`, `[Compat] KEY` passed through); luxwm and luxshell publish
  it as `_LH_THEMES_INI`. `data/themes.ini` and `data/wallpapers.ini` are gone (nothing reads them).
- **luxwm** resolves `_LH_THEME` through liblhde (`lh_theme_apply`): frames + style, frames_classic (none = the style's own),
  `composition = off` or no glass = always classic, `path.fonts`, `Glass.animations`; re-resolves on `_LH_SETTINGS_CHANGED` and
  on a republished record. **luxshell** uses `lh.theme_resolve` (session.lua `S.resolve`); theme keys from its flyout and
  `_LHSHELL_COMMAND taskbar` go into `NAME-modified.theme` and select it; per-user keys into `settings.ini`. Both publish
  `_LH_THEME_RESOLVED` (`theme=`, `serial=`, the record) when it does not describe their record yet.
- **Import**: `lhde-pack` / `lhde-import` (docs/USAGE.md "Getting the Windows resources"), sources in `data/sources/`.

Differences from sections 1-8, decided on the lux side:
1. Frame set names are kept (`jade4074-classic`, `slate4074-classic`, `slate4051-classic`, `luna4051-*`, `aero-patch`): the table's
   "classic frames" column names them by style; renaming would change the frame regression (212 renders, still identical).
2. `basic5048` / `basic5112`: `frames_classic = none` (5048's / 5112's own Window class, the tuned `frame_classic = style` of the
   registry) instead of the table's jade4074.
3. `luna-olive` / `luna-silver` keep their tuned wallpapers (4074/Autumn, 5048/winxp); `luna` has XP's Bliss.
4. `aero4074` / `aerovision` name the demo wallpapers (`demos/...`), which no ISO has: imported by `--from-workspace`, else the
   build's Bliss (a fallback the record reports).
5. Additions: `[Theme] aliases`, `[Shell] startpanel` (the Luna start panels for taskbar = avalon), `[Compat]` (keys copied into
   the compatibility registry). `Shell.sidebar` is the theme's default; the user's sidebar mode (`[sidebar] mode`, the minimise
   button) stays per user and wins when set.
6. Animations default per build (4051: 4074, 4074: 4074plus, 5048/5112: 5048, XP: none); `[theme] animations` is migrated away.
7. The legacy `[override:NAME]` / `[override]` sections and `[desktop] wallpaper` are still honoured as a `settings` layer above the
   files, until flxp's dialog writes `NAME-modified.theme` (wux side).

## 13. Next for the wux side (from the lux side's step-4 report, 2026-10-28)
1. Link liblhde (`$(LHDEA)` or `lhde.pc`) and read the theme with `lhde_theme_resolve(_LH_THEME)` or
   `lhde_theme_parse(_LH_THEME_RESOLVED)` instead of `lookup_session_theme` and the registry. flxp's
   `resolved_field` reads the old `name=;...` format and today falls back to the generated compatibility
   registry. Keys: `path.style`, `VisualStyle.color/size`, `Explorer.chrome/navbar/navbar_preset`,
   `Desktop.icons`/`path.icons`, `path.resources`, `path.wallpaper`, `Desktop.mode`, `Shell.taskbar/start`.
2. Settings path via `lhde_settings_path()`; the dialog writes theme keys with `lhde_theme_set_key`
   (`NAME-modified.theme`) instead of `[override:NAME]`, and no longer writes the global `[taskbar] kind`,
   `[start] menu`, `[desktop] icons`, `[explorer] frame`.
3. flxp-desktop reads `path.wallpaper`/`Desktop.mode` from the record; then luxshell stops writing
   `_FLXP_WALLPAPER`.
4. Backports (decision 1): the compatibility registry no longer emits `flxp_msstyles`, bare `*-xp` styles
   are not listed; do the side-by-side check with the v4 styles.
5. Icons from `data/icons/*.theme` (`resources =`), XP/2000 art by id from `resources/xp`, `resources/2k`
   (imported now); the toolbar strips' bitmap ids are still open.
6. `tests/check.sh`'s ut-dump honours `LHDE_DATA`.
7. Tell the lux side when done: it then removes the compatibility registry and the legacy `[override]` layer.

Status (wux side, 2026-10-28): items 1, 2, 3 and 6 are done (docs/flxp-NOTES.md "flxp on liblhde: the session theme from
theme files"): flxp reads `_LH_THEME_RESOLVED` / `lhde_theme_resolve`, writes theme keys with `lhde_theme_set_key`, no longer
reads the compatibility registry, `[override:NAME]`/`[override]`, `_FLXP_WALLPAPER` or `flxp_*` keys; item 7 applies.
Items 4 and 5 are done too (docs/flxp-NOTES.md "The -xp backports dropped: the side-by-side check" and "XP and 2000 art
by resource id"): one wuxtheme fix (v4 LOGFONTs named "Tahoma Bold"), the remaining differences are the backports' hand
tuning (accepted); the toolbar strip ids are 214-217 (xp shell32), 276-279 (2k browseui); flxp has no nt5src setting. For the
lux side: lhde-import should also take `AMD64/ASMS01.CAB`'s comctl32 6.0 (the file dialog's view/hist strips, 124/130) and,
after this change's `LOGONUI.EX_` line in data/sources/xp.ini, re-import XP for the chess tile.
