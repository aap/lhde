#!/bin/sh
# The Longhorn pane's "Filter by" box (flxp under jade4074, no window manager): typing narrows the list in place as
# typed, over the names and the stored attributes (user.lux.desc, user.xdg.tags, user.xdg.comment); every word must
# match; tag: / desc: / comment: look at one field; a tag added on disk while the filter is on shows at once (inotify's
# attribute change); Escape empties the box. The list's item count is read from flxp's FLXP_TRACE_LIST lines ("list: N
# of M", LVM_GETITEMCOUNT after every change). A HOME and XDG directories of its own. Needs Xvfb, xdotool, setfattr and
# the user's (or LHDE_WINDOWS's) jade4074 style; prints "skip: ..." and passes without them or without user xattrs on
# $TMPDIR. Usage: tests/headless/filter.sh [:DISPLAY]
D=${1:-:74}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
for t in Xvfb xdotool setfattr; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$TOP/build/bin/lhde-theme" find styles 2>/dev/null)   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
[ -f "$styles/jade4074.msstyles" ] || { echo "skip: no jade4074 style imported"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
F=$T/files
mkdir -p $T/home/.config/lhde "$F/Projects"
if ! touch "$F/probe" || ! setfattr -n user.probe -v 1 "$F/probe" 2>/dev/null; then echo "skip: no user xattrs in $T"; exit 0; fi
rm -f "$F/probe"
for f in alpha.txt beta.txt gamma.png delta.md notes.txt; do echo x > "$F/$f"; done
setfattr -n user.xdg.tags -v "red,work" "$F/alpha.txt"
setfattr -n user.xdg.tags -v "blue" "$F/beta.txt"
setfattr -n user.lux.desc -v "A red sunset" "$F/gamma.png"
setfattr -n user.xdg.comment -v "work in progress" "$F/delta.md"
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache LHDE_WINDOWS=${styles%/index/styles} DISPLAY=$D FLXP_TRACE_LIST=1
unset LHDE_DATA LHDE_SETTINGS
printf '[theme]\nname = jade4074\n' > $T/home/.config/lhde/settings.ini
printf '[flxp]\ntaskpane = 0\nstatusbar_longhorn = 1\n' > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
"$B" --geometry 800x500+0+0 "$F" > $T/out 2>&1 & P=$!
W=$(wait_window 30 --class Flxp)
count() { sed -n 's/^list: //p' $T/out | tail -n 1; }
lines() { grep -c '^list: ' $T/out; }
# type TEXT: the keys into the box, then wait until the list has been filled once more per key (or 5 s)
type_in() {
    n=$(lines)
    xdotool type --delay 40 "$1"
    want=$((n + $(printf %s "$1" | wc -c)))
    wait_for 5 test "$(lines)" -ge $want
    sleep .3
}
key() { n=$(lines); xdotool key "$1"; wait_for 5 test "$(lines)" -gt $n; sleep .3; }
if [ -z "$W" ]; then
    echo "FAIL no Explorer window"; fails=1
else
    wait_for 10 test -n "$(count)"
    check "the folder" "6 of 6" "$(count)"
    # the Filter box: the pane's first row under the 4074 header and the menu band (8..194 x 84..105 at 800 x 500)
    xdotool mousemove --window $W 100 94 click 1; sleep .5
    type_in "red";                check "red: the tag of alpha, the description of gamma" "2 of 6" "$(count)"
    key BackSpace; key BackSpace; key BackSpace
    check "emptied" "6 of 6" "$(count)"
    type_in "WORK";               check "WORK: alpha's tag, delta's comment (case ignored)" "2 of 6" "$(count)"
    type_in " red";               check "WORK red: every word" "1 of 6" "$(count)"
    xdotool key ctrl+a; sleep .2; key BackSpace
    type_in "tag:";               check "tag: the tagged" "2 of 6" "$(count)"
    type_in "b";                  check "tag:b blue only" "1 of 6" "$(count)"
    setfattr -n user.xdg.tags -v "blueprint" "$F/notes.txt"
    wait_for 5 test "$(count)" = "2 of 6"; check "a tag set on disk shows" "2 of 6" "$(count)"
    xdotool key ctrl+a; sleep .2; key BackSpace
    type_in "desc:sun";           check "desc:sun" "1 of 6" "$(count)"
    xdotool key ctrl+a; sleep .2; key BackSpace
    type_in "comment:work";       check "comment:work: not alpha's tag" "1 of 6" "$(count)"
    xdotool key ctrl+a; sleep .2; key BackSpace
    type_in "tag:\"red\" alpha";  check "a quoted word and a name" "1 of 6" "$(count)"
    key Escape;                   check "Escape: the whole folder" "6 of 6" "$(count)"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
