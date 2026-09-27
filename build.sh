#!/bin/sh
# build.sh - build ARM-DOS from source into public_html/, a static web site that can be
# copied to any web server (also into a sub-directory such as /armdos/).
#
#   ./build.sh            check the tools, fetch the third-party data, build everything,
#                         stage the site into public_html/
#   ./build.sh --no-fetch skip the download step (3rdparty/ must already be complete)
#   JOBS=4 ./build.sh     limit parallel compile jobs (default: all CPU cores)
#
#   make clean            delete the build outputs (build/)
#   make distclean        delete everything the build and the fetch step made (build/,
#                         public_html/, the downloads in 3rdparty/): back to a fresh checkout
#
# What it does:
#   1. checks that the needed tools are installed (and says how to install them)
#   2. tools/fetch-3rdparty.sh: downloads the shareware/freeware game data, music and
#      the MIDI sound set listed in 3rdparty/manifest.json into 3rdparty/ (hash-checked)
#   3. make: BIOS, kernel, COMMAND.COM, SDK, all programs, disk images, CD-ROM -> build/
#   4. web/tools/build-site.mjs: the play page, the emulator, the manual (web/docs) and
#      the gzipped disk images -> public_html/ (emptied first)
set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
FETCH=1
for a in "$@"; do
  case "$a" in
    --no-fetch) FETCH=0 ;;
    -h|--help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
    *) echo "build.sh: unknown option $a (try --help)" >&2; exit 2 ;;
  esac
done

say() { printf '\n==> %s\n' "$*"; }
OS=$(uname -s)
missing=""
need() {  # need <what> <debian package(s)> <homebrew formula/cask>
  missing="$missing
  - $1
      Debian/Ubuntu: sudo apt install $2
      macOS (Homebrew): $3"
}

say "Checking the tools"
# GNU make (macOS ships an old one as "make"; Homebrew's is "gmake")
MAKE=make
if [ "$OS" = Darwin ] && command -v gmake >/dev/null 2>&1; then MAKE=gmake; fi
if ! $MAKE --version 2>/dev/null | grep -q 'GNU Make [4-9]'; then
  need "GNU make 4 or newer" "make" "brew install make   (then it is used as gmake)"
fi
command -v cc >/dev/null 2>&1 || need "a C compiler for the host (cc)" "build-essential" "xcode-select --install"
if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  need "the ARM cross compiler (arm-none-eabi-gcc) with newlib" "gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib" \
       "brew install --cask gcc-arm-embedded"
else
  case "$(arm-none-eabi-gcc -print-file-name=libc.a)" in
    */*) ;;
    *) need "newlib for arm-none-eabi-gcc (libc.a not found)" "libnewlib-arm-none-eabi" "brew install --cask gcc-arm-embedded" ;;
  esac
  if ! command -v arm-none-eabi-g++ >/dev/null 2>&1; then
    need "the ARM C++ compiler (arm-none-eabi-g++)" "g++-arm-none-eabi libstdc++-arm-none-eabi-newlib" "brew install --cask gcc-arm-embedded"
  else
    case "$(arm-none-eabi-g++ -print-file-name=libstdc++.a)" in
      */*) ;;
      *) need "the C++ library for arm-none-eabi-g++ (libstdc++.a not found)" "libstdc++-arm-none-eabi-newlib" "brew install --cask gcc-arm-embedded" ;;
    esac
  fi
fi
command -v nasm >/dev/null 2>&1 || need "NASM (the x86 assembler)" "nasm" "brew install nasm"
if command -v node >/dev/null 2>&1; then
  NODE_MAJOR=$(node -p 'process.versions.node.split(".")[0]')
  [ "$NODE_MAJOR" -ge 20 ] || need "Node.js 20 or newer (found $(node --version))" "nodejs   (if your release has an older one: https://nodejs.org/ or https://github.com/nodesource/distributions)" "brew install node"
else
  need "Node.js 20 or newer" "nodejs   (if your release has an older one: https://nodejs.org/ or https://github.com/nodesource/distributions)" "brew install node"
fi
command -v python3 >/dev/null 2>&1 || need "Python 3" "python3" "brew install python"
if command -v ffmpeg >/dev/null 2>&1; then
  ENC=$(ffmpeg -hide_banner -encoders 2>/dev/null)
  echo "$ENC" | grep -q libopus && echo "$ENC" | grep -q libmp3lame || need "ffmpeg with the libopus and libmp3lame encoders (for the CD-ROM's audio tracks)" "ffmpeg" "brew install ffmpeg"
else
  need "ffmpeg (for the CD-ROM's audio tracks)" "ffmpeg" "brew install ffmpeg"
fi
if [ -n "$missing" ]; then
  echo "Some tools are missing. Please install:$missing" >&2
  echo >&2
  echo "Then run ./build.sh again." >&2
  exit 1
fi
echo "all tools found ($(arm-none-eabi-gcc -dumpversion 2>/dev/null) arm-none-eabi-gcc, node $(node --version), $($MAKE --version | head -1))"

if [ "$FETCH" = 1 ]; then
  say "Fetching the third-party data (3rdparty/)"
  sh tools/fetch-3rdparty.sh
else
  say "Checking the third-party data (3rdparty/)"
  sh tools/fetch-3rdparty.sh --check
fi

if [ -z "$JOBS" ]; then
  JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
fi
say "Building (make -j$JOBS; this takes a few minutes)"
$MAKE -j"$JOBS" all

say "Staging the web site into public_html/"
rm -rf public_html
node web/tools/build-site.mjs --out public_html

SIZE=$(du -sh public_html | cut -f1)
say "Done"
cat <<EOF
The site is in $ROOT/public_html ($SIZE). It is plain static files with relative URLs:
copy the contents of public_html/ to any web server directory, e.g.
    rsync -a --delete public_html/ you@example.org:/var/www/html/armdos/
and open https://example.org/armdos/ (the play page is index.html, the manual docs/).

To try it here:
    python3 web/tests/serve.py public_html --port 8000
and open http://127.0.0.1:8000/armdos/ in a browser.
EOF
