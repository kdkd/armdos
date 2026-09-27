/*
 * vdi.h - the GEM VDI screen driver for ARM-DOS: shared declarations.
 *
 * Part of the ARM-DOS port of GEM (apps/gem).  The C modules monobj.c,
 * monout.c, opttext.c, isin.c and jmptbl.c are Digital Research's GEM/3
 * screen-driver sources (Copyright 1999 Caldera Thin Clients, Inc., GNU GPL
 * v2, see apps/gem/LICENSE.TXT); this header replaces their GSXDEF.H /
 * GSXEXTRN.H and the data segment that the x86 assembler modules defined.
 *
 * On the PC, GDOS gave every virtual workstation its own copy of the
 * driver's data segment.  Here the per-workstation attributes live in a
 * struct vws, and the old global names are macros for fields of the current
 * one (vw), so the DRI C code compiles unchanged.
 */
#ifndef GEM_VDI_H
#define GEM_VDI_H

#include "portab.h"
#include "fontdef.h"

/* ------------------------------------------------ device independent ---- */
#define MAX_COLOR	16
#define MAX_LINE_STYLE	7
#define MAX_L_WIDTH	40
#define MAX_MARK_INDEX	6
#define MAX_FONT	1
#define MX_FIL_STYLE	4
#define MX_FIL_HAT_INDEX 12
#define MX_FIL_PAT_INDEX 24
#define MAX_MODE	3
#define MAX_ARC_CT	70
#define X_ASPECT	1		/* only used to pick the q_circle size */
#define Y_ASPECT	1

#define SQUARED 0
#define ARROWED 1
#define CIRCLED 2
#define LLUR    0
#define ULLR    1

#define CONTRL_SIZE	12
#define INTIN_SIZE	256
#define PTSIN_SIZE	512
#define INTOUT_SIZE	128
#define PTSOUT_SIZE	16

#define xres      DEV_TAB[0]
#define yres      DEV_TAB[1]
#define xsize     DEV_TAB[3]
#define ysize     DEV_TAB[4]
#define iptscnt   CONTRL[1]
#define optscnt   CONTRL[2]
#define iintcnt   CONTRL[3]
#define ointcnt   CONTRL[4]
#define gdp_code  CONTRL[5]
#define	DEF_CHWT  SIZ_TAB[0]
#define	DEF_CHHT  SIZ_TAB[1]
#define	DEF_LWID  SIZ_TAB[4]
#define DEF_MKWD  SIZ_TAB[8]
#define DEF_MKHT  SIZ_TAB[9]
#define MAX_MKWD  SIZ_TAB[10]
#define MAX_MKHT  SIZ_TAB[11]

/* ---------------------------------------------- the display ("SURF") ---- */
/* A raster: the screen or a memory form in device format.  planes is 1
 * (bytes, leftmost pixel in bit 7) or 8 (one byte per pixel, the colour
 * index).  The mono screens store ink as 0 (CGA/Hercules show 1 bits lit,
 * and GEM's paper is white), so inv = 0xFF there. */
typedef struct surf {
	UBYTE	*addr;		/* first byte (linear forms)		*/
	UBYTE	**rows;		/* row start table (screen) or NULL	*/
	LONG	wb;		/* bytes per row (linear forms)		*/
	WORD	w, h;		/* pixels				*/
	WORD	planes;		/* 1 or 8				*/
	UBYTE	inv;		/* XOR applied to stored 1-bpp bytes	*/
} SURF;

#define SROW(s, y) ((s)->rows ? (s)->rows[y] : (s)->addr + (LONG)(y) * (s)->wb)

/* the video mode in use */
typedef struct vdev {
	const char *name;
	WORD	d_xres, d_yres;	/* pixels - 1 */
	WORD	d_xsize, d_ysize; /* microns */
	WORD	planes;		/* 1 or 8 */
	WORD	colors;		/* 2 or 16 */
	WORD	bios_mode;	/* INT 10h mode, or -1 */
	WORD	hires;		/* 1: HIRESPAT patterns, big markers */
	WORD	font_h;		/* system font cell height (8/14/16) */
} VDEV;

extern VDEV	*dev;
extern SURF	screen;
extern WORD	dev_id;		/* DEV_CGA / DEV_HGC / DEV_VGA */
#define DEV_CGA	0
#define DEV_HGC	1
#define DEV_VGA	2

