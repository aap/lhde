#!/bin/sh
# The list view's tile view and hidden labels (tests/w32/lvtiles.cpp) on an Xvfb display.
# Usage: tests/headless/lvtiles.sh [:DISPLAY]
D=${1:-:74}
B=$(cd "$(dirname "$0")/../.." && pwd)/build/tests/w32/lvtiles
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
DISPLAY=$D "$B"; st=$?
stop_xvfb
exit $st
