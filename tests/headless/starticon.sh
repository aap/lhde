#!/bin/sh
# flxp-taskbar's Start button with Shell.start_icon (settings.ini [taskbar] start_icon) on Xvfb, under classic and, when it is
# imported, luna: the button read back with ImageMagick. Without the key and with start_icon = windows the button is the same
# image, byte for byte (the flag as before); with linux it differs only inside the flag's cell (the left 16 px of the classic
# button's content, the left 20 of luna's) and there in many pixels; the key changed while the bar runs (with
# _LH_SETTINGS_CHANGED) redraws it as a fresh start would, and back to windows gives the flag again.
# Needs Xvfb, xdotool, xprop and ImageMagick. Usage: tests/headless/starticon.sh [:DISPLAY]
D=${1:-:76}
R=$(cd "$(dirname "$0")/../.." && pwd)
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
CMP=compare; command -v magick >/dev/null 2>&1 && CMP="magick compare"
for t in Xvfb xdotool xprop import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
luna=$("$R/build/bin/lhde-theme" find styles/luna.msstyles 2>/dev/null)   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
[ -z "$luna" ] && luna=$("$R/build/bin/lhde-theme" find styles/luna4051.msstyles 2>/dev/null)
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
T=$(mktemp -d)
trap '[ -n "$KEEP" ] || rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D FLXP_TASKBAR_LOG=1
[ -n "$styles" ] && export LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS LHDE_UNAME
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 800x600x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
settings() {   # settings THEME [ICON]
    { printf '[theme]\nname = %s\n[taskbar]\nquicklaunch = 0\n' "$1"; [ -n "$2" ] && printf 'start_icon = %s\n' "$2"; } > $T/home/.config/lhde/settings.ini
}
# shot FILE: the Start button as the bar draws it, from the layout line's start L R and the bar's top and bottom
shot() {
    out=$1
    set -- $(grep 'layout bar' $T/tb.log | tail -1 | sed 's/.*layout bar //')
    bt=$2 bb=$4 sl=$6 sr=$7
    import -window root -crop $((sr - sl))x$((bb - bt))+$sl+$bt +repage "$out"
}
bar() {   # bar THEME [ICON]: a fresh flxp-taskbar
    settings "$1" "$2"
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$1"
    "$R/build/bin/flxp-taskbar" --no-tray > $T/tb.log 2>&1 & PT=$!
    has_layout() { grep -q 'layout bar' $T/tb.log; }
    wait_for 20 has_layout || bad "$1: flxp-taskbar did not lay itself out"
    sleep 1
}
unbar() { kill $PT 2>/dev/null; wait $PT 2>/dev/null; }
ae() { $CMP -metric AE "$1" "$2" null: 2>&1 | awk '{ printf "%d", $1 }'; }   # how many pixels differ (IM 7: "N (fraction)")
same() { [ "$(ae "$1" "$2")" = 0 ]; }
# diff_box A B: "PIXELS WxH+X+Y", how many differ and their bounding box
diff_box() {
    n=$(ae "$1" "$2")
    $CMP "$1" "$2" -compose src -highlight-color white -lowlight-color black $T/d.png 2>/dev/null
    echo "$n $($IM $T/d.png -format '%@' info: 2>/dev/null)"
}

run() {   # run THEME CELL: CELL the flag's columns from the button's left edge
    th=$1 cell=$2
    bar $th; shot $T/$th-none.png; unbar
    bar $th windows; shot $T/$th-windows.png
    if same $T/$th-none.png $T/$th-windows.png; then ok "$th: start_icon = windows is the button without the key"
    else bad "$th: start_icon = windows differs from the button without the key"; fi
    # live: the key changed with _LH_SETTINGS_CHANGED
    settings $th linux
    xprop -root -f _LH_SETTINGS_CHANGED 32c -set _LH_SETTINGS_CHANGED 1
    sleep 1.5
    shot $T/$th-linux-live.png; unbar
    bar $th linux; shot $T/$th-linux.png
    set -- $(diff_box $T/$th-windows.png $T/$th-linux.png)
    n=$1 box=$2
    x=${box#*+}; x=${x%%+*}; w=${box%%x*}
    if [ "${n:-0}" -ge 40 ] && [ $((x + w)) -le $((cell + 1)) ] 2>/dev/null; then
        ok "$th: linux differs from the flag in $n pixels, all inside the flag's cell ($box, the cell ${cell} px from the button's left)"
    else bad "$th: linux against the flag: $n pixels in $box (the cell: the first $cell px)"; fi
    if same $T/$th-linux.png $T/$th-linux-live.png; then ok "$th: the key changed live draws what a fresh start draws"
    else bad "$th: the key changed live: $(diff_box $T/$th-linux.png $T/$th-linux-live.png)"; fi
    settings $th windows
    xprop -root -f _LH_SETTINGS_CHANGED 32c -set _LH_SETTINGS_CHANGED 2
    sleep 1.5
    shot $T/$th-back.png; unbar
    if same $T/$th-windows.png $T/$th-back.png; then ok "$th: back to windows: the flag again"
    else bad "$th: back to windows: $(diff_box $T/$th-windows.png $T/$th-back.png)"; fi
}

# classic: the flag 4 px in (the 2 px edge and 2), 16 px wide; luna: the content margin, then XP's 20 px flag in a 25 px cell
run classic 21
if [ -n "$luna" ]; then run luna 30; else echo "skip luna: not imported"; fi
stop_xvfb
echo "$fails failed"
exit $fails
