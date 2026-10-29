# lhde: how it fits together

lhde is a Windows XP / Longhorn-era desktop for X11. Two families of code meet in it: the **wux** side (a win32 UI layer with
Wine's controls, the uxtheme renderer and the Explorer) and the **lux** side (the window manager, the Longhorn navigation bar
and the Avalon shell in C and Lua). A small desktop-wide library, **liblhde**, is what both sides share: where files are, what
the current theme resolves to, and the settings store. The art is never in the repository; it is read from the user's own
Windows files (docs/LAYOUT.md).

## Components

| directory | library / programs | what it is |
| --- | --- | --- |
| `wux/` | `libwux.a` (static, linked whole) | the win32 UI layer: `gdi/` (DCs, bitmaps, fonts over FreeType), `sys/` (X11 windows, input, clipboard, DnD), `win/` (the window and message machinery), `w32/` (the win32 shim: kernel, user, registry, resources), `vendor/wine/` (comctl32, user32, uxtheme, comdlg32 from Wine, LGPL, with patches), `rc/` (Wine's resources) |
| `wuxtheme/` | `libwuxtheme.{a,so}`, `ut-dump` | the uxtheme renderer: reads and draws `.msstyles` (v3 XP and v4 Longhorn/Vista) through a plain C API `ut_*` (`<wuxtheme/ut.h>`), window-system independent; used by flxp through Wine's uxtheme and directly by luxwm, luxnav, luxshell and liblhde's style probe |
| `flxp/` | `flxp`, `flxp-desktop`, `flxp-taskbar`, `flxp-settings`, `flxp-filedialog`, `flxp-portal` | the Explorer (`explorer/`: the browser, the faithful 4051/4074 headers, task pane, views), the shell layer (`shell/`: namespace, file operations, icons, thumbnails, the liblhde adapter), the desktop (wallpaper and icons), the classic taskbar with XP's start panel and the classic start menu, Display Properties, the Open/Save dialog and its xdg-desktop-portal backend |
| `luxnav/` | `libluxnav.{a,so}` (`lhnav_*`) | the Longhorn navigation bar as the concept art had it (the "vision" header): layout, painting, hit testing; art from the style through wuxtheme; linked into flxp and `examples/navdemo` |
| `luxwm/` | `luxwm`, `luxwmc` | the window manager: non-reparenting, EWMH, frames in three modes (DWM glass from the style's CompositedWindow streams or a generated frame set; non-composited msstyles frames through wuxtheme; Windows Classic from a scheme's colours and metrics), the XP window menu, animations, the extended-frame protocol for the Explorer's tall caption (docs/CLIENT-PROTOCOL.md), a frame regression (`tests/regress.sh`) |
| `luxshell/` | `luxshell` (C core + Lua) | the Avalon reinterpretation: taskbar, sidebar, tiles (clock, notifications, power), tray, toasts, context menus, settings flyout and a Lua start menu; C scene nodes with the behaviour in `*.lua`; packs (`data/packs/*/shell.lua`) name the imageres art it draws |
| `liblhde/` | `liblhde.{a,so}`, `lhde-theme` | `<lhde/data.h>` the three trees and every lookup by relative name; `<lhde/theme.h>` the theme resolver (theme files, inheritance, the current layer, style probe, defaults, fallbacks, "where did this come from"); `<lhde/settings.h>` the settings store with change notification and the migration from the pre-lhde paths; the compatibility registry (`themes.ini`) flxp no longer reads |
| `session/` | `lhde-session`, `lab.sh`, `lhwall.sh`, `theme-*.sh`, picom configs | starting and controlling a session, and the headless bench |
| `tools/` | `lhde-pack`, `lhde-import`, `lhde-schemes`, Python modules, recipe builders | the resource pack from ISOs, the import into the Windows tree, the recipes that derive frame sets, pack images and start panels from the imported files, the layout migration |
| `data/` | text only | the shipped themes, navbar layouts, icon recipes, import sources, frame-set tuning, shell packs |
| `tests/`, `examples/` | | toolkit and w32 tests, headless Xvfb scripts, `check.sh`; `extendframe`, `navdemo`, `glassdemo` as reference clients |

Builds: one top-level Makefile (`make`, `make check`, `make install`; README.md). `make check` runs the wux tests, the headless
scripts, ut-dump over every imported style, then the lux side: liblhde's resolver tests, luxshell's Lua parse, and luxwm's
frame regression against a local baseline.

## A running session

`lhde-session start [THEME]` (a real X server on `:1`, `--nested` in Xephyr, `--attach` to a running server) runs, in this
order: the settings and layout migrations if needed, the root wallpaper once, **luxwm** in a restart loop, **picom** when the
theme's frames want a compositor, **luxshell** (or nothing, for `taskbar = classic`, where luxshell itself starts
`flxp-taskbar`), **flxp-desktop**, and `flxp-taskbar --start-menu-only` as the start-menu provider when the Avalon taskbar is up
and the theme says `start = flxp`. `session/lab.sh` does the same on an Xvfb display with the build-tree binaries, for
screenshots and scripted tests (`lab.sh start; lab.sh shell; lab.sh theme NAME; lab.sh shot FILE`).

