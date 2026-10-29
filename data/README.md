# data: text only (no pixels; docs/LAYOUT.md "Asset policy")

Installed to `$PREFIX/share/lhde` (program files: docs/LAYOUT.md "The three trees"); an uninstalled build reads this directory
as the last data directory (liblhde, lhde/data.h). `~/.config/lhde` comes first, so a copy of a file there under the same
relative name overrides the one here; the Windows tree's `index/` (`~/.local/share/lhde-windows`) comes before it too.

- `themes/*.theme`: the shipped themes (docs/THEME-FORMAT.md sections 2 and 8); `*.theme.todo` are drafts nobody lists.
- `navbar/*.ini`: the navigation bar families (`vision.ini` = luxnav's layout, was frames/aerovision/nav.ini, which stays as a
  symlink for flxp; `4051.ini` and `4074.ini` name flxp's own header).
- `icons/*.theme`: icon set recipes (flxp's res/icons format, `pe:shell32.dll#ID` from `resources/<build>`).
- `sources/*.ini`: what tools/lhde-pack takes from each Windows build and what lhde-import derives from it.
- `frames/NAME/theme.ini`: luxwm's frame sets, the tuning keys; their pixels (or a cache) come from lhde-import.
- `packs/NAME/shell.lua`: luxshell's packs; their images are packs/<build>/... of the Windows tree
  (`generated/packs/<build>`).
