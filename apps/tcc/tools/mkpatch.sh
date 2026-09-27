#!/bin/sh
# apps/tcc/tools/mkpatch.sh [UPSTREAM] - write apps/tcc/tinycc-armdos.patch:
# every change made to TinyCC (src/ against a clone of upstream mob 3dc99db,
# https://repo.or.cz/tinycc.git; the argument or TINYCC_SRC names the clone).
UP=${1:-${TINYCC_SRC:-}}
if [ -z "$UP" ] || [ ! -d "$UP" ]; then
  echo "mkpatch.sh: give the upstream TinyCC clone (argument or TINYCC_SRC=...)" >&2
  exit 1
fi
cd "$(dirname "$0")/../src" || exit 1
OUT=../tinycc-armdos.patch
{
  echo "TinyCC for ARM-DOS: changes against tinycc mob 3dc99dbc82f8e07308c5d398136803e62f9676df"
  echo "(src/config.h is new; see apps/tcc/README.md for what and why)"
  echo
  for f in *.c *.h stab.def include/*.h; do
    [ -f "$UP/$f" ] && diff -u --label "a/$f" --label "b/$f" "$UP/$f" "$f"
  done
} > $OUT
echo "$OUT: $(grep -c '^@@' $OUT) hunks in $(grep -c '^--- a/' $OUT) files"
