# apps/hercules/app.mk - HERCULES.EXE, the Hercules Graphics Card demo (README.md).
# Goes to C:\DEMO\ (apps/hercules/hd.json), not C:\DOS.
HERCULES_DIR := $(here)
$(call armdos_exe,HERCULES,hgc.c,$(ARMDOS_VFP) -Wno-array-bounds,-lm)

.PHONY: hercules-test
hercules-test: $(BUILD)/HERCULES.EXE $(BUILD)/hd.img $(BUILD)/rom.bin
	$(NODE) $(HERCULES_DIR)tests/run.mjs
test: hercules-test
