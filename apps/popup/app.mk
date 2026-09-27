# apps/popup/app.mk - POPUP.COM, a SideKick-style TSR (see README.md).
# Freestanding (no C library): it defines its own __armdos_start, so the
# SDK's start-up and newlib are not linked and the resident part stays small.
# The 3 KB stack reserved after the image is the pop-up's own stack.
POPUP_DIR := $(here)
$(call armdos_com,POPUP,popup.c popasm.S,-ffreestanding -fno-builtin -Wno-array-bounds,,--stack 3072)

.PHONY: popup-test
popup-test: $(BUILD)/POPUP.COM $(BUILD)/ARMINFO.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(POPUP_DIR)tests/run.mjs
test: popup-test
