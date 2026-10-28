#!/bin/sh
# make check: the tests that run without Microsoft files, from the top of the
# tree after make. Each prints ok/FAIL/skip; the exit status is the number of
# failures. Tests that need assets skip when these are absent:
#   ut-dump      the visual styles in $LHDE_STYLES (default: styles/ of the user data
#                directory, $LHDE_DATA or ${XDG_DATA_HOME:-~/.local/share}/lhde)
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
# flxp's lookups in lhde's data directories (shell/lhdata.h) on a fake import
build/tests/lhdatatest > "$T/lhdata.log" 2>&1
result "lhdatatest ($(tail -n 1 "$T/lhdata.log"))" $? "$T/lhdata.log"
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
    tests/headless/filedlg.sh "$3" > "$T/filedlg.log" 2>&1; result headless/filedlg.sh $? "$T/filedlg.log"
fi

[ $fails = 0 ] && echo "check: all passed" || echo "check: $fails failed"
exit $fails
