#!/bin/sh
# luxshell keeps exactly one start-menu provider. The provider is run the way FreeBSD runs the flxp-startmenu symlink: its
# command name is flxp-taskbar, its argv[0] flxp-startmenu (a stand-in in a scratch build tree execs build/bin/flxp-taskbar
# under that argv[0]); luxshell is a copy of build/bin/luxshell in that tree, so lh.prog finds the stand-in. Checked, with the
# Avalon taskbar (LHSHELL_TASKBAR=avalon):
#   after 20 s one provider runs on the display and luxshell started it once;
#   killed, it is started again once (one provider, another pid);
#   luxshell killed with SIGKILL leaves it running, the next luxshell adopts it ("already runs") and starts none for 10 s;
#   luxshell's exit stops it;
# then with the classic taskbar (LHSHELL_TASKBAR=classic): luxshell starts one flxp-taskbar and no provider, and its exit
# stops the bar.
# luxwm and luxshell on Xvfb with a scratch HOME. Needs Xvfb, xprop, python3. Usage: tests/headless/startmenu.sh [:DISPLAY]
D=${1:-:82}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xprop python3; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x "$R/build/bin/luxshell" ] && [ -x "$R/build/bin/luxwm" ] && [ -x "$R/build/bin/flxp-taskbar" ] || { echo "skip: luxwm, luxshell or flxp-taskbar is not built"; exit 0; }
T=$(mktemp -d)
PW= PS=
cleanup() {
    [ -n "$PS" ] && kill $PS 2>/dev/null
    for p in $(providers) $(classics); do kill $p 2>/dev/null; done
    [ -n "$PW" ] && kill $PW 2>/dev/null
    stop_xvfb; rm -rf "$T"
}
trap cleanup EXIT
# the scratch build tree: the source tree's share, luxwm (what marks a tree), luxshell's Lua and build/lib by symlinks
mkdir -p $T/tree/build/bin $T/home/.config/lhde
for d in share luxwm luxshell; do ln -s "$R/$d" $T/tree/$d; done
ln -s "$R/build/lib" $T/tree/build/lib
cp "$R/build/bin/luxshell" $T/tree/build/bin/luxshell
ln -s "$R/build/bin/flxp-taskbar" $T/tree/build/bin/flxp-taskbar
printf '#!/bin/sh\nexec python3 -I -c "import os, sys; os.execv(sys.argv[1], sys.argv[2:])" %s "$0" "$@"\n' "'$R/build/bin/flxp-taskbar'" > $T/tree/build/bin/flxp-startmenu
chmod +x $T/tree/build/bin/flxp-startmenu
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache XDG_RUNTIME_DIR=$T DISPLAY=$D PATH="$R/build/bin:$PATH" LHSHELL_COMPOSITION=0 LHSHELL_WALLPAPER=0 \
       LHDE_WINDOWS=/nonexistent
unset DBUS_SESSION_BUS_ADDRESS LHDE_DATA LHDE_SETTINGS LHSHELL_TASKBAR_CMD
LHDE_SELF_DIR=$R/session; . "$R/session/lhde-paths.sh"
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
# processes of our display started as NAME (argv[0]'s last component), whatever their command name
started_as() {
    for p in $(pgrep -f "$1"); do
        a0=$(lhde_args $p | head -n 1); [ "${a0##*/}" = "$1" ] || continue
        lhde_environ $p | grep -qx "DISPLAY=$D" && echo $p
    done
}
providers() { started_as flxp-startmenu; }
classics() { started_as flxp-taskbar; }
count() { "$@" | wc -l | tr -d ' '; }
one_provider() { [ "$(count providers)" = 1 ]; }
no_provider() { [ "$(count providers)" = 0 ]; }
LOG=$T/luxshell.log
starts() { grep -c 'start menu: started' $LOG; }
shell() { : > $LOG; LHSHELL_TASKBAR=$1 $T/tree/build/bin/luxshell >> $LOG 2>&1 & PS=$!; }
stop_shell() { kill $PS; wait $PS 2>/dev/null; PS=; }

"$R/build/bin/luxwm" > $T/luxwm.log 2>&1 & PW=$!
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
wait_for 20 wm_up || echo "note: luxwm did not announce itself"

shell avalon
wait_for 20 one_provider || bad "no provider within 20 s ($(tail -n 3 $LOG | tr '\n' '|'))"
sleep 20
n=$(count providers)
[ "$n" = 1 ] && [ "$(starts)" = 1 ] && ok "one provider after 20 s, started once (pid $(providers))" || bad "$n providers after 20 s, $(starts) starts"
grep -q "_LH_STARTMENU_S0 is owned" $T/luxshell-startmenu*.log 2>/dev/null && bad "a provider found the start menu owned"

P1=$(providers); kill $P1
wait_for 10 no_provider || true
wait_for 10 one_provider; sleep 7
n=$(count providers); P2=$(providers)
[ "$n" = 1 ] && [ "$P2" != "$P1" ] && [ "$(starts)" = 2 ] && ok "killed, it is started again once (pid $P2)" || bad "after a kill: $n providers, $(starts) starts"

kill -9 $PS; wait $PS 2>/dev/null
shell avalon; sleep 10
n=$(count providers)
[ "$n" = 1 ] && [ "$(providers)" = "$P2" ] && grep -q "start menu: flxp-startmenu already runs ($P2)" $LOG && [ "$(starts)" = 0 ] \
    && ok "the next luxshell adopts it" || bad "after a luxshell restart: $n providers, $(starts) starts ($(grep 'start menu' $LOG | tr '\n' '|'))"
stop_shell
wait_for 5 no_provider && ok "luxshell's exit stops the provider" || bad "$(count providers) providers after luxshell's exit"

shell classic
classic_up() { [ "$(count classics)" = 1 ]; }
wait_for 15 classic_up; sleep 10
n=$(count classics)
[ "$n" = 1 ] && no_provider && grep -q 'taskbar: started' $LOG && ! grep -q 'already runs' $LOG \
    && ok "classic: one flxp-taskbar started, no provider" || bad "classic: $n bars, $(count providers) providers ($(grep 'taskbar\|start menu' $LOG | tr '\n' '|'))"
stop_shell
no_bar() { [ "$(count classics)" = 0 ]; }
wait_for 5 no_bar && ok "luxshell's exit stops the classic bar" || bad "$(count classics) bars after luxshell's exit"

[ $fails = 0 ] && echo "all passed" || { echo "--- luxshell log"; tail -n 30 $LOG; }
exit $fails
