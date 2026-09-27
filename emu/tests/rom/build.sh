#!/bin/sh
# Builds the device test ROM: <out-dir>/testrom.bin (1 MB); default out-dir build/emu-tests/rom
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../../../build/emu-tests/rom}
mkdir -p "$out"
CC="arm-none-eabi-gcc -marm -march=armv5te -mfloat-abi=soft -ffreestanding -nostdlib -fno-builtin -Os -Wall"
$CC -DFONTFILE="\"$here/../../fonts/vga8x16.bin\"" -c "$here/start.S" -o "$out/start.o"
$CC -c "$here/testrom.c" -o "$out/testrom.o"
$CC -T "$here/link.ld" -o "$out/testrom.elf" "$out/start.o" "$out/testrom.o" -lgcc
arm-none-eabi-objcopy -O binary --gap-fill=0xff -j .header -j .text -j .data -j .vectors -j .pad "$out/testrom.elf" "$out/testrom.bin"
ls -l "$out/testrom.bin" | awk '{print $5}'
