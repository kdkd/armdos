# apps/cdrom/app.mk - the Multimedia PC upgrade: ARMCD.SYS (ATAPI CD-ROM driver),
# ARMCDEX.EXE (CD-ROM extensions: INT 2Fh AH=15h + the redirector for drive D:),
# CDPLAY.EXE (Red Book audio player + Ctrl+Alt+C pop-up), and the
# "ARM-DOS Multimedia Sampler '93" disc (ISO 9660 + CD audio tracks).
CDROM_DIR := $(here)


# ---------------------------------------------------------------- ARMCD.SYS
# A freestanding AR1 driver image beginning with its device header (like
# apps/ansi): the resident part (armcd.o + the libgcc helpers it needs) is linked
# before mark.o's armcd_res_end, INIT (init.o, ints.o) after it.
CDROM_OBJ := $(BUILD)/obj/ARMCD
CDROM_DRV_CFLAGS := $(ARMDOS_ARCH) -Os -g -Wall -Wextra -Wno-unused-parameter -Wno-array-bounds -ffreestanding \
    -fno-delete-null-pointer-checks -fno-builtin -fno-jump-tables -fno-tree-loop-distribute-patterns \
    -fno-reorder-functions -ffunction-sections -fno-common -isystem $(ARMDOS_SDK)/include -I$(CDROM_DIR)inc
$(CDROM_OBJ)/%.o: $(CDROM_DIR)drv/%.c $(CDROM_DIR)drv/armcd.h $(CDROM_DIR)inc/cdrom.h
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(CDROM_DRV_CFLAGS) -c $< -o $@
$(CDROM_OBJ)/%.o: $(CDROM_DIR)drv/%.S
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $@
$(BUILD)/ARMCD.SYS: $(CDROM_OBJ)/armcd.o $(CDROM_OBJ)/mark.o $(CDROM_OBJ)/init.o $(CDROM_OBJ)/ints.o $(ARMDOS_SDK)/link.ld $(ARMDOS_SDK)/elf2exe.mjs
	$(ARMDOS_CC) $(ARMDOS_ARCH) -nostdlib -nostartfiles -T $(ARMDOS_SDK)/link.ld -Wl,--entry=0 \
	    -Wl,-q -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(CDROM_OBJ)/ARMCD.map \
	    -o $(CDROM_OBJ)/ARMCD.elf $(CDROM_OBJ)/armcd.o -lgcc $(CDROM_OBJ)/mark.o $(CDROM_OBJ)/init.o $(CDROM_OBJ)/ints.o -lgcc
	$(NODE) apps/ansi/tests/rescheck.mjs $(CDROM_OBJ)/ARMCD.elf 0 armcd_res_end armcd_init
	$(ARMDOS_ELF2EXE) --ar1 --stack 0 $(CDROM_OBJ)/ARMCD.elf -o $@
.PHONY: ARMCD
ARMCD: $(BUILD)/ARMCD.SYS
all: $(BUILD)/ARMCD.SYS
DISK_DEPS += $(BUILD)/ARMCD.SYS

# ---------------------------------------------------------------- ARMCDEX.EXE
# Freestanding like apps/popup (its own __armdos_start, no newlib), so that the
# resident part is only the code, its variables and the /M sector buffers.
$(call armdos_exe,ARMCDEX,cdex/cdex.c,-ffreestanding -fno-builtin -Wno-array-bounds -I$(CDROM_DIR)inc,,--stack 2048)

# ---------------------------------------------------------------- CDPLAY.EXE
# Freestanding too: /R keeps the whole program resident, with its 3 KB stack as
# the pop-up's own stack.
$(call armdos_exe,CDPLAY,cdplay/cdplay.c cdplay/cdasm.S,-ffreestanding -fno-builtin -Wno-array-bounds -I$(CDROM_DIR)inc,,--stack 3072)

# the disc (disc.mk uses $(CDROM_DIR), not $(here))
-include $(CDROM_DIR)disc.mk

# ---------------------------------------------------------------- tests
# MOCKRED.COM: a mock redirector (a TSR serving a tiny read-only D:) that tests the
# kernel's INT 2Fh AH=11h callouts on their own (README.md, "The kernel's redirector interface").
$(call armdos_com_to,MOCKRED,tests/mockred.c,-ffreestanding -fno-builtin -Wno-array-bounds,,--stack 2048,$(BUILD)/cdrom-test)

# CDAPI.EXE: calls the INT 2Fh AX=15xxh API the way 1993 programs did (tests/cdex.mjs)
$(call armdos_exe_to,CDAPI,tests/cdapi.c,-I$(CDROM_DIR)inc -Wno-format,,,$(BUILD)/cdrom-test)

CDROM_TEST_DEPS := $(BUILD)/ARMDOS.SYS $(BUILD)/COMMAND.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/HIMEM.SYS $(BUILD)/bootsect.bin

.PHONY: cdrom-redir-test
cdrom-redir-test: $(CDROM_TEST_DEPS) $(BUILD)/cdrom-test/MOCKRED.COM $(BUILD)/sdk-tests/T_HELLO.EXE
	$(NODE) $(CDROM_DIR)tests/redir-mock.mjs
test: cdrom-redir-test

CDROM_PROGS := $(BUILD)/ARMCD.SYS $(BUILD)/ARMCDEX.EXE $(BUILD)/CDPLAY.EXE $(BUILD)/cdrom-test/CDAPI.EXE $(BUILD)/SBMIX.EXE $(BUILD)/MOUSE.COM $(BUILD)/DEMO.EXE $(BUILD)/ZORK1.EXE

.PHONY: cdrom-cdex-test cdrom-cdplay-test cdrom-test
cdrom-cdex-test: $(CDROM_TEST_DEPS) $(CDROM_PROGS) $(CDROM_DISC)
	$(NODE) $(CDROM_DIR)tests/cdex.mjs
cdrom-cdplay-test: $(CDROM_TEST_DEPS) $(CDROM_PROGS) $(CDROM_DISC)
	$(NODE) $(CDROM_DIR)tests/cdplay.mjs
cdrom-test: cdrom-disc-test cdrom-redir-test cdrom-cdex-test cdrom-cdplay-test
test: cdrom-cdex-test cdrom-cdplay-test
