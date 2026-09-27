#!/bin/sh
# apps/hexen2/tools/build-progs.sh - rebuild data/DATA1/PROGS.DAT (the Hexen II
# game code, HexenC, GPL-2) from gamecode/h2/ with uHexen2's hcc compiler.
#
#   sh apps/hexen2/tools/build-progs.sh [path/to/hcc]      (or HCC=path/to/hcc)
#
# hcc: "make -C utils/hcc" in a uHexen2 checkout
# (https://git.code.sf.net/p/uhexen2/uhexen2, commit 475c048b). Optional: the
# build uses the committed PROGS.DAT; the result is byte-identical to it.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
hcc=${1:-${HCC:-}}
if [ -z "$hcc" ] || [ ! -x "$hcc" ]; then
  echo "build-progs.sh: skipped - give the path of uHexen2's hcc (argument or HCC=...)" >&2
  exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$here"/gamecode/h2/* "$tmp"/
(cd "$tmp" && "$hcc" -os >/dev/null)
cp "$tmp"/progs.dat "$here"/data/DATA1/PROGS.DAT
sha1sum "$here"/data/DATA1/PROGS.DAT
