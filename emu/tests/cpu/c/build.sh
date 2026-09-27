#!/bin/sh
# build.sh <out-dir> <name> <extra cflags...>
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$1; name=$2; shift 2
mkdir -p "$out"
CC="arm-none-eabi-gcc -march=armv5te -mfloat-abi=soft -ffreestanding -nostdlib -fno-builtin -mthumb-interwork"
$CC -c "$here/crt0.S" -o "$out/crt0.o"
$CC "$@" -c "$here/lib.c" -o "$out/lib.o"
$CC "$@" -c "$here/$name.c" -o "$out/$name.o"
$CC "$@" -T "$here/link.ld" -o "$out/$name.elf" "$out/crt0.o" "$out/lib.o" "$out/$name.o" -lgcc
arm-none-eabi-objcopy -O binary "$out/$name.elf" "$out/$name.bin"
