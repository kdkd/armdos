# apps/tc/app.mk - TC.EXE, "ARM Turbo C": a Turbo-C-style IDE on Turbo Vision
# with TinyCC (apps/tcc) linked in as its compiler. See README.md.
#
#   build/tc/TCIMG.EXE   the IDE (an ordinary ARM-DOS EXE image)
#   build/TC.EXE         apps/tvlib's XMS loader stub + that image: the IDE
#                        runs from extended memory (C:\DOS\TC.EXE)
#   build/tc/tchelp.c    the help text (data/tchelp.txt) as a C string

TC_DIR := $(here)
include $(TC_DIR)../tvlib/tvlib.mk

TC_OUT  := $(BUILD)/tc
TC_OBJ  := $(BUILD)/obj/TCIMG
TC_SRCS := tc.cpp tcwin.cpp tcdlg.cpp tcbuild.cpp tchelp.cpp

# the compiler: TinyCC's one-source build as a library (tcomp.c, LGPL-2.1);
# apps/tcc/app.mk (read after this file) makes build/tcc/gen/tccdefs_.h
TC_TCCSRC := $(TC_DIR)../tcc/src
TC_TCCGEN := $(BUILD)/tcc/gen
$(TC_OBJ)/tcomp.o: $(TC_DIR)tcomp.c $(TC_DIR)tcomp.h $(wildcard $(TC_TCCSRC)/*.c $(TC_TCCSRC)/*.h) \
                   $(TC_TCCGEN)/tccdefs_.h $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) -DTCC_HOST_ARMDOS -I$(TC_TCCSRC) -I$(TC_TCCGEN) -w -c $< -o $@

$(TC_OUT)/tchelp.c: $(TC_DIR)data/tchelp.txt $(TVLIB_DIR)tools/mkhelp.mjs
	@mkdir -p $(dir $@)
	$(NODE) $(TVLIB_DIR)tools/mkhelp.mjs $< $@ tcHelpText
$(TC_OBJ)/tchelp.o: $(TC_OUT)/tchelp.c
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) -c $< -o $@

TC_EXTRA := $(TC_OBJ)/tcomp.o $(TC_OBJ)/tchelp.o $(TVLIB_OBJS)
$(eval $(call _armdos_program,TCIMG,$(TC_SRCS),$(TVLIB_CXXFLAGS) -I$(TC_DIR),$(TC_EXTRA) $(TVLIB_TV) $(ARMDOS_CXXLIBS) -lm,--stack 65536,EXE,$(TC_DIR),$(TC_OUT)))
$(TCIMG_ELF): $(TC_EXTRA) $(TVLIB_TV)

$(eval $(call tvlib_bind,$(BUILD)/TC.EXE,$(TC_OUT)/TCIMG.EXE))
ARMDOS_PROGRAMS += $(BUILD)/TC.EXE
all: $(BUILD)/TC.EXE

.PHONY: tc tc-test
tc: $(BUILD)/TC.EXE

# "make tc-test": boot ARM-DOS headless and drive TC (tests/run.mjs)
tc-test: $(BUILD)/TC.EXE $(BUILD)/tcc/disk.stamp $(BUILD)/COMMAND.COM $(BUILD)/MOUSE.COM $(BUILD)/HIMEM.SYS \
         $(BUILD)/MEM.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(TC_DIR)tests/run.mjs
test: tc-test
