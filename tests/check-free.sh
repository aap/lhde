#!/bin/sh
# make check-free (part of make check): the free baseline (docs/THEME-FORMAT.md section 6) as a test configuration. A Windows tree
# that holds only the free pack's sources (reactos, and free: the free themes' photos), imported once into build/free/windows
# from the free pack or ReactOS's boot CD, the first there of: $FREE_PACK (make free-pack's), $LHDE_REACTOS_ISO, the newest
# ReactOS-*.iso in ~/lib/windows_isos, the pack make fetch-free-pack downloaded ($FREE_PACK_CACHE, else ~/.cache/lhde/); skipped
# with a note when there is none, as ut-dump's test skips without styles; imported again when that file or
# share/sources/reactos.ini or free.ini changes. From the ISO the photos come from $FREE_WALLPAPERS (make passes it) when that
# directory is there, and the pack and the ISO import the same tree; without it the free themes' wallpaper is missing, which
# is then reported, not failed. With that tree alone (own config, data and state homes, no LHDE_DATA):
#   - every shipped theme resolves: lhde-theme explain and explain --files succeed;
#   - classic and the themes of build reactos resolve with nothing missing (classic: but its colour scheme, which ReactOS does
#     not carry: the built-in Windows Standard);
#   - every icon key of the icon set recipes (share/icons/*.theme) and every key the class map (share/icons/apps.ini) names
#     has an image in the free set (lhde-theme icon --set reactos --key KEY: reactos itself, or Wine's for what ReactOS lacks);
#   - lhde-firefox generate writes a userChrome.css for every shipped theme (colours alone where the styles are absent), and
#     Firefox's own CSS parser reports no error in them (tests/firefox-css.py: headless, skipped without firefox);
#   - luxwm's Windows Classic frames render through the frame regression (regress.sh --classic-only, ReactOS's Tahoma and Marlett)
#     against a baseline of their own, build/luxwm/regress/baseline-free.sha256 (written on the first run); whether they equal the
#     normal run's classic renders (whose fonts are the imported build's) is reported, not required.
set -e
top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top"
B=$top/build/free
iso=${FREE_PACK:-${LHDE_REACTOS_ISO:-}}
if [ -z "$iso" ]; then
	for f in "$HOME"/lib/windows_isos/ReactOS-*.iso; do [ -f "$f" ] && iso=$f; done
fi
if [ -z "$iso" ]; then
	f=${FREE_PACK_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/lhde/lhde-resources-free-3.zip}
	[ -f "$f" ] && iso=$f
fi
if [ -z "$iso" ] || [ ! -f "$iso" ]; then
	echo "skip  free baseline: no free pack or ReactOS ISO (make fetch-free-pack, FREE_PACK=FILE, LHDE_REACTOS_ISO=FILE, or ReactOS-*.iso in ~/lib/windows_isos)"
	exit 0
fi
W=$B/windows
walls=
case "$iso" in *.iso|*.ISO) [ -n "${FREE_WALLPAPERS:-}" ] && [ -d "$FREE_WALLPAPERS" ] && walls=$FREE_WALLPAPERS;; esac
key="$iso $(stat -c %s.%Y "$iso" 2>/dev/null || stat -f %z.%m "$iso") $walls"   # GNU stat; BSD stat
if [ ! -f "$W/reactos/source.ini" ] || [ "$(cat "$B/iso" 2>/dev/null)" != "$key" ] || [ share/sources/reactos.ini -nt "$W/reactos/source.ini" ] ||
   [ share/sources/free.ini -nt "$W/reactos/source.ini" ]; then
	rm -rf "$W"; mkdir -p "$B"
	case "$iso" in *.iso|*.ISO) set -- --iso "$iso" --source reactos;; *) set -- --pack "$iso";; esac
	if ! tools/lhde-import "$@" --windows "$W" --no-annotate > "$B/import.log" 2>&1; then
		cat "$B/import.log"; echo "FAIL  free baseline: lhde-import $*"; exit 1
	fi
	if [ -n "$walls" ] && ! tools/lhde-import --dir "$walls" --source free --windows "$W" --no-annotate >> "$B/import.log" 2>&1; then
		cat "$B/import.log"; echo "FAIL  free baseline: lhde-import --dir $walls --source free"; exit 1
	fi
	echo "$key" > "$B/iso"
