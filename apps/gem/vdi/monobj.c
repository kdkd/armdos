
/*************************************************************************
**       Copyright 1999, Caldera Thin Clients, Inc.                     ** 
**       This software is licenced under the GNU Public License.        **
**       Please see LICENSE.TXT for further information.                ** 
**                                                                      ** 
**                  Historical Copyright                                ** 
**									**
**									**
**									**
**  Copyright (c) 1987, Digital Research, Inc. All Rights Reserved.	**
**  The Software Code contained in this listing is proprietary to	**
**  Digital Research, Inc., Monterey, California and is covered by U.S.	**
**  and other copyright protection.  Unauthorized copying, adaptation,	**
**  distribution, use or display is prohibited and may be subject to 	**
**  civil and criminal penalties.  Disclosure to others is prohibited.	**
**  For the terms and conditions of software code use refer to the 	**
**  appropriate Digital Research License Agreement.			**
**									**
*************************************************************************/

#include "vdi.h"

/* ARM-DOS: the attribute variables (line_index ... patmsk) are fields of
 * the current virtual workstation (struct vws, vdi.h). */

/* filled area variables */

WORD	y,odeltay,deltay,deltay1,deltay2;
WORD	fill_miny,fill_maxy;
WORD	fill_intersect;

/* gdp area variables */
WORD	xc, yc, xrad, yrad, del_ang, beg_ang, end_ang;
WORD	start, angle, n_steps;

/* attribute environment save variables */
WORD	s_fill_per, *s_patptr, s_patmsk, s_nxtpat;
WORD	s_begsty, s_endsty;

	VOID
v_nop(VOID)
{}



/* CLEAR_WORKSTATION: */
	WORD
v_clrwk(VOID)
{
	CLEARMEM();
}

/* UPDATE_WORKSTATION: */
	WORD
v_updwk(VOID)
{
}

/* SET_COLOR_REP: */
	WORD
vs_color(VOID)
{
    S_COLMAP();
}

/* S_LINE_TYPE: */
	WORD
vsl_type(VOID)
{
	CONTRL[4]=1;
	line_index = ( INTIN[0] - 1 );
	if ((line_index >= MAX_LINE_STYLE) || (line_index < 0))
	  line_index = 0; 
	line_qi = INTOUT[0] = (line_index + 1);
} 



/*-------------
 * S_LINE_WIDTH
 *-------------*/
	WORD
vsl_width(VOID)
{
	WORD i, j, x, y, d, low, high;

	/* Limit the requested line width to a reasonable value. */

	if (PTSIN[0] < 1)
	  PTSIN[0] = 1;
	else if (PTSIN[0] > MAX_L_WIDTH)
	  PTSIN[0] = MAX_L_WIDTH;

	/* Make the line width an odd number (one less, if even). */

	PTSIN[0] = ((PTSIN[0] - 1)/2)*2 + 1;

	/* Set the line width internals and the return parameters.  
	   Return if the line width is being set to one. */

	CONTRL[2] = 1;
	PTSOUT[1] = 0;
	if ( (PTSOUT[0] = line_qw = line_width = PTSIN[0]) == 1)
	  return;

	/* Initialize the circle DDA.  "y" is set to the radius. */

	x = 0;
	y = (line_width + 1)/2;
	d = 3 - 2*y;

#if Y_ASPECT >= X_ASPECT

	for ( i = 0 ; i < MAX_L_WIDTH ; i++ ) {
	  q_circle[i] = 0 ;
	}
#else

	for (i = 0 ; i < ((MAX_L_WIDTH*X_ASPECT/Y_ASPECT))/2+1) ; i++ ) {
	  q_circle[i] = 0 ;
	}

#endif

	/* Do an octant, starting at north.  
	   The values for the next octant (clockwise) will
	   be filled by transposing x and y. */

	 while (x < y) {
	   q_circle[y] = x;
	   q_circle[x] = y;

	   if (d < 0) {
	     d = d + (4 * x) + 6;
	   }
	   else {
	     d = d + (4 * (x - y)) + 10;
	     y--;
	   }
	   x++;
	 }  /* End while loop. */

	 if (x == y)
	   q_circle[x] = x;

	 /* Calculate the number of vertical pixels required. */

	 num_qc_lines = (line_width * xsize / ysize) / 2 + 1;

	 /* Fake a pixel averaging when converting to 
	    non-1:1 aspect ratio. */

