# apps/sbmix/app.mk - SBMIX.EXE: the Sound Blaster 16 mixer levels; AUTOEXEC.BAT
# runs SBMIX /INIT /Q at boot, as a real SB16's setup utility did. (Tested by
# apps/sbtest/tests/run.mjs.)
$(call armdos_exe,SBMIX,sbmix.c)
