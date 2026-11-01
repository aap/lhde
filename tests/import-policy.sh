#!/bin/sh
# lhde-import's overwrite policy (docs/USAGE.md "Getting the Windows resources"): without --force a file that is in the
# Windows tree is kept, whatever it holds; with --force it is written again. On a fresh tree in a scratch directory, with
# a stand-in file of our own (no Microsoft file: lhde-import --style copies whatever it is given) and a Shell/ beside it.
cd "$(dirname "$0")/.." || exit 1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
want() {   # WHAT EXPECTED GOT
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$2] got [$3]"; fails=$((fails + 1)); fi
}
mkdir -p "$T/in/Shell/NormalColor" "$T/w"
printf 'a stand-in style, version 1\n' > "$T/in/zz.msstyles"
printf 'shell 1\n' > "$T/in/Shell/NormalColor/shellstyle.dll"
imp() { tools/lhde-import --windows "$T/w" --no-annotate --style "$T/in/zz.msstyles" zzpolicy "$@" 2>&1 | grep 'copied'; }
D=$T/w/local/themes/zzpolicy.msstyles
want "first import" "lhde: local/themes/zzpolicy.msstyles (2 copied, 0 kept)" "$(imp)"
want "copied" "$(cat "$T/in/zz.msstyles")" "$(cat "$D")"
# the tree's copy changed in place and resized; the source changed too, to the same size as the old copy
printf 'changed in place, longer than before\n' > "$D"
printf 'shell 2\n' > "$T/in/Shell/NormalColor/shellstyle.dll"
want "again: kept" "lhde: local/themes/zzpolicy.msstyles (0 copied, 2 kept)" "$(imp)"
want "kept as it was" "changed in place, longer than before" "$(cat "$D")"
want "shell kept" "shell 1" "$(cat "$T/w/local/themes/zzpolicy/Shell/NormalColor/shellstyle.dll")"
want "--force: replaced" "lhde: local/themes/zzpolicy.msstyles (2 copied, 0 kept)" "$(imp --force)"
want "replaced" "$(cat "$T/in/zz.msstyles")" "$(cat "$D")"
want "shell replaced" "shell 2" "$(cat "$T/w/local/themes/zzpolicy/Shell/NormalColor/shellstyle.dll")"
want "idempotent" "lhde: local/themes/zzpolicy.msstyles (0 copied, 2 kept)" "$(imp)"
echo "$fails failed"
exit $fails
