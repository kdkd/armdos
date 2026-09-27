# apps/crash/app.mk - CRASH.EXE, a tour of ARM exceptions (see README.md)
CRASH_DIR := $(here)
$(call armdos_exe,CRASH,crash.c crashasm.S,-std=gnu99 -Wno-infinite-recursion)

.PHONY: crash-test
crash-test: $(BUILD)/CRASH.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(CRASH_DIR)tests/run.mjs
test: crash-test
