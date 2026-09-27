# apps/unzip/app.mk - UNZIP.EXE, a PKUNZIP-style ZIP extractor with its own inflater (README.md)
UNZIP_DIR := $(here)
$(call armdos_exe,UNZIP,unzip.c)

.PHONY: unzip-test
unzip-test: $(BUILD)/UNZIP.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/COMMAND.COM $(BUILD)/bootsect.bin
	$(NODE) $(UNZIP_DIR)tests/run.mjs
test: unzip-test
