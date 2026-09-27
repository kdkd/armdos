# sdk/sdk.mk - the ARM-DOS SDK: crt0 + libdos, and rules for building programs.
#
# Included from the top-level Makefile. An application needs only an app.mk:
#
#     # apps/hello/app.mk
#     $(call armdos_exe,HELLO,hello.c util.c)
#
# which builds build/HELLO.EXE from sources relative to the app.mk's
# directory. Full form:
#
#     $(call armdos_exe,NAME,SOURCES[,CFLAGS[,LDFLAGS/LIBS[,ELF2EXE-OPTIONS]]])
#     $(call armdos_com,NAME,SOURCES[,...])      -> build/NAME.COM (raw, self-relocating)
#
#   CFLAGS      extra compiler flags for this program (e.g. -DFOO -Iinclude)
#   LDFLAGS     extra link flags/libs; $(ARMDOS_PRINTF_FLOAT) enables %f/%e/%g
#               in printf/scanf (newlib-nano leaves float formatting out)
#   $(ARMDOS_VFP) in CFLAGS: hardware floating point (VFP, softfp ABI)
#   ELF2EXE     e.g. --stack 16384 --min-extra 65536 --flags 2
#
# Sources may be .c, .cpp, .S or .s (C++: see ARMDOS_CXXFLAGS/ARMDOS_CXXLIBS).
# Intermediate files go to build/obj/NAME/, the
# ELF and a map file to build/obj/NAME/NAME.elf / .map.

BUILD ?= build

ARMDOS_CC      := arm-none-eabi-gcc
ARMDOS_AR      := arm-none-eabi-ar
ARMDOS_OBJDUMP := arm-none-eabi-objdump
NODE           ?= node

ARMDOS_SDK     := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ARMDOS_SDKOUT  := $(BUILD)/sdk
ARMDOS_ARCH    := -marm -march=armv5te -mfloat-abi=soft
ARMDOS_CFLAGS  := $(ARMDOS_ARCH) -Os -g -Wall -Wextra -Wno-unused-parameter \
                  -ffunction-sections -fdata-sections -fno-common \
                  --specs=nano.specs -isystem $(ARMDOS_SDK)/include
