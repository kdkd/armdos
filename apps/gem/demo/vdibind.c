/*	LVDIBIND.C	7/18/84 - 4/19/85	John Grant		*/
/*	Modified VDIBIND.C for use with LARGE Model		*/

/*
 * ARM-DOS: the LARGE model binding, with the parameter block as five flat
 * pointers (what GEMVDI expects: svc #0xEF, CX = 0473h, DX = pblock, see
 * demoarm.c vdi()).  The 32-bit addresses in CONTRL[7..8] / [9..10] are
 * only 2-byte aligned: stored and read as two words.
 */
#include "demoapp.h"

struct gsx_parameters {
    WORD *contrl;
    WORD *intin;
    WORD *ptsin;
    WORD *intout;
    WORD *ptsout;
};

struct gsx_parameters pblock = {
    &contrl[0], &intin[0], &ptsin[0], &intout[0], &ptsout[0]
};

#define i_ptsin(ptr) pblock.ptsin = (WORD *) (ptr)
#define i_intin(ptr) pblock.intin = (WORD *) (ptr)
#define i_intout(ptr) pblock.intout = (WORD *) (ptr)
#define i_ptsout(ptr) pblock.ptsout = (WORD *) (ptr)

#define i_ptr(ptr) (contrl[7] = (UWORD) (LONG) (ptr), contrl[8] = (UWORD) ((ULONG) (ptr) >> 16))
#define i_lptr1(ptr) i_ptr(ptr)
#define i_ptr2(ptr) (contrl[9] = (UWORD) (LONG) (ptr), contrl[10] = (UWORD) ((ULONG) (ptr) >> 16))
#define m_lptr2(ptr) (*(ptr) = (LONG) ((ULONG) (UWORD) contrl[9] | ((ULONG) (UWORD) contrl[10] << 16)))

	WORD
v_opnwk(WORD work_in[], WORD *handle, WORD work_out[])
{
        i_intin( work_in );
        i_intout( work_out );
        i_ptsout( work_out + 45 );

        contrl[0] = 1;
        contrl[1] = 0;
        contrl[3] = 103 ;
        vdi();

        *handle = contrl[6];    

        i_intin( intin );
        i_intout( intout );
        i_ptsout( ptsout );
	return( TRUE ) ;
}

	WORD
v_clswk(WORD handle)
{
    contrl[0] = 2;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
}

	WORD
v_clrwk(WORD handle)
{
    contrl[0] = 3;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
}

	WORD
v_updwk(WORD handle)
{
    contrl[0] = 4;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
}

	WORD
vq_chcells(WORD handle, WORD *rows, WORD *columns)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 1;
    contrl[6] = handle;
    vdi();

    *rows = intout[0];
    *columns = intout[1];
}

	WORD
v_exit_cur(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 2;
    contrl[6] = handle;
    vdi();
}

	WORD
v_enter_cur(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 3;
    contrl[6] = handle;
    vdi();
}

	WORD
v_curup(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 4;
    contrl[6] = handle;
    vdi();
}

	WORD
v_curdown(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 5;
    contrl[6] = handle;
    vdi();
}

	WORD
v_curright(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 6;
    contrl[6] = handle;
    vdi();
}

	WORD
v_curleft(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 7;
    contrl[6] = handle;
    vdi();
}

	WORD
v_curhome(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 8;
    contrl[6] = handle;
    vdi();
}

	WORD
v_eeos(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 9;
    contrl[6] = handle;
    vdi();
}

	WORD
v_eeol(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 10;
    contrl[6] = handle;
    vdi();
}

	WORD
vs_curaddress(WORD handle, WORD row, WORD column)
{
    intin[0] = row;
    intin[1] = column;

    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 11;
    contrl[6] = handle;
    vdi();
}

	WORD
v_curtext(WORD handle, BYTE *string)
{
    WORD *intstr;

    intstr = intin;
    while (*intstr++ = *string++)
      ;

    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = intstr - intin - 1;
    contrl[5] = 12;
    contrl[6] = handle;
    vdi();
}

	WORD
v_rvon(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 13;
    contrl[6] = handle;
    vdi();
}

	WORD
v_rvoff(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 14;
    contrl[6] = handle;
    vdi();
}

	WORD
vq_curaddress(WORD handle, WORD *row, WORD *column)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 15;
    contrl[6] = handle;
    vdi();

    *row = intout[0];
    *column = intout[1];
}

	WORD