/* ------------------------------------------ per-workstation attributes -- */
struct vws {
	WORD	a_handle, a_used;
	WORD	a_line_index, a_line_color, a_line_qi, a_line_qc, a_line_width, a_line_qw;
	WORD	a_line_beg, a_line_end;
	WORD	a_mark_height, a_mark_scale, a_mark_index, a_mark_color, a_mark_qi, a_mark_qc;
	WORD	a_fill_style, a_fill_index, a_fill_color, a_fill_per, a_fill_qi;
	WORD	a_fill_qc, a_fill_qp;
	WORD	a_val_mode, a_chc_mode, a_loc_mode, a_str_mode;
	WORD	a_write_qm;
	WORD	a_num_qc_lines;
	WORD	a_q_circle[MAX_L_WIDTH];
	WORD	*a_patptr;
	WORD	a_patmsk;
	WORD	a_next_pat, a_udpt_np;
	WORD	a_ud_patrn[16 * 8];
	WORD	a_line_styl[7];
	WORD	a_wrt_mode;
	WORD	a_clip, a_xmn_clip, a_ymn_clip, a_xmx_clip, a_ymx_clip;
	WORD	a_xfm_mode;
	/* text (opttext.c) */
	WORD	a_t_sclsts, a_mono_status, a_actdely, a_chr_ht, a_special, a_weight;
	WORD	a_r_off, a_l_off, a_char_del, a_text_bp;
	UWORD	a_dda_inc;
	WORD	a_text_color, a_rot_case, a_h_align, a_v_align;
	BOOLEAN	a_rq_type;
	BYTE	a_rq_font, a_rq_attr;
	WORD	a_rq_size, a_dbl, a_loaded;
	struct font_head *a_cur_font, *a_act_font, *a_cur_head;
	struct font_head a_font_inf;
};

extern struct vws *vw;

#define line_index	(vw->a_line_index)
#define line_color	(vw->a_line_color)
#define line_qi		(vw->a_line_qi)
#define line_qc		(vw->a_line_qc)
#define line_width	(vw->a_line_width)
#define line_qw		(vw->a_line_qw)
#define line_beg	(vw->a_line_beg)
#define line_end	(vw->a_line_end)
#define mark_height	(vw->a_mark_height)
#define mark_scale	(vw->a_mark_scale)
#define mark_index	(vw->a_mark_index)
#define mark_color	(vw->a_mark_color)
#define mark_qi		(vw->a_mark_qi)
#define mark_qc		(vw->a_mark_qc)
#define fill_style	(vw->a_fill_style)
#define fill_index	(vw->a_fill_index)
#define fill_color	(vw->a_fill_color)
#define fill_per	(vw->a_fill_per)
#define fill_qi		(vw->a_fill_qi)
#define fill_qc		(vw->a_fill_qc)
#define fill_qp		(vw->a_fill_qp)
#define val_mode	(vw->a_val_mode)
#define chc_mode	(vw->a_chc_mode)
#define loc_mode	(vw->a_loc_mode)
#define str_mode	(vw->a_str_mode)
#define write_qm	(vw->a_write_qm)
#define num_qc_lines	(vw->a_num_qc_lines)
#define q_circle	(vw->a_q_circle)
#define patptr		(vw->a_patptr)
#define patmsk		(vw->a_patmsk)
#define NEXT_PAT	(vw->a_next_pat)
#define udpt_np		(vw->a_udpt_np)
#define UD_PATRN	(vw->a_ud_patrn[0])
#define LINE_STYL	(vw->a_line_styl)
#define WRT_MODE	(vw->a_wrt_mode)
#define CLIP		(vw->a_clip)
#define XMN_CLIP	(vw->a_xmn_clip)
#define YMN_CLIP	(vw->a_ymn_clip)
#define XMX_CLIP	(vw->a_xmx_clip)
#define YMX_CLIP	(vw->a_ymx_clip)
#define xfm_mode	(vw->a_xfm_mode)
#define XFM_MODE	(vw->a_xfm_mode)
#define T_SCLSTS	(vw->a_t_sclsts)
#define MONO_STATUS	(vw->a_mono_status)
#define ACTDELY		(vw->a_actdely)
#define CHR_HT		(vw->a_chr_ht)
#define SPECIAL		(vw->a_special)
#define WEIGHT		(vw->a_weight)
#define R_OFF		(vw->a_r_off)
#define L_OFF		(vw->a_l_off)
#define CHAR_DEL	(vw->a_char_del)
#define TEXT_BP		(vw->a_text_bp)
#define DDA_INC		(vw->a_dda_inc)
#define text_color	(vw->a_text_color)
#define rot_case	(vw->a_rot_case)
#define h_align		(vw->a_h_align)
#define v_align		(vw->a_v_align)
#define rq_type		(vw->a_rq_type)
#define rq_font		(vw->a_rq_font)
#define rq_attr		(vw->a_rq_attr)
#define rq_size		(vw->a_rq_size)
#define dbl		(vw->a_dbl)
#define loaded		(vw->a_loaded)
#define cur_font	(vw->a_cur_font)
#define act_font	(vw->a_act_font)
#define cur_head	(vw->a_cur_head)
#define FONT_INF	(vw->a_font_inf)
#define FIR_CHR		(FONT_INF.first_ade)

/* --------------------------------------------------------- globals ------ */
extern WORD CONTRL[], INTIN[], PTSIN[], INTOUT[], PTSOUT[];
extern WORD FLIP_Y;
extern WORD DEV_TAB[45], SIZ_TAB[12], INQ_TAB[45], INQ_PTS[12];
extern WORD X1, Y1, X2, Y2;
extern WORD LN_MASK, LSTLIN, FG_BP_1;
extern WORD HIDE_CNT, MOUSE_BT, GCURX, GCURY, TERM_CH;
extern WORD COPYTRAN;
extern WORD REAL_COL[3][MAX_COLOR], REQ_COL[3][MAX_COLOR], MAP_COL[MAX_COLOR];

