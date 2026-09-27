/* armdos_duke.h - hooks between the ARM-DOS platform files and the game.
 * GPL-2 or later (see COPYING). */
#ifndef ARMDOS_DUKE_H
#define ARMDOS_DUKE_H

void _platform_shutdown(void);        /* display_armdos.c */
void armdos_sound_shutdown(void);     /* dsl_armdos.c: stop the SB stream */
void Error(int errorType, char *error, ...);
void armdos_duke_init(void);            /* game_armdos.c */
void armdos_title_screen(const char *head);
int armdos_duke_vga_present(void);     /* 0 with the Hercules card option */

#endif
