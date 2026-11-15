# liblhde: the desktop-wide library

What every lhde component shares, prefix `lhde_`, plain C, no X (style probing through libwuxtheme):

| header | file | what |
| --- | --- | --- |
| `<lhde/data.h>` | `data.c` | the three trees and the one lookup over them (docs/LAYOUT.md "The three trees"): `~/.config/lhde`, the Windows tree's `index/` (`$LHDE_WINDOWS`, default `~/.local/share/lhde-windows`; `$LHDE_DATA` a flat directory instead), then the system Windows trees' `index/` (`$XDG_DATA_DIRS/lhde-windows`, behind the default tree only; `lhde_system_windows_dirs`), the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the tree's `share/`; lookup by kind (`lhde_data_find_kind`), which tree a file lies in (`lhde_data_origin`), the code directory (`<prefix>/lib/lhde`) |
| `<lhde/theme.h>` | `theme.c`, `style.c`, `current.c` | theme files and the resolver (base chains, absent vs `none`, defaults from the style, fallbacks, where every key came from), bare styles as themes, the current layer (`settings.ini [current]`, the unsaved changes: `lhde_current_*`, `lhde_theme_select`, `lhde_theme_save_as`, `lhde_theme_delete`), classic schemes (`styles/NAME.scheme`), the style and scheme lists, `lhde_theme_set_key` (`NAME-modified.theme`), the style probe |
| `<lhde/settings.h>` | `ini.c`, `settings.c` | line-keeping ini documents (and their trim, `lhde_trim`); the settings store `~/.config/lhde/settings.ini`; the user's config and state directories (`lhde_config_file`, `lhde_state_file`) |
| `<lhde/icon.h>` | `icon.c` | the icon of a window or program (docs/THEME-FORMAT.md section 9): `lhde_app_icon` (the window's own, the class map `icons/apps.ini`, the desktop file's `Icon=` through the XDG icon theme, the generic application icon), `lhde_icon_key_image` (an icon key through the icon set recipes and their fallbacks), `lhde_pe_has_resource` (whether a PE file has a resource: `lhde-theme explain`'s Start flag); icons read from PE resources, `.ico` and PNG files (libpng) |
| `<lhde/hw.h>` | `hw.c`, `hw_linux.c`, `hw_freebsd.c` | the hardware a desktop shows and sets (docs/APPLICATIONS.md "Hardware"): the battery, the backlight, the volume of the default output, the touchpad, with a back end per system (Linux: sysfs, wpctl / pactl / amixer; FreeBSD: sysctl, `/dev/backlight` and OSS mixer ioctls, their commands as fallbacks; both: xinput for the touchpad, which is the X server's), change-event descriptors (`pactl subscribe`, uevents, devd), `lhde_hw_describe` and `lhde_hw_fields` for bindings; `LHDE_HW_FAKE` / `LHDE_HW_SYSTEM` run either back end over recorded files |

Build: `build/lib/liblhde.a`, `build/lib/liblhde.so.1`, `build/lib/pkgconfig/lhde.pc` (`make liblhde`); `make install`
installs the headers under `include/lhde/`. `lhde-theme` (`lhde-theme.c`) is the command line: `list`, `resolve NAME [--why |
--get KEY | --shell]`, `current`, `set NAME`, `layer`, `layer-set KEY VALUE`, `layer-clear`, `save-as FILE [TITLE]`, `delete NAME`, `styles`, `schemes`,
`set-key NAME KEY VALUE`, `setting SEC KEY [VALUE]`, `style NAME`,
`dirs [--list | --windows | --user]` (the three trees with sizes), `explain [THEME]` (every component and its file:
`explain.c`, the program's own), `find REL | find KIND NAME`, `path VALUE`, `icon CLASS [INSTANCE] | icon --key KEY`. `tests/resolve.c` (`make check-liblhde`) covers the resolver, the store, the three trees and the icon lookup on fake files.
`lhde-hw` (`lhde-hw.c`) is the hardware layer's: the four records, `describe`, `-k WHAT` (a record as key=value lines),
`volume N|+N|-N|mute|unmute|toggle`, `backlight N|+N|-N`, `touchpad on|off|toggle`, `watch`; `tests/hw.c` (`make
check-liblhde`) runs both back ends over recorded trees and stub commands.

Users: luxwm (`lh_theme_apply`), luxshell (the Lua binding `lh.theme_*`, `lh.settings_*`, `lh.hw_*`), libluxnav (`data.c`, `ini.c`'s `lhde_trim`) and flxp (the data directories, the resolver, the store; flxp-taskbar's tray controls the hardware layer),
the session scripts (through `lhde-theme`).
