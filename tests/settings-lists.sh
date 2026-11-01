#!/bin/sh
# Display Properties' Desktop tab, its lists without a window (flxp-settings --list desktop), on a scratch home and Windows tree:
# the backgrounds in XP's order ("(None)" first, then by name: the recently browsed pictures of flxp.ini's wallpapers are listed,
# not put on top) with the one in force selected; the icon sets: every imported one (lhde's icons/*.theme whose resources are
# there, ReactOS's among them) and Wine's built-in set, each chosen one read back as itself. Appearance's "Windows and buttons"
# (flxp-settings --list appearance --pick STYLE...): classic, a bare style with a frame set of its own (an Aero patch's, as
# lhde-import makes it) and one without, and back to the theme's own style, also when the theme file still names it by its old
# name (aero-patch); needs a 4074 style of the user's (jade4074, copied under every name), skipped without one.
# Usage: tests/settings-lists.sh
cd "$(dirname "$0")/.." || exit 1
B=$(pwd)/build/bin/flxp-settings
jade=$(build/bin/lhde-theme find styles 2>/dev/null)/jade4074.msstyles   # the user's (or LHDE_WINDOWS's) tree, before the scratch home
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

# ---- Appearance: pick_style's cases on the theme zzvision (style realaero, frames zzglass, frames_classic zzglass)
if [ -f "$jade" ]; then
    mkdir -p "$T/win/index/styles" "$T/win/index/frames/zzglass" "$T/win/index/frames/zzpatch" "$T/home/.config/lhde/themes"
    for n in realaero zzpatch zzbare; do cp "$jade" "$T/win/index/styles/$n.msstyles"; done   # copies: one file under two names is one style
    printf 'mode = msstyles\n' > "$T/win/index/frames/zzglass/theme.ini"
    # zzpatch's frame set as lhde-import's style_recipes makes it (generated/frames/NAME: realaero's theme.ini, a manifest)
    printf 'mode = dwm\nmsstyles = styles/zzpatch.msstyles\n' > "$T/win/index/frames/zzpatch/theme.ini"
    printf '[recipe]\nkind = composited\nargs = zzpatch realaero\n' > "$T/win/index/frames/zzpatch/manifest.ini"
    vision() {   # STYLE: the theme file naming the style so
        printf '[Theme]\nname = ZZ vision\nbuild = 4074\n[VisualStyle]\nstyle = %s\ncolor = Default\nsize = Normal\n[Glass]\nframes = zzglass\nframes_classic = zzglass\n' \
            "$1" > "$T/home/.config/lhde/themes/zzvision.theme"
        printf '[theme]\nname = zzvision\n[current]\ntheme = zzvision\n' > "$T/home/.config/lhde/settings.ini"
    }
    ap() { "$B" --list appearance "$@" | tr '\n' '|'; }
    vision realaero
    check "appearance: the theme's own" "style realaero|frames zzglass|frames_classic zzglass|" "$(ap)"
    check "appearance: classic" \
        "style classic|frames none|frames_classic none|layer Glass.frames = none|layer Glass.frames_classic = none|layer VisualStyle.style = classic|" \
        "$(ap --pick classic | sed 's/layer VisualStyle\.s[ci][az]e = [^|]*|//g')"   # scheme and size: the classic defaults
    check "appearance: a bare style with a frame set of its own" \
        "style zzpatch|frames zzpatch|frames_classic none|layer Glass.frames = zzpatch|layer Glass.frames_classic = none|layer VisualStyle.style = zzpatch|" \
        "$(ap --pick zzpatch)"
    check "appearance: a bare style without one" \
        "style zzbare|frames none|frames_classic none|layer Glass.frames = none|layer Glass.frames_classic = none|layer VisualStyle.style = zzbare|" \
        "$(ap --pick zzbare)"
    check "appearance: back to the theme's own, the layer clean" "style realaero|frames zzglass|frames_classic zzglass|" \
        "$(ap --pick zzbare --pick realaero)"
    check "appearance: back from classic" "style realaero|frames zzglass|frames_classic zzglass|" "$(ap --pick classic --pick realaero)"
    vision aero-patch   # an older theme file: the style's old name, resolved as realaero
    check "appearance: the theme names aero-patch" "style realaero|frames zzglass|frames_classic zzglass|" "$(ap)"
    check "appearance: aero-patch theme, back to its own" "style realaero|frames zzglass|frames_classic zzglass|" \
        "$(ap --pick zzpatch --pick realaero)"
else
    echo "skip appearance: no jade4074.msstyles imported"
fi
[ $fails = 0 ] && echo "settings-lists: all passed"
exit $fails
