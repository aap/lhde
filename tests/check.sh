#!/bin/sh
# make check: the tests that run without Microsoft files, from the top of the
# tree after make. Each prints ok/FAIL/skip; the exit status is the number of
# failures. Tests that need assets skip when these are absent:
#   ut-dump      the visual styles in $LHDE_STYLES (default: lhde-theme find styles, the
#                Windows tree's index/styles)
#   headless/*   Xvfb and xdotool (clipboard.sh also xclip, luxshot.sh getfattr and setfattr); displays :71-:73,
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
# the details model (shell/details.h): XP's Details lines, 4051's band values
build/tests/detailstest > "$T/details.log" 2>&1
result "detailstest ($(tail -n 1 "$T/details.log"))" $? "$T/details.log"
# lhde-import's overwrite policy (kept without --force, written with it) on a scratch tree and a stand-in file
tests/import-policy.sh > "$T/import.log" 2>&1
result "import-policy ($(tail -n 1 "$T/import.log"))" $? "$T/import.log"
# Display Properties' Desktop lists (flxp-settings --list desktop, no window) on a scratch home and Windows tree
tests/settings-lists.sh > "$T/settings-lists.log" 2>&1
result "settings-lists ($(tail -n 1 "$T/settings-lists.log"))" $? "$T/settings-lists.log"
# lhde-import's Aero patches (by sha256, the unknown one by its bytes) on stand-in files
python3 -I tests/aero-patches.py > "$T/aero-patches.log" 2>&1
result "aero-patches ($(tail -n 1 "$T/aero-patches.log"))" $? "$T/aero-patches.log"
# libwuxtheme through its own API: every style the user imported
styles=${LHDE_STYLES:-$(build/bin/lhde-theme find styles 2>/dev/null)}   # liblhde's lookup: the Windows tree's index/styles (docs/LAYOUT.md)
n=0
for f in "$styles"/*.msstyles "$styles"/*/*.msstyles; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    build/bin/ut-dump "$f" --list > "$T/ut-dump.log" 2>&1; st=$?
    # the true size of every frame part without a rect, as luxwm asks for it (a part absent or without a size exits 1; a crash
    # on IMAGESELECTTYPE = SIZE without a rect was 139)
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
    tests/headless/filedlg.sh "$3" > "$T/filedlg.log" 2>&1; result headless/filedlg.sh $? "$T/filedlg.log"
    tests/headless/luxshot.sh "$1" > "$T/luxshot.log" 2>&1; rc=$?
    result "headless/luxshot.sh$(grep -m1 '^skip' "$T/luxshot.log" | sed 's/^/ (/; s/$/)/')" $rc "$T/luxshot.log"
fi

[ $fails = 0 ] && echo "check: all passed" || echo "check: $fails failed"
exit $fails
