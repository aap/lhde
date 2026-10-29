# lhde

The Longhorn desktop environment: a Windows XP / Longhorn-era desktop for X11,
built on a toolkit that feels like win32 without being Windows. One repository,
one build, for the parts that were separate projects:

| directory   | what                                                                 | was |
|-------------|----------------------------------------------------------------------|-----|
| `wux/`      | the win32 UI layer: `gdi/` `sys/` `win/` `res/` (native toolkit), `w32/` (the win32 shim), `vendor/wine/` (Wine's comctl32/user32/uxtheme, LGPL, with MANIFEST and patches), `rc/` (Wine's comctl32/comdlg32 resources) | flxp `src/{gdi,sys,win,res,w32}`, `vendor/wine` |
| `wuxtheme/` | libwuxtheme, the uxtheme library: `.msstyles` read and drawn without Windows (`<wuxtheme/ut.h>`, C API `ut_*`), and `ut-dump` | flxp `src/uxtheme` (libuxtheme) |
| `flxp/`     | the explorer and shell: `shell/` `explorer/` `desktop/` `taskbar/` `filedlg/` `settings/`, `rc/` (its dialogs), `res/` (icon themes, .desktop, portal files, tools) | flxp |
| `tests/`    | `toolkit/` and `w32/` test programs, `headless/` Xvfb scripts, `check.sh`, `demo.sh` | flxp `src/test`, `src/w32test` |
| `luxwm/`    | the window manager: Longhorn frames (DWM glass frame sets, classic frames from the style through wuxtheme), EWMH, animations; `luxwmc`; `tests/` (frametest, regress) | x11 `lhwm/` |
| `luxnav/`   | libluxnav, the Longhorn navigation bar (the vision header; C API `lhnav_*`, `lhnav.h`), linked into flxp and navdemo | x11 `lhnav/` |
| `luxshell/` | the Avalon bars, sidebar, tiles, toasts, start menu: C core + Lua (`*.lua`, `tiles/`), `shellpack*.sh` | x11 `lhshell/` (without `assets/`) |
| `liblhde/`  | liblhde (`<lhde/data.h>`, `<lhde/theme.h>`, `<lhde/settings.h>`): the three trees and every lookup in them, the theme resolver, the settings store and its migration, the compatibility registry for flxp; `lhde-theme` (with `explain` and `dirs`); `tests/resolve.c` | new (was `lhtheme/`) |
| `session/`  | `lhde-session` (start, theme, taskbar, shell, picom, desktop, stop), `lab.sh` (the headless bench), `lhwall.sh`, theme-reload/-watch, picom configs | x11 `xinitrc.lab`, `second-display.sh`, `lab.sh`, ... |
| `examples/` | extendframe, navdemo, glassdemo | x11 `examples/`, `glassdemo.c` |
| `tools/`    | `lhde-pack` (a resource pack of raw Windows files from ISOs), `lhde-import` (a pack, ISO, WIM or Windows directory into the Windows tree, the recipes, `--migrate`), `lhdeimport.py`, `lhdetree.py` (the Windows tree, its index, the migration), `wim0108.py`, recipe builders (`msstyles2theme.py`, `build_aerovision.py`, `build_aero5112.py`) | x11 `lhwm/tools/` |
| `data/`     | text only: `themes/*.theme` (the shipped themes), `navbar/*.ini`, `icons/*.theme`, `sources/*.ini` (what lhde-pack takes from each Windows build), `frames/*/theme.ini`, `packs/*/shell.lua` | x11 `themes.ini`, `wallpapers.ini`, `lhwm/themes/*/*.ini`, `lhshell/themes/` |
| `docs/`     | `ARCHITECTURE.md` (the map), `LAYOUT.md` (asset policy, the three trees), `THEME-FORMAT.md`, `SETTINGS.md`, `CLIENT-PROTOCOL.md`, `USAGE.md`, `LUXWM.md`, `NAVBAR.md`, `SHELL-DESIGN.md`, `MSSTYLES-FORMAT.md`, `ANNOTATIONS.md`; `docs/README.md` lists them; `history/` holds the journals (flxp-NOTES, THEMES, the merge plan, the theme-format status) | |

