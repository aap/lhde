#!/bin/sh
# luxattr (the shell layer's Properties sheet from a terminal, on its Summary page) under Xvfb (no window manager). Two
# files with the same tags: the Summary page's Description typed, a tag added to Tags, OK writes both files as the Explorer
# writes them (getfattr; build/tests/infotip reads them back); Advanced >>: New Attribute... adds user.zz, then a row removed (Delete) and one edited in place (F2); and a run with nothing at all
# (LHDE_WINDOWS=/nonexistent, an empty HOME, no XDG variables) shows the sheet and exits 1 on Escape. A HOME and XDG
# directories of its own. Needs Xvfb, xdotool, getfattr and setfattr (attr); prints "skip: ..." and passes without them
# or without user xattrs on $TMPDIR. Usage: tests/headless/luxattr.sh [:DISPLAY]
D=${1:-:75}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/luxattr
for t in Xvfb xdotool getfattr setfattr; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config $T/files $T/bare
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_STATE_HOME=$T/home/.state XDG_CACHE_HOME=$T/home/.cache \
       XDG_DATA_HOME=$T/home/.local/share DISPLAY=$D
A=$T/files/a.txt; C=$T/files/b.txt
echo one > $A; echo two > $C
if ! setfattr -n user.xdg.tags -v "one,two" $A 2>/dev/null; then echo "skip: no user xattrs in $T"; rm -rf $T; exit 0; fi
setfattr -n user.xdg.tags -v "one,two" $C
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /tmp/.X11-unix/X${D#:} ] || break; sleep .3; done   # the test before on $D gone
Xvfb $D -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
sleep 1
ok=1
fail() { echo "FAIL: $*"; ok=0; }
attr() { getfattr --only-values -n "$1" "$2" 2>/dev/null; }

# the sheet for these arguments: its window id, focused ("" when it did not come)
sheet() {   # PATTERN
    W=
    for i in 1 2 3 4 5 6 7 8 9 10; do W=$(xdotool search --name "$1" 2>/dev/null | head -1); [ -n "$W" ] && break; sleep .5; done
    [ -n "$W" ] && { xdotool windowfocus --sync $W; sleep .3; }
}
# wait for P to exit; its status in $rc (999: still running, killed)
finish() {
    rc=999
    for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $P 2>/dev/null || break; sleep .5; done
    if kill -0 $P 2>/dev/null; then kill $P; wait $P; else wait $P; rc=$?; fi
}

# 1. the Summary page of two files: a description typed, a tag added, OK
$B $A $C 2>$T/err1.txt & P=$!
sheet 'a\.txt, b\.txt'
if [ -z "$W" ]; then fail "no sheet for a.txt, b.txt"; kill $P; wait $P
else
    xdotool key alt+d; sleep .2; xdotool type "Two test files"; sleep .2
    xdotool key alt+t; sleep .2; xdotool key End; xdotool type ", three"; sleep .3
    xdotool key Return
    finish
    [ $rc = 0 ] || fail "luxattr exited $rc after OK"
    for f in $A $C; do
        desc=$(attr user.lux.desc $f); tags=$(attr user.xdg.tags $f)
        echo "$(basename $f): user.lux.desc=$desc user.xdg.tags=$tags"
        [ "$desc" = "Two test files" ] || fail "$(basename $f): user.lux.desc is '$desc'"
        [ "$tags" = "one,two,three" ] || fail "$(basename $f): user.xdg.tags is '$tags'"
    done
    tip=$($TOP/build/tests/infotip $C)
    echo "$tip" | grep -qx "Description: Two test files" || fail "the infotip has no description: $tip"
    echo "$tip" | grep -qx "Tags: one, two, three" || fail "the infotip has no tags: $tip"
fi

# 2. Advanced >>: New Attribute... user.zz = raw, OK
$B $A 2>$T/err2.txt & P=$!
sheet 'a\.txt'
if [ -z "$W" ]; then fail "no sheet for a.txt"; kill $P; wait $P
else
    xdotool key alt+v; sleep .3; xdotool key alt+n; sleep .5
    xdotool type "zz"; xdotool key Tab; xdotool type "raw value"; sleep .2; xdotool key Return; sleep .5
    xdotool key Return
    finish
    [ $rc = 0 ] || fail "luxattr exited $rc after New Attribute and OK"
    [ "$(attr user.zz $A)" = "raw value" ] || fail "user.zz is '$(attr user.zz $A)'"
    [ "$(attr user.lux.desc $A)" = "Two test files" ] || fail "New Attribute changed the description"
fi

# 3. Advanced >>: the first row (user.lux.desc) removed, the next (user.xdg.tags) edited in place, OK
$B $A 2>$T/err3.txt & P=$!
sheet 'a\.txt'
if [ -z "$W" ]; then fail "no sheet for a.txt (3)"; kill $P; wait $P
else
    xdotool key alt+v; sleep .3; xdotool key Home; sleep .2; xdotool key space; sleep .2; xdotool key Delete; sleep .3
    xdotool key F2; sleep .3; xdotool type "x ; y"; sleep .2; xdotool key Return; sleep .3
    xdotool key Return
    finish
    [ $rc = 0 ] || fail "luxattr exited $rc after the list's edits"
    getfattr -n user.lux.desc $A >/dev/null 2>&1 && fail "user.lux.desc is still there: '$(attr user.lux.desc $A)'"
    [ "$(attr user.xdg.tags $A)" = "x,y" ] || fail "user.xdg.tags is '$(attr user.xdg.tags $A)', not x,y"
    [ "$(attr user.zz $A)" = "raw value" ] || fail "user.zz went"
    [ "$(attr user.lux.desc $C)" = "Two test files" ] || fail "b.txt changed"
    echo "a.txt: $(getfattr -d $A 2>/dev/null | grep = | tr '\n' ' ')"
fi

# 4. nothing imported, no session, no settings: the sheet in wux's defaults, Escape ends it
echo x > $T/bare/c.txt
env -i PATH=/usr/bin:/bin DISPLAY=$D LHDE_WINDOWS=/nonexistent HOME=$T/bare $B $T/bare/c.txt >$T/err4.txt 2>&1 & P=$!
sheet 'c\.txt'
if [ -z "$W" ]; then fail "no sheet without a Windows tree"; cat $T/err4.txt; kill $P; wait $P
else
    xdotool key Escape
    finish
    [ $rc = 1 ] || fail "luxattr exited $rc after Escape, not 1"
    echo "bare: sheet shown, Escape exits $rc"
fi

kill $XP
rm -rf $T
[ $ok = 1 ] && echo PASS || { echo FAIL; exit 1; }
