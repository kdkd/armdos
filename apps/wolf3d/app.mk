# apps/wolf3d/app.mk - WOLF3D.EXE: Wolfenstein 3D (Wolf4SDL, GPL-2, based on
# id Software's source) with its SDL layer replaced by the bare ARM-PC
# hardware: mode 13h, INT 09h keyboard, PIT + INT 08h, PC speaker, AdLib
# ports. See README.md. Soft-float (the SDK default).

WOLF3D_DIR    := $(here)
WOLF3D_SRCS   := $(patsubst $(WOLF3D_DIR)%,%,$(wildcard $(WOLF3D_DIR)src/*.c) $(wildcard $(WOLF3D_DIR)src/armdos/*.c))
WOLF3D_CFLAGS := -std=gnu99 -fno-strict-aliasing -DARMDOS \
                 -I$(WOLF3D_DIR)src/armdos -I$(WOLF3D_DIR)src \
                 -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
                 -Wno-unused-but-set-variable -Wno-missing-field-initializers \
                 -Wno-implicit-fallthrough -Wno-pointer-sign -Wno-format-truncation \
                 -Wno-char-subscripts -Wno-parentheses -Wno-type-limits \
                 -Wno-empty-body -Wno-misleading-indentation -Wno-array-bounds \
                 -Wno-unused-parameter -Wno-address-of-packed-member \
                 -Wno-stringop-truncation -Wno-format-overflow -Wno-enum-conversion

# the ray caster and scaler are hot
WOLF3D_HOT    := wl_draw wl_scale wl_state wl_act2 wl_agent wl_play id_vh id_vl

$(call armdos_exe,WOLF3D,$(WOLF3D_SRCS),$(WOLF3D_CFLAGS),-lm,--stack 32768)

$(patsubst %,$(BUILD)/obj/WOLF3D/src/%.c.o,$(WOLF3D_HOT)): ARMDOS_CFLAGS += -O2

# the disk image carries the shareware data (apps/wolf3d/hd.json)
DISK_DEPS += $(wildcard 3rdparty/wolf3d/*)

# "make wolf3d-test": boot ARM-DOS headless and play (apps/wolf3d/tests/run.mjs)
.PHONY: wolf3d-test
test: wolf3d-test
wolf3d-test: $(BUILD)/WOLF3D.EXE $(BUILD)/ktest/TSHELL.EXE \
             $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(WOLF3D_DIR)tests/run.mjs

# "make wolf3d-joy-test": a joystick in the game port - detection, Calibrate Joystick,
# walking/turning/firing with the stick (apps/wolf3d/tests/joystick.mjs)
.PHONY: wolf3d-joy-test
test: wolf3d-joy-test
wolf3d-joy-test: $(BUILD)/WOLF3D.EXE $(BUILD)/ktest/TSHELL.EXE \
             $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(WOLF3D_DIR)tests/joystick.mjs
