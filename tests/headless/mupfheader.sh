#!/bin/sh
# mupf's Longhorn header against the Explorer's (shell/lhheader.h): under each Longhorn theme (default slate4051, jade4074,
# aero4074 and aerovision; MUPFHEADER_THEMES to choose) flxp on a folder and mupf on a document at the same place and size under
# luxwm (and picom, when installed: the header on glass), the header's band of each client window cut from the screen and
# compared. What may differ is the breadcrumb after its first segment: both windows show the same title ("mupf test":
# the folder's name, the PDF's /Title) and the same first segment ("My Computer": the Explorer's root of a folder outside the
# home, mupf's document named so), so the height, the glass or the opaque band, the travel buttons, the title's and the
# first segment's font, place and glow, the right part, are pixel-identical or the test fails; the search box and the star,
# which mupf hides, are switched off in the Explorer (flxp.ini header_search, header_favorites). The differing pixels must
# lie in one strip on the breadcrumb's row (at most 28 rows, below the band's top third, from x = 120 on: past the travel
# buttons and the first segment's start). Then mupf alone under slate4051 (MUPFHEADER_BEHAVIOUR=THEME, none): the crumbs and
# the Contents pane follow the page, a drop-down, Back and a segment go where they say. MUPFHEADER_KEEP=1 keeps the screenshots (the path
# is printed). Needs mupf (MuPDF), Xvfb, xdotool, xwininfo, python3 and ImageMagick, and an imported Longhorn style;
# prints "skip: ..." and passes without them. Usage: tests/headless/mupfheader.sh [:DISPLAY]
D=${1:-:78}
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/bin/mupf
[ -x "$B" ] || { echo "skip: mupf not built (no MuPDF: docs/BUILDING.md)"; exit 0; }
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool xwininfo xprop python3 import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
[ -n "$styles" ] || { echo "skip: no imported styles (lhde-import)"; exit 0; }
T=$(mktemp -d); [ -n "$MUPFHEADER_KEEP" ] && echo "kept: $T"
trap '[ -n "$MUPFHEADER_KEEP" ] || rm -rf "$T"' EXIT
mkdir -p "$T/home/.config/lhde" "$T/docs" "$T/mupf test/Sub"
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D MUPF_TRACE=1 FLXP_WM_LOG=1 LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }

python3 -I "$R/tests/headless/mkpdf.py" "$T/docs/My Computer" || { echo "FAIL mkpdf.py could not write the PDF"; exit 1; }
printf '[flxp]\nheader_search = 0\nheader_favorites = 0\n' > "$T/home/.config/lhde/flxp.ini"

start_xvfb $D -screen 0 1280x900x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > "$T/luxwm.log" 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
PP=
if command -v picom >/dev/null 2>&1; then
    picom --config "$R/session/picom-glass.conf" --backend xrender --blur-method gaussian --blur-size 5 --blur-deviation 1.2 \
        > "$T/picom.log" 2>&1 & PP=$!
    sleep 1
    kill -0 $PP 2>/dev/null && echo "note: picom runs: the headers on glass" || { PP=; echo "note: picom did not start: opaque headers"; }
else
    echo "note: no picom: opaque headers"
fi

# shoot NAME CLASS PROGRAM ARGS...: the program's window at (60, 60), 900 x 560; its client's band cut to $T/NAME.png, its header
# height (the program's log line) in $H
shoot() {
    name=$1 cls=$2; shift 2
    "$@" > "$T/$name.log" 2>&1 & P=$!
    W=$(wait_window 15 --classname "^$cls$")
    [ -n "$W" ] || { bad "$name: no window"; kill $P 2>/dev/null; return 1; }
    xdotool windowmove --sync $W 60 60 2>/dev/null; xdotool windowsize --sync $W 900 560 2>/dev/null
    xdotool mousemove 1200 880
    sleep 2.5
    H=$(sed -n 's/.*header \([0-9]*\) rows.*/\1/p' "$T/$name.log" | tail -1)
    geo=$(xwininfo -id $W)
    X=$(echo "$geo" | sed -n 's/.*Absolute upper-left X: *//p'); Y=$(echo "$geo" | sed -n 's/.*Absolute upper-left Y: *//p')
    WW=$(echo "$geo" | sed -n 's/.*Width: *//p')
    import -window root "$T/$name.full.png"
    [ -n "$H" ] && [ "$H" -gt 0 ] && $IM "$T/$name.full.png" -crop "${WW}x$H+$X+$Y" +repage "$T/$name.png"
    echo "$name: client ${WW} wide at ($X, $Y), header $H rows" >> "$T/geometry"
    kill $P 2>/dev/null; wait $P 2>/dev/null
    sleep .5
}