fi
nowall=
[ -f "$W/free/source.ini" ] || nowall=1
export LHDE_WINDOWS="$W" XDG_CONFIG_HOME="$B/config" XDG_DATA_HOME="$B/data" XDG_STATE_HOME="$B/state"
unset LHDE_DATA
mkdir -p "$B/config" "$B/data" "$B/state"
lt=$top/build/bin/lhde-theme
fails=0 n=0
for f in share/themes/*.theme; do
	t=$(basename "$f" .theme); n=$((n + 1))
	if ! "$lt" explain --files "$t" > "$B/files-$t.txt" 2>&1; then echo "FAIL  free baseline: lhde-theme explain --files $t"; cat "$B/files-$t.txt"; fails=$((fails + 1)); continue; fi
	if ! "$lt" explain "$t" > "$B/explain-$t.txt" 2>&1; then echo "FAIL  free baseline: lhde-theme explain $t"; cat "$B/explain-$t.txt"; fails=$((fails + 1)); continue; fi
	bad=
	if [ "$t" = classic ]; then bad=$(grep ' missing' "$B/explain-$t.txt" | grep -v -e '^colour scheme' -e '-> fallback reactos' || true)
	elif grep -qi '^build *= *reactos' "$f"; then bad=$(grep ' missing' "$B/explain-$t.txt" | { [ -n "$nowall" ] && grep -v '^wallpaper' || cat; } || true); fi
	if [ -n "$bad" ]; then echo "FAIL  free baseline: $t is not complete with only the free pack imported:"; echo "$bad"; fails=$((fails + 1)); fi
done
[ $fails -eq 0 ] || { echo "FAIL  free baseline: $fails of $n themes"; exit 1; }
echo "ok    free baseline: $n themes resolve with only the free pack imported ($W, from $(basename "$iso")${walls:+ and $walls})"
[ -z "$nowall" ] || echo "      (without the free themes' photos: no FREE_WALLPAPERS directory beside the ISO, their wallpaper is missing)"
# lhde-firefox's userChrome.css for every shipped theme from the free tree alone: the Longhorn themes' styles are absent, so
# the CSS uses colours alone (and Firefox 4's pictures, which are lhde's to ship) and must still be written, in every look
# of the Longhorn themes: the default (the Longhorn navigation with glass, Firefox 4's without), and each of longhorn, ff4
# and explorer asked for (docs/USAGE.md "Firefox")
nf=0 nx=0
for f in share/themes/*.theme; do
	t=$(basename "$f" .theme); nf=$((nf + 1)); mkdir -p "$B/firefox/$t"
	if ! python3 -I tools/lhde-firefox generate "$t" -o "$B/firefox/$t/userChrome.css" > "$B/firefox/$t/log" 2>&1 ||
	   ! grep -q -e '--lhde-ff4-frame:' -e '--lhde-xp-face:' "$B/firefox/$t/userChrome.css"; then
		echo "FAIL  free baseline: lhde-firefox generate $t"; cat "$B/firefox/$t/log"; exit 1
	fi
	if grep -q -e '--lhde-ff4-frame:' "$B/firefox/$t/userChrome.css"; then
		if [ ! -f "$B/firefox/$t/lhde-art/ff4/tab.png" ]; then
			echo "FAIL  free baseline: lhde-firefox generate $t: Firefox 4's pictures not copied beside it"; exit 1
		fi
		nx=$((nx + 1))
		for look in longhorn:--lhde-lh-band-h: ff4:--lhde-ff4-tab-h: explorer:--lhde-band-low:; do
			l=${look%%:*} key=${look#*:}; mkdir -p "$B/firefox/$t-$l"
			if ! python3 -I tools/lhde-firefox generate "$t" --look "$l" -o "$B/firefox/$t-$l/userChrome.css" \
			       > "$B/firefox/$t-$l/log" 2>&1 || ! grep -q -e "$key" "$B/firefox/$t-$l/userChrome.css"; then
				echo "FAIL  free baseline: lhde-firefox generate $t --look $l"; cat "$B/firefox/$t-$l/log"; exit 1
			fi
		done
	fi
done
echo "ok    free baseline: lhde-firefox writes a userChrome.css for the $nf themes (and each look of the $nx Longhorn ones)"
# and Firefox's own CSS parser takes every rule of them (one headless Firefox; skipped without firefox)
rc=0; TMPDIR=$B python3 -I tests/firefox-css.py "$B"/firefox/*/userChrome.css > "$B/firefox/css.log" 2>&1 || rc=$?
case $rc in
0) sed 's/^ok    /ok    free baseline: /' "$B/firefox/css.log" ;;
77) echo "skip  free baseline: no firefox to parse lhde-firefox's CSS" ;;
*) echo "FAIL  free baseline: Firefox rejects lhde-firefox's CSS:"; cat "$B/firefox/css.log"; exit 1 ;;
esac
keys=$(sed -n '/^\[icons\]/,/^\[/{s/^\([a-z_0-9]*\) *=.*/\1/p;}' share/icons/*.theme | sort -u)
mapped=$(sed -n '/^\[classes\]/,/^\[/{s/^[^;[][^=]*= *\([a-z_0-9]*\) *$/\1/p;}' share/icons/apps.ini | sort -u)
nk=0 bad=
for k in $keys $mapped; do
	nk=$((nk + 1))
	out=$("$lt" icon --set reactos --key "$k" --size 32 2>&1) || bad="$bad $k"
	case "$out" in *"icons/wine.theme"*|*"icons/reactos.theme"*|none) ;; *) bad="$bad $k($out)";; esac
done
[ -z "$bad" ] || { echo "FAIL  free baseline: icon keys without an image in the free set:$bad"; exit 1; }
echo "ok    free baseline: $nk icon keys (the recipes' and the class map's) have an image in the free set"
R=$top/build/luxwm/regress
st=0
FRAMETEST_FONTS="$W/index/fonts/reactos" LUXWM_BASELINE="$R/baseline-free.sha256" luxwm/tests/regress.sh --classic-only "$R/out-free" \
	> "$B/regress.log" 2>&1 || st=$?
sed 's/^ok    luxwm regress/ok    free baseline: classic frames/; s/^FAIL  luxwm regress/FAIL  free baseline: classic frames/' "$B/regress.log"
[ $st -eq 0 ] || exit 1
if [ -f "$R/out.sha256" ]; then
	if [ "$(grep '  classic' "$R/out.sha256")" = "$(grep '  classic' "$R/out-free.sha256")" ]; then
		echo "      (identical to the normal run's classic renders)"
	else
		echo "      (they differ from the normal run's classic renders: its Tahoma and Marlett are the imported build's)"
	fi
fi
