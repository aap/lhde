# liblhde: the desktop-wide library (docs/THEME-FORMAT.md section 10.3)

What every lhde component shares, prefix `lhde_`, plain C, no X (style probing through libwuxtheme):

| header | file | what |
| --- | --- | --- |
| `<lhde/data.h>` | `data.c` | the data directories (`$LHDE_DATA` or `~/.local/share/lhde`, the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the tree's `data/`), lookups that also find a manifest-first `cache/` |
| `<lhde/theme.h>` | `theme.c`, `style.c` | theme files and the resolver (base chains, absent vs `none`, defaults from the style, fallbacks, where every key came from), bare styles as themes, `lhde_theme_set_key` (`NAME-modified.theme`), the style probe |
| `<lhde/settings.h>` | `ini.c`, `settings.c` | line-keeping ini documents; the settings store `~/.config/lhde/settings.ini`; the one-time migration from `~/.config/longhorn` + `~/.config/lhshell` |
| `<lhde/data.h>` (compat part) | `compat.c` | the generated old-format `themes.ini` + `wallpapers.ini` for flxp until it reads theme files (`_LH_THEMES_INI`) |

Build: `build/lib/liblhde.a`, `build/lib/liblhde.so.1`, `build/lib/pkgconfig/lhde.pc` (`make liblhde`); `make install`
installs the headers under `include/lhde/`. `lhde-theme` (`lhde-theme.c`) is the command line: `list`, `resolve NAME [--why |
--get KEY | --shell]`, `current`, `set NAME`, `set-key NAME KEY VALUE`, `setting SEC KEY [VALUE]`, `style NAME`, `migrate`,
`registry`, `dirs`. `tests/resolve.c` (`make check-liblhde`) covers the resolver, the store and the migration on fake files.
`lhde_data.h` is the old header name (it includes `lhde/data.h`).

Users: luxwm (`lh_theme_apply`), luxshell (the Lua binding `lh.theme_*`, `lh.settings_*`), libluxnav and flxp (`data.c` so far),
the session scripts (through `lhde-theme`).
