> **Status: draft.** Copied unchanged from the research workspace's
> `LAYOUT-draft.md` (2026-10-27, with the wux session's review at the end) in
> step 2 of its plan; to be consolidated with ARCHITECTURE/PROTOCOL in step 5.
> Step 2 as built (paths, build, what was left out): README.md. Step 3 (the lux side) as built: the
> section "Step 3 as built" at the end of this file. Step 4 (lux side): "Step 4 as built (lux side)" at the end, and
> docs/THEME-FORMAT.md section 12. **The files on disk as they are now: "The three trees" right below**; the paths in the
> draft and the step sections after it (one data directory `~/.local/share/lhde` with `cache/` subdirectories) are history.

# The three trees (on disk, 2026-10-28)

Everything lhde reads at run time lives in one of three trees, and nothing is in two of them. Who writes a tree is the only
thing that may change it:

```
PROGRAM FILES                        make install; never edited by hand (a later make install overwrites them)
$PREFIX/share/lhde/                  shipped data, our text only (DATADIR)
  themes/NAME.theme                    the shipped themes
  navbar/NAME.ini                      navigation bar layouts (4051 4074 vision)
  icons/NAME.theme                     icon recipes (pe:shell32.dll#ID from resources/<build>)
  sources/ID.ini                       what lhde-pack takes from each Windows build, the recipes lhde-import runs
  frames/SET/theme.ini (+ nav.ini)     frame-set tuning; the pixels are generated, below
  packs/PACK/shell.lua                 luxshell's packs; packs/startpanels.lua
  res/icons/                           flxp's own (Wine's) icons
  .lhde-install                        the list of the files above (lhde-import --migrate tells them from yours)
$PREFIX/lib/lhde/                    code that is not a binary (PKGLIBDIR)
  luxshell/                            luxshell's Lua (runtime.lua, tiles/, ...)
  session/                             lhde-session's helpers: lhde-paths.sh, lhwall.sh, lab.sh, theme-*.sh, picom configs
  tools/                               lhde-import's Python: lhdeimport.py, lhdetree.py, lhdeschemes.py, recipe builders
$PREFIX/bin/                         luxwm luxshell flxp* lhde-theme lhde-session lhde-import lhde-pack lhde-schemes ut-dump

WINDOWS FILES                        lhde-import; $LHDE_WINDOWS, default ${XDG_DATA_HOME:-~/.local/share}/lhde-windows
~/.local/share/lhde-windows/
  <source>/                            one build's raw files, exactly as the resource pack has them (2k 4015 ... 5112 xp)
    system32/                            shell32 imageres explorer browseui comctl32 themeui [shellstyle uDWM shdocvw]
    themes/                              the build's Resources/Themes: Jade/jade.msstyles, Luna/Shell/<Scheme>/shellstyle.dll, ...
    fonts/  wallpapers/  setup/          (setup/hivedef.inf: the xp and 2k classic schemes)
    source.ini                           the rules it was taken with
  local/                               files that come from no source: themes/ (lhde-import --style: aero-patch, zune, ...),
                                       schemes/ (lhde-schemes), wallpapers/ resources/ fonts/ (copied by hand, the workspace)
  generated/                           everything derived; deleting it is always safe (lhde-import --regenerate)
    frames/SET/                          PNGs + manifest.ini (recipe, inputs with sha256); theme.ini of a set we do not ship
    packs/<build>/                       imageres/ID.png, shell32/SIZE/ID.png, startpanel-NAME/, manifest-*.ini
    schemes/NAME.scheme                  from a source's HIVEDEF.INF
  index/                               relative symlinks under the names programs ask for (lhde-import --reindex):
    styles/NAME.msstyles, styles/NAME/     -> ../../<source>/themes/<Style>/... or ../../local/themes/...
    styles/NAME.scheme                     -> generated/schemes or local/schemes
    resources/<source> fonts/<source> wallpapers/<source>   -> ../../<source>/system32, fonts, wallpapers
    frames -> ../generated/frames,  packs -> ../generated/packs
  import.log

USER FILES                           yours; lhde writes only what you change in its settings UIs
~/.config/lhde/                      ${XDG_CONFIG_HOME:-~/.config}/lhde
  settings.ini                         per-user settings, the selected theme and its current layer ("NAME (Modified)")
  themes/NAME.theme                    your themes (Save As)
  <any shipped path>                   a copy of a shipped file under its own name overrides it (navbar/4074.ini, ...)
```

**Lookup.** A program asks for a file by kind and name, i.e. by its relative name (`styles/jade4074.msstyles`,
`frames/jade4074/close.png`, `resources/4074`, `wallpapers/4074/Bliss.jpg`), and liblhde (`liblhde/data.c`, `<lhde/data.h>`)
answers from the first of: 1. `~/.config/lhde`; 2. `~/.local/share/lhde-windows/index` (or `$LHDE_DATA`, a flat directory:
tests, scratch imports); 3. an old-layout `~/.local/share/lhde` still holding Windows files (until it is migrated);
4. `$PREFIX/share/lhde` of the running program, then `$XDG_DATA_DIRS/lhde`; 5. the source tree's `data/` when the program runs
from a build tree. Code is found by `lhde_component_dir`: `<tree>/luxshell` or `<prefix>/lib/lhde/luxshell`. Scripts use the
same lookup through `lhde-theme find REL` / `find KIND NAME` / `dirs --list` (session/lhde-paths.sh); lhde-import is the only
writer of the Windows tree and the only code that knows its inside (`tools/lhdetree.py`). A frame set or pack is split by
design: its text is shipped, its pixels are generated; `lhde_data_find_in` joins them.

**Where does this pixel come from.** `lhde-theme explain [THEME]` prints one row per visible component (visual style and
scheme, glass and classic frame set, taskbar, shell pack and its imageres, start menu, navbar and its art, icon recipe and its
DLLs, wallpaper, fonts, resources) with the setting, `ok` / `off` / `missing -> fallback X` and the file with its tree;
`lhde-theme dirs` shows the trees and their sizes (docs/USAGE.md "When something looks wrong").

**Migration.** Before this layout everything was in `~/.local/share/lhde` (with `make install PREFIX=~/.local` also the
install's own share/lhde): shipped text, luxshell's Lua, session and tools beside 435 MB of Windows files and caches, frame sets
and packs mixing shipped `theme.ini`/`shell.lua` with generated PNGs (often twice, beside theme.ini and in `cache/`).
`lhde-import --migrate` (`-n`: only list) moves it without reading any ISO: `resources/<b>`, `fonts/<b>`, `wallpapers/<b>` ->
`lhde-windows/<b>/{system32,fonts,wallpapers}` (names that are no source, e.g. `demo`, `mock`: `local/`); each
`styles/NAME.msstyles` (+ `styles/NAME/Shell`) to the place its source's `[styles]` names (`4074/themes/Jade/jade.msstyles`),
the others to `local/themes`; the schemes to `local/schemes` (the old layout kept no HIVEDEF.INF to remake them); frame and
pack PNGs, `startpanel.ini` and manifests -> `generated/` without the `cache/` level (where a PNG was there twice, the one
beside `theme.ini`, which the programs used, is kept); shipped text stays when the directory is the install's own (it has
`.lhde-install`), else is deleted as a stale copy, and a text file we do not ship (your own theme) goes to `~/.config/lhde`;
`luxshell/ session/ tools/` are deleted (now in `$PREFIX/lib/lhde`); then `<b>/source.ini` (the shipped sources/ID.ini) and
`index/`. It is idempotent (a second run finds nothing to do) and lhde-session runs it once by itself at start
(`$XDG_RUNTIME_DIR/lhde-session:N/layout-migration.log`). An unmigrated directory keeps working meanwhile (tree 3 above).
Switching an existing setup: `make install PREFIX=~/.local`, then `lhde-import --migrate -n`, then `lhde-import --migrate`.

# lhde: layout, asset policy and theme model (draft for the merge)

Draft by the lux session, 2026-10-27, from `merge_feedback.txt` (wux agent, lux agent, aap). To be
agreed by both sessions and aap, then committed as `doc/LAYOUT.md` in the new repo.

## Names and tree
```
lhde/                     Longhorn desktop environment
  luxwm/                  window manager            (was lhwm)
  luxnav/                 navigation bar library    (was lhnav)
  luxshell/               Avalon bars, sidebar, tiles, toasts; the Lua part  (was lhshell / "lux")
  wux/                    Win32 UI layer: gdi, sys, win, w32, vendor/wine
  wuxtheme/               the uxtheme library (was libuxtheme, src/uxtheme); C API prefix ut_ unchanged
  flxp/                   explorer, desktop, taskbar, filedlg, settings, portal
  session/                lhde-session (replaces xinitrc.lab, second-display.sh, lab.sh)
  tools/                  lhde-import, recipe builders (build_aerovision...), ut-dump
  examples/               navdemo, extendframe, glassdemo
  data/                   theme files, navbar presets, recipes: text only, no pixels
  docs/                   LAYOUT, ARCHITECTURE, PROTOCOL (one doc for all X properties),
                          THEMES, SETTINGS, MSSTYLES-FORMAT, NAVBAR
```
The research workspace (COMPOSITION.md, extracted builds, archaeology tools) stays in
`~/fun/longhorn`, outside the repo. X atoms (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`) keep their names
(LH = Longhorn; renaming is churn for no gain).

## Asset policy
Nothing Microsoft-derived is in the repo: no `.msstyles`, DLLs, fonts, wallpapers, extracted or
rendered bitmaps, no reference renders. The repo holds code, configuration and recipes (numbers,
presets, scripts that derive art). The user's own Windows files are imported once:

`lhde-import SOURCE` (an ISO, a WIM, a mounted or copied Windows directory) fills
`~/.local/share/lhde/` (system-wide: `$PREFIX/share/lhde/`) [superseded: the Windows tree `~/.local/share/lhde-windows`,
"The three trees" above]:
```
styles/      *.msstyles (+ shellstyle.dll beside them)      drop-in also by hand, as with wine
frames/      DWM frame sets in our format (ini + png), extracted from CompositedWindow (4042-4093),
             DWMWindow (5112) or uDWM bitmaps (5048); vision frames generated by their recipes
packs/       luxshell packs in our format (pack.lua + png), extracted from imageres.dll
resources/   shell32/imageres/explorer DLLs for icons and framelets, by build name
fonts/       per build
wallpapers/  per build
```
**The resource pack** (step 4): users need no ISOs. `tools/lhde-pack` takes, by the rules of `data/sources/<source>.ini`, only the
raw, unmodified files each build contributes and writes `lhde-resources-<date>[-essentials]/` (and `.tar.zst`, else `.tar.xz`):
```
<source>/system32/   shell32.dll imageres.dll explorer.exe browseui.dll comctl32.dll themeui.dll [shellstyle.dll uDWM.dll shdocvw.dll]
<source>/themes/     the build's Resources/Themes (<Style>/<style>.msstyles, Shell/<Scheme>/shellstyle.dll, *.theme)
<source>/fonts/      Segoe UI 0.7/0.89, Tahoma, Trebuchet MS
<source>/wallpapers/ Web/Wallpaper (setup layouts: the named *.JP_ / *.BM_)
<source>/setup/      hivedef.inf (xp, 2k: the classic Appearance schemes)
<source>/source.ini  the rules it was made with ([files]) and what lhde-import derives from it ([styles], [frames], [packs])
MANIFEST             "# ..." header (format; "# profile: essentials|museum"; "# themes: ..."), then one line per path:
                     sha256, size, pack path, source, medium:in-image path
```
Two **profiles**, same layout and MANIFEST format (lhde-import --pack cannot tell them apart): `museum` holds everything the
rules take; `essentials` (default) only the files a list of themes needs (the shipped themes, or `--themes`), a subset of the museum
pack, made *from* it: the museum pack is imported into a scratch Windows tree and `lhde-theme explain --files THEME`
(liblhde/explain.c, `lhde_explain_files`) prints, per theme, the real paths of the Windows files the resolver reads: style
and Shell/ directory, the inputs of the manifests of its frame sets and pack images (the recipes record them with sha256), the
icon recipe's `pe:` DLLs, the wallpaper, the build's fonts, the DLLs flxp/luxnav/luxshell open (`FLXP_DLLS`), and
`setup/hivedef.inf` for a classic scheme. Builds nothing needs are absent. `lhde-pack --from-pack DIR` re-profiles an existing
pack without ISOs (the day-to-day way); from ISOs it makes the museum pack in `--tmp` first.
**Deduplication**: files with equal sha256 are stored once. Within the pack directory the later paths are hard links to the first
(relative symlinks if links fail); `tar` (never `--hard-dereference`) stores them once; `lhde-import --pack` copies each path
as a normal file (`copy_file` follows links), so the Windows tree, its index and the recipes never see a link. Extended
attributes belong to the inode: the pack's annotation of a shared file is the first path's and is not copied on import
(`shared()` in tools/lhdeimport.py); lhde-import annotates each imported file from its own path (`annotate_tree`). The MANIFEST
keeps one line per path with its own in-image source and is the authority.
No derived pixels go into a pack: `lhde-import --pack` copies the raw files into the Windows tree and runs the recipes there
(manifest-first `generated/` directories). The pack and the ISOs never enter the repository; `data/sources/*.ini` and the tools do.

Tests: `make check` renders locally and compares checksums; it skips when `~/.local/share/lhde`
is missing. Git history: start fresh (aap: no need to keep the messy past); the old x11 and flxp
repos stay archived locally.

## Theme model (aap's two layers)
**Components**, each chosen independently:
1. visual style: a `.msstyles` (+ scheme, size; shellstyle.dll supported): classic decorations,
   controls, menus, start panel, everything uxtheme;
2. DWM frame set (glass decorations), from `frames/`;
3. luxshell pack (Avalon bars), from `packs/`;
4. navbar preset set (`data/navbar/*.ini`; shipped: 4051, 4074, vision; later 5048, 5112, which
   should need nothing but a preset: a test of the architecture);
5. wallpaper (+ mode), icon set, fonts, cursors later.

**Theme files** tie components together, like Windows `.theme` files: `data/themes/*.theme`
(shipped: Slate 4051, Slate 4074, Jade 4074, Aero patch, Aero 5048, Aero 5112, Aero vision, Slate
vision, Luna, Royale, ...), user themes in `~/.config/lhde/themes/`. One file holds every choice:
```
[Theme]       name = Jade (4074)
[VisualStyle] style = jade4074 ; color = NormalColor ; size = NormalSize
[Glass]       frames = jade4074 ; composition = auto ; animations = 4074plus
[Shell]       pack = aero4074 ; taskbar = avalon ; start = flxp ; sidebar = top
[Navigation]  navbar = 4074
[Desktop]     wallpaper = 4074/Bliss ; mode = fill ; icons = longhorn4074
[Fonts]       fonts = 4074
```
Components are referenced by name, never by path. Missing components (not imported) degrade:
no frame set = classic frames from the style; no pack = classic flxp taskbar; the theme picker
shows what is missing. A bare `.msstyles` in `styles/` appears as a theme of its own with defaults
derived from the file (v3: XP explorer, classic taskbar; v4 with CompositedWindow/DWMWindow:
Longhorn, glass when composited).

`settings.ini` keeps only the current theme name and per-user, not-per-theme settings. Editing a
theme in the settings dialog edits a user copy ("Save as"), which replaces today's
`[override:NAME]` sections, `themes.ini`, `wallpapers.ini` and presets. All four go away.

## Build, install, session
Top-level Makefile over the components: wuxtheme and luxnav as real shared libraries with
pkg-config files, wux static, then the programs; `make install PREFIX=`; `make check`.
`lhde-session` starts luxwm, picom (when the theme wants it), luxshell or flxp-taskbar,
flxp-desktop and the portal; `--nested` runs it in Xephyr. Config moves to `~/.config/lhde/` with a
one-time migration from `~/.config/longhorn/` and `~/.config/lhshell/`.

## Order and ownership
Only one session moves files at a time.
1. Agree this document.
2. wux session: skeleton, import of wux/wuxtheme/flxp, top-level build.
3. lux session: import luxwm/luxnav/luxshell/session/examples/tools/data into the skeleton, renames,
   path cleanup, `lhde-session`.
4. Theme model, split: wux side the style discovery and shellstyle; lux side `lhde-import` for
   frames, packs, resources, fonts, wallpapers, and the theme-file loader in luxwm/luxshell; the
   theme-file format itself first, as a joint doc.
5. Docs consolidated (ARCHITECTURE, PROTOCOL) once things stand still.
Afterwards the sessions own directories: wux side `wux/ wuxtheme/ flxp/`, lux side
`luxwm/ luxnav/ luxshell/ session/`; shared `data/ docs/ tools/` by agreement.

## Review by the wux session (2026-10-27)
Agreed as written: names and tree (wuxtheme as its own top-level dir is better than inside wux/, since
luxwm and luxnav use it without wux), asset policy, fresh history, the theme model, build/session,
order and ownership. Additions and two corrections:

1. **nt5src is an asset too.** flxp still reads XP art from the leaked source tree at run time:
   `src/shell/icons.cpp` and `res/icons/{xp,2k}.theme` (XP/2000 icons and toolbar strips),
   `src/explorer/places.cpp` (task-pane headers and chevrons: shellstyle bitmaps),
   `src/taskbar/taskbar.cpp` / `startpanel.cpp` (start.bmp, LogoffButtons.bmp, the user picture tile),
   plus `browtb.bmp` for the Folders bar. All of these exist as resources in XP's own binaries
   (shell32.dll, explorer.exe, browseui.dll, shellstyle.dll), so `lhde-import` must accept an XP
   (and 2000) install or ISO as well, and fill `resources/xp/`, `resources/2k/`; the wux side switches
   those readers from nt5src paths to resource ids in those DLLs. The repo never needs nt5src.
   *Done (2026-10-28):* flxp reads them by id from `resources/xp`, `resources/2k`; no `nt5src=` setting
   (docs/flxp-NOTES.md "XP and 2000 art by resource id").
2. **Reference screenshots** (flxp's `screenshots/*.png` from real XP, the lhwm regress refs) are
   Microsoft renders: they move to the research workspace; `make check` compares against locally
   generated baselines (checksums), as the draft says.
3. **Frames and packs: manifest first, pixels only as a cache.** wuxtheme reads PNGSTREAM/IMAGEDATA
   and bitmaps from a style live, so an imported frame set can be `frame.ini` naming its source
   (`style = jade4074`, stream ids, insets) with a `cache/` of extracted PNGs that may be deleted and
   rebuilt. Vision frames are recipes run into the cache. Same for packs (imageres ids).
4. **The theme file needs the Explorer's own choices** (today `flxp_frame`, `header_engine`, the
   default view) and the icon set by name:
   ```
   [Explorer]  frame = longhorn | xp ; navbar = 4074 | 4051 | vision | none
   [Desktop]   ... ; icons = longhorn4074 | xp | 2k
   ```
   Correction to "navbar presets 4051/4074 in data/navbar/": luxnav has only the vision layouts so
   far; the faithful 4051/4074 headers are flxp's own code (navbar.cpp/header.cpp). Until luxnav grows
   those layouts (NAVBAR.md "Missing"), `navbar = 4051|4074` selects flxp's header and `vision*`
   selects luxnav with `data/navbar/vision.ini`. 5048/5112 then become the real test of whether
   luxnav can take over the faithful ones.
5. **Icon sets are recipes** like the navbars: `data/icons/*.theme` (flxp's res/icons format: keys to
   `resources/<build>/shell32.dll#id`), never files.
6. **Settings dialog**: editing a shipped theme makes it "NAME (modified)", as Windows does; the per-theme scope combos go
   away with `[override:*]`. (2026-10-28: the change is the unsaved current layer in settings.ini, not a copy; Save As
   writes a theme file. THEME-FORMAT section 3.)
7. **Fonts**: per-build font dirs from the import, then fontconfig as the fallback (unchanged).
8. Wine's vendored code (LGPL) stays in `wux/vendor/wine` with its MANIFEST and patches.

Ready to start step 2 (skeleton, import of wux/wuxtheme/flxp, top-level build) once aap says go.

## Step 3 as built (lux session, 2026-10-28)
Imported from `~/fun/longhorn/x11` at d353b69: luxwm/ (lhwm), luxnav/ (lhnav), luxshell/ (lhshell without assets/), examples/,
session/, data/ (text only), tools/ (lhde-import, the recipe builders), docs/ (USAGE, THEMES, SETTINGS, NAVBAR, MSSTYLES-FORMAT,
SHELL-DESIGN, LUXWM = lhwm/README, CLIENT-PROTOCOL, CLASSIC-DECO-NOTES; names updated, history kept), and lhtheme/ (new). One
top-level build (`make`, `make check`, `make install`); README.md has the commands. Choices taken where this document left room:

1. **Data directories.** lhtheme/lhde_data.c (the seed of step 4's resolver; compiled into luxwm, libluxnav, luxshell, glassdemo)
   searches `$LHDE_DATA` (else `~/.local/share/lhde`), the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the tree's `data/`. A
   frame set or pack is split: its text ships (data/frames/NAME/theme.ini, data/packs/NAME/shell.lua), its pixels are imported;
   the lookups find them together. session/lhde-paths.sh does the same for the scripts.
2. **Registry stays for now**, its values relative to the data directories (`styles/...`, `frames/...`, `packs/...`, `fonts/...`,
   `resources/<build>`). flxp resolves registry values against the registry's own directory and is not changed, so luxwm and
   luxshell publish a resolved copy (`$XDG_RUNTIME_DIR/lhde/<display>/themes.ini` + wallpapers.ini, every path absolute,
   refreshed when the source changes) as `_LH_THEMES_INI`. Goes away with the theme files of step 4.
3. **Names in the data directory** (THEME-FORMAT comments, build-qualified): styles as the workspace has them (aero-patch, jade4074,
   slate4074, slate4051, luna4051, luna-xp, aero5048, aero5112, royale, zune, noir, embedded, plex4015; backports
   `styles/jade4074-xp/`, `styles/slate4074-xp/` with Shell/); frame sets aero-patch, jade4074, aero5048, aero5112, aerovision,
   jade4074-classic, slate4074-classic, slate4051-classic, luna4051-{blue,olive,silver}; pack recipes 4074-aero, 4074-slate,
   4051-slate, 5112-aero (not `5112`: packs/5112 is the build's pixel directory), 4074-aerovision; pack pixels per build
   (`packs/4074/{imageres,shell32,startpanel-jade,startpanel-slate}`, `packs/4051/{imageres,startpanel-luna-*}`, `packs/5112/imageres`);
   `resources/4074`, `resources/4051` (a build's system32), `resources/demo`; `fonts/4074|5048|5112`; `wallpapers/<build>`.
4. **lhde-import, first version** (`--from-workspace DIR`): copies; `resources/<build>` are symlinks into the workspace's extracted
   system32 (713 MB each, and flxp reads their `../Resources` and `../Microsoft.NET` siblings) unless `--copy-resources`. The
   startpanel.ini files (StartPanel sections decoded from the styles by shellpack.sh) travel with the pack pixels, not in data/.
5. **Names kept**: X atoms; the window manager's `_NET_WM_NAME` "lhwm" (luxshell and flxp's log-off match it); the X classes of
   luxshell's and luxwm's own windows (`lhshell-*`, `lhwm-menu`); luxnav's C API and header (`lhnav_*`, `lhnav.h`; library
   libluxnav, pkg-config luxnav); configuration paths (`~/.config/longhorn`, `~/.config/lhwm/keys`, `~/.config/lhshell`) and
   `LHWM_*` / `LHSHELL_*` variables. Install-only symlinks lhwm, lhwmc, lhshell.
6. **luxnav is always built** and linked into flxp (build glue in flxp/flxp.mk, the step-2 `LUXNAV_DIR` hook and build/compat removed).
7. **lhde-session** = xinitrc.lab + second-display.sh, plus `start --attach` (a session on a running X server: how it was tested
   here, on Xvfb); `desktop off` now repaints the root with the current theme's wallpaper. session/lab.sh stays the headless bench
   (with a `shell` command).
8. **Regression without reference images**: luxwm/tests/regress.sh compares SHA-256 with a baseline of this machine; the
   baseline was taken from the x11 build's own renders (`--baseline-from-x11`): 212 renders, all identical.

Left out (asset policy or obsolete): every .png/.bmp/.jpg of x11 (frame sets' PNGs, lhshell/assets, tests/ref, compare_*.png,
screenshots; the art reaches the data directory through lhde-import), ref/, binaries, x11's README.md (the glass-experiment
notes), lhwm's configure/Config.mk.in/config.h.in (config.h is static now), tools/uxtheme-flags.sh, lhnav/ut-flags.sh,
lhwm/examples/compile_commands.json and berry's xephyr.sh (killall), the unused lhshell assets sidebar-jade74, sidebar-slate51,
wallpaper/.

Open for step 4 (lux side): lhde-import from ISO/WIM/Windows directories (and XP/2000 for `resources/xp`, `resources/2k`);
frames and packs manifest-first (frame.ini naming the style's streams, packs naming imageres ids read live from
`resources/<build>/imageres.dll`, PNGs only as a cache), the recipe builders (build_*.py, shellpack.sh, msstyles2theme.py) run by
lhde-import; the theme files and liblhtheme replacing themes.ini, wallpapers.ini, presets and the resolved copy; the config move
to `~/.config/lhde`. For the wux side: flxp reading the data directories itself (today through the resolved registry), and
tests/check.sh's ut-dump honouring `LHDE_DATA` (it reads `LHDE_STYLES`).

## Step 4 as built (lux side, 2026-10-28)
1. `lhtheme/` is `liblhde/` (headers `<lhde/data.h>`, `<lhde/theme.h>`, `<lhde/settings.h>`; `lhde_data.h` kept as a one-line
   include): the data directories, the resolver, the settings store and migration, the compatibility registry; `liblhde.a`,
   `liblhde.so.1`, `lhde.pc`, `lhde-theme`, `make check-liblhde`. Its style probe calls wuxtheme through weak symbols (a program
   without libwuxtheme gets no probe). flxp and its tests link liblhde's objects (`flxp/flxp.mk` SHELLOBJS, build glue only).
2. Theme files in `data/themes/`, `data/navbar/`, `data/icons/`, `data/sources/`; `data/themes.ini` and `data/wallpapers.ini` are
   removed, flxp gets the generated registry (`_LH_THEMES_INI`). The source tree is recognised by `data/themes/` + `luxwm/wm.c`.
3. Manifest-first caches: every lookup of `DIR/NAME` in the data directories also tries `DIR/cache/NAME`; lhde-import writes the
   derived pixels there with a `manifest.ini` (`frames/<set>/manifest.ini`, `packs/<build>/manifest-<target>.ini`). A
   CompositedWindow frame set (jade4074, aero-patch) needs no cache at all (luxwm reads the streams live); its cache serves
   `LHWM_THEME_SOURCE=dir`.
4. Compared with aap's existing data directory, the recipes reproduce: every classic frame cache (jade4074-classic,
   slate4074-classic, slate4051-classic, luna4051-*: byte-identical), the CompositedWindow sets (aero-patch, jade4074,
   aerovision's vision buttons: pixel-identical), the imageres and shell32 pack images and the five start panels (byte-identical;
   startpanel.ini now has real DEFAULTPANESIZE values), the 5112 taskbar art (identical); aero5048 within 1 level (the
   un-premultiply rounding of the original is not known), aero5112's plates differ in about 860 corner pixels of near-zero alpha
   (the workspace's copy was retouched after build_aero5112.py; `old/` there).
5. Settings: `~/.config/lhde/` (SETTINGS.md "lhde (step 4)"); `~/.config/lhwm/keys` stays.