#if Y_ASPECT >= X_ASPECT

	low = 0;
	for (i = 0; i < num_qc_lines; i++) {
	  high = ((2 * i + 1) * ysize / xsize) / 2;
	  d = 0;

	  for (j = low; j <= high; j++) {
	    d += q_circle[j];
	  }

	  q_circle[i] = d/(high - low + 1);
	  low = high + 1;
	} 

#else

	for (i = num_qc_lines - 1; i >= 0; i--) {
	  q_circle[i] = q_circle[(2 * i * ysize / xsize + 1) / 2];
	}

#endif

}


 
/* S_END_STYLE: */
	WORD
vsl_ends(VOID)
{
  CONTRL[4] = 2;
  line_beg = INTIN[0];
  if ( (line_beg < 0) || (line_beg > 2) )
    line_beg = 0;
  line_end = INTIN[1];
  if ( (line_end < 0) || (line_end > 2) )
    line_end = 0;
  INTOUT[0] = line_beg;
  INTOUT[1] = line_end;
}  /* End "vsl_ends". */

/* S_LINE_COLOR: */
	WORD
vsl_color(VOID)
{
	CONTRL[4]=1;
	line_color = INTIN[0];
	if ((line_color >= MAX_COLOR) || (line_color < 0))
	  line_color = 1; 
	INTOUT[0] = line_color;
	line_qc = line_color;
	line_color = MAP_COL[ line_color ];
}

/* S_MARK_TYPE: */
	WORD
vsm_type(VOID)
{
	mark_qi = INTIN[0];
	if ((mark_qi > MAX_MARK_INDEX) || (mark_qi < 1))
	  mark_qi = 3; 
	mark_index = (INTOUT[0] = mark_qi) - 1;
}

/* S_MARK_SCALE: */
	WORD
vsm_height(VOID)
{
  /* Limit the requested marker height to a reasonable value. */
  if (PTSIN[1] < DEF_MKHT)
    PTSIN[1] = DEF_MKHT;
  else if (PTSIN[1] > MAX_MKHT)
    PTSIN[1] = MAX_MKHT;

  /* Set the marker height internals and the return parameters. */
  mark_height = PTSIN[1];
  mark_scale = (mark_height + DEF_MKHT/2)/DEF_MKHT;
  CONTRL[2] = 1;
  PTSOUT[0] = mark_scale*DEF_MKWD;
  PTSOUT[1] = mark_scale*DEF_MKHT;
  FLIP_Y = 1;
}  /* End "vsm_height". */

/* S_MARK_COLOR: */
	WORD
vsm_color(VOID)
{
	mark_color = INTIN[0];
	if ((mark_color >= MAX_COLOR) || (mark_color < 0))
	  mark_color = 1; 
	INTOUT[0] = mark_color;
	mark_qc = mark_color;
	mark_color = MAP_COL[ mark_color ];
}

 
/* S_FILL_STYLE: */
	WORD
vsf_interior(VOID)
{
	CONTRL[4]=1;
	fill_style = INTIN[0];
	if ((fill_style > MX_FIL_STYLE) || (fill_style < 0))
	  fill_style = 0; 
	INTOUT[0] = fill_style;
	NEXT_PAT = 0;
	if ( fill_style == 4 )
	   NEXT_PAT = udpt_np;
	st_fl_ptr();
}
	
/* S_FILL_INDEX: */
	WORD
vsf_style(VOID)
{
	CONTRL[4]=1;
	fill_qi = INTIN[0];
	if (fill_style == 2 )
	{
	    if ((fill_qi > MX_FIL_PAT_INDEX) || (fill_qi < 1))
	  	fill_qi = 1; 
	}
	else 
	{
	    if ((fill_qi > MX_FIL_HAT_INDEX) || (fill_qi < 1))
	  	fill_qi = 1; 
	}
	fill_index = (INTOUT[0] = fill_qi) - 1;
	st_fl_ptr();
}

/* S_FILL_COLOR: */
	WORD
vsf_color(VOID)
{
	CONTRL[4]=1;
	fill_color = INTIN[0];	    
	if ((fill_color >= MAX_COLOR) || (fill_color < 0))
	  fill_color = 1; 
	fill_qc = INTOUT[0] = fill_color;
	fill_color = MAP_COL[ fill_color ];
}

/* INQUIRE_COLOR_REP: */
	WORD
vq_color(VOID)
{
    I_COLMAP();
    CONTRL[4] = 4;
    CONTRL[2] = 0;		
}

#if ibmvdi
/* LOCATOR_INPUT: */
	WORD
