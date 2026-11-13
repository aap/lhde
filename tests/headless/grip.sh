#!/bin/sh
# The size grip (wux/w32/theme.cpp WUX_DrawSizeGrip, comctl32 status.c.patch, DefWindowProc's scroll bar corner) under luxwm
# on Xvfb, in luxpad: with its status bar the status bar's grip, without it the corner of its Edit's scroll bars (XP draws a
# grip there when the corner is the window's bottom right). For each: the pointer over it shows the double arrow
# (nwse-resize, read back with tests/w32/cursortest --shown), a drag from it makes the window larger; under jade4074 the grip
# is 4074's dots (a 172 dot, its 255 light below right) at the grip's bottom right. Under jade4074 (skipped when not
# imported), luna (likewise) and classic. Needs Xvfb, xdotool, xwininfo and ImageMagick. Usage: tests/headless/grip.sh [:DISPLAY]
D=${1:-:79}
R=$(cd "$(dirname "$0")/../.." && pwd)
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool xprop xwininfo import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde $T/home/.local/state/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D
[ -n "$styles" ] && export LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS
printf 'hello\n' > $T/a.txt
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1: $3"; else bad "$1: expected [$2] got [$3]"; fi; }
px() { $IM "$1" -format '%[fx:int(255*p{'"$2"','"$3"'}.r)],%[fx:int(255*p{'"$2"','"$3"'}.g)],%[fx:int(255*p{'"$2"','"$3"'}.b)]' info:; }
geom() { xwininfo -id $W | sed -n 's/^ *Width: *//p; s/^ *Height: *//p' | tr '\n' ' '; }
shown() { "$R/build/tests/w32/cursortest" --shown; }
# grip THEME STATUSBAR INSET: the grip's bottom right INSET px inside the window's
grip() {
    theme=$1 sb=$2 in=$3 what=$1
    [ $sb = 1 ] && what="$theme, status bar" || what="$theme, scroll bars' corner"
    printf '[luxpad]\nstatusbar = %s\n' $sb > $T/home/.config/lhde/luxpad.ini
    printf '[window]\nx = 40\ny = 40\nwidth = 500\nheight = 360\nmaximized = 0\n' > $T/home/.local/state/lhde/luxpad.ini
    "$R/build/bin/luxpad" $T/a.txt > $T/luxpad.log 2>&1 & P=$!
    W=$(wait_window 30 --name '^a.txt - luxpad$')
    if [ -z "$W" ]; then bad "$what: no window"; kill $P 2>/dev/null; wait $P 2>/dev/null; return; fi
    xdotool windowactivate --sync $W 2>/dev/null; sleep .8
    set -- $(geom); w=$1 h=$2
    if [ $theme = jade4074 ]; then
        import -window $W $T/grip.png
        check "$what: a dot of 4074's grip" 172,172,172 "$(px $T/grip.png $((w - in - 4)) $((h - in - 4)))"
        check "$what: its light" 255,255,255 "$(px $T/grip.png $((w - in - 2)) $((h - in - 2)))"
    fi
    xdotool mousemove --window $W $((w - in - 5)) $((h - in - 5)); sleep .5
    check "$what: the pointer over the grip" nwse-resize "$(shown)"
    xdotool mousedown 1; sleep .3
    xdotool mousemove_relative 30 20; sleep .3; xdotool mousemove_relative 30 20; sleep .3
    xdotool mouseup 1; sleep .8
    set -- $(geom)
    if [ "$1" -gt $w ] && [ "$2" -gt $h ]; then ok "$what: dragged from ${w}x$h to ${1}x$2"
    else bad "$what: a drag from the grip left the window ${1}x$2 (was ${w}x$h)"; fi
    kill $P 2>/dev/null; wait $P 2>/dev/null
}
for theme in jade4074 luna classic; do
    case $theme in
    jade4074) [ -f "$styles/jade4074.msstyles" ] || { echo "skip $theme: no jade4074 style imported"; continue; } ;;
    luna) [ -f "$styles/luna.msstyles" ] || [ -f "$styles/luna/luna.msstyles" ] || { echo "skip $theme: no luna style imported"; continue; } ;;
    esac
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$theme"; sleep .5
    grip $theme 1 0
    grip $theme 0 2   # inside the Edit's client edge
done
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
