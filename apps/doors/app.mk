# apps/doors/app.mk - door games for The ARM Pit BBS (apps/bbs): each one is
# launched by BBS.EXE with the drop-file directory (DOOR.SYS / DORINFO1.DEF),
# shares the small door library in lib/ (door.c + the BBS's sio.c + the comm/
# screen library of apps/term/lib), and lives on the BBS machine's disk only
# (build/bbs.img, apps/bbs/bbs.json; hd.json keeps them out of the visitor's C:\DOS).
DOORS_DIR := $(here)
DOOR_CFLAGS := -Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds
DOOR_LIB := lib/door.c lib/lib_sio.c lib/lib_comm.c lib/lib_scr.c lib/lib_vt.c
$(call armdos_exe,RISCWARS,riscwars/riscwars.c $(DOOR_LIB),$(DOOR_CFLAGS),,--stack 16384)
$(call armdos_exe,TRIVIA,trivia/trivia.c $(DOOR_LIB),$(DOOR_CFLAGS),,--stack 16384)
$(call armdos_exe,PITPOKER,poker/poker.c $(DOOR_LIB),$(DOOR_CFLAGS),,--stack 16384)
.PHONY: doors-test
doors-test: $(BUILD)/RISCWARS.EXE $(BUILD)/TRIVIA.EXE $(BUILD)/PITPOKER.EXE $(BUILD)/rom.bin
	$(NODE) $(DOORS_DIR)tests/local.mjs
test: doors-test
