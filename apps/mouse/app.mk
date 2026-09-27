# apps/mouse/app.mk - MOUSE.COM, a Microsoft-compatible mouse driver (INT 33h TSR).
#
# The resident part (mres.S, mres.c) is placed in .text.unlikely.* sections,
# which the SDK link script puts right after crt0's _start and before all other
# code; mend.S marks its end. The installer keeps PSP..mouse_res_end with INT
# 21h AH=31h. tests/run.mjs checks that the resident part is self-contained.
MOUSE_DIR := $(here)
MOUSE_CFLAGS := -fno-jump-tables -fno-tree-loop-distribute-patterns -fno-reorder-functions \
                -Wno-array-bounds -fno-delete-null-pointer-checks
$(call armdos_com,MOUSE,mres.S mres.c mend.S mouse.c,$(MOUSE_CFLAGS))

# tests: build/mouse-test/MTEST.EXE (a program that shows the cursor and reports clicks)
$(call armdos_exe_to,MTEST,tests/mtest.c,-Wno-array-bounds,,,$(BUILD)/mouse-test)
.PHONY: mouse-test
mouse-test: $(MOUSE_OUT) $(MTEST_OUT) $(BUILD)/ktest/TSHELL.EXE \
            $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(MOUSE_DIR)tests/run.mjs
test: mouse-test
