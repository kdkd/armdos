
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

#include    "vdi.h"

/* ARM-DOS: the entries are cast to one function type (the DRI table mixed
   VOID and WORD functions, which K&R C did not mind). */
typedef VOID (*VFN)(VOID);
#define F(x) ((VFN)(x))

static VFN jmptb1[] =
{
	F(v_nop),
	F(v_opnwk),
	F(DINIT_G),
	F(CLEARMEM),
	F(v_nop),
	F(CHK_ESC),
	F(v_pline),
	F(v_pmarker),
	F(d_gtext),
	F(plygn),
	F(v_nop),
	F(v_gdp),
	F(dst_height),
	F(dst_rotation),
	F(S_COLMAP),
	F(vsl_type),
	F(vsl_width),
	F(vsl_color),
	F(vsm_type),
	F(vsm_height),
	F(vsm_color),
	F(dst_font),
	F(dst_color),
	F(vsf_interior),
	F(vsf_style),
	F(vsf_color),
	F(vq_color),
	F(v_nop),
	F(v_locator),
	F(v_nop),
	F(v_choice),
	F(v_string),
	F(vswr_mode),
	F(vsin_mode),
	F(v_nop),
	F(vql_attr),
	F(vqm_attr),
	F(vqf_attr),
	F(dqt_attributes),
	F(dst_alignment)
};

static VFN jmptb2[] =
{
	F(d_opnvwk),
	F(v_nop),
	F(vq_extnd),
	F(v_nop),
	F(vsf_perimeter),
	F(v_nop),
	F(dst_style),
	F(dst_point),
	F(vsl_ends),
	F(dro_cpyfm),
	F(TRAN_FM),
	F(XFM_CRFM),
	F(XFM_UDFL),
	F(vsl_udsty),
	F(dr_recfl),
	F(vqi_mode),
	F(dqt_extent),
	F(dqt_width),
	F(EX_TIMV),
	F(dt_loadfont),
	F(dt_unloadfont),
	F(drt_cpyfm),
	F(v_show_c),
	F(HIDE_CUR),
	F(vq_mouse_status),
	F(VEX_BUTV),
	F(VEX_MOTV),
	F(VEX_CURV),
	F(vq_key_s),
	F(s_clip),
	F(dqt_name),
	F(dqt_fontinfo),
	F(dqt_just)
};


VOID
SCREEN(VOID)
{
	CONTRL[2] = CONTRL[4] = 0;
	if (CONTRL[0] >= 0 && CONTRL[0] <= 39)
	    (*jmptb1[CONTRL[0]])();
	else if (CONTRL[0] >= 100 && CONTRL[0] <= 132 )
	    (*jmptb2[CONTRL[0] - 100])();
}
