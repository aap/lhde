# Building, installing and running lhde

The long form of README.md's "Install & Run": the prerequisites, what `make` builds and installs, the three trees, how the
sessions are started. The repository, one build for the parts that were separate projects:

| directory   | what                                                                 | was |
|-------------|----------------------------------------------------------------------|-----|
| `wux/`      | the win32 UI layer that lhde's own programs are built against from source (it runs no Windows executables): `gdi/` `sys/` `win/` `res/` (native toolkit), `w32/` (the win32 shim), `vendor/wine/` (Wine's comctl32/user32/uxtheme, LGPL, with MANIFEST and patches), `rc/` (Wine's comctl32/comdlg32 resources) | flxp `src/{gdi,sys,win,res,w32}`, `vendor/wine` |
| `wuxtheme/` | libwuxtheme, the uxtheme library: `.msstyles` read and drawn without Windows (`<wuxtheme/ut.h>`, C API `ut_*`), and `ut-dump` | flxp `src/uxtheme` (libuxtheme) |
| `flxp/`     | the explorer and shell: `shell/` `explorer/` `desktop/` `taskbar/` `filedlg/` `settings/`, `rc/` (its dialogs), `res/` (icon themes, .desktop, portal files, tools) | flxp |
| `tests/`    | `toolkit/` and `w32/` test programs, `headless/` Xvfb scripts, `check.sh`, `demo.sh` | flxp `src/test`, `src/w32test` |
| `luxwm/`    | the window manager: Longhorn frames (DWM glass frame sets, classic frames from the style through wuxtheme), EWMH, animations; `luxwmc`; `tests/` (frametest, regress) | x11 `lhwm/` |
| `luxnav/`   | libluxnav, the Longhorn navigation bar (the vision header; C API `lhnav_*`, `lhnav.h`), linked into flxp and navdemo | x11 `lhnav/` |
| `luxshell/` | the Avalon bars, sidebar, tiles, toasts, start menu: C core + Lua (`*.lua`, `tiles/`) | x11 `lhshell/` (without `assets/`) |
| `liblhde/`  | liblhde (`<lhde/data.h>`, `<lhde/theme.h>`, `<lhde/settings.h>`): the three trees and every lookup in them, the theme resolver, the settings store; `lhde-theme` (with `explain` and `dirs`); `tests/resolve.c` | new (was `lhtheme/`) |
| `session/`  | `lhde-session` (start, theme, taskbar, shell, picom, desktop, stop), `lab.sh` (the headless bench), `lhwall.sh`, theme-reload/-watch, picom configs | x11 `xinitrc.lab`, `second-display.sh`, `lab.sh`, ... |
| `apps/`     | lhde's applications on the public entry point (`wux/wux.h`, `shell/shell.h`, the recipe `apps/app.mk`; docs/APPLICATIONS.md): `luxshot` (screenshots), `skeleton` (the one to copy) | new |
| `examples/` | extendframe, navdemo, aurorademo (its tables made at build time from the Windows tree) | x11 `examples/` |
| `tools/`    | `lhde-pack` (a resource pack of raw Windows files from ISOs), `lhde-import` (a pack, ISO, WIM or Windows directory into the Windows tree, the recipes), `lhdeimport.py`, `lhdetree.py` (the Windows tree, its index, the migration), `wim0108.py`, `lhdebaml.py` + `lhdeaurora.py` (the aurora: BAML decoders, layer lists, aurorademo's tables), recipe builders (`msstyles2theme.py`, `build_aerovision.py`, `build_aero5112.py`) | x11 `lhwm/tools/` |
| `share/`    | text only: `themes/*.theme` (the shipped themes), `navbar/*.ini`, `icons/*.theme`, `sources/*.ini` (what lhde-pack takes from each Windows build), `frames/*/theme.ini`, `packs/*/shell.lua` | x11 `themes.ini`, `wallpapers.ini`, `lhwm/themes/*/*.ini`, `lhshell/themes/` |
| `docs/`     | `ARCHITECTURE.md` (the map), `LAYOUT.md` (asset policy, the three trees), `THEME-FORMAT.md`, `SETTINGS.md`, `CLIENT-PROTOCOL.md`, `USAGE.md`, `LUXWM.md`, `NAVBAR.md`, `SHELL-DESIGN.md`, `MSSTYLES-FORMAT.md`, `ANNOTATIONS.md`, `LICENSING.md`, `APPLICATIONS.md`; `docs/README.md` lists them; `history/` holds the journals (flxp-NOTES, THEMES, the merge plan, the theme-format status) | |

No Microsoft files are in this repository, and none may be added
(docs/LAYOUT.md, "Asset policy"): no `.msstyles`, DLLs, fonts, wallpapers,
extracted bitmaps, Microsoft concept art or screenshots of the real Windows
builds. Screenshots of lhde itself are fine and live in `screenshots/`. The
icons in `flxp/res/icons` are Wine's (LGPL), the `.res` files in `wux/rc` are
built from Wine's `.rc` sources, `wux/w32/stdres.res` from Wine's user32
resources (`wux/w32/mkstdres.py WINE-SRC`). Windows art is read at run time from
the user's own files (settings below).

## Building

Needs a C and C++17 compiler, GNU make, Python 3 (the standard library: every `make` runs
`tools/lhdeaurora.py` for aurorademo's tables) and pkg-config with x11, xext, freetype2, fontconfig,
libpng, libjpeg (libwebp/libwebpdemux optional), dbus-1 (flxp-portal, luxshell);
for the lux side also xfixes, xinerama, xft, cairo, cairo-xlib, pangocairo and lua5.4 (system Lua 5.4).
Every `.res` is checked in (`wux/rc/`, `wux/w32/stdres.res`, `flxp/rc/`, `tests/w32/test.res`):
the mingw-w64 cross toolchain (`x86_64-w64-mingw32-windres`, which preprocesses with its gcc) is needed
only when a `.rc` or a header it includes changes, and Wine's sources only to rebuild Wine's resources
(`wux/vendor/wine/MANIFEST`).

Importing the Windows resources (`lhde-import`, `lhde-pack`, `lhde-schemes`; docs/USAGE.md "Getting the
Windows resources") needs Python 3 with Pillow, 7z (ISOs, WIMs, the compressed files of an I386 layout;
cabextract also does those), and tar with xz (zstd when installed) for resource packs.
At run time the session uses picom, feh, xprop, xsetroot and dbus-daemon; `lhde-session start` also
startx (xinit), `--nested` Xephyr; the lab and `make check` Xvfb, xdotool, ImageMagick's import, xclip and
luac5.4 (each test is skipped when its tool is missing).

On Void Linux, everything above in one command:

    xbps-install -S gcc make pkg-config python3 libX11-devel libXext-devel libXfixes-devel \
        libXinerama-devel libXft-devel freetype-devel fontconfig-devel libpng-devel libjpeg-turbo-devel \
        libwebp-devel dbus-devel cairo-devel pango-devel lua54-devel \
        python3-Pillow 7zip cabextract xz tar \
        dbus picom feh xprop xsetroot xinit xorg-server-xephyr xorg-server-xvfb xdotool ImageMagick xclip lua54

(python3-pillow-simd works in place of python3-Pillow; `cross-x86_64-w64-mingw32` is the windres above).
Other distributions need the equivalent packages. Then:

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
    build/bin/luxshot skeleton           the applications (apps/; skeleton is not installed)
    build/lib/libflxpshell.a             the shell facilities an application links (apps/app.mk)
    build/examples/extendframe navdemo aurorademo
    build/tests/..., build/tests/w32/... test programs and demos (tests/demo.sh runs some)
    build/luxwm/tests/frametest menutest desktopwin dockorder strutdock, build/luxshell/tests/trayicon

The programs run uninstalled from `build/bin` (they find `flxp/res`, `luxshell/` and `share/` in the source
tree). Per component: `make wux`, `make wuxtheme`, `make flxp`, `make tests`, `make luxnav`, `make luxwm`,
`make luxshell`, `make examples`, `make apps`.

`make install` installs program files only (docs/LAYOUT.md "The three trees"): `bin/` (the six flxp programs, ut-dump,
luxwm, luxwmc, luxshell, luxshot, lhde-theme, lhde-session, lhde-import, lhde-pack, lhde-schemes), `lib/` (`libwuxtheme`, `libluxnav`, `liblhde` `.so.1` + `.so`, `liblhde.a`, `pkgconfig/`),
`lib/lhde/` (`PKGLIBDIR`: the code that is not a binary: `luxshell/` the Lua, `session/` lhde-session's helpers and lab.sh,
`tools/` lhde-import's Python), `share/lhde/` (`DATADIR`: our shipped text, `themes/ navbar/ icons/ sources/ frames/*/theme.ini
packs/*/shell.lua`, flxp's `res/icons`), `include/`,
the man pages luxwm.1 and luxwmc.1 (`MANDIR`), `share/applications/flxp.desktop`, the portal backend's `flxp.portal` (in
`PORTALDIR`, default `$PREFIX/share/xdg-desktop-portal/portals`; xdg-desktop-portal reads its own datadir, so set
`PORTALDIR=/usr/share/xdg-desktop-portal/portals` for a system install) and its D-Bus service file (`DBUSSERVICEDIR`, default
`$PREFIX/share/dbus-1/services`). Also settable: `BINDIR LIBDIR INCLUDEDIR DATADIR PKGLIBDIR APPDIR PKGCONFIGDIR`.
`DATADIR` is compiled into flxp (its `res/icons`; everything else it finds through liblhde).
No Windows file and no user setting is ever installed: those are the two other trees below.

### luxnav (the explorer header's vision mode)

luxnav is in the tree (luxnav/, `make luxnav`); flxp links `build/lib/libluxnav.a` and always has the vision
header (the step-2 `LUXNAV_DIR` hook and build/compat are gone).

### make check

`tests/check.sh` runs what needs no Microsoft files: kerneltest (the shim's
kernel/CRT/registry/resource/usp10 parts), gditest, fileops_test, `tests/settings-lists.sh` (Display Properties' Desktop
lists without a window: the backgrounds' order and selection, the icon sets; Appearance's "Windows and buttons", `--list
appearance --pick STYLE`: classic, a bare style with and without a frame set, back to the theme's own, with the user's
`jade4074.msstyles` copied under other names, skipped without it), `tests/aero-patches.py` (lhde-import's table of Aero
patches and which styles get a frame set, on stand-in files), and the
headless scripts under Xvfb (clipboard between two explorers, drag and drop
incl. GTK, the file dialog in both looks, the modifier keys across a focus change) on displays :71-:73
(`LHDE_TEST_DISPLAYS="A B C"` for others), each with a scratch HOME. Skipped when
missing: the headless tests without Xvfb/xdotool (clipboard also xclip), ut-dump
without imported visual styles (`lhde-theme find styles`: the Windows tree's `index/styles`; `LHDE_STYLES` overrides).
Then the lux side (`make check-lux`): luxshell builds and its Lua parses (`luac -p`), and luxwm's frame
regression (`luxwm/tests/regress.sh`): every frame set rendered offline and compared by SHA-256 with a
baseline of this machine (`build/luxwm/regress/baseline.sha256`, written by the first run); skipped
without imported styles.
Last the free baseline (`make check-free`, `tests/check-free.sh`): the free pack or ReactOS's boot CD (`FREE_PACK=FILE`,
`LHDE_REACTOS_ISO=FILE`, `~/lib/windows_isos/ReactOS-*.iso`, or the pack `make fetch-free-pack` downloaded into
`~/.cache/lhde`, the first there; skipped without any, so a CI runs `make fetch-free-pack` first; beside the ISO the free
themes' photos from `FREE_WALLPAPERS`, and without that directory their missing wallpaper is reported, not failed) imported
once into `build/free/windows`, and with that tree
alone every shipped theme must resolve (`lhde-theme explain`, `explain --files`), classic and the ReactOS themes with
nothing missing but what ReactOS stands in for, and luxwm's classic frames render against a baseline of their own
(`build/luxwm/regress/baseline-free.sha256`; ReactOS's Tahoma and Marlett, so they differ from the normal run's).
Nothing imported at all (`make check-bare`, `tests/check-bare.sh`, what a CI job runs): the whole `make check` with an empty
Windows tree and empty config, data, state and cache homes and HOME (fresh temporary directories; no `LHDE_DATA`, no ReactOS
ISO or free pack), where every test must pass or skip with a message; a test that fails for lack of resources is a bug. It is not part of
`make check`, being as long (about 3 minutes, most of it the headless scripts).

## The three trees, lhde-import and lhde-theme explain

lhde's files live in three places that never mix (docs/LAYOUT.md "The three trees"):

    $PREFIX/share/lhde/        program files: our shipped text (make install; never edited)
    $PREFIX/lib/lhde/          program files: luxshell's Lua, the session scripts, lhde-import's Python
    ~/.local/share/lhde-windows/   the user's Windows files (lhde-import; $LHDE_WINDOWS): <build>/{system32,themes,fonts,
                               wallpapers}, local/ (dropped in), generated/ (derived PNGs: delete freely), index/ (symlinks)
    ~/.config/lhde/            the user's files: settings.ini, themes/, and any shipped file overridden under its own name

The art comes from the user's own files, imported once (docs/USAGE.md "Getting the Windows resources"). Three ways to a
clean Windows tree (each deletes it first, asking; `CLEAN=1` without asking):

| make target | imports | gives |
| --- | --- | --- |
| `install-resources-free` | the free pack, ReactOS 0.4.16's files (downloaded; `FREE_PACK=FILE`, or the boot CD: `LHDE_REACTOS_ISO=FILE`, `~/lib/windows_isos/ReactOS-*.iso`; free, GPL; below) | classic and ReactOS's four styles complete, every other theme as classic with ReactOS's icons and fonts |
| `install-resources-windows PACK=lhde_winthemes.zip` | your small Windows pack (`lhde-pack`, essentials) | the shipped Windows themes |
| `install-resources-full PACK=lhde_wintheme_full.zip` | the free resources (as above) and your whole Windows pack (`lhde-pack --profile museum`) | everything |

The pack names are defaults for files lhde does not supply (`lhde-pack --zip` writes one; a `.tar.zst` / `.tar.xz` or a
pack directory works too). By hand:

    tools/lhde-pack --isos DIR --out PACKS                  a resource pack (raw files) from the ISOs in DIR
    tools/lhde-import --pack PACKS/lhde-resources-DATE.tar.xz   the pack into the Windows tree, recipes run
    tools/lhde-import --iso FILE | --wim FILE | --dir WINDOWS   the same from one medium (build detected)
    tools/lhde-import --style FILE NAME                     a style dropped in (RealAero: realaero)
    tools/lhde-import --styles DIR [-n]                     every style of a downloaded folder (installers unpacked, not run)
    tools/lhde-import --regenerate                          generated/ made again from the imported files
    tools/lhde-import --list                                what is there
    tools/lhde-import --clean [--yes]                       the Windows tree deleted (asks first)

### The free resources
ReactOS's files (shell32, browseui, comctl32, themeui, explorer.exe, its four visual styles, 40 free fonts under the Windows
file names; docs/USAGE.md "Getting the Windows resources") are free, and so are the wallpapers of the four ReactOS themes
(photos from Pexels, the Pexels licence, chosen by aap: the source `free`, share/sources/free.ini), so lhde publishes them as
one pack, outside the repository: `lhde-resources-free-1.zip` (10 MB), made once from ReactOS 0.4.16's boot CD and the
photos. Its version (`FREE_PACK_VERSION`) counts its contents, a new ReactOS or other photos being the next. `make
install-resources-free` takes, in this order, `FREE_PACK=FILE`, the ISO (`LHDE_REACTOS_ISO=FILE`, else the newest
`~/lib/windows_isos/ReactOS-*.iso`, with the photos from `FREE_WALLPAPERS` when that directory is there), else the pack
downloaded from `FREE_PACK_URL` (curl or wget) into `~/.cache/lhde/` and checked against `FREE_PACK_SHA256` before the import;
both ways give the same Windows tree, byte for byte. `make fetch-free-pack` only downloads and checks it (for `make
check-free` on a machine without the ISO).

`make free-pack` builds the pack in the tree root (gitignored): `lhde-pack --profile museum --sources reactos,free
--reproducible --no-tar --zip` from `REACTOS_ISO` (default `~/lib/windows_isos/ReactOS-0.4.16-i386.iso`) and
`FREE_WALLPAPERS` (default `~/cfg/wallpaper`, aap's downloads under Pexels's names; the ini takes only the four it names, as
`free/wallpapers/<photographer>-<photo id>.jpg`, with `LICENSE.txt` from share/sources/free-wallpapers-LICENSE.txt), with
`--notes` adding a `LICENSE` (share/sources/free-pack-LICENSE.txt: what the pack holds, ReactOS's GPL 2 or later and LGPL 2.1
or later, each font's licence, the photos') and the licence texts from ReactOS's source at tag 0.4.16 (`REACTOS_SRC`, default
`~/lib/reactos-0.4.16`: `COPYING`, `COPYING.LIB`, `COPYING3`, `COPYING3.LIB`, `CREDITS`, `media/fonts/doc` as
`licenses/fonts/`); it prints the size and the sha256. `--reproducible` dates every entry the same and writes no
annotations, so the same CD and photos (and the same `share/sources/reactos.ini` and `free.ini`, which the pack carries)
give the same sha256 as the published pack; a new pack is published under `FREE_PACK_URL` and its sha256 set in
`FREE_PACK_SHA256`.

Every program looks a file up by its relative name (`styles/jade4074.msstyles`, `frames/jade4074/close.png`,
`resources/4074`) in this order: `~/.config/lhde`, the Windows tree's `index/` (`$LHDE_DATA`, a flat directory, instead:
tests), the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the
source tree's `share/` (liblhde/data.c, lhde/data.h; scripts through `lhde-theme find`). **When something looks wrong:**
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
(docs/SETTINGS.md "Files"). There are no path keys: the theme and the imported tree supply the files.

| what | where it comes from (lhde's data directories) |
|-----|-----------------------------------|
| the session theme | `_LH_THEME` resolved by liblhde (the theme files; `_LH_THEME_RESOLVED` when it is current): style, icons, chrome, navigation bar, taskbar, start menu, wallpaper; flxp.ini has no key for any of them |
| `--theme NAME` / `FLXP_THEME` | one run's style instead of the session theme's, by name: `styles/NAME.msstyles`, `styles/NAME/` (one style inside), else `NAME-xp` |
| the Longhorn art (shell32, slate.msstyles) | the session theme's `path.resources` when it is a Longhorn build's (`resources/4051`, `resources/4074`, ...) |
| the framelets | `Microsoft.Windows.Client.dll` beside (or in) `resources/<build>`, the theme's build first, then 4074 |
| the fonts | `fonts/<build>` with tahoma.ttf (4074's); the session theme's `fonts` (registry) are added too |
| Display Properties' wallpaper list | flxp.ini `wallpaper_dirs` (`:`-separated), default `~/Pictures/Wallpapers` and every `wallpapers/<build>`; the theme's wallpaper from `wallpapers.ini` (root `wallpapers/`) |
| the task band's aurora (flxp.ini `taskpane_background = aurora`) | `resources/demo` (needs the demo's `docLib/`, `search/` images; the import has only `aurora_strip.png` so far) |
| (XP/2000 art) | no setting: the xp/2k/longhorn4074 icon recipes (`share/icons/*.theme`), the toolbar strips, the task pane's shellstyle bitmaps, the start flag, the Log Off buttons, the user tile and the Folders bar's close glyph are read by resource id from `resources/xp`, `resources/2k` (lhde-import; docs/history/flxp-NOTES.md "XP and 2000 art by resource id"). Without them: Wine's icons and drawn lookalikes. |

`LHDE_WINDOWS` (environment) is another Windows tree, `LHDE_DATA` a flat data directory used instead of it (tests, scratch
imports), for every lhde program and script.
The X properties (`_LH_*`, `_LHWM_*`, `_LHSHELL_*`), the window manager's name `lhwm` (its `_NET_WM_NAME`) and the C
prefixes (`ut_`, `lhnav_`, `lhde_`) are stable names; the programs are luxwm, luxwmc, luxshell, luxnav and the flxp ones.
