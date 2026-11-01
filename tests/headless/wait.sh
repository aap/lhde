# Bounded waits for the headless scripts (sourced): poll for what a fixed sleep stood for, so a loaded machine only makes a
# test slower, not fail. Each gives up after its timeout (seconds) and returns 1.
#   wait_for SECS COMMAND...       run COMMAND every 0.1 s until it succeeds
#   start_xvfb DISPLAY ARGS...     wait for an earlier server on DISPLAY to be gone (the previous run's, still exiting), start
#                                  Xvfb there (-noreset; its pid in $XP) and wait until it answers; false when it does not
#   stop_xvfb                      kill it and wait until it has exited, so the next run can have the display
#   wait_window SECS SEARCH...     a visible window matching `xdotool search --onlyvisible SEARCH...`: prints its id
wait_for() {
    _n=$(($1 * 10)); shift
    while [ $_n -gt 0 ]; do
        "$@" && return 0
        sleep .1; _n=$((_n - 1))
    done
    return 1
}
_answers() { DISPLAY=$1 xdotool getdisplaygeometry >/dev/null 2>&1; }
_free() { ! _answers "$1" && [ ! -e "/tmp/.X${1#:}-lock" ]; }
_alive() { kill -0 "$XP" 2>/dev/null; }
_up() { _alive && _answers "$1"; }
start_xvfb() {
    _d=$1; shift
    wait_for 20 _free "$_d" || { echo "note: display $_d is in use by another X server"; return 1; }
    Xvfb "$_d" -noreset "$@" >/dev/null 2>&1 & XP=$!   # no reset when the last client leaves: a client connecting then is refused
    wait_for 20 _up "$_d"
}
stop_xvfb() { kill "$XP" 2>/dev/null; wait "$XP" 2>/dev/null; }
_window() { _w=$(xdotool search --onlyvisible "$@" 2>/dev/null | head -1); [ -n "$_w" ]; }
wait_window() { _s=$1; shift; wait_for "$_s" _window "$@" && echo "$_w"; }
