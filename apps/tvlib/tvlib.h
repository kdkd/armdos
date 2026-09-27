/*
 * tvlib.h - shared pieces of the ARM-DOS Turbo Vision IDEs (TC.EXE, QB.EXE):
 * the EDIT platform layer (apps/edit/armdos) plus a few extras.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#ifndef TVLIB_H
#define TVLIB_H

#ifdef __cplusplus
extern "C" {
#endif

/* ximage.c: 1 when this program runs from an XMS block (xload.c stub) */
int xload_in_xms(void);

#ifdef __cplusplus
}

#include <stdint.h>

/* plat.cpp: the screen is not what the shadow buffer says any more (a
 * program ran on it): the next flush rewrites every cell. */
void tvInvalidateScreen();
/* is a key (or mouse click) waiting? read it (INT 16h AH=10h) */
int tvKeyWaiting();
unsigned tvGetKey();
/* wait for a key press or mouse click (for "Press any key" screens) */
void tvWaitAnyKey();

/* edits_hl.cpp: syntax colouring hook for TEditor::formatLine. For every
 * line drawn, hook(editor, lineStartPtr, lineLen, attrs, normal) may fill
 * attrs[0..lineLen) with colours (TColorAttr); it returns false to use the
 * normal colour. The selection still wins. */
class TEditor;
typedef bool (*TvHighlightFn)( TEditor *ed, unsigned linePtr, unsigned len,
                               uint8_t *attrs, uint8_t normal );
extern TvHighlightFn tvHighlight;
/* a line to show highlighted (the error line, the next statement):
 * editor, the line's start pointer, attribute; ed = 0 for none */
extern TEditor *tvMarkEditor;
extern unsigned tvMarkLine;
extern uint8_t tvMarkAttr;
/* breakpoint lines (QB): called per line, returns an attribute or 0 */
typedef uint8_t (*TvLineAttrFn)( TEditor *ed, unsigned linePtr );
extern TvLineAttrFn tvLineAttr;

#endif

#endif
