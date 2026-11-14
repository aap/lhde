#!/bin/sh
# make check: the tests that run without Microsoft files, from the top of the
# tree after make. Each prints ok/FAIL/skip; the exit status is the number of
# failures. Tests that need assets skip when these are absent:
#   ut-dump      the visual styles in $LHDE_STYLES (default: lhde-theme find styles, the
#                Windows tree's index/styles)
#   headless/*   Xvfb and xdotool (clipboard.sh also xclip, luxshot.sh, luxattr.sh and properties.sh getfattr and setfattr, luxpad.sh python3, filter.sh and runas.sh setfattr
#                and the jade4074 style, alttab.sh xprop, xwininfo and ImageMagick, sidebar.sh xprop, xwininfo, xwd and ImageMagick, startmenu.sh xprop and python3, menushadow.sh
#                xwininfo, xwd, xsetroot, xterm, python3 and ImageMagick,
#                thumbsmem.sh ImageMagick, nowm.sh xprop, xwininfo and ImageMagick, fvwm2 when installed, xsession.sh xprop and
#                dbus-daemon, picom when installed); displays :71-:73,
#                LHDE_TEST_DISPLAYS="A B C" for others
cd "$(dirname "$0")/.." || exit 1
fails=0
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
result() {   # NAME STATUS LOG
    if [ "$2" = 0 ]; then echo "ok    $1"; else echo "FAIL  $1 (exit $2; log below)"; sed 's/^/      /' "$3" | tail -n 20; fails=$((fails + 1)); fi
}
skip() { echo "skip  $1: $2"; }
have() { command -v "$1" >/dev/null 2>&1; }

# the shim's kernel/CRT/registry/resources/usp10 tests
build/tests/w32/kerneltest build/tests/w32 "$T" > "$T/kerneltest.log" 2>&1
result "kerneltest ($(tail -n 1 "$T/kerneltest.log"))" $? "$T/kerneltest.log"
# GDI semantics drawn into a DIB section
build/tests/w32/gditest "$T/gditest.ppm" > "$T/gditest.log" 2>&1
result gditest $? "$T/gditest.log"
# the copy/move engine (in fresh directories under $TMPDIR and /dev/shm)
build/tests/fileops_test > "$T/fileops.log" 2>&1
result "fileops_test ($(tail -n 1 "$T/fileops.log"))" $? "$T/fileops.log"
# the copy/move dialogs' texts against XP's (sizes, dates, CopyError's messages)
build/tests/w32/fileopuitest > "$T/fileopui.log" 2>&1
result "fileopuitest ($(tail -n 1 "$T/fileopui.log"))" $? "$T/fileopui.log"
# flxp's lookups in lhde's data directories (shell/lhdata.h) on a fake import
build/tests/lhdatatest > "$T/lhdata.log" 2>&1
result "lhdatatest ($(tail -n 1 "$T/lhdata.log"))" $? "$T/lhdata.log"
# the Explorer's view model (shell/viewmodel.h): views, sizes, 4074's slider stops, memory
build/tests/viewmodeltest > "$T/viewmodel.log" 2>&1
result "viewmodeltest ($(tail -n 1 "$T/viewmodel.log"))" $? "$T/viewmodel.log"
# a colour swatch as an icon (the Details view's Color column) through an image list
if [ -n "$DISPLAY" ]; then   # an image list needs the shim's screen
    build/tests/w32/swatchtest > "$T/swatch.log" 2>&1
    result "swatchtest ($(tail -n 1 "$T/swatch.log"))" $? "$T/swatch.log"
