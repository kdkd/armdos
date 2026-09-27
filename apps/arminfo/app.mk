# apps/arminfo/app.mk - ARMINFO.EXE, System Information for the ARM/AT (see README.md)
ARMINFO_DIR := $(here)
$(call armdos_exe,ARMINFO,arminfo.c bench.c armasm.S,-std=gnu99)
# the benchmark kernels are compiled -O2 ("compiled C" in the tests)
$(BUILD)/obj/ARMINFO/bench.c.o: ARMDOS_CFLAGS += -O2

.PHONY: arminfo-test
arminfo-test: $(BUILD)/ARMINFO.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(ARMINFO_DIR)tests/run.mjs
test: arminfo-test
