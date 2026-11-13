#!/bin/sh
# Run as (user.lux.run, docs/ANNOTATIONS.md): a file whose attribute names a command opens with that command, not with its
# type's program. Under Xvfb without a window manager and with nothing imported: data.xyz carries
# user.lux.run = "sh RUN.SH %f", where RUN.SH writes its argument and its working directory to a marker; a double click on
# the file in flxp runs it (the marker holds the file's path and its folder), Enter on the selected file runs it again; a
# path with a space and a quote (the folder's name) arrives as one argument.
# Needs Xvfb, xdotool and setfattr; prints "skip: ..." and passes without them or without user xattrs on $TMPDIR.
# Usage: tests/headless/runas.sh [:DISPLAY]
D=${1:-:77}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
B=$TOP/build/bin/flxp
for t in Xvfb xdotool setfattr; do command -v $t >/dev/null 2>&1 || { echo "skip: needs $t"; exit 0; }; done
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
F="$T/it's here"
mkdir -p $T/home/.config/lhde "$F"
if ! touch "$F/probe" || ! setfattr -n user.probe -v 1 "$F/probe" 2>/dev/null; then echo "skip: no user xattrs in $T"; exit 0; fi
rm -f "$F/probe"
cat > $T/run.sh <<'EOF'
#!/bin/sh
printf '%s\n%s\n' "$1" "$(pwd)" >> "$(dirname "$0")/marker"
EOF
echo data > "$F/data.xyz"
setfattr -n user.lux.run -v "sh $T/run.sh %f" "$F/data.xyz"
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache LHDE_WINDOWS=/nonexistent DISPLAY=$D
unset LHDE_DATA LHDE_SETTINGS
printf '[flxp]\ntaskpane = 0\n' > $T/home/.config/lhde/flxp.ini
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1: $3"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
lines() { [ -f $T/marker ] && wc -l < $T/marker | tr -d ' ' || echo 0; }
has() { [ "$(lines)" -ge "$1" ]; }

"$B" --geometry 800x500+0+0 "$F" > $T/out 2>&1 & P=$!
W=$(wait_window 30 --class Flxp)
if [ -z "$W" ]; then echo "FAIL no Explorer window"; fails=$((fails + 1)); else
    timeout 10 xdotool windowfocus --sync $W 2>/dev/null; sleep .5
    # the first row (y 101 at 800 x 500 in the classic look): data.xyz, the folder's only file
    xdotool mousemove --window $W 260 101 click --repeat 2 --delay 80 1
    wait_for 10 has 2
    check "double click: the file as the argument" "$F/data.xyz" "$(sed -n 1p $T/marker 2>/dev/null)"
    check "double click: run in the file's folder" "$F" "$(sed -n 2p $T/marker 2>/dev/null)"
    sleep .5
    xdotool key Return
    wait_for 10 has 4
    check "Enter: run again" "$F/data.xyz" "$(sed -n 3p $T/marker 2>/dev/null)"
    check "the folder's own window stays" 1 "$(xdotool search --onlyvisible --class Flxp 2>/dev/null | wc -l | tr -d ' ')"
fi
kill $P 2>/dev/null; wait $P 2>/dev/null
stop_xvfb
[ $fails = 0 ] && echo PASS || { echo "FAIL ($fails)"; exit 1; }
