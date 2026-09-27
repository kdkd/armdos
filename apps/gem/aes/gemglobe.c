/*	GEMGLOBE.C 	4/23/84 - 06/23/85	Lee Lorenzen		*/
/*	merge High C vers. w. 2.2 		8/21/87		mdf	*/ 

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Application Environment Services		  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1987			Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "aes.h"

#if I8086 && !PTR32
GLOBAL WORD		D;
#endif

#if MC68K || PTR32

GLOBAL THEGLO		D;

/* ARM-DOS: far_call() is in gemarm.c */
#endif


