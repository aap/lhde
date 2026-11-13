#!/bin/sh
# A Win32 program's menu bar (Wine's user32 in wux with the menu painter: menu.c.patch, wuxtheme/menu.cpp) under luxwm on
# Xvfb: tests/w32/menutest's bar read back with ImageMagick at points measured on Longhorn 4074's Notepad under Jade and on
# XP. Under jade4074: the 1 px top row in MENU (244), the bar in BAR's flat image (233) and the line under it white, the item
# under the pointer hot (BARITEM: the 181 grey outline, 248 inside its top), the open one pushed (128 under its outline),
# an inactive window's bar BAR's gradient (244 in the middle, white at the right end); under luna XP's flat bar (MENUBAR, the
# hot and the open item MENUHILIGHT); under classic the 3D face bar, the hot item raised and the open one sunken. Needs Xvfb,
# xdotool and ImageMagick; jade4074 and luna are skipped when not imported. Usage: tests/headless/menubar.sh [:DISPLAY]
D=${1:-:77}
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/tests/w32/menutest
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool xprop xwininfo import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D
[ -n "$styles" ] && export LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
# px FILE X Y: the pixel as "R,G,B"
px() { $IM "$1" -format '%[fx:int(255*p{'"$2"','"$3"'}.r)],%[fx:int(255*p{'"$2"','"$3"'}.g)],%[fx:int(255*p{'"$2"','"$3"'}.b)]' info:; }
expect() {   # expect NAME FILE X Y R,G,B
    got=$(px "$2" "$3" "$4")
    if [ "$got" = "$5" ]; then ok "$1: ($3,$4) $got"; else bad "$1: ($3,$4) expected $5 got $got"; fi
}
item() { sed -n "s/^item $1 //p" $T/rects; }   # L T R B in window coordinates
run() {   # run THEME: the checks under it
    theme=$1
    printf '[theme]\nname = %s\n' "$theme" > $T/home/.config/lhde/settings.ini
    "$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
    wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
    wait_for 20 wm_up || echo "note: luxwm did not announce itself"
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$theme"   # the session theme (luxwm publishes its own default first)
    sleep .5
    MENUTEST_RECTS=1 "$B" > $T/rects 2> $T/menutest.err & PM=$!
    W=$(wait_window 30 --name '^flxp menus$')
    if [ -z "$W" ]; then bad "$theme: no window"; kill $PM $PW 2>/dev/null; wait $PM $PW 2>/dev/null; return; fi
    xdotool windowactivate --sync $W 2>/dev/null; sleep .5
    eval "$(xwininfo -id $W | sed -n 's/^ *Absolute upper-left X: *\([0-9-]*\)/X=\1/p; s/^ *Absolute upper-left Y: *\([0-9-]*\)/Y=\1/p')"
    set -- $(item 2); L=$1 TP=$2 RT=$3 BT=$4   # View
    xdotool mousemove $((X + 300)) $((Y + 150)); sleep .3
    import -window $W $T/plain.png
    xdotool mousemove $((X + L + 10)) $((Y + 10)); sleep .5
    import -window $W $T/hot.png
    xdotool click 1; sleep .7
    import -window $W $T/open.png
    xdotool key Escape; sleep .2; xdotool key Escape; sleep .3
    case $theme in
    jade4074)
        expect "$theme: the bar's top row (MENU)" $T/plain.png 300 0 244,244,244
        expect "$theme: the bar (BAR, the active window's image)" $T/plain.png 300 10 233,233,233
        expect "$theme: the line under the bar (WINDOW)" $T/plain.png 300 19 255,255,255
        expect "$theme: the hot item's outline" $T/hot.png $L 9 181,181,181
        expect "$theme: inside the hot item's top" $T/hot.png $((L + 3)) 1 248,248,248
        expect "$theme: the hot item over the top row" $T/hot.png $((L + 3)) 0 186,186,186
        expect "$theme: inside the pushed item's top" $T/open.png $((L + 3)) 1 128,128,128
        expect "$theme: the pushed item's outline" $T/open.png $L 9 181,181,181
        # another window active: this bar in BAR's other image
        xdotool mousemove $((X + 300)) $((Y + 150)); sleep .2
        MENUTEST_RECTS=1 "$B" > /dev/null 2>&1 & P2=$!
        sleep 1.5
        W2=$(xdotool search --onlyvisible --name '^flxp menus$' | grep -vx "$W" | head -1)
        [ -n "$W2" ] && xdotool windowactivate --sync $W2 2>/dev/null && xdotool windowmove $W2 600 450; sleep .8
        import -window $W $T/inactive.png
        expect "$theme: an inactive window's bar (BAR's gradient)" $T/inactive.png 200 10 244,244,244
        expect "$theme: its right end" $T/inactive.png 398 10 255,255,254
        kill $P2 2>/dev/null; wait $P2 2>/dev/null ;;
    luna)
        expect "$theme: the bar (MENUBAR)" $T/plain.png 300 10 236,233,216
        expect "$theme: the hot item (MENUHILIGHT)" $T/hot.png $((L + 2)) 2 49,106,197
        expect "$theme: the open item (MENUHILIGHT)" $T/open.png $((L + 2)) 2 49,106,197 ;;
    classic)
        expect "$theme: the bar (MENU)" $T/plain.png 300 10 212,208,200
        expect "$theme: the hot item raised (top left)" $T/hot.png $L $TP 255,255,255
        expect "$theme: the hot item raised (bottom right)" $T/hot.png $((RT - 1)) $((BT - 1)) 128,128,128
        expect "$theme: the open item sunken (top left)" $T/open.png $L $TP 128,128,128
        expect "$theme: the open item sunken (bottom right)" $T/open.png $((RT - 1)) $((BT - 1)) 255,255,255 ;;
    esac
    kill $PM 2>/dev/null; wait $PM 2>/dev/null
    kill $PW 2>/dev/null; wait $PW 2>/dev/null
}
for theme in jade4074 luna classic; do
    case $theme in
    jade4074) [ -f "$styles/jade4074.msstyles" ] || { echo "skip $theme: no jade4074 style imported"; continue; } ;;
    luna) [ -f "$styles/luna.msstyles" ] || [ -f "$styles/luna/luna.msstyles" ] || { echo "skip $theme: no luna style imported"; continue; } ;;
    esac
    run $theme
done
stop_xvfb
echo "$fails failed"
exit $fails
