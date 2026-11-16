#!/bin/sh
# luxview under luxwm on Xvfb, driven with xdotool, what it shows read from LUXVIEW_TRACE=1's lines (mapped, title, index,
# shown, drawn, slideshow, trashed, properties, exit). A folder of generated pictures with a 6000 x 4000 JPEG first: the
# window is up within 300 ms of the start with the first picture's name in the title before the picture is decoded, and
# that picture is decoded at the window's size, not at its own; Right / Left / End / Home walk the folder in the Explorer's
# Name order (10_last.bmp after 4_alpha.png); three Rights made fast end on the fourth picture; + and - change the drawn
# size by the zoom steps (46% -> 50% -> 100% -> 50%; 100% decodes the picture whole); Delete asks (Confirm File Delete)
# and Return moves the file to the Trash, the next picture shown; F11 is the full screen slide show
# (_NET_WM_STATE_FULLSCREEN, the screen's size), Escape leaves it; Alt+Enter opens the Properties sheet; the peak RSS at
# Best Fit stays under LIMIT_FIT_MB; Actual Size of the 6000 x 4000 JPEG decodes it whole, and closing the window during a
# decode exits cleanly; the peak RSS stays under LIMIT_MB; the window's size comes back from luxview.ini. Then the look at
# measured points: under luna XP's (the window colour around the picture, its grey frame, the toolbar band), under
# slate4051 and jade4074 4051's (black, the box's 128 grey frame at 40, 31 grey inside, the filmstrip's selected cell); a
# theme not imported is skipped. Then 4051's filmstrip on a network folder (tests/headless/slowfs.c) past
# thumbnails_network_budget: a cell whose file's server does not answer shows its stand-in (the file type's icon in the
# thumbnail's white frame, on black), the current one's on its selected cell, then every cell on screen its thumbnail. A HOME and XDG directories of its own. Needs Xvfb, xdotool, xprop, xwininfo and
# ImageMagick; prints "skip: ..." and passes without them. Usage: tests/headless/luxview.sh [:DISPLAY]
D=${1:-:78}
LIMIT_FIT_MB=80       # the folder walked at Best Fit (the 6000 x 4000 JPEG decoded at 1/4, resampled to the window)
LIMIT_MB=200          # then that picture at its own size: 96 MB of it
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/bin/luxview
IM=convert; command -v magick >/dev/null 2>&1 && IM=magick
for t in Xvfb xdotool xprop xwininfo import $IM; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
T=$(mktemp -d); [ -n "$LV_KEEP" ] && echo "kept: $T"
trap '[ -n "$LV_KEEP" ] || rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde $T/pics
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LUXVIEW_TRACE=1
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

$IM -size 6000x4000 gradient:navy-orange -quality 85 $T/pics/1_big.jpg || { echo "FAIL ImageMagick could not write the JPEG"; exit 1; }
$IM -size 640x480 xc:red $T/pics/2_red.png
$IM -size 1600x1200 gradient:white-green $T/pics/3_grad.jpg
$IM -size 400x300 xc:none -fill 'rgba(0,0,255,0.5)' -draw 'rectangle 0,0 399,299' $T/pics/4_alpha.png
$IM -size 320x240 xc:yellow $T/pics/10_last.bmp
printf 'not a picture\n' > $T/pics/notes.txt
mkdir -p $T/look; cp $T/pics/2_red.png $T/look/a.png; cp $T/pics/10_last.bmp $T/look/b.bmp

start_xvfb $D -screen 0 1280x800x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
theme() {   # the session theme: settings.ini and the root property, as the session sets it
    printf '[theme]\nname = %s\n' "$1" > $T/home/.config/lhde/settings.ini
    xprop -root -f _LH_THEME 8u -set _LH_THEME "$1"
}
have_style() { [ -n "$styles" ] && [ -f "$styles/$1.msstyles" ]; }
if have_style luna; then theme luna; else theme classic; fi
sleep .5

