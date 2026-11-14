#!/bin/sh
# Super+R opens the start menu's Run dialog (Start > Run...): luxwm's default "Mod4+r shell run" is _LHSHELL_COMMAND run,
# luxshell forwards it to the start-menu provider (flxp-startmenu, which it starts) as _LHSHELL_START with l[0] bit 1, and the
# provider shows its Run dialog ("Run", WM_CLASS flxp-startmenu, or flxp-taskbar under a classic taskbar); Escape closes it. Then settings.ini [commands] run, when set,
# runs instead. luxwm, luxshell and the provider on Xvfb with a scratch HOME. Needs Xvfb, xdotool and xprop.
# Usage: tests/headless/runkey.sh [:DISPLAY]
D=${1:-:80}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x "$R/build/bin/luxshell" ] || { echo "skip: luxshell is not built"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p $T/home/.config/lhde
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D PATH="$R/build/bin:$PATH"
unset DBUS_SESSION_BUS_ADDRESS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"
shell() { "$R/build/bin/luxshell" > $T/luxshell.log 2>&1 & PS=$!; }
shell
RUN="--onlyvisible --name ^Run$"   # the dialog by its title (its win32 class FlxpRunDialog is not the X one)
run_up() { xdotool search $RUN >/dev/null 2>&1; }
# Super+R until the dialog shows (the provider takes a moment to start and claim the start menu)
# (before it does, luxshell opens its own run box, which Escape closes)
# the provider is flxp-startmenu, or the classic taskbar itself when luxshell runs one (a theme without the Avalon taskbar's
# images, as with nothing imported)
started() { grep -q 'start menu: started\|taskbar: started' $T/luxshell.log; }
wait_for 20 started; sleep 2
PROV=flxp-startmenu
grep -q 'taskbar classic' $T/luxshell.log && PROV=flxp-taskbar
try() { xdotool key super+r; wait_for 2 run_up || { xdotool key Escape; sleep 1; false; }; }
n=0; until try || [ $n -ge 10 ]; do n=$((n + 1)); done
if run_up; then
    RW=$(xdotool search $RUN | head -1)
    if xprop -id $RW WM_CLASS | grep -q "\"$PROV\""; then ok "Super+R: the Run dialog ($PROV's \"Run\")"
    else bad "Super+R: a \"Run\" window that is not $PROV's ($(xprop -id $RW WM_CLASS))"; fi
    xdotool windowactivate --sync $RW 2>/dev/null; sleep .3
    xdotool key Escape
    run_gone() { ! run_up; }
    if wait_for 5 run_gone; then ok "Escape closes it"
    else bad "the Run dialog is still up after Escape"; fi
else
    bad "Super+R: no Run dialog (luxshell's log: $(tail -n 3 $T/luxshell.log | tr '\n' '|'))"
fi
# [commands] run overrides it
kill $PS 2>/dev/null; wait $PS 2>/dev/null
printf '[commands]\nrun = touch %s/ran\n' "$T" > $T/home/.config/lhde/settings.ini
shell; sleep 2
n=0; until [ -e $T/ran ] || [ $n -ge 10 ]; do xdotool key super+r; sleep 1; n=$((n + 1)); done
[ -e $T/ran ] && ok "[commands] run: run instead" || bad "[commands] run did not run"
sleep .5
run_up && bad "[commands] run: the Run dialog showed as well" || ok "[commands] run: no Run dialog"
kill $PS 2>/dev/null; wait $PS 2>/dev/null
kill $PW 2>/dev/null; wait $PW 2>/dev/null
# what luxshell started on this display (the provider) ends with the server
stop_xvfb
echo "$fails failed"
exit $fails