vq_tabstatus(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 16;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
v_hardcopy(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 17;
    contrl[6] = handle;
    vdi();
}

	WORD
v_dspcur(WORD handle, WORD x, WORD y)
{
    ptsin[0] = x;
    ptsin[1] = y;

    contrl[0] = 5;
    contrl[1] = 1;
    contrl[3] = 0;
    contrl[5] = 18;
    contrl[6] = handle;
    vdi();
}

	WORD
v_rmcur(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 19;
    contrl[6] = handle;
    vdi();
}

	WORD
v_form_adv(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 20;
    contrl[6] = handle;
    vdi();
}

	WORD
v_output_window(WORD handle, WORD xy[])
{
    i_ptsin( xy );

    contrl[0] = 5;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[5] = 21;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_clear_disp_list(WORD handle)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 22;
    contrl[6] = handle;
    vdi();
}

	WORD
v_bit_image(WORD handle, BYTE *filename, WORD aspect, WORD x_scale, WORD y_scale, WORD h_align, WORD v_align, WORD xy[])
{
    WORD i;

    for (i = 0; i < 4; i++)
	ptsin[i] = xy[i];
    intin[0] = aspect;
    intin[1] = x_scale;
    intin[2] = y_scale;
    intin[3] = h_align;
    intin[4] = v_align;
    i = 5;
    while (intin[i++] = *filename++)
        ;

    contrl[0] = 5;
    contrl[1] = 2;
    contrl[3] = --i;
    contrl[5] = 23;
    contrl[6] = handle;
    vdi();
}

	WORD
vq_scan(WORD handle, WORD *g_height, WORD *g_slice, WORD *a_height, WORD *a_slice, WORD *factor)
{
    contrl[0] = 5;
    contrl[1] = contrl[3] = 0;
    contrl[5] = 24;
    contrl[6] = handle;
    vdi();

    *g_height = intout[0];
    *g_slice = intout[1];
    *a_height = intout[2];
    *a_slice = intout[3];
    *factor = intout[4];
}

	WORD
v_alpha_text(WORD handle, BYTE *string)
{
    WORD i;

    i = 0;
    while (intin[i++] = *string++)
        ;

    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = --i;
    contrl[5] = 25;
    contrl[6] = handle;
    vdi();
}

	WORD
vs_palette(WORD handle, WORD palette)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[5] = 60;
    contrl[6] = handle;
    intin[0] = palette;
    vdi();
    return( intout[0] );
}

	WORD
v_sound(WORD handle, WORD frequency, WORD duration)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 61;
    contrl[6] = handle;

    intin[0] = frequency;
    intin[1] = duration;
    vdi();
}

	WORD
vs_mute(WORD handle, WORD action)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[5] = 62;
    contrl[6] = handle;

    intin[0] = action;
    vdi();
    return( intout[0] );
}

	WORD
vsp_film(WORD handle, WORD index, WORD lightness)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 91;
    contrl[6] = handle;
    intin[0] = index;
    intin[1] = lightness;
    vdi();
}

	BOOLEAN
vqp_filmname(WORD handle, WORD index, BYTE name[])
{
    WORD i;

    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[5] = 92;
    contrl[6] = handle;
    intin[0] = index;
    vdi();

    if ( contrl[4] != 0) {
	for (i = 0; i < contrl[4]; i++)
	    name[i] = intout[i];
	return(TRUE);
    }
    return(FALSE);
}

	WORD
vsc_expose(WORD handle, WORD state)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 93;
    contrl[6] = handle;
    intin[0] = state;
    vdi();
}

	WORD
v_meta_extents(WORD handle, WORD min_x, WORD min_y, WORD max_x, WORD max_y)
{
    ptsin[0] = min_x;
    ptsin[1] = min_y;
    ptsin[2] = max_x;
    ptsin[3] = max_y;

    contrl[0] = 5;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[5] = 98;
    contrl[6] = handle;
    vdi();
}

	WORD
v_write_meta(WORD handle, WORD num_ints, WORD ints[], WORD num_pts, WORD pts[])
{
    i_intin( ints );
    i_ptsin( pts );

    contrl[0] = 5;
    contrl[1] = num_pts;
    contrl[3] = num_ints;
    contrl[5] = 99;
    contrl[6] = handle;
    vdi();

    i_intin( intin );
    i_ptsin( ptsin );
}

	WORD
vm_filename(WORD handle, BYTE *filename)
{
    WORD *intstr;

    intstr = intin;
    while( *intstr++ = *filename++ )
        ;

    contrl[0] = 5;
    contrl[1] = 0;
    contrl[5] = 100;
    contrl[6] = handle;
    contrl[3] = intstr - intin - 1;
    vdi();
}

	WORD
