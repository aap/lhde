#!/bin/sh
# Leaving fullscreen brings luxwm's frame back at once: a window (tests/w32/keystate, Windows Classic) is toggled to fullscreen
# and back (_NET_WM_STATE_FULLSCREEN, xdotool windowstate), and the screenshots taken right after it left fullscreen (xwd of the
# root, a few over 400 ms) show the caption row exactly as before (no frame-less or half-faded frame: luxwm keeps the decoration
# window mapped while fullscreen instead of mapping a new one, which picom would fade in). With picom (xrender, the session's
# picom-glass.conf) when installed, since only a compositor animates; without it the check is weaker and says so.
# Needs Xvfb, xdotool, xprop, xwininfo, xwd and ImageMagick. Usage: tests/headless/fullscreen.sh [:DISPLAY]
D=${1:-:79}
R=$(cd "$(dirname "$0")/../.." && pwd)
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool xprop xwininfo xwd $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config/lhde
printf '[theme]\nname = classic\n' > $T/home/.config/lhde/settings.ini
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=/nonexistent
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; rm -rf $T; exit 1; }
fails=0
fail() { echo "FAIL $*"; fails=$((fails + 1)); }
PP=
if command -v picom >/dev/null 2>&1; then
    picom --config "$R/session/picom-glass.conf" --backend xrender --blur-method gaussian --blur-size 5 --blur-deviation 1.2 \
        > $T/picom.log 2>&1 & PP=$!
    sleep 1
    kill -0 $PP 2>/dev/null && echo "note: picom runs" || { PP=; echo "note: picom did not start: no fade to catch"; }
else
    echo "note: no picom: no fade to catch, only the frame's return is checked"
fi
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
xdotool mousemove 0 0
"$R/build/tests/w32/keystate" > /dev/null 2>&1 & PK=$!
W=$(wait_window 30 --name '^keystate$')
fs() { xprop -id $W _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_FULLSCREEN; }
notfs() { ! fs; }
# caption FILE: the caption row 10 pixels above the client (the classic caption's middle) in an xwd of the root, as text
caption() { $IM xwd:$1 -crop ${CW}x1+$X+$((Y - 10)) +repage -depth 8 txt:- 2>/dev/null | sed 1d | md5sum | cut -d' ' -f1; }
if [ -z "$W" ]; then fail "no keystate window"
else
    sleep 1.5   # its open animation over
    eval "$(xwininfo -id $W | sed -n 's/^ *Absolute upper-left X: *\([0-9-]*\)/X=\1/p; s/^ *Absolute upper-left Y: *\([0-9-]*\)/Y=\1/p; s/^ *Width: *\([0-9]*\)/CW=\1/p')"
    xwd -root -silent > $T/before.xwd
    ref=$(caption $T/before.xwd)
    echo "keystate at $X,$Y, ${CW} wide; caption row $ref"
    for round in 1 2; do
        xdotool windowstate --toggle FULLSCREEN $W
        wait_for 5 fs || fail "round $round: keystate did not go fullscreen"
        sleep 1
        xdotool windowstate --toggle FULLSCREEN $W
        wait_for 5 notfs || fail "round $round: keystate did not leave fullscreen"
        for i in 1 2 3 4 5; do xwd -root -silent > $T/after$i.xwd; sleep .08; done
        for i in 1 2 3 4 5; do
            got=$(caption $T/after$i.xwd)
            [ "$got" = "$ref" ] && echo "ok   round $round, shot $i: the caption row is back" ||
                { fail "round $round, shot $i after leaving fullscreen: the caption row differs"; }
        done
        sleep .5
    done
fi
kill $PK $PW $PP 2>/dev/null; wait $PK $PW $PP 2>/dev/null
stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