else skip swatchtest "no DISPLAY"; fi
# the details model (shell/details.h): XP's Details lines, 4051's band values
build/tests/detailstest > "$T/details.log" 2>&1
result "detailstest ($(tail -n 1 "$T/details.log"))" $? "$T/details.log"
# the Details columns' layout (shell/columns.h) and the per-folder records of flxp-views, on a scratch state directory
build/tests/columnstest > "$T/columns.log" 2>&1
result "columnstest ($(tail -n 1 "$T/columns.log"))" $? "$T/columns.log"
# the thumbnails' memory bounds (shell/thumbs.h, gdi/image.h): big pictures into small thumbnails without their full image
build/tests/thumbstest > "$T/thumbs.log" 2>&1
result "thumbstest ($(tail -n 1 "$T/thumbs.log"))" $? "$T/thumbs.log"
# lhde-import's overwrite policy (kept without --force, written with it) on a scratch tree and a stand-in file
tests/import-policy.sh > "$T/import.log" 2>&1
result "import-policy ($(tail -n 1 "$T/import.log"))" $? "$T/import.log"
# Display Properties' Desktop lists (flxp-settings --list desktop, no window) on a scratch home and Windows tree
tests/settings-lists.sh > "$T/settings-lists.log" 2>&1
result "settings-lists ($(tail -n 1 "$T/settings-lists.log"))" $? "$T/settings-lists.log"
# lhde-import's Aero patches (by sha256, the unknown one by its bytes) and which dropped-in styles get a frame set, stand-in files
python3 -I tests/aero-patches.py > "$T/aero-patches.log" 2>&1
result "aero-patches ($(tail -n 1 "$T/aero-patches.log"))" $? "$T/aero-patches.log"
# docs/KEYS.md against luxwm's default key table: every default listed, the Super keys it calls free unbound
python3 -I tests/keys-doc.py > "$T/keys-doc.log" 2>&1
result "keys-doc ($(tail -n 1 "$T/keys-doc.log"))" $? "$T/keys-doc.log"
# Folder Options' File Types model: the extensions, where a default comes from, Change / Restore, the actions' copy, New / Delete
build/tests/filetypestest > "$T/filetypes.log" 2>&1
result "filetypestest ($(tail -n 1 "$T/filetypes.log"))" $? "$T/filetypes.log"
# which program opens a type: lhde-mime's verdicts, flxp's reader (assoctest), xdg-mime and gio on a scratch XDG tree; check
python3 -I tests/mime.py > "$T/mime.log" 2>&1
result "mime ($(tail -n 1 "$T/mime.log"))" $? "$T/mime.log"
# libwuxtheme through its own API: every style the user imported
styles=${LHDE_STYLES:-$(build/bin/lhde-theme find styles 2>/dev/null)}   # liblhde's lookup: the Windows tree's index/styles (docs/LAYOUT.md)
n=0
for f in "$styles"/*.msstyles "$styles"/*/*.msstyles; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    build/bin/ut-dump "$f" --list > "$T/ut-dump.log" 2>&1; st=$?
    # the true size of every frame part without a rect, as luxwm asks for it (a part absent or without a size exits 1; a crash,
    # as on IMAGESELECTTYPE = SIZE without a rect, exits 139)
    for p in CAPTION MAXCAPTION SMALLCAPTION FRAMELEFT FRAMERIGHT FRAMEBOTTOM CLOSEBUTTON MAXBUTTON MINBUTTON RESTOREBUTTON HELPBUTTON \
             SMALLCLOSEBUTTON; do
        [ $st = 0 ] || break
        build/bin/ut-dump "$f" --size-of WINDOW $p 1 true >> "$T/ut-dump.log" 2>&1; r=$?
        [ $r -le 1 ] || { echo "ut-dump --size-of WINDOW $p 1 true: exit $r" >> "$T/ut-dump.log"; st=$r; }
    done
    result "ut-dump $(basename "$f")" $st "$T/ut-dump.log"
done
[ $n = 0 ] && skip ut-dump "no visual styles in ${styles:-the Windows tree (nothing imported: tools/lhde-import)}"

# the programs under Xvfb
set -- ${LHDE_TEST_DISPLAYS:-:71 :72 :73}
if ! have Xvfb || ! have xdotool; then
    skip headless "needs Xvfb and xdotool"
