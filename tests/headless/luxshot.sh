#!/bin/sh
# luxshot under Xvfb: `luxshot --screen FILE` writes a PNG of the display's size; `--region`: a click is the whole screen,
# a drag the rectangle, Shift+click on a window (luxwm's, Windows Classic, tests/w32/keystate) that window with its frame
# (_NET_FRAME_EXTENTS around it), Escape exits 1; then the window: a name, a description and tags typed, Enter saves into
# XDG_PICTURES_DIR, and the file carries user.lux.desc and user.xdg.tags as the Explorer writes them and its infotip reads
# them (build/tests/infotip). A HOME and XDG directories of its own. Needs Xvfb, xdotool, xprop, xwininfo, getfattr and
# setfattr (attr); prints "skip: ..." and passes without them or without user xattrs on $TMPDIR.
# Usage: tests/headless/luxshot.sh [:DISPLAY]
D=${1:-:74}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/luxshot
. "$(dirname "$0")/xattrs.sh"   # getfattr / setfattr over extattr where attr is not installed (FreeBSD)
for t in Xvfb xdotool xprop xwininfo getfattr setfattr; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config/lhde $T/pics
printf '[theme]\nname = classic\n' > $T/home/.config/lhde/settings.ini
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_STATE_HOME=$T/home/.state XDG_CACHE_HOME=$T/home/.cache \
       XDG_DATA_HOME=$T/home/.local/share XDG_PICTURES_DIR=$T/pics DISPLAY=$D LHDE_WINDOWS=/nonexistent
