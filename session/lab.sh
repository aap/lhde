#!/bin/sh
# lhde's headless test bench (was the x11 lab's lab.sh, d353b69): a display of its own (LAB=:2 by default; Xvfb, nothing appears on
# your screen, or a visible Xephyr) with luxwm, picom and the wallpaper, for screenshots and scripted tests. lhde-session is the
# real session; this is the bench. Programs come from the build tree when it runs from one (build/bin, build/examples), else from
# beside the install; the art from lhde's data directories (LHDE_WINDOWS and LHDE_DATA honoured, as by every lhde program).
#   LAB_WM=fvwm for an fvwm2 session (lab.fvwm2rc), default luxwm; LAB_THEME=NAME a theme (lhde-theme list; aliases such as aero-patch work: jade4074,
#   slate4074, slate4051, luna-xp, aero5048, aero5112, aerovision, ...); LAB_PICOM=0 without a compositor (luxwm then uses the
#   theme's frame_classic); LAB_WMARGS="" restarts luxwm without -d (debug log); LAB_BACKEND=glx for picom's glx backend.
#   session/lab.sh start [xephyr]   start Xvfb (or a visible Xephyr window on $DISPLAY) on $LAB with luxwm, picom, wallpaper
#   session/lab.sh shell [on|off]   start (restart) / stop luxshell on $LAB (LAB_SHELL_DESKTOP=0: without its desktop layer)
#   session/lab.sh demo             open two terminals ($LAB_TERM, default xterm) and the glassdemo window on $LAB
#   session/lab.sh shot [file.png]  screenshot $LAB
#   session/lab.sh theme NAME       switch the session theme (root _LH_THEME): luxwm, luxshell and clients follow live
#   session/lab.sh restart-wm | restart-picom
#   session/lab.sh stop             kill everything on $LAB (only processes whose DISPLAY is $LAB, and its X server)
SELF=$(readlink -f "$0"); . "$(dirname "$SELF")/lhde-paths.sh"; lhde_self "$SELF"; lhde_setup_paths
LAB=${LAB:-:2}; LOG=${TMPDIR:-/tmp}/lhdelab$LAB
mkdir -p "$LOG"
LUXWM=$(lhde_bin luxwm) LUXSHELL=$(lhde_bin luxshell)
# Xvfb's software GLX does not refresh ARGB window textures, so the headless lab uses the xrender backend
# (gaussian blur, size 5 / deviation 1.2 = the glass shader's ~1.2 px sigma); on real hardware (lhde-session) the glx backend is right.
startpicom() {
  if [ "${LAB_PICOM:-1}" = 0 ]; then echo "LAB_PICOM=0: not starting picom on $LAB (DWM themes fall back to their frame_classic)"; return; fi
  PCONF="$LHDE_SESSION_DIR/picom-glass.conf"; [ "${LAB_WM:-luxwm}" = fvwm ] && PCONF="$LHDE_SESSION_DIR/picom-glass-fvwm.conf"
  if [ "${LAB_BACKEND:-xrender}" = glx ]; then (DISPLAY=$LAB setsid picom --config "$PCONF" --backend glx >"$LOG/picom.log" 2>&1 < /dev/null &)
  else (DISPLAY=$LAB setsid picom --config "$PCONF" --backend xrender --blur-method gaussian --blur-size 5 --blur-deviation 1.2 >"$LOG/picom.log" 2>&1 < /dev/null &); fi
}
# a theme name (an alias, a bare style) through liblhde: prints the theme's name, nothing when unknown
themename() { lhde_themename "$1"; }
# what luxwm gets as LHWM_THEME: the registry name, or a frame set that is not in the registry. luxwm only uses it while the
# display has no _LH_THEME yet (the session's setting wins; `theme` / LAB_THEME on restart-wm change that)
wmtheme() { n=$(themename "${LAB_THEME:-aero-patch}"); if [ -n "$n" ]; then echo "$n"; else lhde_resolve "frames/$LAB_THEME"; fi; }
killlab() { for pid in $(pgrep -x "$1"); do tr '\0' '\n' < /proc/$pid/environ 2>/dev/null | grep -qx "DISPLAY=$LAB" && kill "$pid"; done; }
startwm() {
  if [ "${LAB_WM:-luxwm}" = fvwm ]; then (DISPLAY=$LAB fvwm2 -f "$LHDE_SESSION_DIR/lab.fvwm2rc" >"$LOG/fvwm.log" 2>&1 &)
  else (cd "$HOME" && DISPLAY=$LAB LHWM_THEME="$(wmtheme)" setsid "$LUXWM" ${LAB_WMARGS--d} >"$LOG/luxwm.log" 2>&1 < /dev/null &); fi
}
case "$1" in
start)
  if [ "$2" = xephyr ]; then (Xephyr $LAB -screen 1280x800 -title "lhde lab $LAB" +extension GLX -ac -br -noreset >"$LOG/x.log" 2>&1 &)
  else setsid sh -c "trap '' TERM HUP INT; exec Xvfb $LAB -screen 0 1280x800x24 +extension GLX +render -noreset -nolisten tcp" >>"$LOG/x.log" 2>&1 < /dev/null & fi   # ignores TERM/HUP: only 'stop' (kill -9) ends it
  sleep 2; export DISPLAY=$LAB
  wp=$(lhde_find wallpapers/mock/longhorn.jpg); [ -n "$wp" ] && feh --no-fehbg --bg-fill "$wp" 2>/dev/null
  startwm; sleep 2
  startpicom; sleep 2
  if [ "${LAB_PICOM:-1}" = 0 ]; then echo "lab up on $LAB (no compositor)"
  else killlab_check=$(for p in $(pgrep -x picom); do tr '\0' '\n' </proc/$p/environ | grep -qx "DISPLAY=$LAB" && echo $p; done)
    [ -n "$killlab_check" ] && echo "lab up on $LAB (picom ${LAB_BACKEND:-xrender})" || { echo "picom failed:"; tail -3 "$LOG/picom.log"; }; fi ;;
