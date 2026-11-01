#!/bin/sh
# make check: the tests that run without Microsoft files, from the top of the
# tree after make. Each prints ok/FAIL/skip; the exit status is the number of
# failures. Tests that need assets skip when these are absent:
#   ut-dump      the visual styles in $LHDE_STYLES (default: lhde-theme find styles, the
#                Windows tree's index/styles)
#   headless/*   Xvfb and xdotool (clipboard.sh also xclip); displays :71-:73,
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
# libwuxtheme through its own API: every style the user imported
styles=${LHDE_STYLES:-$(build/bin/lhde-theme find styles 2>/dev/null)}   # liblhde's lookup: the Windows tree's index/styles (docs/LAYOUT.md)
n=0
for f in "$styles"/*.msstyles "$styles"/*/*.msstyles; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    build/bin/ut-dump "$f" --list > "$T/ut-dump.log" 2>&1
    result "ut-dump $(basename "$f")" $? "$T/ut-dump.log"
done
[ $n = 0 ] && skip ut-dump "no visual styles in $styles"

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
    tests/headless/filedlg.sh "$3" > "$T/filedlg.log" 2>&1; result headless/filedlg.sh $? "$T/filedlg.log"
fi

[ $fails = 0 ] && echo "check: all passed" || echo "check: $fails failed"
exit $fails
