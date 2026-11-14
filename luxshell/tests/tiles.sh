#!/bin/sh
# The tray items (volume, brightness, touchpad), the Classic Tray tile hosting them and the power tile, over liblhde's
# hardware layer (build/bin/lhde-hw) with a fake tree (LHDE_HW_FAKE: sysfs for Linux, sysctl and mixer files for FreeBSD)
# and the system commands stood in by shell scripts on a PATH of their own (wpctl, pactl, amixer, brightnessctl, xinput: each
# logs its arguments to $TR_LOG and answers from $TR_STATE); tiles.lua drives the tiles through a stand-in `lh`. No X
# display. Usage: tiles.sh [LUA]
cd "$(dirname "$0")/../.." || exit 1
LUA=${1:-$(command -v lua5.4 || command -v lua54 || echo lua5.4)}   # lua54: FreeBSD's name
command -v "$LUA" >/dev/null 2>&1 || { echo "skip: no $LUA"; exit 0; }
[ -x build/bin/lhde-hw ] || { echo "skip: build/bin/lhde-hw not built"; exit 0; }
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
XINPUT='case "$1" in
list) printf "%s\n" "⎡ Virtual core pointer                    	id=2	[master pointer  (3)]" "⎜   ↳ Virtual core XTEST pointer              	id=4	[slave  pointer  (2)]" "⎜   ↳ SynPS/2 Synaptics TouchPad              	id=12	[slave  pointer  (2)]" "⎣ Virtual core keyboard                   	id=3	[master keyboard (2)]";;
list-props) read v < "$TR_STATE"; printf "Device '"'"'SynPS/2 Synaptics TouchPad'"'"':\n\tDevice Enabled (185):\t%s\n\tlibinput Tapping Enabled (320):\t1\n\tlibinput Device Enabled (186):\t%s\n" "$v" "$v";;
set-prop) echo "$4" > "$TR_STATE";;
esac'
XINPUT_NONE='[ "$1" = list ] && printf "%s\n" "⎡ Virtual core pointer                    	id=2	[master pointer  (3)]" "⎜   ↳ Virtual core XTEST pointer              	id=4	[slave  pointer  (2)]"'
run() {   # TEST STATE: a fresh PATH with the stubs set up before; $SYSTEM the layer's back end (linux by default)
    echo "$2" > "$T/state"; rm -f "$T/state.mute"; : > "$T/log"
    TR_LOG="$T/log" TR_STATE="$T/state" PATH="$T/bin" LHDE_HW_FAKE="$T/hw" LHDE_HW_SYSTEM="${SYSTEM:-linux}" XDG_STATE_HOME="$T/xdg" \
        TR_LHDE_HW="$PWD/build/bin/lhde-hw" "$LUA" luxshell/tests/tiles.lua luxshell "$1" || fails=$((fails + 1))
    rm -rf "$T/bin"; mkdir -p "$T/bin" "$T/hw" "$T/xdg"
unset LHDE_HW_VOLUME
BL="$T/hw/sys/class/backlight"
}
mkdir -p "$T/bin"
stub wpctl "$WPCTL"; stub pactl "$PACTL"; run volume-wpctl 0.40
stub wpctl "$WPCTL_BROKEN"; stub pactl "$PACTL"; run volume-pactl 55
stub amixer "$AMIXER"; run volume-amixer 30
run volume-none 0
mkdir -p "$T/hw/dev"; printf 'vol 66:66\npcm 100:100\nmute 0\n' > "$T/hw/dev/mixer0"
SYSTEM=freebsd run volume-oss 0
rm -rf "$T/hw/dev"
mkdir -p "$BL/acpi_video0" "$BL/empty"; echo 600 > "$BL/acpi_video0/brightness"; echo 1000 > "$BL/acpi_video0/max_brightness"
echo firmware > "$BL/acpi_video0/type"
run brightness 0
echo 600 > "$BL/acpi_video0/brightness"
if [ "$(id -u)" != 0 ]; then
    chmod 444 "$BL/acpi_video0/brightness"; run brightness-readonly 0
    stub brightnessctl 'case "$*" in *set*) for a; do v=$a; done; echo "$v" > "$LHDE_HW_FAKE/sys/class/backlight/acpi_video0/brightness.new";; esac'
    TR_BRIGHTNESSCTL=1; export TR_BRIGHTNESSCTL; run brightness-readonly 0; unset TR_BRIGHTNESSCTL
    chmod 644 "$BL/acpi_video0/brightness"
fi
rm -rf "$BL/acpi_video0"; run brightness-none 0
PS="$T/hw/sys/class/power_supply"
mkdir -p "$PS/AC" "$PS/BAT0" "$PS/BAT1"; echo Mains > "$PS/AC/type"; echo 0 > "$PS/AC/online"
for f in type:Battery status:Discharging energy_now:20000000 energy_full:23190000 power_now:7500000; do echo "${f#*:}" > "$PS/BAT0/${f%%:*}"; done
for f in type:Battery status:Unknown energy_now:10000000 energy_full:21000000 power_now:0; do echo "${f#*:}" > "$PS/BAT1/${f%%:*}"; done
run power 0
rm -rf "$PS"
mkdir -p "$T/hw/sysctl"
for f in units:2 life:99 time:-1 state:0 rate:0; do echo "${f#*:}" > "$T/hw/sysctl/hw.acpi.battery.${f%%:*}"; done
echo 1 > "$T/hw/sysctl/hw.acpi.acline"
SYSTEM=freebsd run power-freebsd 0
rm -rf "$T/hw/sysctl"
stub xinput "$XINPUT"; run touchpad 1
stub xinput "$XINPUT_NONE"; run touchpad-none 1
stub wpctl "$WPCTL"; stub xinput "$XINPUT"; run host 0.40
[ $fails = 0 ] && echo "tiles: all passed" || echo "tiles: $fails failed"
exit $fails
