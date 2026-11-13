#!/bin/sh
# flxp follows a session theme switch without leaving anything of the old theme: started under one theme and switched twice
# (root _LH_THEME, as luxshell's flyout and lhde-theme set do), its window is read back and compared with a fresh start
# under the final theme: the list's icons (the theme's Desktop.icons set: XP's under luna, Windows 2000's under classic),
# the menu band, the toolbar and Address bands (their heights, the Address label, the icon set's Go button and throbber),
# the list's header and the face colours (classic's status bar COLOR_3DFACE 212 208 200); at most 0.5 % of the pixels may
# differ. Two runs: luna -> jade4074 -> classic and classic -> jade4074 -> luna. THEMESWITCH_KEEP=DIR keeps the pictures.
# Needs Xvfb, xdotool, xprop, xwd and ImageMagick, and luna and jade4074 imported (else skipped).
# Usage: tests/headless/themeswitch.sh [:DISPLAY]
D=${1:-:78}
R=$(cd "$(dirname "$0")/../.." && pwd)
IM=convert CMP=compare; command -v magick >/dev/null 2>&1 && IM=magick CMP="magick compare"
for t in Xvfb xdotool xprop xwd $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
for s in luna jade4074; do [ -e "$styles/$s.msstyles" ] || { echo "skip: $s not imported"; exit 0; }; done
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde $T/dir/alpha $T/dir/beta
printf 'text\n' > $T/dir/notes.txt
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS FLXP_THEME
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
px() { $IM "$1" -format '%[fx:int(255*p{'"$2"','"$3"'}.r)],%[fx:int(255*p{'"$2"','"$3"'}.g)],%[fx:int(255*p{'"$2"','"$3"'}.b)]' info:; }
theme() {   # the session theme, as lhde-theme set leaves it: settings.ini and the root property
    printf '[theme]\nname = %s\n' "$1" > $T/home/.config/lhde/settings.ini
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$1"
}
# open: flxp on the folder, its window id in $W
open() {
    "$R/build/bin/flxp" $T/dir > $T/flxp.log 2>&1 & P=$!
    W=$(wait_window 20 --name '^dir$')
    [ -n "$W" ] || { bad "flxp did not open its window"; return 1; }
    xdotool windowmove $W 0 0 2>/dev/null; xdotool windowsize $W 640 420 2>/dev/null
    sleep 2
}
shut() { kill $P 2>/dev/null; wait $P 2>/dev/null; }
grab() { sleep 2; W=$(wait_window 10 --name '^dir$'); xdotool windowmove $W 0 0 2>/dev/null; xdotool windowsize $W 640 420 2>/dev/null; sleep 1; xdotool mousemove --window $W 420 300 click 1; xdotool mousemove 1000 700; sleep 1; xwd -silent -id "$W" > $T/grab.xwd && $IM xwd:$T/grab.xwd "$1"; }
# compare A B NAME: the share of pixels that differ by more than a few levels
compare() {
    n=$($CMP -metric AE -fuzz 3% "$1" "$2" null: 2>&1 | sed 's/ .*//')
    total=$($IM "$1" -format '%[fx:w*h]' info:)
    [ -n "$THEMESWITCH_KEEP" ] && for f in switched fresh; do cp $T/$f.png "$THEMESWITCH_KEEP/${3%% *}-$f.png"; done
    if [ "${n%.*}" -le $((total / 200)) ] 2>/dev/null; then ok "$3: as a fresh start ($n of $total pixels differ)"
    else bad "$3: $n of $total pixels differ from a fresh start"; $CMP -fuzz 3% "$1" "$2" "$T/diff.png" 2>/dev/null; [ -n "$THEMESWITCH_KEEP" ] && for f in switched fresh diff; do cp $T/$f.png "$THEMESWITCH_KEEP/${3%% *}-$f.png"; done; fi
}
run() {   # run FIRST MIDDLE LAST
    theme $1
    open || return
    theme $2; sleep 3
    theme $3; grab $T/switched.png
    shut
    open || return
    grab $T/fresh.png
    shut
    compare $T/switched.png $T/fresh.png "$1 -> $2 -> $3"
    h=$($IM $T/switched.png -format '%h' info:)
    got=$(px $T/switched.png 300 $((h - 2))); want=$(px $T/fresh.png 300 $((h - 2)))   # the status bar: the face colour
    [ "$got" = "$want" ] && ok "$1 -> $2 -> $3: status bar $got" || bad "$1 -> $2 -> $3: status bar expected $want got $got"
    case $3 in classic) [ "$got" = 212,208,200 ] || bad "classic: status bar not COLOR_3DFACE 212,208,200: $got" ;; esac
}
run luna jade4074 classic
run classic jade4074 luna
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
