#!/bin/sh
# hover/press test on a lab display ($DISPLAY, default :2 as session/lab.sh): zooms of the composited screen and of the raw
# decoration window
export DISPLAY=${DISPLAY:-:2}; S=${1:-/tmp}; mkdir -p "$S"
w=$(xdotool search --name "^Terminal$" | head -1)
if [ -z "$w" ]; then (${LAB_TERM:-xterm} -geometry 70x18+80+90 -title Terminal >/dev/null 2>&1 &); sleep 2; w=$(xdotool search --name "^Terminal$" | head -1); fi
eval $(xdotool getwindowgeometry --shell $w); fx=$((X-9)); fy=$((Y-27)); fw=$((WIDTH+18)); fh=$((HEIGHT+37))
d=$(xwininfo -root -children | grep -a -E "^ +0x" | grep -a "${fw}x${fh}+$fx+$fy" | awk '{print $1}' | head -1)
echo "client $w at $X,$Y ${WIDTH}x${HEIGHT}; dec $d frame ${fw}x${fh}+$fx+$fy"
xdotool mousemove $((fx+fw-9-18)) $((fy+13)); sleep 0.8
import -window root "$S/hov_root.png"; magick "$S/hov_root.png" -crop 260x60+$((fx+fw-250))+$((fy-5)) +repage -scale 250% "$S/hov_root_zoom.png"
[ -n "$d" ] && import -window "$d" "$S/hov_dec.png" && magick "$S/hov_dec.png" -crop 260x40+$((fw-250))+0 +repage -scale 250% "$S/hov_dec_zoom.png" && echo "dec grabbed"
xdotool mousedown 1; sleep 0.5; import -window root "$S/press_root.png"; magick "$S/press_root.png" -crop 260x60+$((fx+fw-250))+$((fy-5)) +repage -scale 250% "$S/press_root_zoom.png"
xdotool mousemove $((fx+fw-9-18)) $((fy+70)); sleep 0.3; xdotool mouseup 1
echo "done; luxwm alive: $(pgrep -c -x luxwm)"
