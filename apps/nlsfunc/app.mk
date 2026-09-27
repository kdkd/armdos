# apps/nlsfunc/app.mk - NLSFUNC.EXE (the kernel's COUNTRY.SYS reader after
# CONFIG.SYS time, for CHCP) and COUNTRY.SYS (from MS-DOS 4.00's MKCNTRY.ASM,
# country/, MIT licence, by tools/mkcountry.mjs). The resident part
# (nlsres.c, nlsint.S) sits in .text.unlikely.* sections right after crt0.
NLS_DIR := $(here)
$(call armdos_exe,NLSFUNC,nlsres.c nlsint.S nlsfunc.c,-fno-jump-tables -fno-tree-loop-distribute-patterns \
       -fno-reorder-functions -Wno-array-bounds -fno-delete-null-pointer-checks)

$(BUILD)/COUNTRY.SYS: $(wildcard $(NLS_DIR)country/*) $(NLS_DIR)tools/mkcountry.mjs
	$(NODE) $(NLS_DIR)tools/mkcountry.mjs $@
all: $(BUILD)/COUNTRY.SYS
DISK_DEPS += $(BUILD)/COUNTRY.SYS

.PHONY: nlsfunc-test
nlsfunc-test: $(NLSFUNC_OUT) $(BUILD)/COUNTRY.SYS $(BUILD)/DISPLAY.SYS $(BUILD)/EGA.CPI $(BUILD)/MODE.COM $(BUILD)/KEYB.COM \
              $(BUILD)/KEYBOARD.SYS $(BUILD)/COMMAND.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin $(BUILD)/keyb-test/KEYTEST.EXE
	$(NODE) $(NLS_DIR)tests/run.mjs
test: nlsfunc-test
