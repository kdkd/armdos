/*	CALCIF.C		10/23/84-10/23/84	Andrew Muir	*/

#include <portab.h>
#include <machine.h>
#include <taddr.h>
#include <obdefs.h>
#include <crysbind.h>
#include <cryslib.h>
#include "accproto.h"
#include "accarm.h"

typedef struct cblk
{
	LONG		cb_pcontrol;
	LONG		cb_pglobal;
	LONG		cb_pintin;
	LONG		cb_pintout;
	LONG		cb_padrin;
	LONG		cb_padrout;		
} CBLK;
						/* in STARTUP.A86	*/

GLOBAL CBLK		c;
GLOBAL UWORD		control[C_SIZE];
GLOBAL UWORD		global[G_SIZE];
GLOBAL UWORD		int_in[I_SIZE];
GLOBAL UWORD		int_out[O_SIZE];
GLOBAL LONG		addr_in[AI_SIZE];
GLOBAL LONG		addr_out[AO_SIZE];

GLOBAL WORD		gl_apid;
GLOBAL LONG		ad_c;

GLOBAL WORD	 	gl_handle;

						/* in DOSIF.A86		*/

GLOBAL ULONG	DOS_AX;
GLOBAL ULONG	DOS_BX;
GLOBAL ULONG	DOS_CX;
GLOBAL ULONG	DOS_DX;
GLOBAL ULONG	DOS_SI;
GLOBAL ULONG	DOS_DI;
GLOBAL ULONG	DOS_ES;
GLOBAL ULONG	DOS_DS;
GLOBAL ULONG	DOS_ERR;


	VOID
dos_func(UWORD ax, LONG dsdx)
{
	DOS_AX = ax;
	DOS_DX = dsdx;			/* ARM-DOS: DS:DX = flat pointer */

	__DOS();
}

	WORD
dos_gtime(VOID)
{
	DOS_AX = 0x2c00;
	__DOS();
	return(DOS_CX);
}

	WORD
dos_gsec(VOID)
{
	DOS_AX = 0x2c00;
	__DOS();
	return(DOS_DX);
}


	WORD
dos_gyear(VOID)
{
	DOS_AX = 0x2a00;
	__DOS();
	return(DOS_CX);
}

	WORD
dos_gdate(VOID)
{
	DOS_AX = 0x2a00;
	__DOS();
	return(DOS_DX);
}

	WORD
dos_syear(WORD year)
{

	DOS_DX = dos_gdate();
	if (year > 83)
	  DOS_CX = year + 1900;
         else
	   DOS_CX = year + 2000;
	DOS_AX = 0x2b00;
	__DOS();	    
	DOS_AX &= 0x00ff;
	if (DOS_AX == 0)
	   return (TRUE);
	 else
	    return (FALSE); 
}


	WORD
dos_sdate(WORD date)
{	
	WORD		tmp_date;

 	DOS_CX = dos_gyear();
	DOS_DX = dos_gdate();
	tmp_date = date;
	tmp_date &= 0x00ff;
	DOS_DX &= 0xff00;
	DOS_DX = DOS_DX | tmp_date;
	DOS_AX = 0x2b00;
	__DOS();       
	DOS_AX &= 0x00ff;
	if (DOS_AX == 0)
	  return (TRUE);
         else
	   return(FALSE);
}		


	WORD
dos_smonth(WORD date)
{	
	WORD		tmp_date;

 	DOS_CX = dos_gyear();
	DOS_DX = dos_gdate();
	tmp_date = date;
	tmp_date <<= 8;
	DOS_DX &= 0x00ff;
	DOS_DX = DOS_DX | tmp_date;
	DOS_AX = 0x2b00;
	__DOS();       
	DOS_AX &= 0x00ff;
	if (DOS_AX == 0)
	  return (TRUE);
         else
	   return(FALSE);
}		


	WORD
dos_shour(WORD the_hour)
{	
	WORD		tmp_hour;

	DOS_AX = 0x2c00;
	__DOS();
	tmp_hour = the_hour;
	tmp_hour <<= 8;
	DOS_CX &= 0x00ff;
	DOS_CX = DOS_CX | tmp_hour;
	DOS_AX = 0x2d00;
	__DOS();       
	DOS_AX &= 0x00ff;
	if (DOS_AX == 0)
	  return (TRUE);
         else
	   return(FALSE);
}		


	WORD
