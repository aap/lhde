#!/bin/sh
# flxp's Properties (the shell layer's sheet, shell::properties) from the item context menu, under Xvfb without a window
# manager and with nothing imported: a right click on a tagged file, then r (P&roperties), opens "a.txt Properties" (XP's
# caption) on General (Ctrl+Tab then reaches Metadata, where Alt+D is the description); a description typed there, OK, and
# getfattr has it, the tags kept. Add Tag... from the same menu adds a tag. The Metadata page's widgets: two tags typed into
# the tag box, each added with Enter, a tag removed from the list with Delete, the second swatch chosen with the keyboard
# (Alt+L, Right, Space: Red), a comment of two lines (Enter in it is a new line), Run as typed; OK writes user.xdg.tags,
# user.lux.color = red, user.xdg.comment with its newline and user.lux.run; a later description leaves the comment's lines.
# With both files selected Properties is one sheet ("a.txt, b.txt Properties") whose description goes to both, a tag added
# there goes to both, each keeping its own. On a read-only file the write fails: a message box (wux's MessageBoxW, captioned as
# the sheet) over the sheet, which stays; nothing is written; Escape closes the sheet.
# Needs Xvfb, xdotool, getfattr and setfattr; prints "skip: ..." and passes without them or without user xattrs on $TMPDIR.
# Usage: tests/headless/properties.sh [:DISPLAY]
D=${1:-:76}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
. "$(dirname "$0")/xattrs.sh"   # getfattr / setfattr over extattr where attr is not installed (FreeBSD)
for t in Xvfb xdotool getfattr setfattr; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
trap 'chmod -R u+w "$T"; rm -rf "$T"' EXIT
F=$T/files
mkdir -p $T/home/.config/lhde "$F" "$T/ro"
if ! touch "$F/probe" || ! setfattr -n user.probe -v 1 "$F/probe" 2>/dev/null; then echo "skip: no user xattrs in $T"; exit 0; fi
rm -f "$F/probe"
echo one > "$F/a.txt"; echo two > "$F/b.txt"; echo three > "$T/ro/c.txt"
setfattr -n user.xdg.tags -v "red,work" "$F/a.txt"
setfattr -n user.lux.desc -v "kept" "$T/ro/c.txt"
chmod a-w "$T/ro/c.txt"
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache LHDE_WINDOWS=/nonexistent DISPLAY=$D
unset LHDE_DATA LHDE_SETTINGS
printf '[flxp]\ntaskpane = 0\n' > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1: $3"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
attr() { getfattr --only-values -n "$1" "$2" 2>/dev/null; }
nwin() { xdotool search --onlyvisible --name "$1" 2>/dev/null | wc -l; }
gone() { [ "$(nwin "$1")" = 0 ]; }
two() { [ "$(nwin "$1")" -ge 2 ]; }
focus() { timeout 10 xdotool windowfocus --sync "$1" 2>/dev/null; sleep .3; }
# the Explorer on DIR, focused; $W its window. menu VERB-KEY: a right click on the first row (y 101 at 800 x 500 in the
# classic look: it selects that item unless it is selected already) and the item's mnemonic
explorer() {
    "$B" --geometry 800x500+0+0 "$1" > $T/out 2>&1 & P=$!
    W=$(wait_window 30 --class Flxp)
    [ -n "$W" ] && { focus $W; sleep .5; }
}
menu() { focus $W; xdotool mousemove --window $W 260 101 click 3; sleep .7; xdotool key "$1"; }
close_explorer() { kill $P 2>/dev/null; wait $P 2>/dev/null; }

