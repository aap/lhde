#!/bin/sh
# The list view's tile view and hidden labels (tests/w32/lvtiles.cpp) on an Xvfb display.
# Usage: tests/headless/lvtiles.sh [:DISPLAY]
D=${1:-:74}
B=$(cd "$(dirname "$0")/../.." && pwd)/build/tests/w32/lvtiles
Xvfb $D -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
sleep 1
DISPLAY=$D "$B"; st=$?
kill $XP 2>/dev/null
exit $st
