# apps/qbasic/app.mk - QB.EXE, "ARM QuickBASIC": a QBasic-style BASIC
# environment on Turbo Vision (apps/tvlib) with Bywater BASIC (bw/, a
# QuickBASIC-flavoured copy of apps/basic's bwBASIC, GPL-2) linked in.
#
#   build/qb/QBIMG.EXE   the environment (an ordinary ARM-DOS EXE image)
#   build/QB.EXE         apps/tvlib's XMS loader stub + that image
#                        (C:\DOS\QB.EXE); samples in C:\QB (hd.json)

QB_DIR := $(here)
include $(QB_DIR)../tvlib/tvlib.mk

QB_OUT  := $(BUILD)/qb
QB_OBJ  := $(BUILD)/obj/QBIMG
QB_SRCS := qb.cpp qbdlg.cpp qbrun.cpp qbhelp.cpp

# the interpreter (C, GPL-2)
QB_BW_SRCS := bwbasic.c bwb_cmd.c bwb_cnd.c bwb_dio.c bwb_exp.c bwb_fnc.c bwb_inp.c \
              bwb_int.c bwb_prn.c bwb_stc.c bwb_str.c bwb_tbl.c bwb_var.c bwd_cmd.c \
              bwd_fun.c bwx_dos.c dosvid.c
QB_BW_OBJS := $(patsubst %.c,$(QB_OBJ)/bw/%.o,$(QB_BW_SRCS))
QB_BW_CFLAGS := -DARMDOS=1 -DQBIDE=1 -DHAVE_MSDOS=1 -I$(QB_DIR)bw \
                -DDEF_PROMPT='"Ok\n"' -DDEF_EXTENSION='".BAS"' -DPROFILENAME='"PROFILE.BAS"' \
                -Wno-sign-compare -Wno-unused-but-set-variable -Wno-unused-variable \
                -Wno-implicit-fallthrough -Wno-missing-field-initializers -Wno-array-bounds \
                -Wno-char-subscripts -Wno-format-truncation -Wno-misleading-indentation \
                -Wno-unused-function
$(QB_OBJ)/bw/%.o: $(QB_DIR)bw/%.c $(wildcard $(QB_DIR)bw/*.h) $(QB_DIR)bw/qbengine.inc $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) -O2 $(ARMDOS_VFP) $(QB_BW_CFLAGS) -c $< -o $@

$(QB_OUT)/qbhelp.c: $(QB_DIR)data/qbhelp.txt $(TVLIB_DIR)tools/mkhelp.mjs
	@mkdir -p $(dir $@)
	$(NODE) $(TVLIB_DIR)tools/mkhelp.mjs $< $@ qbHelpText
$(QB_OBJ)/qbhelp_text.o: $(QB_OUT)/qbhelp.c
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) -c $< -o $@

QB_EXTRA := $(QB_BW_OBJS) $(QB_OBJ)/qbhelp_text.o $(TVLIB_OBJS)
$(eval $(call _armdos_program,QBIMG,$(QB_SRCS),$(TVLIB_CXXFLAGS) -I$(QB_DIR) -I$(QB_DIR)bw,$(QB_EXTRA) $(TVLIB_TV) $(ARMDOS_CXXLIBS) $(ARMDOS_PRINTF_FLOAT) -lm $(ARMDOS_VFP),--stack 65536,EXE,$(QB_DIR),$(QB_OUT)))
$(QBIMG_ELF): $(QB_EXTRA) $(TVLIB_TV)

$(eval $(call tvlib_bind,$(BUILD)/QB.EXE,$(QB_OUT)/QBIMG.EXE))
ARMDOS_PROGRAMS += $(BUILD)/QB.EXE
all: $(BUILD)/QB.EXE

.PHONY: qb qb-test
qb: $(BUILD)/QB.EXE

# "make qb-test": boot ARM-DOS headless and drive QB (tests/run.mjs)
qb-test: $(BUILD)/QB.EXE $(BUILD)/COMMAND.COM $(BUILD)/MOUSE.COM $(BUILD)/HIMEM.SYS \
         $(BUILD)/MEM.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(QB_DIR)tests/run.mjs
test: qb-test
