#!/bin/sh
# mupf on a Markdown file, under luxwm on Xvfb, driven with xdotool, what it shows read from MUPF_TRACE=1's lines (opened,
# title, shown PAGE:X,Y,WxH, tocentry INDEX LEVEL PAGE Y TITLE, tocitem, linkrect PAGE X0 Y0 X1 Y1 URI). A README.md with
# front matter, an H1, a paragraph with bold and a link to other.md, a table, a fenced code block, a task list and a
# picture beside it (a magenta PNG): the title is the H1's; at Fit Page the H1's band of the page has ink, the table's band
# its borders' grey, the picture's band magenta; the Contents pane (F4) lists the headings; a click on the link opens
# other.md in the same window and Alt+Left comes back; a heading appended to the file is laid out without a key pressed.
# Pixel bands come from the headings' places (tocentry: points down the page; the page 660 pt wide). Needs mupf (built when
# MuPDF is installed), Xvfb, xdotool, python3 and ImageMagick; prints "skip: ..." and passes without them.
# Usage: tests/headless/mupfmd.sh [:DISPLAY]
D=${1:-:78}
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

$IM -size 160x80 xc:'#ff00ff' $T/docs/pic.png
cat > $T/docs/README.md <<'EOF'
---
title: front matter, not shown
---
# Markdown Test

Some **bold** text and a [link to the other file](other.md) in a paragraph.

## Table

| Name | Value |
| --- | --- |
| alpha | 1 |
| beta | 2 |

## Code

```c
int main(void) { return 0; }
```

## Tasks

- [ ] an open task
- [x] a done task

## Picture

<img src="pic.png" alt="a magenta box">

## End

The last paragraph.
EOF
printf '# Other File\n\nThe second file.\n' > $T/docs/other.md

start_xvfb $D -screen 0 1280x900x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
theme=classic; [ -n "$styles" ] && [ -f "$styles/luna.msstyles" ] && theme=luna
printf '[theme]\nname = %s\n' $theme > $T/home/.config/lhde/settings.ini
xprop -root -f _LH_THEME 8u -set _LH_THEME $theme
sleep .3

traced() { grep -q "$1" $T/out.log; }
last() { grep "^$1 " $T/out.log | tail -1 | sed "s/^$1 //"; }
title_is() { [ "$(last title)" = "$1" ]; }
key() { timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key "$@"; }
page_box() { last shown | tr ' ' '\n' | grep "^$1:" | tail -1 | sed 's/^[0-9]*://; s/[,x]/ /g'; }
entry_y() { grep "^tocentry [0-9]* [0-9]* 1 [0-9.e+-]* $1\$" $T/out.log | tail -1 | cut -d' ' -f5; }
# count FILE X0 Y0 X1 Y1 R G B TOL: the pixels of the box within TOL of the colour in each channel
count() {
    $IM "$1" -crop "$(($4 - $2))x$(($5 - $3))+$2+$3" +repage -depth 8 rgb:- | python3 -I -c '
import sys
d = sys.stdin.buffer.read(); r, g, b, t = (int(a) for a in sys.argv[1:5])
print(sum(1 for i in range(0, len(d) - 2, 3) if abs(d[i] - r) <= t and abs(d[i + 1] - g) <= t and abs(d[i + 2] - b) <= t))' $6 $7 $8 $9
}
# band NAME FROM TO R G B TOL MIN: the page's band from heading FROM to heading TO (their places, points) holds at least MIN
# such pixels
band() {
    y0=$(entry_y "$2"); y1=$(entry_y "$3")
    if [ -z "$y0" ] || [ -z "$y1" ] || [ -z "$PX" ]; then bad "$1: no place for $2 or $3"; return; fi
    a=$(python3 -I -c "print(int($PY + $y0 * $PW_ / 660))"); b=$(python3 -I -c "print(int($PY + $y1 * $PW_ / 660))")
    n=$(count $T/shot.png $PX $a $((PX + PW_)) $b $4 $5 $6 $7)
    if [ "$n" -ge "$8" ]; then ok "$1: $n pixels in rows $a..$b"; else bad "$1: $n pixels in rows $a..$b, wanted $8"; fi
}

