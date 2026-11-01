#!/bin/sh
# Display Properties' Desktop tab, its lists without a window (flxp-settings --list desktop), on a scratch home and Windows tree:
# the backgrounds in XP's order ("(None)" first, then by name: the recently browsed pictures of flxp.ini's wallpapers are listed,
# not put on top) with the one in force selected. Usage: tests/settings-lists.sh
cd "$(dirname "$0")/.." || exit 1
B=$(pwd)/build/bin/flxp-settings
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/home/.config/lhde" "$T/home/Pictures/Wallpapers" "$T/elsewhere" "$T/win"
for f in beach.jpg Autumn.png zebra.bmp notes.txt; do echo x > "$T/home/Pictures/Wallpapers/$f"; done
echo x > "$T/elsewhere/Mountain.jpg"; echo x > "$T/elsewhere/apple.jpg"
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache LHDE_WINDOWS=$T/win XDG_DATA_DIRS=/nonexistent   # the scratch home only
unset DISPLAY LHDE_DATA LHDE_SETTINGS
# the recently browsed pictures, newest first
printf '[flxp]\nwallpapers = %s;%s\n' "$T/elsewhere/Mountain.jpg" "$T/elsewhere/apple.jpg" > "$T/home/.config/lhde/flxp.ini"
fails=0
check() {   # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then echo "ok   $1"; else printf 'FAIL %s:\n  expected: %s\n  got:      %s\n' "$1" "$2" "$3"; fails=$((fails + 1)); fi
}
settings() {   # WALLPAPER: settings.ini with the classic theme and that current layer
    printf '[theme]\nname = classic\n[current]\ntheme = classic\nDesktop.wallpaper = %s\n' "$1" > "$T/home/.config/lhde/settings.ini"
}
walls() { "$B" --list desktop | sed -n 's/^\(\*\{0,1\}\)wallpaper /\1/p' | tr '\n' '|'; }

settings "$T/elsewhere/Mountain.jpg"
check "backgrounds: (None) first, then by name; the one in force selected" \
    "(None)|apple|Autumn|beach|*Mountain|zebra|" "$(walls)"
settings none
check "backgrounds: (None) selected" "*(None)|apple|Autumn|beach|Mountain|zebra|" "$(walls)"
settings "$T/home/Pictures/Wallpapers/zebra.bmp"
check "backgrounds: the last one selected" "(None)|apple|Autumn|beach|Mountain|*zebra|" "$(walls)"
[ $fails = 0 ] && echo "settings-lists: all passed"
exit $fails
