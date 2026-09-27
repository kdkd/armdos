# apps/keyb/app.mk - KEYB.COM and KEYBOARD.SYS (international keyboard layouts,
# DOS 4.00 style). The resident part (kbres.c) sits in .text.unlikely.*
# sections right after crt0 (see keyb.h); kbend.S marks its end.
KEYB_DIR := $(here)
$(call armdos_com,KEYB,kbres.c kbend.S keyb.c,-fno-jump-tables -fno-tree-loop-distribute-patterns \
       -fno-reorder-functions -Wno-array-bounds -fno-delete-null-pointer-checks)

# KEYBOARD.SYS: MS-DOS 4.00's keyboard definition sources (kdf/, MIT licence)
# plus ARM-DOS's Dvorak layouts, assembled by tools/kdfasm.mjs
$(BUILD)/KEYBOARD.SYS: $(wildcard $(KEYB_DIR)kdf/*) $(KEYB_DIR)tools/kdfasm.mjs
	$(NODE) $(KEYB_DIR)tools/kdfasm.mjs $@
all: $(BUILD)/KEYBOARD.SYS
DISK_DEPS += $(BUILD)/KEYBOARD.SYS

.PHONY: keyb-test
keyb-test: $(KEYB_OUT) $(BUILD)/KEYBOARD.SYS $(BUILD)/COMMAND.COM $(BUILD)/DISPLAY.SYS $(BUILD)/EGA.CPI $(BUILD)/MODE.COM \
           $(BUILD)/NLSFUNC.EXE $(BUILD)/COUNTRY.SYS $(BUILD)/ANSI.SYS \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(KEYB_DIR)tests/run.mjs
test: keyb-test

# tests (apps/keyb/tests/run.mjs, "make keyb-test"): helper programs go to
# build/keyb-test/, not to C:\DOS
$(call armdos_exe_to,KEYTEST,tests/keytest.c,,,,$(BUILD)/keyb-test)
keyb-test: $(KEYTEST_OUT)
