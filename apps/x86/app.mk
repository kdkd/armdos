# apps/x86/app.mk - ELBOW.EXE, "Emulated Legacy Binaries On Workstation":
# the ARM-DOS 8086 Compatibility Box (see README.md).  Also FILE.EXE.
X86_DIR  := $(here)
X86_SRCS := main.c cpu.c mem.c hle.c dos.c proc.c irq.c kbd86.c jit.c ems.c jitasm.S t22.S

$(call armdos_exe,ELBOW,$(X86_SRCS),-fno-strict-aliasing -DX86_JIT -Wno-array-bounds -Wno-format-truncation,,--stack 16384)

# the CPU core and the translator are the hot paths
$(BUILD)/obj/ELBOW/cpu.c.o $(BUILD)/obj/ELBOW/jit.c.o: ARMDOS_CFLAGS += -O2

# FILE.EXE: "what is this file?" (C:\DOS)
$(call armdos_exe,FILE,file/file.c)

# ---- the ELBOW demo (C:\ELBOW): nasm samples and the two-machine benchmark
ELBOW_OUT := $(BUILD)/elbow
$(call armdos_exe_to,BENCHARM,demo/bench.c,-Wno-format -Wno-array-bounds,,,$(ELBOW_OUT))

# BENCH86.EXE is the same bench.c built for 16-bit x86 DOS with Open Watcom v2.
# The build uses the prebuilt copy committed in demo/BENCH86.EXE; to rebuild it,
# install Open Watcom v2 (https://github.com/open-watcom/open-watcom-v2) and run
#     make WATCOM_DIR=/path/to/watcom build/elbow/BENCH86.EXE
# (WATCOM_DIR is the directory holding binl64/wcl; the new binary also replaces demo/BENCH86.EXE).
WATCOM_DIR ?=
$(ELBOW_OUT)/BENCH86.EXE: $(X86_DIR)demo/bench.c
	@mkdir -p $(dir $@)
	if [ -n "$(WATCOM_DIR)" ] && [ -x "$(WATCOM_DIR)/binl64/wcl" ]; then \
	  cd $(dir $@) && WATCOM=$(WATCOM_DIR) INCLUDE=$(WATCOM_DIR)/h PATH=$(WATCOM_DIR)/binl64:$$PATH \
	    wcl -q -bt=dos -ms -0 -ox -fe=BENCH86.EXE $(abspath $<) && rm -f bench.o && \
	  cp BENCH86.EXE $(abspath $(X86_DIR))/demo/BENCH86.EXE; \
	else cp $(X86_DIR)demo/BENCH86.EXE $@; fi

ELBOW_ASM := $(wildcard $(X86_DIR)demo/*.asm)
ELBOW_COMS := $(patsubst $(X86_DIR)demo/%.asm,$(ELBOW_OUT)/%.COM,$(ELBOW_ASM))
$(ELBOW_OUT)/%.COM: $(X86_DIR)demo/%.asm
	@mkdir -p $(dir $@)
	nasm -f bin -o $@.tmp $< && mv $@.tmp $@
elbow-demo: $(ELBOW_OUT)/BENCH86.EXE $(ELBOW_COMS) $(BENCHARM_OUT)
all: elbow-demo
DISK_DEPS += $(ELBOW_OUT)/BENCH86.EXE $(ELBOW_COMS) $(BENCHARM_OUT)
.PHONY: elbow-demo

# ---- tests: "make x86-test" (part of "make test")
X86_TEST_ASM := $(wildcard $(X86_DIR)tests/asm/*.asm)
X86_TEST_COMS := $(patsubst $(X86_DIR)tests/asm/%.asm,$(BUILD)/x86-test/%.com,$(X86_TEST_ASM))
$(BUILD)/x86-test/%.com: $(X86_DIR)tests/asm/%.asm
	@mkdir -p $(dir $@)
	nasm -f bin -o $@.tmp $< && mv $@.tmp $@

# the CPU conformance suite: the same cases as 16-bit DOS programs (CPUTn.COM)
# and as 32-bit Linux programs whose output on the host's own x86 CPU is the
# reference (skipped where the host cannot run i386 code)
$(BUILD)/x86-cpu/.stamp: $(X86_DIR)tests/cpu/gen.py
	@rm -rf $(BUILD)/x86-cpu && mkdir -p $(BUILD)/x86-cpu
	cd $(BUILD)/x86-cpu && python3 $(abspath $<) . > groups.txt && for k in $$(cat groups.txt); do \
	  nasm -f bin -o CPUT$$k.COM cput$$k.asm || exit 1; \
	  if nasm -f elf32 -o cput$$k.o cput$${k}_elf.asm && ld -m elf_i386 -o cput$$k cput$$k.o 2>/dev/null && ./cput$$k > ref$$k.txt; then :; \
	  else echo "x86-cpu: no i386 reference on this host, the CPU suite will be skipped"; rm -f templates.txt; break; fi; done
	@touch $@

.PHONY: x86-test
x86-test: $(BUILD)/ELBOW.EXE $(BUILD)/FILE.EXE $(BUILD)/HIMEM.SYS $(BUILD)/MOUSE.COM $(X86_TEST_COMS) $(BUILD)/x86-cpu/.stamp elbow-demo \
          $(BUILD)/ktest/TSHELL.EXE $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE \
          $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(X86_DIR)tests/run.mjs
	$(NODE) $(X86_DIR)tests/jitmath.mjs
	$(NODE) $(X86_DIR)tests/jitcool.mjs
test: x86-test

# the inspector's ELBOW view: the x86 disassembler (web/js/x86disasm.js) against
# ndisasm on real binaries, and ELBOW's descriptor on ports FCh-FFh (ARCH.md 4.7)
.PHONY: elbowview-test
elbowview-test: $(BUILD)/ELBOW.EXE elbow-demo $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(X86_DIR)tests/elbowview.mjs
test: elbowview-test
