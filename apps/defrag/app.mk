# apps/defrag/app.mk - DEFRAG.EXE, the ARM Disk Optimizer (see README.md).
# Not installed on C: (hd.json excludes it): it is a download on The ARM Pit
# BBS, as build/DEFRAG11.ZIP (DEFRAG.EXE + DEFRAG.DOC + FILE_ID.DIZ).
DEFRAG_DIR := $(here)

$(call armdos_exe,DEFRAG,main.c engine.c ui.c dosutil_inc.c,-I$(DEFRAG_DIR) -I$(DEFRAG_DIR)../format -Wno-array-bounds,,--stack 16384)

$(BUILD)/DEFRAG11.ZIP: $(BUILD)/DEFRAG.EXE $(DEFRAG_DIR)dist/DEFRAG.DOC $(DEFRAG_DIR)dist/FILE_ID.DIZ $(DEFRAG_DIR)tools/mkzip.py
	python3 $(DEFRAG_DIR)tools/mkzip.py $@ $(BUILD)/DEFRAG.EXE $(DEFRAG_DIR)dist/DEFRAG.DOC $(DEFRAG_DIR)dist/FILE_ID.DIZ
all: $(BUILD)/DEFRAG11.ZIP

.PHONY: defrag-test
defrag-test: $(BUILD)/DEFRAG.EXE $(BUILD)/DEFRAG11.ZIP $(BUILD)/UNZIP.EXE $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin \
             $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin $(BUILD)/MOUSE.COM
	$(NODE) $(DEFRAG_DIR)tests/run.mjs
test: defrag-test
