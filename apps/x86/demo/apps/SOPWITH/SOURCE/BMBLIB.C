/*
	bmblib	-	SW Old BMB STDLIB routines

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
			94-12-19	Original development
			2003-01-27	GNU General Public License
*/


#include "dos.h"
#include "sw.h"
#include "string.h"


int _systype = PCDOS;





/*----------------------------------------------------------------------------
		GETFLAGS flags processing ( Jack Cole )
----------------------------------------------------------------------------*/




getflags(ac, av, fmt, flds)
int  *ac;
char **av;
char *fmt;
int  *flds[];
  {
    char **arg,*apend;
    int i,j,k;
    register char *aptr;
    char *flag;
    register char *fptr;
    int  **var;
    int  *adrvar;
    char *cfmt;
    int  sts = 0;
    int  got_next;

    arg = (char **) *av;
    i = *ac;
    ++arg;				/* point past program name */

    while ( --i )  {			/* for all args */
	aptr = *arg;			/* point at string */
	if ( *aptr++ != '-' ) break;    /* past the switches */
	if ( *aptr == '-' ) {           /* or -- at eol */
		++arg;			/* flush it */
		--i;
		break;
	}

nextbool:
	flag = aptr;				/* get the switch */
	var = (int **) &flds;			/* find in format */

	for (fptr = fmt; *fptr && *fptr != ':' ; ++var) {
	    j = 0;
	    while ( isalnum(*(fptr+j)) ) ++j;		/* get switch length */

	    /* match and number or space following? */

	    if ( !strncmp( flag, fptr, j ) && (!isalpha(*(flag+j)) ||
						*(fptr+j)!='#') ) break;

	    fptr += j;					/* skip to next */
	    if (*fptr) ++fptr;				/* past format */
	}

	if ( !(*fptr) || *fptr == ':' )  {              /* no match? */
	    if ( (k = index( fmt, ':')) ) {          /* find usage info */
		cfmt = fmt + k - 1;
		*cfmt++ = '\0';
		if ( !(*cfmt) ) {			/* return on error? */
			sts = 1;
			goto ret;
		}
	    }
	    exit(0);
	}

	flag = fptr+j;				/* the type */
	aptr += j;
	adrvar = *var;				/* this is addr of real var */
	got_next = NO;
	if (*aptr == 0 && *flag != '&')  {      /* more expected */
	    if ( i > 1 ) {			/* any more args? */
		aptr = *(++arg);		/* step to next arg */
		--i;
		got_next = YES;
	    }
	}

	switch	(*flag)  {			 /* what kind expected */
	    case '#' :
		j = 0;
		if ( *aptr ) {			/* any more chars? */
		    *adrvar=strtol(aptr,&apend,10);
		    j = apend - aptr;
		    aptr = apend;
		}
		if ( !j ) {			/* how many digits? */
			if (got_next) { 	/* none - push back? */
				--arg;		/* yes, push back */
				++i;
			}
			*(int *)adrvar = -1;	/* flag present, but no arg */
		}
		break;

	    case '*' : *(char **)adrvar = aptr;
			break;

	    case '&' : *(int *)adrvar = YES;              /* boolean */
			break;
	  }

	if (*flag == '&' && *aptr) goto nextbool;
	++arg;
    }

ret:
    *av = (char *)arg;				/* point past those processed */
    *ac = i;
    return(sts);				/* successful */
  }


int index(char *str,int c)
{
char *s;

    return((s=strchr(str,c))==NULL?0:s-str+1);
}


int inportb(unsigned port)
{
    return(inp(port));
}


void movblock(unsigned int srcoff,unsigned int srcseg,
	      unsigned int destoff,unsigned int destseg,
	      unsigned int count)
{
    movedata(srcseg,srcoff,destseg,destoff,count);
}



void movmem(void *src,void *dest,unsigned count)
{
    memmove(dest,src,count);
}



int outportb(unsigned port,int data)
{
    return(outp(port,data));
}


void setmem(void *dest,unsigned count,int c)
{
    memset(dest,c,count);
}


int sysint(int intnum,struct regval *inrv,struct regval *outrv)
{
union REGS regs;
struct SREGS segregs;
int rc;

    regs.x.ax=inrv->axr;
    regs.x.bx=inrv->bxr;
    regs.x.cx=inrv->cxr;
    regs.x.dx=inrv->dxr;
    segregs.ds=inrv->dsr;
    rc=int86x(intnum,&regs,&regs,&segregs);
    outrv->axr=regs.x.ax;
    outrv->bxr=regs.x.bx;
    outrv->cxr=regs.x.cx;
    outrv->dxr=regs.x.dx;
    outrv->dsr=segregs.ds;
    return(rc);
}


int sysint21(struct regval *inrv,struct regval *outrv)
{
union REGS regs;
struct SREGS segregs;
int rc;

    regs.x.ax=inrv->axr;
    regs.x.bx=inrv->bxr;
    regs.x.cx=inrv->cxr;
    regs.x.dx=inrv->dxr;
    segregs.ds=inrv->dsr;
    rc=intdosx(&regs,&regs,&segregs);
    outrv->axr=regs.x.ax;
    outrv->bxr=regs.x.bx;
    outrv->cxr=regs.x.cx;
    outrv->dxr=regs.x.dx;
    outrv->dsr=segregs.ds;
    return(rc);
}
