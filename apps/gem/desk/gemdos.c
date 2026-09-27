/*	GEMDOS.C	4/18/84 - 03/07/85	Lee Lorenzen		*/
/*	for 3.0		6/23/86			MDF			*/
/*	merge source	5/28/87			mdf			*/
/*	work around dos_delete	11/5/87	mdf			*/

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Application Environment Services		  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1984 - 1987		Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "desk.h"

#define DESKTOP 1
						/* in DOSIF.A86		*/


GLOBAL ULONG	DOS_AX;
GLOBAL ULONG	DOS_BX;
GLOBAL ULONG	DOS_CX;
GLOBAL ULONG	DOS_DX;
GLOBAL ULONG	DOS_DS;
GLOBAL ULONG	DOS_ES;
GLOBAL ULONG	DOS_SI;
GLOBAL ULONG	DOS_DI;
GLOBAL ULONG	DOS_ERR;

	VOID
dos_func(UWORD ax, LONG dsdx)
{
	DOS_AX = ax;
	DOS_DX = dsdx;			/* ARM-DOS: DS:DX = flat pointer */

	__DOS();
}


	VOID
dos_chdir(LONG pdrvpath)
{
	dos_func(0x3b00, pdrvpath);
}


	WORD
dos_gdir(WORD drive, LONG pdrvpath)
{
	DOS_AX = 0x4700;
	DOS_DX = (UWORD) drive;
	DOS_SI = pdrvpath;		/* ARM-DOS: flat pointer */

	__DOS();

	return(TRUE);
}


	WORD
dos_gdrv(VOID)
{
	DOS_AX = 0x1900;

	__DOS();
	return(DOS_AX & 0x00ff);
}


	WORD
dos_sdrv(WORD newdrv)
{
	DOS_AX = 0x0e00;
	DOS_DX = newdrv;

	__DOS();

	return(DOS_AX & 0x00ff);
}

/*
	VOID
dos_term()
{
	DOS_AX = 0x4c00;
	__DOS();
}
*/


	VOID
dos_sdta(LONG ldta)
{
	dos_func(0x1a00, ldta);
}


	LONG
dos_gdta(VOID)
{
	dos_func(0x2f00, LLDS() );
	return( DOS_BX );		/* ARM-DOS: ES:BX = flat pointer */
}


	WORD
dos_gpsp(VOID)
{
	DOS_AX = 0x5100;

	__DOS();
	return(DOS_BX);
}


	WORD
dos_spsp(WORD newpsp)
{
	DOS_AX = 0x5000;
	DOS_BX = newpsp;

	__DOS();

	return(DOS_AX);
}


	WORD
dos_sfirst(LONG pspec, WORD attr)
{
	DOS_CX = attr;

	dos_func(0x4e00, pspec);
	return(!DOS_ERR);
}


	WORD
dos_snext(VOID)
{
	DOS_AX = 0x4f00;

	__DOS();

	return(!DOS_ERR);
}


	WORD
dos_open(LONG pname, WORD access)
{
	dos_func(0x3d00 + access, pname);

	return(DOS_AX);
}


	WORD
dos_close(WORD handle)
{
	DOS_AX = 0x3e00;
	DOS_BX = handle;

	__DOS();

	return(!DOS_ERR);
}

	WORD
dos_read(WORD handle, WORD cnt, LONG pbuffer)
{
	DOS_CX = cnt;
	DOS_BX = handle;
	dos_func(0x3f00, pbuffer);
	return(DOS_AX);
}


	LONG
dos_lseek(WORD handle, WORD smode, LONG sofst)
{
	DOS_AX = 0x4200;
	DOS_AX += smode;
	DOS_BX = handle;
	DOS_CX = LHIWD(sofst);
	DOS_DX = LLOWD(sofst);

	__DOS();

	return((DOS_AX & 0xFFFF) + HW(DOS_DX) );
}


	VOID
dos_exec(LONG pcspec, WORD segenv, LONG pcmdln, LONG pfcb1, LONG pfcb2)
{
	EXEC_BLK	exec;

	exec.eb_segenv = segenv;
	exec.eb_pcmdln = pcmdln;
	exec.eb_pfcb1 = pfcb1;
	exec.eb_pfcb2 = pfcb2;

	DOS_AX = 0x4b00;
	DOS_BX = ADDR(&exec);		/* ARM-DOS: flat pointers */
	DOS_DX = pcspec;

	__EXEC();
}


	WORD
dos_wait(VOID)
{
	DOS_AX = 0x4d00;
	__DOS();

	return(DOS_AX);
}


	LONG
