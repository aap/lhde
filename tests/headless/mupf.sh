#!/bin/sh
# mupf under luxwm on Xvfb, driven with xdotool, what it shows read from MUPF_TRACE=1's lines (mapped, opened, rendered,
# shown PAGE:X,Y,WxH, status MESSAGE|PAGE|ZOOM, toc, tocitem INDEX X Y TITLE, match, exit). A PDF written by
# tests/headless/mkpdf.py (two pages, an outline of two chapters, a link, a black box): the first page is rendered with
# ink where the box is and white beside it; Page Down shows page 2; Home page 1; a click on the link goes to page 2 and
# Alt+Left back; the search for "lantern" finds its 4 hits (Match 1 of 4), F3 goes to the second; F4 shows the Contents
# pane with both chapters and a click on "Chapter One" goes to page 1; the pane's choice is in mupf.ini after Alt+F4, and
# the window comes back with the pane shown; a file that is not a document gives a message box, not a crash; a protected
# copy (mutool's encryption, when mutool is installed) asks for its password, again after a wrong one, and opens. A HOME and
# XDG directories of its own. Needs mupf (built when MuPDF is installed), Xvfb, xdotool, python3 and ImageMagick; prints
# "skip: ..." and passes without them. Usage: tests/headless/mupf.sh [:DISPLAY]
D=${1:-:79}
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/bin/mupf
[ -x "$B" ] || { echo "skip: mupf not built (no MuPDF: docs/BUILDING.md)"; exit 0; }
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool python3 import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
T=$(mktemp -d); [ -n "$MUPF_KEEP" ] && echo "kept: $T"
trap '[ -n "$MUPF_KEEP" ] || rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde $T/docs
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D MUPF_TRACE=1
[ -n "$styles" ] && export LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1: expected [$2] got [$3]"; fi; }
px() { $IM "$1" -format '%[fx:int(255*p{'"$2"','"$3"'}.r)],%[fx:int(255*p{'"$2"','"$3"'}.g)],%[fx:int(255*p{'"$2"','"$3"'}.b)]' info:; }
expect() {   # expect NAME FILE X Y R,G,B
    got=$(px "$2" "$3" "$4")
    if [ "$got" = "$5" ]; then ok "$1: ($3,$4) $got"; else bad "$1: ($3,$4) expected $5 got $got"; fi
}

python3 -I "$R/tests/headless/mkpdf.py" $T/docs/test.pdf || { echo "FAIL mkpdf.py could not write the PDF"; exit 1; }
printf 'not a document\0\1\2\3' > $T/docs/broken.pdf

start_xvfb $D -screen 0 1280x900x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
theme=classic; [ -n "$styles" ] && [ -f "$styles/luna.msstyles" ] && theme=luna
printf '[theme]\nname = %s\n' $theme > $T/home/.config/lhde/settings.ini
xprop -root -f _LH_THEME 8u -set _LH_THEME $theme
sleep .3

traced() { grep -q "$1" $T/out.log; }                         # a trace line matching
last() { grep "^$1 " $T/out.log | tail -1 | sed "s/^$1 //"; }  # the last trace line of a kind
page_is() { last status | cut -d'|' -f2 | grep -qx "$1"; }
message_is() { last status | cut -d'|' -f1 | grep -qx "$1"; }
run() {   # run ARGS...: mupf, its window in $W
    : > $T/out.log
    "$B" "$@" >> $T/out.log 2>$T/err.log & P=$!
    W=$(wait_window 10 --classname '^mupf$')
    [ -n "$W" ] && timeout 10 xdotool windowactivate --sync $W 2>/dev/null
}
quit() {
    timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key alt+F4
    for i in $(seq 100); do kill -0 $P 2>/dev/null || break; sleep .1; done
    if kill -0 $P 2>/dev/null; then kill $P; bad "mupf did not exit"; fi
    wait $P 2>/dev/null; RC=$?
}
key() { timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key "$@"; }
# the box of page N as last shown, in root coordinates: "X Y W H"
page_box() { last shown | tr ' ' '\n' | grep "^$1:" | tail -1 | sed 's/^[0-9]*://; s/[,x]/ /g'; }