peak_kb() {   # peak_kb PID: the process's peak resident set in KB (Linux: VmHWM of /proc; FreeBSD: procstat's maximum RSS)
    if [ -r /proc/$1/status ]; then sed -n 's/^VmHWM:[[:space:]]*\([0-9]*\) kB/\1/p' /proc/$1/status
    else procstat -r $1 2>/dev/null | sed -n 's/.*maximum RSS[[:space:]]*\([0-9]*\) KB.*/\1/p'; fi
}
traced() { grep -q "$1" $T/out.log; }                         # a trace line matching
last() { grep "^$1 " $T/out.log | tail -1 | sed "s/^$1 //"; }  # the last trace line of a kind
title_is() { [ "$(last title)" = "$1" ]; }
run() {   # run ARGS...: luxview, its window in $W, the time to the window in $MS
    : > $T/out.log
    t0=$(date +%s%N)
    # FreeBSD's jemalloc (MALLOC_CONF; glibc and musl do not read it): one arena and freed pages returned at once, so the
    # peak RSS is the pictures' memory and not what four arenas per CPU hold back
    MALLOC_CONF=narenas:1,dirty_decay_ms:0,muzzy_decay_ms:0 "$B" "$@" >> $T/out.log 2>$T/err.log & P=$!
    W=""
    n=0
    while [ $n -lt 3000 ]; do   # poll for the window without sleeping, to time it
        W=$(xdotool search --onlyvisible --classname '^luxview$' 2>/dev/null | head -1)
        [ -n "$W" ] && break
        n=$((n + 1))
    done
    MS=$(( ($(date +%s%N) - t0) / 1000000 ))
    [ -n "$W" ] && timeout 10 xdotool windowactivate --sync $W 2>/dev/null
}
quit() {
    timeout 10 xdotool windowactivate --sync $W 2>/dev/null; xdotool key alt+F4
    for i in $(seq 100); do kill -0 $P 2>/dev/null || break; sleep .1; done
    if kill -0 $P 2>/dev/null; then kill $P; bad "luxview did not exit"; fi
    wait $P 2>/dev/null; RC=$?
}
key() { xdotool key "$@"; }

