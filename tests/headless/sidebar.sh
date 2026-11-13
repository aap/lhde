#!/bin/sh
# The sidebar's hide / unhide and stacking keys: luxwm's default Super+H ("shell sidebar-mode toggle") minimizes the sidebar into
# the taskbar at once (its window unmapped, the taskbar the full width, two screenshots 50 ms apart identical) and brings it
# back (mapped, the taskbar ending at its edge); Super+S ("shell sidebar-stacking toggle") puts it under the windows
# (_NET_WM_STATE_BELOW on luxshell-sidebar) and back (_NET_WM_STATE_ABOVE); the minimized state and the mode it restores
# (luxshell-state.ini [sidebar] restore: pinned here) survive a luxshell restart; with [sidebar] animate = 1 both bars slide
# (screenshots over 200 ms differ, the taskbar's width passes between its two values). luxwm and luxshell on Xvfb with a
# scratch HOME (LHDE_WINDOWS, when set, gives the pack its images). Needs Xvfb, xdotool, xprop, xwininfo, xwd and ImageMagick.
# Usage: tests/headless/sidebar.sh [:DISPLAY]
D=${1:-:81}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop xwininfo xwd magick; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x "$R/build/bin/luxshell" ] && [ -x "$R/build/bin/luxwm" ] || { echo "skip: luxwm or luxshell is not built"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D PATH="$R/build/bin:$PATH" LHSHELL_COMPOSITION=0 LHSHELL_WALLPAPER=0
unset DBUS_SESSION_BUS_ADDRESS
. "$(dirname "$0")/wait.sh"
SW=1024
start_xvfb $D -screen 0 ${SW}x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
INI=$T/home/.config/lhde/settings.ini
STATE=$T/home/.local/state/lhde/luxshell-state.ini
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
win() { xdotool search --classname "^$1\$" 2>/dev/null | head -1; }
mapped() { xwininfo -id "$1" 2>/dev/null | grep -q 'Map State: IsViewable'; }
width() { xwininfo -id "$1" 2>/dev/null | sed -n 's/^ *Width: //p'; }
PS=
shell() {
    "$R/build/bin/luxshell" >> $T/luxshell.log 2>&1 & PS=$!
    up() { SB=$(win luxshell-sidebar); TB=$(win luxshell-taskbar); [ -n "$SB" ] && [ -n "$TB" ] && grep -q 'luxshell: theme' $T/luxshell.log; }
    wait_for 20 up || { bad "luxshell did not come up ($(tail -n 3 $T/luxshell.log | tr '\n' '|'))"; return 1; }
    sleep 1
}
stop_shell() { [ -n "$PS" ] && kill $PS 2>/dev/null; wait $PS 2>/dev/null; PS=; : > $T/luxshell.log; }
shot() { xwd -root -silent > "$1"; }
# the taskbar and the sidebar's top 300 rows (the clock's second hand further down moves on its own)
same() {
    a=$(magick xwd:"$1" -crop ${SW}x300+0+0 +repage ppm:- | md5sum); b=$(magick xwd:"$2" -crop ${SW}x300+0+0 +repage ppm:- | md5sum)
    c=$(magick xwd:"$1" -crop ${SW}x30+0+738 +repage ppm:- | md5sum); d=$(magick xwd:"$2" -crop ${SW}x30+0+738 +repage ppm:- | md5sum)
    [ "$a" = "$b" ] && [ "$c" = "$d" ]
}
# a hide or unhide: the key, then a wait for the sidebar's map state, two screenshots 50 ms apart
hidden() { ! mapped "$SB"; }
shown() { mapped "$SB"; }

shell || { kill $PW; stop_xvfb; exit 1; }
shown && ok "the sidebar is up ($(width $TB) px taskbar)" || bad "no sidebar mapped at startup"
TBW0=$(width $TB)

ms() { echo $(($(date +%s%N) / 1000000)); }
quick() { n=0; while [ $n -lt 300 ] && ! "$@"; do sleep .01; n=$((n + 1)); done; "$@"; }   # polls every 10 ms, 3 s at most
# Super+H: minimized into the taskbar at once (the 400 ms slide is the animate option's)
t0=$(ms); xdotool key super+h
if quick hidden; then
    dt=$(($(ms) - t0))
    shot $T/h1.xwd; sleep .05; shot $T/h2.xwd
    ok "Super+H: the sidebar's window is unmapped"
    [ $dt -lt 300 ] && ok "at once: unmapped $dt ms after the key" || bad "the hide took $dt ms (a slide?)"
    [ "$(width $TB)" = "$SW" ] && ok "the taskbar runs the full width ($SW)" || bad "the taskbar is $(width $TB) px wide, not $SW"
    same $T/h1.xwd $T/h2.xwd && ok "no transition: two screenshots 50 ms apart are the same" || bad "the screen still changes 50 ms after the hide"
    grep -q '^mode *= *minimized' $INI && ok "settings.ini [sidebar] mode = minimized" || bad "settings.ini has no mode = minimized"
    grep -q 'sidebar full -> minimized' $T/luxshell.log && ! grep -q 'minimized.*(animated)' $T/luxshell.log && ok "luxshell: full -> minimized, not animated" \
        || bad "luxshell's log: $(grep 'sidebar ' $T/luxshell.log | tail -n 1)"
else
    bad "Super+H: the sidebar is still mapped ($(tail -n 3 $T/luxwm.log | tr '\n' '|'))"
fi
# Super+H again: back at once
t0=$(ms); xdotool key super+h
if quick shown; then
    dt=$(($(ms) - t0))
    [ $dt -lt 300 ] && ok "at once: mapped $dt ms after the key" || bad "the unhide took $dt ms"
    shot $T/s1.xwd; sleep .05; shot $T/s2.xwd
    ok "Super+H again: the sidebar is mapped"
    [ "$(width $TB)" = "$TBW0" ] && ok "the taskbar ends at its edge again ($TBW0)" || bad "the taskbar is $(width $TB) px wide, not $TBW0"
    same $T/s1.xwd $T/s2.xwd && ok "no transition on the way back" || bad "the screen still changes 50 ms after the unhide"
else
    bad "Super+H again: the sidebar did not come back"
fi

# Super+S: under the windows and back
st() { xprop -id "$SB" _NET_WM_STATE 2>/dev/null; }
below() { st | grep -q _NET_WM_STATE_BELOW; }
above() { st | grep -q _NET_WM_STATE_ABOVE; }
above && ok "stacking top: _NET_WM_STATE_ABOVE" || bad "the sidebar's state at startup: $(st)"
xdotool key super+s
wait_for 2 below && ok "Super+S: _NET_WM_STATE_BELOW (under the windows)" || bad "Super+S: $(st)"
grep -q '^stacking *= *bottom' $INI && ok "settings.ini [sidebar] stacking = bottom" || bad "settings.ini has no stacking = bottom"
xdotool key super+s
wait_for 2 above && ok "Super+S again: _NET_WM_STATE_ABOVE" || bad "Super+S again: $(st)"

# the state survives a restart: pinned, minimized, restart, still minimized, Super+H gives pinned back
xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND "sidebar-mode pinned"
pinned() { grep -q '^mode *= *pinned' $INI; }
wait_for 2 pinned || bad "sidebar-mode pinned: settings.ini says $(grep '^mode' $INI)"
xdotool key super+h
wait_for 2 hidden || bad "Super+H from pinned: still mapped"
grep -q '^restore *= *pinned' $STATE 2>/dev/null && ok "luxshell-state.ini [sidebar] restore = pinned" || bad "luxshell-state.ini: $(cat $STATE 2>/dev/null | tr '\n' '|')"
stop_shell
shell
hidden && ok "after a restart the sidebar stays minimized" || bad "after a restart the sidebar is mapped"
[ "$(width $TB)" = "$SW" ] && ok "and the taskbar runs the full width" || bad "after a restart the taskbar is $(width $TB) px wide"
xdotool key super+h
wait_for 2 shown && grep -q '^mode *= *pinned' $INI && ok "Super+H restores the pinned mode it was minimized from" \
    || bad "Super+H after the restart: mode $(grep '^mode' $INI)"

# [sidebar] animate = 1: both bars slide over 400 ms
xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND "sidebar-mode full"
full() { grep -q '^mode *= *full' $INI; }
wait_for 2 full; sleep .6
stop_shell
sed -i 's/^animate *= *0/animate = 1/' $INI
grep -q '^animate = 1' $INI || bad "settings.ini has no [sidebar] animate to set"
shell
t0=$(ms); xdotool key super+h
w0=$(width $TB); shot $T/a1.xwd; sleep .1; w1=$(width $TB); shot $T/a2.xwd; sleep .1; shot $T/a3.xwd
same $T/a1.xwd $T/a2.xwd && same $T/a2.xwd $T/a3.xwd && bad "animate = 1: the screen did not change over 200 ms" \
    || ok "animate = 1: the screen changes over 200 ms (the slide)"
wait_for 2 hidden && sleep .3
w2=$(width $TB)
[ "$w2" = "$SW" ] && ok "animate = 1: at the end the taskbar runs the full width" || bad "animate = 1: the taskbar ends $w2 px wide"
if [ "$w0" -gt "$TBW0" ] 2>/dev/null && [ "$w0" -lt "$SW" ] || [ "$w1" -gt "$TBW0" ] 2>/dev/null && [ "$w1" -lt "$SW" ]; then
    ok "animate = 1: the taskbar's width passes between $TBW0 and $SW ($w0, $w1)"
else bad "animate = 1: the taskbar jumped ($w0, $w1)"; fi
grep -q 'full -> minimized.*(animated)' $T/luxshell.log && ok "luxshell: full -> minimized (animated)" || bad "luxshell's log: $(grep 'sidebar ' $T/luxshell.log | tail -n 1)"
xdotool key super+h
wait_for 2 shown && sleep .6
[ "$(width $TB)" = "$TBW0" ] && ok "animate = 1: back, the taskbar ends at the sidebar's edge" || bad "animate = 1: back, the taskbar is $(width $TB) px"

stop_shell
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
