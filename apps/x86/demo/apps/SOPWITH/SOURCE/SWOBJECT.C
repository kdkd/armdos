/*
	swobject -	SW object allocation and deallocation

	Copyright (C) 1984-2003 David L. Clark.
	This program is free software; you can redistribute it and/or modify it under
	the terms of the GNU General Public License as published by the Free Software
	Foundation; either version 2 of the License, or (at your option) any later
	version. This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
	or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
	more details. You should have received a copy of the GNU General Public
	License along with this program; if not, write to the Free Software Foundation,
	Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. 

			Author: Dave Clark

	Modification History:
			84-02-07	Development
			84-06-12	PCjr Speed-up
			84-10-31	Atari
			87-03-09	Microsoft compiler.
			2003-01-27	GNU General Public License
*/
#include	"sw.h"



extern	OBJECTS *nobjects;		/* Objects list.		    */
extern	OBJECTS *objbot, *objtop,	/* Top and bottom of object list    */
		*objfree,		/* Free list			    */
		*deltop, *delbot;	/* Newly deallocated objects	    */
extern	OBJECTS *objsmax;		/* Maximum allocated object	    */



OBJECTS *allocobj()
{
register OBJECTS *ob;

	if ( !objfree )
		return( NULL );

	ob = objfree;
	objfree = ob->ob_next;

	ob->ob_next = NULL;
	ob->ob_prev = objbot;

	if ( objbot )
		objbot->ob_next = ob;
	else
		objtop = ob;

	ob->ob_sound = NULL;
	ob->ob_drwflg = ob->ob_delflg = 0;
	if ( ob > objsmax )
		objsmax = ob;
	return( objbot = ob );
}



deallobj( obp )
OBJECTS *obp;
{
register OBJECTS *ob, *obb;

	ob =obp;
	if ( obb = ob->ob_prev )
		obb->ob_next = ob->ob_next;
	else
		objtop = ob->ob_next;

	if ( obb = ob->ob_next )
		obb->ob_prev = ob->ob_prev;
	else
		objbot = ob->ob_prev;

	ob->ob_next = 0;
	if ( delbot )
		delbot->ob_next = ob;
	else
		deltop = ob;

	delbot = ob;


}
