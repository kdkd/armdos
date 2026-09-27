# apps/online/app.mk - ARM-DOS Online, the client (ONLINE.EXE); the service is emu/online/.
# The serial and screen code is TERM's (apps/term/lib, included by lib_*.c).
ONLINE_DIR := $(here)
$(call armdos_exe,ONLINE,main.c link.c doc.c reader.c viewer.c ui.c lib_comm.c lib_scr.c,-Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds,,--stack 16384)

.PHONY: online-test online-service-test
online-service-test:
	$(NODE) $(ONLINE_DIR)tests/service.mjs
online-test: online-service-test $(BUILD)/ONLINE.EXE $(BUILD)/MOUSE.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/COMMAND.COM $(BUILD)/bootsect.bin
	$(NODE) $(ONLINE_DIR)tests/run.mjs
test: online-test