# 1. the folder: the window at once, the first picture's name before its decode, then the decode at the window's size
run $T/pics
if [ -z "$W" ]; then bad "no luxview window"; cat $T/err.log
else
    if [ $MS -le 300 ]; then ok "the window is up in $MS ms (300 at most)"; else bad "the window took $MS ms (300 at most)"; fi
    wait_for 10 title_is "1_big.jpg - luxview"
    check "the title names the first picture (the Explorer's order)" "1_big.jpg - luxview" "$(last title)"
    wait_for 20 traced "^shown 1_big.jpg picture"
    set -- $(grep "^shown 1_big.jpg picture" $T/out.log | head -1)
    check "the 6000 x 4000 JPEG: its own size known" "6000x4000" "$6"
    w=${4%x*}
    if [ -n "$w" ] && [ "$w" -le 1280 ]; then ok "decoded at the window's size ($4), not its own"; else bad "decoded at $4"; fi
    # 2. the walk
    key Right; wait_for 10 title_is "2_red.png - luxview"; check "Right: the next" "2_red.png - luxview" "$(last title)"
    key Left; wait_for 10 title_is "1_big.jpg - luxview"; check "Left: back" "1_big.jpg - luxview" "$(last title)"
    key End; wait_for 10 title_is "10_last.bmp - luxview"; check "End: the last, 10 after 4 (natural order)" "10_last.bmp - luxview" "$(last title)"
    key Home; wait_for 10 title_is "1_big.jpg - luxview"; check "Home: the first" "1_big.jpg - luxview" "$(last title)"
    xdotool key --delay 15 Right Right Right
    wait_for 10 title_is "4_alpha.png - luxview"
    check "three fast Rights: the fourth" "4_alpha.png - luxview" "$(last title)"
    wait_for 10 traced "^shown 4_alpha.png picture"
    check "and it is the one shown" "4_alpha.png" "$(grep '^shown ' $T/out.log | tail -1 | cut -d' ' -f2)"
    # 3. zoom steps on 3_grad.jpg (1600 x 1200)
    key Left; wait_for 10 traced "^shown 3_grad.jpg picture"
    drawn_is() { [ "$(last drawn)" = "$1" ]; }
    sleep .3
    fit=$(last drawn)
    key plus; wait_for 10 drawn_is "800x600 zoom 50%"; check "+ from Best Fit ($fit): 50%" "800x600 zoom 50%" "$(last drawn)"
    key plus; wait_for 10 drawn_is "1600x1200 zoom 100%"; check "+: 100%" "1600x1200 zoom 100%" "$(last drawn)"
    wait_for 10 traced "^shown 3_grad.jpg picture 1600x1200"
    check "100%: decoded at its own size" "1600x1200" "$(grep '^shown 3_grad.jpg picture' $T/out.log | tail -1 | cut -d' ' -f4)"
    key minus; wait_for 10 drawn_is "800x600 zoom 50%"; check "-: 50%" "800x600 zoom 50%" "$(last drawn)"
    key ctrl+b; sleep .3; check "Ctrl+B: Best Fit again" "$fit" "$(last drawn)"
    # 4. Delete: asks, Return says Yes, the file is in the Trash, the next one shown
    key Delete
    MB=$(wait_window 10 --name '^Confirm File Delete$')
    if [ -z "$MB" ]; then bad "Delete: no Confirm File Delete"
    else
        ok "Delete asks (Confirm File Delete)"
        timeout 10 xdotool windowactivate --sync $MB 2>/dev/null; xdotool key Return
        gone() { [ ! -e $T/pics/3_grad.jpg ] && [ -e $XDG_DATA_HOME/Trash/files/3_grad.jpg ]; }
        wait_for 10 gone
        if gone; then ok "Delete: 3_grad.jpg in the Trash"; else bad "Delete: 3_grad.jpg not moved to the Trash"; fi
        wait_for 10 title_is "4_alpha.png - luxview"; check "Delete: the next one shown" "4_alpha.png - luxview" "$(last title)"
    fi
    # 5. the slide show: full screen, Escape leaves it
    timeout 10 xdotool windowactivate --sync $W 2>/dev/null
    key F11
    fs() { xprop -id $W _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_FULLSCREEN; }
    wait_for 10 fs
    if fs; then ok "F11: full screen"; else bad "F11: not full screen ($(xprop -id $W _NET_WM_STATE))"; fi
    sleep .5
    check "the slide show covers the screen" "1280x800" "$(xwininfo -id $W | sed -n 's/^ *Width: *//p')x$(xwininfo -id $W | sed -n 's/^ *Height: *//p')"
    key Escape
    notfs() { ! fs; }
    wait_for 10 notfs
    if notfs; then ok "Escape: the slide show ends"; else bad "Escape: still full screen"; fi
    check "trace: in and out" "1 0" "$(grep '^slideshow ' $T/out.log | cut -d' ' -f2 | tr '\n' ' ' | sed 's/ $//')"
    # 6. Properties
    sleep .5; timeout 10 xdotool windowactivate --sync $W 2>/dev/null
    key alt+Return
    PS=$(wait_window 10 --name '^4_alpha.png Properties$')
    if [ -n "$PS" ]; then ok "Alt+Enter: the Properties sheet"; timeout 10 xdotool windowactivate --sync $PS 2>/dev/null; xdotool key Escape; sleep .5
    else bad "Alt+Enter: no Properties sheet"; fi
    # 7. Actual Size of the big one, closed during its decode
    timeout 10 xdotool windowactivate --sync $W 2>/dev/null
    hwm_fit=$(peak_kb $P)
    if [ -n "$hwm_fit" ] && [ $((hwm_fit / 1024)) -lt $LIMIT_FIT_MB ]; then ok "peak RSS at Best Fit $((hwm_fit / 1024)) MB (under $LIMIT_FIT_MB)"
    else bad "peak RSS at Best Fit ${hwm_fit:-?} KB (limit $LIMIT_FIT_MB MB)"; fi
    key Home; wait_for 10 title_is "1_big.jpg - luxview"
    wait_for 20 traced "^shown 1_big.jpg"
    key ctrl+a
    wait_for 20 traced "^shown 1_big.jpg picture 6000x4000"
    check "Actual Size: decoded whole" "6000x4000" "$(grep '^shown 1_big.jpg picture' $T/out.log | tail -1 | cut -d' ' -f4)"
    key ctrl+b; key End; sleep .2; key Home; sleep .05; key ctrl+a; sleep .05
    hwm=$(peak_kb $P)
    quit
    check "closed during a decode: exit 0" 0 "$RC"
    traced "^exit" && ok "closed during a decode: the loader joined, main returned" || bad "no exit line (crashed?)"
    if [ -n "$hwm" ] && [ $((hwm / 1024)) -lt $LIMIT_MB ]; then ok "peak RSS $((hwm / 1024)) MB (under $LIMIT_MB)"
    else bad "peak RSS ${hwm:-?} KB (limit $LIMIT_MB MB)"; fi
    check "luxview.ini keeps the window" "800" "$(sed -n 's/^width *= *//p' $XDG_STATE_HOME/lhde/luxview.ini)"
