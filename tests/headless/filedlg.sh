#!/bin/sh
# flxp-filedialog in both looks (XP template, Longhorn): open, multi-select,
# save with the overwrite prompt (No, then Yes), folder mode, a typed
# wildcard, a typed folder, cancel; Longhorn only: Hide Explorer (remembered
# across runs), Show in Groups from the list's menu (remembered), the
# view keys (Ctrl+0..3) remembered with the view.
# Needs Xvfb and xdotool. Usage: tests/headless/filedlg.sh [:DISPLAY]
D=${1:-:73}
T=$(mktemp -d)
B=$(cd "$(dirname "$0")/../.." && pwd)/build/bin/flxp-filedialog
mkdir -p $T/home/.config/lhde $T/dir/Projects $T/dir/Photos
for f in notes.txt todo.txt Budget.xls Accounts.xls readme.md; do echo x > "$T/dir/$f"; done
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config DISPLAY=$D
Xvfb $D -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
sleep 1
fails=0
check() {   # check NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
# run LOOK KEYS... -- ARGS: start the dialog, wait for its window, send the keys
# (words: a key name, "type:TEXT", "sleep"), print what it printed and the exit status
run() {
    look=$1; shift
    keys=""
    while [ "$1" != "--" ]; do keys="$keys
$1"; shift; done
    shift
    (cd $T/dir && $B --look $look --directory-start $T/dir "$@" > $T/out 2>/dev/null; echo $? > $T/status) &
    W=""
    for i in 1 2 3 4 5 6 7 8 9 10; do
        sleep .5
        W=$(xdotool search --onlyvisible --class Flxp 2>/dev/null | head -1)
        [ -n "$W" ] && break
    done
    sleep 1.5
    xdotool windowfocus --sync $W 2>/dev/null
    sleep .3
    echo "$keys" | while IFS= read -r k; do
        [ -z "$k" ] && continue
        case "$k" in
        type:*) xdotool type --delay 30 "${k#type:}" ;;
        sleep) sleep 1 ;;
        click3:*) xy=${k#click3:}; xdotool mousemove --window $(xdotool search --onlyvisible --class Flxp | head -1) ${xy%,*} ${xy#*,} click 3 ;;
        geom) xdotool getwindowgeometry $(xdotool search --onlyvisible --class Flxp | head -1) | sed -n 's/.*Geometry: //p' > $T/geom ;;
        *) xdotool key $k ;;
        esac
        sleep .4
    done
    for i in 1 2 3 4 5 6 7 8 9 10; do [ -s $T/status ] && break; sleep .5; done
    if [ ! -s $T/status ]; then xdotool search --class Flxp | while read w; do xdotool windowkill $w 2>/dev/null; done; sleep .5; fi
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
    check "$look save no" "1 " "$(run $look ctrl+a type:notes.txt Return sleep n sleep Escape -- --save)"
    check "$look save yes" "0 $T/dir/notes.txt|" "$(run $look ctrl+a type:notes.txt Return sleep Return -- --save)"
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
run longhorn alt+h sleep geom Escape -- --save > /dev/null; small=$(cut -dx -f2 $T/geom)
check "hide explorer shrinks" "yes" "$([ -n "$small" ] && [ -n "$full" ] && [ "$small" -lt $((full - 150)) ] && echo yes || echo "no ($full -> $small)")"
check "hide explorer remembered" "filedlg_explorer = 0" "$(grep '^filedlg_explorer' $HOME/.config/lhde/flxp.ini)"
run longhorn geom alt+b sleep Escape -- --save > /dev/null; again=$(cut -dx -f2 $T/geom)
check "starts hidden" "$small" "$again"
check "browse folders remembered" "filedlg_explorer = 1" "$(grep '^filedlg_explorer' $HOME/.config/lhde/flxp.ini)"
check "accept while hidden" "0 $T/dir/kept.txt|" "$(run longhorn alt+h ctrl+a type:kept.txt Return -- --save)"
run longhorn alt+b sleep Escape -- --save > /dev/null
# Show in Groups from the list's background menu (right-click: View > Show in Groups)
check "groups on by default" "" "$(grep '^filedlg_groups' $HOME/.config/lhde/flxp.ini)"
run longhorn click3:650,300 v g sleep Escape -- --open > /dev/null
check "groups off remembered" "filedlg_groups = 0" "$(grep '^filedlg_groups' $HOME/.config/lhde/flxp.ini)"
run longhorn click3:650,300 v g sleep Escape -- --open > /dev/null
check "groups on again" "filedlg_groups = 1" "$(grep '^filedlg_groups' $HOME/.config/lhde/flxp.ini)"
check "size remembered" "yes" "$(grep -q '^filedlg_size = [0-9]*x[0-9]*' $HOME/.config/lhde/flxp.ini && echo yes)"
# The view keys (browser::view_key): Ctrl+3 big icons with thumbnails, Ctrl+1
# Details keeping the thumbnails toggle, Ctrl+2 Icons at 48 px, Ctrl+0 the
# default view (Longhorn: Icons at 48 px with thumbnails); remembered in filedlg_view
run longhorn ctrl+3 sleep Escape -- --open > /dev/null
check "ctrl+3 thumbnails" "filedlg_view = thumbnails@96" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"
run longhorn ctrl+1 sleep Escape -- --open > /dev/null
check "toggle kept in details" "filedlg_view = details+thumbnails@96" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"
run longhorn ctrl+2 sleep Escape -- --open > /dev/null
check "ctrl+2 icons" "filedlg_view = thumbnails@48" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"
run longhorn ctrl+4 ctrl+0 sleep Escape -- --open > /dev/null
check "ctrl+0 default view" "filedlg_view = thumbnails@48" "$(grep '^filedlg_view' $HOME/.config/lhde/flxp.ini)"

kill $XP 2>/dev/null
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
