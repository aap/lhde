# liblhde: the desktop-wide library

What every lhde component shares, prefix `lhde_`, plain C, no X (style probing through libwuxtheme):

| header | file | what |
| --- | --- | --- |
| `<lhde/data.h>` | `data.c` | the three trees and the one lookup over them (docs/LAYOUT.md "The three trees"): `~/.config/lhde`, the Windows tree's `index/` (`$LHDE_WINDOWS`, default `~/.local/share/lhde-windows`; `$LHDE_DATA` a flat directory instead), the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the tree's `share/`; lookup by kind (`lhde_data_find_kind`), which tree a file lies in (`lhde_data_origin`), the code directory (`<prefix>/lib/lhde`) |
| `<lhde/theme.h>` | `theme.c`, `style.c`, `current.c` | theme files and the resolver (base chains, absent vs `none`, defaults from the style, fallbacks, where every key came from), bare styles as themes, the current layer (`settings.ini [current]`, the unsaved changes: `lhde_current_*`, `lhde_theme_select`, `lhde_theme_save_as`, `lhde_theme_delete`), classic schemes (`styles/NAME.scheme`), the style and scheme lists, `lhde_theme_set_key` (`NAME-modified.theme`), the style probe |
| `<lhde/settings.h>` | `ini.c`, `settings.c` | line-keeping ini documents (and their trim, `lhde_trim`); the settings store `~/.config/lhde/settings.ini`; the user's config and state directories (`lhde_config_file`, `lhde_state_file`) |

Build: `build/lib/liblhde.a`, `build/lib/liblhde.so.1`, `build/lib/pkgconfig/lhde.pc` (`make liblhde`); `make install`
installs the headers under `include/lhde/`. `lhde-theme` (`lhde-theme.c`) is the command line: `list`, `resolve NAME [--why |
--get KEY | --shell]`, `current`, `set NAME`, `layer`, `layer-set KEY VALUE`, `layer-clear`, `save-as FILE [TITLE]`, `delete NAME`, `styles`, `schemes`,
`set-key NAME KEY VALUE`, `setting SEC KEY [VALUE]`, `style NAME`,
`dirs [--list | --windows | --user]` (the three trees with sizes), `explain [THEME]` (every component and its file:
`explain.c`, the program's own), `find REL | find KIND NAME`, `path VALUE`. `tests/resolve.c` (`make check-liblhde`) covers the resolver, the store and the three trees on fake files.

Users: luxwm (`lh_theme_apply`), luxshell (the Lua binding `lh.theme_*`, `lh.settings_*`), libluxnav (`data.c`, `ini.c`'s `lhde_trim`) and flxp (the data directories, the resolver, the store),
the session scripts (through `lhde-theme`).
