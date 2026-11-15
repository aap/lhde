#!/bin/sh
# Menus and the clicks around them (docs/CLIENT-PROTOCOL.md "Menus and clicks"), under luxwm on Xvfb: tests/w32/menuclick
# windows (A and A2 one process, B another) open a context menu on a right click and log what reaches them. A click
# outside an open menu ends it and goes on to the window under the pointer, which is activated and acts on it: a right
# click on another program's window opens that window's menu, one on the same window or another window of the same
# program likewise; Escape ends a menu; luxwm's window menu (Alt+Space) the same way. luxshell's menus when luxshell
# comes up (skipped otherwise). First flxp-desktop's context menu on a desktop without windows: from its submenu's item down
# onto the next item, the submenu closes and the menu stays.
# Needs Xvfb and xdotool. Usage: tests/headless/menus.sh [:DISPLAY]
D=${1:-:76}
T=$(mktemp -d)
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/tests/w32/menuclick
mkdir -p $T/home/.config
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D   # the scratch home only: nothing exported from outside leaks in
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
# flxp-desktop's context menu on a desktop without windows: the pointer on Arrange Icons By opens its submenu, then down
# onto Refresh: the submenu closes, the menu stays (luxwm leaves the focus on the desktop when the submenu's window goes)
mkdir -p $HOME/Desktop
"$R/build/bin/flxp-desktop" > $T/desktop.log 2>&1 & PD=$!
if wait_window 20 --classname '^flxp-desktop$' > /dev/null; then
    sleep .5
    nwin() { xdotool search --onlyvisible --classname '^flxp-desktop$' 2>/dev/null | wc -l; }
    n0=$(nwin)
    click_root() { xdotool mousemove "$1" "$2" sleep .1 mousedown "$3" sleep .1 mouseup "$3"; }
    click_root 500 300 3
    more() { [ "$(nwin)" -gt "$1" ]; }
    if wait_for 5 more $n0; then
        n1=$(nwin)
        xdotool mousemove 530 312
        if wait_for 5 more $n1; then
            n2=$(nwin)
            xdotool mousemove 530 331
            back() { [ "$(nwin)" = "$n1" ]; }
            if wait_for 5 back && sleep 1 && back; then ok "desktop menu: off the submenu's item onto Refresh, the submenu closes and the menu stays"
            else bad "desktop menu: off the submenu's item onto Refresh: $(nwin) windows, the menu up was $n1, with its submenu $n2"; fi
        else bad "desktop menu: no submenu on Arrange Icons By"; fi
        xdotool key Escape; sleep .3; xdotool key Escape
        gone() { [ "$(nwin)" = "$n0" ]; }
        wait_for 5 gone || bad "desktop menu: still up after Escape"
    else bad "desktop menu: no menu on a right click"; fi
