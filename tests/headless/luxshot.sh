#!/bin/sh
# luxshot under Xvfb (no window manager): `luxshot --screen FILE` writes a PNG of the display's size; then the window:
# a name, a description and tags typed, Enter saves into XDG_PICTURES_DIR, and the file carries user.lux.desc and
# user.xdg.tags as the Explorer writes them and its infotip reads them (build/tests/infotip). A HOME and XDG directories of
# its own. Needs Xvfb, xdotool, getfattr and setfattr (attr); prints "skip: ..." and passes without them or without user
# xattrs on $TMPDIR. Usage: tests/headless/luxshot.sh [:DISPLAY]
D=${1:-:74}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/luxshot
for t in Xvfb xdotool getfattr setfattr; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config $T/pics
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_STATE_HOME=$T/home/.state XDG_CACHE_HOME=$T/home/.cache \
       XDG_PICTURES_DIR=$T/pics DISPLAY=$D
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

# 2. the window: typed name, description, tags; Enter saves
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
