/*
 * armdos.c - ARM-DOS: JOIN's _PARSE.ASM feature switches for the C
 * parser (apps/mslib/src/parse.c): file specs, CAPS, switches, drives.
 * Portions (c) Microsoft Corp. (MS-DOS 4.0 CMD/JOIN), MIT License.
 */
#include "mslib.h"

const unsigned _mslib_parse_features = MSLIB_PARSE_DATE | MSLIB_PARSE_FILE |
    MSLIB_PARSE_CAPS | MSLIB_PARSE_SW | MSLIB_PARSE_DRV;