unset LHDE_DATA LHDE_SETTINGS
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /tmp/.X11-unix/X${D#:} ] || break; sleep .3; done   # the test before on $D gone
Xvfb $D -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
sleep 1
ok=1
fail() { echo "FAIL: $*"; ok=0; }

# 1. a script's screenshot: a PNG of 1024 x 768
$B --screen $T/screen.png || fail "luxshot --screen exited $?"
sig=$(od -An -c -j1 -N3 $T/screen.png 2>/dev/null | tr -d ' ')
wh=$(od -An -tu1 -j16 -N8 $T/screen.png 2>/dev/null | awk '{ printf "%d %d", $1*16777216+$2*65536+$3*256+$4, $5*16777216+$6*65536+$7*256+$8 }')
[ "$sig" = PNG ] || fail "screen.png is no PNG ($sig)"
[ "$wh" = "1024 768" ] && echo "screen.png: PNG $wh" || fail "screen.png is $wh, not 1024 768"

# pngsize FILE: "W H" from the IHDR
pngsize() { od -An -tu1 -j16 -N8 "$1" 2>/dev/null | awk '{ printf "%d %d", $1*16777216+$2*65536+$3*256+$4, $5*16777216+$6*65536+$7*256+$8 }'; }
# region FILE X Y XDOTOOL-ARGS...: luxshot --region FILE, the pointer at X,Y, then the given clicks; luxshot's exit status
region() {
    _f=$1; _x=$2; _y=$3; shift 3
    rm -f "$_f"
    $B --region "$_f" >/dev/null 2>&1 & _p=$!
    sleep 1.5; xdotool mousemove $_x $_y; sleep .2; xdotool "$@"
    for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $_p 2>/dev/null || break; sleep .3; done
    kill $_p 2>/dev/null; wait $_p
}

# 2. --region with a click: the whole screen; with Shift+click: the window under the pointer with luxwm's frame
region $T/click.png 500 400 click 1 || fail "a region click exited $?"
wh=$(pngsize $T/click.png)
[ "$wh" = "1024 768" ] && echo "click.png: $wh (the root)" || fail "a click gave $wh, not 1024 768"
region $T/drag.png 100 100 mousedown 1 sleep .1 mousemove 200 150 sleep .1 mousemove 300 250 sleep .1 mouseup 1 ||
    fail "a region drag exited $?"
wh=$(pngsize $T/drag.png)
[ "$wh" = "200 150" ] && echo "drag.png: $wh (the rectangle)" || fail "a drag gave $wh, not 200 150"
"$TOP/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
for i in 1 2 3 4 5 6 7 8 9 10; do xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id' && break; sleep .3; done
"$TOP/build/tests/w32/keystate" >/dev/null 2>&1 & PK=$!
K=
for i in 1 2 3 4 5 6 7 8 9 10; do K=$(xdotool search --onlyvisible --name '^keystate$' 2>/dev/null | head -1); [ -n "$K" ] && break; sleep .5; done
if [ -z "$K" ]; then fail "no keystate window"
else
    sleep .5
    eval "$(xwininfo -id $K | sed -n 's/^ *Absolute upper-left X: *\([0-9-]*\)/X=\1/p; s/^ *Absolute upper-left Y: *\([0-9-]*\)/Y=\1/p; s/^ *Width: *\([0-9]*\)/CW=\1/p; s/^ *Height: *\([0-9]*\)/CH=\1/p')"
    ext=$(xprop -id $K _NET_FRAME_EXTENTS | sed -n 's/.*= *//p' | tr -d ' ')
    fw=$((CW + $(echo $ext | cut -d, -f1) + $(echo $ext | cut -d, -f2))); fh=$((CH + $(echo $ext | cut -d, -f3) + $(echo $ext | cut -d, -f4)))
    echo "keystate: ${CW}x$CH at $X,$Y, extents $ext, frame ${fw}x$fh"
    [ -n "$ext" ] && [ "$fw" -gt "$CW" ] || fail "keystate has no frame extents ($ext)"
    region $T/win.png $((X + CW / 2)) $((Y + CH / 2)) keydown shift click 1 keyup shift || fail "a region shift+click exited $?"
    wh=$(pngsize $T/win.png)
    [ "$wh" = "$fw $fh" ] && echo "win.png: $wh (keystate with its frame)" || fail "a shift+click gave $wh, not $fw $fh"
fi
kill $PK $PW 2>/dev/null; wait $PK $PW 2>/dev/null

# 3. the window: typed name, description, tags; Enter saves
if ! touch $T/pics/probe || ! setfattr -n user.probe -v 1 $T/pics/probe 2>/dev/null; then
    echo "skip: no user xattrs in $T"; rm -f $T/pics/probe
else
    rm -f $T/pics/probe
    $B --region >/dev/null 2>&1 & RP=$!   # a region cancelled with Escape: no window, exit 1
    sleep 1.5; xdotool key Escape; sleep .5
    wait $RP; [ $? = 1 ] || fail "an Escaped region did not exit 1"
    $B --screen > $T/out.txt 2>$T/err.txt & P=$!
    W=
    for i in 1 2 3 4 5 6 7 8 9 10; do W=$(xdotool search --name '^Save Screenshot$' 2>/dev/null | head -1); [ -n "$W" ] && break; sleep .5; done
    if [ -z "$W" ]; then fail "no Save Screenshot window"; kill $P
    else
        xdotool windowfocus --sync $W; sleep .3
        xdotool key ctrl+a; xdotool type "tested"; sleep .2   # the name box has the focus, its text selected
        xdotool key alt+d; sleep .2; xdotool type "A headless screenshot"; sleep .2
        xdotool key alt+t; sleep .2; xdotool type "test, luxshot;headless"; sleep .3
        xdotool key Return
        for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $P 2>/dev/null || break; sleep .5; done
        if kill -0 $P 2>/dev/null; then fail "luxshot still runs after Enter"; kill $P; fi
        F=$T/pics/tested.png
        [ "$(cat $T/out.txt)" = "$F" ] || fail "luxshot printed '$(cat $T/out.txt)', not $F"
        [ -s "$F" ] || fail "$F was not written"
        desc=$(getfattr --only-values -n user.lux.desc "$F" 2>/dev/null)
        tags=$(getfattr --only-values -n user.xdg.tags "$F" 2>/dev/null)
        echo "user.lux.desc=$desc user.xdg.tags=$tags"
        [ "$desc" = "A headless screenshot" ] || fail "user.lux.desc is '$desc'"
        [ "$tags" = "test,luxshot,headless" ] || fail "user.xdg.tags is '$tags'"
        tip=$($TOP/build/tests/infotip "$F")
        echo "$tip" | grep -qx "Description: A headless screenshot" || fail "the infotip has no description: $tip"
        echo "$tip" | grep -qx "Tags: test, luxshot, headless" || fail "the infotip has no tags: $tip"
    fi
fi
kill $XP
rm -rf $T
[ $ok = 1 ] && echo PASS || { echo FAIL; exit 1; }
