# apps/ac/app.mk - the ARM Commander (see README.md):
#   AC.EXE      the small resident loader (stays in memory while commands run)
#   ACMAIN.EXE  the Commander itself
AC_DIR := $(here)

$(call armdos_exe,AC,ac.c,-Wno-array-bounds,,--stack 1536)
$(call armdos_exe,ACMAIN,main.c scr.c panel.c ops.c view.c edit.c menu.c dos.c,-I$(AC_DIR) -Wno-array-bounds,,--stack 65536)

.PHONY: ac-test
ac-test: $(BUILD)/AC.EXE $(BUILD)/ACMAIN.EXE $(BUILD)/COMMAND.COM $(BUILD)/MEM.EXE $(BUILD)/MOUSE.COM $(BUILD)/HIMEM.SYS \
         $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(AC_DIR)tests/run.mjs
test: ac-test
