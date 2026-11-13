#!/bin/sh
# The tray tile (volume) with its system commands stood in by shell scripts on a PATH of their own (wpctl, pactl,
# amixer: each logs its arguments to $TR_LOG and answers from $TR_STATE);
# tiles.lua drives the tiles through a stand-in `lh`. No X display. Usage: tiles.sh [LUA]
cd "$(dirname "$0")/../.." || exit 1
LUA=${1:-lua5.4}
command -v "$LUA" >/dev/null 2>&1 || { echo "skip: no $LUA"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
LUA=$(command -v "$LUA")
fails=0
stub() { printf '#!/bin/sh\necho "%s $*" >> "$TR_LOG"\n%s\n' "$1" "$2" > "$T/bin/$1"; chmod +x "$T/bin/$1"; }
WPCTL='case "$1" in
get-volume) read v < "$TR_STATE"; m=0; [ -f "$TR_STATE.mute" ] && read m < "$TR_STATE.mute"; [ "$m" = 1 ] && v="$v [MUTED]"; echo "Volume: $v";;
set-volume) echo "$3" > "$TR_STATE";;
set-mute) echo "$3" > "$TR_STATE.mute";;
esac'
WPCTL_BROKEN='echo "Translate ID error: '"'"'-1'"'"' is not a valid ID (returned by default-nodes-api)"'
PACTL='case "$1" in
get-sink-volume) read v < "$TR_STATE"; echo "Volume: front-left: 36044 /  $v% / -15.58 dB,   front-right: 36044 /  $v% / -15.58 dB"; echo "        balance 0.00";;
get-sink-mute) echo "Mute: no";;
set-sink-volume) echo "${3%\%}" > "$TR_STATE";;
subscribe) while :; do read x < /dev/null || exit 0; done;;
esac'
AMIXER='read v < "$TR_STATE"; case "$*" in
"get Master") echo "Simple mixer control '"'"'Master'"'"',0"; echo "  Mono: Playback 26 [$v%] [-12.00dB] [off]";;
esac'
run() {   # TEST STATE: a fresh PATH with the stubs set up before
    echo "$2" > "$T/state"; rm -f "$T/state.mute"; : > "$T/log"
    TR_LOG="$T/log" TR_STATE="$T/state" PATH="$T/bin" "$LUA" luxshell/tests/tiles.lua luxshell "$1" || fails=$((fails + 1))
    rm -rf "$T/bin"; mkdir -p "$T/bin"
}
mkdir -p "$T/bin"
stub wpctl "$WPCTL"; stub pactl "$PACTL"; run volume-wpctl 0.40
stub wpctl "$WPCTL_BROKEN"; stub pactl "$PACTL"; run volume-pactl 55
stub amixer "$AMIXER"; run volume-amixer 30
run volume-none 0
[ $fails = 0 ] && echo "tiles: all passed" || echo "tiles: $fails failed"
exit $fails
