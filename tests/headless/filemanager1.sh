#!/bin/sh
# org.freedesktop.FileManager1 (browsers' "Show in Folder") on a private session bus under Xvfb, without a window manager:
# flxp-desktop owns the name; gdbus calls ShowItems on a.txt (an Explorer on x/ with a.txt selected: flxp's FLXP_TRACE_LIST
# "selected:" line), ShowFolders on y/ (a window named y) and ShowItemProperties on a.txt (its sheet, "a.txt Properties").
# Then without flxp-desktop the bus starts the service from a service file (flxp-desktop --filemanager1, as installed):
# ShowItems on b.txt selects it. Needs Xvfb, xdotool, dbus-daemon and gdbus. Usage: tests/headless/filemanager1.sh [:DISPLAY]
D=${1:-:79}
R=$(cd "$(dirname "$0")/../.." && pwd)
for t in Xvfb xdotool dbus-daemon gdbus; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
mkdir -p $T/home/.config/lhde $T/home/.local/share/dbus-1/services $T/x $T/y $T/home/Desktop
echo a > $T/x/a.txt; echo b > $T/x/b.txt; echo c > $T/x/c.txt
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D LHDE_WINDOWS=/nonexistent FLXP_TRACE_LIST=1
unset LHDE_DATA LHDE_SETTINGS
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; rm -rf $T; exit 1; }
# the service file as make install writes it, Exec= the build tree's flxp-desktop (the bus reads $XDG_DATA_HOME/dbus-1/services)
sed "s|@BINDIR@|$R/build/bin|" "$R/flxp/res/org.freedesktop.FileManager1.service.in" > $T/home/.local/share/dbus-1/services/org.freedesktop.FileManager1.service
out=$(dbus-daemon --session --fork --print-address 1 --print-pid 1) || { echo "FAIL no dbus-daemon"; stop_xvfb; rm -rf $T; exit 1; }
export DBUS_SESSION_BUS_ADDRESS=$(echo "$out" | sed -n 1p); BP=$(echo "$out" | sed -n 2p)
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
owned() { gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.NameHasOwner org.freedesktop.FileManager1 2>/dev/null | grep -q true; }
owner_pid() { gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.GetConnectionUnixProcessID org.freedesktop.FileManager1 2>/dev/null | sed -n 's/^(uint32 \([0-9]*\),)$/\1/p'; }
fm1() {   # METHOD URI...: the call, its reply
    _m=$1; shift; _a=""
    for u in "$@"; do _a="$_a${_a:+, }'$u'"; done
    gdbus call --session --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 \
        --method org.freedesktop.FileManager1.$_m "[$_a]" "" 2>&1
}
selected() { grep -q "^selected: $1\$" $T/desk.err; }
close_named() { for w in $(xdotool search --onlyvisible --name "$1" 2>/dev/null); do xdotool windowclose $w 2>/dev/null; done; }
"$R/build/bin/flxp-desktop" > /dev/null 2> $T/desk.err & PD=$!
wait_for 30 owned
check "flxp-desktop owns org.freedesktop.FileManager1" "$PD" "$(owner_pid)"
check "ShowItems answers" "()" "$(fm1 ShowItems "file://$T/x/a.txt")"
W=$(wait_window 30 --name '^x$'); wait_for 20 selected "$T/x/a.txt"
check "ShowItems: x/ opens with a.txt selected" "yes yes" "$([ -n "$W" ] && echo yes) $(selected "$T/x/a.txt" && echo yes)"
close_named '^x$'
check "ShowFolders answers" "()" "$(fm1 ShowFolders "file://$T/y")"
check "ShowFolders: y/ opens" "yes" "$([ -n "$(wait_window 30 --name '^y$')" ] && echo yes)"
close_named '^y$'
check "ShowItemProperties answers" "()" "$(fm1 ShowItemProperties "file://$T/x/a.txt")"
check "ShowItemProperties: a.txt's sheet" "yes" "$([ -n "$(wait_window 30 --name '^a.txt Properties$')" ] && echo yes)"
close_named '^a.txt Properties$'
kill $PD; wait $PD 2>/dev/null
# no flxp-desktop: the bus starts flxp-desktop --filemanager1 (its children's trace goes to the bus's stderr, so the window
# is the check; flxp's own trace with the selection from a second call in a terminal of ours)
wait_for 10 sh -c '! gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.NameHasOwner org.freedesktop.FileManager1 | grep -q true'
check "activated: ShowItems answers" "()" "$(fm1 ShowItems "file://$T/x/b.txt")"
check "activated: x/ opens" "yes" "$([ -n "$(wait_window 30 --name '^x$')" ] && echo yes)"
SP=$(owner_pid)
check "activated: the service is flxp-desktop --filemanager1" "yes" "$(ps -o args= -p "$SP" 2>/dev/null | grep -q 'flxp-desktop --filemanager1' && echo yes)"
close_named '^x$'
# flxp-desktop takes the name from the activated service, which exits
"$R/build/bin/flxp-desktop" > /dev/null 2> $T/desk.err & PD=$!
gone() { ! kill -0 "$SP" 2>/dev/null; }
wait_for 30 gone
check "flxp-desktop takes the name, the service exits" "$PD yes" "$(owner_pid) $(gone && echo yes)"
check "and answers: c.txt selected" "()" "$(fm1 ShowItems "file://$T/x/c.txt")"
wait_for 30 selected "$T/x/c.txt"
check "ShowItems: c.txt selected" "yes" "$(selected "$T/x/c.txt" && echo yes)"
close_named '^x$'
kill $PD; wait $PD 2>/dev/null
[ -n "$SP" ] && kill $SP 2>/dev/null
for p in $(pgrep -f "$T/" 2>/dev/null); do kill $p 2>/dev/null; done
kill $BP 2>/dev/null
stop_xvfb
rm -rf $T
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
