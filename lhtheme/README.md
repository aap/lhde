# lhtheme: the theme resolver (step 4); for now the data directories

`lhde_data.c` / `lhde_data.h`: where the lux programs find things (docs/LAYOUT.md "Asset policy"): `$LHDE_DATA` or
`~/.local/share/lhde`, the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the source tree's `data/`; frame sets and packs split
between text (shipped) and pixels (imported); the registry's resolved copy for readers that resolve against its directory.
Compiled into luxwm, libluxnav (and so navdemo and flxp), luxshell and glassdemo. Step 4 (THEME-FORMAT discussion) grows this
into liblhtheme, the one resolver of theme files.
