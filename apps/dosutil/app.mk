# apps/dosutil/app.mk - the helpers shared by the DOS 4.00 utilities (FIND,
# SORT, MORE, TREE, COMP, XCOPY, LABEL, REPLACE, PRINT) and their tests.
# No program of its own goes to the disk; each utility compiles u4.c,
# u4crt.c and u4int.S into itself (see README.md).
U4_DIR  := $(here)
U4_TEST := $(BUILD)/u4test
# test helpers (named U4* so they don't collide with other apps' programs;
# the tests put them on the disk as \T\CLS.EXE and \T\REDIR.EXE)
$(call armdos_exe_to,U4CLS,tests/cls.c tests/u4lib.c tests/u4int.S,-I$(U4_DIR),,,$(U4_TEST))
$(call armdos_exe_to,U4REDIR,tests/redir.c tests/u4lib.c tests/u4int.S,-I$(U4_DIR),,,$(U4_TEST))
$(U4_TEST)/CLS.EXE: $(U4_TEST)/U4CLS.EXE
	cp $< $@
$(U4_TEST)/REDIR.EXE: $(U4_TEST)/U4REDIR.EXE
	cp $< $@
