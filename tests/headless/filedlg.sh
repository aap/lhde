#!/bin/sh
# flxp-filedialog in both looks (XP template, Longhorn): open, multi-select,
# save with the overwrite prompt (No, then Yes), folder mode, a typed
# wildcard, a typed folder, cancel; Longhorn only: Hide Explorer (remembered
# across runs), Show in Groups from the list's menu (remembered), the
# view keys (Ctrl+0..5) remembered with the view and its size.
# Needs Xvfb and xdotool. Usage: tests/headless/filedlg.sh [:DISPLAY]
D=${1:-:73}
T=$(mktemp -d)
B=$(cd "$(dirname "$0")/../.." && pwd)/build/bin/flxp-filedialog
mkdir -p $T/home/.config/lhde $T/dir/Projects $T/dir/Photos
for f in notes.txt todo.txt Budget.xls Accounts.xls readme.md; do echo x > "$T/dir/$f"; done
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D   # the scratch home only: nothing exported from outside leaks in
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1280x800x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
check() {   # check NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
# the dialog's toplevels: its own, a message box, a menu (a drop shadow not counted)
wins() {
    xdotool search --onlyvisible --class Flxp 2>/dev/null | while read -r w; do
        [ "$(xdotool getwindowname "$w" 2>/dev/null)" = "drop shadow" ] || echo "$w"; done | wc -l
}
nwins() { [ "$(wins)" -eq "$1" ]; }
geom() { xdotool getwindowgeometry "$W" 2>/dev/null | sed -n 's/.*Geometry: //p'; }
resized() { [ "$(geom)" != "$G0" ]; }
# run LOOK KEYS... -- ARGS: start the dialog, wait for its window, send the keys, print what it printed and the exit status.
# Words: a key name; "type:TEXT"; "click3:X,Y" (a right click into the dialog, again until its menu shows and stays: a
# click right after the pointer arrives opens it and closes it at once); "geom" (its size into $T/geom); "wait:N" (until
# N toplevels show: the overwrite question, a submenu, back to 1 when they close); "resized" (until the dialog's geometry
# is no longer what it was at the start: Hide Explorer, Browse Folders); "sleep" (one second, for what shows nowhere: a
# view key's setting written). Every wait gives up after 30 s.
run() {
    look=$1; shift
    keys=""
    while [ "$1" != "--" ]; do keys="$keys
$1"; shift; done
    shift
    (cd $T/dir && $B --look $look --directory-start $T/dir "$@" > $T/out 2>/dev/null; echo $? > $T/status) &
    W=$(wait_window 30 --class Flxp)
    G0=$(geom)
    [ -n "$W" ] && timeout 10 xdotool windowfocus --sync $W 2>/dev/null   # its keys go there (no window manager focuses it)
    echo "$keys" | while IFS= read -r k; do
        [ -z "$k" ] && continue
        case "$k" in
        type:*) xdotool type --delay 30 "${k#type:}" ;;
        sleep) sleep 1 ;;
        wait:*) wait_for 30 nwins ${k#wait:} ;;
        resized) wait_for 30 resized ;;
        click3:*) xy=${k#click3:}; xdotool mousemove --window $W ${xy%,*} ${xy#*,}   # again until a menu shows and stays
            for i in 1 2 3 4 5 6 7 8 9 10; do xdotool click 3; wait_for 3 nwins 2 && sleep .5 && nwins 2 && break; done ;;
        geom) geom > $T/geom ;;
        *) xdotool key $k ;;
        esac
        sleep .2
    done
    wait_for 30 test -s $T/status
    if [ ! -s $T/status ]; then xdotool search --class Flxp | while read w; do xdotool windowkill $w 2>/dev/null; done; wait_for 10 test -s $T/status; fi
    st=$(cat $T/status 2>/dev/null); rm -f $T/status
    out=$(cat $T/out | tr '\n' '|')
    echo "$st $out"
}

