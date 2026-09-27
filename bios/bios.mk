# ARM/AT BIOS -> build/rom.bin (1 MB)
BIOS_SRC := bios/start.S bios/font.S bios/post.c bios/video.c bios/kbd.c bios/timer.c bios/disk.c bios/setup.c bios/lib.c
BIOS_OBJ := $(patsubst bios/%,build/bios/%.o,$(BIOS_SRC))
BIOS_CFLAGS := -marm -march=armv5te -mfloat-abi=soft -Os -ffreestanding -fno-builtin -nostdlib \
	-Wall -Wextra -Wno-unused-parameter -Wno-array-bounds -fno-delete-null-pointer-checks -ffunction-sections -fdata-sections -Ibios

build/bios/%.o: bios/% bios/bios.h
	@mkdir -p $(dir $@)
	arm-none-eabi-gcc $(BIOS_CFLAGS) -c $< -o $@

build/bios/font.S.o: emu/fonts/vga8x16.bin emu/fonts/cga8x8.bin

build/rom.elf: $(BIOS_OBJ) bios/rom.ld
	arm-none-eabi-gcc $(BIOS_CFLAGS) -T bios/rom.ld -Wl,--gc-sections -o $@ $(BIOS_OBJ) -lgcc

build/rom.bin: build/rom.elf
	arm-none-eabi-objcopy -O binary --gap-fill=0xFF -j .text -j .ARM.exidx -j .data -j .vectors $< $@.tmp
	python3 -c "import sys;d=open('$@.tmp','rb').read();assert len(d)<=1<<20,len(d);open('$@','wb').write(d+b'\xff'*((1<<20)-len(d)))"
	@rm -f $@.tmp

bios: build/rom.bin
.PHONY: bios
all: build/rom.bin
