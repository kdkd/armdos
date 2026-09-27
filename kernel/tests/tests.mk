# kernel/tests/tests.mk - kernel test programs (build/ktest/) and "make kernel-test"
KTEST_OUT := $(BUILD)/ktest
KTEST_PROGS :=
define ktest_prog
$(call armdos_exe_to,$(1),$(2),$(3),$(4),$(5),$(KTEST_OUT))
KTEST_PROGS += $(KTEST_OUT)/$(1).EXE
endef
$(eval $(call ktest_prog,TSHELL,tshell.c,-I$(here)))
$(eval $(call ktest_prog,K_HELLO,k_hello.c,-I$(here)))
$(eval $(call ktest_prog,K_FILES,k_files.c,-I$(here)))

$(eval $(call ktest_prog,K_EXEC,k_exec.c,-I$(here)))
$(eval $(call ktest_prog,K_MEM,k_mem.c,-I$(here)))
$(eval $(call ktest_prog,K_CON,k_con.c,-I$(here)))
$(eval $(call ktest_prog,K_FLOP,k_flop.c,-I$(here)))
$(eval $(call ktest_prog,K_XMS,k_xms.c,-I$(here)))
$(eval $(call ktest_prog,K_DRVT,k_drvt.c,-I$(here)))
$(eval $(call ktest_prog,K_TSR,k_tsr.c,-I$(here)))
$(eval $(call ktest_prog,K_MAP,k_map.c,-I$(here)))
$(eval $(call ktest_prog,K_FCB,k_fcb.c,-I$(here)))
$(eval $(call ktest_prog,K_MISC,k_misc.c,-I$(here)))
$(eval $(call ktest_prog,K_STRESS,k_stress.c,-I$(here)))
$(eval $(call ktest_prog,K_ROUND2,k_round2.c,-I$(here)))
$(eval $(call ktest_prog,K_ROUND3,k_round3.c,-I$(here)))
$(eval $(call ktest_prog,K_ROUND4,k_round4.c,-I$(here)))
$(eval $(call ktest_prog,K_NLS,k_nls.c,-I$(here)))

# hand-written raw .COM programs
KTEST_DIR := $(here)
$(KTEST_OUT)/%.COM: $(KTEST_DIR)%.S $(ARMDOS_SDK)/elf2exe.mjs
	@mkdir -p $(BUILD)/obj/ktest $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $(BUILD)/obj/ktest/$*.o
	$(ARMDOS_CC) $(ARMDOS_ARCH) -nostdlib -nostartfiles -T $(ARMDOS_SDK)/link.ld -Wl,-q \
	    -Wl,--no-warn-rwx-segments -o $(BUILD)/obj/ktest/$*.elf $(BUILD)/obj/ktest/$*.o
	$(ARMDOS_ELF2EXE) --com $(BUILD)/obj/ktest/$*.elf -o $@
KTEST_PROGS += $(KTEST_OUT)/K_RET.COM $(KTEST_OUT)/K_CALL5.COM

# test device drivers (freestanding AR1 images with a device header)
$(K_OUT)/ktest-drv/%.o: $(KTEST_DIR)drv/%.c $(K_HDRS)
	@mkdir -p $(dir $@)
	$(K_CC) $(K_CFLAGS) -c $< -o $@
$(K_OUT)/ktest-drv/klib.o: kernel/lib/klib.c $(K_HDRS)
	@mkdir -p $(dir $@)
	$(K_CC) $(K_CFLAGS) -c $< -o $@
$(K_OUT)/ktest-drv/karm.o: kernel/lib/karm.S
	@mkdir -p $(dir $@)
	$(K_CC) $(K_ARCH) -c $< -o $@
$(KTEST_OUT)/K_UCON.SYS: $(K_OUT)/ktest-drv/ucon.o $(K_OUT)/ktest-drv/klib.o $(K_OUT)/ktest-drv/karm.o
	$(K_CC) $(K_CFLAGS) -T sdk/link.ld -Wl,--entry=0 -Wl,-q -Wl,--use-blx -Wl,--gc-sections -Wl,--no-warn-rwx-segments -o $@.elf $^ -lgcc
	$(NODE) sdk/elf2exe.mjs --ar1 --stack 0 $@.elf -o $@
$(KTEST_OUT)/K_RAMD.SYS: $(K_OUT)/ktest-drv/ramd.o $(K_OUT)/ktest-drv/klib.o $(K_OUT)/ktest-drv/karm.o
	$(K_CC) $(K_CFLAGS) -T sdk/link.ld -Wl,--entry=0 -Wl,-q -Wl,--use-blx -Wl,--gc-sections -Wl,--no-warn-rwx-segments -o $@.elf $^ -lgcc
	$(NODE) sdk/elf2exe.mjs --ar1 --stack 0 $@.elf -o $@
KTEST_PROGS += $(KTEST_OUT)/K_UCON.SYS $(KTEST_OUT)/K_RAMD.SYS

.PHONY: kernel-test ktest-progs
ktest-progs: $(KTEST_PROGS) $(KERNEL_OUT)
kernel-test: ktest-progs $(BUILD)/COUNTRY.SYS $(BUILD)/NLSFUNC.EXE
	$(NODE) kernel/tests/run.mjs
test: kernel-test
