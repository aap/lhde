#!/bin/sh
# The caption buttons luxwm gives wux's windows, from the styles wux turns into _MOTIF_WM_HINTS functions: a message box
# (tests/w32/msgbox) and a property sheet (luxattr's) have Close alone, as XP's; luxpad's modeless Find dialog too; a
# WS_OVERLAPPEDWINDOW (tests/w32/keystate) has minimise, maximise and Close. Under Windows Classic (no imported files
# needed): the caption row half way down is read from a screenshot of the root, and the buttons counted by their right
# edge, the one 3DDKSHADOW column (64,64,64) each DrawFrameControl button has, over the 60 pixels of the caption above
# the client's right end. Also the _MOTIF_WM_HINTS wux wrote. Needs Xvfb, xdotool, xprop, xwininfo and ImageMagick.
# Usage: tests/headless/capbuttons.sh [:DISPLAY]
D=${1:-:78}
R=$(cd "$(dirname "$0")/../.." && pwd)
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool xprop xwininfo import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config/lhde $T/files
printf '[theme]\nname = classic\n' > $T/home/.config/lhde/settings.ini
echo x > $T/files/a.txt
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=/nonexistent
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; rm -rf $T; exit 1; }
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1: $3"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
xprop -root -f _LH_THEME 8u -set _LH_THEME classic
sleep .5
# buttons WINDOW: the caption buttons over the client window's caption (10 rows above the client: the classic caption's middle)
buttons() {
    eval "$(xwininfo -id $1 | sed -n 's/^ *Absolute upper-left X: *\([0-9-]*\)/X=\1/p; s/^ *Absolute upper-left Y: *\([0-9-]*\)/Y=\1/p; s/^ *Width: *\([0-9]*\)/CW=\1/p')"
    sleep .5; import -window root $T/root.png 2>/dev/null
    $IM $T/root.png -crop 60x1+$((X + CW - 60))+$((Y - 10)) +repage -depth 8 txt:- | grep -c '(64,64,64)'
}
# mwm WINDOW: the functions word of _MOTIF_WM_HINTS (none: no property)
mwm() { xprop -id $1 _MOTIF_WM_HINTS 2>/dev/null | sed -n 's/^_MOTIF_WM_HINTS([^)]*) = *//p' | cut -d, -f2 | tr -d ' ' | grep . || echo none; }
# run NAME PATTERN EXPECTED-BUTTONS EXPECTED-FUNCTIONS COMMAND...
run() {
    _n=$1; _p=$2; _b=$3; _f=$4; shift 4
    "$@" > $T/out 2>$T/err & P=$!
    W=$(wait_window 30 --name "$_p")
    if [ -z "$W" ]; then echo "FAIL $_n: no window"; fails=$((fails + 1)); else
        sleep .5
        check "$_n: caption buttons" "$_b" "$(buttons $W)"
        check "$_n: _MOTIF_WM_HINTS functions" "$_f" "$(mwm $W)"
    fi
    kill $P 2>/dev/null; wait $P 2>/dev/null
}
# MOVE | CLOSE = 36 (0x24); with RESIZE 38
run "message box" '^Box$' 1 0x24 "$R/build/tests/w32/msgbox" ok Box "Close alone"
run "property sheet" '^a.txt Properties$' 1 0x24 "$R/build/bin/luxattr" $T/files/a.txt
run "overlapped window" '^keystate$' 3 none "$R/build/tests/w32/keystate"
# luxpad's Find (owned, modeless)
"$R/build/bin/luxpad" $T/files/a.txt > /dev/null 2>&1 & PL=$!
WL=$(wait_window 30 --name 'a.txt - luxpad')
if [ -n "$WL" ]; then
    timeout 10 xdotool windowactivate --sync $WL 2>/dev/null; sleep .3; xdotool key ctrl+f
    WF=$(wait_window 20 --name '^Find$')
    if [ -n "$WF" ]; then sleep .5; check "luxpad's Find: caption buttons" 1 "$(buttons $WF)"
    else echo "FAIL luxpad's Find did not show"; fails=$((fails + 1)); fi
else echo "FAIL no luxpad window"; fails=$((fails + 1)); fi
kill $PL 2>/dev/null; wait $PL 2>/dev/null
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
