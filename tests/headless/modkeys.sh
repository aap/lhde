#!/bin/sh
# The modifier state GetKeyState reports (wux's key table) when Ctrl changes while another window has the keyboard: two
# tests/w32/keystate windows, xdotool moving the focus between them. Ctrl pressed in the other window, then 1 here: the
# WM_KEYDOWN of 1 must see Ctrl down (flxp's Ctrl+1..5 view keys did nothing then); Ctrl pressed here, released in the
# other window, then 2 here: Ctrl must be up (it stayed down, and plain keys looked like Ctrl+key).
# Needs Xvfb and xdotool. Usage: tests/headless/modkeys.sh [:DISPLAY]
D=${1:-:75}
T=$(mktemp -d)
B=$(cd "$(dirname "$0")/../.." && pwd)/build/tests/w32/keystate
mkdir -p $T/home/.config
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D   # the scratch home only: nothing exported from outside leaks in
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$B" other right > $T/other.log 2>$T/other.err & P1=$!
"$B" probe > $T/probe.log 2>$T/probe.err & P2=$!
WO=$(wait_window 30 --name '^other$')
WP=$(wait_window 30 --name '^probe$')
fails=0
check() {   # check NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
focus() { timeout 10 xdotool windowfocus --sync "$1" 2>/dev/null; sleep .2; }
seen() { grep -q "^down $1 " $T/probe.log; }
line() { wait_for 10 seen "$1"; grep "^down $1 " $T/probe.log | tail -1; }
if [ -z "$WO" ] || [ -z "$WP" ]; then
    echo "FAIL the keystate windows did not show"; cat $T/other.err $T/probe.err; fails=1
else
    focus $WO; xdotool keydown ctrl; focus $WP; xdotool key 1; xdotool keyup ctrl
    check "Ctrl pressed elsewhere, then 1 here" "down 0x31 ctrl=1 shift=0 alt=0" "$(line 0x31)"
    focus $WP; xdotool keydown ctrl; line 0x11 > /dev/null; focus $WO; xdotool keyup ctrl; focus $WP; xdotool key 2
    check "Ctrl released elsewhere, then 2 here" "down 0x32 ctrl=0 shift=0 alt=0" "$(line 0x32)"
fi
kill $P1 $P2; wait $P1 $P2 2>/dev/null; stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
