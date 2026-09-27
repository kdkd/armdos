#	Copyright (C) 1984-2003 David L. Clark.
#	This program is free software; you can redistribute it and/or modify it under
#	the terms of the GNU General Public License as published by the Free Software
#	Foundation; either version 2 of the License, or (at your option) any later
#	version. This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
#	or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
#	more details. You should have received a copy of the GNU General Public
#	License along with this program; if not, write to the Free Software Foundation,
#	Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. 
#

.asm.obj:
	masm /Dmodel=small /Dlang=c /Mx $*.asm;

.c.obj:
	cl -c -Gs -Zl -FPc -AS -Osa $*.c

_ints.obj:	   _ints.asm

swcomm.obj:	   swcomm.asm

_inta.obj:	   _inta.asm

swgrph.obj:	   swgrph.asm

swhist.obj:	   swhist.asm

swutil.obj:	   swutil.asm

_dkio.obj:	   _dkio.asm


bmblib.obj:	   bmblib.c

_intc.obj:	   _intc.c

swasynio.obj:	   swasynio.c

swauto.obj:	   swauto.c

swcollsn.obj:	   swcollsn.c

swdisp.obj:	   swdisp.c

swend.obj:	   swend.c

swgames.obj:	   swgames.c

swground.obj:	   swground.c

swinit.obj:	   swinit.c

swmain.obj:	   swmain.c

swmisc.obj:	   swmisc.c

swmove.obj:	   swmove.c

swmultio.obj:	   swmultio.c

swobject.obj:	   swobject.c

swplanes.obj:	   swplanes.c

swsound.obj:	   swsound.c

swsymbol.obj:	   swsymbol.c

swtitle.obj:	   swtitle.c

sopwith.exe:	   _ints.obj \
		   swcomm.obj \
		   _inta.obj \
		   swgrph.obj \
		   swutil.obj \
		   _dkio.obj \
		   _intc.obj \
		   swasynio.obj \
		   swmain.obj \
		   swmove.obj \
		   swinit.obj \
		   swauto.obj \
		   swdisp.obj \
		   swcollsn.obj \
		   swplanes.obj \
		   swsymbol.obj \
		   swend.obj \
		   swgames.obj \
		   swground.obj \
		   swhist.obj \
		   swmisc.obj \
		   swmultio.obj \
		   swobject.obj \
		   swsound.obj \
		   swtitle.obj \
		   bmblib.obj \
		   sw.mak \
		   sw.lnk
	link @sw.lnk
