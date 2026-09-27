# apps/turbo/app.mk - TURBO.COM: the turbo switch from DOS, and TURBO MAX (no
# clock limit) for the heavy x86 demos under ELBOW. See turbo.c.
TURBO_DIR := $(here)
$(call armdos_com,TURBO,turbo.c,-Wno-array-bounds)

.PHONY: turbo-test
turbo-test: $(BUILD)/TURBO.COM $(BUILD)/COMMAND.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(TURBO_DIR)tests/run.mjs
test: turbo-test
