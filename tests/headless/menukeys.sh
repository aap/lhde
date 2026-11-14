#!/bin/sh
# A Win32 menu bar from the keyboard (Wine's user32 menus in wux), tests/w32/menutest under Xvfb without a window manager,
# with the focus on the window and on an Edit control in it (MENUTEST_EDIT=1): Alt+F drops File at once (its
# WM_INITMENUPOPUP, a popup window shows), Escape twice leaves; Alt+V drops View; F10 alone enters the bar on File without
# dropping it (no popup), Right then Down drops Edit; F10, Down, Enter chooses New (WM_COMMAND 101); Alt+F then N too.
# Needs Xvfb and xdotool. Usage: tests/headless/menukeys.sh [:DISPLAY]
D=${1:-:76}
B=$(cd "$(dirname "$0")/../.." && pwd)/build/tests/w32/menutest
for t in Xvfb xdotool; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; rm -rf $T; exit 1; }
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
wins() { xdotool search --onlyvisible --name '.*' 2>/dev/null | wc -l | tr -d ' '; }
more() { [ "$(wins)" -gt "$N0" ]; }
back() { [ "$(wins)" -eq "$N0" ]; }
logged() { grep -q "^$1\$" $T/err; }
# keys KEYS...: sent one at a time to the focused window
keys() { for k in "$@"; do xdotool key "$k"; sleep .3; done; }
for where in window edit; do
    : > $T/err
    if [ $where = edit ]; then MENUTEST_EDIT=1 "$B" 2>> $T/err & P=$!; else "$B" 2>> $T/err & P=$!; fi
    W=$(wait_window 30 --name '^flxp menus$')
    if [ -z "$W" ]; then echo "FAIL $where: no menutest window"; fails=$((fails + 1)); kill $P; wait $P 2>/dev/null; continue; fi
    timeout 10 xdotool windowfocus --sync $W 2>/dev/null; sleep .5
    N0=$(wins)
    keys alt+f; wait_for 5 more
    check "$where: Alt+F drops File" "WM_INITMENUPOPUP 0 yes" "$(grep '^WM_INITMENUPOPUP' $T/err | tail -1) $(more && echo yes)"
    keys Escape Escape; wait_for 5 back
    check "$where: Escape twice leaves it" "yes" "$(back && echo yes)"
    : > $T/err; keys alt+v; wait_for 5 more
    check "$where: Alt+V drops View" "WM_INITMENUPOPUP 2 yes" "$(grep '^WM_INITMENUPOPUP' $T/err | tail -1) $(more && echo yes)"
    keys Escape Escape; wait_for 5 back
    : > $T/err; keys F10; sleep .5
    check "$where: F10 enters the bar without dropping" "no" "$(more && echo yes || echo no)$(grep '^WM_INITMENUPOPUP' $T/err)"
    keys Right Down; wait_for 5 more
    check "$where: F10, Right, Down drops Edit" "WM_INITMENUPOPUP 1 yes" "$(grep '^WM_INITMENUPOPUP' $T/err | tail -1) $(more && echo yes)"
    keys Escape Escape; wait_for 5 back
    : > $T/err; keys F10 Down Return; wait_for 5 logged "WM_COMMAND 101"
    check "$where: F10, Down, Enter chooses New" "WM_COMMAND 101" "$(grep '^WM_COMMAND' $T/err)"
    : > $T/err; keys alt+f n; wait_for 5 logged "WM_COMMAND 101"
    check "$where: Alt+F, N chooses New" "WM_COMMAND 101" "$(grep '^WM_COMMAND' $T/err)"
    wait_for 5 back
    kill $P; wait $P 2>/dev/null
done
stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
