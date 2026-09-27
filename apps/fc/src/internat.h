/*
	Data structure for international table
 */

#ifdef ARMDOS
/* ARM-DOS: the same buffer (INT 21h AH=38h, DOS's byte layout) with MS C's
   16-bit unsigned/int spelled out; the case map routine is a flat pointer */
struct InterTbl
{
	unsigned short dateform ;
	char	currsym[5] ;
	char	thousp[2] ;
	char	decsp[2] ;
	char	datesp[2] ;
	char	timesp[2] ;
	unsigned char bits ;
	unsigned char numdig ;
	unsigned char timeform ;
	unsigned long casecall ;
	char	datasp[2] ;
	short	reserv[5] ;
} __attribute__((packed)) ;
#else
struct InterTbl
{
	unsigned dateform ;	/* Date format				   */
	char	currsym[5] ;	/* Currency symbol as ASCIZ string	   */
	char	thousp[2] ;	/* Thousands separator as ASCIZ string	   */
	char	decsp[2] ;	/* Decimal   separator as ASCIZ string	   */
	char	datesp[2] ;	/* Date      separator as ASCIZ string	   */
	char	timesp[2] ;	/* Time      separator as ASCIZ string	   */
	unsigned char bits ;	/* Bit field				   */
	unsigned char numdig ;	/* Number of signifigant decimal digits    */
	unsigned char timeform ;/* Time format				   */
	unsigned long casecall ;/* Case mapping call			   */
	char	datasp[2] ;	/* Data list separator as ASCIZ string	   */
	int	reserv[5] ;	/* RESERVED				   */
} ;
#endif


#define DATEFORM_USA	0
#define DATEFORM_EUROPE 1
#define DATEFORM_JAPAN	2

#define BITS_CURRENCY	0x0001
#define BITS_NUMSPC	0x0002

#define TIMEFORM_12	0
#define TIMEFORM_24	1
