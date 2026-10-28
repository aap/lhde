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
| `lhtheme/`  | `lhde_data.c`: the data directories (and the registry's resolved copy); the seed of step 4's theme resolver | new |
| `session/`  | `lhde-session` (start, theme, taskbar, shell, picom, desktop, stop), `lab.sh` (the headless bench), `lhwall.sh`, theme-reload/-watch, picom configs | x11 `xinitrc.lab`, `second-display.sh`, `lab.sh`, ... |
| `examples/` | extendframe, navdemo, glassdemo | x11 `examples/`, `glassdemo.c` |
| `tools/`    | `lhde-import` (fills the data directory), recipe builders (`msstyles2theme.py`, `build_aerovision.py`, `build_aero5112.py`) | x11 `lhwm/tools/` |
| `data/`     | text only: `themes.ini`, `wallpapers.ini`, `frames/*/theme.ini` (+ `nav.ini`), `packs/*/shell.lua` | x11 `themes.ini`, `wallpapers.ini`, `lhwm/themes/*/*.ini`, `lhshell/themes/` |
| `docs/`     | `LAYOUT.md` (layout, asset policy, theme model; draft), `flxp-NOTES.md` (flxp's notes); from x11: `USAGE.md`, `THEMES.md`, `SETTINGS.md`, `NAVBAR.md`, `SHELL-DESIGN.md`, `MSSTYLES-FORMAT.md`, `LUXWM.md` (was lhwm/README.md), `CLIENT-PROTOCOL.md`, `CLASSIC-DECO-NOTES.md` (to be consolidated in step 5) | |

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
    make install-icons [NT5SRC=] copy the XP/2000 icon files from nt5src into the data dir
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

`make install` installs `bin/` (the six flxp programs, ut-dump, luxwm, luxwmc, luxshell, lhde-session,
lhde-import, and the compatibility symlinks lhwm, lhwmc, lhshell), `lib/libwuxtheme.so.1` + `.so`,
`lib/libluxnav.so.1` + `.so`, `include/wuxtheme/ut.h`, `include/luxnav/lhnav.h`, `lib/pkgconfig/{wuxtheme,luxnav}.pc`,
the man pages luxwm.1 and luxwmc.1 (`MANDIR`), the data directory `$PREFIX/share/lhde` (`res/icons`;
`themes.ini`, `wallpapers.ini`, `frames/`, `packs/` from data/; `luxshell/` (the Lua); `session/`; `tools/`), `share/applications/flxp.desktop`,
the portal backend's `flxp.portal` (in `PORTALDIR`, default
`$PREFIX/share/xdg-desktop-portal/portals`; xdg-desktop-portal reads its own
datadir, so set `PORTALDIR=/usr/share/xdg-desktop-portal/portals` for a system
install) and its D-Bus service file (`DBUSSERVICEDIR`, default
`$PREFIX/share/dbus-1/services`). Also settable: `BINDIR LIBDIR INCLUDEDIR DATADIR APPDIR PKGCONFIGDIR`.
The data directory is compiled into the programs; `$PREFIX/share/flxp` (flxp's
old one) is still read as a fallback, `FLXP_DATA` overrides.

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
without visual styles in `~/.local/share/lhde/styles` (`LHDE_STYLES`).
Then the lux side (`make check-lux`): luxshell builds and its Lua parses (`luac -p`), and luxwm's frame
regression (`luxwm/tests/regress.sh`): every frame set rendered offline and compared by SHA-256 with a
baseline of this machine (`build/luxwm/regress/baseline.sha256`, written by the first run;
`--baseline-from-x11 DIR` takes it from the x11 lab's build); skipped without imported styles.

## The data directory and lhde-import

The art comes from the user's own Windows files, imported once into the data directory
`~/.local/share/lhde` (`$LHDE_DATA` instead, honoured by every lhde program and script; docs/LAYOUT.md
"Asset policy"):

    tools/lhde-import --from-workspace ~/fun/longhorn       copy what the research workspace has (first version)
    tools/lhde-import --list                                what is there

It fills `styles/` (build-qualified names: aero-patch, jade4074, slate4074, slate4051, luna4051, luna-xp,
aero5048, aero5112, royale, ...; the XP backports as `styles/jade4074-xp/`, `styles/slate4074-xp/`),
`frames/<name>/` (the frame sets' PNGs), `packs/<build>/` (luxshell's images: `4074/imageres`,
`4074/shell32`, `4074/startpanel-*`, `4051/imageres`, `4051/startpanel-luna-*`, `5112/imageres`),
`resources/<build>` (a build's system32; symlinks into the workspace unless `--copy-resources`),
`resources/demo`, `fonts/<build>`, `wallpapers/<build>`. The lux programs search, in order: that
directory, the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the source tree's `data/`
(lhtheme/lhde_data.h); the shipped text (data/) and the imported pixels of a frame set or pack are
found together. Importing from an ISO, a WIM or a Windows directory is step 4.

## Running

    lhde-session start [THEME]               a real X session (startx on :1, vt8; log in on that tty first)
    lhde-session start --nested [THEME]      in a Xephyr window
    lhde-session start --attach [THEME]      on an X server already running on $LHDE_DISPLAY
    lhde-session theme NAME | taskbar KIND | shell on|off | picom on|off | desktop on|off | stop
    LAB=:2 session/lab.sh start; session/lab.sh shell; session/lab.sh theme jade4074; session/lab.sh shot; session/lab.sh stop

From a build tree they use `build/bin`; installed, `$PREFIX/bin`. docs/USAGE.md is the user guide (from
the x11 lab).

## Settings that point at your Windows files

flxp needs none: it finds the imported art through the data directories above (`flxp/shell/lhdata.h` over
`lhtheme/lhde_data.c`; `flxp --info` prints every lookup and which data directory answered, or "not imported").
Settings live in `~/.config/flxp/flxp.ini` (any key also as `FLXP_<KEY>` in the environment) and the session
store `~/.config/longhorn/settings.ini`; the path keys below are overrides only.

| key | default (lhde's data directories) |
|-----|-----------------------------------|
| registry (`_LH_THEMES_INI`, `$LH_THEMES_INI`, `themes_ini`) | `themes.ini`; its relative values (`styles/...`, `resources/4074`, `fonts/4074`) are resolved there |
| `theme`, `theme_path` | styles by name: `styles/NAME.msstyles`, `styles/NAME/` (one style inside: the -xp backports), else `NAME-xp` (`luna` is `styles/luna-xp.msstyles`); a session theme's Longhorn-compiled style falls back to `styles/NAME-xp/` |
| `longhorn_system32` | the registry's `flxp_longhorn_system32`, else `resources/<build>` of the session theme's build (`slate4051`, `luna4051`: 4051; the 4074 themes: 4074) |
| `explorer_resources`, `longhorn_wim` | the framelets: `Microsoft.Windows.Client.dll` beside (or in) `resources/<build>`, the theme's build first, then 4074 |
| `font_path` | `fonts/<build>` with tahoma.ttf (4074's); the session theme's `fonts` (registry) are added too |
| `wallpaper_dirs` | `~/Pictures/Wallpapers` and every `wallpapers/<build>`; the theme's wallpaper from `wallpapers.ini` (root `wallpapers/`) |
| `taskpane_aurora` | `resources/demo` (needs the demo's `docLib/`, `search/` images; the import has only `aurora_strip.png` so far) |
| `nt5src` | none: the XP source tree for the xp/2k icon themes (`flxp/res/icons/{xp,2k}.theme`), the task pane's shellstyle bitmaps (`explorer/places.cpp`), start.bmp / LogoffButtons.bmp / the user tile (`taskbar/taskbar.cpp`, `startpanel.cpp`), browtb.bmp (Folders bar close button); `make install-icons` copies the icon files. Without it: Wine's icons and drawn lookalikes. To be replaced by resources from an imported XP's own DLLs (docs/flxp-NOTES.md "flxp finds its files through lhde_data" has the resource table). |

`LHDE_DATA` (environment) replaces the user data directory for every lux program and script; the theme
registry is `data/themes.ini` (luxwm and luxshell publish its resolved copy as `_LH_THEMES_INI`).
Program names of the flxp side, these settings files and the X properties (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`) are
unchanged from flxp and the Longhorn lab; the lux programs are renamed (lhwm → luxwm, lhwmc → luxwmc,
lhshell → luxshell, lhnav → luxnav; the install keeps lhwm, lhwmc, lhshell symlinks).
