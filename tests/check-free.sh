#!/bin/sh
# make check-free (part of make check): the free baseline (docs/THEME-FORMAT.md section 6) as a test configuration. A Windows tree
# that holds only the reactos source, imported once into build/free/windows from ReactOS's boot CD ($LHDE_REACTOS_ISO, else the
# newest ReactOS-*.iso in ~/lib/windows_isos; skipped with a note when there is none, as ut-dump's test skips without styles) and
# again when the ISO or share/sources/reactos.ini changes. With that tree alone (own config, data and state homes, no LHDE_DATA):
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
iso=${LHDE_REACTOS_ISO:-}
if [ -z "$iso" ]; then
	for f in "$HOME"/lib/windows_isos/ReactOS-*.iso; do [ -f "$f" ] && iso=$f; done
fi
if [ -z "$iso" ] || [ ! -f "$iso" ]; then
	echo "skip  free baseline: no ReactOS ISO (LHDE_REACTOS_ISO=FILE, or ReactOS-*.iso in ~/lib/windows_isos; make install-resources-free names the download)"
	exit 0
fi
W=$B/windows
key="$iso $(stat -c %s.%Y "$iso")"
if [ ! -f "$W/reactos/source.ini" ] || [ "$(cat "$B/iso" 2>/dev/null)" != "$key" ] || [ share/sources/reactos.ini -nt "$W/reactos/source.ini" ]; then
	rm -rf "$W"; mkdir -p "$B"
	if ! tools/lhde-import --iso "$iso" --source reactos --windows "$W" --no-annotate > "$B/import.log" 2>&1; then
		cat "$B/import.log"; echo "FAIL  free baseline: lhde-import --iso $iso"; exit 1
	fi
	echo "$key" > "$B/iso"
fi
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
	elif grep -qi '^build *= *reactos' "$f"; then bad=$(grep ' missing' "$B/explain-$t.txt" || true); fi
	if [ -n "$bad" ]; then echo "FAIL  free baseline: $t is not complete with only ReactOS imported:"; echo "$bad"; fails=$((fails + 1)); fi
done
[ $fails -eq 0 ] || { echo "FAIL  free baseline: $fails of $n themes"; exit 1; }
echo "ok    free baseline: $n themes resolve with only reactos imported ($W)"
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
