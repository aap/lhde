#!/bin/sh
# flxp-taskbar's Start button with Shell.start_icon (settings.ini [taskbar] start_icon) on Xvfb, under classic and, when it is
# imported, luna: the button read back with ImageMagick. Without the key and with start_icon = windows the button is the same
# image, byte for byte (the flag as before); with linux it differs only inside the flag's cell (the left 16 px of the classic
# button's content, the left 20 of luna's) and there in many pixels; the key changed while the bar runs (with
# _LH_SETTINGS_CHANGED) redraws it as a fresh start would, and back to windows gives the flag again. windows:BUILD: a stand-in
# build's flag differs from the theme's inside the cell only, as 2000's does when imported (and differs from the stand-in's);
# windows:4074 (its explorer.exe has no Start bitmap) is the white flag of jade4074's CompositedWindow.DEFAULTICON when that style
# is imported, inside the cell only and different from the stand-in's, else the theme's flag byte for byte, as a build not
# imported is.
# Needs Xvfb, xdotool, xprop and ImageMagick. Usage: tests/headless/starticon.sh [:DISPLAY]
D=${1:-:76}
R=$(cd "$(dirname "$0")/../.." && pwd)
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
CMP=compare; command -v magick >/dev/null 2>&1 && CMP="magick compare"
for t in Xvfb xdotool xprop import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
luna=$("$R/build/bin/lhde-theme" find styles/luna.msstyles 2>/dev/null)   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
[ -z "$luna" ] && luna=$("$R/build/bin/lhde-theme" find styles/luna4051.msstyles 2>/dev/null)
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
k2=$("$R/build/bin/lhde-theme" find resources/2k/explorer.exe 2>/dev/null)     # 2000's flag (16 px, 4-bit), when imported
k4074=$("$R/build/bin/lhde-theme" find resources/4074/explorer.exe 2>/dev/null) # 4074's explorer.exe: no Start bitmap
jade=$("$R/build/bin/lhde-theme" find styles/jade4074.msstyles 2>/dev/null)     # 4074's flag: its CompositedWindow.DEFAULTICON
T=$(mktemp -d)
trap '[ -n "$KEEP" ] || rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D FLXP_TASKBAR_LOG=1
[ -n "$styles" ] && export LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS LHDE_UNAME
# a stand-in build zzflag in the scratch ~/.config/lhde (the first data directory): an explorer.exe (a 32-bit .res, which flxp
# reads as a PE) with a 25 x 20 IDB_START 143 (a magenta flag in the left 20 columns) and a 16 x 16 IDB_STARTCLASSIC 176 (cyan)
mkdir -p $T/home/.config/lhde/resources/zzflag
python3 -I - $T/home/.config/lhde/resources/zzflag/explorer.exe <<'EOF' || { echo "FAIL the stand-in explorer.exe"; exit 1; }
import struct, sys
def dib(w, h, px):
    d = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 32, 0, 0, 0, 0, 0, 0)
    return d + b''.join(struct.pack('<I', px(x, y)) for y in range(h - 1, -1, -1) for x in range(w))
def entry(rid, data):
    e = struct.pack('<IIHHHHIHHII', len(data), 32, 0xffff, 2, 0xffff, rid, 0, 0x30, 1033, 0, 0) + data
    return e + b'\0' * (-len(e) % 4)
res = struct.pack('<IIHHHH', 0, 32, 0xffff, 0, 0xffff, 0) + b'\0' * 16
res += entry(143, dib(25, 20, lambda x, y: 0xffe000e0 if 2 <= x < 18 and 2 <= y < 18 else 0))
res += entry(176, dib(16, 16, lambda x, y: 0xff00d0d0 if 1 <= x < 15 and 1 <= y < 15 else 0))
open(sys.argv[1], 'wb').write(res)
EOF
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
    builds $th $cell
}

# inside_cell THEME BUILD CELL: windows:BUILD's button against windows' differs in many pixels, all inside the flag's cell
inside_cell() {
    set -- "$1" "$2" "$3" $(diff_box $T/$1-windows.png $T/$1-$2.png)
    n=$4 box=$5
    x=${box#*+}; x=${x%%+*}; w=${box%%x*}
    if [ "${n:-0}" -ge 40 ] && [ $((x + w)) -le $(($3 + 1)) ] 2>/dev/null; then ok "$1: windows:$2 differs from the theme's flag in $n pixels, all inside the flag's cell ($box)"
    else bad "$1: windows:$2 against the theme's flag: $n pixels in $box (the cell: the first $3 px)"; fi
}
# windows:BUILD: the stand-in zzflag's own flag, 2000's when imported; 4074's from jade4074 when imported (its explorer.exe has
# no Start bitmap); a build not imported has none: the theme's flag
builds() {
    th=$1 cell=$2
    for v in windows:zzflag windows:4074 windows:9999; do bar $th $v; shot $T/$th-${v#windows:}.png; unbar; done
    inside_cell $th zzflag $cell
    if [ -n "$jade" ]; then
        inside_cell $th 4074 $cell
        if same $T/$th-4074.png $T/$th-zzflag.png; then bad "$th: windows:4074 is the stand-in's flag"
        else ok "$th: windows:4074 (jade4074's white flag) differs from the stand-in's flag"; fi
        vs=windows:9999
    else vs="windows:4074 windows:9999"; fi
    for v in $vs; do
        why="not imported"; [ $v = windows:4074 ] && [ -n "$k4074" ] && why="its explorer.exe has no IDB_START, jade4074 not imported"
        if same $T/$th-windows.png $T/$th-${v#windows:}.png; then ok "$th: $v ($why) is the theme's flag"
        else bad "$th: $v ($why): $(diff_box $T/$th-windows.png $T/$th-${v#windows:}.png)"; fi
    done
    if [ -n "$k2" ]; then
        bar $th windows:2k; shot $T/$th-2k.png; unbar
        inside_cell $th 2k $cell
        if same $T/$th-2k.png $T/$th-zzflag.png; then bad "$th: windows:2k is the stand-in's flag"
        else ok "$th: windows:2k differs from the stand-in's flag"; fi
    else echo "skip $th windows:2k: resources/2k/explorer.exe not imported"; fi
}

# classic: the flag 4 px in (the 2 px edge and 2), 16 px wide; luna: the content margin, then XP's 20 px flag in a 25 px cell
run classic 21
if [ -n "$luna" ]; then run luna 30; else echo "skip luna: not imported"; fi
stop_xvfb
echo "$fails failed"
exit $fails
