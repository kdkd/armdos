#!/bin/bash
# dos400run.sh - run commands under the REAL MS-DOS 4.00 (PCjs disk set) in
# DOSBox-X and collect what they wrote: how the expected outputs in
# apps/*/tests/expected/ were made.  Not part of "make test" (needs DOSBox-X,
# nasm, mtools, Xvfb for key input, and the MS-DOS 4.00 reference images in
# $ARMDOS_REFS: msdos400-pcjs/MSDOS400-DISK1.img, dos400-verify/tools/hd-after-tests.img;
# not distributed).
#
#   dos400run.sh WORKDIR RUN.BAT [SECONDS] [HOSTDIR-TO-COPY-TO-C:\...]
#
# Boots DISK1 (CONFIG.SYS: FILES=20 BUFFERS=20, or $CONFIG) with the verified
# 32 MB hard disk image (65 cylinders x 16 heads x 63 sectors) as C:, copies
# RUN.BAT (and the optional host tree) to C:\, runs it from AUTOEXEC.BAT,
# waits SECONDS, then copies C:\OUT\*.* to WORKDIR/out/.
#
# Screens: C:\SCRDUMP.COM FILE (tools/scrdump.asm) saves the text screen as
# 4000 bytes of char/attribute pairs - "SCRDUMP C:\OUT\S1.SCR" in RUN.BAT;
# tools/scr2txt.py makes WORKDIR/out/S1.SCT of it.
#
# Interactive programs (FDISK):  KEYS=file types keys (the verification kit's
# keys.py format: "delay key key ..." / 'delay "text" Return') into DOSBox-X
# on an Xvfb display and keeps a decoded screen per second in WORKDIR/shots/;
# AUTOCMD='A:\FDISK' runs that from AUTOEXEC.BAT instead of C:\RUN.BAT (RUN.BAT
# may then be /dev/null); HD=blank uses an empty, unpartitioned 65-cylinder
# disk as C: instead of the verified one (WORKDIR/hd.img afterwards: KEEP=1).
# Put WORKDIR under build/ (the images are 32 MB each).
set -e
W=$(realpath "$1"); BAT=$2; N=${3:-20}; EXTRA=$4
REF=${ARMDOS_REFS:-}
if [ -z "$REF" ] || [ ! -f "$REF/msdos400-pcjs/MSDOS400-DISK1.img" ]; then
  echo "dos400run.sh: skipped - set ARMDOS_REFS to the MS-DOS 4.00 reference images" >&2
  exit 0
fi
HERE=$(dirname "$(realpath "$0")")
mkdir -p "$W"; rm -rf "$W/out" "$W/shots"; mkdir -p "$W/out"
cp $REF/msdos400-pcjs/MSDOS400-DISK1.img "$W/fd.img"
export MTOOLS_SKIP_CHECK=1
OFF=$((63*512))
if [ "$HD" = blank ]; then
  rm -f "$W/hd.img"; dd if=/dev/zero of="$W/hd.img" bs=512 count=0 seek=$((65*16*63)) 2>/dev/null
elif [ -n "$HD" ]; then
  cp "$HD" "$W/hd.img.new"; mv "$W/hd.img.new" "$W/hd.img"; HD=blank   # a given image, used as is
else
  cp $REF/dos400-verify/tools/hd-after-tests.img "$W/hd.img"
fi
printf "${CONFIG:-FILES=20\\r\\nBUFFERS=20\\r\\n}" > "$W/CONFIG.SYS"
if [ -n "$AUTOCMD" ]; then
  printf '@ECHO OFF\r\n%s\r\n' "$AUTOCMD" > "$W/AUTOEXEC.BAT"
else
  printf '@ECHO OFF\r\nPATH C:\\DOS\r\nC:\r\nCD \\\r\nCALL C:\\RUN.BAT\r\n' > "$W/AUTOEXEC.BAT"
fi
mcopy -o -i "$W/fd.img" "$W/CONFIG.SYS" "$W/AUTOEXEC.BAT" ::/
if [ "$HD" != blank ]; then
  sed 's/$/\r/' "$BAT" > "$W/RUN.BAT"
  nasm -f bin -o "$W/SCRDUMP.COM" "$HERE/scrdump.asm"
  mdeltree -i "$W/hd.img@@$OFF" ::/OUT >/dev/null 2>&1 || true
  mmd -i "$W/hd.img@@$OFF" ::/OUT
  mcopy -o -i "$W/hd.img@@$OFF" "$W/RUN.BAT" "$W/SCRDUMP.COM" ::/
  if [ -n "$EXTRA" ]; then mcopy -o -s -i "$W/hd.img@@$OFF" "$EXTRA"/* ::/; fi
fi
cat > "$W/t.conf" <<EOF
[sdl]
output=surface
[dosbox]
machine=svga_s3
memsize=4
quit warning=false
[cpu]
cycles=max
[autoexec]
IMGMOUNT 2 $W/hd.img -t hdd -fs none -size 512,63,16,65
IMGMOUNT A $W/fd.img -t floppy
BOOT -L A
EOF
if [ -n "$KEYS" ]; then
  D=:$((150 + RANDOM % 50))
  mkdir -p "$W/fb" "$W/shots"
  Xvfb $D -screen 0 1024x768x24 -fbdir "$W/fb" >"$W/xvfb.log" 2>&1 & XP=$!
  sleep 1
  DISPLAY=$D timeout $((N+2)) dosbox-x -conf "$W/t.conf" -nopromptfolder -fastlaunch >"$W/log.txt" 2>&1 &
  DISPLAY=$D python3 $REF/dos400-verify/tools/keys.py $D "$KEYS" >"$W/keys.log" 2>&1 &
  for i in $(seq 1 $N); do sleep 1; cp "$W/fb/Xvfb_screen0" "$W/shots/s$i.xwd" 2>/dev/null || true; done
  sleep 2; kill $XP 2>/dev/null || true
  ( cd "$W/shots"; for f in s*.xwd; do ffmpeg -loglevel error -y -i $f ${f%.xwd}.png 2>/dev/null; rm -f $f; done )
  ( cd $REF/dos400-verify/tools; for f in "$W"/shots/s*.png; do python3 decode.py "$f" > "${f%.png}.txt" 2>/dev/null || true; done )
  rm -rf "$W/fb"
else
  SDL_VIDEODRIVER=dummy timeout $N dosbox-x -conf "$W/t.conf" -nopromptfolder -fastlaunch >"$W/log.txt" 2>&1 || true
fi
if [ "$HD" != blank ]; then
  mcopy -o -n -i "$W/hd.img@@$OFF" '::/OUT/*' "$W/out/" 2>/dev/null || true
  for f in "$W"/out/*.SCR; do [ -e "$f" ] && python3 "$HERE/scr2txt.py" "$f" > "${f%.SCR}.SCT"; done
fi
[ -n "$KEEP" ] || rm -f "$W/hd.img" "$W/fd.img"
ls -la "$W/out"
