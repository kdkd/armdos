/*
	_intc.c   -	Interrupt Override Handling

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
			84-03-15	Development
			85-11-06	Atari
			87-03-10	Microsoft compiler.
			2003-01-27	GNU General Public License

	Usage:	--------------------------------------------------------
		_intsetup( inter, offset );
		int	inter, *offset;

		replaces the vector for the specified interrupt with the
		IP:CS passed in as the driver address.

		returns an index into the overridden interrupt table if
		successful, -1 if not.
		--------------------------------------------------------

		_intreset( index )
		int	index;

		resets the interrupt vector for the specified index into
		the interrupt table.  Returns the index passed if
		successful, -1 if not.

		---------------------------------------------------

		_intterm()

		Call to cleanup all overridden vectors.to cleanup
*/


#include	"std.h"
#define MAXINTS 10




struct {
	long	newserv;
	long	oldserv;
	int	internum;
}	_inttab[MAXINTS];



_intsetup( inter, offset )
int	inter, *offset;
{
char	 *vec1, *vec2;
int	 _int1vec(), _int2vec();
long	 get_ivec();
register i;


	/*					   */
	/*	Find an empty slot in the table    */
	/*					   */

	for ( i = 0; i < MAXINTS ; ++i )
		if ( !_inttab[i].internum )
			break;
	if ( i == MAXINTS )
		return( -1 );
	_inttab[i].internum = inter + 1;



	/*					    */
	/*	Get old interrupt vector	    */
	/*	Set up new interrupt vector	    */
	/*					    */

	_inttab[i].oldserv = get_ivec( inter );
	_inttab[i].newserv = (long) offset;
	vec1 = (char *)_int1vec;
	vec2 = (char *)_int2vec;
	set_ivec( inter, vec1 + (int)( vec2 - vec1 ) * i, csseg() );

	return( i );
}



_intreset( i )
int	i;
{
	/*					      */
	/*	Edit the index passed		      */
	/*					      */

	if ( ( i < 0 ) || ( i >= MAXINTS )
		|| ( _inttab[i].internum == 0 ) )
		return( -1 );


	/*					    */
	/*	Reset Vector.			    */
	/*					    */

	set_ivec( _inttab[i].internum - 1, _inttab[i].oldserv );
	_inttab[i].internum = 0;

	return( i );
}



_intterm()
{
register int	i;

	for ( i = MAXINTS - 1; i >= 0; --i )
		if ( _inttab[i].internum )
			_intreset( i );
}
