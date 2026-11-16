#!/bin/sh
# luxfind under luxwm on Xvfb, driven with xdotool, what it shows read from LUXFIND_TRACE=1's lines (status, pending,
# deferred, tagged, commit, opened). A scratch root of generated files (3,000 in 30 folders, some tagged) with its
# catalogue: the window lists everything with the count of matches; typing a part of a name narrows it as typed (the
# count lhde-cat find --name gives); a tag chosen narrows it to the tagged ones. Then the edits through the catalogue
# while the root's directory cannot be reached (moved away): the window says so, Tag Selected records the tag as an
# operation that waits (lhde-cat diff shows it, the file is untouched, the Pending column), the directory comes back,
# Commit writes it (the file carries the tag, diff is empty, the journal has it applied). A HOME and XDG directories of
# its own. Needs Xvfb, xdotool, getfattr and setfattr; prints "skip: ..." and passes without them. LUXFIND_SHOT=FILE
# keeps a screenshot of the window (ImageMagick's import). Usage: tests/headless/luxfind.sh [:DISPLAY]
D=${1:-:79}
R=$(cd "$(dirname "$0")/../.." && pwd)
B=$R/build/bin/luxfind
C=$R/build/bin/lhde-cat
for t in Xvfb xdotool getfattr setfattr; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
styles=$("$R/build/bin/lhde-theme" find styles 2>/dev/null)
T=$(mktemp -d); [ -n "$LF_KEEP" ] && echo "kept: $T"
trap '[ -n "$LF_KEEP" ] || rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde $T/root
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LUXFIND_TRACE=1 LUXFIND_TEST=1
[ -n "$styles" ] && export LHDE_WINDOWS=${styles%/index/styles}
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1: expected [$2] got [$3]"; fi; }

# the root: 30 folders of 100 files, the notes ending in 1 tagged "work", the pictures of 2019 tagged "holiday"
for d in $(seq -w 0 29); do
    mkdir -p "$T/root/album-$d"
    for i in $(seq -w 0 99); do
        case $i in
        *[05]) f="$T/root/album-$d/IMG_$d$i.jpg" ;;
        *) f="$T/root/album-$d/note-$d$i.txt" ;;
        esac
        : > "$f"
    done
done
setfattr -n user.xdg.tags -v test "$T/root/album-00/note-0001.txt" 2>/dev/null || { echo "skip: no user attributes on $T"; exit 0; }
for f in "$T"/root/album-*/note-*1.txt; do setfattr -n user.xdg.tags -v work "$f"; done
for f in "$T"/root/album-1*/IMG_*.jpg; do setfattr -n user.xdg.tags -v holiday "$f"; done
setfattr -n user.lux.desc -v "a shopping list" "$T/root/album-03/note-0301.txt"
$C add-root t "$T/root" > /dev/null || { echo "FAIL add-root"; exit 1; }
$C index t --quiet > /dev/null || { echo "FAIL index"; exit 1; }
all=$($C find t --quiet | wc -l | tr -d ' ')

start_xvfb $D -screen 0 1280x800x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
command -v xprop >/dev/null 2>&1 && { wait_for 20 wm_up || echo "note: luxwm did not announce itself"; }
theme=classic; [ -n "$styles" ] && [ -f "$styles/luna.msstyles" ] && theme=luna
printf '[theme]\nname = %s\n' $theme > $T/home/.config/lhde/settings.ini
command -v xprop >/dev/null 2>&1 && xprop -root -f _LH_THEME 8u -set _LH_THEME $theme

