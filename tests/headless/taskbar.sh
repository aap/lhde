#!/bin/sh
# flxp-taskbar's notification area, Quick Launch and menus under luxwm on Xvfb, with nothing imported (the classic look):
#   the tray: two luxshell/tests/trayicon icons dock (reparented into the bar), 16 x 16 in cells 18 px apart (16 px and
#     XP's 2 px), the pointer over one shows its name as a tooltip; with flxp.ini's tray_idle = 1 s both become inactive
#     and hide behind the chevron, a click on the chevron shows them again; [taskbar] hide_inactive = 0 never hides them;
#   Quick Launch: Show Desktop and the entries of a scratch [quicklaunch] dir, 23 px buttons from 6 px after the Start
#     button; a click on an entry runs it; [taskbar] quicklaunch = 0 hides the toolbar;
#   the menus: a right click on the bar's empty part opens the taskbar menu, Show the Desktop (k) sets _NET_SHOWING_DESKTOP,
#     Show Open Windows clears it (with an xterm to minimise; without xterm these two are skipped).
# The tray's own controls run against stub commands (no wpctl, pactl, amixer, xinput; no backlight directory): Volume only.
# Needs Xvfb, xdotool, xprop and xwininfo. Usage: tests/headless/taskbar.sh [:DISPLAY]
D=${1:-:79}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop xwininfo; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x "$R/build/luxshell/tests/trayicon" ] || { echo "skip: build/luxshell/tests/trayicon not built"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde $T/ql $T/bin $T/bl
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=/nonexistent LHSHELL_BACKLIGHT=$T/bl FLXP_TASKBAR_LOG=1
unset LHDE_DATA LHDE_SETTINGS
# a PATH without the mixers and xinput: our Volume item says it has none, Touchpad stays away
for c in sh touch sleep cat xdg-open; do p=$(command -v $c) && ln -s "$p" $T/bin/$c; done
printf '[Desktop Entry]\nType=Application\nName=Make a mark\nExec=touch %s/launched\n' "$T" > $T/ql/mark.desktop
printf '[Desktop Entry]\nType=Application\nName=Other\nExec=true\n' > $T/ql/other.desktop
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1: $3"; else bad "$1: expected [$2] got [$3]"; fi; }
settings() { printf '[theme]\nname = classic\n[quicklaunch]\ndir = %s\n[taskbar]\n%s\n' "$T/ql" "$1" > $T/home/.config/lhde/settings.ini; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
bar() {   # bar SETTINGS...: flxp-taskbar with these [taskbar] keys
    settings "$1"
    PATH=$T/bin FLXP_TRAY_IDLE=${IDLE:-3600} "$R/build/bin/flxp-taskbar" > $T/tb.log 2>&1 & PT=$!
    has_layout() { grep -q 'layout bar' $T/tb.log; }
    wait_for 20 has_layout || bad "flxp-taskbar did not lay itself out"
    sleep 1
}
unbar() { kill $PT 2>/dev/null; wait $PT 2>/dev/null; }
icons() { "$R/build/luxshell/tests/trayicon" '#3399ff' > $T/i1.log 2>&1 & P1=$!; "$R/build/luxshell/tests/trayicon" '#ff9933' > $T/i2.log 2>&1 & P2=$!; }
unicons() { kill $P1 $P2 2>/dev/null; wait $P1 $P2 2>/dev/null; }
last() { grep 'layout bar' $T/tb.log | tail -1; }
field() { last | sed "s/.* $1 //" | cut -d' ' -f$2; }   # field NAME N: the Nth number after NAME in the last layout line
docked() { grep -q 'reparented to' $T/i1.log && grep -q 'reparented to' $T/i2.log; }
icon_win() { sed -n 's/.*window \(0x[0-9a-f]*\).*/\1/p' $1 | head -1; }
geom() { xwininfo -id "$1" 2>/dev/null | sed -n 's/.*Absolute upper-left X: *//p; s/.*Absolute upper-left Y: *//p; s/.*Width: *//p; s/.*Height: *//p; s/.*Map State: *//p' | tr '\n' ' '; }

# ---- the tray
bar "hide_inactive = 1"
icons
wait_for 10 docked || bad "the icons did not dock"
wait_for 5 sh -c "[ \"\$(grep 'layout bar' $T/tb.log | tail -1 | sed 's/.* icons [0-9-]* [0-9-]* //' | cut -d' ' -f1)\" = 2 ]"
check "two docked icons" 2 "$(field icons 3)"
w1=$(icon_win $T/i1.log); w2=$(icon_win $T/i2.log)
g1=$(geom $w1); g2=$(geom $w2)
set -- $g1; x1=$1 y1=$2 sz1="$3x$4" m1=$5
set -- $g2; x2=$1 sz2="$3x$4"
check "icon 1 size" 16x16 "$sz1"
check "icon 2 size" 16x16 "$sz2"
check "icon cells 18 px apart (16 + 2)" 18 "$(( x2 > x1 ? x2 - x1 : x1 - x2 ))"
check "icon 1 shown" IsViewable "$m1"
check "our Volume item beside them" 1 "$(field own 3)"
xdotool mousemove $((x1 + 8)) $((y1 + 8)); sleep 0.3; xdotool mousemove $((x1 + 7)) $((y1 + 8))
has_tip() { grep -q 'tooltip trayicon' $T/tb.log; }
wait_for 5 has_tip && ok "the icon's name as its tooltip" || bad "no tooltip for the icon"
xdotool mousemove 500 300
unicons; unbar

# hidden behind the chevron when inactive (tray_idle 1 s), shown again by it
IDLE=1 bar "hide_inactive = 1"
icons
wait_for 10 docked || bad "the icons did not dock (idle run)"
hidden() { [ "$(geom $(icon_win $T/i1.log) | cut -d' ' -f5)" = IsUnMapped ] && [ "$(geom $(icon_win $T/i2.log) | cut -d' ' -f5)" = IsUnMapped ]; }
wait_for 10 hidden && ok "inactive icons hidden" || bad "inactive icons still shown: $(geom $(icon_win $T/i1.log))"
cl=$(field chevron 1); ct=$(field chevron 2); cr=$(field chevron 3); cb=$(field chevron 4)
check "the chevron is there (16 px classic)" 16 "$((cr - cl))"
by=$(last | sed 's/.*layout bar //' | cut -d' ' -f2)
xdotool mousemove $(( (cl + cr) / 2 )) $(( by + (ct + cb) / 2 )) click 1
shown() { [ "$(geom $(icon_win $T/i1.log) | cut -d' ' -f5)" = IsViewable ]; }
wait_for 5 shown && ok "the chevron shows them" || bad "the chevron did not show them"
unicons; unbar
IDLE=1 bar "hide_inactive = 0"
icons
wait_for 10 docked; sleep 3
check "hide_inactive = 0: shown" IsViewable "$(geom $(icon_win $T/i1.log) | cut -d' ' -f5)"
check "hide_inactive = 0: no chevron" 0 "$(( $(field chevron 3) - $(field chevron 1) ))"
unicons; unbar

# ---- Quick Launch
bar "quicklaunch = 1"
check "Quick Launch: Show Desktop and two entries" 3 "$(field buttons 1)"
sr=$(field start 2); ql=$(field quicklaunch 1); qr=$(field quicklaunch 3); qt=$(field quicklaunch 2); qb=$(field quicklaunch 4)
check "Quick Launch 6 px after the Start button" 6 "$((ql - sr))"
check "Quick Launch buttons 23 px" 69 "$((qr - ql))"
by=$(last | sed 's/.*layout bar //' | cut -d' ' -f2)
# the second entry in file-name order is "other", the first "mark": button 2 of 3
xdotool mousemove $((ql + 23 + 11)) $((by + (qt + qb) / 2)) click 1
launched() { [ -e $T/launched ]; }
wait_for 5 launched && ok "a Quick Launch entry runs" || bad "the Quick Launch entry did not run"
unbar
bar "quicklaunch = 0"
check "quicklaunch = 0: no toolbar" 0 "$(field buttons 1)"
unbar

# ---- the taskbar's menu: Show the Desktop
bar "locked = 1"
# a window to minimise (luxwm shows the desktop only over a window: an empty set leaves it at 0)
if command -v xterm >/dev/null 2>&1; then xterm -geometry 40x5+200+200 > /dev/null 2>&1 & PX=$!; wait_window 10 --class xterm > /dev/null; fi
by=$(last | sed 's/.*layout bar //' | cut -d' ' -f2); tl=$(last | sed 's/.* tray //' | cut -d' ' -f1)
showing() { xprop -root _NET_SHOWING_DESKTOP 2>/dev/null | sed 's/.*= //'; }
menu() { xdotool mousemove $((tl - 40)) $((by + 12)) mousedown 3; sleep 0.1; xdotool mouseup 3; sleep 1; xdotool key "$1"; sleep 1; }
if [ -n "$PX" ]; then
menu k
check "Show the Desktop" 1 "$(showing)"
menu w
check "Show Open Windows" 0 "$(showing)"
else echo "skip Show the Desktop: needs xterm"; fi
[ -n "$PX" ] && { kill $PX 2>/dev/null; wait $PX 2>/dev/null; }
unbar

kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
