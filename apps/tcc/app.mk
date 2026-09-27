# apps/tcc/app.mk - TCC.EXE: TinyCC (LGPL-2.1) as a native C compiler for
# ARM-DOS, plus everything it needs on C:\TC (see README.md):
#
#   build/TCC.EXE              the compiler, built by the SDK (runs on ARM-DOS;
#                              C:\DOS\TCC.EXE like every build/*.EXE)
#   build/tcc/host/armdos-tcc  the same compiler for Linux (tests, samples)
#   build/tcc/libc.a           libdos + newlib-nano libc/libm + libgcc, one archive
#   build/tcc/sys/             include/ + lib/ for armdos-tcc (long names)
#   build/tcc/disk/TC/         C:\TC as it goes on the hard disk (8.3 names):
#                              INCLUDE\ LIB\ SAMPLES\ README.TXT COPYING.TXT
#
# apps/tcc/hd.json puts build/tcc/disk/TC on drive C:.

TCX_DIR  := $(here)
TCX_SRC  := $(TCX_DIR)src
TCX_OUT  := $(BUILD)/tcc
TCX_GEN  := $(TCX_OUT)/gen
TCX_HOST := $(TCX_OUT)/host/armdos-tcc
TCX_LIBC := $(TCX_OUT)/libc.a
TCX_DEPS := $(wildcard $(TCX_SRC)/*.c $(TCX_SRC)/*.h $(TCX_SRC)/include/*.h)
TCX_HOSTCC ?= cc

# tccdefs.h as C strings (compiled into the compiler), as TinyCC's Makefile does
$(TCX_GEN)/tccdefs_.h: $(TCX_SRC)/include/tccdefs.h $(TCX_SRC)/conftest.c
	@mkdir -p $(dir $@)
	$(TCX_HOSTCC) -DC2STR $(TCX_SRC)/conftest.c -o $(TCX_GEN)/c2str
	$(TCX_GEN)/c2str $< $@

# --- the Linux-hosted cross compiler (same sources, TCC_HOST_ARMDOS unset)
$(TCX_HOST): $(TCX_DEPS) $(TCX_GEN)/tccdefs_.h
	@mkdir -p $(dir $@)
	$(TCX_HOSTCC) -O2 -g -w -I$(TCX_SRC) -I$(TCX_GEN) \
	    -DCONFIG_TCCDIR=\"$(abspath $(TCX_OUT)/sys)\" -o $@ $(TCX_SRC)/tcc.c -lm

# --- the run-time library: one archive, so one pass resolves everything.
# Debug info stripped (it is most of the size).
TCX_LIBGCC := $(shell $(ARMDOS_CC) $(ARMDOS_ARCH) -print-libgcc-file-name)
TCX_NEWLIB := $(dir $(shell $(ARMDOS_CC) $(ARMDOS_ARCH) --specs=nano.specs -print-file-name=libc_nano.a))
$(TCX_LIBC): $(ARMDOS_LIBDOS) $(TCX_LIBGCC)
	@mkdir -p $(dir $@)
	rm -f $@
	printf 'create %s\naddlib %s\naddlib %s\naddlib %s\naddlib %s\nsave\nend\n' \
	    $@ $(ARMDOS_LIBDOS) $(TCX_NEWLIB)libc_nano.a $(TCX_NEWLIB)libm.a $(TCX_LIBGCC) | $(ARMDOS_AR) -M
	arm-none-eabi-strip -g $@
	$(ARMDOS_AR) s $@

# --- system directory for the cross compiler
$(TCX_OUT)/sys.stamp: $(TCX_DIR)tools/mksys.mjs $(ARMDOS_SDK_HEADERS) $(wildcard $(TCX_SRC)/include/*.h $(TCX_DIR)include/*.h) \
                      $(TCX_LIBC) $(ARMDOS_CRT0)
	$(NODE) $(TCX_DIR)tools/mksys.mjs --host $(TCX_OUT)/sys
	mkdir -p $(TCX_OUT)/sys/lib
	cp $(TCX_LIBC) $(TCX_OUT)/sys/lib/libc.a
	cp $(ARMDOS_CRT0) $(TCX_OUT)/sys/lib/crt0.o
	touch $@

# --- TCC.EXE (runs on ARM-DOS; goes to C:\DOS, on the PATH). -w: TinyCC is
# not -Wextra clean. Linked twice: the second link adds the table of the C
# library symbols TCC.EXE exports to "TCC -run" programs (tools/mkhostsyms.mjs:
# a -run program shares TCC.EXE's run-time instead of loading a second one).
# printf %f is linked in, since -run programs use TCC.EXE's printf.
TCX_OBJ    := $(BUILD)/obj/TCC
TCX_CFLAGS := -DTCC_HOST_ARMDOS -I$(TCX_SRC) -I$(TCX_GEN) -w
TCX_LDLIBS := $(ARMDOS_PRINTF_FLOAT) -Wl,--start-group $(ARMDOS_LIBDOS) -lc -lm -lgcc -Wl,--end-group
TCX_SYMLIBS := $(ARMDOS_LIBDOS) $(TCX_NEWLIB)libc_nano.a $(TCX_NEWLIB)libm.a $(TCX_LIBGCC)

$(TCX_OBJ)/tcc.o: $(TCX_DEPS) $(TCX_GEN)/tccdefs_.h $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) $(TCX_CFLAGS) -c $(TCX_SRC)/tcc.c -o $@

$(TCX_OBJ)/hostsyms0.c: $(TCX_DIR)tools/mkhostsyms.mjs
	@mkdir -p $(dir $@)
	$(NODE) $< $@

$(TCX_OBJ)/%.o: $(TCX_OBJ)/%.c
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) -fno-builtin -w -c $< -o $@

$(TCX_OBJ)/TCC0.elf: $(TCX_OBJ)/tcc.o $(TCX_OBJ)/hostsyms0.o $(ARMDOS_SDK_DEPS)
	$(ARMDOS_CC) $(ARMDOS_LDFLAGS) -o $@ $(ARMDOS_CRT0) $(TCX_OBJ)/tcc.o $(TCX_OBJ)/hostsyms0.o $(TCX_LDLIBS)

$(TCX_OBJ)/hostsyms.c: $(TCX_OBJ)/TCC0.elf $(TCX_DIR)tools/mkhostsyms.mjs
	$(NODE) $(TCX_DIR)tools/mkhostsyms.mjs $@ $< $(TCX_SYMLIBS)

$(TCX_OBJ)/TCC.elf: $(TCX_OBJ)/tcc.o $(TCX_OBJ)/hostsyms.o $(ARMDOS_SDK_DEPS)
	$(ARMDOS_CC) $(ARMDOS_LDFLAGS) -Wl,-Map=$(@:.elf=.map) -o $@ $(ARMDOS_CRT0) $(TCX_OBJ)/tcc.o $(TCX_OBJ)/hostsyms.o $(TCX_LDLIBS)

$(BUILD)/TCC.EXE: $(TCX_OBJ)/TCC.elf $(ARMDOS_SDK)/elf2exe.mjs
	$(ARMDOS_ELF2EXE) --stack 65536 $< -o $@

ARMDOS_PROGRAMS += $(BUILD)/TCC.EXE
all: $(BUILD)/TCC.EXE
.PHONY: TCC
TCC: $(BUILD)/TCC.EXE

# --- C:\TC
TCX_SAMPLES := $(wildcard $(TCX_DIR)samples/*)
$(TCX_OUT)/disk.stamp: $(TCX_LIBC) $(ARMDOS_CRT0) $(TCX_DIR)tools/mksys.mjs \
                       $(TCX_DIR)tools/mkdisk.mjs $(ARMDOS_SDK_HEADERS) $(wildcard $(TCX_SRC)/include/*.h $(TCX_DIR)include/*.h) \
                       $(TCX_SAMPLES) $(TCX_DIR)README.TXT $(TCX_SRC)/COPYING
	$(NODE) $(TCX_DIR)tools/mkdisk.mjs $(TCX_OUT)
	touch $@

DISK_DEPS += $(TCX_OUT)/disk.stamp
all: $(TCX_OUT)/disk.stamp $(TCX_HOST) $(TCX_OUT)/sys.stamp

.PHONY: tcc tcc-test
tcc: $(BUILD)/TCC.EXE $(TCX_OUT)/disk.stamp $(TCX_HOST) $(TCX_OUT)/sys.stamp

# "make tcc-test": compile the samples and a test suite with TCC.EXE on ARM-DOS
# and compare with the cross compiler and with GCC (tests/run.mjs)
tcc-test: tcc $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin \
          $(BUILD)/COMMAND.COM $(BUILD)/HIMEM.SYS $(BUILD)/EDLIN.COM
	$(NODE) $(TCX_DIR)tests/run.mjs
test: tcc-test
