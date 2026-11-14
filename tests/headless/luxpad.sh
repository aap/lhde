#!/bin/sh
# luxpad under Xvfb (no window manager), driven with xdotool, its status bar read from LUXPAD_TRACE=1's "status PART TEXT"
# lines. The status bar off until View > Status Bar (Alt+V from the Edit control drops View, S), which luxpad.ini keeps. A UTF-8 file with CR LF line ends and a character above U+FFFF opens as UTF-8 / Windows (CR + LF); an emoji typed
# into it and Ctrl+S: the file has the emoji's four bytes where it was typed and is otherwise byte for byte what it was (the
# 𝄞, the ü, every CR LF); Ln/Col after moving (Ln 2, Col 3); Find (Ctrl+F "third", Find Next: Ln 3, Col 6); Go To
# (Ctrl+G 2: Ln 2, Col 1); Save As with the Encoding box on Unicode: UTF-16 LE with a BOM, the same text, CR LF; that file
# reopened says Unicode and saves back identical; a file that is not UTF-8 (Latin-1 bytes) opens as ANSI and saves back
# identical; LF stays LF; closing with a change asks (the XP box), No leaves the file; the window placement from
# luxpad.ini's state file; a file dragged from flxp onto the edit opens; and a run with nothing imported, an empty HOME
# and no XDG variables. A HOME and XDG directories of its own. Needs Xvfb, xdotool and python3; prints "skip: ..." and
# passes without them. Usage: tests/headless/luxpad.sh [:DISPLAY]
D=${1:-:75}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/luxpad
for t in Xvfb xdotool python3; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config $T/home/.state/lhde $T/files $T/drop $T/bare
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_STATE_HOME=$T/home/.state XDG_CACHE_HOME=$T/home/.cache \
       XDG_DATA_HOME=$T/home/.local/share DISPLAY=$D LUXPAD_TRACE=1
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1280x800x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; rm -rf $T; exit 1; }
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
ok() { echo "ok   $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
hex() { python3 -I -c 'import sys; print(open(sys.argv[1], "rb").read().hex())' "$1" 2>/dev/null; }
# the last status bar text of a part (pos, eoln, encoding), its leading blanks dropped
status() { grep "^status $1 " $T/out.log | tail -1 | sed "s/^status $1 *//"; }
is_status() { [ "$(status "$1")" = "$2" ]; }
is_hex() { [ "$(hex "$1")" = "$2" ]; }
# luxpad on FILE; its window (matching TITLE) in $W, focused
run() {   # TITLE FILE
    : > $T/out.log
    $B "$2" >> $T/out.log 2>$T/err.log & P=$!
    W=$(wait_window 30 --name "$1")
    [ -n "$W" ] && { timeout 10 xdotool windowfocus --sync $W 2>/dev/null; sleep .3; }
}
# Alt+F4 and wait for luxpad to exit (killed after 10 s)
quit() {
    timeout 10 xdotool windowfocus --sync $W 2>/dev/null; xdotool key alt+F4
    for i in $(seq 50); do kill -0 $P 2>/dev/null || break; sleep .2; done
    kill -0 $P 2>/dev/null && { kill $P; fail "luxpad did not exit"; }
    wait $P 2>/dev/null
}
# the File menu's Save As (the menu bar is the window's top row without a window manager)
save_as() {
    xdotool mousemove --window $W 14 9 click 1; sleep .5; xdotool key Down Down Down Down Return
    DW=$(wait_window 20 --name '^Save As$'); [ -n "$DW" ] && sleep .5
}

# 0. no status bar until View > Status Bar (Alt+V, S), which luxpad.ini keeps for the next runs
printf 'x\n' > $T/files/first.txt
run '^first.txt - luxpad$' $T/files/first.txt
if [ -z "$W" ]; then fail "no luxpad window"; cat $T/err.log
else
    sleep .5
    check "no status bar at first" "" "$(status encoding)"
    # Alt+V from the Edit control drops View at once (a menu window shows), S chooses Status Bar
    n0=$(xdotool search --onlyvisible --name '' 2>/dev/null | wc -l)
    xdotool key alt+v
    dropped() { [ "$(xdotool search --onlyvisible --name '' 2>/dev/null | wc -l)" -gt $n0 ]; }
    wait_for 5 dropped
    check "Alt+V drops View" "yes" "$(dropped && echo yes)"
    xdotool key s
    wait_for 5 is_status encoding "UTF-8"
    check "View > Status Bar shows it" "UTF-8" "$(status encoding)"
    quit
    check "luxpad.ini keeps it" "statusbar = 1" "$(grep '^statusbar' $T/home/.config/lhde/luxpad.ini)"
fi

# 1. a UTF-8 file with CR LF and 𝄞 (U+1D11E), an emoji typed, Ctrl+S
F=$T/files/crlf.txt
printf 'alpha \360\235\204\236 beta\r\nsecond line \303\274\r\nthird\r\n' > $F
run '^crlf.txt - luxpad$' $F
if [ -z "$W" ]; then fail "no luxpad window"; cat $T/err.log
else
    check "opened as UTF-8" "UTF-8" "$(status encoding)"
    check "line ends Windows (CR + LF)" "Windows (CR + LF)" "$(status eoln)"
    xdotool key ctrl+Home End; xdotool type --delay 80 ' 😀'; sleep .3; xdotool key ctrl+s
    want=$(printf 'alpha \360\235\204\236 beta \360\237\230\200\r\nsecond line \303\274\r\nthird\r\n' | python3 -I -c 'import sys; print(sys.stdin.buffer.read().hex())')
    wait_for 10 is_hex $F "$want"
    check "Ctrl+S: the emoji's 4 bytes where typed, the rest (CR LF, U+1D11E, ü) as it was" "$want" "$(hex $F)"
    # 2. Ln, Col
    xdotool key ctrl+Home Down Right Right; wait_for 5 is_status pos "Ln 2, Col 3"
    check "Ln/Col after Down, Right, Right" "Ln 2, Col 3" "$(status pos)"
    # 3. Find
    xdotool key ctrl+f; FW=$(wait_window 20 --name '^Find$')
    if [ -z "$FW" ]; then fail "no Find dialog"
    else
        sleep .3; xdotool type --delay 30 third; xdotool key Return; wait_for 5 is_status pos "Ln 3, Col 6"
        check "Find Next selects 'third'" "Ln 3, Col 6" "$(status pos)"
        xdotool key Escape; sleep .3
    fi
    # 4. Go To
    timeout 10 xdotool windowfocus --sync $W 2>/dev/null; xdotool key ctrl+g; GW=$(wait_window 20 --name '^Go To Line$')
    if [ -z "$GW" ]; then fail "no Go To dialog"
    else
        sleep .3; xdotool type 2; xdotool key Return; wait_for 5 is_status pos "Ln 2, Col 1"
        check "Go To line 2" "Ln 2, Col 1" "$(status pos)"
    fi
    # 5. Save As Unicode (UTF-16 LE): the Encoding box under Save as type
    U=$T/files/u16.txt
    timeout 10 xdotool windowfocus --sync $W 2>/dev/null; save_as
    if [ -z "$DW" ]; then fail "no Save As dialog"
    else
        xdotool type --delay 20 "$U"; xdotool key Tab Tab Home Down; sleep .3; xdotool key Return
        want16=$(printf 'alpha \360\235\204\236 beta \360\237\230\200\r\nsecond line \303\274\r\nthird\r\n' | python3 -I -c 'import sys; print((b"\xff\xfe" + sys.stdin.buffer.read().decode().encode("utf-16-le")).hex())')
        wait_for 10 is_hex $U "$want16"
        check "Save As Unicode: BOM + UTF-16 LE, same text, CR LF" "$want16" "$(hex $U)"
        check "the title follows" "u16.txt - luxpad" "$(xdotool getwindowname $W 2>/dev/null)"
        check "status bar: Unicode" "Unicode" "$(status encoding)"
    fi
    quit
fi

# 6. the UTF-16 file reopened, saved back unchanged
run '^u16.txt - luxpad$' $T/files/u16.txt
if [ -z "$W" ]; then fail "no window for u16.txt"
else
    check "reopened as Unicode" "Unicode" "$(status encoding)"
    before=$(hex $T/files/u16.txt); touch -d '2001-01-01' $T/files/u16.txt
    xdotool key ctrl+s; wait_for 10 sh -c "[ \$(stat -c %Y $T/files/u16.txt 2>/dev/null || stat -f %m $T/files/u16.txt) -gt 978307200 ]"
    check "UTF-16 saved back byte for byte" "$before" "$(hex $T/files/u16.txt)"
    quit
fi

# 7. not UTF-8 (Latin-1 bytes) with LF: ANSI, saved back byte for byte, LF kept; a change and Alt+F4: No leaves the file
L=$T/files/latin1.txt
printf 'caf\351 cr\350me\nna\357ve\n' > $L
run '^latin1.txt - luxpad$' $L
if [ -z "$W" ]; then fail "no window for latin1.txt"
else
    check "Latin-1 bytes open as ANSI" "ANSI (UTF-8)" "$(status encoding)"
    check "line ends Unix (LF)" "Unix (LF)" "$(status eoln)"
    before=$(hex $L); touch -d '2001-01-01' $L
    xdotool key ctrl+s; wait_for 10 sh -c "[ \$(stat -c %Y $L 2>/dev/null || stat -f %m $L) -gt 978307200 ]"
    check "ANSI saved back byte for byte (LF kept)" "$before" "$(hex $L)"
    xdotool key ctrl+End; xdotool type x; sleep .3
    timeout 10 xdotool windowfocus --sync $W 2>/dev/null; xdotool key alt+F4
    MW=$(wait_window 20 --name '^luxpad$')
    if [ -z "$MW" ]; then fail "no unsaved-changes question on close"
    else
        ok "closing with a change asks"
        timeout 10 xdotool windowfocus --sync $MW 2>/dev/null; xdotool key n
    fi
    for i in $(seq 50); do kill -0 $P 2>/dev/null || break; sleep .2; done
    if kill -0 $P 2>/dev/null; then kill $P; fail "luxpad did not exit after No"; else ok "No: luxpad exits"; fi
    wait $P 2>/dev/null
    check "No leaves the file" "$before" "$(hex $L)"
fi

# 8. the window where luxpad.ini's state file puts it, and a file dragged from flxp onto it
printf '[window]\nx = 660\ny = 20\nwidth = 520\nheight = 400\nmaximized = 0\n' > $T/home/.state/lhde/luxpad.ini
printf 'dropped text\n' > $T/drop/dropped.txt
run '^Untitled - luxpad$' ""
if [ -z "$W" ]; then fail "no Untitled window"
else
    check "Untitled - luxpad, placed as remembered" "660,20" "$(xdotool getwindowgeometry $W | sed -n 's/.*Position: \([0-9]*,[0-9]*\).*/\1/p')"
    if [ -x $TOP/build/bin/flxp ]; then
        $TOP/build/bin/flxp --geometry 600x500+0+0 $T/drop 2>$T/flxp.log & FP=$!
        if [ -n "$(wait_window 30 --name '^drop$')" ]; then
            sleep 1
            xdotool mousemove 265 104; sleep .2; xdotool mousedown 1; sleep .1; xdotool mousemove 273 107; sleep .1
            for f in 1 2 3 4 5 6 7 8; do xdotool mousemove $((265 + (900 - 265) * f / 8)) $((104 + (220 - 104) * f / 8)); sleep .08; done
            sleep .3; xdotool mouseup 1
            wait_for 10 sh -c "[ \"\$(xdotool getwindowname $W 2>/dev/null)\" = 'dropped.txt - luxpad' ]"
            check "a file dropped from flxp opens" "dropped.txt - luxpad" "$(xdotool getwindowname $W 2>/dev/null)"
        else fail "flxp did not show"; fi
        kill $FP 2>/dev/null; wait $FP 2>/dev/null
    else echo "skip the drop: no build/bin/flxp"; fi
    quit
fi

# 9. nothing imported, an empty HOME, no XDG variables
printf 'bare \342\234\223\n' > $T/bare/bare.txt
env -i PATH="$PATH" DISPLAY=$D HOME=$T/bare LHDE_WINDOWS=/nonexistent LUXPAD_TRACE=1 $B $T/bare/bare.txt > $T/out.log 2>$T/err.log & P=$!
W=$(wait_window 30 --name '^bare.txt - luxpad$')
if [ -z "$W" ]; then fail "no window with nothing imported"; cat $T/err.log
else
    ok "nothing imported: opens"
    quit
    [ -s $T/bare/.config/lhde/luxpad.ini ] && ok "luxpad.ini written in the empty HOME" || fail "no luxpad.ini in the empty HOME"
    check "the empty HOME's luxpad.ini: no status bar" "statusbar = 0" "$(grep '^statusbar' $T/bare/.config/lhde/luxpad.ini)"
fi

stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
