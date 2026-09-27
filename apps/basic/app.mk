# apps/basic/app.mk - BASIC.EXE: Bywater BASIC 3.40 (GPL-2) in its GW-BASIC
# dialect, with an ARM-DOS screen/graphics/sound front end (bwx_dos.c,
# dosvid.c). Goes to C:\DOS (on the PATH). See README.md.
BASIC_SRCS := src/bwbasic.c src/bwb_cmd.c src/bwb_cnd.c src/bwb_dio.c src/bwb_exp.c \
              src/bwb_fnc.c src/bwb_inp.c src/bwb_int.c src/bwb_prn.c src/bwb_stc.c \
              src/bwb_str.c src/bwb_tbl.c src/bwb_var.c src/bwd_cmd.c src/bwd_fun.c \
              bwx_dos.c dosvid.c
BASIC_CFLAGS := -DARMDOS=1 -DHAVE_MSDOS=1 -I$(here) -I$(here)src \
                -DDEF_PROMPT='"Ok\n"' -DDEF_EXTENSION='".BAS"' -DPROFILENAME='"PROFILE.BAS"' \
                -Wno-sign-compare -Wno-unused-but-set-variable -Wno-unused-variable \
                -Wno-implicit-fallthrough -Wno-missing-field-initializers -Wno-array-bounds \
                -Wno-char-subscripts -Wno-format-truncation -Wno-misleading-indentation
$(call armdos_exe,BASIC,$(BASIC_SRCS),$(BASIC_CFLAGS),$(ARMDOS_PRINTF_FLOAT) -lm,--stack 65536)

# "make basic-test": regression checks of BASIC.EXE (tests/run.mjs)
.PHONY: basic-test
basic-test: $(BUILD)/BASIC.EXE $(BUILD)/COMMAND.COM $(BUILD)/MOUSE.COM $(BUILD)/HIMEM.SYS $(BUILD)/MEM.EXE \
            $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) apps/basic/tests/run.mjs
test: basic-test
