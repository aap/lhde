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
	f=${FREE_PACK_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/lhde/lhde-resources-free-1.zip}
	[ -f "$f" ] && iso=$f
fi
if [ -z "$iso" ] || [ ! -f "$iso" ]; then
	echo "skip  free baseline: no free pack or ReactOS ISO (make fetch-free-pack, FREE_PACK=FILE, LHDE_REACTOS_ISO=FILE, or ReactOS-*.iso in ~/lib/windows_isos)"
	exit 0
fi
W=$B/windows
walls=
case "$iso" in *.iso|*.ISO) [ -n "${FREE_WALLPAPERS:-}" ] && [ -d "$FREE_WALLPAPERS" ] && walls=$FREE_WALLPAPERS;; esac
key="$iso $(stat -c %s.%Y "$iso") $walls"
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
