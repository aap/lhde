# lhde's files on disk: the three trees and the asset policy

What lhde reads at run time, where it lives, and who is allowed to write it. The merge plan this layout came from, and the
"as built" reports of the merge steps, are in docs/history/LAYOUT-draft.md.

## Asset policy

Nothing Microsoft-derived is in the repository: no `.msstyles`, DLLs, fonts, wallpapers, extracted or rendered bitmaps, no
Microsoft concept art and no screenshots or reference renders of the real Windows builds. The repository holds code,
configuration and recipes (numbers, presets, scripts that derive art), screenshots of lhde itself in `screenshots/`, and the
few pictures that are lhde's own work: `share/icons/flags/*.png`, the Start button pictures `tools/start_icons.py` draws from
circles and polygons in its source (`make check` draws them again and compares). No third-party logo or mascot is drawn there
either; the Windows flag itself is only ever the user's imported one. Windows
art is read at run time from the user's own files, imported once into the Windows tree below by `lhde-import` (from a resource
pack made by `lhde-pack`, an ISO, a WIM or a Windows directory; docs/USAGE.md "Getting the Windows resources"). Data extracted
from Windows files is derived there too, never committed: the aurora scenes' numbers come from the user's jade.msstyles and
WCPClient.dll at import (`generated/aurora/`) and reach `build/examples/aurora_data.h` at build time. The icons in
`flxp/res/icons` are Wine's (LGPL), the `.res` files in `wux/rc` are built from Wine's `.rc` sources, and Wine's vendored code
stays in `wux/vendor/wine` with its MANIFEST and patches, md4c (mupf's Markdown) in `apps/mupf/vendor/md4c` the same way, and Firefox 4's theme pictures (Mozilla's, for lhde-firefox) in `share/firefox/ff4` with their MANIFEST and licence (docs/LICENSING.md). Tests compare against checksums of local renders, never against
reference images. Resource packs built in the tree (`lhde-resources-*`) are gitignored.

## The three trees (on disk)

Everything lhde reads at run time lives in one of three trees, and nothing is in two of them. Who writes a tree is the only
thing that may change it:

```
PROGRAM FILES                        make install; never edited by hand (a later make install overwrites them)
$PREFIX/share/lhde/                  shipped data, our text and our own drawings (DATADIR)
  themes/NAME.theme                    the shipped themes
  navbar/NAME.ini                      navigation bar layouts (4051 4074 vision)
  fonts/fontlink.ini                   font linking's table (Windows' FontLink\SystemLink): the faces that draw what a font lacks
  icons/NAME.theme                     icon recipes (pe:shell32.dll#ID from resources/<build>, file:PACK/PATH from an icon pack)
  icons/flags/NAME-SIZE.png            lhde's own Start button pictures (lhde linux freebsd at 16 20 24 32: Shell.start_icon),
                                       drawn in code by tools/start_icons.py
  sources/ID.ini                       what lhde-pack takes from each Windows build, the recipes lhde-import runs
  frames/SET/theme.ini (+ nav.ini)     frame-set tuning; the pixels are generated, below
  packs/PACK/shell.lua                 luxshell's packs
  res/icons/                           flxp's own (Wine's) icons, luxpad.ico (ReactOS's notepad icon)
  applications/roles.ini               the default programs' roles (docs/SETTINGS.md "Default programs")
$PREFIX/lib/lhde/                    code that is not a binary (PKGLIBDIR)
  luxshell/                            luxshell's Lua (runtime.lua, tiles/, ...)
  session/                             lhde-session's helpers: lhde-paths.sh, lhwall.sh, lab.sh, theme-*.sh, picom configs
  tools/                               lhde-import's Python: lhdeimport.py, lhdetree.py, lhdeschemes.py, recipe builders;
                                       lhde-mime's lhdemime.py
$PREFIX/share/applications/          flxp.desktop, luxpad.desktop, luxview.desktop, lhde-mimeapps.list (lhde's default programs, read when
                                     XDG_CURRENT_DESKTOP names lhde: docs/SETTINGS.md "Associations"), mimeinfo.cache
                                     (update-desktop-database, when installed)
$PREFIX/share/thumbnailers/          lhde-pdf, lhde-xps, lhde-postscript, lhde-djvu .thumbnailer: freedesktop thumbnailer
                                     entries for document pages (THUMBNAILERDIR; any program reads them, flxp after the
                                     system's own: docs/USAGE.md, thumbnails)
$PREFIX/bin/                         luxwm luxshell luxshot luxattr luxpad luxview flxp* lhde-theme lhde-session lhde-import lhde-pack lhde-schemes lhde-mime ut-dump

WINDOWS FILES                        lhde-import; $LHDE_WINDOWS, default ${XDG_DATA_HOME:-~/.local/share}/lhde-windows
~/.local/share/lhde-windows/
  <source>/                            one build's raw files, exactly as the resource pack has them (2k 4015 ... 5112 xp reactos)
    system32/                            shell32 imageres explorer browseui comctl32 themeui [shellstyle uDWM shdocvw]
    themes/                              the build's Resources/Themes: Jade/jade.msstyles, Luna/Shell/<Scheme>/shellstyle.dll, ...
    fonts/  wallpapers/  setup/          (setup/hivedef.inf: the xp and 2k classic schemes)
    source.ini                           the rules it was taken with
  local/                               files that come from no source: themes/ (lhde-import --style: realaero, zune, ...;
                                       --styles: a downloaded folder's), schemes/ (lhde-schemes), icons/PACK/ (lhde-import
                                       --icons: a free icon pack as .ico), wallpapers/ (a --styles style's .theme wallpaper)
                                       resources/ fonts/ (copied by hand, the workspace); a resource pack carries
                                       it as its local/ (below)
  generated/                           everything derived; deleting it is always safe (lhde-import --regenerate)
    frames/SET/                          PNGs + manifest.ini (recipe, inputs with sha256); theme.ini of a set we do not ship
    packs/<build>/                       imageres/ID.png, shell32/SIZE/ID.png, manifest-*.ini
    schemes/NAME.scheme                  from a source's HIVEDEF.INF
    aurora/SCENE-<source>.json           the aurora scenes as layer lists (previewpane, desktop), manifest-*.ini; make examples
                                         turns them into aurorademo's tables
  index/                               relative symlinks under the names programs ask for (lhde-import --reindex):
    styles/NAME.msstyles, styles/NAME/     -> ../../<source>/themes/<Style>/... or ../../local/themes/...
    styles/NAME.scheme                     -> generated/schemes or local/schemes
    resources/<source> fonts/<source> wallpapers/<source>   -> ../../<source>/system32, fonts, wallpapers
    icons/PACK                             -> ../../local/icons/PACK
    frames -> ../generated/frames,  packs -> ../generated/packs,  aurora -> ../generated/aurora
  import.log
$XDG_DATA_DIRS/lhde-windows/         a system Windows tree (/usr/local/share/lhde-windows): the same layout, made by root with
                                     LHDE_WINDOWS=<it> lhde-import (or make install-resources-* LHDE_WINDOWS=<it>); read-only for
                                     users, read after the user's own tree

A RESOURCE PACK                      lhde-pack; lhde-import --pack makes the Windows tree from it (docs/USAGE.md)
lhde-resources-<date>[-essentials]/
  <source>/                            the raw files as the Windows tree's <source>/ holds them, source.ini
  local/                               the Windows tree's local/ (themes/ schemes/ resources/ fonts/ wallpapers/ icons/): all
                                       of it in a museum pack, what the themes name in an essentials pack
  MANIFEST                             sha256, size, pack path, source (`local` for local/), in-image path (local:<path>,
                                       tree:<path> for a pack made --from-tree); # profile: and # themes: header lines

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
  flxp-view-sizes                      the size each of the Explorer's Longhorn views had last
  flxp-columns                         the Details columns chosen, their order and widths
  flxp-mfu, flxp-run-history           the start panel's launch counts, the Run box's history
  catalogue/NAME.db, NAME.thumbs/      lhde-cat's catalogue of the root NAME (docs/CATALOGUE.md); under a root whose
                                       authority is the catalogue its uncommitted edits and journal are the only copy
```

**Lookup.** A program asks for a file by kind and name, i.e. by its relative name (`styles/jade4074.msstyles`,
`frames/jade4074/close.png`, `resources/4074`, `wallpapers/4074/Bliss.jpg`), and liblhde (`liblhde/data.c`, `<lhde/data.h>`)
answers from the first of: 1. `~/.config/lhde`; 2. `~/.local/share/lhde-windows/index` (or `$LHDE_DATA`, a flat directory:
tests, scratch imports), then the system Windows trees' `index/` (`$XDG_DATA_DIRS/lhde-windows`;
only when neither `$LHDE_WINDOWS` nor `$LHDE_DATA` names the one tree), so the user's copy of a file wins and a file only the
system tree has is found there, a frame set's or pack's generated pixels in the first tree that has them; 3. `$PREFIX/share/lhde` of the running program, then `$XDG_DATA_DIRS/lhde`; 4. the source tree's
`share/` when the program runs from a build tree. Code is found by `lhde_component_dir`: `<tree>/luxshell` or `<prefix>/lib/lhde/luxshell`. Scripts use the
same lookup through `lhde-theme find REL` / `find KIND NAME` / `dirs --list` (session/lhde-paths.sh); lhde-import is the only
writer of the Windows trees (of the user's as the user, of a system tree as root with `LHDE_WINDOWS`; `--regenerate`
remakes the one tree it writes) and the only code that knows their inside (`tools/lhdetree.py`). A frame set or pack is split by
design: its text is shipped, its pixels are generated; `lhde_data_find_in` joins them.

**Where does this pixel come from.** `lhde-theme explain [THEME]` prints one row per visible component (visual style and
scheme, glass and classic frame set, taskbar, shell pack and its imageres, start menu, navbar and its art, icon recipe and its
DLLs, wallpaper, fonts, resources) with the setting, `ok` / `off` / `missing -> fallback X` and the file with its tree;
`lhde-theme dirs` shows the trees and their sizes (docs/USAGE.md "When something looks wrong").