ARMDOS_ASFLAGS := $(ARMDOS_ARCH) -g -isystem $(ARMDOS_SDK)/include
# C++ (.cpp sources): no exceptions, no RTTI, no thread-safe statics (DOS is
# single-threaded). Link C++ programs with $(ARMDOS_CXXLIBS) in LDFLAGS.
ARMDOS_CXX      := arm-none-eabi-g++
ARMDOS_CXXFLAGS  = $(ARMDOS_CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
ARMDOS_CXXLIBS  := -lstdc++ -lsupc++
ARMDOS_LDFLAGS := $(ARMDOS_ARCH) --specs=nano.specs -nostartfiles -nostdlib \
                  -T $(ARMDOS_SDK)/link.ld -Wl,-q -Wl,--gc-sections \
                  -Wl,--no-warn-rwx-segments -Wl,--target2=rel \
                  -Wl,-u,__aeabi_idiv0 -Wl,-u,__aeabi_ldiv0
ARMDOS_CRT0    := $(ARMDOS_SDKOUT)/crt0.o
ARMDOS_LIBDOS  := $(ARMDOS_SDKOUT)/libdos.a
ARMDOS_LIBS    := -Wl,--start-group $(ARMDOS_LIBDOS) -lc -lgcc -Wl,--end-group
ARMDOS_ELF2EXE := $(NODE) $(ARMDOS_SDK)/elf2exe.mjs
ARMDOS_EXEINFO := $(NODE) $(ARMDOS_SDK)/exeinfo.mjs

ARMDOS_PRINTF_FLOAT := -Wl,-u,_printf_float -Wl,-u,_scanf_float

# Opt-in hardware floating point (the ARM926's VFP9-S, VFPv2, which the ROM
# BIOS enables): give $(ARMDOS_VFP) in a program's CFLAGS. The code then uses
# VFP instructions with the soft-float calling convention ("softfp"), so it
# links with libdos/crt0 (built soft) unchanged, and the link picks newlib's
# arm/v5te/softfp multilib instead of the default one. Programs without it
# are built exactly as before.
ARMDOS_VFP := -mfpu=vfp -mfloat-abi=softfp
ARMDOS_LDFLAGS_VFP = $(filter-out -mfloat-abi=soft,$(ARMDOS_LDFLAGS)) $(ARMDOS_VFP)

# The directory of the makefile currently being read (use inside app.mk).
here = $(dir $(lastword $(filter %.mk,$(MAKEFILE_LIST))))

# Every program built with armdos_exe/armdos_com (for disk images).
ARMDOS_PROGRAMS :=

# ------------------------------------------------------------------ libdos --
ARMDOS_LIBDOS_SRCS := $(wildcard $(ARMDOS_SDK)/libdos/*.c $(ARMDOS_SDK)/libdos/*.S)
ARMDOS_LIBDOS_OBJS := $(patsubst $(ARMDOS_SDK)/libdos/%,$(ARMDOS_SDKOUT)/libdos/%.o,$(ARMDOS_LIBDOS_SRCS))
ARMDOS_SDK_HEADERS := $(wildcard $(ARMDOS_SDK)/include/*.h $(ARMDOS_SDK)/libdos/*.h)

$(ARMDOS_SDKOUT)/libdos/%.c.o: $(ARMDOS_SDK)/libdos/%.c $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) -I$(ARMDOS_SDK)/libdos -c $< -o $@

$(ARMDOS_SDKOUT)/libdos/%.S.o: $(ARMDOS_SDK)/libdos/%.S
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $@

$(ARMDOS_LIBDOS): $(ARMDOS_LIBDOS_OBJS)
	@rm -f $@
	$(ARMDOS_AR) rcs $@ $^

$(ARMDOS_CRT0): $(ARMDOS_SDK)/crt0.S
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $@

ARMDOS_SDK_DEPS := $(ARMDOS_CRT0) $(ARMDOS_LIBDOS) $(ARMDOS_SDK)/link.ld $(ARMDOS_SDK)/elf2exe.mjs

.PHONY: sdk
sdk: $(ARMDOS_CRT0) $(ARMDOS_LIBDOS)
all: sdk

# ------------------------------------------------------------ programs ------
# $(1)=NAME $(2)=SOURCES $(3)=CFLAGS $(4)=LDFLAGS $(5)=ELF2EXE options
# $(6)=EXE|COM $(7)=source directory $(8)=output directory
define _armdos_program
$(1)_SRCDIR := $(7)
$(1)_OUT    := $(8)/$(1).$(6)
$(1)_ELF    := $(BUILD)/obj/$(1)/$(1).elf
$(1)_OBJS   := $$(patsubst %,$(BUILD)/obj/$(1)/%.o,$(2))

$(BUILD)/obj/$(1)/%.c.o: $(7)%.c $$(ARMDOS_SDK_HEADERS)
	@mkdir -p $$(dir $$@)
	$$(ARMDOS_CC) $$(ARMDOS_CFLAGS) $(3) -MMD -MP -c $$< -o $$@

$(BUILD)/obj/$(1)/%.cpp.o: $(7)%.cpp $$(ARMDOS_SDK_HEADERS)
	@mkdir -p $$(dir $$@)
	$$(ARMDOS_CXX) $$(ARMDOS_CXXFLAGS) $(3) -MMD -MP -c $$< -o $$@

$(BUILD)/obj/$(1)/%.S.o: $(7)%.S
	@mkdir -p $$(dir $$@)
	$$(ARMDOS_CC) $$(ARMDOS_ASFLAGS) $(3) -MMD -MP -c $$< -o $$@

$(BUILD)/obj/$(1)/%.s.o: $(7)%.s
	@mkdir -p $$(dir $$@)
	$$(ARMDOS_CC) $$(ARMDOS_ASFLAGS) $(3) -c $$< -o $$@

$$($(1)_ELF): $$($(1)_OBJS) $$(ARMDOS_SDK_DEPS)
	$$(ARMDOS_CC) $$(if $$(findstring -mfloat-abi=softfp,$(3) $(4)),$$(ARMDOS_LDFLAGS_VFP),$$(ARMDOS_LDFLAGS)) -Wl,-Map=$$(@:.elf=.map) -o $$@ \
	    $$(ARMDOS_CRT0) $$($(1)_OBJS) $(4) $$(ARMDOS_LIBS)

$$($(1)_OUT): $$($(1)_ELF) $$(ARMDOS_SDK)/elf2exe.mjs
	@mkdir -p $$(dir $$@)
	$$(ARMDOS_ELF2EXE) $(if $(filter COM,$(6)),--com --selfreloc) $(5) $$< -o $$@

-include $$($(1)_OBJS:.o=.d)
ARMDOS_PROGRAMS += $$($(1)_OUT)
.PHONY: $(1)
$(1): $$($(1)_OUT)
endef

armdos_exe = $(eval $(call _armdos_program,$(strip $(1)),$(strip $(2)),$(3),$(4),$(5),EXE,$(here),$(BUILD)))$(eval all: $(BUILD)/$(strip $(1)).EXE)
armdos_com = $(eval $(call _armdos_program,$(strip $(1)),$(strip $(2)),$(3),$(4),$(5),COM,$(here),$(BUILD)))$(eval all: $(BUILD)/$(strip $(1)).COM)
# Same, but not part of "make all" and written to a chosen directory.
armdos_exe_to = $(eval $(call _armdos_program,$(strip $(1)),$(strip $(2)),$(3),$(4),$(5),EXE,$(here),$(6)))
armdos_com_to = $(eval $(call _armdos_program,$(strip $(1)),$(strip $(2)),$(3),$(4),$(5),COM,$(here),$(6)))

# --------------------------------------------------------------- tests ------
ARMDOS_TESTOUT := $(BUILD)/sdk-tests
$(call armdos_exe_to,T_HELLO,tests/hello.c,,,,$(ARMDOS_TESTOUT))
$(call armdos_exe_to,T_FILES,tests/files.c,,,--stack 16384,$(ARMDOS_TESTOUT))
$(call armdos_exe_to,T_INTS,tests/ints.c,,,,$(ARMDOS_TESTOUT))
$(call armdos_exe_to,T_FLOAT,tests/float.c,,$(ARMDOS_PRINTF_FLOAT),,$(ARMDOS_TESTOUT))
$(call armdos_com_to,T_HELLOC,tests/hello.c,,,,$(ARMDOS_TESTOUT))

# a hand-written position-independent .COM (no crt0/libc): plain --com mode
$(ARMDOS_TESTOUT)/T_PIC.COM: $(ARMDOS_SDK)/tests/pic.S $(ARMDOS_SDK)/elf2exe.mjs $(ARMDOS_SDK)/link.ld
	@mkdir -p $(BUILD)/obj/T_PIC $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $(BUILD)/obj/T_PIC/pic.o
	$(ARMDOS_CC) $(ARMDOS_ARCH) -nostdlib -nostartfiles -T $(ARMDOS_SDK)/link.ld -Wl,-q \
	    -Wl,--no-warn-rwx-segments -o $(BUILD)/obj/T_PIC/T_PIC.elf $(BUILD)/obj/T_PIC/pic.o
	$(ARMDOS_ELF2EXE) --com $(BUILD)/obj/T_PIC/T_PIC.elf -o $@
T_PIC_OUT := $(ARMDOS_TESTOUT)/T_PIC.COM

ARMDOS_TEST_BINS := $(T_HELLO_OUT) $(T_FILES_OUT) $(T_INTS_OUT) $(T_FLOAT_OUT) $(T_HELLOC_OUT) $(T_PIC_OUT)

.PHONY: sdk-test
sdk-test: $(ARMDOS_TEST_BINS)
	$(NODE) $(ARMDOS_SDK)/tests/run-tests.mjs $(ARMDOS_TEST_BINS)
test: sdk-test

# ARM + Thumb in one program: interworking veneers, relocated by elf2exe, run on the machine
$(call armdos_exe_to,T_THUMB,tests/thumb.c,,,,$(ARMDOS_TESTOUT))
.PHONY: sdk-thumb-test
sdk-thumb-test: $(ARMDOS_TESTOUT)/T_THUMB.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin $(BUILD)/ktest/TSHELL.EXE
	$(NODE) $(ARMDOS_SDK)/tests/thumb-run.mjs $(ARMDOS_TESTOUT)/T_THUMB.EXE $(BUILD)/obj/T_THUMB/T_THUMB.elf
test: sdk-thumb-test
