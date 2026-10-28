#!/bin/bash
# Watch the theme files and reload luxshell + luxwm on every save (docs/USAGE.md "Live-editing a theme"): themes.ini,
# wallpapers.ini, frames/*/theme.ini and nav.ini (luxnav's tuning), packs/*/shell.lua in the data directories (the source tree's
# data/ and the user's data directory), and luxshell's tiles/*.lua. Each change (a burst of saves within 0.3 s counts once) runs
# session/theme-reload.sh on the display. inotifywait (inotify-tools) when installed (directories are watched, so editors that
# replace the file are seen), else a 1 s mtime/size poll. Ctrl-C stops it.
# Usage: session/theme-watch.sh [DISPLAY]   (default $DISPLAY, else :1)
SELF=$(readlink -f "$0"); . "$(dirname "$SELF")/lhde-paths.sh"; lhde_self "$SELF"; lhde_setup_paths
D=${1:-${DISPLAY:-:1}}
RELOAD="$LHDE_SESSION_DIR/theme-reload.sh"
LUA=${LHDE_TOP:+$LHDE_TOP/luxshell}; LUA=${LUA:-$LHDE_SHARE/luxshell}
mapfile -t DIRS < <(lhde_data_dirs; echo "$LUA")
PAT='(/themes\.ini|/wallpapers\.ini|/frames/[^/]+/(theme|nav)\.ini|/packs/[^/]+/shell\.lua|/packs/startpanels\.lua|/tiles/[^/]+\.lua)$'
files() { for d in "${DIRS[@]}"; do ls -d "$d/themes.ini" "$d/wallpapers.ini" "$d"/frames/*/theme.ini "$d"/frames/*/nav.ini "$d"/packs/*/shell.lua "$d"/packs/startpanels.lua "$d"/tiles/*.lua; done 2>/dev/null; }
reload() { echo "theme-watch: $1 changed"; "$RELOAD" "$D"; }
echo "theme-watch: $(files | wc -l) files on display $D (${DIRS[*]})"
if command -v inotifywait >/dev/null 2>&1; then
  echo "theme-watch: inotifywait"
  inotifywait -m -q -r -e close_write,moved_to,create --format "%w%f" "${DIRS[@]}" 2>/dev/null |   # filtered by $PAT
  while IFS= read -r f; do
    [[ "$f" =~ $PAT ]] || continue
    while IFS= read -r -t 0.3 more; do :; done   # the rest of the burst (editor temp files, a second write)
    reload "$f"
  done
else
  echo "theme-watch: no inotifywait, polling every second"
  sig() { files | while IFS= read -r f; do stat -c '%n %Y %s' "$f" 2>/dev/null; done; }
  old=$(sig)
  while sleep 1; do
    new=$(sig)
    if [ "$new" != "$old" ]; then
      what=$(diff <(echo "$old") <(echo "$new") | sed -n 's/^> \([^ ]*\) .*/\1/p' | head -1)
      old=$new; reload "$what"
    fi
  done
fi
