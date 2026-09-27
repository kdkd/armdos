;
;	ints	 -	Extra DOS Interrupt Control
;
;	Copyright (C) 1984-2003 David L. Clark.
;	This program is free software; you can redistribute it and/or modify it under
;	the terms of the GNU General Public License as published by the Free Software
;	Foundation; either version 2 of the License, or (at your option) any later
;	version. This program is distributed in the hope that it will be useful,
;	but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;	or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
;	more details. You should have received a copy of the GNU General Public
;	License along with this program; if not, write to the Free Software Foundation,
;	Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. ;
;			Author: Dave Clark
;
;	Modification History:
;			84-03-06	Development
;			87-03-10	Microsoft compiler
;			94-12-18	C6 Compiler
;			2003-01-27	GNU General Public License
;

%	.MODEL	model,lang
	INCLUDE mixed.inc

	.CODE

	include segments.h

	public	intson
	public	intsoff


intson:
	sti
	ret

intsoff:
	cli
	ret

	end