dos_smin(WORD the_min)
{	
	WORD		tmp_min;

	DOS_AX = 0x2c00;
	__DOS();
	tmp_min = the_min;
	tmp_min &= 0x00ff;
	DOS_CX &= 0xff00;
	DOS_CX = DOS_CX | tmp_min;
	DOS_AX = 0x2d00;
	__DOS();       
	DOS_AX &= 0x00ff;
	if (DOS_AX == 0)
	  return (TRUE);
         else
	   return(FALSE);
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


	WORD
dos_gdrv(VOID)
{
	DOS_AX = 0x1900;

	__DOS();
	return(DOS_AX & 0x00ff);
}



	WORD
dos_delete(LONG pname)
{
	dos_func(0x4100, pname);
	return(DOS_AX);
}


	WORD
dos_sfirst(LONG pspec, WORD attr)
{
	DOS_CX = attr; /* file attributes */

	dos_func(0x4e00, pspec);
	return(!DOS_ERR);
}


	VOID
strcat(BYTE *pd, BYTE *ps)
{
	while(*pd)
		pd++;
	while(*pd++ = *ps++)
		;
}


/************************************************************************/
	WORD
r_intersect(WORD x, WORD y, WORD w, WORD h, WORD *px, WORD *py, WORD *pw, WORD *ph)
{
	WORD		tx, ty, tw, th;

	tw = min(*px + *pw, x + w);
	th = min(*py + *ph, y + h);
	tx = max(*px, x);
	ty = max(*py, y);
	*px = tx;
	*py = ty;
	*pw = tw - tx;
	*ph = th - ty;
	return( (tw > tx) && (th > ty) );
}


	WORD
objc_offset(LONG tree, WORD obj, WORD *poffx, WORD *poffy)
{
	OB_TREE = tree;
	OB_OBJ = obj;
	crys_if(OBJC_OFFSET);
	*poffx = OB_XOFF;
	*poffy = OB_YOFF;
	return( RET_CODE );
}

	UWORD
inside(UWORD x, UWORD y, UWORD tx, UWORD ty, UWORD tw, UWORD th)
{
	if ( (x >= tx) && (y >= ty) &&
	     (x < tx + tw) && (y < ty + th) )
	  return(TRUE);
	else
	  return(FALSE);
} /* inside */


	WORD
min(WORD a, WORD b)
{
	return((a < b) ? a:b);
}

	WORD
max(WORD a, WORD b)
{
	return((a > b) ? a:b);
}



	WORD
strcpy(BYTE *ps, BYTE *pd)
{
	while(*pd++ = *ps++)
	  ;
}


	WORD
crys_if(WORD opcode)
{
	WORD		i;

	i = opcode * 5;
	c.cb_pcontrol = ADDR( &ctrl_cnts[i] );
	crystal(ad_c);
	return( RET_CODE );
}


	WORD
menu_register(WORD pid, LONG pstr)
{
	MM_PID = pid;
	MM_PSTR = pstr;
	return(crys_if(MENU_REGISTER));
}


	UWORD
appl_init(VOID)
{
	WORD		i;

	c.cb_pcontrol = ADDR(&control[0]); 
	c.cb_pglobal = ADDR(&global[0]);
	c.cb_pintin = ADDR(&int_in[0]);
	c.cb_pintout = ADDR(&int_out[0]);
	c.cb_padrin = ADDR(&addr_in[0]);
	c.cb_padrout = ADDR(&addr_out[0]);
	ad_c = ADDR(&c);
	crys_if(APPL_INIT);
	gl_apid = RET_CODE;
	return( global[10] );
} /* appl_init */


	WORD
appl_write(WORD rwid, WORD length, LONG pbuff)
{
	AP_RWID = rwid;
	AP_LENGTH = length;
	AP_PBUFF = pbuff;
	return( crys_if(APPL_WRITE) );
}



	WORD
evnt_timer(UWORD locnt, UWORD hicnt)
{
	T_LOCOUNT = locnt;
	T_HICOUNT = hicnt;
	return( crys_if(EVNT_TIMER) );
}


	WORD
evnt_multi(UWORD flags, UWORD bclk, UWORD bmsk, UWORD bst,
	UWORD m1flags, UWORD m1x, UWORD m1y, UWORD m1w, UWORD m1h,
	UWORD m2flags, UWORD m2x, UWORD m2y, UWORD m2w, UWORD m2h, LONG mepbuff,
	UWORD tlc, UWORD thc, UWORD *pmx, UWORD *pmy, UWORD *pmb, UWORD *pks, UWORD *pkr, UWORD *pbr)
{
	MU_FLAGS = flags;

	MB_CLICKS = bclk;
	MB_MASK = bmsk;
	MB_STATE = bst;

	MMO1_FLAGS = m1flags;
	MMO1_X = m1x;
	MMO1_Y = m1y;
	MMO1_WIDTH = m1w;
	MMO1_HEIGHT = m1h;

	MMO2_FLAGS = m2flags;
	MMO2_X = m2x;
	MMO2_Y = m2y;
	MMO2_WIDTH = m2w;
	MMO2_HEIGHT = m2h;

	MME_PBUFF = mepbuff;

	MT_LOCOUNT = tlc;
	MT_HICOUNT = thc;

	crys_if(EVNT_MULTI);

	*pmx = EV_MX;
	*pmy = EV_MY;
	*pmb = EV_MB;
	*pks = EV_KS;
	*pkr = EV_KRET;
	*pbr = EV_BRET;

	return( RET_CODE );
}


	WORD
objc_draw(LONG tree, WORD drawob, WORD depth, WORD xc, WORD yc, WORD wc, WORD hc)
{
	OB_TREE = tree;
	OB_DRAWOB = drawob;
	OB_DEPTH = depth;
	OB_XCLIP = xc;
	OB_YCLIP = yc;
	OB_WCLIP = wc;
	OB_HCLIP = hc;
	return( crys_if( OBJC_DRAW ) );
}


	WORD
objc_find(LONG tree, WORD startob, WORD depth, WORD mx, WORD my)
{
	OB_TREE = tree;
	OB_STARTOB = startob;
	OB_DEPTH = depth;
	OB_MX = mx;
	OB_MY = my;
	return( crys_if( OBJC_FIND ) );
}



	WORD
objc_change(LONG tree, WORD drawob, WORD depth, WORD xc, WORD yc, WORD wc, WORD hc, WORD newstate, WORD redraw)
{
	OB_TREE = tree;
	OB_DRAWOB = drawob;
	OB_DEPTH = depth;
	OB_XCLIP = xc;
	OB_YCLIP = yc;
	OB_WCLIP = wc;
	OB_HCLIP = hc;
	OB_NEWSTATE = newstate;
	OB_REDRAW = redraw;
	return( crys_if( OBJC_CHANGE ) );
}


	WORD
form_alert(WORD defbut, LONG astring)
{
	FM_DEFBUT = defbut;
	FM_ASTRING = astring;
	return( crys_if( FORM_ALERT ) );
}


	WORD
graf_mouse(WORD m_number, LONG m_addr)
{
	GR_MNUMBER = m_number;
	GR_MADDR = m_addr;
	return(crys_if(GRAF_MOUSE));
}


	WORD
graf_mkstate(WORD *pmx, WORD *pmy, WORD *pmstate, WORD *pkstate)
{
	crys_if( GRAF_MKSTATE );
	*pmx = GR_MX;
	*pmy = GR_MY;
	*pmstate = GR_MSTATE;
	*pkstate = GR_KSTATE;
}


	WORD
graf_handle(WORD *pwchar, WORD *phchar, WORD *pwbox, WORD *phbox)
{
	crys_if(GRAF_HANDLE);
	*pwchar = GR_WCHAR ;
	*phchar = GR_HCHAR;
	*pwbox = GR_WBOX;
	*phbox = GR_HBOX;
	return(RET_CODE);
}


	VOID
graf_growbox(WORD orgx, WORD orgy, WORD orgw, WORD orgh, WORD x, WORD y, WORD w, WORD h)
{
	GR_I1 = orgx;
	GR_I2 = orgy;
	GR_I3 = orgw;
	GR_I4 = orgh;
	GR_I5 = x;
	GR_I6 = y;
	GR_I7 = w;
	GR_I8 = h;
	return( crys_if( GRAF_GROWBOX ) );
} /* graf_growbox */


	VOID
graf_shrinkbox(WORD orgx, WORD orgy, WORD orgw, WORD orgh, WORD x, WORD y, WORD w, WORD h)
{
	GR_I1 = orgx;
	GR_I2 = orgy;
	GR_I3 = orgw;
	GR_I4 = orgh;
	GR_I5 = x;
	GR_I6 = y;
	GR_I7 = w;
	GR_I8 = h;
	return( crys_if( GRAF_SHRINKBOX ) );
} /* graf_shrinkbox */


	WORD
graf_watchbox(LONG tree, WORD obj, UWORD instate, UWORD outstate)
{
	GR_TREE = tree;
	GR_OBJ = obj;
	GR_INSTATE = instate;
	GR_OUTSTATE = outstate;
	return( crys_if( GRAF_WATCHBOX ) );
} /* graf_watchbox */


	VOID
graf_rubbox(WORD xorigin, WORD yorigin, WORD wmin, WORD hmin, WORD *pwend, WORD *phend)
{
	GR_I1 = xorigin;
	GR_I2 = yorigin;
	GR_I3 = wmin;
	GR_I4 = hmin;
	crys_if( GRAF_RUBBOX );
	*pwend = GR_O1;
	*phend = GR_O2;
	return( RET_CODE );
}


	WORD
fsel_input(LONG pipath, LONG pisel, WORD *pbutton)
{
	FS_IPATH = pipath;
	FS_ISEL = pisel;
	crys_if( FSEL_INPUT );
	*pbutton = FS_BUTTON;
	return( RET_CODE );
}


					/* Window Manager		*/
	WORD
wind_create(UWORD kind, WORD wx, WORD wy, WORD ww, WORD wh)
{
	WM_KIND = kind;
	WM_WX = wx;
	WM_WY = wy;
	WM_WW = ww;
	WM_WH = wh;
	return( crys_if( WIND_CREATE ) );
}


	WORD
wind_open(WORD handle, WORD wx, WORD wy, WORD ww, WORD wh)
{
	WM_HANDLE = handle;
	WM_WX = wx;
	WM_WY = wy;
	WM_WW = ww;
	WM_WH = wh;
	return( crys_if( WIND_OPEN ) );
}


	WORD
wind_close(WORD handle)
{
	WM_HANDLE = handle;
	return( crys_if( WIND_CLOSE ) );
}


	WORD
wind_delete(WORD handle)
{
	WM_HANDLE = handle;
	return( crys_if( WIND_DELETE ) );
}


	WORD
wind_get(WORD w_handle, WORD w_field, WORD *pw1, WORD *pw2, WORD *pw3, WORD *pw4)
{
	WM_HANDLE = w_handle;
	WM_WFIELD = w_field;
	crys_if( WIND_GET );
	*pw1 = WM_OX;
	*pw2 = WM_OY;
	*pw3 = WM_OW;
	*pw4 = WM_OH;
	return( RET_CODE );
}



	WORD
wind_set(WORD w_handle, WORD w_field, WORD w2, WORD w3, WORD w4, WORD w5)
{
	WM_HANDLE = w_handle;
	WM_WFIELD = w_field;
	WM_IX = w2;
	WM_IY = w3;
	WM_IW = w4;
	WM_IH = w5;
	return( crys_if( WIND_SET ) );
}


	WORD
wind_find(WORD mx, WORD my)
{
	WM_MX = mx;
	WM_MY = my;
	return( crys_if( WIND_FIND ) );
}


	WORD
wind_update(WORD beg_update)
{
	WM_BEGUP = beg_update;
	return( crys_if( WIND_UPDATE ) );
}

	WORD
wind_calc(WORD wctype, UWORD kind, WORD x, WORD y, WORD w, WORD h, WORD *px, WORD *py, WORD *pw, WORD *ph)
{
	WM_WCTYPE = wctype;
	WM_WCKIND = kind;
	WM_WCIX = x;
	WM_WCIY = y;
	WM_WCIW = w;
	WM_WCIH = h;
	crys_if( WIND_CALC );
	*px = WM_WCOX;
	*py = WM_WCOY;
	*pw = WM_WCOW;
	*ph = WM_WCOH;
	return( RET_CODE );
}

	WORD
rsrc_obfix(LONG tree, WORD obj)
{
	RS_TREE = tree;
	RS_OBJ = obj;
	return( crys_if(RSRC_OBFIX) );
}

