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
| `luxwm/` `luxnav/` `luxshell/` `session/` `examples/` `tools/` `data/` | placeholders: the lux side imports lhwm, lhnav, lhshell, the session scripts, examples, tools and data here (step 3 of docs/LAYOUT.md) | ~/fun/longhorn/x11 |
| `docs/`     | `LAYOUT.md` (layout, asset policy, theme model; draft), `flxp-NOTES.md` (flxp's notes, to be consolidated) | |

No Microsoft files are in this repository, and none may be added (docs/LAYOUT.md,
"Asset policy"): no `.msstyles`, DLLs, fonts, wallpapers, extracted bitmaps or
screenshots of them. The icons in `flxp/res/icons` are Wine's (LGPL), the `.res`
files in `wux/rc` are built from Wine's `.rc` sources, `wux/w32/stdres.res` from
Wine's user32 resources (`wux/w32/mkstdres.py WINE-SRC`). Windows art is read at
run time from the user's own files (settings below).

## Building

Needs a C++17 compiler and pkg-config with x11, xext, freetype2, fontconfig,
libpng, libjpeg (libwebp/libwebpdemux optional), dbus-1 (flxp-portal);
`x86_64-w64-mingw32-windres` only to rebuild a changed `.rc`.

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
    build/bin/flxp flxp-desktop flxp-taskbar flxp-settings flxp-filedialog flxp-portal ut-dump
    build/tests/..., build/tests/w32/... test programs and demos (tests/demo.sh runs some)

The programs run uninstalled from `build/bin` (they find `flxp/res` in the source
tree). Per component: `make wux`, `make wuxtheme`, `make flxp`, `make tests`.

`make install` installs `bin/` (the six flxp programs and ut-dump),
`lib/libwuxtheme.so.1` + `.so`, `include/wuxtheme/ut.h`, `lib/pkgconfig/wuxtheme.pc`,
the data directory `$PREFIX/share/lhde` (`res/icons`), `share/applications/flxp.desktop`,
the portal backend's `flxp.portal` (in `PORTALDIR`, default
`$PREFIX/share/xdg-desktop-portal/portals`; xdg-desktop-portal reads its own
datadir, so set `PORTALDIR=/usr/share/xdg-desktop-portal/portals` for a system
install) and its D-Bus service file (`DBUSSERVICEDIR`, default
`$PREFIX/share/dbus-1/services`). Also settable: `BINDIR LIBDIR INCLUDEDIR DATADIR APPDIR PKGCONFIGDIR`.
The data directory is compiled into the programs; `$PREFIX/share/flxp` (flxp's
old one) is still read as a fallback, `FLXP_DATA` overrides.

### luxnav (the explorer header's vision mode)

luxnav is not in this tree yet. `LUXNAV_DIR` names the lhnav source directory
to build it from (its lhnav.c, lhnav_px.c, lhnav_res.c, into build/luxnav):

    make                                                    without the vision header (the default)
    make LUXNAV_DIR=/u/aap/fun/longhorn/x11/lhnav           with it, as flxp built with its LHNAV_DIR

lhnav includes `<uxtheme/ut.h>`; build/compat forwards that to `wuxtheme/ut.h`
until it includes `<wuxtheme/ut.h>`. When luxnav/ is imported, the build
switches to it.

### make check

`tests/check.sh` runs what needs no Microsoft files: kerneltest (the shim's
kernel/CRT/registry/resource/usp10 parts), gditest, fileops_test, and the
headless scripts under Xvfb (clipboard between two explorers, drag and drop
incl. GTK, the file dialog in both looks) on displays :71-:73
(`LHDE_TEST_DISPLAYS="A B C"` for others), each with a scratch HOME. Skipped when
missing: the headless tests without Xvfb/xdotool (clipboard also xclip), ut-dump
without visual styles in `~/.local/share/lhde/styles` (`LHDE_STYLES`).

## Settings that point at your Windows files

Settings live in `~/.config/flxp/flxp.ini` (any key also as `FLXP_<KEY>` in the
environment; `flxp --info` shows what was found) and the session store
`~/.config/longhorn/settings.ini`. None of these has a default; without them the
programs fall back to Wine's icons, drawn lookalikes and fontconfig fonts.

| key | what reads it |
|-----|---------------|
| `nt5src` | the XP source tree: the xp/2k icon themes (`flxp/res/icons/{xp,2k}.theme`, `shell/icons.cpp`), the task pane's shellstyle bitmaps (`explorer/places.cpp`), start.bmp / LogoffButtons.bmp / the user tile (`taskbar/taskbar.cpp`, `startpanel.cpp`), browtb.bmp (Folders bar close button); `make install-icons` copies the icon files. To be replaced by resources from an imported XP's own DLLs (docs/LAYOUT.md review, point 1). |
| `longhorn_system32` | an extracted Longhorn WINDOWS/system32 (longhorn icons, slate.msstyles gradients, framelets) |
| `longhorn_wim` | a directory of extracted Longhorn builds (`<build>/<n>/WINDOWS`), searched for the header's framelet images (replaces the built-in ~/fun/longhorn/wim) |
| `theme`, `theme_path` | the visual style by name or path, and more style directories (`theme_path` replaces the built-in ~/fun/longhorn/themes/xp-backport for style names) |
| `font_path` | Windows font directories |
| `wallpaper_dirs` | Display Properties' wallpaper list (default `~/Pictures/Wallpapers` only) |
| `taskpane_aurora` | the demo's aurora images (else `<longhorn_system32>/../../../../demo_resources`) |

Program names, these settings files and the X properties (`_LH_*`, `_LHWM_*`) are
unchanged from flxp and the Longhorn lab.