# 1. the document: the first page rendered, ink where the black box is
run $T/docs/test.pdf
if [ -z "$W" ]; then bad "no mupf window"; cat $T/err.log
else
    wait_for 10 traced "^opened 2 pages"
    check "opened: two pages" "2 pages PDF 1.4" "$(last opened)"
    check "the title names the file" "test.pdf - mupf" "$(last title)"
    wait_for 10 traced "^shown 1:"
    set -- $(page_box 1)
    if [ $# = 4 ]; then
        ok "page 1 rendered at $3x$4"
        sleep .3
        import -window root $T/shot1.png
        # the box: 200 pt square in the middle of the Letter page; white a tenth in from the top left
        expect "ink in the middle of page 1" $T/shot1.png $(($1 + $3 / 2)) $(($2 + $4 / 2)) 0,0,0
        expect "white paper near its corner" $T/shot1.png $(($1 + $3 / 10)) $(($2 + $4 / 10)) 255,255,255
        LX=$(($1 + $3 * 100 / 612)); LY=$(($2 + $4 * 166 / 792))   # the link: "Go to page two"
    else bad "page 1 never shown"; fi
    check "the status bar: page 1 of 2" "Page 1 of 2" "$(last status | cut -d'|' -f2)"
    # 2. pages
    key Next; wait_for 10 page_is "Page 2 of 2"; check "Page Down: page 2" "Page 2 of 2" "$(last status | cut -d'|' -f2)"
    wait_for 10 traced "^shown.* 2:"
    traced "^rendered 2 " && ok "page 2 rendered" || bad "page 2 not rendered"
    key Home; wait_for 10 page_is "Page 1 of 2"; check "Home: page 1" "Page 1 of 2" "$(last status | cut -d'|' -f2)"
    if [ -n "$LX" ]; then
        sleep .3
        xdotool mousemove $LX $LY click 1
        wait_for 10 page_is "Page 2 of 2"; check "a click on the link: page 2" "Page 2 of 2" "$(last status | cut -d'|' -f2)"
        key alt+Left; wait_for 10 page_is "Page 1 of 2"; check "Alt+Left: back to page 1" "Page 1 of 2" "$(last status | cut -d'|' -f2)"
    fi
    # 3. the search
    key ctrl+f; xdotool type --delay 20 lantern; xdotool key Return
    wait_for 10 message_is "Match 1 of 4"
    check "the search: 4 hits, the first shown" "Match 1 of 4" "$(last status | cut -d'|' -f1)"
    key F3; wait_for 10 message_is "Match 2 of 4"
    check "F3: the second" "Match 2 of 4" "$(last status | cut -d'|' -f1)"
    check "on page 2" "Page 2 of 2" "$(last status | cut -d'|' -f2)"
    # 4. the Contents pane
    key F4
    wait_for 10 traced "^tocitem 0 "
    check "F4: the pane shown" "shown" "$(last contents)"
    check "the outline's entries" "Chapter One|Chapter Two" "$(grep '^tocitem' $T/out.log | tail -2 | cut -d' ' -f5- | paste -sd'|')"
    set -- $(grep '^tocitem 0 ' $T/out.log | tail -1)
    if [ -n "$3" ]; then
        xdotool mousemove $3 $4 click 1
        wait_for 10 page_is "Page 1 of 2"
        check "a click on Chapter One: page 1" "Page 1 of 2" "$(last status | cut -d'|' -f2)"
    fi
    import -window root $T/shot2.png
    [ -n "$MUPF_SHOT" ] && cp $T/shot2.png "$MUPF_SHOT"
    quit
    check "Alt+F4: mupf exits" 0 $RC
    traced "^exit" && ok "the trace's exit line" || bad "no exit line"
    grep -q '^contents *= *1' $XDG_STATE_HOME/lhde/mupf.ini && ok "mupf.ini remembers the pane" || bad "mupf.ini: $(cat $XDG_STATE_HOME/lhde/mupf.ini 2>&1)"
    # 5. again: the pane comes back
    run $T/docs/test.pdf
    wait_for 10 traced "^tocitem 0 "
    traced "^tocitem 0 " && ok "the next window shows the pane" || bad "the pane did not come back"
    quit
fi

# 6. not a document: a message box, mupf alive
run $T/docs/broken.pdf
if [ -n "$W" ]; then
    wait_for 10 traced "^failed "
    traced "^failed " && ok "a broken file: failed ($(last failed | cut -c1-60))" || bad "a broken file: no failure"
    MB=$(wait_window 10 --name '^mupf$')
    [ -n "$MB" ] && ok "a message box says so" || bad "no message box"
    [ -n "$MB" ] && { timeout 10 xdotool windowactivate --sync $MB 2>/dev/null; xdotool key Return; }
    kill -0 $P 2>/dev/null && ok "mupf still running" || bad "mupf ended"
    quit
fi

# 7. a protected document (mutool's encryption, when mutool is there): the Password dialog, a wrong one asks again, the
# right one opens it
if command -v mutool >/dev/null 2>&1 && mutool clean -E aes-128 -U lantern -O owner $T/docs/test.pdf $T/docs/locked.pdf 2>/dev/null; then
    run $T/docs/locked.pdf
    if [ -n "$W" ]; then
        wait_for 10 traced "^password asked"
        traced "^password asked" && ok "a protected document asks for its password" || bad "no Password dialog"
        PD=$(wait_window 10 --name '^Password$')
        if [ -n "$PD" ]; then
            timeout 10 xdotool windowactivate --sync $PD 2>/dev/null; xdotool type x; xdotool key Return
            wait_for 10 traced "^password asked again"
            traced "^password asked again" && ok "a wrong password asks again" || bad "a wrong password did not ask again"
            PD=$(wait_window 10 --name '^Password$')
            timeout 10 xdotool windowactivate --sync $PD 2>/dev/null; xdotool type lantern; xdotool key Return
            wait_for 10 traced "^opened 2 pages"
            traced "^opened 2 pages" && ok "the right password opens it" || bad "the password did not open it"
        else bad "no Password window"; fi
        quit
    fi
else
    echo "skip the protected document: no mutool"
fi

kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