for look in xp longhorn; do
    echo "-- $look"
    check "$look open" "0 $T/dir/notes.txt|" "$(run $look type:notes.txt Return -- --open)"
    check "$look multi" "0 $T/dir/notes.txt|$T/dir/todo.txt|" "$(run $look 'type:"notes.txt" "todo.txt"' Return -- --open --multiple)"
    check "$look save new" "0 $T/dir/new.txt|" "$(run $look ctrl+a type:new.txt Return -- --save --filename x.txt)"
    # the overwrite question: No keeps the dialog (Escape cancels it then), Yes replaces
    check "$look save no" "1 " "$(run $look ctrl+a type:notes.txt Return wait:2 n wait:1 Escape -- --save)"
    check "$look save yes" "0 $T/dir/notes.txt|" "$(run $look ctrl+a type:notes.txt Return wait:2 Return -- --save)"
    check "$look folder" "0 $T/dir/Projects|" "$(run $look type:Projects Return -- --directory)"
    check "$look folder current" "0 $T/dir|" "$(run $look Return -- --directory)"
    check "$look wildcard" "0 $T/dir/Budget.xls|" "$(run $look 'type:*.xls' Return ctrl+a type:Budget.xls Return -- --open)"
    check "$look typed folder" "0 $T/dir/Projects/a.txt|" "$(run $look ctrl+a type:Projects/ Return type:a.txt Return -- --save)"
    check "$look default ext" "0 $T/dir/report.txt|" "$(run $look ctrl+a type:report Return -- --save --default-ext txt)"
    check "$look filter index" "0 filter=1|$T/dir/notes.txt|" "$(run $look type:notes.txt Return -- --open --print-filter --filter 'Sheets|*.xls' --filter 'Text|*.txt' --current-filter 1)"
    check "$look cancel" "1 " "$(run $look Escape -- --open)"
done

echo "-- longhorn only"
# Hide Explorer: the window gets shorter, flxp.ini remembers it, the next Save starts so; Browse Folders brings it back
run longhorn geom -- --save > /dev/null; full=$(cut -dx -f2 $T/geom)
run longhorn alt+h resized geom Escape -- --save > /dev/null; small=$(cut -dx -f2 $T/geom)
check "hide explorer shrinks" "yes" "$([ -n "$small" ] && [ -n "$full" ] && [ "$small" -lt $((full - 150)) ] && echo yes || echo "no ($full -> $small)")"
check "hide explorer remembered" "filedlg_explorer = 0" "$(grep '^filedlg_explorer' $HOME/.config/lhde/flxp.ini)"
run longhorn geom alt+b resized Escape -- --save > /dev/null; again=$(cut -dx -f2 $T/geom)
check "starts hidden" "$small" "$again"
check "browse folders remembered" "filedlg_explorer = 1" "$(grep '^filedlg_explorer' $HOME/.config/lhde/flxp.ini)"
check "accept while hidden" "0 $T/dir/kept.txt|" "$(run longhorn alt+h resized ctrl+a type:kept.txt Return -- --save)"
run longhorn alt+b resized Escape -- --save > /dev/null
# Show in Groups from the list's background menu (right-click: View > Show in Groups)
check "groups on by default" "" "$(grep '^filedlg_groups' $HOME/.config/lhde/flxp.ini)"
run longhorn click3:650,300 v wait:3 g wait:1 Escape -- --open > /dev/null
check "groups off remembered" "filedlg_groups = 0" "$(grep '^filedlg_groups' $HOME/.config/lhde/flxp.ini)"
run longhorn click3:650,300 v wait:3 g wait:1 Escape -- --open > /dev/null
check "groups on again" "filedlg_groups = 1" "$(grep '^filedlg_groups' $HOME/.config/lhde/flxp.ini)"
check "size remembered" "yes" "$(grep -q '^filedlg_size = [0-9]*x[0-9]*' $HOME/.config/lhde/flxp.ini && echo yes)"
# The view keys (browser::view_key, 4074's slider stops): Ctrl+3 Large Icons at
# 128 px, Ctrl+1 Details at 16, Ctrl+2 Tiles at 48, Ctrl+plus a step bigger in
# the view shown, Ctrl+5 Small Icons, Ctrl+0 the default view (Icons at 48 px);
# remembered in filedlg_view, the sizes each view had in flxp-view-sizes
run longhorn ctrl+3 sleep Escape -- --open > /dev/null
check "ctrl+3 large icons" "filedlg_view = icons@128" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"
run longhorn ctrl+1 sleep Escape -- --open > /dev/null
check "ctrl+1 details" "filedlg_view = details@16" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"
run longhorn ctrl+2 ctrl+plus sleep Escape -- --open > /dev/null
check "ctrl+2 tiles, a step bigger" "filedlg_view = tiles@64" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"
run longhorn ctrl+5 ctrl+2 sleep Escape -- --open > /dev/null
check "tiles remember their size" "filedlg_view = tiles@64" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"
check "sizes remembered" "details=16 tiles=64 large=128 medium=48 small=16 icons=small" "$(cat $HOME/.local/state/lhde/flxp-view-sizes)"
run longhorn ctrl+4 ctrl+0 sleep Escape -- --open > /dev/null
check "ctrl+0 default view" "filedlg_view = icons@48" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"

stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
