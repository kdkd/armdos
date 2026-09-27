// sys_armdos.h -- hooks between the ARM-DOS platform files and the engine.
// GPL-2 or later, like the rest of Quake (see COPYING).
#ifndef SYS_ARMDOS_H
#define SYS_ARMDOS_H

void VID_ArmdosTextMode (void);     // vid_armdos.c: back to mode 3

// The program image and its bss live in conventional memory (below 640 KB),
// where QUAKE.EXE's DOS extender put them in extended memory. The biggest
// static arrays (350 KB) therefore become pointers to blocks that
// Sys_ArmdosAllocBig() takes from the (XMS) heap before Host_Init: every
// "T name[N]" declaration and definition turns into "T (*name_p)[N]", and
// uses of name (indexing, &name[i], sizeof name) work unchanged.
#define ARMDOS_BIG(X) \
	X(cl_entities) X(cl_static_entities) X(cl_temp_entities) X(cl_efrags) \
	X(mod_known) X(snd_scaletable) X(newsky) X(bottomsky) X(bottommask)

#define cl_entities        (*cl_entities_p)
#define cl_static_entities (*cl_static_entities_p)
#define cl_temp_entities   (*cl_temp_entities_p)
#define cl_efrags          (*cl_efrags_p)
#define mod_known          (*mod_known_p)
#define snd_scaletable     (*snd_scaletable_p)
#define newsky             (*newsky_p)
#define bottomsky          (*bottomsky_p)
#define bottommask         (*bottommask_p)

#endif
