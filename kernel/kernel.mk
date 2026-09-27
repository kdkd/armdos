# kernel/kernel.mk - the ARM-DOS 4.00 kernel (ARCH.md 14):
#   build/bootsect.bin   FAT boot sector (kernel/boot)
#   build/IO.SYS         resident drivers + SYSINIT (kernel/io), flat, at 0x700
#   build/ARMDOS.SYS     the DOS proper (kernel/dos), AR1 relocatable
#   build/HIMEM.SYS      XMS driver (kernel/himem), AR1 with a device header
# plus the test programs and test images (kernel/tests, "make kernel-test").

K_CC      := arm-none-eabi-gcc
K_OBJCOPY := arm-none-eabi-objcopy
K_OUT     := $(BUILD)/kernel
K_ARCH    := -marm -march=armv5te -mfloat-abi=soft
K_CFLAGS  := $(K_ARCH) -mthumb -Os -g -ffreestanding -nostdlib -fno-common -fno-builtin \
             -Wall -Wextra -Wno-unused-parameter -Wno-array-bounds \
             -fno-delete-null-pointer-checks -ffunction-sections -fdata-sections \
             -Ikernel/inc -Ikernel/lib
K_HDRS    := $(wildcard kernel/inc/*.h kernel/lib/*.h)

# ------------------------------------------------------------ boot sector
$(K_OUT)/boot/boot.o: kernel/boot/boot.S
	@mkdir -p $(dir $@)
	$(K_CC) $(K_ARCH) -c $< -o $@

$(BUILD)/bootsect.bin: $(K_OUT)/boot/boot.o
	$(K_CC) $(K_ARCH) -nostdlib -Wl,-Ttext=0x9F000 -Wl,--no-warn-rwx-segments -o $(K_OUT)/boot/boot.elf $<
	$(K_OBJCOPY) -O binary -j .text $(K_OUT)/boot/boot.elf $@
	@test $$(wc -c < $@) -eq 512 || (echo "boot sector is not 512 bytes"; rm -f $@; exit 1)

# ------------------------------------------------------------------ IO.SYS
IO_SRC := kernel/io/start.S kernel/io/con.c kernel/io/aux.c kernel/io/disk.c \
          kernel/io/init_sysinit.c kernel/io/init_config.c kernel/lib/klib.c kernel/lib/karm.S
IO_OBJ := $(patsubst kernel/%,$(K_OUT)/io-obj/%.o,$(IO_SRC))
# the linker script picks SYSINIT's objects by name: keep "init_" in them
$(K_OUT)/io-obj/io/init_%.c.o: kernel/io/init_%.c $(K_HDRS) kernel/io/iosys.h kernel/io/init.h
	@mkdir -p $(dir $@)
	$(K_CC) $(K_CFLAGS) -Ikernel/io -c $< -o $@
$(K_OUT)/io-obj/%.c.o: kernel/%.c $(K_HDRS) kernel/io/iosys.h
	@mkdir -p $(dir $@)
	$(K_CC) $(K_CFLAGS) -Ikernel/io -c $< -o $@
$(K_OUT)/io-obj/%.S.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(K_CC) $(K_ARCH) -c $< -o $@

$(K_OUT)/IO.elf: $(IO_OBJ) kernel/io/io.ld
	$(K_CC) $(K_CFLAGS) -T kernel/io/io.ld -Wl,--use-blx -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
	    -Wl,-Map=$(K_OUT)/IO.map -o $@ $(IO_OBJ) -lgcc
$(BUILD)/IO.SYS: $(K_OUT)/IO.elf
	$(K_OBJCOPY) -O binary -j .text -j .data -j .init $< $@

# -------------------------------------------------------------- ARMDOS.SYS
DOS_SRC := kernel/dos/entry.S kernel/dos/init.c kernel/dos/int21.c kernel/dos/char.c \
           kernel/dos/dev.c kernel/dos/buf.c kernel/dos/fat.c kernel/dos/name.c \
           kernel/dos/dir.c kernel/dos/file.c kernel/dos/find.c kernel/dos/fcb.c \
           kernel/dos/mem.c kernel/dos/proc.c kernel/dos/ioctl.c kernel/dos/misc.c kernel/dos/redir.c \
           kernel/lib/klib.c kernel/lib/karm.S
DOS_OBJ := $(patsubst kernel/%,$(K_OUT)/dos-obj/%.o,$(DOS_SRC))
$(K_OUT)/dos-obj/%.c.o: kernel/%.c $(K_HDRS) kernel/dos/dos.h
	@mkdir -p $(dir $@)
	$(K_CC) $(K_CFLAGS) -Ikernel/dos -c $< -o $@
$(K_OUT)/dos-obj/%.S.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(K_CC) $(K_ARCH) -c $< -o $@

$(K_OUT)/ARMDOS.elf: $(DOS_OBJ) kernel/dos/dos.ld
	$(K_CC) $(K_CFLAGS) -T kernel/dos/dos.ld -Wl,-q -Wl,--use-blx -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
	    -Wl,-Map=$(K_OUT)/ARMDOS.map -o $@ $(DOS_OBJ) -lgcc
$(BUILD)/ARMDOS.SYS: $(K_OUT)/ARMDOS.elf sdk/elf2exe.mjs
	$(NODE) sdk/elf2exe.mjs --ar1 --stack 0 $< -o $@

# --------------------------------------------------------------- HIMEM.SYS
-include kernel/himem/himem.mk

KERNEL_OUT := $(BUILD)/bootsect.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(KERNEL_EXTRA)
DISK_DEPS += $(KERNEL_OUT)
.PHONY: kernel
kernel: $(KERNEL_OUT)
all: $(KERNEL_OUT)

# ------------------------------------------------------------------ tests
-include kernel/tests/tests.mk
