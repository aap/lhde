#!/bin/sh
# Typed text reaches the Edit control as the right UTF-16 and back as the right UTF-8, without an input method: X gives a
# keysym, wux makes it UTF-8 text (XLookupString alone answers in Latin-1 or not at all), and TranslateMessage posts one
# WM_CHAR per UTF-16 code unit (a character above U+FFFF as two, high surrogate first) which the edit stores as the pair.
# First build/tests/w32/edittext --self (the WM_CHARs sent directly), then xdotool types ASCII, é (Latin-1's range), ж
# (a keysym outside Latin-1) and 😀 (U+1F600, a Unicode keysym) into edittext's window, which prints its text as hex; then
# Ctrl+Backspace and Ctrl+Delete delete a word (lhde's Edit; XP's inserted a DEL character for Ctrl+Backspace).
# Needs Xvfb and xdotool. Usage: tests/headless/typing.sh [:DISPLAY]
D=${1:-:75}
T=$(mktemp -d)
B=$(cd "$(dirname "$0")/../.." && pwd)/build/tests/w32/edittext
mkdir -p $T/home/.config
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=/nonexistent
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
check() {   # check NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
timeout 30 "$B" --self > $T/self.log 2>&1 || fails=$((fails + 1))
cat $T/self.log
"$B" typing > $T/out.log 2>$T/err.log & P=$!
W=$(wait_window 30 --name '^typing$')
last() { tail -n 1 $T/out.log | sed 's/^text //'; }
is() { [ "$(last)" = "$1" ]; }
if [ -z "$W" ]; then
    echo "FAIL the edittext window did not show"; cat $T/err.log; fails=$((fails + 1))
else
    timeout 10 xdotool windowfocus --sync "$W" 2>/dev/null; sleep .2
    xdotool type --delay 50 'a'; wait_for 10 is 61
    check "ASCII" 61 "$(last)"
    xdotool type --delay 50 'é'; wait_for 10 is 61c3a9
    check "e-acute: c3a9, not the Latin-1 byte" 61c3a9 "$(last)"
    xdotool type --delay 50 'ж'; wait_for 10 is 61c3a9d0b6
    check "zhe (a keysym outside Latin-1): d0b6" 61c3a9d0b6 "$(last)"
    xdotool type --delay 50 '😀'; wait_for 10 is 61c3a9d0b6f09f9880
    check "U+1F600 typed: f09f9880 (two WM_CHARs, one pair)" 61c3a9d0b6f09f9880 "$(last)"
    xdotool key BackSpace; wait_for 10 is 61c3a9d0b6
    check "Backspace takes the whole pair" 61c3a9d0b6 "$(last)"
    # Ctrl+Backspace deletes the word before the caret, Ctrl+Delete the one after, to where Ctrl+Left / Ctrl+Right go
    xdotool key ctrl+a; xdotool type --delay 30 'alpha beta gamma'; wait_for 10 is 616c70686120626574612067616d6d61
    xdotool key ctrl+BackSpace; wait_for 10 is 616c706861206265746120
    check "Ctrl+Backspace: 'gamma' gone" 616c706861206265746120 "$(last)"
    xdotool key ctrl+BackSpace; wait_for 10 is 616c70686120
    check "Ctrl+Backspace again: 'beta ' gone (the word and the space after it)" 616c70686120 "$(last)"
    xdotool type --delay 30 'beta'; wait_for 10 is 616c7068612062657461
    xdotool key Home ctrl+Delete; wait_for 10 is 62657461
    check "Ctrl+Delete at the start: 'alpha ' gone" 62657461 "$(last)"
    xdotool key End ctrl+Delete ctrl+BackSpace ctrl+BackSpace; sleep .5
    check "Ctrl+Delete at the end does nothing, Ctrl+Backspace empties it, no DEL inserted" "" "$(last)"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null; stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
