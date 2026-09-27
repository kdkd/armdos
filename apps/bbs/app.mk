# apps/bbs/app.mk - The ARM Pit BBS (BBS.EXE), its first door game (DRAGON.EXE; more in apps/doors) and
# the BBS machine's hard disk, build/bbs.img ("make bbs-image"). See README.md.
# The comm/ANSI/ZMODEM library is apps/term/lib (lib_*.c include it).
BBS_DIR := $(here)
BBS_CFLAGS := -Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds
$(call armdos_exe,BBS,bbs.c users.c msgs.c files.c misc.c sio.c lib_comm.c lib_scr.c lib_vt.c lib_zmodem.c lib_xmodem.c,$(BBS_CFLAGS),,--stack 24576)
$(call armdos_exe,DRAGON,door/dragon.c sio.c lib_comm.c lib_scr.c lib_vt.c,$(BBS_CFLAGS),,--stack 16384)

BBS_DATA := $(shell find $(BBS_DIR)data apps/doors/data -type f 2>/dev/null)
BBS_DOORS := $(BUILD)/DRAGON.EXE $(BUILD)/RISCWARS.EXE $(BUILD)/TRIVIA.EXE $(BUILD)/PITPOKER.EXE
# DEFRAG11.ZIP (apps/defrag) goes in the Utilities file area when it can be built:
# optional, so a broken or missing DEFRAG never stops the BBS disk from building.
BBS_DEFRAG := $(wildcard $(BUILD)/DEFRAG11.ZIP)
$(BUILD)/bbs.img: $(BBS_DIR)bbs.json $(BBS_DATA) 3rdparty/keen/KEENDRMS.ZIP $(BUILD)/BBS.EXE $(BBS_DOORS) $(BUILD)/TERM.EXE \
                  $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/COMMAND.COM $(BUILD)/bootsect.bin $(BBS_DEFRAG)
	$(if $(wildcard apps/defrag/app.mk),-@$(MAKE) --no-print-directory $(BUILD)/DEFRAG11.ZIP >/dev/null 2>&1 || echo "bbs.img: DEFRAG11.ZIP could not be built - the Utilities area goes without it")
	$(NODE) disk/mkimage.mjs build $(BBS_DIR)bbs.json -o $@
.PHONY: bbs-image bbs-test
bbs-image: $(BUILD)/bbs.img
all: $(BUILD)/bbs.img
bbs-test: $(BUILD)/bbs.img $(BUILD)/TERM.EXE $(BUILD)/UNZIP.EXE $(BUILD)/rom.bin
	$(NODE) $(BBS_DIR)tests/run.mjs
	$(NODE) $(BBS_DIR)tests/doors.mjs
test: bbs-test
