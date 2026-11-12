#!/bin/sh
# luxwm's Alt+Tab task switcher (docs/LUXWM.md "Task switcher") under Xvfb: luxwm (Windows Classic) and three testwin windows A, B,
# C (C active, stacking C B A). Alt held and Tab: the switcher shows centred, 358 px wide (7 cells of 48 + 2 x 8 + the 3 px frame),
# one row high, the second entry (B) selected and framed in COLOR_HIGHLIGHT; Tab, Shift+Tab move the selection (read from the
# window's _LHWM_SWITCHER: selected, count, first shown, window); releasing Alt activates it. Then Alt+Tab+Tab, Alt+Shift+Tab,
# Escape (no switch), a quick alt+Tab (the previous window, no switcher left), a click on an item, a minimised window (listed last,
# restored when chosen), a skip-taskbar window (not listed) and a transient dialog (listed instead of its owner).
# Needs Xvfb, xdotool, xprop, xwininfo and ImageMagick's import. Usage: tests/headless/alttab.sh [:DISPLAY]
D=${1:-:76}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop xwininfo import; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x $TOP/build/bin/luxwm ] && [ -x $TOP/build/luxwm/tests/testwin ] || { echo "FAIL build luxwm and build/luxwm/tests/testwin first"; exit 1; }
T=$(mktemp -d)
mkdir -p $T/home/.config
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
$TOP/build/bin/luxwm -d -t classic > $T/luxwm.log 2>&1 & WM=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
fails=0 pids=
check() {   # check NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
active() { xprop -root _NET_ACTIVE_WINDOW | sed 's/.*# //; s/,.*//' | xargs printf '0x%x\n' 2>/dev/null; }
switcher() { xwininfo -root -tree | awk '/"lhwm task switcher"/ { print $1; exit }'; }
shown() { [ -n "$(switcher)" ]; }
gone() { [ -z "$(switcher)" ]; }
# _LHWM_SWITCHER: "SEL COUNT FIRST WINDOW" (the window as 0x...)
state() { xprop -id "$(switcher)" _LHWM_SWITCHER 2>/dev/null | sed -n 's/.*= //p' | tr -d ',' | awk '{ printf "%s %s %s 0x%x\n", $1, $2, $3, $4 }'; }
state_is() { [ "$(state)" = "$1" ]; }
is_active() { [ "$(active)" = "$1" ]; }
win() {   # win NAME COLOUR [ARGS]: start a testwin, print its id once luxwm has it on the screen and active
    $TOP/build/luxwm/tests/testwin "$@" > $T/$1.id 2>/dev/null & pids="$pids $!"
    wait_for 10 test -s $T/$1.id; id=$(cat $T/$1.id)
    wait_for 10 is_active $(printf '0x%x' $id); printf '0x%x\n' $id
}
if ! wait_for 20 wm_up; then
    echo "FAIL luxwm did not start"; cat $T/luxwm.log; fails=1
else
    A=$(win A c04040); B=$(win B 40a040); C=$(win C 3a6ea5)
    echo "A=$A B=$B C=$C"
    check "C active" $C "$(active)"

    # 1. Alt held, Tab: the switcher with B selected; its size and the selection frame
    xdotool keydown alt; xdotool key Tab
    wait_for 5 shown; SW=$(switcher)
    check "switcher after the first Tab" "1 3 0 $B" "$(state)"
    geo=$(xwininfo -id "$SW" | awk '/Absolute upper-left X/ { x = $NF } /Absolute upper-left Y/ { y = $NF } /Width:/ { w = $NF } /Height:/ { h = $NF } END { print x, y, w, h }')
    set -- $geo
    check "width (7 x 48 + 16 + 6)" 358 "$3"
    fh=$(($4 - 48 - 16 - 8 - 6))   # one row: the rest is the dialog font's height
    [ $fh -ge 10 ] && [ $fh -le 20 ] && echo "ok   height $4 (font height $fh)" || { echo "FAIL height $4 (font height $fh)"; fails=$((fails + 1)); }
    check "centred" "$(((1024 - 358) / 2)) $(((768 - $4) / 2))" "$1 $2"
    import -window "$SW" $T/sw1.png 2>/dev/null
    # three entries centred in 7 cells: item 1 at x = 3 + 8 + 48 + 4 + (336 - 144) / 2 = 159, y = 15; its left edge in COLOR_HIGHLIGHT
    px() { convert $T/sw1.png -format "%[hex:p{$1,$2}]" info: 2>/dev/null | cut -c1-6; }
    check "selection frame (Windows Standard highlight)" "0A246A 0A246A" "$(px 159 35) $(px 160 35)"
    check "inside the frame: 3D face" "D4D0C8" "$(px 161 35)"
    check "item 0 not framed" "D4D0C8" "$(px 111 35)"
    xdotool key Tab; wait_for 3 state_is "2 3 0 $A"; check "Tab" "2 3 0 $A" "$(state)"
    xdotool key Tab; wait_for 3 state_is "0 3 0 $C"; check "Tab wraps" "0 3 0 $C" "$(state)"
    xdotool key shift+Tab; wait_for 3 state_is "2 3 0 $A"; check "Shift+Tab wraps back" "2 3 0 $A" "$(state)"
    xdotool key shift+Tab; wait_for 3 state_is "1 3 0 $B"; check "Shift+Tab" "1 3 0 $B" "$(state)"
    xdotool keyup alt
    wait_for 5 gone; check "switcher gone after the release" "" "$(switcher)"
    wait_for 5 is_active $B; check "Alt released: B active" $B "$(active)"

    # 2. B C A now: Alt+Tab+Tab chooses A
    xdotool keydown alt; xdotool key Tab; wait_for 5 shown; xdotool key Tab; wait_for 3 state_is "2 3 0 $A"; xdotool keyup alt
    wait_for 5 is_active $A; check "Alt+Tab+Tab: A" $A "$(active)"

    # 3. A B C: Alt+Shift+Tab starts at the last entry, C
    xdotool keydown alt; xdotool keydown shift; xdotool key Tab; wait_for 5 shown
    check "Alt+Shift+Tab selects the last" "2 3 0 $C" "$(state)"
    xdotool keyup shift; xdotool keyup alt
    wait_for 5 is_active $C; check "Alt+Shift+Tab: C" $C "$(active)"

    # 4. Escape while Alt is held: no switch
    xdotool keydown alt; xdotool key Tab; wait_for 5 shown; xdotool key Escape; wait_for 5 gone
    check "Escape closes the switcher" "" "$(switcher)"
    xdotool keyup alt; sleep .3
    check "Escape: C still active" $C "$(active)"

    # 5. a quick alt+Tab (released before luxwm looks): the previous window, A (C A B), no switcher left behind
    xdotool key alt+Tab; wait_for 5 is_active $A
    check "quick Alt+Tab: A" $A "$(active)"
    check "no switcher after a quick Alt+Tab" "" "$(switcher)"

    # 6. a click on an item chooses it (A C B: the third, B)
    xdotool keydown alt; xdotool key Tab; wait_for 5 shown; SW=$(switcher)
    set -- $(xwininfo -id "$SW" | awk '/Absolute upper-left X/ { x = $NF } /Absolute upper-left Y/ { y = $NF } END { print x, y }')
    xdotool mousemove $(($1 + 3 + 8 + 96 + 2 * 48 + 24)) $(($2 + 3 + 8 + 24)); wait_for 3 state_is "2 3 0 $B"
    check "the pointer over the third item selects it" "2 3 0 $B" "$(state)"
    xdotool click 1; wait_for 5 gone; xdotool keyup alt
    wait_for 5 is_active $B; check "click: B" $B "$(active)"
    xdotool mousemove 5 5

    # 7. a minimised window is listed last and restored when chosen (B minimised: C A, then B)
    xdotool windowminimize $B; sleep .5
    xdotool keydown alt; xdotool key Tab; wait_for 5 shown
    check "B minimised: A C, then B" "1 3 0 $C" "$(state)"
    xdotool key Tab; wait_for 3 state_is "2 3 0 $B"; check "minimised B selectable" "2 3 0 $B" "$(state)"
    xdotool keyup alt
    wait_for 5 is_active $B; check "minimised B chosen: active" $B "$(active)"
    check "B restored (mapped)" "IsViewable" "$(xwininfo -id $B | awk '/Map State/ { print $NF }')"

    # 8. a skip-taskbar window is not listed; a transient dialog stands for its owner
    S=$(win S 808080 skip)
    xdotool keydown alt; xdotool key Tab; wait_for 5 shown
    check "skip-taskbar window not listed" 3 "$(state | awk '{ print $2 }')"
    xdotool key Escape; wait_for 5 gone; xdotool keyup alt
    X=$(win X d0a020 transient $A)
    xdotool keydown alt; xdotool key Tab; wait_for 5 shown
    s=$(state); n=$(echo $s | awk '{ print $2 }')
    check "a transient dialog replaces its owner (count)" 3 "$n"
    xdotool key Tab; xdotool key Tab; xdotool key Tab; sleep .3
    seen=$(for i in 1 2 3; do xdotool key Tab; sleep .2; state | awk '{ print $4 }'; done | sort | tr '\n' ' ')
    case " $seen " in *" $A "*) echo "FAIL owner A listed beside its dialog: $seen"; fails=$((fails + 1)) ;; *) echo "ok   owner A not listed" ;; esac
    case " $seen " in *" $X "*) echo "ok   dialog X listed" ;; *) echo "FAIL dialog X not listed: $seen"; fails=$((fails + 1)) ;; esac
    xdotool key Escape; wait_for 5 gone; xdotool keyup alt
fi
kill $pids $WM 2>/dev/null; wait $WM 2>/dev/null; stop_xvfb
grep 'alt+tab' $T/luxwm.log | tail -5
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
