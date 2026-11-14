#!/bin/sh
# Folder Options' File Types page (flxp --folder-options=filetypes) on a scratch home and data directory under luxwm on Xvfb:
# typing in the list jumps to an extension (png, then txt); TXT opens with the program that claims text/plain ("Set by"
# the first claimant); Change... picks it in the Open With dialog and the user's mimeapps.list gets the line, Restore takes it
# out; Advanced (Edit File Type) with New... adds an action, which writes flxp's copy of the program's desktop file into the
# data home, and Restore removes the copy; New makes the user's own extension .zzq (lhde-extensions.xml, when
# update-mime-database is installed) and Delete removes it. FLXP_TRACE_FILETYPES's lines say what the details show.
# Needs Xvfb, xdotool and xprop. Usage: tests/headless/filetypes.sh [:DISPLAY]
D=${1:-:77}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config $T/home/.local/share/applications $T/usr/applications $T/usr/mime
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache XDG_DATA_DIRS=$T/usr XDG_CONFIG_DIRS=$T/etc DISPLAY=$D FLXP_TRACE_FILETYPES=1
unset LHDE_DATA LHDE_SETTINGS XDG_CURRENT_DESKTOP
# a distribution of its own: a mime database with a few globs, one program claiming text/plain
printf '50:text/plain:*.txt\n50:image/png:*.png\n50:text/x-csrc:*.c\n' > $T/usr/mime/globs2
printf 'text/x-csrc text/plain\n' > $T/usr/mime/subclasses
printf '[Desktop Entry]\nType=Application\nName=ZZ Test Editor\nExec=/bin/true %%f\nMimeType=text/plain;\n' > $T/usr/applications/zztest.desktop
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
shows() { tail -n 1 $T/out | grep -q "^filetypes: $1\$"; }
expect() {   # WHAT LINE: wait until the details show LINE
    if wait_for 10 shows "$2"; then ok "$1: $2"; else bad "$1: expected [$2] got [$(tail -n 1 $T/out)]"; fi
}
has() { grep -q "$2" "$1" 2>/dev/null; }
"$R/build/bin/flxp" --folder-options=filetypes > $T/out 2>&1 & P=$!
W=$(wait_window 30 --name '^Folder Options$')
if [ -z "$W" ]; then
    bad "no Folder Options window"
else
    xdotool windowactivate --sync $W 2>/dev/null; sleep .5
    expect "the page opens at TXT" "TXT text/plain zztest.desktop claimant"
    xdotool type --delay 80 png; expect "typing png" "PNG image/png - -"
    sleep 1.5
    xdotool type --delay 80 txt; expect "typing txt" "TXT text/plain zztest.desktop claimant"
    # Change...: the Open With dialog, its first recommended program
    xdotool key alt+c
    OW=$(wait_window 10 --name '^Open With$')
    [ -n "$OW" ] && ok "Change... opens Open With" || bad "Change... opened no Open With dialog"
    sleep .5; xdotool key Return
    expect "Change: the user's choice" "TXT text/plain zztest.desktop user restore"
    has $T/home/.config/mimeapps.list '^text/plain=zztest.desktop$' && ok "Change: ~/.config/mimeapps.list has text/plain=zztest.desktop" ||
        bad "Change: ~/.config/mimeapps.list: $(cat $T/home/.config/mimeapps.list 2>&1)"
    xdotool key alt+r
    expect "Restore" "TXT text/plain zztest.desktop claimant"
    has $T/home/.config/mimeapps.list 'text/plain' && bad "Restore: still in mimeapps.list: $(cat $T/home/.config/mimeapps.list)" ||
        ok "Restore: text/plain is out of ~/.config/mimeapps.list"
    # Advanced: Edit File Type, New... an action
    xdotool key alt+v
    ET=$(wait_window 10 --name '^Edit File Type$')
    if [ -n "$ET" ]; then
        ok "Advanced opens Edit File Type"
        sleep .5; xdotool key alt+n
        NA=$(wait_window 10 --name '^New Action$')
        [ -n "$NA" ] && ok "New... opens New Action" || bad "New... opened no New Action dialog"
        sleep .5; xdotool type --delay 30 "Print"; xdotool key Tab; xdotool type --delay 30 "/bin/true --print %f"; xdotool key Return
        sleep .8; xdotool key Return    # Edit File Type's OK
        expect "an action added: the copy, so Restore" "TXT text/plain zztest.desktop claimant restore"
        C=$T/home/.local/share/applications/zztest.desktop
        has $C '^X-Lhde-Override=true$' && has $C '^Actions=print;$' && has $C '^Exec=/bin/true --print %f$' &&
            ok "the copy in the data home has the action" || bad "the copy: $(cat $C 2>&1)"
        xdotool key alt+r
        expect "Restore: the distribution's file again" "TXT text/plain zztest.desktop claimant"
        [ -f $C ] && bad "Restore left $C" || ok "Restore removed the copy"
    else
        bad "Advanced opened no Edit File Type dialog"
    fi
    # New / Delete: the user's own extension
    if command -v update-mime-database >/dev/null 2>&1; then
        xdotool key alt+e
        NE=$(wait_window 10 --name '^Create New Extension$')
        if [ -n "$NE" ]; then
            sleep .5; xdotool type --delay 30 zzq; xdotool key Tab; xdotool key ctrl+a; xdotool type --delay 30 text/plain; xdotool key Return
            expect "New .zzq" "ZZQ text/plain zztest.desktop claimant"
            has $T/home/.local/share/mime/packages/lhde-extensions.xml 'glob pattern="\*.zzq"' && ok "New: lhde-extensions.xml has *.zzq" ||
                bad "New: lhde-extensions.xml: $(cat $T/home/.local/share/mime/packages/lhde-extensions.xml 2>&1)"
            xdotool key alt+d
            MB=$(wait_window 10 --name '^File Types$')
            sleep .5; xdotool key Return
            wait_for 10 sh -c "! grep -q zzq $T/home/.local/share/mime/packages/lhde-extensions.xml"
            has $T/home/.local/share/mime/packages/lhde-extensions.xml 'zzq' && bad "Delete: .zzq still there" || ok "Delete: .zzq removed"
        else
            bad "New opened no Create New Extension dialog"
        fi
    else
        echo "skip New / Delete: no update-mime-database"
    fi
    xdotool key Escape
    wait_for 10 sh -c "! kill -0 $P 2>/dev/null" && ok "Escape closes the sheet, flxp exits" || bad "flxp still runs after Escape"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
kill $PW 2>/dev/null; wait $PW 2>/dev/null
stop_xvfb
echo "$fails failed"
exit $fails