# 1. one tagged file: Properties from the menu, General first, Metadata's description, OK
explorer "$F"
if [ -z "$W" ]; then echo "FAIL no Explorer window"; fails=$((fails + 1)); else
    menu r
    S=$(wait_window 20 --name '^a\.txt Properties$')
    check "the sheet's caption" "yes" "$([ -n "$S" ] && echo yes || echo no)"
    if [ -n "$S" ]; then
        focus $S
        xdotool key ctrl+Tab; sleep .5
        xdotool key alt+d; sleep .2; xdotool type "a described file"; sleep .2
        xdotool key Return
        wait_for 10 gone '^a\.txt Properties$'
        check "a.txt: user.lux.desc" "a described file" "$(attr user.lux.desc "$F/a.txt")"
        check "a.txt: user.xdg.tags kept" "red,work" "$(attr user.xdg.tags "$F/a.txt")"
    fi

    # 2. Add Tag... from the same menu
    menu g
    A=$(wait_window 20 --name '^Add Tag$')
    if [ -n "$A" ]; then
        focus $A; xdotool type "blue"; sleep .2; xdotool key Return; wait_for 10 gone '^Add Tag$'
    fi
    check "a.txt: Add Tag" "red,work,blue" "$(attr user.xdg.tags "$F/a.txt")"

    # 2b. the Metadata page's widgets: two tags added in the tag box (Enter adds), "work" removed from the list (Home, Down:
    # the second row; Delete), the colour Red (Alt+L: None; Right: Red; Space), a comment of two lines, Run as; OK from Run as
    menu r
    S=$(wait_window 20 --name '^a\.txt Properties$')
    if [ -z "$S" ]; then echo "FAIL no sheet for the widgets"; fails=$((fails + 1)); else
        focus $S; xdotool key ctrl+Tab; sleep .5
        xdotool key alt+t; sleep .2; xdotool type "green"; xdotool key Return; sleep .2
        xdotool type "yellow"; xdotool key Return; sleep .2
        xdotool key Tab Tab; sleep .2; xdotool key Home; sleep .1; xdotool key Down; sleep .1; xdotool key Delete; sleep .2
        xdotool key alt+l; sleep .2; xdotool key Right; sleep .1; xdotool key space; sleep .2
        xdotool key alt+o; sleep .2; xdotool type "line one"; xdotool key Return; xdotool type "line two"; sleep .2
        xdotool key alt+r; sleep .2; xdotool type "python3 %f"; sleep .2
        xdotool key Return
        wait_for 10 gone '^a\.txt Properties$'
        check "widgets: user.xdg.tags" "red,blue,green,yellow" "$(attr user.xdg.tags "$F/a.txt")"
        check "widgets: user.lux.color" "red" "$(attr user.lux.color "$F/a.txt")"
        check "widgets: user.lux.run" "python3 %f" "$(attr user.lux.run "$F/a.txt")"
        check "widgets: user.xdg.comment" "line one
line two" "$(attr user.xdg.comment "$F/a.txt")"
        check "widgets: the description kept" "a described file" "$(attr user.lux.desc "$F/a.txt")"
    fi
    # a description changed later leaves the comment's lines
    menu r
    S=$(wait_window 20 --name '^a\.txt Properties$')
    if [ -n "$S" ]; then
        focus $S; xdotool key ctrl+Tab; sleep .5
        xdotool key alt+d; sleep .2; xdotool key ctrl+a; xdotool type "described again"; sleep .2; xdotool key Return
        wait_for 10 gone '^a\.txt Properties$'
    fi
    check "the comment survives" "line one
line two" "$(attr user.xdg.comment "$F/a.txt")"
    check "described again" "described again" "$(attr user.lux.desc "$F/a.txt")"

    # 3. both selected (Ctrl+A in the list): one sheet, the description written to both; a tag added to both, each keeping
    # its own
    focus $W; xdotool key ctrl+a; sleep .3
    menu r
    S=$(wait_window 20 --name '^a\.txt, b\.txt Properties$')
    check "two files: one sheet" "yes" "$([ -n "$S" ] && echo yes || echo no)"
    if [ -n "$S" ]; then
        focus $S; xdotool key ctrl+Tab; sleep .5
        xdotool key alt+d; sleep .2; xdotool type "both"; sleep .2
        xdotool key alt+t; sleep .2; xdotool type "shared"; xdotool key Return; sleep .2; xdotool key Return
        wait_for 10 gone '^a\.txt, b\.txt Properties$'
        check "a.txt after two" "both" "$(attr user.lux.desc "$F/a.txt")"
        check "b.txt after two" "both" "$(attr user.lux.desc "$F/b.txt")"
        check "a.txt: its tags and the shared one" "red,blue,green,yellow,shared" "$(attr user.xdg.tags "$F/a.txt")"
        check "b.txt: the shared tag" "shared" "$(attr user.xdg.tags "$F/b.txt")"
    fi
fi
close_explorer

# 4. a read-only file: the message box over the sheet, nothing written, the sheet stays
explorer "$T/ro"
if [ -n "$W" ]; then
    menu r
    S=$(wait_window 20 --name '^c\.txt Properties$')
    if [ -z "$S" ]; then echo "FAIL no sheet for c.txt"; fails=$((fails + 1)); else
        focus $S; xdotool key ctrl+Tab; sleep .5
        xdotool key alt+d; sleep .2; xdotool key ctrl+a; xdotool type "changed"; sleep .2; xdotool key Return
        wait_for 10 two '^c\.txt Properties$'
        check "read-only: a message box over the sheet" 2 "$(nwin '^c\.txt Properties$')"
        xdotool key Return; sleep .5
        check "read-only: the sheet stays" 1 "$(nwin '^c\.txt Properties$')"
        check "read-only: nothing written" "kept" "$(attr user.lux.desc "$T/ro/c.txt")"
        S=$(xdotool search --onlyvisible --name '^c\.txt Properties$' | head -1); focus $S
        xdotool key Escape; wait_for 10 gone '^c\.txt Properties$'
        check "read-only: Escape closes it" 0 "$(nwin '^c\.txt Properties$')"
    fi
fi
close_explorer
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