fi

# 8. the look at measured points; the window 856 x 689 (the reference shot's client), a 640 x 480 red picture, the first of two
look() {   # look THEME
    theme $1; sleep .5
    run $T/look/a.png
    if [ -z "$W" ]; then bad "$1: no window"; return; fi
    xdotool windowsize $W 856 689; sleep .3
    wait_for 10 traced "^shown a.png picture"
    wait_for 10 traced "^look "
    sleep 1
    import -window $W $T/look.png
    quit
}
if have_style luna; then
    look luna
    check "luna: XP's look" "xp" "$(last look)"
    # the toolbar band (34 px) at the bottom, the picture area 8 px in: 640 x 480 centred in 840 x 639 at (8, 8)
    expect "luna: the field (the window colour)" $T/look.png 20 20 255,255,255
    expect "luna: the picture" $T/look.png 428 327 255,0,0
    expect "luna: its 1 px frame (button shadow)" $T/look.png 107 327 172,168,153
    expect "luna: the band under the area" $T/look.png 20 670 255,255,255
else echo "skip luna: not imported"; fi
for t in slate4051 jade4074; do
    if ! have_style $t; then echo "skip $t: not imported"; continue; fi
    look $t
    check "$t: 4051's look" "4051" "$(last look)"
    expect "$t: black" $T/look.png 10 10 0,0,0
    expect "$t: the box's frame at (40, 40)" $T/look.png 40 300 128,128,128
    expect "$t: its right side at 41 from the edge" $T/look.png 815 300 128,128,128
    expect "$t: its bottom at 141 from the bottom" $T/look.png 300 548 128,128,128
    expect "$t: inside the box" $T/look.png 50 300 31,31,31
    expect "$t: the picture" $T/look.png 428 294 255,0,0
    expect "$t: the selected cell's border (the first)" $T/look.png 2 630 124,88,49
    expect "$t: its light ring" $T/look.png 30 593 200,195,180
    expect "$t: its fill under the ring" $T/look.png 30 594 157,147,117
    expect "$t: its fill at the bottom" $T/look.png 30 665 157,122,75
done

