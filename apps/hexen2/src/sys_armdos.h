/* sys_armdos.h -- hooks between the ARM-DOS platform files and the engine.
 * GPL-2 or later, like the rest of Hexen II (see COPYING).
 * Included at the end of hexen2/quakeinc.h (marked ARMDOS).
 */
#ifndef SYS_ARMDOS_H
#define SYS_ARMDOS_H

void VID_ArmdosTextMode (void);		/* vid_armdos.c: back to mode 3 */
int  VID_ArmdosGraphics (void);		/* vid_armdos.c: in mode 13h? */
void Sys_Stack_f (void);
void *Sys_ArmdosZoneBlock (int size);	/* sys_armdos.c: the zone in low memory */		/* sys_armdos.c: "sys_stack" */

#endif