No Microsoft files are in this repository, and none may be added (docs/LAYOUT.md,
"Asset policy"): no `.msstyles`, DLLs, fonts, wallpapers, extracted bitmaps or
screenshots of them. The icons in `flxp/res/icons` are Wine's (LGPL), the `.res`
files in `wux/rc` are built from Wine's `.rc` sources, `wux/w32/stdres.res` from
Wine's user32 resources (`wux/w32/mkstdres.py WINE-SRC`). Windows art is read at
run time from the user's own files (settings below).

## Building

Needs a C++17 compiler and pkg-config with x11, xext, freetype2, fontconfig,
libpng, libjpeg (libwebp/libwebpdemux optional), dbus-1 (flxp-portal, luxshell);
for the lux side also xfixes, xinerama, xft, cairo, cairo-xlib, pangocairo and lua5.4 (system Lua 5.4);
`x86_64-w64-mingw32-windres` only to rebuild a changed `.rc`. At run time the session
uses picom, feh and xprop (xdotool, ImageMagick's import for the lab).

    make -j                      libs, programs and tests into build/
    make check                   tests/check.sh (below)
    make install PREFIX=/usr/local [DESTDIR=]
    make uninstall PREFIX=...
    make clean

What `make` builds:

    build/lib/libwux.a                   wux (static; programs link it whole)
    build/lib/libwuxtheme.a              wuxtheme, linked into the flxp programs
    build/lib/libwuxtheme.so.1 (+ .so)   wuxtheme for other programs (exports ut_* only)
    build/lib/pkgconfig/wuxtheme.pc      pkg-config for the build tree (PKG_CONFIG_PATH=$PWD/build/lib/pkgconfig)
    build/lib/libluxnav.a, libluxnav.so.1 (+ .so), pkgconfig/luxnav.pc   luxnav (flxp links the .a)
    build/bin/flxp flxp-desktop flxp-taskbar flxp-settings flxp-filedialog flxp-portal ut-dump
    build/bin/luxwm luxwmc luxshell
    build/examples/extendframe navdemo glassdemo
    build/tests/..., build/tests/w32/... test programs and demos (tests/demo.sh runs some)
    build/luxwm/tests/frametest menutest desktopwin dockorder strutdock, build/luxshell/tests/trayicon

The programs run uninstalled from `build/bin` (they find `flxp/res`, `luxshell/` and `data/` in the source
tree). Per component: `make wux`, `make wuxtheme`, `make flxp`, `make tests`, `make luxnav`, `make luxwm`,
`make luxshell`, `make examples`.

`make install` installs program files only (docs/LAYOUT.md "The three trees"): `bin/` (the six flxp programs, ut-dump,
luxwm, luxwmc, luxshell, lhde-theme, lhde-session, lhde-import, lhde-pack, lhde-schemes, and the compatibility symlinks
lhwm, lhwmc, lhshell), `lib/` (`libwuxtheme`, `libluxnav`, `liblhde` `.so.1` + `.so`, `liblhde.a`, `pkgconfig/`),
`lib/lhde/` (`PKGLIBDIR`: the code that is not a binary: `luxshell/` the Lua, `session/` lhde-session's helpers and lab.sh,
`tools/` lhde-import's Python), `share/lhde/` (`DATADIR`: our shipped text, `themes/ navbar/ icons/ sources/ frames/*/theme.ini
packs/*/shell.lua packs/startpanels.lua`, flxp's `res/icons`, and `.lhde-install`, the list of what it put there), `include/`,
the man pages luxwm.1 and luxwmc.1 (`MANDIR`), `share/applications/flxp.desktop`, the portal backend's `flxp.portal` (in
`PORTALDIR`, default `$PREFIX/share/xdg-desktop-portal/portals`; xdg-desktop-portal reads its own datadir, so set
`PORTALDIR=/usr/share/xdg-desktop-portal/portals` for a system install) and its D-Bus service file (`DBUSSERVICEDIR`, default
`$PREFIX/share/dbus-1/services`). Also settable: `BINDIR LIBDIR INCLUDEDIR DATADIR PKGLIBDIR APPDIR PKGCONFIGDIR`.
`DATADIR` is compiled into flxp; `$PREFIX/share/flxp` (flxp's old one) is still read as a fallback, `FLXP_DATA` overrides.
No Windows file and no user setting is ever installed: those are the two other trees below.

### luxnav (the explorer header's vision mode)

luxnav is in the tree (luxnav/, `make luxnav`); flxp links `build/lib/libluxnav.a` and always has the vision
header (the step-2 `LUXNAV_DIR` hook and build/compat are gone).

### make check

`tests/check.sh` runs what needs no Microsoft files: kerneltest (the shim's
kernel/CRT/registry/resource/usp10 parts), gditest, fileops_test, and the
headless scripts under Xvfb (clipboard between two explorers, drag and drop
incl. GTK, the file dialog in both looks) on displays :71-:73
(`LHDE_TEST_DISPLAYS="A B C"` for others), each with a scratch HOME. Skipped when
missing: the headless tests without Xvfb/xdotool (clipboard also xclip), ut-dump
without imported visual styles (`lhde-theme find styles`: the Windows tree's `index/styles`; `LHDE_STYLES` overrides).
Then the lux side (`make check-lux`): luxshell builds and its Lua parses (`luac -p`), and luxwm's frame
regression (`luxwm/tests/regress.sh`): every frame set rendered offline and compared by SHA-256 with a
baseline of this machine (`build/luxwm/regress/baseline.sha256`, written by the first run;
`--baseline-from-x11 DIR` takes it from the x11 lab's build); skipped without imported styles.

## The three trees, lhde-import and lhde-theme explain

lhde's files live in three places that never mix (docs/LAYOUT.md "The three trees"):

    $PREFIX/share/lhde/        program files: our shipped text (make install; never edited)
    $PREFIX/lib/lhde/          program files: luxshell's Lua, the session scripts, lhde-import's Python
    ~/.local/share/lhde-windows/   the user's Windows files (lhde-import; $LHDE_WINDOWS): <build>/{system32,themes,fonts,
                               wallpapers}, local/ (dropped in), generated/ (derived PNGs: delete freely), index/ (symlinks)
    ~/.config/lhde/            the user's files: settings.ini, themes/, and any shipped file overridden under its own name

The art comes from the user's own Windows files, imported once (docs/USAGE.md "Getting the Windows resources"):

    tools/lhde-pack --isos DIR --out PACKS                  a resource pack (raw files) from the ISOs in DIR
    tools/lhde-import --pack PACKS/lhde-resources-DATE.tar.xz   the pack into the Windows tree, recipes run
    tools/lhde-import --iso FILE | --wim FILE | --dir WINDOWS   the same from one medium (build detected)
    tools/lhde-import --style FILE NAME                     a style dropped in (the Aero patch: aero-patch)
    tools/lhde-import --regenerate                          generated/ made again from the imported files
    tools/lhde-import --migrate [-n]                        an old-layout ~/.local/share/lhde into the three trees
    tools/lhde-import --list                                what is there

Every program looks a file up by its relative name (`styles/jade4074.msstyles`, `frames/jade4074/close.png`,
`resources/4074`) in this order: `~/.config/lhde`, the Windows tree's `index/` (`$LHDE_DATA`, a flat directory, instead:
tests), an old-layout `~/.local/share/lhde` until it is migrated, the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the
source tree's `data/` (liblhde/data.c, lhde/data.h; scripts through `lhde-theme find`). **When something looks wrong:**
`lhde-theme explain [THEME]` prints every visible component of the theme, the file it comes from, its tree and whether it
fell back; `lhde-theme dirs` shows the trees with their sizes.

## Running

    lhde-session start [THEME]               a real X session (startx on :1, vt8; log in on that tty first)
    lhde-session start --nested [THEME]      in a Xephyr window
    lhde-session start --attach [THEME]      on an X server already running on $LHDE_DISPLAY
    lhde-session theme NAME | taskbar KIND | shell on|off | picom on|off | desktop on|off | stop
    LAB=:2 session/lab.sh start; session/lab.sh shell; session/lab.sh theme jade4074; session/lab.sh shot; session/lab.sh stop

From a build tree they use `build/bin`; installed, `$PREFIX/bin`. docs/USAGE.md is the user guide (from
the x11 lab).

## Settings that point at your Windows files

flxp needs none: it finds the imported art through the lookup above (`flxp/shell/lhdata.h` over
`liblhde/data.c`; `flxp --info` prints every lookup and which data directory answered, or "not imported";
`lhde-theme explain` the same for the whole theme).
Settings live in `~/.config/lhde/flxp.ini` (flxp's preferences; any key also as `FLXP_<KEY>` in the environment) and the session
store `~/.config/lhde/settings.ini`; what flxp remembers on its own (views, icon positions) is in `~/.local/state/lhde/`
(docs/SETTINGS.md "Files"). The path keys below exist as `FLXP_<KEY>` for one run only; the theme supplies the files.

| key | default (lhde's data directories) |
|-----|-----------------------------------|
| the session theme | `_LH_THEME` resolved by liblhde (the theme files; `_LH_THEME_RESOLVED` when it is current): style, icons, chrome, navigation bar, taskbar, start menu, wallpaper; flxp.ini's `icons`, `frame`, `taskbar_kind`, `wallpaper`, `wallpaper_mode`, `themes_ini` are ignored (docs/history/flxp-NOTES.md "flxp on liblhde") |
| `theme`, `theme_path` | without a session theme: styles by name, `styles/NAME.msstyles`, `styles/NAME/` (one style inside), else `NAME-xp` |
| `longhorn_system32` | the session theme's `path.resources` when it is a Longhorn build's (`resources/4051`, `resources/4074`, ...) |
| `explorer_resources`, `longhorn_wim` | the framelets: `Microsoft.Windows.Client.dll` beside (or in) `resources/<build>`, the theme's build first, then 4074 |
| `font_path` | `fonts/<build>` with tahoma.ttf (4074's); the session theme's `fonts` (registry) are added too |
| `wallpaper_dirs` | `~/Pictures/Wallpapers` and every `wallpapers/<build>`; the theme's wallpaper from `wallpapers.ini` (root `wallpapers/`) |
| `taskpane_aurora` | `resources/demo` (needs the demo's `docLib/`, `search/` images; the import has only `aurora_strip.png` so far) |
| (XP/2000 art) | no setting: the xp/2k/longhorn4074 icon recipes (`data/icons/*.theme`), the toolbar strips, the task pane's shellstyle bitmaps, the start flag, the Log Off buttons, the user tile and the Folders bar's close glyph are read by resource id from `resources/xp`, `resources/2k` (lhde-import; docs/history/flxp-NOTES.md "XP and 2000 art by resource id"). Without them: Wine's icons and drawn lookalikes. |

`LHDE_WINDOWS` (environment) is another Windows tree, `LHDE_DATA` a flat data directory used instead of it (tests, scratch
imports), for every lhde program and script; flxp's compatibility registry is generated by liblhde (luxwm and luxshell
publish its path as `_LH_THEMES_INI`).
Program names of the flxp side, these settings files and the X properties (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`) are
unchanged from flxp and the Longhorn lab; the lux programs are renamed (lhwm → luxwm, lhwmc → luxwmc,
lhshell → luxshell, lhnav → luxnav; the install keeps lhwm, lhwmc, lhshell symlinks).
