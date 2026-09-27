# apps/keen/app.mk - KEEN.EXE: Commander Keen in Keen Dreams (id Software /
# Softdisk, 1991-92; GPL-2+ source release 2014) for ARM-DOS, built from
# ReflectionHLE's port of that source (src/kdreams, GPL-2+) with its SDL
# backend replaced by the bare ARM-PC hardware (src/armdos/be_armdos.c).
# Ships without game data: GETKEEN.BAT fetches the unmodified shareware
# KEENDRMS.ZIP from The ARM Pit BBS. See README.md.

KEEN_DIR    := $(here)
KEEN_SRCS   := $(patsubst $(KEEN_DIR)%,%,$(filter-out %/id_us_s_kdreams100.c %/id_us_s_kdreams113.c %/id_us_s_kdreams120.c %/id_us_s_kdreams192andlater.c, \
                 $(wildcard $(KEEN_DIR)src/kdreams/*.c)) \
                 $(wildcard $(KEEN_DIR)src/kdreams/lscr/*.c) \
                 $(KEEN_DIR)src/be_cross.c $(KEEN_DIR)src/be_cross_mem.c $(KEEN_DIR)src/be_cross_doszeroseg.c $(KEEN_DIR)src/be_filesystem_file.c \
                 $(KEEN_DIR)src/unlzexe/unlzexe.c $(KEEN_DIR)src/crc32/crc32.c \
                 $(wildcard $(KEEN_DIR)src/armdos/*.c))
KEEN_CFLAGS := -std=gnu99 -fno-strict-aliasing -DARMDOS \
               -I$(KEEN_DIR)src -I$(KEEN_DIR)src/kdreams \
               -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
               -Wno-unused-but-set-variable -Wno-missing-field-initializers \
               -Wno-implicit-fallthrough -Wno-pointer-sign -Wno-format-truncation \
               -Wno-char-subscripts -Wno-parentheses -Wno-type-limits \
               -Wno-empty-body -Wno-misleading-indentation -Wno-array-bounds \
               -Wno-unused-parameter -Wno-stringop-truncation -Wno-format-overflow \
               -Wno-enum-conversion -Wno-old-style-declaration -Wno-unused-value \
               -Wno-dangling-pointer -Wno-shift-negative-value

$(call armdos_exe,KEEN,$(KEEN_SRCS),$(KEEN_CFLAGS),,--stack 32768)

# the refresh manager, sprite drawing and the EGA emulation are hot
KEEN_HOT := src/kdreams/id_rf src/kdreams/id_rf_a src/kdreams/id_vw src/kdreams/id_vw_a src/kdreams/id_vw_ae src/armdos/be_armdos
$(patsubst %,$(BUILD)/obj/KEEN/%.c.o,$(KEEN_HOT)): ARMDOS_CFLAGS += -O2

# "make keen-test": the whole GETKEEN.BAT story on two machines (apps/keen/tests/run.mjs)
.PHONY: keen-test
test: keen-test
keen-test: $(BUILD)/KEEN.EXE $(BUILD)/TERM.EXE $(BUILD)/UNZIP.EXE $(BUILD)/bbs.img \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/COMMAND.COM $(BUILD)/bootsect.bin
	$(NODE) $(KEEN_DIR)tests/run.mjs

# "make keen-joy-test": the joystick on the game port (apps/keen/tests/joystick.mjs)
.PHONY: keen-joy-test
test: keen-joy-test
keen-joy-test: $(BUILD)/KEEN.EXE $(BUILD)/HIMEM.SYS \
               $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/COMMAND.COM $(BUILD)/bootsect.bin
	$(NODE) $(KEEN_DIR)tests/joystick.mjs

# C:\GAMES\KEEN gets GETKEEN.BAT, GETKEEN.SCR, README.TXT (hd.json); KEENDRMS.ZIP only goes on build/bbs.img
DISK_DEPS += $(KEEN_DIR)data/GETKEEN.BAT $(KEEN_DIR)data/GETKEEN.SCR $(KEEN_DIR)data/README.TXT
