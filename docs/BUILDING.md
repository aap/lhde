# Building, installing and running lhde

The long form of README.md's "Install & Run": the prerequisites, what `make` builds and installs, the three trees, how the
sessions are started. The repository, one build for the parts that were separate projects:

| directory   | what                                                                 | was |
|-------------|----------------------------------------------------------------------|-----|
| `wux/`      | the win32 UI layer that lhde's own programs are built against from source (it runs no Windows executables): `gdi/` `sys/` `win/` `res/` (native toolkit), `w32/` (the win32 shim), `vendor/wine/` (Wine's comctl32/user32/uxtheme, LGPL, with MANIFEST and patches), `rc/` (Wine's comctl32/comdlg32 resources) | flxp `src/{gdi,sys,win,res,w32}`, `vendor/wine` |
| `wuxtheme/` | libwuxtheme, the uxtheme library: `.msstyles` read and drawn without Windows (`<wuxtheme/ut.h>`, C API `ut_*`), and `ut-dump` | flxp `src/uxtheme` (libuxtheme) |
| `flxp/`     | the explorer and shell: `shell/` (with `shell/ui/`, its win32 part) `explorer/` `desktop/` `taskbar/` `filedlg/` `settings/`, `rc/` (its dialogs), `res/` (icon themes, .desktop, portal files, tools) | flxp |
| `tests/`    | `toolkit/` and `w32/` test programs, `headless/` Xvfb scripts, `check.sh`, `demo.sh` | flxp `src/test`, `src/w32test` |
| `luxwm/`    | the window manager: Longhorn frames (DWM glass frame sets, classic frames from the style through wuxtheme), EWMH, animations; `luxwmc`; `tests/` (frametest, regress) | x11 `lhwm/` |
| `luxnav/`   | libluxnav, the Longhorn navigation bar (the vision header; C API `lhnav_*`, `lhnav.h`), linked into flxp and navdemo | x11 `lhnav/` |
| `luxshell/` | the Avalon bars, sidebar, tiles, toasts, start menu: C core + Lua (`*.lua`, `tiles/`) | x11 `lhshell/` (without `assets/`) |
| `liblhde/`  | liblhde (`<lhde/data.h>`, `<lhde/theme.h>`, `<lhde/settings.h>`, `<lhde/hw.h>`): the three trees and every lookup in them, the theme resolver, the settings store, the hardware layer; `lhde-theme` (with `explain` and `dirs`), `lhde-hw`; `tests/resolve.c`, `tests/hw.c` | new (was `lhtheme/`) |
| `session/`  | `lhde-session` (start, theme, taskbar, shell, picom, desktop, stop), `lab.sh` (the headless bench), `lhwall.sh`, theme-reload/-watch, picom configs | x11 `xinitrc.lab`, `second-display.sh`, `lab.sh`, ... |
| `apps/`     | lhde's applications on the public entry point (`wux/wux.h`, `shell/shell.h`, the recipe `apps/app.mk`; docs/APPLICATIONS.md): `luxshot` (screenshots), `luxattr` (the attributes editor), `luxpad` (the notepad), `skeleton` (the one to copy) | new |
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
`tools/lhdeaurora.py` for aurorademo's tables) and pkg-config with x11, xext, xcursor, xfixes, xkbcommon, freetype2,
fontconfig, libpng, libjpeg (libwebp/libwebpdemux optional), dbus-1 (flxp-portal, luxshell);
for the lux side also xinerama, xft, cairo, cairo-xlib, pangocairo and lua5.4 (system Lua 5.4).
Every `.res` is checked in (`wux/rc/`, `wux/w32/stdres.res`, `flxp/rc/`, `tests/w32/test.res`):
the mingw-w64 cross toolchain (`x86_64-w64-mingw32-windres`, which preprocesses with its gcc) is needed
only when a `.rc` or a header it includes changes, and Wine's sources only to rebuild Wine's resources
(`wux/vendor/wine/MANIFEST`).

Importing the Windows resources (`lhde-import`, `lhde-pack`, `lhde-schemes`; docs/USAGE.md "Getting the
Windows resources") needs Python 3 with Pillow, 7z (ISOs, WIMs, the compressed files of an I386 layout;
cabextract also does those), and tar with xz (zstd when installed) for resource packs.
At run time the session uses picom, feh, xprop, xsetroot and dbus-daemon; `lhde-session start` also
startx (xinit), `--nested` Xephyr; the lab and `make check` Xvfb, xdotool, ImageMagick's import, xclip and
luac5.4 (each test is skipped when its tool is missing). Thumbnails of documents (docs/USAGE.md, thumbnails) come from
optional run-time tools, each for lhde's own thumbnailer entries: poppler-utils' pdftoppm (PDF, and DjVu with ddjvu),
MuPDF's mutool (XPS, EPUB, comic books; PDF without pdftoppm), Ghostscript's gs (PostScript, EPS) and djvulibre's ddjvu
(DjVu); any other thumbnailer a package installs (ffmpegthumbnailer for videos, gdk-pixbuf's for GIF and TIFF) is used too.
On Void: `xbps-install poppler-utils mupdf-tools ghostscript djvulibre ffmpegthumbnailer`.

On Void Linux, everything above in one command:

    xbps-install -S gcc make pkg-config python3 libX11-devel libXext-devel libXcursor-devel libxkbcommon-devel libXfixes-devel \
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
    build/bin/luxshot luxattr luxpad skeleton   the applications (apps/; skeleton is not installed)
    build/lib/libflxpshell.a             the shell facilities an application links (apps/app.mk)
    build/examples/extendframe navdemo aurorademo
    build/tests/..., build/tests/w32/... test programs and demos (tests/demo.sh runs some)
    build/luxwm/tests/frametest menutest desktopwin dockorder strutdock, build/luxshell/tests/trayicon

The programs run uninstalled from `build/bin` (they find `flxp/res`, `luxshell/` and `share/` in the source
tree). Per component: `make wux`, `make wuxtheme`, `make flxp`, `make tests`, `make luxnav`, `make luxwm`,
`make luxshell`, `make examples`, `make apps`.

`make install` installs program files only (docs/LAYOUT.md "The three trees"): `bin/` (the six flxp programs, ut-dump,
luxwm, luxwmc, luxshell, luxshot, luxattr, luxpad, lhde-theme, lhde-hw, lhde-session, lhde-import, lhde-pack, lhde-schemes, lhde-mime), `lib/` (`libwuxtheme`, `libluxnav`, `liblhde` `.so.1` + `.so`, `liblhde.a`, `pkgconfig/`),
`lib/lhde/` (`PKGLIBDIR`: the code that is not a binary: `luxshell/` the Lua, `session/` lhde-session's helpers and lab.sh,
`tools/` lhde-import's Python), `share/lhde/` (`DATADIR`: our shipped text, `themes/ navbar/ icons/ sources/ frames/*/theme.ini
packs/*/shell.lua`, flxp's `res/icons`), `include/`,
the man pages luxwm.1 and luxwmc.1 (`MANDIR`), `share/applications/flxp.desktop`, `luxpad.desktop` and `lhde-mimeapps.list` (lhde's
default programs, docs/SETTINGS.md "Associations"; without a `DESTDIR` update-desktop-database then indexes `APPDIR`), the portal backend's `flxp.portal` (in
`PORTALDIR`, default `$PREFIX/share/xdg-desktop-portal/portals`; xdg-desktop-portal reads its own datadir, so set
`PORTALDIR=/usr/share/xdg-desktop-portal/portals` for a system install) and its D-Bus service file (`DBUSSERVICEDIR`, default
`$PREFIX/share/dbus-1/services`). Also settable: `BINDIR LIBDIR INCLUDEDIR DATADIR PKGLIBDIR APPDIR PKGCONFIGDIR`.
`DATADIR` is compiled into flxp (its `res/icons`; everything else it finds through liblhde).
No Windows file and no user setting is ever installed: those are the two other trees below.

### FreeBSD

lhde builds and runs on FreeBSD 15 (amd64) with clang, libc++ and GNU make; 15 is the first release with inotify(2) (the
explorer following its folders), renameat2 and O_PATH, which flxp's file operations use. The packages:

    pkg install gmake pkgconf python3 py312-pillow 7-zip cabextract libX11 libXext libXcursor libxkbcommon libXfixes \
        libXinerama libXft freetype2 fontconfig png jpeg-turbo webp dbus cairo pango lua54 \
        picom feh xprop xsetroot xinit xephyr xorg-vfbserver xdotool ImageMagick7 xclip gtar

(`python3` is the package that puts `python3` on the PATH; `7-zip` installs `7z` and `7zz`, there is no p7zip.) Then, as above
with GNU make (FreeBSD's make is not):

    gmake -j8
    PREFIX=~/.local gmake install
    gmake install-resources-free FREE_PACK=lhde-resources-free-2.zip    # or with no FREE_PACK: downloaded
    LHDE_TEST_DISPLAYS=":71 :72 :73" gmake check

`gmake check` there runs everything `make check` runs on Linux: the shim's and the toolkit's tests, the scripts, liblhde's
resolver and hardware layer, luxshell's Lua, and the headless scripts under Xvfb, the attribute ones (luxattr, properties, filter, runas,
luxshot) through `getextattr` / `setextattr` (`tests/headless/xattrs.sh`: attr's getfattr and setfattr do not exist there).
What skips there and why: swatchtest without a `DISPLAY`; luxwm's frame regression and ut-dump without the frame sets'
styles (a tree with the free pack only); fileops_test's moves between two file systems when its scratch directories share
one (no `/dev/shm`: `build/tests/fileops_test DIR1 DIR2` names a second); a test that needs a style the free pack lacks
(jade4074, luna). luxshell's Lua is parsed with lua54's `luac54`. With the free pack imported, `lhde-theme resolve`
gives the same records as on Linux, and `lhde-session start --attach` on an Xvfb shows the session (luxwm, luxshell,
flxp-desktop, flxp-taskbar). What differs, all behind `liblhde/compat.h` (one header, the only place that knows the system),
`wux/w32/include/flxp_crt.h` for the win32 code and two shell helpers in `session/lhde-paths.sh`:

- Extended attributes are extattr(2)'s: lhde's programs and tools name them as on Linux (`user.lux.desc`), stored in the user
  namespace without the prefix (`getextattr user lux.desc FILE`; `lsextattr user FILE` lists them), so lhde-import's
  annotations, the Python tools (through ctypes: Python has no `os.getxattr` there) and the C programs read each other's.
  Only the user namespace is listed and copied. ZFS keeps them (`xattr=sa`). A missing attribute is `ENOATTR`, which the C
  code compares as `LHDE_ENOATTR` (libc++ defines an `ENODATA` of its own).
- The running binary, another process's binary, its environment and arguments come from sysctl(3) (`kern.proc`), not
  `/proc`; the session scripts read them with procstat(1) (`lhde_environ`, `lhde_args`), and detach with daemon(8) where
  there is no setsid(1) (`$LHDE_SETSID`: a command, since FreeBSD's sh does not export an assignment written before a
  function call). A set-user-id program (FreeBSD's urxvt) does not show its environment.
- The mounts (My Computer's drives, the Details pane's file system, which folders are on the network for thumbnails) come
  from getmntinfo(3) and statfs's `f_fstypename` (nfs, smbfs, p9fs and the network FUSE file systems are remote).
- Pack archives: bsdtar unpacks a `.tar.zst` / `.tar.xz` pack with its annotations; making one (`lhde-pack` without `--zip`)
  needs GNU tar, `gtar`.
- In C, FreeBSD's headers type `wchar_t` as `int` whatever `-fshort-wchar` says; `flxp_crt.h` types it as the compiler's
  16-bit one for the win32 code. In C++, libc++ runs `std::find` over 16-bit types, `u16string::find` and
  `char_traits<wchar_t>` through the wide-string builtins, which would call the libc's 32-bit `wmemchr`, `wmemcmp` and
  `wcslen`; `flxp_crt.h` makes them 16-bit loops.
- No mingw windres package: a `.res` whose `.rc` looks newer (a checkout's times) is kept as checked in, with a warning.
- The hardware (the power tile, the Volume, Brightness and Touchpad items of luxshell and flxp-taskbar, `lhde-hw`) is
  liblhde's layer's FreeBSD back end (`liblhde/hw_freebsd.c`, docs/APPLICATIONS.md "Hardware"): the battery from the
  `hw.acpi.battery` sysctls and `hw.acpi.acline` (every battery together; changes from devd's socket,
  `/var/run/devd.seqpacket.pipe`), the backlight through `/dev/backlight/backlight0`'s ioctls (the i915 driver registers it
  when `i915kms` is loaded; `backlight(8)` when they fail), the volume through the OSS mixer of `hw.snd.default_unit`
  (`/dev/mixerN`: `vol`, its mute mask; `mixer(8)` when the ioctls fail, pactl when there is no mixer device; read every
  second: the mixer has no change events). sndio is not a back end: on FreeBSD it sits on the same OSS mixer, which the layer
  reads directly. The touchpad is the X server's (`xinput`, as on Linux): Xorg's libinput driver adds psm's evdev device
  (`hw.psm.synaptics_support=1`, `kern.evdev.rcpt_mask=12`) as "SynPS/2 Synaptics TouchPad", which the item finds by its
  name. Permissions: the mixer devices are 0666 and the sysctls and devd's socket readable by everyone; the backlight
  device is root:video 0660, so a user who sets it is in the `video` group (as root: `pw groupmod video -m USER`, then log in
  again); without it the item shows the level and its tooltip names the group. `lhde-hw describe` says what is used.

### luxnav (the explorer header's vision mode)

luxnav is in the tree (luxnav/, `make luxnav`); flxp links `build/lib/libluxnav.a` and always has the vision
header (the step-2 `LUXNAV_DIR` hook and build/compat are gone).

### make check

`make check` first compiles a probe that calls glibc's wcslen in win32 C++ and fails if it compiles (`check-poison`:
wchar_t is 16 bits there, `wux/w32/include/flxp_crt.h` poisons glibc's wide functions). `tests/check.sh` runs what needs no Microsoft files: kerneltest (the shim's
kernel/CRT/registry/resource/usp10 parts), gditest, swatchtest (the Details Color column's square through an image list),
fileops_test, thumbstest (the thumbnails' memory bounds on pictures it writes: a 6000 x 4000 JPEG and a 4000 x 4000 PNG into 128 px thumbnails with the peak RSS growing by less than 16 MB, the PNG reduced row by row; the view's image budget over 300 inserts, the pinned ones kept; a cancelled loader generation
decoding nothing more, three loaders decoding at most two pictures at once; thumbnailer entries with stand-in scripts: their order,
a missing TryExec skipped, a parent type's entry, the Exec line's quoting and field codes, the page at the requested size, the
child's minimal environment, the cache written and read back, the output cap and the failure marker, the timeout killing the
script and its own child), `tests/settings-lists.sh` (Display Properties' Desktop
lists without a window: the backgrounds' order and selection, the icon sets; Appearance's "Windows and buttons", `--list
appearance --pick STYLE`: classic, a bare style with and without a frame set, back to the theme's own, with the user's
`jade4074.msstyles` copied under other names, skipped without it), `tests/aero-patches.py` (lhde-import's table of Aero
patches and which styles get a frame set, on stand-in files), `tests/keys-doc.py` (docs/KEYS.md lists every default of luxwm's
key table, and the Super keys it calls free are unbound), `build/tests/filetypestest` (the File Types page's model on a scratch tree:
the extensions, where a default comes from, Change and Restore, the actions' copy, the user's extensions), `tests/mime.py` (which program opens a type on a scratch XDG tree
with a distribution, lhde's `lhde-mimeapps.list` and a home: lhde-mime's verdicts against the spec's order, flxp's reader
(`build/tests/assoctest default`), `xdg-mime query default` and `gio mime` against them when installed, `lhde-mime default` /
`reset` and `check`'s findings), and the
headless scripts under Xvfb (clipboard between two explorers, drag and drop
incl. GTK, the file dialog in both looks, the modifier keys across a focus change, typed text into the Edit control as
UTF-16 (é, a keysym outside Latin-1, an emoji as a surrogate pair: `tests/headless/typing.sh`), luxpad (`tests/headless/luxpad.sh`:
an emoji typed into a UTF-8 file with CR LF and saved byte for byte, Ln/Col, Find, Go To, Save As Unicode through the Encoding
box and back, a Latin-1 file as ANSI saved back unchanged, the unsaved-changes question, the remembered placement, a file
dropped from flxp, a run with nothing imported), wux's MessageBoxW (its buttons per type,
the default, Enter and Escape, XP's 75x23 buttons 6 px apart under classic, luna and jade4074 with the text in the message font:
tests/w32/msgbox), luxshot, luxattr: the Properties sheet's Metadata
and Advanced views driven with xdotool and checked with getfattr, and a run with nothing imported and an empty HOME, flxp's
Properties from the item menu (General first, Metadata's description, its tag list, colour, multi-line comment and Run as, two
files in one sheet with a tag added to both, a read-only file's message box)
and Add Tag, the Explorer's Filter box under jade4074 over names and attributes, skipped without that style or user xattrs,
luxwm's Alt+Tab switcher with test windows: `tests/headless/alttab.sh`, clicks outside open menus under luxwm:
`tests/headless/menus.sh`, a Win32 menu bar's pixels (plain, hot, open, inactive) under jade4074, luna and classic with
ImageMagick: `tests/headless/menubar.sh`, flxp switched luna -> jade4074 -> classic and classic -> jade4074 -> luna against a fresh
start under the last theme (icons, colours, bands, xwd and ImageMagick): `tests/headless/themeswitch.sh`, flxp's chrome with no
window manager, luxwm started, killed and under fvwm2 (the Small header without the title row, the full one, the vision header's XP
chrome; the log and the band's row in the pixels): `tests/headless/nowm.sh`, Folder Options' File Types page on a scratch home and
mime database (typing jumps, Change... writes `mimeapps.list`, Restore, Edit File Type's New... writes and Restore removes the
program's copy, New / Delete an extension): `tests/headless/filetypes.sh`, flxp-taskbar's tray (XEmbed icons in 18 px cells, a tooltip, the chevron; its own items over the hardware layer's fake tree: Brightness with a backlight, the wheel setting it), Quick Launch and taskbar menu under luxwm
(xprop, xwininfo; luxshell/tests/trayicon): `tests/headless/taskbar.sh`, every IDC_ cursor shown under the X cursor theme's name (XFixes): `tests/headless/cursors.sh`, Super+R
opening the start menu's Run dialog (luxwm, luxshell, flxp-startmenu) and settings.ini `[commands] run` instead:
`tests/headless/runkey.sh`, luxshell keeping exactly one start-menu provider when the provider's command name is not
`flxp-startmenu` (FreeBSD's for the symlink: one after 20 s, restarted once when killed, adopted by the next luxshell, stopped
for the classic bar and on exit; xprop, python3): `tests/headless/startmenu.sh`, Super+H minimizing the sidebar into the taskbar and back at once (unmapped within 300 ms, two
screenshots 50 ms apart the same), Super+S its stacking (`_NET_WM_STATE` ABOVE / BELOW), the minimized state and the mode it
restores over a luxshell restart, `[sidebar] animate = 1` sliding both bars, a tile's hover band (with
the pack's images): `tests/headless/sidebar.sh`, luxshell's menu shadow against luxwm's window menu's, alpha per pixel over the right and bottom
strips and the corner, XP's ramp, none with `[effects] menu_shadows = 0`: `tests/headless/menushadow.sh`, luxpad's size grips (the status
bar's and the scroll bars' corner: the cursor over them, a drag sizes the window, 4074's dots under jade4074): `tests/headless/grip.sh`, the task band's folder picture against the Icons view's: `tests/headless/folderpic.sh`, flxp's memory on 600 6000 x 4000
JPEGs in the Thumbnails view at 256 px, paged through and into a subfolder: the peak RSS under 400 MB, the view's images within
their 64 MB budget plus what is on screen, some evicted, and 30 PDFs among them each made by a thumbnailer (where pdftoppm or
mutool is installed), skipped without ImageMagick: `tests/headless/thumbsmem.sh`, a PDF's, a PostScript file's and a DjVu
file's thumbnails made by lhde's thumbnailer entries, in the cache and on screen (the PS and DjVu where gs and ddjvu are
installed; skipped without pdftoppm or mutool): `tests/headless/thumbdocs.sh`, a double
click and Enter on a file with Run as running its command: `tests/headless/runas.sh`, the window and program icons through
liblhde's lookup, counted by colour in screenshots of a scratch icon set: the Open With list's and Properties' Opens with icon
from a desktop file, luxshell's task buttons and the Alt+Tab switcher with an xterm (the class map's cmd) and Display Properties
(its display key), none generic: `tests/headless/appicons.sh`, `lhde-session xsession` as a display manager runs it, ended by
luxshell's Log Off and by `lhde-session stop` with nothing left: `tests/headless/xsession.sh`) on displays :71-:73
(`LHDE_TEST_DISPLAYS="A B C"` for others), each with a scratch HOME. Skipped when
missing: the headless tests without Xvfb/xdotool (clipboard also xclip), ut-dump
without imported visual styles (`lhde-theme find styles`: the Windows tree's `index/styles`; `LHDE_STYLES` overrides).
Then the lux side (`make check-lux`): liblhde's hardware layer over recorded trees for both systems and stub commands
(`build/liblhde/tests/hw`: batteries, backlights, the volume back ends, the OSS mute, the touchpad), luxshell builds and its
Lua parses (`luac -p`), its tray items, the power tile and the Classic Tray tile hosting them work over that layer with a
fake tree and stub system commands (`luxshell/tests/tiles.sh`), every tile is in the sidebar menu's parts list
(`luxshell/tests/parts.py`), and luxwm's frame
regression (`luxwm/tests/regress.sh`): every frame set rendered offline and compared by SHA-256 with a
baseline of this machine (`build/luxwm/regress/baseline.sha256`, written by the first run); skipped
without imported styles or when a frame set's style is missing (the free pack only: its classic renders are check-free's).
Last the free baseline (`make check-free`, `tests/check-free.sh`): the free pack or ReactOS's boot CD (`FREE_PACK=FILE`,
`LHDE_REACTOS_ISO=FILE`, `~/lib/windows_isos/ReactOS-*.iso`, or the pack `make fetch-free-pack` downloaded into
`~/.cache/lhde`, the first there; skipped without any, so a CI runs `make fetch-free-pack` first; beside the ISO the free
themes' photos from `FREE_WALLPAPERS`, and without that directory their missing wallpaper is reported, not failed) imported
once into `build/free/windows`, and with that tree
alone every shipped theme must resolve (`lhde-theme explain`, `explain --files`), classic and the ReactOS themes with
nothing missing but what ReactOS stands in for, every icon key of the icon recipes and the class map has a picture in the
free set (`lhde-theme icon --set reactos --key KEY`), and luxwm's classic frames render against a baseline of their own
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
one pack, outside the repository: `lhde-resources-free-2.zip` (10 MB), made once from ReactOS 0.4.16's boot CD and the
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
tests), the system Windows trees' `index/` (`$XDG_DATA_DIRS/lhde-windows`, below), the install's `share/lhde`, `$XDG_DATA_DIRS/lhde`, the
source tree's `share/` (liblhde/data.c, lhde/data.h; scripts through `lhde-theme find`). **When something looks wrong:**
`lhde-theme explain [THEME]` prints every visible component of the theme, the file it comes from, its tree and whether it
fell back (`[windows]` the user's tree, `[system windows]` a system one); `lhde-theme dirs` shows the trees with their sizes.

## Running

    lhde-session start [THEME]               a real X session (startx on :1, vt8; log in on that tty first)
    lhde-session start --nested [THEME]      in a Xephyr window
    lhde-session start --attach [THEME]      on an X server already running on $LHDE_DISPLAY
    lhde-session xsession [THEME]            the session a display manager runs on its server (share/xsessions/lhde.desktop)
    lhde-session theme NAME | taskbar KIND | shell on|off | picom on|off | desktop on|off | stop
    LAB=:2 session/lab.sh start; session/lab.sh shell; session/lab.sh theme jade4074; session/lab.sh shot; session/lab.sh stop

From a build tree they use `build/bin`; installed, `$PREFIX/bin`. docs/USAGE.md is the user guide (from
the x11 lab).

## A whole-system install and a display manager

lhde for every user of a machine, chosen at a graphical login: the programs in `/usr/local`, the Windows files in a system
Windows tree, and a display manager that lists the X session entry `make install` puts in `$PREFIX/share/xsessions/lhde.desktop`
(`Exec=$BINDIR/lhde-session xsession`, `DesktopNames=lhde`).

**The session.** `lhde-session xsession [THEME]` is what the display manager runs on the X server it started: the session of
`lhde-session start` (the root wallpaper, luxwm in its loop, picom when the theme wants it, luxshell, flxp-desktop; no terminal
unless `LHDE_TERMINAL` names one) in the foreground on its `$DISPLAY`, with `XDG_CURRENT_DESKTOP=lhde`, `DESKTOP_SESSION=lhde`,
`LHDE_DISPLAY` and a D-Bus session bus of its own when the login brings none. Log Off (luxshell's, the start panel's) runs
`lhde-session stop`, which ends every program the session started and its bus; `lhde-session xsession` then returns and the
display manager ends the server and shows the greeter again. Ctrl+Alt+Backspace and the server itself are the display manager's.
`tests/headless/xsession.sh` runs it on an Xvfb as a display manager would.

**The system Windows tree.** After the user's own tree (`~/.local/share/lhde-windows`), every lhde program reads
`$XDG_DATA_DIRS/lhde-windows` (default `/usr/local/share/lhde-windows`, then `/usr/share/lhde-windows`), by their `index/`: a
file only the system tree has is found there, a user's copy of it wins, and generated pixels come from the first tree that has
them. Root fills it, with lhde-import pointed at it by `LHDE_WINDOWS`; users only read it. A user's
`lhde-import` (a `--style` of their own, another pack, `--regenerate`) writes and regenerates their own tree only. With
`LHDE_WINDOWS` or `LHDE_DATA` set, that one tree is the only Windows tree (tests, root's import). `lhde-theme dirs` lists the
system trees under "Windows files" and in the search order (`--system`: only them); `lhde-theme explain` marks a file of theirs
`[system windows]`.

**The display manager: LightDM with lightdm-gtk-greeter.** Of what FreeBSD 15's packages offer (`lightdm` with
`lightdm-gtk-greeter`, `slick-greeter` or `lightdm-kde-greeter`; `sddm`, `ly`, `slim`, `xdm`; no greetd, gtkgreet, tuigreet or
emptty), LightDM is the small one that reads `/usr/local/share/xsessions`, remembers each user's last session and has an rc.d
script, and lightdm-gtk-greeter is its greeter: the package's default greeter (`/usr/local/share/xgreeters/default.desktop`) is
a link to `lightdm-gtk-greeter.desktop`, it needs nothing but GTK 3, and its look is a centred login box over a colour or a
picture with a small panel (the session chooser, the clock, power), all set in one file. slick-greeter looks finer but brings
Python, PyGObject, the Ubuntu font and libcanberra, and its look is Linux Mint's; sddm needs Qt and a theme; ly is a text-mode
login; slim is unmaintained; xdm has no session chooser. Neither LightDM nor Xorg needs seatd on FreeBSD (that is for Wayland
compositors); LightDM needs the system D-Bus (ConsoleKit2 and AccountsService, which it depends on, are reached over it).

**The recipe for soma** (FreeBSD 15.1; lightdm 1.33.1 installed, no greeter yet; the source tree `~/src/lhde-port`, built as
aap). From the Linux machine, the two packs (`lhde_winthemes.zip` already holds `reactos/` and `free/`; the free pack adds the
whole ReactOS import):

    scp /u/aap/src/lhde/lhde-resources-free-2.zip /u/aap/src/lhde/lhde_winthemes.zip soma:src/lhde-port/

As aap on soma, the build for `/usr/local` (flxp has `DATADIR` compiled in, so a build for another `PREFIX` would be
rebuilt by the install; built here, root only copies and no root-owned file lands in the build tree; the next `PREFIX=~/.local
gmake install` rebuilds those few files back):

    cd ~/src/lhde-port && PATH=$HOME/src/lhde-port/bsd_bin:$PATH PREFIX=/usr/local gmake -j8

As root (`su -`), in this order:

    pkg install lightdm-gtk-greeter
    cd /u/aap/src/lhde-port && PREFIX=/usr/local gmake install
    ls /usr/local/share/xsessions/lhde.desktop
    export LHDE_WINDOWS=/usr/local/share/lhde-windows
    lhde-import --pack /u/aap/src/lhde-port/lhde-resources-free-2.zip
    lhde-import --pack /u/aap/src/lhde-port/lhde_winthemes.zip
    lhde-import --list
    unset LHDE_WINDOWS

(`gmake install-resources-full FREE_PACK=lhde-resources-free-2.zip PACK=lhde_winthemes.zip LHDE_WINDOWS=/usr/local/share/lhde-windows`
does the same import from the source tree's `tools/`, after deleting that tree, but leaves root-owned Python caches in the
tree.) Then LightDM's configuration, in `/usr/local/etc/lightdm/lightdm.conf` under `[Seat:*]` (the package's file already has
`session-wrapper=/usr/local/etc/lightdm/Xsession`, and `minimum-vt=9` under `[LightDM]`, so the greeter's server is on vt9,
clear of a startx session on vt8):

    [Seat:*]
    greeter-session=lightdm-gtk-greeter
    user-session=lhde
    session-wrapper=/usr/local/etc/lightdm/Xsession

and the greeter's, `/usr/local/etc/lightdm/lightdm-gtk-greeter.conf` (made from its `.sample` by the package):

    [greeter]
    background=#3a6ea5
    user-background=false
    theme-name=Adwaita
    icon-theme-name=Adwaita
    font-name=Sans 11
    hide-user-image=true
    clock-format=%a %e %b  %H:%M
    indicators=~host;~spacer;~clock;~spacer;~session;~power
    position=50%,center 50%,center

(`background=/usr/local/share/lhde-windows/5048/wallpapers/Longhorn.jpg` puts 5048's wallpaper behind the box instead of
Windows Classic's desktop colour.) `lightdm --show-config`, as any user, prints the merged configuration and the files it came
from, without starting anything. Then the services:

    sysrc dbus_enable=YES lightdm_enable=YES
    service dbus start
    service lightdm start

The greeter appears on vt9 (Ctrl+Alt+F9) with "lhde" chosen in its session menu; it is where Log Off returns. The console
session started with `lhde-session start` stays on vt8 (Ctrl+Alt+F8), the text consoles on Ctrl+Alt+F1 and up. Back to the
console only: `service lightdm stop` (from a text console or ssh; it ends the greeter's server and any session it runs) and
`sysrc lightdm_enable=NO`, so it does not start at boot. A user who logs in for the first time gets the system tree's themes:
`pw useradd -n guest -m -G video -s /bin/sh && passwd guest` makes one to try the out-of-the-box session with.

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
