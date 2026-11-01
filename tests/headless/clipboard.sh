#!/bin/sh
# Copy A then B in one explorer process, paste each in another: the second Paste
# must paste B (it pasted A again: the pasting process reused the list it had
# read from the X CLIPBOARD the first time). xclip shows what is offered.
# Needs Xvfb, xdotool, xclip. Usage: tests/headless/clipboard.sh [:DISPLAY]
D=${1:-:71}
T=$(mktemp -d)
B=$(cd "$(dirname "$0")/../.." && pwd)/build/bin/flxp
mkdir -p $T/home/.config $T/src $T/dst; echo a >$T/src/alpha.txt; echo b >$T/src/beta.txt
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D   # the scratch home only: nothing exported from outside leaks in
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1280x800x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
$B --geometry 600x500+0+0 $T/src 2>/dev/null & P1=$!
$B --geometry 600x500+650+0 $T/dst 2>/dev/null & P2=$!
W1=$(wait_window 30 --name '^src$')
W2=$(wait_window 30 --name '^dst$')
sleep .5   # their lists filled after the map
xdotool mousemove --window $W1 400 400 click 1; sleep .3
xdotool mousemove --window $W2 400 400 click 1; sleep .3
copy() { xdotool windowfocus --sync $W1; sleep .3; xdotool key ctrl+a; sleep .2; xdotool type "$1"; sleep .4; xdotool key ctrl+c; sleep .5
         echo "offered: $(xclip -o -selection clipboard -t x-special/gnome-copied-files | tr '\n' ' ')"; }
paste() { rm -f $T/dst/*; xdotool windowfocus --sync $W2; sleep .3; xdotool key ctrl+v; sleep .8; echo "pasted: $(ls $T/dst)"; }
ok=1
copy alpha; paste; [ -e $T/dst/alpha.txt ] || ok=0
copy beta; paste; [ -e $T/dst/beta.txt ] && [ ! -e $T/dst/alpha.txt ] || ok=0
kill $P1 $P2; wait $P1 $P2 2>/dev/null; stop_xvfb
rm -rf $T
[ $ok = 1 ] && echo PASS || { echo FAIL; exit 1; }