else bad "flxp-desktop did not show"; cat $T/desktop.log; fi
kill $PD 2>/dev/null; wait $PD 2>/dev/null
"$B" A 40 60 A2 400 60 > $T/a.log 2>$T/a.err & PA=$!
"$B" B 40 400 > $T/b.log 2>$T/b.err & PB=$!
WA=$(wait_window 30 --name '^A$'); WA2=$(wait_window 30 --name '^A2$'); WB=$(wait_window 30 --name '^B$')
# lines of a log after a mark: mark LOG; since LOG
mark() { eval "m_$1=\$(wc -l < $T/$1.log)"; }
since() { eval "_m=\$m_$1"; tail -n +$((_m + 1)) $T/$1.log; }
has() { since "$1" | grep -qx "$2"; }
expect() {   # expect NAME LOG LINE: LINE appears in LOG after its mark (bounded wait)
    if wait_for 5 has "$2" "$3"; then ok "$1: $3"; else bad "$1: no \"$3\" in $2 (got: $(since "$2" | tr '\n' '|'))"; fi
}
expect_not() {   # expect_not NAME LOG PATTERN: nothing like it after the mark (after a settle)
    sleep .5; if since "$2" | grep -q "$3"; then bad "$1: \"$3\" in $2 (got: $(since "$2" | tr '\n' '|'))"; else ok "$1: no $3"; fi
}
active() { xprop -root _NET_ACTIVE_WINDOW | sed 's/.*# //'; }
is_active() { [ "$(printf '%d' "$(active)")" = "$(printf '%d' "$1")" ]; }
expect_active() {   # expect_active NAME WINDOW LABEL
    if wait_for 5 is_active "$2"; then ok "$1: $3 active"; else bad "$1: $3 not active (_NET_ACTIVE_WINDOW $(active))"; fi
}
# click WINDOW X Y BUTTON (client coordinates): held 0.1 s, as a hand does; xdotool's click sends the press and the release
# in one burst, and Xorg then delivers the release past a synchronous grab's freeze, before the press is replayed
click() { xdotool mousemove --window "$1" "$2" "$3" sleep .1 mousedown "$4" sleep .1 mouseup "$4"; }
reset() { xdotool key Escape; sleep .3; }
if [ -z "$WA" ] || [ -z "$WA2" ] || [ -z "$WB" ]; then
    echo "FAIL the menuclick windows did not show"; cat $T/a.err $T/b.err; fails=1
else
    # another program's window: A's menu ends, B is activated and opens its own
    mark a; mark b
    click $WA 50 50 3; expect "right click A" a "open A"
    click $WB 60 60 3
    expect "right click B with A's menu up" a "end A 0"
    expect "right click B with A's menu up" b "down B R 60 60"
    expect "right click B with A's menu up" b "open B"
    expect_active "right click B with A's menu up" $WB B
    # the reverse
    mark a; mark b
    click $WA 70 40 3
    expect "right click A with B's menu up" b "end B 0"
    expect "right click A with B's menu up" a "down A R 70 40"
    expect "right click A with B's menu up" a "open A"
    expect_active "right click A with B's menu up" $WA A
    # the same window outside the menu: the menu ends and the click acts there (XP lets it pass through)
    mark a
    click $WA 250 190 1
    expect "left click A outside A's menu" a "end A 0"
    expect "left click A outside A's menu" a "down A L 250 190"
    expect_not "left click A outside A's menu" a "^open"
    # another window of the same program: activated, and its menu opens
    mark a
    click $WA 50 50 3; expect "right click A again" a "open A"
    click $WA2 80 80 3
    expect "right click A2 with A's menu up" a "end A 0"
    expect "right click A2 with A's menu up" a "down A2 R 80 80"
    expect "right click A2 with A's menu up" a "open A2"
    expect_active "right click A2 with A's menu up" $WA2 A2
    # Escape
    mark a
    xdotool key Escape
    expect "Escape" a "end A2 0"
    # a click on an item chooses it
    mark b
    click $WB 30 30 3; expect "right click B" b "open B"
    xdotool mousemove_relative 30 8 sleep .1 mousedown 1 sleep .1 mouseup 1
    expect "click on the first item" b "end B 1"
    # luxwm's window menu (Alt+Space on B), then a click into A: the menu goes, A is activated and gets the click
    mark a
    xdotool key alt+space
    if wait_window 5 --classname lhwm-menu > /dev/null; then
        ok "Alt+Space: luxwm's window menu"
        click $WA 120 120 1
        expect "click A with luxwm's menu up" a "down A L 120 120"
        expect_active "click A with luxwm's menu up" $WA A
        menu_gone() { [ -z "$(xdotool search --onlyvisible --classname lhwm-menu 2>/dev/null)" ]; }
        if wait_for 5 menu_gone; then ok "click A with luxwm's menu up: menu gone"; else bad "click A with luxwm's menu up: luxwm's menu still up"; fi
    else
        bad "Alt+Space: no window menu"
    fi
fi
kill $PA $PB 2>/dev/null; wait $PA $PB 2>/dev/null
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
