# apps/edit/app.mk - EDIT.EXE, the ARM-DOS Editor: an MS-DOS-EDIT-like
# full-screen editor built on Turbo Vision (magiblot's C++ port, MIT; the
# vendored copy is in tvision/, our ARM-DOS platform layer in armdos/).
# See README.md.

EDIT_DIR := $(here)

# Turbo Vision's library sources go into an archive, so that only what the
# editor uses is linked (and with it only the libstdc++ parts it needs).
# The s*.cpp files only register classes for TV's object streams (unused
# here) and are left out, except snprintf/stddlg/strmstat/syserr.
EDIT_TV_ALL  := $(notdir $(wildcard $(EDIT_DIR)tvision/source/tvision/*.cpp))
EDIT_TV_SKIP := $(filter-out snprintf.cpp stddlg.cpp strmstat.cpp syserr.cpp,$(filter s%.cpp,$(EDIT_TV_ALL)))
EDIT_TV_SRCS := $(addprefix tvision/source/tvision/,$(filter-out $(EDIT_TV_SKIP),$(EDIT_TV_ALL))) \
                tvision/source/platform/strings.cpp tvision/source/platform/colors.cpp
EDIT_TV_OBJS := $(patsubst %,$(BUILD)/obj/EDIT/%.o,$(EDIT_TV_SRCS))
EDIT_TV_LIB  := $(BUILD)/obj/EDIT/libtv.a

EDIT_PLAT_SRCS := armdos/hardware.cpp armdos/ttext.cpp armdos/dosdir.cpp armdos/cxxrt.cpp
EDIT_APP_SRCS  := edit.cpp dialogs.cpp help.cpp

EDIT_CXXFLAGS := -std=gnu++17 -D__ARMDOS__ \
                 -I$(EDIT_DIR)tvision/include -I$(EDIT_DIR)tvision/include/tvision \
                 -I$(EDIT_DIR)tvision/include/tvision/compat/borland \
                 -I$(EDIT_DIR)tvision/include/tvision/compat/windows \
                 -I$(EDIT_DIR)armdos \
                 -Wno-deprecated-declarations -Wno-unused-variable -Wno-sign-compare \
                 -Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-parentheses \
                 -Wno-class-memaccess -Wno-unused-but-set-variable

$(call armdos_exe,EDIT,$(EDIT_PLAT_SRCS) $(EDIT_APP_SRCS),$(EDIT_CXXFLAGS),$(EDIT_TV_LIB) $(ARMDOS_CXXLIBS),--stack 32768)

$(EDIT_TV_LIB): $(EDIT_TV_OBJS)
	@rm -f $@
	$(ARMDOS_AR) rcs $@ $^
$(EDIT_ELF): $(EDIT_TV_LIB)
-include $(EDIT_TV_OBJS:.o=.d)

# EDIT.HLP (C:\DOS, next to EDIT.EXE) from data/help.txt
EDIT_OUT := $(BUILD)/edit
$(EDIT_OUT)/EDIT.HLP: $(EDIT_DIR)data/help.txt $(EDIT_DIR)tools/mkhlp.mjs
	@mkdir -p $(EDIT_OUT)
	$(NODE) $(EDIT_DIR)tools/mkhlp.mjs $< $@
all: $(EDIT_OUT)/EDIT.HLP
DISK_DEPS += $(EDIT_OUT)/EDIT.HLP $(BUILD)/EDIT.EXE

# "make edit-test": boot ARM-DOS headless and drive EDIT (tests/run.mjs)
.PHONY: edit-test
edit-test: $(BUILD)/EDIT.EXE $(EDIT_OUT)/EDIT.HLP $(BUILD)/COMMAND.COM $(BUILD)/MOUSE.COM $(BUILD)/HIMEM.SYS \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(EDIT_DIR)tests/run.mjs
test: edit-test
