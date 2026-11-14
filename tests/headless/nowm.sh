#!/bin/sh
# flxp's chrome follows the window manager (docs/CLIENT-PROTOCOL.md "Without luxwm"): on Xvfb under jade4074 (Explorer.chrome =
# longhorn) with no window manager the header is the Small one, without its title row (no caption-like rows on top: the band
# starts where the Small header ends), and the same in the window's pixels: the dark band starts higher than under luxwm; when
# luxwm starts the full header comes back (the band lower, by the title row), when luxwm is killed (a stale
# _NET_SUPPORTING_WM_CHECK) it goes again, under fvwm2 (when installed) it stays Small; under aerovision (the vision header,
# which lives in luxwm's frame) with no window manager the XP chrome. FLXP_WM_LOG=1's lines give what flxp decided; the pixels
# are read with import. Needs Xvfb, xdotool, xprop, xwininfo and ImageMagick. Usage: tests/headless/nowm.sh [:DISPLAY]
D=${1:-:78}
R=$(cd "$(dirname "$0")/../.." && pwd)
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool xprop xwininfo import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde $T/home/.local/state/lhde $T/home/docs
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D FLXP_WM_LOG=1
[ -n "$styles" ] && export LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS FLXP_LHWM FLXP_HEADER_SIZE
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
# the last FLXP_WM_LOG line, once it names the window manager $1
logged() { tail -n 1 $T/flxp.log 2>/dev/null | grep -q "^flxp: window manager $1:"; }
last() { grep '^flxp: window manager' $T/flxp.log | tail -n 1; }
other_wm() { case "$(last)" in *"manager lhwm:"*|*"no window manager:"*|"") return 1 ;; esac; }
rows() { last | sed -n 's/.*header \([0-9]*\) rows.*/\1/p'; }
# the first row of the window (a column at 3/5 of its width, clear of the buttons and the breadcrumb) much darker than its top
# rows: where the header's dark band starts
band() {
    import -window $W $T/w.png 2>/dev/null
    x=$(($($IM $T/w.png -format %w info:) * 3 / 5))
    $IM $T/w.png -crop 1x200+$x+0 +repage -colorspace gray -depth 8 txt:- 2>/dev/null |
        sed -n 's/^0,\([0-9]*\): (\([0-9.]*\).*/\1 \2/p' |
        awk 'NR <= 3 { top += $2 / 3 } NR > 3 && $2 < top * 0.6 { n++; if (n == 3) { print $1 - 2; exit } next } { n = 0 }'
}
xprop -root -f _LH_THEME 8u -set _LH_THEME jade4074
"$R/build/bin/flxp" $T/home/docs > $T/flxp.log 2>&1 & P=$!
W=$(wait_window 30 --name '^docs$')
[ -n "$W" ] || { bad "no flxp window"; kill $P 2>/dev/null; stop_xvfb; exit 1; }
wait_for 10 logged 'no window manager' && sleep .8
small=$(rows) b_small=$(band)
case "$(last)" in
*"chrome longhorn, header "*" rows (small)") ok "no window manager: the Small header ($small rows; $(last))" ;;
*) bad "no window manager: $(last)" ;;
esac
[ -n "$b_small" ] && [ $((b_small - small)) -le 1 ] && [ $((small - b_small)) -le 1 ] && ok "no window manager: the band starts at row $b_small, under the Small header" ||
    bad "no window manager: the band starts at row [$b_small], the Small header has $small rows"
# luxwm starts: the full header (the title row back)
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
if wait_for 20 logged lhwm; then
    sleep .8
    full=$(rows) b_full=$(band)
    case "$(last)" in *"chrome longhorn, header "*" rows") ok "luxwm started: the full header ($full rows)" ;; *) bad "luxwm started: $(last)" ;; esac
    [ -n "$full" ] && [ "$full" -gt "${small:-0}" ] && ok "luxwm: the header is $((full - small)) rows taller (the title row)" ||
        bad "luxwm: header $full rows, without luxwm $small"
    [ -n "$b_full" ] && [ "$b_full" -gt "${b_small:-0}" ] && ok "luxwm: the band starts at row $b_full, lower than without luxwm" ||
        bad "luxwm: the band starts at row [$b_full], without luxwm [$b_small]"
else
    bad "luxwm started, flxp did not follow: $(last)"
fi
# luxwm killed: its _NET_SUPPORTING_WM_CHECK stays behind, naming a window that is gone
kill $PW 2>/dev/null; wait $PW 2>/dev/null
if wait_for 10 logged 'no window manager'; then
    sleep .8
    [ "$(rows)" = "$small" ] && ok "luxwm killed: the Small header again ($(rows) rows)" || bad "luxwm killed: $(last)"
else
    bad "luxwm killed, flxp did not follow: $(last)"
fi
# another window manager: its caption has the title
if command -v fvwm2 >/dev/null 2>&1; then
    fvwm2 -f "$R/session/lab.fvwm2rc" > $T/fvwm.log 2>&1 & PF=$!
    if wait_for 20 other_wm; then
        case "$(last)" in *"chrome longhorn, header $small rows (small)") ok "fvwm2: the Small header ($(last))" ;; *) bad "fvwm2: $(last)" ;; esac
    else
        bad "fvwm2 started, flxp did not follow: $(last)"
    fi
    kill $PF 2>/dev/null; wait $PF 2>/dev/null
else
    echo "skip fvwm2: not installed"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
# the vision header needs luxwm's frame: the XP chrome without it
xprop -root -remove _NET_SUPPORTING_WM_CHECK 2>/dev/null   # (fvwm2 leaves it behind)
xprop -root -f _LH_THEME 8u -set _LH_THEME aerovision
: > $T/flxp.log
"$R/build/bin/flxp" $T/home/docs > $T/flxp.log 2>&1 & P=$!
if wait_for 30 logged 'no window manager'; then
    case "$(last)" in *"chrome xp, header 0 rows") ok "aerovision, no window manager: the XP chrome" ;; *) bad "aerovision, no window manager: $(last)" ;; esac
else
    bad "aerovision: no log line ($(tail -n 2 $T/flxp.log))"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