ensure) if ! DISPLAY=$LAB xdpyinfo >/dev/null 2>&1; then "$0" start; else echo "lab already up on $LAB"; fi ;;
shell)
  killlab luxshell
  [ "$2" = off ] && { echo "luxshell stopped on $LAB"; exit 0; }
  (cd "$HOME" && DISPLAY=$LAB LHSHELL_DESKTOP=${LAB_SHELL_DESKTOP:-1} setsid "$LUXSHELL" >"$LOG/luxshell.log" 2>&1 < /dev/null &)
  echo "luxshell on $LAB (log $LOG/luxshell.log)" ;;
demo)
  export DISPLAY=$LAB; t=${LAB_TERM:-xterm}
  ($t -geometry 70x18+40+60 -title Terminal >/dev/null 2>&1 &); ($t -geometry 60x14+120+330 -title "Another window" >/dev/null 2>&1 &)
  ("$(lhde_bin glassdemo)" >/dev/null 2>&1 &); sleep 2; w=$(xdotool search --class glassdemo | head -1); [ -n "$w" ] && xdotool windowmove "$w" 560 120 ;;
shot) DISPLAY=$LAB import -window root "${2:-$LOG/shot.png}" && echo "${2:-$LOG/shot.png}" ;;
restart-wm) killlab fvwm2; killlab luxwm; sleep 0.5
  # an explicit LAB_THEME becomes the session theme (otherwise the running session's _LH_THEME stays)
  if [ -n "$LAB_THEME" ] && n=$(themename "$LAB_THEME") && [ -n "$n" ]; then DISPLAY=$LAB xprop -root -f _LH_THEME 8u -set _LH_THEME "$n"; fi
  startwm; echo "wm restarted on $LAB" ;;
theme) n=$(themename "$2"); [ -n "$n" ] || { echo "no such theme; have: $(lhde_themes)"; exit 1; }
  DISPLAY=$LAB xprop -root -f _LH_THEME 8u -set _LH_THEME "$n" && echo "theme $n on $LAB" ;;
restart-picom) if [ "${LAB_PICOM:-1}" = 0 ]; then startpicom; else killlab picom; sleep 0.5; startpicom; echo "picom restarted on $LAB"; fi ;;
stop)
  for n in picom fvwm2 glassdemo navdemo extendframe xterm luxshell luxwm flxp-desktop flxp-taskbar flxp-startmenu flxp flxp-settings flxp-filedialog; do killlab $n; done
  for p in $(pgrep -x Xvfb) $(pgrep -x Xephyr); do tr '\0' '\n' </proc/$p/cmdline | grep -qx -- "$LAB" && kill -9 $p; done; echo "lab stopped" ;;
*) echo "usage: $0 start [xephyr] | ensure | shell [on|off] | demo | shot [file] | theme NAME | restart-wm | restart-picom | stop" ;;
esac