v_locator(VOID)
{
	WORD	i;

	INTIN[0] = 1;

        /* Set the initial locator position. */
	if ( loc_mode == 0 )
	{
	    HIDE_CNT = 1;
	    DIS_CUR();
	    while ( (i = GLOC_KEY()) != 1 ); /* loop till some event */
	    CONTRL[4] = 1;
	    INTOUT[0] = TERM_CH & 0x00ff;
	    CONTRL[2] = 1;
	    PTSOUT[0] = X1;
	    PTSOUT[1] = Y1;
	    HIDE_CUR();
	}
	else
	{
	  i = GLOC_KEY();
	  CONTRL[2] = 1;
	  CONTRL[4] = 0;
	  switch ( i )
	    {
	      case 0:
	        CONTRL[2] = 0;
		break;

	      case 1:
	        CONTRL[2] = 0;
		CONTRL[4] = 1;
		INTOUT[0] = TERM_CH & 0x00ff;
		break;

	      case 2:
		PTSOUT[0] = X1;
		PTSOUT[1] = Y1;
		break;

	     case 3:
		CONTRL[4] = 1;
		PTSOUT[0] = X1;
		PTSOUT[1] = Y1;	
		break;
	    }
	}
}
#else
	WORD
v_locator(VOID)
{
}
#endif

/* SHOW CURSOR */
	WORD
v_show_c(VOID)
{
  if ( !INTIN[0] )
    HIDE_CNT = 1;
  DIS_CUR();
}

/* HIDE CURSOR */
	WORD
v_hide_c(VOID)
{
  HIDE_CUR();
}

/* RETURN MOUSE BUTTON STATUS */
	WORD
vq_mouse_status(VOID)
{
	INTOUT[0] = MOUSE_BT;
	CONTRL[4] = 1;
	CONTRL[2] = 1;
	PTSOUT[0] = GCURX;
	PTSOUT[1] = GCURY;
}

/* VALUATOR_INPUT: */
	WORD
v_valuator(VOID)
{
}
#if ibmvdi
/* CHOICE_INPUT: */
	WORD
v_choice(VOID)
{
	WORD	i;
	if ( chc_mode == 0 )
	{
	  CONTRL[4]=1;
	  while ( GCHC_KEY() != 1 );
	    INTOUT[0]=TERM_CH &0x00ff;
	}
	else
	{
	  i = GCHC_KEY();
	  CONTRL[4]=i;
	  if (i == 1)
	    INTOUT[0]=TERM_CH &0x00ff;
	  if (i == 2)
	    INTOUT[1]=TERM_CH & 0x00ff;
	}	
}
#else
	WORD
v_choice(VOID)
{
}
#endif

/* STRING_INPUT: */
	WORD
v_string(VOID)
{
	WORD	i,j,k,mask;
	mask = 0x00ff;
	j = INTIN[0];
	if ( j < 0 )
	{
	    j = -j;
	    mask = 0xffff;
	}
	if ( !str_mode )  /* Request mode */
	{
	  TERM_CH = 0;
	  for ( i = 0;( i < j) && ((TERM_CH & 0x00ff) != 0x000d); i++)
	  {
	    while ( GCHR_KEY() == 0 );
	    INTOUT[i] = TERM_CH = TERM_CH & mask;	
	  }
	  if ( ( TERM_CH & 0x00ff )== 0x000d )
	    --i;
	  CONTRL[4] = i; 
	}
	else  /* Sample mode */
	{
	  i = 0;
	  while ( (i < j) && (GCHR_KEY() != 0) )
	    INTOUT[i++] = TERM_CH & mask;
	  CONTRL[4] = i;
	}
}
/* Return Shift, Control, Alt State */
	WORD
vq_key_s(VOID)
{
	CONTRL[4] = 1;
	INTOUT[0] = GSHIFT_S();
}

/* SET_WRITING_MODE: */
	WORD
vswr_mode(VOID)
{
	CONTRL[4]=1;
	WRT_MODE = (INTIN[0] - 1);
	if ((WRT_MODE > MAX_MODE) | (WRT_MODE < 0))
	  WRT_MODE = 0;
	
	write_qm = INTOUT[0] = (WRT_MODE + 1);
}
/* SET_INPUT_MODE: */
	WORD
vsin_mode(VOID)
{
	WORD	i;
	CONTRL[4] = 1;
	i = INTIN[1];
	INTOUT[0] = i;
	i--; 
	switch ( INTIN[0] )
	{	
	  case 0:
	    break;
	  
	  case 1:	/* locator */
	    loc_mode = i;
	    break;

	  case 2:	/* valuator */
	    val_mode = i;
	    break;

	  case 3: /* choice */
	    chc_mode = i;
	    break;

	  case 4: /* string */
	    str_mode = i;
	    break;
	}
}

