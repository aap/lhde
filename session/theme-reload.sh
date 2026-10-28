#!/bin/sh
# Reload the current theme everywhere without restarting anything (docs/USAGE.md "Live-editing a theme"):
#   luxshell: root property _LHSHELL_COMMAND = "reload" -> re-reads the pack's shell.lua, tiles/*.lua, the start panel data,
#             changed images, and rebuilds the bars and tiles in place (runtime.lua lh.reload_pack)
#   luxwm:    `luxwmc reload-theme`; when luxwmc is missing or does not know the command, the root ClientMessage _LHWM_RELOAD
#             (python3-xlib) instead. A missing luxwm command is not an error. Both resolve the theme again through
#             liblhde (theme files edited: the new record), and the compatibility registry for flxp is rewritten.
# Usage: session/theme-reload.sh [DISPLAY]   (default $DISPLAY, else :1)
SELF=$(readlink -f "$0"); . "$(dirname "$SELF")/lhde-paths.sh"; lhde_self "$SELF"; lhde_setup_paths
D=${1:-${DISPLAY:-:1}}
export DISPLAY="$D"
if ! xprop -root -f _LHSHELL_COMMAND 8u -set _LHSHELL_COMMAND reload 2>/dev/null; then
  echo "theme-reload: cannot reach display $D" >&2; exit 1
fi
LUXWMC=$(lhde_bin luxwmc)
if [ -n "$LUXWMC" ] && "$LUXWMC" reload-theme >/dev/null 2>&1; then
  echo "theme-reload: luxshell + luxwm (luxwmc reload-theme) on $D"
elif command -v python3 >/dev/null 2>&1 && python3 -I - "$D" >/dev/null 2>&1 <<'PY'
import sys
from Xlib import X, display, protocol
d = display.Display(sys.argv[1]); r = d.screen().root
ev = protocol.event.ClientMessage(window=r, client_type=d.intern_atom("_LHWM_RELOAD"), data=(32, [0, 0, 0, 0, 0]))
r.send_event(ev, event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask); d.flush()
PY
then
  echo "theme-reload: luxshell + luxwm (_LHWM_RELOAD) on $D"
else
  echo "theme-reload: luxshell on $D (no way to reach luxwm: neither luxwmc reload-theme nor python3-xlib)"
fi
