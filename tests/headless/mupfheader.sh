#!/bin/sh
# mupf's Longhorn header against the Explorer's (shell/lhheader.h): under each Longhorn theme (default slate4051, jade4074,
# aero4074 and aerovision; MUPFHEADER_THEMES to choose) flxp on a folder and mupf on a document at the same place and size under
# luxwm (and picom, when installed: the header on glass), twice: mupf's default, the Small header, against the Explorer's with
# header_size = small, and mupf's View > Large Header (mupf.ini large_header = 1) against the Explorer's own size. The header's
# band of each client window is cut from the screen and compared. What may differ is the breadcrumb after its first segment:
# both windows show the same title ("mupf test": the folder's name, the PDF's /Title) and the same first segment ("My
# Computer": the Explorer's root of a folder outside the home, mupf's document named so), so the height, the glass or the
# opaque band, the travel buttons, the title's and the first segment's font, place and glow, the search box, are
# pixel-identical or the test fails; the star, which mupf hides, is switched off in the Explorer (flxp.ini header_favorites).
# The differing pixels must lie in one strip on the breadcrumb's row (at most 28 rows, below the band's top third, from x =
# 120 on: past the travel buttons and the first segment's start). The row under the header (the menu band and the toolbar
# on one row, the Explorer's 4051 command bar) must be as high in both and its left edge identical. Then mupf alone under
# slate4051 (MUPFHEADER_BEHAVIOUR=THEME, none): the header is flxp's Small height and View > Large Header makes it the large
# one; the header's search box is the Find box (Ctrl+F, Return: "Match 1 of 4", F3 the next); the crumbs and the Contents
# pane follow the page, a drop-down, Back and a segment go where they say; File > Open is the Longhorn dialog (from
# flxp-filedialog: the header is mupf's). MUPFHEADER_KEEP=1 keeps the screenshots (the path is printed). Needs mupf (MuPDF), Xvfb, xdotool, xwininfo, python3 and ImageMagick, and an imported Longhorn style;
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
mkdir -p "$XDG_STATE_HOME/lhde"

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
    RB=$(sed -n 's/^rebar \([0-9]*\) rows.*/\1/p' "$T/$name.log" | tail -1)
    geo=$(xwininfo -id $W)
    X=$(echo "$geo" | sed -n 's/.*Absolute upper-left X: *//p'); Y=$(echo "$geo" | sed -n 's/.*Absolute upper-left Y: *//p')
    WW=$(echo "$geo" | sed -n 's/.*Width: *//p')
    import -window root "$T/$name.full.png"
    [ -n "$H" ] && [ "$H" -gt 0 ] && $IM "$T/$name.full.png" -crop "${WW}x$H+$X+$Y" +repage "$T/$name.png"
    # the row under the header: its left edge (no menu text), one row more than mupf's band (MB rows, set before a flxp shot)
    [ -n "$H" ] && [ -n "$MB" ] && $IM "$T/$name.full.png" -crop "6x$((MB + 1))+$X+$((Y + H))" +repage "$T/$name.row.png"
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
    for size in small large; do
        t="$theme $size"
        if [ $size = small ]; then
            printf '[flxp]\nheader_size = small\nheader_favorites = 0\n' > "$T/home/.config/lhde/flxp.ini"
            printf '[view]\nlarge_header = 0\n' > "$XDG_STATE_HOME/lhde/mupf.ini"
        else
            printf '[flxp]\nheader_favorites = 0\n' > "$T/home/.config/lhde/flxp.ini"
            printf '[view]\nlarge_header = 1\n' > "$XDG_STATE_HOME/lhde/mupf.ini"
        fi
        : > "$T/geometry"
        MB=
        shoot "mupf-$theme-$size" mupf "$B" "$T/docs/My Computer" || continue
        HM=$H WM=$WW MB=$RB
        $IM "$T/mupf-$theme-$size.full.png" -crop "6x$((MB + 1))+$X+$((Y + H))" +repage "$T/mupf-$theme-$size.row.png"
        shoot "flxp-$theme-$size" flxp "$R/build/bin/flxp" "$T/mupf test" || continue
        HF=$H WF=$WW
        eval "H_${theme}_$size=\$HF"
        cat "$T/geometry"
        if [ -z "$HF" ] || [ -z "$HM" ]; then bad "$t: a header height missing (flxp [$HF], mupf [$HM])"; continue; fi
        [ "$HF" = "$HM" ] && ok "$t: the header is $HF rows in both" || { bad "$t: header rows flxp $HF, mupf $HM"; continue; }
        [ "$WF" = "$WM" ] || { bad "$t: client widths flxp $WF, mupf $WM"; continue; }
        grep -q "^crumbs My Computer > " "$T/mupf-$theme-$size.log" || bad "$t: mupf's crumbs: $(grep '^crumbs' "$T/mupf-$theme-$size.log" | tail -1)"
        # the differing pixels: none, or one strip on the breadcrumb's row right of "My Computer"
        n=$($IM compare -metric AE "$T/flxp-$theme-$size.png" "$T/mupf-$theme-$size.png" null: 2>&1 | sed 's/ .*//')
        box=$($IM "$T/flxp-$theme-$size.png" "$T/mupf-$theme-$size.png" -compose difference -composite -threshold 0 -format '%@' info: 2>/dev/null)
        echo "$t: $n pixels differ, in $box"
        bw=${box%%x*}; r=${box#*x}; bh=${r%%+*}; r=${r#*+}; bx=${r%%+*}; by=${r#*+}
        top=$((HF / 3)); [ $size = small ] && top=4   # (the Small band is the breadcrumb's row)
        if [ "$n" = 0 ]; then ok "$t: the bands are identical"
        elif [ "$bh" -le 28 ] && [ "$bx" -ge 120 ] && [ "$by" -ge $top ]; then
            ok "$t: identical but for the breadcrumb after its first segment ($n pixels in ${bw}x$bh at ($bx, $by))"
        else
            bad "$t: the bands differ outside the breadcrumb: $n pixels in $box"
        fi
        # the command bar's row: its MB rows identical in both, and the Explorer's ends there (its next row is another
        # colour than its last: the list or the left pane, not more of the bar)
        if [ -n "$MB" ] && [ -f "$T/flxp-$theme-$size.row.png" ] && [ -f "$T/mupf-$theme-$size.row.png" ]; then
            n=$($IM compare -metric AE "$T/flxp-$theme-$size.row.png[6x$MB+0+0]" "$T/mupf-$theme-$size.row.png[6x$MB+0+0]" null: 2>&1 | sed 's/ .*//')
            last=$($IM "$T/flxp-$theme-$size.row.png" -format "%[pixel:p{3,$((MB - 1))}]" info:)
            next=$($IM "$T/flxp-$theme-$size.row.png" -format "%[pixel:p{3,$MB}]" info:)
            if [ "$n" != 0 ]; then bad "$t: the menu band's row (mupf $MB rows) differs from the Explorer's: $n pixels"
            elif [ "$last" = "$next" ]; then bad "$t: the Explorer's menu band's row goes on past mupf's $MB rows ($last)"
            else ok "$t: the menu band's row is $MB rows in both, its edge identical"; fi
        else bad "$t: no menu band row (mupf's rebar rows [$MB])"; fi
    done
done
rm -f "$XDG_STATE_HOME/lhde/mupf.ini"
printf '[flxp]\nheader_favorites = 0\n' > "$T/home/.config/lhde/flxp.ini"

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
        # the Small header by default; View > Large Header (the menu band: Alt+V, H) the large one, the Explorer's heights
        wait_for 10 traced "^header [0-9]* rows"
        hs=$(sed -n 's/^header \([0-9]*\) rows.*/\1/p' $L | tail -1)
        eval "fs=\$H_${theme}_small fl=\$H_${theme}_large"
        [ -z "$fs" ] || [ "$hs" = "$fs" ] && ok "the Small header by default: $hs rows" || bad "the header is $hs rows, flxp's Small $fs"
        xdotool key alt+v; sleep .5; xdotool key h
        wait_for 10 traced "^large header on"
        sleep 1
        hl=$(sed -n 's/^header \([0-9]*\) rows.*/\1/p' $L | tail -1)
        if ! traced "^large header on"; then bad "View > Large Header did nothing"
        elif [ "$hl" = "$hs" ]; then bad "View > Large Header: the header stayed $hs rows"
        elif [ -n "$fl" ] && [ "$hl" != "$fl" ]; then bad "View > Large Header: $hl rows, flxp's $fl"
        else ok "View > Large Header: $hs -> $hl rows"; fi
        geo=$(xwininfo -id $W)
        X=$(echo "$geo" | sed -n 's/.*Absolute upper-left X: *//p'); Y=$(echo "$geo" | sed -n 's/.*Absolute upper-left Y: *//p')
        # the header's search box is the Find box
        traced "^find box header" && ok "the Find box is the header's" || bad "the Find box: $(grep '^find box' $L | tail -1)"
        xdotool key ctrl+f; sleep .3; xdotool type --delay 20 lantern; xdotool key Return
        wait_for 10 grep -q "^status Match 1 of 4|" $L
        grep -q "^status Match 1 of 4|" $L && ok "the header's box, Return: Match 1 of 4" || bad "the header's Find: $(grep '^status' $L | tail -1)"
        xdotool key F3
        wait_for 10 grep -q "^status Match 2 of 4|" $L
        grep -q "^status Match 2 of 4|" $L && ok "F3: Match 2 of 4" || bad "F3 after the header's Find: $(grep '^status' $L | tail -1)"
        xdotool key Escape; sleep .3; xdotool key Home
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
        # File > Open: the Longhorn dialog, run by flxp-filedialog (the header is mupf's); Escape cancels it
        timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key ctrl+o
        D=$(wait_window 15 --classname '^flxp-filedialog$')
        if [ -n "$D" ]; then
            ps -o args= -p "$(pgrep -n -f 'flxp-filedialog --look longhorn')" 2>/dev/null | grep -q -- '--parent x11:' &&
                ok "File > Open: the Longhorn dialog, from flxp-filedialog (--look longhorn, --parent)" ||
                bad "File > Open: a dialog window, but not flxp-filedialog --look longhorn"
            timeout 10 xdotool windowactivate --sync $D 2>/dev/null; xdotool key Escape
            for i in $(seq 50); do pgrep -f 'flxp-filedialog --look longhorn' > /dev/null || break; sleep .1; done
            pgrep -f 'flxp-filedialog --look longhorn' > /dev/null && bad "Escape did not close the dialog" || ok "Escape: the dialog gone"
        else bad "File > Open: no Longhorn dialog (flxp-filedialog)"; fi
        timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key alt+F4
        for i in $(seq 100); do kill -0 $P 2>/dev/null || break; sleep .1; done
        grep -q '^large_header *= *1' "$XDG_STATE_HOME/lhde/mupf.ini" && ok "mupf.ini remembers the Large Header" ||
            bad "mupf.ini: $(grep large_header "$XDG_STATE_HOME/lhde/mupf.ini" 2>&1)"
    else bad "$theme: mupf has no window"; fi
    kill $P 2>/dev/null; wait $P 2>/dev/null
fi

[ -n "$PP" ] && { kill $PP 2>/dev/null; wait $PP 2>/dev/null; }
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
