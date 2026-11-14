#!/bin/sh
# lhde-session xsession under a stand-in display manager: an Xvfb, `lhde-session xsession` on it in the background as a DM runs
# share/xsessions/lhde.desktop's Exec. luxwm, luxshell and flxp-desktop come up with XDG_CURRENT_DESKTOP=lhde, LHDE_DISPLAY and
# the session's own D-Bus bus (the login has none); luxshell's Log Off (_LHSHELL_COMMAND logoff) ends the session: the xsession
# process exits with 0 and nothing it started is left (no process with its LHDE_SESSION_TAG, its dbus-daemon gone, no luxwm,
# luxshell, flxp-desktop or picom on the display). A second session ends by `lhde-session stop` from outside, with a theme given.
# Scratch HOME and XDG_RUNTIME_DIR. Needs Xvfb, xdotool, xprop and dbus-daemon; picom (xrender) when installed.
# Usage: tests/headless/xsession.sh [:DISPLAY]
D=${1:-:84}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool xprop dbus-daemon; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
[ -x "$R/build/bin/luxshell" ] && [ -x "$R/build/bin/luxwm" ] || { echo "skip: luxwm or luxshell is not built"; exit 0; }
T=$(mktemp -d)
mkdir -p $T/home/.config/lhde $T/run
chmod 700 $T/run
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache XDG_RUNTIME_DIR=$T/run DISPLAY=$D LHSHELL_WALLPAPER=0
unset DBUS_SESSION_BUS_ADDRESS LHDE_DISPLAY LHDE_SESSION_TAG LHDE_TERMINAL XDG_CURRENT_DESKTOP DESKTOP_SESSION
if command -v picom >/dev/null 2>&1; then export LHDE_PICOM=1 LHDE_PICOM_BACKEND=xrender; else export LHDE_PICOM=0; fi
. "$(dirname "$0")/wait.sh"
XS=
cleanup() { [ -n "$XS" ] && kill $XS 2>/dev/null; "$R/session/lhde-session" stop >/dev/null 2>&1; stop_xvfb; [ -n "$LHDE_TEST_KEEP" ] && echo "kept $T" || rm -rf "$T"; }
trap cleanup EXIT
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; exit 1; }
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
if [ -r /proc/self/environ ]; then envof() { tr '\0' '\n' 2>/dev/null </proc/$1/environ; }
else envof() { procstat -h -e "$1" 2>/dev/null | sed 's/^ *[0-9]* *[^ ]* *//' | tr ' ' '\n'; }; fi
alive() { kill -0 "$1" 2>/dev/null; }
# our processes whose environment has LINE
with_env() { for p in $(pgrep -u "$(id -u)"); do [ "$p" = $$ ] && continue; envof $p | grep -qxF "$1" && echo $p; done; }
on_display() { for p in $(pgrep -x "$1"); do envof $p | grep -qxF "DISPLAY=$D" && echo $p; done; }
wm_up() { xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; }
shell_up() { [ -n "$(xdotool search --classname '^luxshell-taskbar$' 2>/dev/null | head -1)" ]; }
gone() { ! alive $XS; }
ST=$T/run/lhde-session$D

# one session: start it, see it up, end it with END (a command), check that nothing is left
round() {
    what=$1 end=$2; shift 2
    "$R/session/lhde-session" xsession "$@" > $T/xsession.log 2>&1 & XS=$!
    TAG="LHDE_SESSION_TAG=$D.$XS"
    wait_for 30 wm_up && ok "$what: luxwm up" || bad "$what: luxwm did not come up ($(tail -n 3 $T/xsession.log | tr '\n' '|'))"
    wait_for 30 shell_up && ok "$what: luxshell up" || bad "$what: luxshell did not come up ($(tail -n 3 $ST/luxshell.log 2>/dev/null | tr '\n' '|'))"
    alive $XS && ok "$what: the xsession process runs in the foreground" || bad "$what: lhde-session xsession returned at once"
    sp=$(on_display luxshell | head -1)
    e=$(envof "$sp")
    printf '%s\n' "$e" | grep -qx 'XDG_CURRENT_DESKTOP=lhde' && printf '%s\n' "$e" | grep -qx "LHDE_DISPLAY=$D" \
        && printf '%s\n' "$e" | grep -qxF "$TAG" && ok "$what: luxshell has XDG_CURRENT_DESKTOP=lhde, LHDE_DISPLAY=$D and the tag" \
        || bad "$what: luxshell's environment lacks the session's ($(printf '%s\n' "$e" | grep -E '^(XDG_CURRENT|LHDE_)' | tr '\n' ' '))"
    bus=$(cat $ST/dbus.pid 2>/dev/null)
    [ -n "$bus" ] && alive "$bus" && printf '%s\n' "$e" | grep -q "^DBUS_SESSION_BUS_ADDRESS=$(cat $ST/dbus.addr)\$" \
        && ok "$what: the session's own bus ($bus)" || bad "$what: no session bus of its own"
    [ -n "$(with_env "$TAG" | head -1)" ] || bad "$what: no process carries $TAG"
    [ "$(grep -m1 . $ST/theme 2>/dev/null)" = "${want_theme:-$(grep -m1 . $ST/theme 2>/dev/null)}" ] || bad "$what: theme $(cat $ST/theme), not $want_theme"
    sleep 1
    eval "$end"
    wait_for 15 gone && { wait $XS; rc=$?; [ $rc = 0 ] && ok "$what: the xsession process exited (0)" || bad "$what: the xsession process exited with $rc"; } \
        || bad "$what: the xsession process still runs ($(tail -n 3 $T/xsession.log | tr '\n' '|'))"
    XS=
    # the Log Off command itself (its shell, lhde-session stop) may finish a moment after the session; give it 5 s
    none_left() { [ -z "$(with_env "$TAG")" ]; }
    wait_for 5 none_left
    left=$(with_env "$TAG" | tr '\n' ' ')
    [ -z "$left" ] && ok "$what: no process with the session's tag is left" || bad "$what: left with the tag: $(for p in $left; do ps -o pid= -o comm= -p $p; done | tr '\n' ' ')"
    [ -n "$bus" ] && alive "$bus" && bad "$what: the session's dbus-daemon $bus is left"
    for n in luxwm luxshell flxp-desktop picom; do p=$(on_display $n | tr '\n' ' '); [ -z "$p" ] || bad "$what: $n is left on $D ($p)"; done
    [ -f $ST/loop.pid ] && bad "$what: loop.pid is left"
    :
}
round "Log Off" 'xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND logoff'
grep -q 'session logoff: .*lhde-session.* stop' $ST/luxshell.log 2>/dev/null && ok "luxshell's Log Off ran lhde-session stop" \
    || bad "luxshell's Log Off did not run lhde-session stop ($(grep 'session logoff' $ST/luxshell.log 2>/dev/null))"
want_theme=classic round "lhde-session stop" "LHDE_DISPLAY=$D '$R/session/lhde-session' stop >/dev/null" classic
[ $fails = 0 ] && echo "xsession: all passed" || echo "xsession: $fails failed"
exit $fails
