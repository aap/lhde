#!/bin/sh
# Display Properties' Desktop tab, its lists without a window (flxp-settings --list desktop), on a scratch home and Windows tree:
# the backgrounds in XP's order ("(None)" first, then by name: the recently browsed pictures of flxp.ini's wallpapers are listed,
# not put on top) with the one in force selected; the icon sets: every imported one (lhde's icons/*.theme whose resources are
# there, ReactOS's among them) and Wine's built-in set, each chosen one read back as itself. Usage: tests/settings-lists.sh
cd "$(dirname "$0")/.." || exit 1
B=$(pwd)/build/bin/flxp-settings
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/home/.config/lhde" "$T/home/Pictures/Wallpapers" "$T/elsewhere" \
         "$T/win/index/resources/reactos" "$T/win/index/resources/xp"   # ReactOS's and XP's icon sets imported, 2000's and 4074's not
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
settings() {   # ICONS WALLPAPER: settings.ini with the classic theme and that current layer
    printf '[theme]\nname = classic\n[current]\ntheme = classic\nDesktop.icons = %s\nDesktop.wallpaper = %s\n' "$1" "$2" > "$T/home/.config/lhde/settings.ini"
}
walls() { "$B" --list desktop | sed -n 's/^\(\*\{0,1\}\)wallpaper /\1/p' | tr '\n' '|'; }
icons() { "$B" --list desktop | sed -n 's/^\(\*\{0,1\}\)icons /\1/p' | tr '\n' '|'; }

settings wine "$T/elsewhere/Mountain.jpg"
check "backgrounds: (None) first, then by name; the one in force selected" \
    "(None)|apple|Autumn|beach|*Mountain|zebra|" "$(walls)"
settings wine none
check "backgrounds: (None) selected" "*(None)|apple|Autumn|beach|Mountain|zebra|" "$(walls)"
settings wine "$T/home/Pictures/Wallpapers/zebra.bmp"
check "backgrounds: the last one selected" "(None)|apple|Autumn|beach|Mountain|*zebra|" "$(walls)"
for set in reactos wine xp; do
    settings $set none
    case $set in
    reactos) want="*ReactOS = reactos|Windows XP = xp|Wine = wine|" ;;
    wine)    want="ReactOS = reactos|Windows XP = xp|*Wine = wine|" ;;
    xp)      want="ReactOS = reactos|*Windows XP = xp|Wine = wine|" ;;
    esac
    check "icon sets: $set chosen, read back as itself" "$want" "$(icons)"
done
# a set whose resources are not imported is not offered; chosen anyway, it falls back (the first of xp, 2k, reactos)
settings 2k none
check "icon sets: 2k not imported falls back to xp" "ReactOS = reactos|*Windows XP = xp|Wine = wine|" "$(icons)"
[ $fails = 0 ] && echo "settings-lists: all passed"
exit $fails
