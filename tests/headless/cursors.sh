#!/bin/sh
# The Win32 cursors on X (wux/sys/x11.cpp): tests/w32/cursortest sets every IDC_ cursor over its window on Xvfb (no window
# manager) and reads back the one the server shows (XFixesGetCursorImage's name: the X cursor theme's name wux asked for
# first, nwse-resize for IDC_SIZENWSE, ...). Needs Xvfb and xdotool. Usage: tests/headless/cursors.sh [:DISPLAY]
D=${1:-:78}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 800x600x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
xdotool mousemove 150 100   # where the window will be
"$R/build/tests/w32/cursortest" > $T/out 2>$T/err
rc=$?
cat $T/out
[ $rc = 0 ] || tail -5 $T/err
stop_xvfb
exit $rc
