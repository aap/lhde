#!/bin/sh
# Compile Wine .rc files into .res with mingw windres. Wine's wrc strips the
# "#msgctxt#...#" translation prefixes; we do the same with sed first.
# usage: rc/build.sh <wine-dll-dir> <name> [extra windres args]
set -e
DIR=$1; NAME=$2; shift 2
OUT=$(dirname "$0")/$NAME.res
TMP=$(mktemp -d)
sed 's/#msgctxt#[^#]*#//g' "$DIR/$NAME.rc" > "$TMP/$NAME.rc"
x86_64-w64-mingw32-windres -O res -I "$DIR" -I "$TMP" "$@" -o "$OUT" "$TMP/$NAME.rc"
rm -rf "$TMP"
echo "wrote $OUT"