: > $T/out.log
"$B" $T/docs/README.md >> $T/out.log 2>$T/err.log & P=$!
W=$(wait_window 10 --classname '^mupf$')
if [ -z "$W" ]; then bad "no mupf window"; cat $T/err.log
else
    timeout 10 xdotool windowactivate --sync $W 2>/dev/null
    wait_for 10 traced "^opened 1 pages Markdown"
    check "opened: one page of Markdown" "1 pages Markdown" "$(last opened)"
    wait_for 5 traced "^title Markdown Test - mupf"
    check "the title is the H1" "Markdown Test - mupf" "$(last title)"
    traced "^tocentry 0 0 1 [0-9.e+-]* Markdown Test$" && ok "the outline starts with the H1" || bad "no H1 in the outline"
    traced "front matter" && bad "the front matter is in the outline" || ok "no front matter in the outline"
    traced "^watch " && ok "the file's directory is followed" || bad "no watch"
    # the whole page in view (Fit Page): bands of it between the headings' places
    key ctrl+0
    wait_for 10 traced "^zoom"
    sleep .5
    wait_for 10 traced "^shown 1:"
    sleep .5
    set -- $(page_box 1)
    if [ $# = 4 ]; then
        PX=$1 PY=$2 PW_=$3 PH=$4
        ok "the page shown at $3x$4"
        import -window root $T/shot.png
        band "the H1's ink" "Markdown Test" "Table" 31 35 40 60 40
        band "the table's borders" "Table" "Code" 209 217 224 14 40
        band "the code block's grey" "Code" "Tasks" 246 248 250 2 400
        band "the picture" "Picture" "End" 255 0 255 8 1000
        n=$(count $T/shot.png $PX $(python3 -I -c "print(int($PY + $(entry_y Table) * $PW_ / 660))") $((PX + PW_)) $(python3 -I -c "print(int($PY + $(entry_y Picture) * $PW_ / 660))") 255 0 255 8)
        check "no magenta between the table and the picture" 0 "$n"
    else bad "the page never shown"; fi
    # the Contents pane: the headings
    key F4
    wait_for 10 traced "^tocitem 5 "
    for h in "Markdown Test" Table Code Tasks Picture End; do
        traced "^tocitem [0-9]* [0-9]* [0-9]* $h\$" && ok "the Contents pane lists $h" || bad "the Contents pane lacks $h"
    done
    key F4
    sleep .5
    # the link to other.md: opened in this window, Alt+Left back
    set -- $(grep '^linkrect 1 .* other.md$' $T/out.log | tail -1)
    set -- $(page_box 1) $3 $4 $5 $6
    if [ $# = 8 ]; then
        LX=$(python3 -I -c "print(int($1 + ($5 + $7) / 2 * $3 / 660))"); LY=$(python3 -I -c "print(int($2 + ($6 + $8) / 2 * $3 / 660))")
        xdotool mousemove $LX $LY click 1
        wait_for 10 traced "^title Other File - mupf"
        check "the link opens other.md here" "Other File - mupf" "$(last title)"
        key alt+Left
        wait_for 10 title_is "Markdown Test - mupf"
        check "Alt+Left: back to the README" "Markdown Test - mupf" "$(last title)"
    else bad "no link to other.md traced"; fi
    # a heading appended: laid out again on its own
    wait_for 10 traced "^restored"
    printf '\n## Appended Later\n\nWritten while mupf shows the file.\n' >> $T/docs/README.md
    wait_for 10 traced "^tocentry [0-9]* 1 1 [0-9.e+-]* Appended Later$"
    traced "^tocentry [0-9]* 1 1 [0-9.e+-]* Appended Later$" && ok "a heading written to the file appears" || bad "the file's change not shown"
    check "the title stays" "Markdown Test - mupf" "$(last title)"
    timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key alt+F4
    for i in $(seq 100); do kill -0 $P 2>/dev/null || break; sleep .1; done
    if kill -0 $P 2>/dev/null; then kill $P; bad "mupf did not exit"; fi
    wait $P 2>/dev/null; check "Alt+F4: mupf exits" 0 $?
fi

kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
