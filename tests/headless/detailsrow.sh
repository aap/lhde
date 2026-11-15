#!/bin/sh
# The Longhorn frame's Details view selects whole rows (LVS_EX_FULLROWSELECT), and the whole row is the item: on an
# unselected row, the pointer over the empty part of its Type column gives the row its hover look and the item's infotip
# (the tip's area is the row, not the Name column), and a click there selects it. flxp under jade4074 with no window manager
# (FLXP_LHWM=1, the 4074 header as under luxwm), 800 x 500 at 0, 0, five text files; what shows is read from the screen at
# measured points (the second row across y 124, the Type column from x 502 to 621 with its text ending at 586). A HOME and XDG
# directories of their own. Needs Xvfb, xdotool, ImageMagick and the user's (or LHDE_WINDOWS's) jade4074 style; prints
# "skip: ..." and passes without them. Usage: tests/headless/detailsrow.sh [:DISPLAY]
D=${1:-:75}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$TOP/build/bin/lhde-theme" find styles 2>/dev/null)   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
[ -f "$styles/jade4074.msstyles" ] || { echo "skip: no jade4074 style imported"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
F=$T/files
mkdir -p $T/home/.config/lhde "$F"
for i in 1 2 3 4 5; do echo x > "$F/file$i.txt"; done
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache LHDE_WINDOWS=${styles%/index/styles} DISPLAY=$D FLXP_LHWM=1
unset LHDE_DATA LHDE_SETTINGS
printf '[theme]\nname = jade4074\n' > $T/home/.config/lhde/settings.ini
printf '[flxp]\ntaskpane = 0\ndefault_view_longhorn = details\n' > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
px() { import -window root -crop 1x1+$1+$2 -depth 8 txt:- 2>/dev/null | sed -n 's/.*#\([0-9A-Fa-f]\{6\}\).*/\1/p'; }
nwin() { xdotool search --onlyvisible --class Flxp 2>/dev/null | wc -l; }
"$B" --geometry 800x500+0+0 "$F" > $T/out 2>&1 & P=$!
W=$(wait_window 30 --class Flxp)
if [ -z "$W" ]; then
    bad "no Explorer window"
else
    sleep 2
    ROW=$((107 + 17))           # the second row's middle
    xdotool mousemove 700 400; sleep .5
    idle=$(px 450 $ROW); n0=$(nwin)
    # the empty part of the Type column (right of "Text Document")
    xdotool mousemove 605 $ROW
    differs() { [ "$(px 450 $ROW)" != "$idle" ]; }
    if wait_for 3 differs; then ok "hover in the row's empty part: the row's hover look ($idle -> $(px 450 $ROW))"
    else bad "hover in the row's empty part: the row unchanged ($idle)"; fi
    tip() { [ "$(nwin)" -gt "$n0" ]; }
    if wait_for 5 tip; then ok "hover in the row's empty part: the infotip"
    else bad "hover in the row's empty part: no infotip ($(nwin) windows, $n0 before)"; fi
    xdotool click 1; sleep .3
    xdotool mousemove 700 400
    sel() { [ "$(px 450 $ROW)" != "$idle" ]; }
    if wait_for 3 sel; then ok "click in the row's empty part: the row selected ($(px 450 $ROW))"
    else bad "click in the row's empty part: the row not selected ($(px 450 $ROW))"; fi
    [ -n "$KEEP_SHOT" ] && import -window root "$KEEP_SHOT"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
stop_xvfb
[ $fails = 0 ] && echo "detailsrow: all passed" || { echo "detailsrow: $fails failed"; exit 1; }
