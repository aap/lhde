#!/bin/sh
# make check-bare: the whole make check on a machine with nothing imported and nothing configured, what a CI job runs. Fresh
# empty directories stand for the Windows tree (LHDE_WINDOWS), the config, data, state and cache homes and HOME; no LHDE_DATA,
# LHDE_STYLES, ReactOS ISO or free pack. Every test must pass or skip with a message: one that fails for lack of resources is a
# bug of that test. The exit status is make check's; a FAIL line in its output fails too.
cd "$(dirname "$0")/.." || exit 1
B=$(mktemp -d "${TMPDIR:-/tmp}/lhde-check-bare.XXXXXX") || exit 1
trap 'rm -rf "$B"' EXIT
mkdir -p "$B/windows" "$B/home/.config" "$B/home/.local/share" "$B/home/.local/state" "$B/home/.cache" "$B/runtime"
chmod 700 "$B/runtime"
unset LHDE_DATA LHDE_STYLES LHDE_REACTOS_ISO FREE_PACK FREE_PACK_CACHE FREE_WALLPAPERS LUXWM_BASELINE FRAMETEST_FONTS XDG_DATA_DIRS XDG_CONFIG_DIRS
export LHDE_WINDOWS="$B/windows" HOME="$B/home" XDG_CONFIG_HOME="$B/home/.config" XDG_DATA_HOME="$B/home/.local/share" \
       XDG_STATE_HOME="$B/home/.local/state" XDG_CACHE_HOME="$B/home/.cache" XDG_RUNTIME_DIR="$B/runtime"
make --no-print-directory check > "$B/check.log" 2>&1
status=$?
cat "$B/check.log"
if [ $status = 0 ] && grep -q '^FAIL' "$B/check.log"; then status=1; fi
[ $status = 0 ] && echo "check-bare: every test passed or skipped" || echo "check-bare: failed (status $status)"
exit $status
