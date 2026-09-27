# command/command.mk - COMMAND.COM (included by the top-level Makefile).
#
# build/COMMAND.COM is an MZ+AR1 program (ARCH.md 8; DOS loads by signature,
# so the .COM name is fine), linked with the SDK but with its own start-up
# (command/start.S) instead of the C runtime's: COMMAND manages its own
# memory and environment and must not grow a heap.

CMD_DIR   := command
CMD_OBJ   := $(BUILD)/obj/COMMAND
CMD_OUT   := $(BUILD)/COMMAND.COM
CMD_TESTOUT := $(BUILD)/cmdtest
CMD_CFLAGS := -Wno-unused-function

# the transient part: the command processor, an AR1 image with no initialised
# writable data (its checksum tells whether a program overwrote it)
CMD_TSRCS := tstart.S main.c dos.c output.c messages.c parse.c env.c exec.c \
             batch.c cmds1.c cmds2.c copy.c eggs.c
CMD_TOBJS := $(patsubst %,$(CMD_OBJ)/%.o,$(CMD_TSRCS))
CMD_TELF  := $(CMD_OBJ)/transient.elf
CMD_TAR1  := $(CMD_OBJ)/transient.ar1

# the resident part: start-up, INT 22h/23h/24h, EXEC, the transient loader
CMD_RSRCS := resstart.S res.c
CMD_ROBJS := $(patsubst %,$(CMD_OBJ)/%.o,$(CMD_RSRCS)) $(CMD_OBJ)/blob.S.o
CMD_RELF  := $(CMD_OBJ)/COMMAND.elf

$(CMD_OBJ)/%.c.o: $(CMD_DIR)/%.c $(CMD_DIR)/cmd.h $(CMD_DIR)/res.h $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) $(CMD_CFLAGS) -c $< -o $@

$(CMD_OBJ)/%.S.o: $(CMD_DIR)/%.S
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $@

$(CMD_TELF): $(CMD_TOBJS) $(ARMDOS_LIBDOS) $(ARMDOS_SDK)/link.ld
	$(ARMDOS_CC) $(ARMDOS_LDFLAGS) -Wl,-Map=$(@:.elf=.map) -o $@ $(CMD_TOBJS) $(ARMDOS_LIBS)
	@d=$$(arm-none-eabi-objdump -t $@ | awk '$$4 == ".data" && $$5 != "00000000"'); \
	 if [ -n "$$d" ]; then echo "COMMAND transient: no initialised writable data allowed:"; \
	   echo "$$d"; rm -f $@; exit 1; fi

$(CMD_TAR1): $(CMD_TELF) $(ARMDOS_SDK)/elf2exe.mjs
	$(ARMDOS_ELF2EXE) --ar1 --stack 10240 $< -o $@

$(CMD_OBJ)/blob.S.o: $(CMD_DIR)/blob.S $(CMD_TAR1)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -DTRANSIENT_AR1='"$(CMD_TAR1)"' -c $< -o $@

$(CMD_RELF): $(CMD_ROBJS) $(ARMDOS_LIBDOS) $(CMD_DIR)/res.ld
	$(ARMDOS_CC) $(ARMDOS_LDFLAGS:$(ARMDOS_SDK)/link.ld=$(CMD_DIR)/res.ld) -Wl,-Map=$(@:.elf=.map) -o $@ \
	    $(CMD_ROBJS) $(ARMDOS_LIBS)

$(CMD_OUT): $(CMD_RELF) $(ARMDOS_SDK)/elf2exe.mjs
	$(ARMDOS_ELF2EXE) --stack 0 $< -o $@

.PHONY: command command-test
command: $(CMD_OUT)
all: $(CMD_OUT)
DISK_DEPS += $(CMD_OUT)

$(CMD_TESTOUT)/fd.img: $(CMD_DIR)/tests/fd.json $(CMD_OUT) $(CMD_DIR)/tests/disk/AUTOEXEC.BAT \
                       $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS disk/mkimage.mjs
	@mkdir -p $(dir $@)
	$(NODE) disk/mkimage.mjs build $(CMD_DIR)/tests/fd.json -o $@

$(CMD_TESTOUT)/fdn.img: $(CMD_DIR)/tests/fdn.json $(CMD_OUT) $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS disk/mkimage.mjs
	@mkdir -p $(dir $@)
	$(NODE) disk/mkimage.mjs build $(CMD_DIR)/tests/fdn.json -o $@

command-test: $(CMD_OUT) $(CMD_TESTOUT)/hd.img $(CMD_TESTOUT)/fd.img $(CMD_TESTOUT)/fdn.img
	$(NODE) $(CMD_DIR)/tests/run.mjs

test: command-test

# ---- test programs (ARM twins of command/tests/ref/x86/*.asm) and the test disk
$(call armdos_com_to,CTRET,tests/progs/ret.c,,,,$(CMD_TESTOUT))
$(call armdos_com_to,CTARGS,tests/progs/args.c,,,,$(CMD_TESTOUT))
$(call armdos_com_to,CTUPCASE,tests/progs/upcase.c,,,,$(CMD_TESTOUT))
$(call armdos_com_to,CTTRASH,tests/progs/trash.c,,,,$(CMD_TESTOUT))
$(call armdos_com_to,CTWAITKEY,tests/progs/waitkey.c,,,,$(CMD_TESTOUT))
CMD_TESTPROGS := $(CTRET_OUT) $(CTARGS_OUT) $(CTUPCASE_OUT) $(CTTRASH_OUT) $(CTWAITKEY_OUT)
CMD_TESTTREE := $(shell find $(CMD_DIR)/tests/disk -type f 2>/dev/null)

$(CMD_TESTOUT)/hd.img: $(CMD_DIR)/tests/hd.json $(CMD_OUT) $(CMD_TESTPROGS) $(CMD_TESTTREE) \
                       $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS disk/mkimage.mjs
	@mkdir -p $(dir $@)
	$(NODE) disk/mkimage.mjs build $(CMD_DIR)/tests/hd.json -o $@