dos_alloc(LONG nbytes)
{
	LONG		maddr;

	DOS_AX = 0x4800;
	if (nbytes == 0xFFFFFFFFL)
	  DOS_BX = 0xffff;
	else
	  DOS_BX = (nbytes + 15L) >> 4L;

	__DOS();

	if (DOS_ERR)
	  maddr = 0x0L;
	else
	  maddr = (LONG)(DOS_AX & 0xFFFF) << 4;	/* ARM-DOS: segment -> flat */

	return(maddr);
}


/*
*	Returns the amount of memory available in bytes
*/
	LONG
dos_avail(VOID)
{
	LONG		mlen;

	DOS_AX = 0x4800;
	DOS_BX = 0xffff;

	__DOS();

	mlen = ((LONG) (DOS_BX & 0xFFFF)) << 4;
	return(mlen);
}


	WORD
dos_free(LONG maddr)
{
	DOS_AX = 0x4900;
	DOS_ES = (ULONG)maddr >> 4;		/* ARM-DOS: flat -> segment (ES = r8) */

	__DOS();

	return(DOS_AX);
}

#if MULTIAPP

	WORD
dos_stblk(blockseg, newsize)
	UWORD		blockseg;
	UWORD		newsize;		/* in paragraphs	*/
{
	DOS_AX = 0x4a00;
	DOS_ES = blockseg;
	DOS_BX = newsize;
	
	__DOS();

	return(DOS_AX);
}

#endif
/************************************************************************/
/*	ONLY USED BY THE DESKTOP					*/
/************************************************************************/

#if (DESKTOP)
	VOID
dos_label(BYTE drive, BYTE *plabel)
{
	BYTE		label_buf[128];
	BYTE		ex_fcb[40];

	dos_sdta(ADDR(&label_buf[0]));
	ex_fcb[0] = 0xff;
	bfill(5, 0, &ex_fcb[1]);
	ex_fcb[6] = 0x08;		/* volume label	*/
	ex_fcb[7] = drive;
	bfill(11, '?', &ex_fcb[8]);
	bfill(21, 0, &ex_fcb[19]);

	dos_func(0x1100, ADDR(&ex_fcb[0]));

	if ( (DOS_AX & 0x00ff) == 0xff )
	  *plabel = NULL;
	else
	{
	  label_buf[19] = 0x0;
	  strcpy(&label_buf[8], plabel);
	}
}

	VOID
dos_space(WORD drv, LONG *ptotal, LONG *pavail)
{
	DOS_AX = 0x3600;
	DOS_DX = drv;
	__DOS();
	
	DOS_AX *= DOS_CX;
	*ptotal = (LONG) DOS_AX * (LONG) DOS_DX;
	*pavail = (LONG) DOS_AX * (LONG) DOS_BX;
}


	WORD
dos_rmdir(LONG ppath)
{
	dos_func(0x3a00, ppath);
	return(!DOS_ERR);
}


	WORD
dos_create(LONG pname, WORD attr)
{
	DOS_CX = attr;
	dos_func(0x3c00, pname);

	return(DOS_AX);
}


	WORD
dos_mkdir(LONG ppath, WORD attr)
{
	dos_func(0x3900, ppath);
	return(!DOS_ERR);
}


	WORD
dos_delete(LONG pname)
{
	WORD	savret;

	dos_func(0x4100, pname);
	if (DOS_ERR)
	{
	  savret = DOS_AX;			/* flush cashe -- DOS BUG */
	  dos_create(pname, 0);
	  DOS_AX = savret;
	  DOS_ERR = TRUE;
	}
	return(DOS_AX);
}


	WORD
dos_rename(LONG poname, LONG pnname)
{
	DOS_DI = pnname;		/* ARM-DOS: flat pointer */
	dos_func(0x5600, poname);
	return(DOS_AX);
}


	WORD
dos_write(WORD handle, WORD cnt, LONG pbuffer)
{
	DOS_CX = cnt;
	DOS_BX = handle;
	dos_func(0x4000, pbuffer);
	return(DOS_AX);
}


	WORD
dos_chmod(LONG pname, WORD func, WORD attr)
{
	DOS_CX = attr;
	dos_func(0x4300 + func, pname);
	return(DOS_CX);
}


	VOID
dos_setdt(WORD handle, WORD time, WORD date)
{
	DOS_AX = 0x5701;
	DOS_BX = handle;
	DOS_CX = time;
	DOS_DX = date;

	__DOS();
}

#endif
