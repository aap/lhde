# data: text only (no pixels; docs/LAYOUT.md "Asset policy")

Installed to `$PREFIX/share/lhde`; an uninstalled build reads this directory as the last data directory
(lhtheme/lhde_data.h). The user's data directory (`~/.local/share/lhde`, `LHDE_DATA`) comes first, so a file there overrides
the one here.

- `themes.ini`: the theme registry (docs/THEMES.md), values relative to the data directories (`styles/jade4074.msstyles`,
  `frames/jade4074`, `packs/4074-aero`, `fonts/4074`, `resources/4074`). luxwm and luxshell publish its resolved copy (absolute
  paths) as `_LH_THEMES_INI`. Transitional: step 4 replaces it with theme files.
- `wallpapers.ini`: each theme's wallpaper, relative to `wallpapers/` of the data directories.
- `frames/NAME/theme.ini` (+ `nav.ini` for aerovision): luxwm's frame sets, the tuning keys; their PNGs come from lhde-import.
  Names: aero-patch, jade4074, aero5048, aero5112, aerovision, jade4074-classic, slate4074-classic, slate4051-classic,
  luna4051-blue, luna4051-olive, luna4051-silver (the x11 directories aero, jade, luna-* renamed by build).
- `packs/NAME/shell.lua`, `packs/startpanels.lua`: luxshell's packs (4074-aero, 4074-slate, 4051-slate, 5112-aero,
  4074-aerovision; were lhshell/themes/aero, slate, slate4051, aero5112, aerovision); their images are packs/<build>/... of
  the user's data directory.