/* monobj.c transients */
extern WORD y, odeltay, deltay, deltay1, deltay2;
extern WORD fill_miny, fill_maxy, fill_intersect;
extern WORD xc, yc, xrad, yrad, del_ang, beg_ang, end_ang;
extern WORD start, angle, n_steps;
extern WORD s_fill_per, *s_patptr, s_patmsk, s_nxtpat;
extern WORD s_begsty, s_endsty;

/* patterns (pattern.c, from LORESPAT.A86/HIRESPAT.A86) */
extern WORD *DITHER_P, *HATCH0_P, *HATCH1_P, *OEMPAT_P;
extern WORD DITHRMSK, HAT_0_MSK, HAT_1_MSK, OEMMSKPAT;
extern WORD SOLID, HOLLOW;
#define DITHER	(DITHER_P)
#define HATCH0	(HATCH0_P)
#define HATCH1	(HATCH1_P)
#define OEMPAT	(OEMPAT_P)
extern const WORD line_sty_def[6];
extern const WORD ud_patrn_def[16];
extern WORD m_dot[], m_plus[], m_star[], m_square[], m_cross[], m_dmnd[];
void pattern_init(WORD hires);

/* fonts (fonts.c, generated from the GEM/3 .FUL system fonts) */
extern struct font_head *font_top;
struct font_head *fonts_for(WORD cell_height);

/* opttext.c text state kept global (per call) */
extern WORD DESTX, DESTY, width, height;
extern UWORD XACC_DDA;

/* ------------------------------------------------ primitives (C) -------- */
VOID	ABLINE(VOID);
VOID	HABLINE(VOID);
VOID	RECTFILL(VOID);
VOID	CLC_FLIT(VOID);
VOID	CLEARMEM(VOID);
VOID	COPY_RFM(VOID);
VOID	TRAN_FM(VOID);
VOID	XFM_CRFM(VOID);
VOID	XFM_UDFL(VOID);
VOID	TEXT_BLT(VOID);
WORD	MONO8XHT(VOID);
UWORD	CLC_DDA(WORD actual, WORD requested);
WORD	ACT_SIZ(WORD size);
VOID	clr_skew(VOID);
VOID	cpy_head(VOID);
WORD	chk_ade(WORD ch);
VOID	chk_fnt(VOID);
VOID	inc_lfu(VOID);
VOID	in_rot(VOID);
VOID	in_doub(VOID);
VOID	d_justified(VOID);
VOID	dqt_just(VOID);
WORD	HIDE_CUR(VOID);
VOID	DIS_CUR(VOID);
VOID	INIT_G(VOID);
VOID	DINIT_G(VOID);
VOID	CHK_ESC(VOID);
WORD	GLOC_KEY(VOID);
WORD	GCHC_KEY(VOID);
WORD	GCHR_KEY(VOID);
WORD	GSHIFT_S(VOID);
VOID	EX_TIMV(VOID);
VOID	VEX_BUTV(VOID);
VOID	VEX_MOTV(VOID);
VOID	VEX_CURV(VOID);
VOID	S_COLMAP(VOID);
VOID	I_COLMAP(VOID);
WORD	SMUL_DIV(WORD m1, WORD m2, WORD d1);
WORD	vec_len(WORD dx, WORD dy);
WORD	Isin(WORD angle);
WORD	Icos(WORD angle);

/* surface helpers (draw.c) */
void	surf_hspan(SURF *s, int y, int x1, int x2, UWORD pat, int mode, int color);
void	surf_pixel(SURF *s, int x, int y, int on, int mode, int color);
int	surf_get(SURF *s, int x, int y);
void	surf_mono_blit(const UBYTE *src, LONG swb, int sx, int sy, int w, int h,
		SURF *d, int dx, int dy, int mode, int fg, int bg,
		int cx0, int cy0, int cx1, int cy1);
int	dev_xor_mask(void);

/* mouse.c */
void	mouse_init(void);
void	mouse_exit(void);
void	mouse_set_form(const WORD *form37);
extern volatile WORD mouse_hidden;

/* dev.c */
int	dev_open(int id);
void	dev_close(void);
void	dev_tables(void);
void	dev_detect(int requested);
extern WORD dispmode;		/* 1 = alpha (text) mode */

/* gdos.c */
struct vws *vws_alloc(void);

/* interrupts off / back to what they were (usable at interrupt time) */
static inline unsigned long irq_save(void)
{
	unsigned long c, t;
	__asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1"
		: "=r"(c), "=r"(t) :: "memory");
	return c;
}
static inline void irq_restore(unsigned long c)
{
	__asm__ volatile("msr cpsr_c, %0" :: "r"(c) : "memory");
}

void	mouse_block(void);
void	mouse_unblock(void);
void	mouse_default_form(void);
void	keyboard_mouse(WORD buttons, WORD dx, WORD dy);
void	timer_exit(void);

#define	FHEAD struct font_head

/* the ported DRI modules */
#include "vdiproto.h"
WORD	vsl_type(VOID);
#define dst_alignment dst_aligmnent

#endif