for theme in ${MUPFHEADER_THEMES:-slate4051 jade4074 aero4074 aerovision}; do
    name=$("$R/build/bin/lhde-theme" resolve "$theme" 2>/dev/null | sed -n 's/^theme=//p' | head -1)
    style=$("$R/build/bin/lhde-theme" resolve "$theme" 2>/dev/null | sed -n 's/^path.style=//p' | head -1)
    if [ -z "$name" ] || { [ -n "$style" ] && [ ! -f "$style" ]; }; then echo "skip $theme: not available here"; continue; fi
    printf '[theme]\nname = %s\n' "$theme" > "$T/home/.config/lhde/settings.ini"
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$theme"
    sleep 1
    : > "$T/geometry"
    shoot "flxp-$theme" flxp "$R/build/bin/flxp" "$T/mupf test" || continue
    HF=$H WF=$WW
    shoot "mupf-$theme" mupf "$B" "$T/docs/My Computer" || continue
    HM=$H WM=$WW
    cat "$T/geometry"
    if [ -z "$HF" ] || [ -z "$HM" ]; then bad "$theme: a header height missing (flxp [$HF], mupf [$HM])"; continue; fi
    [ "$HF" = "$HM" ] && ok "$theme: the header is $HF rows in both" || { bad "$theme: header rows flxp $HF, mupf $HM"; continue; }
    [ "$WF" = "$WM" ] || { bad "$theme: client widths flxp $WF, mupf $WM"; continue; }
    grep -q "^crumbs My Computer > " "$T/mupf-$theme.log" || bad "$theme: mupf's crumbs: $(grep '^crumbs' "$T/mupf-$theme.log" | tail -1)"
    # the differing pixels: none, or one strip on the breadcrumb's row right of "My Computer"
    n=$($IM compare -metric AE "$T/flxp-$theme.png" "$T/mupf-$theme.png" null: 2>&1 | sed 's/ .*//')
    box=$($IM "$T/flxp-$theme.png" "$T/mupf-$theme.png" -compose difference -composite -threshold 0 -format '%@' info: 2>/dev/null)
    echo "$theme: $n pixels differ, in $box"
    bw=${box%%x*}; r=${box#*x}; bh=${r%%+*}; r=${r#*+}; bx=${r%%+*}; by=${r#*+}
    if [ "$n" = 0 ]; then ok "$theme: the bands are identical"
    elif [ "$bh" -le 28 ] && [ "$bx" -ge 120 ] && [ "$by" -ge $((HF / 3)) ]; then
        ok "$theme: identical but for the breadcrumb after its first segment ($n pixels in ${bw}x$bh at ($bx, $by))"
    else
        bad "$theme: the bands differ outside the breadcrumb: $n pixels in $box"
    fi
done

# mupf's header at work under slate4051 (LHHEADER_TRACE=1: the segments' places): the crumbs follow the page and the
# Contents pane its entry; a segment's drop-down goes to the entry chosen; a click on the first segment goes to page 1; the
# header's Back button is mupf's Back
theme=${MUPFHEADER_BEHAVIOUR:-slate4051}
if [ "$theme" != none ] && "$R/build/bin/lhde-theme" resolve "$theme" > /dev/null 2>&1; then
    printf '[theme]\nname = %s\n' "$theme" > "$T/home/.config/lhde/settings.ini"
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$theme"
    sleep 1
    L=$T/work.log
    LHHEADER_TRACE=1 "$B" "$T/docs/My Computer" > $L 2>&1 & P=$!
    W=$(wait_window 15 --classname '^mupf$')
    traced() { grep -q "$1" $L; }
    crumb() { grep "^lhheader crumb $1 " $L | tail -1 | cut -d' ' -f"$2"; }   # crumb I FIELD: 4..7 the box, 9 10 the chevron
    if [ -n "$W" ]; then
        timeout 10 xdotool windowactivate --sync $W 2>/dev/null
        geo=$(xwininfo -id $W)
        X=$(echo "$geo" | sed -n 's/.*Absolute upper-left X: *//p'); Y=$(echo "$geo" | sed -n 's/.*Absolute upper-left Y: *//p')
        wait_for 10 traced "^crumbs My Computer > Chapter One > Page 1 of 2"
        traced "^crumbs My Computer > Chapter One > Page 1 of 2" && ok "the crumbs: the document, the outline's entry, the page" ||
            bad "crumbs on page 1: $(grep '^crumbs' $L | tail -1)"
        xdotool key Next      # (the view has the focus; the pane follows shown or not)
        wait_for 10 traced "^crumbs My Computer > Chapter Two > Page 2 of 2"
        traced "^crumbs My Computer > Chapter Two > Page 2 of 2" && ok "Page Down: the crumbs follow" || bad "crumbs on page 2: $(grep '^crumbs' $L | tail -1)"
        traced "^toc follows 1 Chapter Two" && ok "the Contents pane follows the page" || bad "the pane did not select Chapter Two"
        # Chapter Two's chevron: its siblings, Chapter One first
        cx=$(crumb 1 9) cy=$(crumb 1 10)
        if [ -n "$cx" ]; then
            xdotool mousemove $((X + cx)) $((Y + cy)) click 1; sleep .7
            xdotool key Down Return
            wait_for 10 traced "^header go toc:0"
            traced "^header go toc:0" && ok "the drop-down's Chapter One goes there" || bad "the drop-down chose nothing"
            wait_for 10 traced "^toc follows 0 Chapter One"
            traced "^toc follows 0 Chapter One" && ok "the pane follows to Chapter One" || bad "the pane did not follow back"
        else bad "no place for crumb 1 in the trace"; fi
        # Back (the header's orb, the 4051 layout's: its circle's middle) goes to page 2 again
        n0=$(grep -c "^crumbs " $L)
        xdotool mousemove $((X + 23)) $((Y + 27)) click 1; sleep .7
        grep "^crumbs " $L | tail -n +$((n0 + 1)) | grep -q "Page 2 of 2" && ok "the header's Back: page 2" ||
            bad "the header's Back did nothing ($(grep '^crumbs' $L | tail -1))"
        # the first segment: page 1
        fx=$(crumb 0 4) fy=$(crumb 0 5)
        xdotool mousemove $((X + fx + 10)) $((Y + fy + 6)) click 1; sleep .7
        traced "^header go doc" && ok "a click on the document's segment: page 1" || bad "the first segment did nothing"
        timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key alt+F4
        for i in $(seq 100); do kill -0 $P 2>/dev/null || break; sleep .1; done
    else bad "$theme: mupf has no window"; fi
    kill $P 2>/dev/null; wait $P 2>/dev/null
fi

[ -n "$PP" ] && { kill $PP 2>/dev/null; wait $PP 2>/dev/null; }
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