traced() { grep -q -- "$1" $T/out.log; }
last() { grep "^$1 " $T/out.log | tail -1 | sed "s/^$1 //"; }
status_has() { last status | grep -q -- "$1"; }
run() {
    : > $T/out.log
    "$B" "$@" >> $T/out.log 2> $T/err.log & P=$!
    W=$(wait_window 20 --classname '^luxfind$') || { bad "luxfind's window did not show (luxfind $*)"; cat $T/err.log; return 1; }
    wait_for 20 traced '^mapped' || bad "no mapped line"
}
quit() {
    xdotool windowactivate --sync "$W" 2>/dev/null; xdotool key ctrl+w 2>/dev/null
    wait_for 10 sh -c "! kill -0 $P 2>/dev/null" || { bad "luxfind did not exit"; kill $P; }
    wait $P 2>/dev/null
}
n_fmt() { _n=$1; _s=; while [ ${#_n} -gt 3 ]; do _s=",${_n#${_n%???}}$_s"; _n=${_n%???}; done; echo "$_n$_s"; }

# ---- the window: everything, a part of a name as typed, a tag -----------------------------------------------------------
run --root t
wait_for 20 status_has "$(n_fmt $all) items" && ok "everything: $all items" || bad "everything: [$(last status)], want $all items"
xdotool type --delay 30 "note-012"
want=$($C find t --name note-012 --quiet | wc -l | tr -d ' ')
wait_for 20 status_has "^$want items" && ok "as typed: note-012 gives $want" || bad "as typed: [$(last status)], want $want items"
xdotool key alt+n BackSpace
wait_for 20 status_has "$(n_fmt $all) items" && ok "cleared: everything again" || bad "cleared: [$(last status)]"
xdotool key alt+t; xdotool type --delay 20 "holiday"; xdotool key Return
want=$($C find t --tag holiday --quiet | wc -l | tr -d ' ')
wait_for 20 status_has "^$want items" && ok "a tag: holiday gives $want" || bad "a tag: [$(last status)], want $want"
if [ -n "$LUXFIND_SHOT" ] && command -v import >/dev/null 2>&1; then
    xdotool key alt+t ctrl+a BackSpace Return alt+n
    xdotool type --delay 20 "note-03"
    wait_for 20 status_has "items" && sleep 1
    import -window root "$LUXFIND_SHOT" 2>/dev/null && ok "screenshot $LUXFIND_SHOT"
fi
quit

# ---- an edit while the root cannot be reached, then the commit --------------------------------------------------------------
mv "$T/root" "$T/root.away"
run --root t --name note-0402
wait_for 20 status_has "t: not reachable" && ok "the root said not reachable" || bad "reachability: [$(last status)]"
wait_for 20 status_has "^1 item" || bad "note-0402: [$(last status)]"
xdotool mousemove --window "$W" 420 110 click 1
xdotool key ctrl+a
xdotool key alt+t; xdotool type --delay 20 "offline-tag"
xdotool key alt+a
wait_for 10 traced '^deferred t album-04/note-0402.txt ok' && ok "Tag Selected deferred while not reachable" || { bad "no deferred line"; tail -5 $T/out.log; }
wait_for 10 traced '^pending t 1' && ok "pending 1" || bad "pending: $(grep ^pending $T/out.log | tr '\n' ' ')"
diff=$($C diff t 2>&1)
case "$diff" in *note-0402.txt*offline-tag*) ok "lhde-cat diff shows it pending" ;; *) bad "lhde-cat diff: [$diff]" ;; esac
check "the file untouched" "" "$(getfattr --only-values -n user.xdg.tags "$T/root.away/album-04/note-0402.txt" 2>/dev/null)"
xdotool key alt+c
wait_for 10 traced '^commit t unreachable' && ok "Commit refused while not reachable" || bad "commit while away: $(grep ^commit $T/out.log)"
xdotool key Return   # the message box
mv "$T/root.away" "$T/root"
xdotool key alt+c
wait_for 20 traced '^commit t ok' && ok "Commit from the window" || { bad "commit: $(grep ^commit $T/out.log)"; tail -5 $T/out.log; }
check "the file carries the tag" "offline-tag" "$(getfattr --only-values -n user.xdg.tags "$T/root/album-04/note-0402.txt" 2>/dev/null)"
check "lhde-cat diff empty" "" "$($C diff t 2>/dev/null)"
j=$($C journal t --path album-04/note-0402.txt 2>&1)
case "$j" in *tag_add*offline-tag*applied*|*applied*tag_add*offline-tag*) ok "the journal: tag_add applied" ;; *) bad "journal: [$j]" ;; esac
wait_for 10 traced '^pending t 0' && ok "nothing pending after" || bad "pending after: $(grep ^pending $T/out.log | tail -1)"
quit

kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
[ $fails = 0 ]
