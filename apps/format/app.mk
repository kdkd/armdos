# apps/format/app.mk - FORMAT.COM (see README.md). dosutil.c/bootrec.c/bootsect.S
# are shared with SYS, CHKDSK, DISKCOPY and DISKCOMP (compiled into each).
# The ARM boot sector (build/bootsect.bin, kernel/boot) is embedded at build time.
FORMAT_DIR := $(here)
DISKUTIL_CFLAGS = -I$(FORMAT_DIR) -Wno-array-bounds -DBOOTSECT_BIN='"$(BUILD)/bootsect.bin"'

$(call armdos_com,FORMAT,format.c dosutil.c bootrec.c bootsect.S,$(DISKUTIL_CFLAGS))
$(BUILD)/obj/FORMAT/bootsect.S.o: $(BUILD)/bootsect.bin

# test helper: run a program with both stdin and stdout redirected
$(call armdos_exe_to,DUREDIR,tests/redir.c,,,,$(BUILD)/diskutil-test)

# "make diskutil-test": FORMAT, SYS, CHKDSK, DISKCOPY and DISKCOMP, booted headless
# (apps/{format,sys,chkdsk,diskcopy}/tests/run.mjs); part of "make test".
.PHONY: diskutil-test
diskutil-test: $(BUILD)/FORMAT.COM $(BUILD)/SYS.COM $(BUILD)/CHKDSK.COM $(BUILD)/DISKCOPY.COM \
               $(BUILD)/DISKCOMP.COM $(DUREDIR_OUT) $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin \
               $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(FORMAT_DIR)tests/run.mjs
	$(NODE) $(FORMAT_DIR)../sys/tests/run.mjs
	$(NODE) $(FORMAT_DIR)../chkdsk/tests/run.mjs
	$(NODE) $(FORMAT_DIR)../diskcopy/tests/run.mjs
test: diskutil-test