vm_pagesize(WORD handle, WORD pgwidth, WORD pgheight)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 3;
    contrl[5] = 99;
    contrl[6] = handle;
    intin[0] = 0;
    intin[1] = pgwidth;
    intin[2] = pgheight;
    vdi();
}

	WORD
vm_coords(WORD handle, WORD llx, WORD lly, WORD urx, WORD ury)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 5;
    contrl[5] = 99;
    contrl[6] = handle;
    intin[0] = 1;
    intin[1] = llx;
    intin[2] = lly;
    intin[3] = urx;
    intin[4] = ury;
    vdi();
}

	WORD
vt_resolution(WORD handle, WORD xres, WORD yres, WORD *xset, WORD *yset)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 81;
    contrl[6] = handle;
    intin[0] = xres;
    intin[1] = yres;
    vdi();
    *xset = intout[0];
    *yset = intout[1];
}

	WORD
vt_axis(WORD handle, WORD xres, WORD yres, WORD *xset, WORD *yset)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 82;
    contrl[6] = handle;
    intin[0] = xres;
    intin[1] = yres;
    vdi();
    *xset = intout[0];
    *yset = intout[1];
}

	WORD
vt_origin(WORD handle, WORD xorigin, WORD yorigin)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 83;
    contrl[6] = handle;
    intin[0] = xorigin;
    intin[1] = yorigin;
    vdi();
}

	WORD
vq_tdimensions(WORD handle, WORD *xdimension, WORD *ydimension)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 0;
    contrl[5] = 84;
    contrl[6] = handle;
    vdi();
    *xdimension = intout[0];
    *ydimension = intout[1];
}

	WORD
vt_alignment(WORD handle, WORD dx, WORD dy)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[5] = 85;
    contrl[6] = handle;
    intin[0] = dx;
    intin[1] = dy;
    vdi();
}

	WORD