# 9. the filmstrip on a network folder: 40 pictures of 1.5 MB (one picture hard-linked) under tests/headless/slowfs.c, which
# makes the folder an NFS mount to luxview, with thumbnails_network_budget = 1 (MB), no EXIF thumbnails, End: every one of the
# 13 cells on screen gets its thumbnail (the strip reads what it shows whole, past the budget), the current one's too.
# n39.jpg's server does not answer for HANG_MS a call (the shim's LHDE_SLOW_HANG_PREFIX): until it does, its cell (the
# twelfth) and the current one's (n40, behind it in the strip's one loader) show their stand-ins
SHIM=$R/build/tests/slowfs.so
if have_style slate4051 && [ -f "$SHIM" ]; then
    theme slate4051; sleep .5
    mkdir -p $T/net
    $IM -size 1600x1200 xc:'rgb(220,60,60)' -attenuate 0.4 +noise Gaussian -quality 95 $T/net/n01.jpg
    for i in $(seq 2 40); do ln $T/net/n01.jpg $T/net/n$(printf %02d $i).jpg; done
    printf '[flxp]\nthumbnails_network_budget = 1\n' > $T/home/.config/lhde/flxp.ini
    export LD_PRELOAD=$SHIM LHDE_SLOW_PREFIX=$T/net LHDE_SLOW_MS=1 LHDE_SLOW_REMOTE=1 LHDE_SLOW_HANG_PREFIX=$T/net/n39.jpg \
           LHDE_SLOW_HANG_MS=3000
    run $T/net/n01.jpg
    unset LD_PRELOAD LHDE_SLOW_PREFIX LHDE_SLOW_MS LHDE_SLOW_REMOTE LHDE_SLOW_HANG_PREFIX LHDE_SLOW_HANG_MS
    if [ -z "$W" ]; then bad "filmstrip: no window"
    else
        xdotool windowsize $W 856 689; sleep .3
        wait_for 10 traced "^shown n01.jpg picture"
        key End; wait_for 10 traced "^shown n40.jpg picture"
        # the stand-ins: cell 11 (n39) at x 2 + 62 * 11 = 684, the strip's top 689 - 97 = 592; the thumbnail's box 46 x 34
        # at the cell's centre (715, 628), its white frame 3 px around it (x 689-691), black inside, the icon at 32 px
        standins() {
            import -window $W $T/strip.png 2>/dev/null || return 1
            [ "$(px $T/strip.png 690 628)" = 255,255,255 ] && [ "$(px $T/strip.png 694 628)" = 0,0,0 ] &&
            [ "$(px $T/strip.png 715 628)" != 0,0,0 ] &&
            [ "$(px $T/strip.png 746 630)" = 124,88,49 ] && [ "$(px $T/strip.png 752 628)" = 255,255,255 ] &&
            [ "$(px $T/strip.png 756 628)" = 0,0,0 ]
        }
        if wait_for 3 standins; then ok "filmstrip: before its thumbnail a cell shows the stand-in (icon in the white frame), the current one on its selected cell"
        else bad "filmstrip: no stand-in before the thumbnail: n39's frame $(px $T/strip.png 690 628), inside $(px $T/strip.png 694 628), centre $(px $T/strip.png 715 628); n40's cell $(px $T/strip.png 746 630), frame $(px $T/strip.png 752 628), inside $(px $T/strip.png 756 628)"; fi
        [ -n "$LV_KEEP" ] && cp $T/strip.png $T/standin.png   # (kept: the stand-ins as they were checked)
        reddish() { case "$1" in 2[0-9][0-9],[0-9],*|2[0-9][0-9],[0-9][0-9],*|1[5-9][0-9],[0-9],*|1[5-9][0-9],[0-9][0-9],*) return 0;; esac; return 1; }
        cells_full() {   # every cell's centre (2 + 62 k + 31, 689 - 97 + 36) the picture's red
            import -window $W $T/strip.png 2>/dev/null || return 1
            k=0; while [ $k -lt 13 ]; do reddish "$(px $T/strip.png $((33 + 62 * k)) 628)" || return 1; k=$((k + 1)); done
        }
        if wait_for 60 cells_full; then ok "filmstrip on a network folder past its budget: all 13 cells show their thumbnails, n39's after its stand-in"
        else
            k=0; got=""; while [ $k -lt 13 ]; do got="$got $(px $T/strip.png $((33 + 62 * k)) 628)"; k=$((k + 1)); done
            bad "filmstrip on a network folder past its budget: cells without a thumbnail (centres:$got)"
        fi
        quit
    fi
    rm -f $T/home/.config/lhde/flxp.ini
else echo "skip the filmstrip on a network folder: slate4051 not imported or no slowfs.so"; fi

kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
[ $fails = 0 ] && echo "luxview: all passed" || echo "luxview: $fails failed"
exit $fails