else
    if have xclip; then
        tests/headless/clipboard.sh "$1" > "$T/clipboard.log" 2>&1; result headless/clipboard.sh $? "$T/clipboard.log"
    else
        skip headless/clipboard.sh "needs xclip"
    fi
    tests/headless/dnd.sh "$2" > "$T/dnd.log" 2>&1; result headless/dnd.sh $? "$T/dnd.log"
    tests/headless/lvtiles.sh "$2" > "$T/lvtiles.log" 2>&1; result "headless/lvtiles.sh ($(tail -n 1 "$T/lvtiles.log"))" $? "$T/lvtiles.log"
    tests/headless/modkeys.sh "$1" > "$T/modkeys.log" 2>&1; result headless/modkeys.sh $? "$T/modkeys.log"
    tests/headless/typing.sh "$3" > "$T/typing.log" 2>&1; result headless/typing.sh $? "$T/typing.log"
    tests/headless/msgbox.sh "$3" > "$T/msgbox.log" 2>&1; result headless/msgbox.sh $? "$T/msgbox.log"
    tests/headless/filedlg.sh "$3" > "$T/filedlg.log" 2>&1; result headless/filedlg.sh $? "$T/filedlg.log"
    tests/headless/filter.sh "$2" > "$T/filter.log" 2>&1; rc=$?
    result "headless/filter.sh$(grep -m1 '^skip' "$T/filter.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/filter.log"
    tests/headless/alttab.sh "$2" > "$T/alttab.log" 2>&1; rc=$?
    result "headless/alttab.sh$(grep -m1 '^skip' "$T/alttab.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/alttab.log"
    # a click outside an open menu ends it and acts where it was made (docs/CLIENT-PROTOCOL.md "Menus and clicks"), under luxwm
    tests/headless/menus.sh "$3" > "$T/menus.log" 2>&1; result headless/menus.sh $? "$T/menus.log"
    # every IDC_ cursor as the X cursor theme's double arrows and kin (XFixes reads back the one shown)
    tests/headless/cursors.sh "$2" > "$T/cursors.log" 2>&1; result headless/cursors.sh $? "$T/cursors.log"
    # the size grip in luxpad's status bar and its scroll bars' corner: 4074's dots under jade4074, the double arrow, a drag sizes
    tests/headless/grip.sh "$3" > "$T/grip.log" 2>&1; rc=$?
    result "headless/grip.sh$(grep -m1 '^skip' "$T/grip.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/grip.log"
    # Folder Options' File Types page: typing jumps, Change... writes mimeapps.list, Restore, Edit File Type's New..., New / Delete
    tests/headless/filetypes.sh "$2" > "$T/filetypes-page.log" 2>&1; rc=$?
    result "headless/filetypes.sh$(grep -m1 '^skip' "$T/filetypes-page.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/filetypes-page.log"
    # flxp's chrome without luxwm, with luxwm, after it, under fvwm2 (the Small header, the full one; the vision header: XP)
    tests/headless/nowm.sh "$1" > "$T/nowm.log" 2>&1; rc=$?
    result "headless/nowm.sh$(grep -m1 '^skip' "$T/nowm.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/nowm.log"
    # Super+R: the start menu's Run dialog through luxwm, luxshell and flxp-startmenu; [commands] run instead when set
    tests/headless/runkey.sh "$1" > "$T/runkey.log" 2>&1; rc=$?
    result "headless/runkey.sh$(grep -m1 '^skip' "$T/runkey.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/runkey.log"
    # one start-menu provider under luxshell, also when its command name is flxp-taskbar (FreeBSD's for the flxp-startmenu symlink)
    tests/headless/startmenu.sh "$2" > "$T/startmenu.log" 2>&1; rc=$?
    result "headless/startmenu.sh$(grep -m1 '^skip' "$T/startmenu.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/startmenu.log"
    # Super+H hides and shows the sidebar at once, Super+S flips its stacking, the state survives a restart; [sidebar] animate = 1
    tests/headless/sidebar.sh "$2" > "$T/sidebar.log" 2>&1; rc=$?
    result "headless/sidebar.sh$(grep -m1 '^skip' "$T/sidebar.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/sidebar.log"
    # luxshell's menu shadow is luxwm's (XP's 5 px, ut_menu_shadow_alpha), none with [effects] menu_shadows = 0
    tests/headless/menushadow.sh "$3" > "$T/menushadow.log" 2>&1; rc=$?
    result "headless/menushadow.sh$(grep -m1 '^skip' "$T/menushadow.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/menushadow.log"
    # a Win32 program's menu bar under jade4074 (4074's themed bar), luna (XP's flat bar) and classic, read back at 4074's and XP's pixel values
    tests/headless/menubar.sh "$1" > "$T/menubar.log" 2>&1; rc=$?
    result "headless/menubar.sh$(grep -m1 '^skip' "$T/menubar.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/menubar.log"
    # flxp switched luna -> jade4074 -> classic and back looks as a fresh start under the last theme (icons, colours, bands)
    tests/headless/themeswitch.sh "$2" > "$T/themeswitch.log" 2>&1; rc=$?
    result "headless/themeswitch.sh$(grep -m1 '^skip' "$T/themeswitch.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/themeswitch.log"
    # flxp-taskbar under luxwm: XEmbed icons in 18 px cells, their tooltip, the chevron, Quick Launch, the taskbar menu's Show the Desktop
    tests/headless/taskbar.sh "$3" > "$T/taskbar.log" 2>&1; rc=$?
    result "headless/taskbar.sh$(grep -m1 '^skip' "$T/taskbar.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/taskbar.log"
    # the task band's picture of a folder with two JPEGs is the Icons view's square (folderpictest)
    tests/headless/folderpic.sh "$2" > "$T/folderpic.log" 2>&1; result headless/folderpic.sh $? "$T/folderpic.log"
    # flxp's peak RSS on 600 6000 x 4000 JPEGs at 256 px, the view's images within their budget (needs ImageMagick)
    tests/headless/thumbsmem.sh "$1" > "$T/thumbsmem.log" 2>&1; rc=$?
    result "headless/thumbsmem.sh ($(tail -n 1 "$T/thumbsmem.log"))" $rc "$T/thumbsmem.log"
    tests/headless/luxshot.sh "$1" > "$T/luxshot.log" 2>&1; rc=$?
    result "headless/luxshot.sh$(grep -m1 '^skip' "$T/luxshot.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/luxshot.log"
    tests/headless/luxattr.sh "$2" > "$T/luxattr.log" 2>&1; rc=$?
    result "headless/luxattr.sh$(grep -m1 '^skip' "$T/luxattr.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/luxattr.log"
    tests/headless/luxpad.sh "$3" > "$T/luxpad.log" 2>&1; rc=$?
    result "headless/luxpad.sh$(grep -m1 '^skip' "$T/luxpad.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/luxpad.log"
    tests/headless/properties.sh "$3" > "$T/properties.log" 2>&1; rc=$?
    result "headless/properties.sh$(grep -m1 '^skip' "$T/properties.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/properties.log"
    # the window and program icons (lhde_app_icon): Open With, Properties' Opens with, luxshell's task buttons, Alt+Tab
    tests/headless/appicons.sh "$1" > "$T/appicons.log" 2>&1; rc=$?
    result "headless/appicons.sh$(grep -m1 '^skip' "$T/appicons.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/appicons.log"
    # a double click on a file with user.lux.run runs that command
    tests/headless/runas.sh "$1" > "$T/runas.log" 2>&1; rc=$?
    result "headless/runas.sh$(grep -m1 '^skip' "$T/runas.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/runas.log"
    # lhde-session xsession as a display manager runs it: up, ended by luxshell's Log Off and by lhde-session stop, nothing left
    tests/headless/xsession.sh "$2" > "$T/xsession.log" 2>&1; rc=$?
    result "headless/xsession.sh$(grep -m1 '^skip' "$T/xsession.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/xsession.log"
fi

[ $fails = 0 ] && echo "check: all passed" || echo "check: $fails failed"
exit $fails
