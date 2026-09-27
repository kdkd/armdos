# apps/mode/app.mk - MODE.COM (DOS 4.00 MODE: display, CON, COM, LPT, status).
# Its resident portion (mdres.c, printer redirection and retries) sits in
# .text.unlikely.* sections right after crt0 (see mode.h); mdend.S marks its end.
MODE_DIR := $(here)
$(call armdos_com,MODE,mdres.c mdend.S mode.c,-fno-jump-tables -fno-tree-loop-distribute-patterns \
       -fno-reorder-functions -Wno-array-bounds -fno-delete-null-pointer-checks)

.PHONY: mode-test
mode-test: $(MODE_OUT) $(BUILD)/ANSI.SYS $(BUILD)/EGA.CPI $(BUILD)/ktest/TSHELL.EXE \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(MODE_DIR)tests/run.mjs
test: mode-test
