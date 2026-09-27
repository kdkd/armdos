/*

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

	Modification History:
			2003-01-27	GNU General Public License
*/

#define NULL	 ((void *)0)
#define FALSE	 0
#define TRUE	 1
#define NO	 0
#define YES	 1
#define FOREVER  for (;;)

int index(char *str,int c);
int inportb(unsigned port);
void movblock(unsigned int srcoff,unsigned int srcseg,
	      unsigned int destoff,unsigned int destseg,
	      unsigned int count);
void movmem(void *src,void *dest,unsigned count);
int outportb(unsigned port,int data);
void setmem(void *dest,unsigned count,int c);
