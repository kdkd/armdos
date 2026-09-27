# apps/fdisk/app.mk - FDISK.EXE: Microsoft's MS-DOS 4.0 FDISK (CMD/FDISK),
# MIT-licensed source compiled for ARM.  See README.md.
#
# Generated at build time:
#   fdiskm.c   the screens: FDISK.MSG + USA-MS.MSG through tools/menubld.py
#              (Microsoft's MENUBLD.EXE is binary-only)
#   bootrec.c  the master boot record for new disks: disk/mbr.S (ARM code)
#              through tools/mbr2c.py (the original embeds x86 FDBOOT.ASM)
FDISK_DIR := $(here)
include apps/mslib/mslib.mk

FDISK_SRCS := src/main.c src/mainmenu.c src/d_menus.c src/c_menus.c src/fdisk.c \
              src/display.c src/input.c src/tdisplay.c src/vdisplay.c src/space.c \
              src/partinfo.c src/video.c src/makepart.c src/int13.c src/diskout.c \
              src/messages.c src/fdparse.c src/convert.c src/global.c src/armdos.c
FDISK_GEN    := $(BUILD)/obj/FDISK/gen
FDISK_CFLAGS := $(MSLIB_MSC_CFLAGS) -I$(FDISK_DIR)src -Dpascal= -DLINT_ARGS
FDISK_EXTRA  := $(FDISK_GEN)/fdiskm.o $(FDISK_GEN)/bootrec.o
FDISK_BRANDING := --subst "MS-DOS Version 4.00=ARM-DOS Version 4.00" \
                  --subst "(C)Copyright Microsoft Corp. 1983, 1988=(C)Copyright Europa Micro Systems 1988"

$(FDISK_GEN)/fdiskm.c: $(FDISK_DIR)src/FDISK.MSG $(FDISK_DIR)tools/menubld.py $(MSLIB_MSGDEP)
	@mkdir -p $(dir $@)
	$(MSLIB_PY) $(FDISK_DIR)tools/menubld.py $(FDISK_DIR)src/FDISK.MSG $(MSLIB_DIR)msg/USA-MS.MSG -o $@ $(FDISK_BRANDING)

$(FDISK_GEN)/mbr.bin: disk/mbr.S
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) -marm -march=armv5te -c $< -o $(FDISK_GEN)/mbr.o
	arm-none-eabi-objcopy -O binary $(FDISK_GEN)/mbr.o $@

$(FDISK_GEN)/bootrec.c: $(FDISK_GEN)/mbr.bin $(FDISK_DIR)tools/mbr2c.py
	$(MSLIB_PY) $(FDISK_DIR)tools/mbr2c.py $< -o $@

$(FDISK_GEN)/%.o: $(FDISK_GEN)/%.c
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) $(FDISK_CFLAGS) -c $< -o $@

$(call mslib_exe,FDISK,$(FDISK_DIR),$(FDISK_SRCS),$(FDISK_CFLAGS),src/FDISK.SKL,,--stack 16384,$(FDISK_EXTRA))

.PHONY: fdisk-test
fdisk-test: $(BUILD)/FDISK.EXE $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(FDISK_DIR)tests/run.mjs
test: fdisk-test
