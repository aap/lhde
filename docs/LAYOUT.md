# lhde's files on disk: the three trees and the asset policy

What lhde reads at run time, where it lives, and who is allowed to write it. The merge plan this layout came from, and the
"as built" reports of the merge steps, are in docs/history/LAYOUT-draft.md.

## Asset policy

Nothing Microsoft-derived is in the repository: no `.msstyles`, DLLs, fonts, wallpapers, extracted or rendered bitmaps, no
reference renders or screenshots of them. The repository holds code, configuration and recipes (numbers, presets, scripts that
derive art). Windows art is read at run time from the user's own files, imported once into the Windows tree below by
`lhde-import` (from a resource pack made by `lhde-pack`, an ISO, a WIM or a Windows directory; docs/USAGE.md "Getting the
Windows resources"). The icons in `flxp/res/icons` are Wine's (LGPL), the `.res` files in `wux/rc` are built from Wine's `.rc`
sources, and Wine's vendored code stays in `wux/vendor/wine` with its MANIFEST and patches. Tests compare against checksums of
local renders, never against reference images. Resource packs built in the tree (`lhde-resources-*`) are gitignored.

## The three trees (on disk)

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
  packs/PACK/shell.lua                 luxshell's packs
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
    packs/<build>/                       imageres/ID.png, shell32/SIZE/ID.png, manifest-*.ini
    schemes/NAME.scheme                  from a source's HIVEDEF.INF
  index/                               relative symlinks under the names programs ask for (lhde-import --reindex):
    styles/NAME.msstyles, styles/NAME/     -> ../../<source>/themes/<Style>/... or ../../local/themes/...
    styles/NAME.scheme                     -> generated/schemes or local/schemes
    resources/<source> fonts/<source> wallpapers/<source>   -> ../../<source>/system32, fonts, wallpapers
    frames -> ../generated/frames,  packs -> ../generated/packs
  import.log

USER FILES                           yours; lhde writes only what you change in its settings UIs
~/.config/lhde/                      ${XDG_CONFIG_HOME:-~/.config}/lhde: what you chose (docs/SETTINGS.md "Files")
  settings.ini                         per-user settings, the selected theme and its current layer ("NAME (Modified)")
  themes/NAME.theme                    your themes (Save As)
  flxp.ini                             flxp's preferences (any key also FLXP_<KEY> for one run)
  keys                                 luxwm's key bindings
  <any shipped path>                   a copy of a shipped file under its own name overrides it (navbar/4074.ini, ...)
~/.local/state/lhde/                 ${XDG_STATE_HOME:-~/.local/state}/lhde: what the programs remember on their own; losing it
  luxshell-state.ini                   costs nothing but convenience. The tile order
  luxshell-mfu.ini                     launch counts behind the start menu's frequently-used list
  flxp-views, flxp-desktop-positions   the view remembered per folder, the desktop icons' positions
```

**Lookup.** A program asks for a file by kind and name, i.e. by its relative name (`styles/jade4074.msstyles`,
`frames/jade4074/close.png`, `resources/4074`, `wallpapers/4074/Bliss.jpg`), and liblhde (`liblhde/data.c`, `<lhde/data.h>`)
answers from the first of: 1. `~/.config/lhde`; 2. `~/.local/share/lhde-windows/index` (or `$LHDE_DATA`, a flat directory:
tests, scratch imports); 3. `$PREFIX/share/lhde` of the running program, then `$XDG_DATA_DIRS/lhde`; 4. the source tree's
`data/` when the program runs from a build tree. Code is found by `lhde_component_dir`: `<tree>/luxshell` or `<prefix>/lib/lhde/luxshell`. Scripts use the
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
pack PNGs and manifests -> `generated/` without the `cache/` level (where a PNG was there twice, the one
beside `theme.ini`, which the programs used, is kept); shipped text stays when the directory is the install's own (it has
`.lhde-install`), else is deleted as a stale copy, and a text file we do not ship (your own theme) goes to `~/.config/lhde`;
`luxshell/ session/ tools/` are deleted (now in `$PREFIX/lib/lhde`); then `<b>/source.ini` (the shipped sources/ID.ini) and
`index/`. It is idempotent (a second run finds nothing to do) and lhde-session runs it once by itself at start
(`$XDG_RUNTIME_DIR/lhde-session:N/layout-migration.log`). An unmigrated directory is not searched: its files reach the
programs only once moved, and `lhde-theme dirs` names it with the command to run until then.
Switching an existing setup: `make install PREFIX=~/.local`, then `lhde-import --migrate -n`, then `lhde-import --migrate`.

