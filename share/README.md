# share: text, and lhde's own drawings (no Windows pixels; docs/LAYOUT.md "Asset policy")

Installed to `$PREFIX/share/lhde` (program files: docs/LAYOUT.md "The three trees"); an uninstalled build reads this directory
as the last data directory (liblhde, lhde/data.h). `~/.config/lhde` comes first, so a copy of a file there under the same
relative name overrides the one here; the Windows tree's `index/` (`~/.local/share/lhde-windows`) comes before it too.

- `themes/*.theme`: the shipped themes (docs/THEME-FORMAT.md sections 2 and 8); `*.theme.todo` are drafts nobody lists.
- `navbar/*.ini`: the navigation bar families (`vision.ini` = luxnav's layout, with frames/aerovision/nav.ini a symlink to it
  for flxp; `4051.ini` and `4074.ini` name flxp's own header).
- `icons/*.theme`: icon set recipes (flxp's res/icons format, `pe:shell32.dll#ID` from `resources/<build>`).
- `icons/flags/NAME-SIZE.png`: lhde's own Start button pictures (`lhde`, `linux`, `freebsd` at 16, 20, 24 and 32 px;
  `Shell.start_icon`, docs/THEME-FORMAT.md section 2b), drawn in code by `tools/start_icons.py`; run it again after changing a
  drawing (`make check` compares).
- `firefox/userChrome.css.in`: the template of `tools/lhde-firefox`'s userChrome.css (docs/USAGE.md "Firefox").
- `firefox/ff4/`: Firefox 4's theme pictures (tab, toolbar icons, close, new tab, reload and stop), Mozilla's under its
  MPL/GPL/LGPL tri-licence (`LICENSE` there, docs/LICENSING.md); `MANIFEST` names the release tag and each file's path and
  sha256. `lhde-firefox install` copies them into the profile.
- `sources/*.ini`: what tools/lhde-pack takes from each Windows build and what lhde-import derives from it.
- `frames/NAME/theme.ini`: luxwm's frame sets, the tuning keys; their pixels (or a cache) come from lhde-import.
- `applications/lhde-mimeapps.list`: lhde's default programs, installed to `$PREFIX/share/applications` (not `share/lhde`; an
  uninstalled build does not read it: docs/SETTINGS.md "Associations").
- `thumbnailers/lhde-*.thumbnailer`: freedesktop thumbnailer entries for document pages (PDF, XPS, PostScript, DjVu), installed
  to `$PREFIX/share/thumbnailers` (not `share/lhde`); flxp reads them after every other entry, uninstalled from here too
  (docs/USAGE.md, thumbnails).
- `xsessions/lhde.desktop.in`: the X session entry a display manager lists (`lhde-session xsession`), installed with its
  `@BINDIR@` filled in to `$PREFIX/share/xsessions/lhde.desktop` (docs/BUILDING.md "A whole-system install and a display
  manager").
- `packs/NAME/shell.lua`: luxshell's packs; their images are packs/<build>/... of the Windows tree
  (`generated/packs/<build>`).
