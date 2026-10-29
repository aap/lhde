> **History.** Sections 9 to 15 of docs/THEME-FORMAT.md as the two sessions wrote them on 2026-10-28: the open points, aap's
> decisions, the step-4 work split and the status reports of both sides. The format itself (sections 1 to 8) stays in
> docs/THEME-FORMAT.md; the decisions that still matter are recorded there and in docs/ARCHITECTURE.md.

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

- **The three trees** (2026-10-28, later): the single data directory of this section is split into program files
  (`$PREFIX/share/lhde`, `$PREFIX/lib/lhde`), the Windows files (`~/.local/share/lhde-windows`: `<build>/`, `local/`,
  `generated/` (the former `cache/` directories), `index/`) and the user's (`~/.config/lhde`); the lookup is the same relative
  names in that order (docs/LAYOUT.md "The three trees"). `lhde-theme explain [THEME]` shows the file and tree behind every
  component of a resolved theme; `lhde-import --migrate` moves an old directory.

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

Status (wux side, 2026-10-28): items 1, 2, 3 and 6 are done (docs/history/flxp-NOTES.md "flxp on liblhde: the session theme from
theme files"): flxp reads `_LH_THEME_RESOLVED` / `lhde_theme_resolve`, writes theme keys with `lhde_theme_set_key`, no longer
reads the compatibility registry, `[override:NAME]`/`[override]`, `_FLXP_WALLPAPER` or `flxp_*` keys; item 7 applies.
Items 4 and 5 are done too (docs/history/flxp-NOTES.md "The -xp backports dropped: the side-by-side check" and "XP and 2000 art
by resource id"): one wuxtheme fix (v4 LOGFONTs named "Tahoma Bold"), the remaining differences are the backports' hand
tuning (accepted); the toolbar strip ids are 214-217 (xp shell32), 276-279 (2k browseui); flxp has no nt5src setting. For the
lux side: lhde-import should also take `AMD64/ASMS01.CAB`'s comctl32 6.0 (the file dialog's view/hist strips, 124/130) and,
after this change's `LOGONUI.EX_` line in data/sources/xp.ini, re-import XP for the chess tile.

## 14. Themes and Appearance apart (2026-10-28, wux side)
aap: "currently everything is driven by the themes and doing any modifications writes yet another theme". Now as in Windows:
- **Themes** are the theme files only (the 13 shipped, the user's); a change anywhere is the current layer (section 3), shown as
  "NAME (Modified)"; Save As makes it a theme file; selecting another theme drops it. No `NAME-modified.theme` is written by
  Display Properties or Set as Desktop Background any more (the existing ones stay ordinary user themes).
- **Appearance** = XP's tab: "Windows and buttons" (every `styles/` style by its DisplayName, plus "Windows Classic style"),
  "Color scheme" (the style's colours; classic: the schemes of section 2a), "Font size", Effects..., Advanced (classic: the
  per-item editor writing `Colors.*` / `Metrics.*` into the layer). The Longhorn extras moved to a **Shell** tab.
- **For the lux side** (liblhde changes, all additive; nothing removed): `enum lhde_source` gained `LHDE_FROM_CURRENT`,
  `LHDE_FROM_SCHEME`; `lhde_theme_resolve` applies the current layer when it belongs to the name resolved (the derived key
  `current`); new `lhde_theme_resolve_flags` (`LHDE_RESOLVE_FILES`), `lhde_theme_resolve_with`, `lhde_theme_resolve_current`,
  `lhde_current_theme/keys/set_key/set/clear`, `lhde_theme_select`, `lhde_theme_save_as`, `lhde_theme_delete`,
  `lhde_style_list`, `lhde_scheme_list`, `lhde_color_name`, `lhde_metric_name`; `lhde_theme_is_key` accepts `Colors.*` /
  `Metrics.*`; sections `[Colors]`, `[Metrics]`; a missing `styles/NAME.scheme` falls back to `VisualStyle.scheme = none`;
  `lhde-theme set` clears another theme's layer, new `lhde-theme layer | layer-set | layer-clear | save-as | delete | styles |
  schemes`, `resolve --files`. Wanted from the lux side: luxshell's flyout and `_LHSHELL_COMMAND taskbar` write the layer
  (`lhde_current_set_key`, a `lh.current_set_key` binding) instead of `lhde_theme_set_key`, its theme switch through
  `lhde_theme_select`, its theme list without the bare styles; luxwm: a frame for `style = classic` (today it keeps the previous
  theme's frames: "no frame set and no visual style"), drawn from the record's `Colors.*` (activecaption,
  gradientactivecaption, inactivecaption, gradientinactivecaption, captiontext, inactivecaptiontext, activeborder,
  inactiveborder, btnface/btnhighlight/btnshadow/3ddkshadow/3dlight for the edges and buttons) and `Metrics.*` (captionheight,
  captionwidth, borderwidth, captionfont), i.e. Windows' classic DrawCaption / DrawEdge frame.

## 15. The lux side of section 14 (2026-10-28, lux side)
- **luxwm: Windows Classic frames.** A record with `style.kind = classic` (and a theme with neither a frame set nor a visual
  style) gets the classic frame instead of keeping the previous one: luxwm passes the record's `Colors.*`, `Metrics.borderwidth /
  captionwidth / captionheight / captionfont / menufont` and `Fonts.smoothing` to frame.c, which paints user32's non-client area
  (DrawEdge, the sizing border, DrawFrameControl caption buttons with Marlett's glyphs as bitmaps, the GradientFill caption, the
  text placed as GDI places it); `frames_classic` and `Glass.frames` do not apply then. Measured against Longhorn 4051's Windows
  Classic: the caption of an 800 px window is pixel-identical (docs/LUXWM.md "Windows Classic frames"). The window menu takes the
  scheme's menu colours and `Metrics.menufont`. `luxwm/tests/regress.sh`: `classic/`, `classic.xl/`, `classic.hc/`.
- **luxshell: the layer.** The settings flyout (composition, taskbar, start menu) and `_LHSHELL_COMMAND taskbar KIND` write
  `Shell.*` / `Glass.composition` into the current layer (`lhde_current_set_key`, the Lua binding `lh.current_set_key`; then
  `_LH_SETTINGS_CHANGED` with the hint `current` and the record republished, `_LH_THEME` keeps its name); no `NAME-modified.theme`.
  Theme switches go through `lhde_theme_select` (another theme's layer dropped), Save As through `lhde_theme_save_as`. The flyout's
  theme list has the theme files only (the shipped ones, then the user's), the selected one with a layer reads "TITLE (Modified)"
  (the resolved key `current` > 0; session.lua `e.modified`, `e.display`). Bindings: `lh.current_set_key`, `lh.current_keys`,
  `lh.current_theme`, `lh.theme_select`, `lh.theme_save_as`. The per-user keys (`[sidebar] mode` and stacking, `[taskbar]
  autohide`, `[desktop] wallpaper`) stay in settings.ini as before.
- **Wanted from the wux side (wuxtheme):** wux already vendors Wine's user32 drawing code (`wux/vendor/wine/user32/uitools.c`:
  `UITOOLS95_DrawRectEdge`, `UITOOLS95_DFC_ButtonPush`, `UITOOLS95_DrawFrameCaption`, and the caption painting behind flxp's
  classic windows). If libwuxtheme exported three plain-C helpers that draw into a 32-bit pixel buffer with a caller-given
  system-colour table (the 31 COLOR_ values) instead of the process's own, `ut_draw_edge(px, w, h, stride, rect, edge, flags,
  colors)` (DrawEdge, BF_* / BDR_* as Windows), `ut_draw_frame_control(px, w, h, stride, rect, DFC_CAPTION, DFCS_* state,
  colors)` (with the Marlett glyphs it renders: the font from the user's data, or its own bitmaps) and `ut_draw_caption(px, w, h,
  stride, rect, flags DC_ACTIVE | DC_GRADIENT | DC_ICON | DC_TEXT, title, icon, font, colors)` (DrawCaptionTemp: icon slot,
  GradientFill, text), luxwm's `classic_paint` would call them instead of its own copies (frame.c `cl_edge`, `cl_button`,
  `cl_gradient`, `cl_text`), and the window manager's frame, flxp's own classic windows and Display Properties' sample would be
  drawn by one implementation, byte for byte. luxwm keeps the geometry (cnBorders, the button rectangles, maximised), the hit test
  and the shape.
