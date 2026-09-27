/*
 * tvhelp.h - the online help of the ARM-DOS Turbo Vision IDEs (TC.EXE,
 * QB.EXE): a help window showing topics of a help text compiled into the
 * program (tools/mkhelp.mjs).
 *
 * The text: ".topic Name[,Alias...]" starts a topic; "{Text}" or
 * "{Text:Topic}" is a cross reference; ".ex" ... ".endex" is an example
 * (shown as is; tvHelpExample gives it for Edit / Copy example).
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#ifndef TVHELP_H
#define TVHELP_H

#define Uses_TRect
#include <tvision/tv.h>

/* text: the help text (NUL terminated); palette: 8 entries of the help
 * window (frame passive/active/icons, scroll bar page/controls, text, cross
 * reference, selected cross reference); ctx: the helpCtx of the window */
void tvHelpInit( const char *text, const char *palette, ushort ctx );
/* where the window goes (default: the upper part of the desktop) */
extern TRect (*tvHelpRect)();
/* the window's title: "Help" (0 = "HELP: topic") */
extern const char *tvHelpTitle;
/* the window's frame (0 = Turbo Vision's) */
class TFrame;
extern TFrame *(*tvHelpFrame)( TRect );
extern bool tvHelpShadow;               /* default true */

bool tvHelpShow( const char *topic );   /* open the window at a topic */
bool tvHelpHas( const char *topic );    /* is there such a topic (or alias)? */
void tvHelpIndex();                     /* the generated "Index" topic */
void tvHelpPrevious();                  /* back (Alt+F1) */
void tvHelpModal( const char *topic );  /* on top of a dialog, until Esc */
bool tvHelpIsOpen();
bool tvHelpIsCurrent();                 /* the help window is the active one */
void tvHelpClose();
const char *tvHelpTopic();              /* the topic shown ("" none) */
/* the example of the topic shown: a malloc'd string (caller frees), 0 if none */
char *tvHelpExample();

#endif