/* INQUIRE INPUT MODE: */
	WORD
vqi_mode(VOID)
{
	CONTRL[4] = 1;
	switch ( INTIN[0] )
	{	
	  case 0:
	    break;
	  
	  case 1:	/* locator */
	    INTOUT[0] = loc_mode;
	    break;

	  case 2:	/* valuator */
	    INTOUT[0] = val_mode;
	    break;

	  case 3: /* choice */
	    INTOUT[0] = chc_mode;
	    break;

	  case 4: /* string */
	    INTOUT[0] = str_mode;
	    break;
	}
}
/* CLOSE_VWORKSTATION: */
	WORD
v_clsvwk(VOID)
{
}
	    
/* ST_FILLPERIMETER: */
	WORD
vsf_perimeter(VOID)
{
	if ( INTIN[0] == 0 )
	{
	  INTOUT[0] = 0;  
	  fill_per = FALSE;
	}
	else
	{
	  INTOUT[0] = 1;
	  fill_per = TRUE;
	}
	CONTRL[4] = 1;
}

/* ST_UD_LINE_STYLE: */
	WORD
vsl_udsty(VOID)
{
	LINE_STYL[6] = INTIN[0];
}

/* Set Clip Region */
	WORD
s_clip(VOID)
{
    CLIP = INTIN[0];
    if ( CLIP != 0 )
    {		
        arb_corner(PTSIN, ULLR);
    	XMN_CLIP = PTSIN[0];
    	YMN_CLIP = PTSIN[1];
    	XMX_CLIP = PTSIN[2];
    	YMX_CLIP = PTSIN[3];
    	if ( XMN_CLIP < 0 )
            XMN_CLIP = 0;
    	if ( XMX_CLIP > DEV_TAB[0] )
            XMX_CLIP = DEV_TAB[0];
    	if ( YMN_CLIP < 0 )
	    YMN_CLIP = 0;
    	if ( YMX_CLIP > DEV_TAB[1] )
	    YMX_CLIP = DEV_TAB[1];
    }	
    else
    {
        XMN_CLIP = YMN_CLIP = 0;
        XMX_CLIP = xres;
        YMX_CLIP = yres;
   }  /* End else:  clipping turned off. */
}

	WORD
arb_corner(WORD xy [], WORD type)
{
  /* Local declarations. */
  WORD temp;

  /* Fix the x coordinate values, if necessary. */
  if (xy[0] > xy[2])
  {
    temp = xy[0];
    xy[0] = xy[2];
    xy[2] = temp;
  }  /* End if:  "x" values need to be swapped. */

  /* Fix y values based on whether traditional (ll, ur) or raster-op */
  /* (ul, lr) format is desired.                                     */
  if ( ( (type == LLUR) && (xy[1] < xy[3]) ) ||
       ( (type == ULLR) && (xy[1] > xy[3]) ) )
    {
      temp = xy[1];
      xy[1] = xy[3];
      xy[3] = temp;
    }  /* End if:  "y" values need to be swapped. */
}  /* End "arb_corner". */

	WORD
dro_cpyfm(VOID)
{
  arb_corner(PTSIN, ULLR);
  arb_corner(&PTSIN[4], ULLR);
  COPYTRAN = 0; 	
  COPY_RFM();
}  /* End "dr_cpyfm". */

	WORD
drt_cpyfm(VOID)
{
  arb_corner(PTSIN, ULLR);
  arb_corner(&PTSIN[4], ULLR);
  COPYTRAN = 0xffff; 	
  COPY_RFM();
}  /* End "dr_cpyfm". */

	WORD
dr_recfl(VOID)
{
  /* Perform arbitrary corner fix-ups and invoke the rectangle fill routine. */
            arb_corner(PTSIN, LLUR);
	    FG_BP_1 = fill_color;
	    LSTLIN = FALSE;
	    X1 = PTSIN[0];
	    X2 = PTSIN[2];
	    Y2 = PTSIN[1];
	    Y1 = PTSIN[3];
	    RECTFILL();
}  /* End "dr_recfl". */
/* transform form entry code */
	WORD
r_trnfm(VOID)
{
  TRAN_FM();
} /* end of trnsform form */
/* exchange timer vector */
	WORD
dex_timv(VOID)
{
  EX_TIMV();
} /* end of timer vector */