v_pline(WORD handle, WORD count, WORD xy[])
{
    i_ptsin( xy );

    contrl[0] = 6;
    contrl[1] = count;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_pmarker(WORD handle, WORD count, WORD xy[])
{
    i_ptsin( xy );

    contrl[0] = 7;
    contrl[1] = count;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_gtext(WORD handle, WORD x, WORD y, BYTE *string)
{
    WORD i;

    ptsin[0] = x;
    ptsin[1] = y;
    i = 0;
    while (intin[i++] = *string++)
        ;

    contrl[0] = 8;
    contrl[1] = 1;
    contrl[3] = --i;
    contrl[6] = handle;
    vdi();
}

	WORD
v_fillarea(WORD handle, WORD count, WORD xy[])
{
    i_ptsin( xy );

    contrl[0] = 9;
    contrl[1] = count;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_cellarray(WORD handle, WORD xy[4], WORD row_length, WORD el_per_row, WORD num_rows, WORD wr_mode, WORD *colors)
{
    i_intin( colors );
    i_ptsin( xy );

    contrl[0] = 10;
    contrl[1] = 2;
    contrl[3] = row_length * num_rows;
    contrl[6] = handle;
    contrl[7] = row_length;
    contrl[8] = el_per_row;
    contrl[9] = num_rows;
    contrl[10] = wr_mode;
    vdi();

    i_intin( intin );
    i_ptsin( ptsin );
}

	WORD
v_bar(WORD handle, WORD xy[])
{
    i_ptsin( xy );

    contrl[0] = 11;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[5] = 1;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_arc(WORD handle, WORD xc, WORD yc, WORD rad, WORD sang, WORD eang)
{
    ptsin[0] = xc;
    ptsin[1] = yc;
    ptsin[2] = ptsin[3] = ptsin[4] = ptsin[5] = ptsin[7] = 0;
    ptsin[6] = rad;
    intin[0] = sang;
    intin[1] = eang;

    contrl[0] = 11;
    contrl[1] = 4;
    contrl[3] = 2;
    contrl[5] = 2;
    contrl[6] = handle;
    vdi();
}

	WORD
v_pieslice(WORD handle, WORD xc, WORD yc, WORD rad, WORD sang, WORD eang)
{
    ptsin[0] = xc;
    ptsin[1] = yc;
    ptsin[2] = ptsin[3] = ptsin[4] = ptsin[5] = ptsin[7] = 0;
    ptsin[6] = rad;
    intin[0] = sang;
    intin[1] = eang;

    contrl[0] = 11;
    contrl[1] = 4;
    contrl[3] = 2;
    contrl[5] = 3;
    contrl[6] = handle;
    vdi();
}

	WORD
v_circle(WORD handle, WORD xc, WORD yc, WORD rad)
{
    ptsin[0] = xc;
    ptsin[1] = yc;
    ptsin[2] = ptsin[3] = ptsin[5] = 0;
    ptsin[4] = rad;

    contrl[0] = 11;
    contrl[1] = 3;
    contrl[3] = 0;
    contrl[5] = 4;
    contrl[6] = handle;
    vdi();
}

	WORD
v_ellipse(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad)
{
    ptsin[0] = xc;
    ptsin[1] = yc;
    ptsin[2] = xrad;
    ptsin[3] = yrad;

    contrl[0] = 11;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[5] = 5;
    contrl[6] = handle;
    vdi();
}

	WORD
v_ellarc(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad, WORD sang, WORD eang)
{
    ptsin[0] = xc;
    ptsin[1] = yc;
    ptsin[2] = xrad;
    ptsin[3] = yrad;
    intin[0] = sang;
    intin[1] = eang;

    contrl[0] = 11;
    contrl[1] = 2;
    contrl[3] = 2;
    contrl[5] = 6;
    contrl[6] = handle;
    vdi();
}

	WORD
v_ellpie(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad, WORD sang, WORD eang)
{
    ptsin[0] = xc;
    ptsin[1] = yc;
    ptsin[2] = xrad;
    ptsin[3] = yrad;
    intin[0] = sang;
    intin[1] = eang;

    contrl[0] = 11;
    contrl[1] = 2;
    contrl[3] = 2;
    contrl[5] = 7;
    contrl[6] = handle;
    vdi();
}

	WORD
v_rbox(WORD handle, WORD xy[])
{
    i_ptsin( xy );

    contrl[0] = 11;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[5] = 8;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_rfbox(WORD handle, WORD xy[])
{
    i_ptsin( xy );

    contrl[0] = 11;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[5] = 9;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_justified(WORD handle, WORD x, WORD y, BYTE string[], WORD length, WORD word_space, WORD char_space)
{
    WORD *intstr;

    ptsin[0] = x;
    ptsin[1] = y;
    ptsin[2] = length;
    ptsin[3] = 0;
    intin[0] = word_space;
    intin[1] = char_space;
    intstr = &intin[2];
    while (*intstr++ = *string++)
        ;

    contrl[0] = 11;
    contrl[1] = 2;
    contrl[3] = intstr - intin - 1;
    contrl[5] = 10;
    contrl[6] = handle;
    vdi();
}

	WORD
vqt_justified(WORD handle, WORD x, WORD y, BYTE string[], WORD length, WORD word_space, WORD char_space, WORD offsets[])
{
    WORD *intstr,t;

    ptsin[0] = x;
    ptsin[1] = y;
    ptsin[2] = length;
    ptsin[3] = 0;
    intin[0] = word_space;
    intin[1] = char_space;
    intstr = &intin[2];
    while (*intstr++ = *string++)
        ;

    contrl[0] = 132;
    contrl[1] = 2;
    contrl[3] = intstr - intin - 1;
    contrl[6] = handle;
    i_ptsout( offsets );
    vdi();
    i_ptsout( ptsout );
    t = contrl[2];	/*this is a fix for bug in Lattice v3.2, it will*/
			/*give warning 85, if return(contrl[4]) directly*/
    return( t );
}

	WORD
vst_height(WORD handle, WORD height, WORD *char_width, WORD *char_height, WORD *cell_width, WORD *cell_height)
{
    ptsin[0] = 0;
    ptsin[1] = height;

    contrl[0] = 12;
    contrl[1] = 1;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *char_width = ptsout[0];
    *char_height = ptsout[1];
    *cell_width = ptsout[2];
    *cell_height = ptsout[3];
}

	WORD
vst_rotation(WORD handle, WORD angle)
{
    intin[0] = angle;

    contrl[0] = 13;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vs_color(WORD handle, WORD index, WORD *rgb)
{
    WORD i;

    intin[0] = index;
    for ( i = 1; i < 4; i++ )
        intin[i] = *rgb++;

    contrl[0] = 14;
    contrl[1] = 0;
    contrl[3] = 4;
    contrl[6] = handle;
    vdi();
}

	WORD
vsl_type(WORD handle, WORD style)
{
    intin[0] = style;

    contrl[0] = 15;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vsl_width(WORD handle, WORD width)
{
    ptsin[0] = width;
    ptsin[1] = 0;

    contrl[0] = 16;
    contrl[1] = 1;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();
    return( ptsout[0] );
}

	WORD
vsl_color(WORD handle, WORD index)
{
    intin[0] = index;

    contrl[0] = 17;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vsm_type(WORD handle, WORD symbol)
{
    intin[0] = symbol;

    contrl[0] = 18;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vsm_height(WORD handle, WORD height)
{
    WORD t;
    ptsin[0] = 0;
    ptsin[1] = height;

    contrl[0] = 19;
    contrl[1] = 1;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();
    t = ptsout[1];
    return( t );
}

	WORD
vsm_color(WORD handle, WORD index)
{
    intin[0] = index;

    contrl[0] = 20;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vst_font(WORD handle, WORD font)
{
    intin[0] = font;

    contrl[0] = 21;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vst_color(WORD handle, WORD index)
{
    intin[0] = index;

    contrl[0] = 22;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}
    
	WORD
vsf_interior(WORD handle, WORD style)
{
    intin[0] = style;

    contrl[0] = 23;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vsf_style(WORD handle, WORD index)
{
    intin[0] = index;

    contrl[0] = 24;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vsf_color(WORD handle, WORD index)
{
    intin[0] = index;

    contrl[0] = 25;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vq_color(WORD handle, WORD index, WORD set_flag, WORD rgb[])
{
    intin[0] = index;
    intin[1] = set_flag;

    contrl[0] = 26;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[6] = handle;
    vdi();

    rgb[0] = intout[1];
    rgb[1] = intout[2];
    rgb[2] = intout[3];
}

	WORD
vq_cellarray(WORD handle, WORD xy[], WORD row_len, WORD num_rows, WORD *el_used, WORD *rows_used, WORD *stat, WORD colors[])
{
    i_ptsin( xy );
    i_intout( colors );

    contrl[0] = 27;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[6] = handle;
    contrl[7] = row_len;
    contrl[8] = num_rows;
    vdi();

    *el_used = contrl[9];
    *rows_used = contrl[10];
    *stat = contrl[11];
    i_ptsin( ptsin );
    i_intout( intout );
}

	WORD
vrq_locator(WORD handle, WORD initx, WORD inity, WORD *xout, WORD *yout, WORD *term)
{
    ptsin[0] = initx;
    ptsin[1] = inity;

    contrl[0] = 28;
    contrl[1] = 1;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *xout = ptsout[0];
    *yout = ptsout[1];
    *term = intout[0];
}

	WORD
vsm_locator(WORD handle, WORD initx, WORD inity, WORD *xout, WORD *yout, WORD *term)
{
    ptsin[0] = initx;
    ptsin[1] = inity;

    contrl[0] = 28;
    contrl[1] = 1;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *xout = ptsout[0];
    *yout = ptsout[1];
    *term = intout[0];
    return( (contrl[4] << 1) | contrl[2] );
}

	WORD
vrq_valuator(WORD handle, WORD val_in, WORD *val_out, WORD *term)
{
    intin[0] = val_in;

    contrl[0] = 29;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    *val_out = intout[0];
    *term = intout[1];
}

	WORD
vsm_valuator(WORD handle, WORD val_in, WORD *val_out, WORD *term, WORD *status)
{
    intin[0] = val_in;

    contrl[0] = 29;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    *val_out = intout[0];
    *term = intout[1];
    *status = contrl[4];
}

	WORD
vrq_choice(WORD handle, WORD in_choice, WORD *out_choice)
{
    intin[0] = in_choice;

    contrl[0] = 30;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    *out_choice = intout[0];
}

	WORD
vsm_choice(WORD handle, WORD *choice)
{
    WORD t;
    contrl[0] = 30;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *choice = intout[0];
    t = contrl[4];	/*this is a fix for bug in Lattice v3.2, it will*/
			/*give warning 85, if return(contrl[4]) directly*/
    return( t );
}

	WORD
vrq_string(WORD handle, WORD length, WORD echo_mode, WORD echo_xy[], BYTE *string)
{
    WORD    count;

    intin[0] = length;
    intin[1] = echo_mode;
    i_ptsin( echo_xy );

    contrl[0] = 31;
    contrl[1] = echo_mode;
    contrl[3] = 2;
    contrl[6] = handle;
    vdi();

    for (count = 0; count < contrl[4]; count++)
      *string++ = intout[count];
    *string = 0;  
    i_ptsin( ptsin );
}

	WORD
vsm_string(WORD handle, WORD length, WORD echo_mode, WORD echo_xy[], BYTE *string)
{
    WORD    count,t;

    intin[0] = length;
    intin[1] = echo_mode;
    i_ptsin( echo_xy );

    contrl[0] = 31;
    contrl[1] = echo_mode;
    contrl[3] = 2;
    contrl[6] = handle;
    vdi();

    for (count = 0; count < contrl[4]; count++)
      *string++ = intout[count];
    *string = 0;  
    i_ptsin( ptsin );
    t = contrl[4];	/*this is a fix for bug in Lattice v3.2, it will*/
			/*give warning 85, if return(contrl[4]) directly*/
    return( t );
}

	WORD
vswr_mode(WORD handle, WORD mode)
{
    intin[0] = mode;

    contrl[0] = 32;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vsin_mode(WORD handle, WORD dev_type, WORD mode)
{
    intin[0] = dev_type;
    intin[1] = mode;

    contrl[0] = 33;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[6] = handle;
    vdi();
}

	WORD
vql_attributes(WORD handle, WORD attributes[])
{
    i_intout( attributes );

    contrl[0] = 35;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_intout( intout );
    attributes[3] = ptsout[0];
}

	WORD
vqm_attributes(WORD handle, WORD attributes[])
{
    i_intout( attributes );

    contrl[0] = 36;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_intout( intout );
    attributes[3] = ptsout[1];
}

	WORD
vqf_attributes(WORD handle, WORD attributes[])
{
    i_intout( attributes );

    contrl[0] = 37;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_intout( intout );
}

	WORD
vqt_attributes(WORD handle, WORD attributes[])
{
    i_intout( attributes );
    i_ptsout( attributes + 6 );

    contrl[0] = 38;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_intout( intout );
    i_ptsout( ptsout );
}

	WORD
vst_alignment(WORD handle, WORD hor_in, WORD vert_in, WORD *hor_out, WORD *vert_out)
{
    intin[0] = hor_in;
    intin[1] = vert_in;

    contrl[0] = 39;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[6] = handle;
    vdi();

    *hor_out = intout[0];
    *vert_out = intout[1];
}

	WORD
v_opnvwk(WORD work_in[], WORD *handle, WORD work_out[])
{
   i_intin( work_in );
   i_intout( work_out );
   i_ptsout( work_out + 45 );

   contrl[0] = 100;
   contrl[1] = 0;
   contrl[3] = 11;
   contrl[6] = *handle;
   vdi();

   *handle = contrl[6];    
   i_intin( intin );
   i_intout( intout );
   i_ptsout( ptsout );
}

	WORD
v_clsvwk(WORD handle)
{
    contrl[0] = 101;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
}

	WORD
vq_extnd(WORD handle, WORD owflag, WORD work_out[])
{
    i_intout( work_out );
    i_ptsout( work_out + 45 );
    intin[0] = owflag;

    contrl[0] = 102;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    i_intout( intout );
    i_ptsout( ptsout );
}

	WORD
v_contourfill(WORD handle, WORD x, WORD y, WORD index)
{
    intin[0] = index;
    ptsin[0] = x;
    ptsin[1] = y;

    contrl[0] = 103;
    contrl[1] = contrl[3] = 1;
    contrl[6] = handle;
    vdi();
}

	WORD
vsf_perimeter(WORD handle, WORD per_vis)
{
    intin[0] = per_vis;

    contrl[0] = 104;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
v_get_pixel(WORD handle, WORD x, WORD y, WORD *pel, WORD *index)
{
    ptsin[0] = x;
    ptsin[1] = y;

    contrl[0] = 105;
    contrl[1] = 1;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *pel = intout[0];
    *index = intout[1];
}

	WORD
vst_effects(WORD handle, WORD effect)
{
    intin[0] = effect;

    contrl[0] = 106;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vst_point(WORD handle, WORD point, WORD *char_width, WORD *char_height, WORD *cell_width, WORD *cell_height)
{
    intin[0] = point;

    contrl[0] = 107;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    *char_width = ptsout[0];
    *char_height = ptsout[1];
    *cell_width = ptsout[2];
    *cell_height = ptsout[3];
    return( intout[0] );
}

	WORD
vsl_ends(WORD handle, WORD beg_style, WORD end_style)
{
    intin[0] = beg_style;
    intin[1] = end_style;

    contrl[0] = 108;
    contrl[1] = 0;
    contrl[3] = 2;
    contrl[6] = handle;
    vdi();
}

	WORD
vro_cpyfm(WORD handle, WORD wr_mode, WORD xy[], void *srcMFDB, void *desMFDB)
{
    intin[0] = wr_mode;
    i_ptr( srcMFDB );
    i_ptr2( desMFDB );
    i_ptsin( xy );

    contrl[0] = 109;
    contrl[1] = 4;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
vr_trnfm(WORD handle, void *srcMFDB, void *desMFDB)
{
    i_ptr( srcMFDB );
    i_ptr2( desMFDB );	

    contrl[0] = 110;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
}

	WORD
vsc_form(WORD handle, WORD *cur_form)
{
    i_intin( cur_form );

    contrl[0] = 111;
    contrl[1] = 0;
    contrl[3] = 37;
    contrl[6] = handle;
    vdi();

    i_intin( intin );
}

	WORD
vsf_udpat(WORD handle, WORD fill_pat[], WORD planes)
{
    i_intin( fill_pat );

    contrl[0] = 112;
    contrl[1] = 0;
    contrl[3] = 16*planes;
    contrl[6] = handle;
    vdi();
    i_intin( intin );	
}

	WORD
vsl_udsty(WORD handle, WORD pattern)
{
    intin[0] = pattern;

    contrl[0] = 113;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
}

	WORD
vr_recfl(WORD handle, WORD *xy)
{
    i_ptsin( xy );

    contrl[0] = 114;
    contrl[1] = 2;
    contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}



	WORD
vqin_mode(WORD handle, WORD dev_type, WORD *mode)
{
    intin[0] = dev_type;

    contrl[0] = 115;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    *mode = intout[0];
}

	WORD
vqt_extent(WORD handle, BYTE string[], WORD extent[])
{
    WORD *intstr;

    intstr = intin;
    while (*intstr++ = *string++)
        ;
    i_ptsout( extent );

    contrl[0] = 116;
    contrl[1] = 0;
    contrl[3] = intstr - intin - 1;
    contrl[6] = handle;
    vdi();

    i_ptsout( ptsout );
}

	WORD
vqt_width(WORD handle, BYTE character, WORD *cell_width, WORD *left_delta, WORD *right_delta)
{
    intin[0] = character;

    contrl[0] = 117;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    *cell_width = ptsout[0];
    *left_delta = ptsout[2];
    *right_delta = ptsout[4];
    return( intout[0] );
}

	WORD
vex_timv(WORD handle, LONG tim_addr, LONG *old_addr, WORD *scale)
{
    i_lptr1( tim_addr );

    contrl[0] = 118;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    m_lptr2( old_addr );
    *scale = intout[0];
}

	WORD
vst_load_fonts(WORD handle, WORD select)
{
    contrl[0] = 119;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
    return( intout[0] );
}

	WORD
vst_unload_fonts(WORD handle, WORD select)
{
    contrl[0] = 120;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
}

	WORD
vrt_cpyfm(WORD handle, WORD wr_mode, WORD xy[], void *srcMFDB, void *desMFDB, WORD *index)
{
    intin[0] = wr_mode;
    intin[1] = *index++;
    intin[2] = *index;		
    i_ptr( srcMFDB );
    i_ptr2( desMFDB );
    i_ptsin( xy );

    contrl[0] = 121;
    contrl[1] = 4;
    contrl[3] = 3;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
v_show_c(WORD handle, WORD reset)
{
    intin[0] = reset;

    contrl[0] = 122;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();
}

	WORD
v_hide_c(WORD handle)
{
    contrl[0] = 123;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();
}

	WORD
vq_mouse(WORD handle, WORD *status, WORD *px, WORD *py)
{
    contrl[0] = 124;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *status = intout[0];
    *px = ptsout[0];
    *py = ptsout[1];
}

	WORD
vex_butv(WORD handle, LONG usercode, LONG *savecode)
{
    i_lptr1( usercode );   

    contrl[0] = 125;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    m_lptr2( savecode );
}

	WORD
vex_motv(WORD handle, LONG usercode, LONG *savecode)
{
    i_lptr1( usercode );

    contrl[0] = 126;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    m_lptr2( savecode );
}

	WORD
vex_curv(WORD handle, LONG usercode, LONG *savecode)
{
    i_lptr1( usercode );

    contrl[0] = 127;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    m_lptr2( savecode );
}

	WORD
vq_key_s(WORD handle, WORD *status)
{
    contrl[0] = 128;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *status = intout[0];
}

	WORD
vs_clip(WORD handle, WORD clip_flag, WORD xy[])
{
    i_ptsin( xy );
    intin[0] = clip_flag;

    contrl[0] = 129;
    contrl[1] = 2;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    i_ptsin( ptsin );
}

	WORD
vqt_name(WORD handle, WORD element_num, BYTE name[])
{
    WORD i;

    intin[0] = element_num;

    contrl[0] = 130;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[6] = handle;
    vdi();

    for (i = 0 ; i < 32 ; i++)
	name[i] = intout[i + 1];
    return( intout[0] );
}

	WORD
vqt_font_info(WORD handle, WORD *minADE, WORD *maxADE, WORD distances[], WORD *maxwidth, WORD effects[])
{
    contrl[0] = 131;
    contrl[1] = contrl[3] = 0;
    contrl[6] = handle;
    vdi();

    *minADE = intout[0];
    *maxADE = intout[1];
    *maxwidth = ptsout[0];
    distances[0] = ptsout[1];
    distances[1] = ptsout[3];
    distances[2] = ptsout[5];
    distances[3] = ptsout[7];
    distances[4] = ptsout[9];
    effects[0] = ptsout[2];
    effects[1] = ptsout[4];
    effects[2] = ptsout[6];
}
/* The following functions are GEM/3 only*/
	VOID
v_copies(WORD handle, WORD count)
{
    contrl[0] = 5;
    contrl[1] = 0;
    contrl[3] = 1;
    contrl[5] = 28;
    contrl[6] = handle;
    intin[0] = count;
    vdi();
}
	VOID
v_etext(WORD handle, WORD x, WORD y, UBYTE string[], WORD offsets[])
{
    WORD	*intptr, *ptsptr;
    
    intptr = intin;
    ptsptr = ptsin;
    *ptsptr++ = x;
    *ptsptr++ = y;
    while ((*intptr++ = *string++) != 0) {
	*ptsptr++ = *offsets++;
	*ptsptr++ = *offsets++;
    }
    contrl[0] = 11;
    contrl[1] = intptr - intin;
    contrl[3] = intptr - intin - 1;
    contrl[5] = 11;
    contrl[6] = handle;
    vdi();
}

	VOID
v_orient(WORD handle, WORD orientation)
{
    contrl[ 0 ] = 5 ;
    contrl[ 1 ] = 0 ;
    contrl[ 3 ] = 1 ;
    contrl[ 5 ] = 27 ;
    contrl[ 6 ] = handle ;
    intin[ 0 ] = orientation ;
    vdi() ;
} /* v_orient() */

/* end of vtray.c */

	VOID
v_tray(WORD handle, WORD tray)
{
    contrl[ 0 ] = 5 ;
    contrl[ 1 ] = 0 ;
    contrl[ 3 ] = 1 ;
    contrl[ 5 ] = 29 ;
    contrl[ 6 ] = handle ;
    intin[ 0 ] = tray ;
    vdi() ;
} /* v_tray() */

/* end of vtray.c */

    WORD
v_xbit_image( handle, filename, aspect, x_scale, y_scale, h_align, v_align, 
			rotate, background, foreground, xy )
WORD handle, aspect, x_scale, y_scale, h_align, v_align;
WORD rotate, background, foreground;
WORD xy[];
BYTE *filename;
{
    WORD ii;

    for (ii = 0; ii < 4; ii++)
	ptsin[ ii ] = xy[ ii ] ;
    intin[0] = aspect;
    intin[1] = x_scale;
    intin[2] = y_scale;
    intin[3] = h_align;
    intin[4] = v_align;
    intin[5] = rotate ;
    intin[6] = background ;
    intin[7] = foreground ;
    ii = 8 ;
    while ( *filename )
    	{
	    intin[ ii++ ] = (WORD)( *filename++ ) ;
	}
    intin[ ii ] = 0 ;		/* final null */
    contrl[0] = 5;
    contrl[1] = 2;
    contrl[3] = ii ;
    contrl[5] = 101 ;
    contrl[6] = handle ; 
    vdi();
    return( TRUE ) ;
}

	WORD
vst_ex_load_fonts(WORD handle, WORD select, WORD font_max, WORD font_free)
{
    contrl[0] = 119;
    contrl[1] = 0;
    contrl[3] = 3;
    contrl[6] = handle;
    intin[0] = select;
    intin[1] = font_max;
    intin[2] = font_free;
    vdi();
    return( intout[0] );
}
