#!/bin/sh
# A wux timer under a busy X server: tests/w32/timertick scrolls its window a step every 16 ms tick (ScrollWindowEx and a
# paint of the strip, mupf's autoscroll in small) while the server is kept busy: picom (xrender, the session's
# picom-glass.conf, its blur) when installed, another client reading the whole root in a loop (xwd), and every reply the
# program waits for made 5 ms late (tests/headless/xcalls.c's XCALLS_DELAY_MS, a server behind a compositor's frame on a real
# GPU). The ticks must come every 16 ms (mean 14..20, nine in ten under 25) and none may wait for the server: no reply between
# the first tick and the last (a message's pt is the pointer as the events left it, a present never asks), and what is
# painted reaches the window while it ticks (presents during the run, not only at its end).
# Needs Xvfb, xdotool, xwd and a C compiler's build of xcalls.so (make tests). Usage: tests/headless/timerbusy.sh [:DISPLAY]
D=${1:-:79}
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/tests/w32/timertick
SHIM=$R/build/tests/xcalls.so
for t in Xvfb xdotool xwd; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x "$B" ] && [ -f "$SHIM" ] || { echo "FAIL build/tests/w32/timertick or build/tests/xcalls.so not built (make tests)"; exit 1; }
T=$(mktemp -d)
mkdir -p $T/home/.config/lhde
printf '[theme]\nname = classic\n' > $T/home/.config/lhde/settings.ini
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=/nonexistent
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; rm -rf $T; exit 1; }
fails=0
ok() { echo "ok   $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }
PP=
if command -v picom >/dev/null 2>&1; then
    picom --config "$R/session/picom-glass.conf" --backend xrender --blur-method gaussian --blur-size 5 --blur-deviation 1.2 \
        > $T/picom.log 2>&1 & PP=$!
    sleep 1
    kill -0 $PP 2>/dev/null && echo "note: picom runs" || { PP=; echo "note: picom did not start"; }
else
    echo "note: no picom: the server is kept busy by the xwd loop alone"
fi
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
xdotool mousemove 300 300
(while :; do xwd -root -silent > /dev/null 2>&1; done) & PB=$!
# the start-up's replies are many and not the point: the delay from 0.8 s on, the ticks from 1 s (the window managed by then)
LD_PRELOAD=$SHIM XCALLS_LOG=$T/x.log XCALLS_DELAY_MS=5 XCALLS_DELAY_AFTER=800 "$B" 150 > $T/out.log 2>&1 & PT=$!
wait_for 30 grep -q '^done' $T/out.log || fail "timertick did not finish its 150 ticks ($(grep -c '^tick' $T/out.log) seen)"
kill $PB 2>/dev/null; wait $PB 2>/dev/null
# the intervals: every tick's GetTickCount
grep '^tick' $T/out.log | awk '{print $2}' > $T/ticks
read n mean slow << EOF
$(awk 'NR > 1 { d = $1 - p; n++; s += d; if (d >= 25) slow++ } { p = $1 } END { printf "%d %.1f %d\n", n, n ? s / n : 0, slow }' $T/ticks)
EOF
echo "note: $n intervals, mean $mean ms, $slow of them 25 ms or more"
awk -v m=$mean 'BEGIN { exit !(m >= 14 && m <= 20) }' && ok "the ticks every $mean ms on average" || fail "the ticks every $mean ms on average, not 16"
[ $((slow * 10)) -le $n ] && ok "$slow of $n ticks 25 ms or more after the one before" || fail "$slow of $n ticks 25 ms or more late"
# the round trips and presents between the first and the last of those ticks (the shim's clock is CLOCK_MONOTONIC in ms as
# GetTickCount's, which wraps at 2^32)
a=$(head -1 $T/ticks); z=$(tail -1 $T/ticks)
read replies queries puts << EOF
$(awk -v a=$a -v z=$z '{ t = $1 % 4294967296 } t >= a && t <= z { if ($2 == "reply") r++; if ($2 == "XQueryPointer") q++; if ($2 ~ /PutImage$/) p++ }
     END { printf "%d %d %d\n", r, q, p }' $T/x.log)
EOF
[ "$replies" = 0 ] && ok "no reply waited for during $n ticks" || fail "$replies replies waited for during $n ticks ($queries of them XQueryPointer)"
[ "$puts" -ge $((n / 4)) ] && ok "$puts presents during $n ticks" || fail "only $puts presents during $n ticks: what is painted does not reach the window"
# GetMessagePos: the pointer where it is (300,300 on the screen), without asking the server
pos=$(grep '^tick' $T/out.log | tail -1 | awk '{print $3}')
[ "$pos" = "300,300" ] && ok "a tick's pt is the pointer: $pos" || fail "a tick's pt is $pos, not 300,300"
kill $PT $PW $PP 2>/dev/null; wait $PT $PW $PP 2>/dev/null
stop_xvfb
[ -n "$TIMERBUSY_KEEP" ] && echo "kept: $T" || rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
