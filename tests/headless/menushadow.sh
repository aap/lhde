#!/bin/sh
# luxshell's context menus have the drop shadow of luxwm's window menu: XP's, UT_MENU_SHADOW (5) px right and below, the alpha
# of wuxtheme's ut_menu_shadow_alpha. Without a compositor both darken a copy of the screen under the menu, so the shadow's
# alpha at each pixel is 1 - menu / background (the menu window's own pixels, xwd -id, against a screenshot taken before it
# opened). luxshell's menu painter
# (lh.menu_render in menupaint.c, the pixels menu.lua shows) is run by a small luxshell script that puts one menu over the plain
# root, as menu.lua does without a compositor; luxwm's window menu opens with Alt+Space over a white xterm. The alpha must be
# the same over the right strip's first 15 rows, the bottom strip's first 15 columns and the corner, and the right strip at
# row 10 XP's ramp (113 84 43 14 3 / 255). With settings.ini [effects] menu_shadows = 0 both menu windows are 5 px smaller: no
# shadow. Xvfb with a scratch HOME. Needs Xvfb, xdotool, xwininfo, xwd, xsetroot, xterm, python3 and ImageMagick.
# Usage: tests/headless/menushadow.sh [:DISPLAY]
D=${1:-:82}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xwininfo xwd xsetroot xterm python3 magick; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x "$R/build/bin/luxshell" ] && [ -x "$R/build/bin/luxwm" ] || { echo "skip: luxwm or luxshell is not built"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D PATH="$R/build/bin:$PATH"
unset DBUS_SESSION_BUS_ADDRESS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
xsetroot -solid '#3a6ea5'
# luxshell with a script of its own: one menu at 100,100 whose shadow is made from the root under it
cat > $T/menu.lua <<'EOF'
local items = { { text = "&Restore" }, { text = "&Move" }, { text = "&Size" }, { separator = true }, { text = "&Close", accel = "Alt+F4" } }
local r = lh.menu_render(items, nil, { key = "@menu:1", under = { 100, 100 } })
local s = lh.surface{ name = "luxshell-menu", popup = true, type = "menu", x = 100, y = 100, w = 10, h = 10 }
local root = lh.node{ layout = "canvas" }
root:add(lh.node{ kind = "image", image = r.image, x = 0, y = 0, w = r.w + r.shadow, h = r.h + r.shadow })
s:root(root); s:move(100, 100, r.w + r.shadow, r.h + r.shadow); s:show(); s:raise()
EOF
xterm -geometry 80x30+400+60 -bg white -fg black > /dev/null 2>&1 & PX=$!
XW=$(wait_window 10 --class '^XTerm$') || bad "no xterm"
sleep 1
geom() { xwininfo -id "$1" | awk '/Absolute upper-left X/ {x=$NF} /Absolute upper-left Y/ {y=$NF} /Width:/ {w=$NF} /Height:/ {h=$NF} END {print x, y, w, h}'; }
menu_win() { xdotool search --onlyvisible --classname "^$1\$" 2>/dev/null | head -1; }
shown_win() { W=$(menu_win $1); [ -n "$W" ]; }
gone_win() { [ -z "$(menu_win $1)" ]; }
# open_menu NAME TAG: the background, the menu (luxshell's script; luxwm's Alt+Space on the xterm), its geometry, the shot
PS=
open_menu() {
    xdotool windowactivate --sync $XW 2>/dev/null; xdotool mousemove 1000 740; sleep .3
    xwd -root -silent > $T/$2-bg.xwd
    if [ "$1" = luxshell-menu ]; then "$R/build/bin/luxshell" $T/menu.lua > $T/luxshell.log 2>&1 & PS=$!
    else xdotool key alt+space; fi
    wait_for 5 shown_win $1 || { bad "$2: no menu window ($1)"; [ -n "$PS" ] && kill $PS; PS=; return 1; }
    sleep .4
    geom $W > $T/$2.geom
    xwd -id $W -silent > $T/$2.xwd
    if [ -n "$PS" ]; then kill $PS; wait $PS 2>/dev/null; PS=; else xdotool key Escape; fi
    wait_for 5 gone_win $1
    magick xwd:$T/$2.xwd -depth 8 rgb:$T/$2.rgb && magick xwd:$T/$2-bg.xwd -depth 8 rgb:$T/$2-bg.rgb
}
cat > $T/alpha.py <<'EOF'
import sys
SW = 1024   # the screen's width: the raw RGB rows
def alpha(t):
    x, y, W, H = map(int, open(sys.argv[1] + "/" + t + ".geom").read().split())   # the window's pixels: W x H
    s = open(sys.argv[1] + "/" + t + ".rgb", "rb").read(); b = open(sys.argv[1] + "/" + t + "-bg.rgb", "rb").read()
    w, h = W - 5, H - 5
    def a(px, py):
        o = ((y + py) * SW + x + px) * 3; q = (py * W + px) * 3
        bs, ss = sum(b[o:o+3]), sum(s[q:q+3])
        return None if bs < 90 else 1 - ss / bs
    right = [[a(w + c, r) for c in range(5)] for r in range(min(15, h))]
    bottom = [[a(c, h + r) for c in range(min(15, w))] for r in range(5)]
    corner = [[a(w + c, h + r) for c in range(5)] for r in range(5)]
    return right, bottom, corner, (W, H)
A, B = alpha("shell"), alpha("wm")
fails = 0
for name, i in (("right strip", 0), ("bottom strip", 1), ("corner", 2)):
    d, n = 0.0, 0
    for ra, rb in zip(A[i], B[i]):
        for va, vb in zip(ra, rb):
            if va is not None and vb is not None: d = max(d, abs(va - vb)); n += 1
    good = n >= 25 and d <= 0.03
    print(("ok   " if good else "FAIL ") + "%s: luxshell's shadow alpha = luxwm's (%d pixels, largest difference %.3f)" % (name, n, d))
    fails += not good
ramp = [113, 84, 43, 14, 3]
row = A[0][10] if len(A[0]) > 10 else []
d = max((abs(v * 255 - r) for v, r in zip(row, ramp) if v is not None), default=99)
good = d <= 8
print(("ok   " if good else "FAIL ") + "luxshell's right strip at row 10 is XP's ramp 113 84 43 14 3 (%s)" % " ".join("%.0f" % (v * 255) if v is not None else "-" for v in row))
fails += not good
print("geometry %d %d %d %d" % (A[3] + B[3]))
sys.exit(fails)
EOF
if open_menu luxshell-menu shell && open_menu lhwm-menu wm; then
    python3 -I $T/alpha.py $T > $T/alpha.log; rc=$?
    grep -v '^geometry' $T/alpha.log; fails=$((fails + rc))
    set -- $(sed -n 's/^geometry //p' $T/alpha.log); SW0=$1 SH0=$2 WW0=$3 WH0=$4
    # [effects] menu_shadows = 0: the same menus, 5 px smaller
    printf '[effects]\nmenu_shadows = 0\n' >> $T/home/.config/lhde/settings.ini
    if open_menu luxshell-menu shell0 && open_menu lhwm-menu wm0; then
        set -- $(cat $T/shell0.geom); [ "$3" = $((SW0 - 5)) ] && [ "$4" = $((SH0 - 5)) ] && ok "menu_shadows = 0: luxshell's menu has no shadow (${3}x$4)" \
            || bad "menu_shadows = 0: luxshell's menu is ${3}x$4, not $((SW0 - 5))x$((SH0 - 5))"
        set -- $(cat $T/wm0.geom); [ "$3" = $((WW0 - 5)) ] && [ "$4" = $((WH0 - 5)) ] && ok "menu_shadows = 0: luxwm's menu has no shadow (${3}x$4)" \
            || bad "menu_shadows = 0: luxwm's menu is ${3}x$4, not $((WW0 - 5))x$((WH0 - 5))"
    fi
fi
kill $PX 2>/dev/null; wait $PX 2>/dev/null
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