Everything a program needs is looked up by a relative name through liblhde (docs/LAYOUT.md "The three trees"):
`~/.config/lhde` first, then the Windows tree's `index/`, then the install's `share/lhde` (the source tree's `data/` when run
from a build tree). `lhde-theme explain [THEME]` prints every visible component of a theme, the file behind it, its tree and
whether it fell back; this is the first thing to run when something looks wrong.

## How the programs talk

There is no daemon: the X root window carries the session state as properties, and liblhde reads and writes the files behind
them. The complete per-property reference is docs/CLIENT-PROTOCOL.md (luxwm's client side) and docs/SETTINGS.md (the store);
the ones that matter for orientation:

| property | set by | meaning |
| --- | --- | --- |
| `_LH_THEME` | the session, luxshell's flyout, Display Properties, `lhde-theme set` | the session theme's name; luxwm publishes the setting's value when it starts first, after that the root wins and everyone follows it live |
| `_LH_THEME_RESOLVED` | luxwm or luxshell, whoever resolved first | the resolved record (`section.key=value` lines, `theme=`, `serial=`); a client with a stale or missing record resolves itself through liblhde |
| `_LH_SETTINGS_SERIAL`, `_LH_SETTINGS_CHANGED` | whoever wrote `settings.ini` | the store changed (a hint names what); every program re-reads what it cares about, and the theme is resolved again when the current layer changed |
| `_LHWM_COMPOSITED`, `_LHWM_FRAME_METRICS`, `_LHWM_THEME` | luxwm | glass or not, the frame geometry and colours a client lays its header out against, the frame set in use |
| `_LHWM_EXTEND_FRAME`, `_LHWM_FRAME_FLAGS`, `_LHWM_HEADER_PIXMAP`, `_LHWM_CAPTION_RECT` | the client / luxwm | the extended-frame contract: the client asks for the plate to continue behind its header; luxwm paints it, or hands over a pixmap when not composited |
| `_LHSHELL_COMMAND`, `_LHSHELL_START`, `_LH_STARTMENU_OPEN`, `_LH_STARTMENU_S<n>` | luxshell, luxwm, flxp-taskbar | commands to the shell (taskbar kind, ...), the Start key, whether a start menu is open, who owns the start menu |
| `_LH_THEMES_INI` | luxwm, luxshell | the path of the generated compatibility registry; nothing reads it any more and it is slated for removal |

Settings a user made live in `~/.config/lhde/settings.ini` (the selected theme, its unsaved current layer, per-user keys such as
the sidebar mode and taskbar autohide); theme files the user saved in `~/.config/lhde/themes/`. flxp still keeps its own
preferences in `~/.config/flxp/flxp.ini`, luxshell its runtime state in `~/.config/lhshell/`; consolidating those is pending
(docs/SETTINGS.md).

## Where things are duplicated, on purpose or not yet resolved

- **Start menu**: flxp has XP's start panel and the classic popup menu (Wine controls); luxshell has a Lua start panel and a
  provider protocol that forwards to flxp's. Every shipped theme uses flxp's.
- **Navigation header**: flxp's own `explorer/header.cpp` draws the faithful 4051 and 4074 headers; luxnav draws the concept-art
  "vision" header and is meant to take over 5048 and 5112. Which one a theme gets is `Explorer.navbar`.
- **Frame painting**: luxwm paints glass and msstyles frames through wuxtheme and has its own Windows Classic painter; Wine's
  user32 in wux draws the same classic non-client art for flxp's own windows. The plan is for wuxtheme to export the three
  classic drawing helpers so there is one implementation.
- **Theme resolution**: one implementation, liblhde; flxp's `shell/lhtheme.cpp` only maps the record onto flxp's older keys.

## Where to look first

- A pixel is wrong: `lhde-theme explain THEME`, then `lhde-theme resolve THEME --why`, then the component's doc.
- A frame is wrong: docs/LUXWM.md, `luxwm/tests/regress.sh` (renders every frame set offline).
- The Explorer or a dialog is wrong: `flxp --info`, docs/history/flxp-NOTES.md for how a piece was built, `tests/check.sh`.
- The shell is wrong: `luxshell`'s log in the session directory (`$XDG_RUNTIME_DIR/lhde-session:N/`), docs/SHELL-DESIGN.md.
- The import is wrong: `lhde-import --list`, `lhde-theme dirs`, the manifests in `lhde-windows/generated/`.
