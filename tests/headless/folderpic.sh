#!/bin/sh
# The task band's picture of a folder with pictures is the Icons view's square (tests/w32/folderpictest), on Xvfb with a
# scratch home (its thumbnail cache too), once with the default theme (the folder icon with the pictures on it) and once
# under jade4074 (4074's photo in the open folder where its resources are imported). Usage: tests/headless/folderpic.sh [:DISPLAY]
D=${1:-:77}
T=$(mktemp -d)
B=$(cd "$(dirname "$0")/../.." && pwd)/build/tests/w32/folderpictest
mkdir -p $T/home/.config
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config XDG_DATA_HOME=$T/home/.local/share XDG_STATE_HOME=$T/home/.local/state \
       XDG_CACHE_HOME=$T/home/.cache DISPLAY=$D TMPDIR=$T   # the scratch home only: nothing exported from outside leaks in
. "$(dirname "$0")/wait.sh"
start_xvfb $D -screen 0 1024x768x24 -nolisten tcp || { echo "FAIL Xvfb $D did not start"; stop_xvfb; exit 1; }
timeout 60 "$B"; rc=$?
# 4074's photo in the open folder, where the imported tree has the build (jade4074's resources)
timeout 60 "$B" jade4074 || rc=1
stop_xvfb
rm -rf $T
[ $rc = 0 ] && echo PASS || { echo FAIL; exit 1; }
