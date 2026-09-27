# kernel/himem/himem.mk - build/HIMEM.SYS (AR1 image beginning with its device header)
HIMEM_SRC := kernel/himem/himem.c kernel/himem/entry.S kernel/lib/klib.c kernel/lib/karm.S
HIMEM_OBJ := $(patsubst kernel/%,$(K_OUT)/himem-obj/%.o,$(HIMEM_SRC))
$(K_OUT)/himem-obj/%.c.o: kernel/%.c $(K_HDRS)
	@mkdir -p $(dir $@)
	$(K_CC) $(K_CFLAGS) -c $< -o $@
$(K_OUT)/himem-obj/%.S.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(K_CC) $(K_ARCH) -c $< -o $@
$(K_OUT)/HIMEM.elf: $(HIMEM_OBJ) sdk/link.ld
	$(K_CC) $(K_CFLAGS) -T sdk/link.ld -Wl,--entry=himem_header -Wl,-q -Wl,--use-blx -Wl,--gc-sections \
	    -Wl,--no-warn-rwx-segments -Wl,-Map=$(K_OUT)/HIMEM.map -o $@ $(HIMEM_OBJ) -lgcc
$(BUILD)/HIMEM.SYS: $(K_OUT)/HIMEM.elf sdk/elf2exe.mjs
	$(NODE) sdk/elf2exe.mjs --ar1 --stack 0 $< -o $@
KERNEL_EXTRA += $(BUILD)/HIMEM.SYS
