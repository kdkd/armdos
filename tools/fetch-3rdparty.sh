#!/bin/sh
# tools/fetch-3rdparty.sh - download and verify the third-party data ARM-DOS's disks
# carry but the repository does not (see 3rdparty/manifest.json for the list, the
# sources and the licences). Safe to run again: what is already there and correct is
# kept, nothing is downloaded twice.
#
#   tools/fetch-3rdparty.sh           fetch what is missing
#   tools/fetch-3rdparty.sh --check   only check
#   tools/fetch-3rdparty.sh --list    show the items, sources and licences
set -e
cd "$(dirname "$0")/.."
if ! command -v node >/dev/null 2>&1; then
  echo "fetch-3rdparty: Node.js is needed (https://nodejs.org/, version 20 or newer)." >&2
  exit 1
fi
exec node tools/fetch-3rdparty.mjs "$@"
