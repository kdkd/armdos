/*  Module ATTRIB.H  */

/*컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�*/
/* All defines for attrib.c                                                */
/*컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�*/

#define FALSE      0                    /*;AN000;*/
#define TRUE       !FALSE               /*;AN000;*/

#ifndef BYTE
#define BYTE       unsigned char        /*;AN000;*/
#define WORD       unsigned short       /*;AN000;*/
#define DWORD      unsigned long        /*;AN000;*/
#endif

#ifdef ARMDOS
/* ARM-DOS port helpers */
#define DOSWORD    unsigned short       /* a 16-bit word inside a DOS structure */
#define PACKED     __attribute__((packed))
#define RD16(p)    (((BYTE far *)(p))[0] | (((BYTE far *)(p))[1] << 8))
#define RD32(p)    (RD16(p) | ((DWORD)RD16((BYTE far *)(p) + 2) << 16))
#define WR16(p,v)  (((BYTE far *)(p))[0] = (BYTE)(v), ((BYTE far *)(p))[1] = (BYTE)((v) >> 8))
/* strcpy(fix_es_reg, NUL) restored ES after far pointer use in MS C; here
   it would read from address 0 */
#include <string.h>
static char *armdos_strcpy(char *d, const char *s) { return s ? strcpy(d, s) : d; }
#define strcpy(d, s) armdos_strcpy((d), (s))
#endif

#define CARRY      0x0001               /*;AN000;   carry flag */

#define YES        1                    /*;AN000;   Yes return from int 21h Y/N check */
#define NO         0                    /*;AN000;   NO return from int 21h Y/N check */

#define NUL        0x0                  /*;AN000;*/
#define BLANK      0x20                 /*;AN000;*/
#define TAB        0x09                 /*;AN000;*/
#define CR         0x0d                 /*;AN000;*/
#define LF         0x0a                 /*;AN000;*/

/* Error_exit() parameter values */
#define ERR_EXTENDED 1                  /*;AN000;*/
#define ERR_PARSE    2                  /*;AN000;*/


/* standard file handles */
#define STDIN           0x00            /*;AN000; Standard Input device handle */
#define STDOUT          0x01            /*;AN000; Standard Output device handle */
#define STDERR          0x02            /*;AN000; Standard Error Output device handle */
#define STDAUX          0x03            /*;AN000; Standard Auxiliary device handle */
#define STDPRN          0x04            /*;AN000; Standard Printer device handle */

/* attribute byte defines */
#define AFILE      0x00                 /*;AN000;*/
#define READONLY   0x01                 /*;AN000;*/
#define HIDDEN     0x02                 /*;AN000;*/
#define SYSTEM     0x04                 /*;AN000;*/
#define LABEL      0x08                 /*;AN000;*/
#define SUBDIR     0x10                 /*;AN000;*/
#define ARCHIVE    0x20                 /*;AN000;*/

/* extended attribute type defines */
#define EAISUNDEF    0                  /*;AN000;  undefined type */
#define EAISLOGICAL  1                  /*;AN000;  logical (0 or 1) */
#define EAISBINARY   2                  /*;AN000;  binary integer  */
#define EAISASCII    3                  /*;AN000;  ASCII type */
#define EAISDATE     4                  /*;AN000;  DOS file date format */
#define EAISTIME     5                  /*;AN000;  DOS file time format */

#define EANAMES      6                  /*;AN000;  ext attr names ASCII */

/* extended attribute flag defines */
#define EASYSTEM     0x8000             /*;AN000;  EA is system defined */
#define EAREADONLY   0x4000             /*;AN000;  EA is read only */
#define EAHIDDEN     0x2000             /*;AN000;  EA is hidden */
#define EACREATEONLY 0x1000             /*;AN000;  EA is setable only at create time */

/* extended attribute failure return code defines */
#define EARCNOERROR  0                  /*;AN000;  no error */
#define EARCNOTFOUND 1                  /*;AN000;  name not found */
#define EARCNOSPACE  2                  /*;AN000;  no space to hold name or value */
#define EARCNOTNOW   3                  /*;AN000;  name can't be set on this function */
#define EARCNOTEVER  4                  /*;AN000;  name can't be set */
#define EARCUNDEF    5                  /*;AN000;  name known to this FS but not supported */
#define EARCDEFBAD   6                  /*;AN000;  EA definition bad (TYPE,LENGTH, etc.) */
#define EARCACCESS   7                  /*;AN000;  EA access denied */
#define EARCVALBAD   8                  /*;AN000;  EA value not supported */
#define EARCUNKNOWN  -1                 /*;AN000;  undetermined cause */

/* message retriever interface defines */
#define NOSUBPTR   0                    /*;AN000;  no sublist pointer         */
#define NOSUBCNT   0                    /*;AN000;  0 substitution count       */
#define ONEPARM    1                    /*;AN000;  1 substitution count       */
#define TWOPARM    2                    /*;AN000;  2 substitution count       */
#define NOINPUT    0                    /*;AN000;  no user input              */
#define INPUT      1                    /*;AN000;  ask user for Y/N input     */

/* misc. defines */
#define A_FILESIZE  1                   /* id of special attribute: filesize */
#define A_DATE      2                   /* id of special attribute: date */
#define A_TIME      3                   /* id of special attribute: time */

#define MAX_ATTR_SIZE 160               /*;AN000;  max ext attribute buffer size */
#define MAX_KEYWORD 128                 /*;AN000;  max size of extended attribute keyword */
#define MAX_SPL     3                   /*;AN000;  max number of special attributes */

#define ATTR_SIZE   7                   /*;AN000;  size in bytes of attr struct */
#define NAME_SIZE   4                   /*;AN000;  size in bytes of name struct */

#define NOERROR      0                  /*;AN000;*/
#define NOMOREFILES 18                  /*;AN000;*/
#define FILENOTFOUND 2                  /*;AN000;*/

#define GET_DATE     1                  /*;AN000;*/
#define GET_TIME     2                  /*;AN000;*/

#define INACTIVE     0x7fff             /*;AN000;*/
/*컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�*/
/* All structures defined for attrib.c                                     */
/*컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�*/

struct p_null {                        /*;AN000;  a null value list for parser */
    unsigned char null;                /*;AN000;*/
    };                                 /*;AN000;*/

#ifdef ARMDOS
struct query_list {
    DOSWORD ql_num;
    BYTE    ql_type;
    DOSWORD ql_flags;
    BYTE    ql_name_len;
    char    ql_name[MAX_KEYWORD];
    } PACKED;
#else
struct query_list {                    /*;AN000;  Generic attribute overlay structure  */
    WORD    ql_num;
    BYTE    ql_type;                   /*;AN000;   EA type          */
    WORD    ql_flags;                  /*;AN000;   EA flags         */
    BYTE    ql_name_len;               /*;AN000;   name length      */
    char    ql_name[MAX_KEYWORD];      /*;AN000;   name             */
    };                                 /*;AN000;*/
#endif

#ifdef ARMDOS
struct name_list {
    BYTE    nl_type;
    DOSWORD nl_flags;
    BYTE    nl_name_len;
    char    nl_name[MAX_KEYWORD];
    } PACKED;
#else
struct name_list {                     /*;AN000;  Generic attribute overlay structure  */
    BYTE    nl_type;                   /*;AN000;   EA type          */
    WORD    nl_flags;                  /*;AN000;   EA flags         */
    BYTE    nl_name_len;               /*;AN000;   name length      */
    char    nl_name[MAX_KEYWORD];      /*;AN000;   name             */
    };                                 /*;AN000;*/
#endif

#ifdef ARMDOS
struct attr_list {
    BYTE    at_type;
    DOSWORD at_flags;
    BYTE    at_rc;
    BYTE    at_name_len;
    DOSWORD at_value_len;
    char    at_name[MAX_KEYWORD];
    } PACKED;
#else
struct attr_list {                     /*;AN000;  Generic attribute overlay structure  */
    BYTE    at_type;                   /*;AN000;   EA type          */
    WORD    at_flags;                  /*;AN000;   EA flags         */
    BYTE    at_rc;                     /*;AN000;   EA return code   */
    BYTE    at_name_len;               /*;AN000;   name length      */
    WORD    at_value_len;              /*;AN000;   value length     */
    char    at_name[MAX_KEYWORD];      /*;AN000;   name             */
    };                                 /*;AN000;*/
#endif

#ifdef ARMDOS
struct parm_list {
    DWORD    pm_list;
    DOSWORD  pm_num_parms;
    BYTE     pm_id;
    DOSWORD  pm_iomode;
    } PACKED;
#else
struct parm_list {                     /*;AN000;  Parm list for extended open DOS call */
    DWORD    pm_list;                  /*;AN000;   extended attr. list */
    WORD     pm_num_parms;             /*;AN000;   number of parameters */
    BYTE     pm_id;                    /*;AN000;   id                  */
    WORD     pm_iomode;                /*;AN000;   iomode              */
    };                                 /*;AN000;*/
#endif

struct spl_list {                      /*;AN000;*/
    char     name[MAX_KEYWORD];        /*;AN000;*/
    WORD     id;                       /*;AN000;*/
    };                                 /*;AN000;*/

struct bin_struct {                    /*;AN000;*/
    BYTE     length;                   /*;AN000;*/
    DWORD    dword;                    /*;AN000;*/
    };                                 /*;AN000;*/

union eav_union {                      /*;AN000;*/
    WORD     ea_undef;                 /*;AN000;*/
    BYTE     ea_logical;               /*;AN000;*/
    struct bin_struct ea_bin;          /*;AN000;*/
    char     ea_ascii[129];            /*;AN000;*/
    DWORD    ea_time;                  /*;AN000;*/
    DWORD    ea_date;                  /*;AN000;*/
    };                                 /*;AN000;*/
/*컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�*/
/* All global variables for attrib.c                                       */
/*컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�*/
struct spl_list specials[MAX_SPL] = {  /*;AN000;*/
   "FILESIZE", A_FILESIZE,             /*;AN000;*/
   "DATE",     A_DATE,                 /*;AN000;*/
   "TIME",     A_TIME };               /*;AN000;*/


/* parser structure variables */
union eav_union ext_attr_value;        /*;AN000; result value union */

struct p_result_blk  pos1_buff;        /*;AN000; result buffer for -+A,-+R   */
struct p_result_blk  pos2_buff;        /*;AN000; result buffer for -+A,-+R   */
struct p_result_blk  pos3_buff;        /*;AN000; result buffer for filespec  */
struct p_result_blk  pos4_buff;        /*;AN000; result buffer for -+A,-+R   */
struct p_result_blk  pos5_buff;        /*;AN000; result buffer for -+A,-+R   */
struct p_result_blk  pos6_buff;        /*;AN000; result buffer for id        */
struct p_result_blk  pos6b_buff;       /*;AN000; result buffer for id        */
struct p_result_blk  sw_buff;          /*;AN000; result buffer for /S        */

char  nullword[]  = "     ";           /*;AN000; used when no word attribute */
char  nulldword[] = "          ";      /*;AN000; used when no double word attribute */
char  nulldate[]  = "        ";        /*;AN000; used when no date attribute */
char  nulltime[]  = "         ";       /*;AN000; used when no time attribute */

char  plusa[]  = "+A";                 /*;AN000; strings for match */
char  minusa[] = "-A";                 /*;AN000;*/
char  plusr[]  = "+R";                 /*;AN000;*/
char  minusr[] = "-R";                 /*;AN000;*/

struct p_null noval =                  /*;AN000; structure for no value list */
     { 0 };                            /*;AN000;*/

#ifdef ARMDOS
/* ARM-DOS: the string pointers sit at odd offsets in the packed list, which
   the loader cannot relocate: they are filled in at start-up */
struct ps_valist_blk vals1 =
     { p_nval_string, 0, 0, 4,
       0x20, 0,
       0x20, 0,
       0x01, 0,
       0x01, 0 };
__attribute__((constructor)) static void armdos_vals1(void)
{
   vals1.ps_strings1 = (WORD)plusa;
   vals1.ps_strings2 = (WORD)minusa;
   vals1.ps_strings3 = (WORD)plusr;
   vals1.ps_strings4 = (WORD)minusr;
}
#else
struct ps_valist_blk vals1 =           /*;AN000; +A-A+R-R value list         */
     { p_nval_string,                  /*;AN000;  string value list type     */
       0,                              /*;AN000;  number of ranges           */
       0,                              /*;AN000;  number of numbers          */
       4,                              /*;AN000;  number of strings          */
       0x20,                           /*;AN000;  item tag                   */
       (WORD)plusa,                    /*;AN000;  address of string          */
       0x20,                           /*;AN000;  item tag                   */
       (WORD)minusa,                   /*;AN000;  address of string          */
       0x01,                           /*;AN000;  item tag                   */
       (WORD)plusr,                    /*;AN000;  address of string          */
       0x01,                           /*;AN000;  item tag                   */
       (WORD)minusr };                 /*;AN000;  address of string          */
#endif

struct p_control_blk p_con1 =          /*;AN000; describes +-A or +-R        */
     { 0x2001,                         /*;AN000;  Simple string,optional     */
       0x0002,                         /*;AN000;  Cap result by char table   */
       (WORD)&pos1_buff,               /*;AN000;*/
       (WORD)&vals1,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_con2 =          /*;AN000; describes +-A or +-R (2nd occurrance) */
     { 0x2001,                         /*;AN000;  Simple string,optional     */
       0x0002,                         /*;AN000;  Cap result by char table   */
       (WORD)&pos2_buff,               /*;AN000;*/
       (WORD)&vals1,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_con3 =          /*;AN000; describes filespec          */
     { 0x0200,                         /*;AN000;  File spec required         */
       0x0001,                         /*;AN000;  Cap result by file table   */
       (WORD)&pos3_buff,               /*;AN000;*/
       (WORD)&noval,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_con4 =          /*;AN000; describes +-A or +-R        */
     { 0x2001,                         /*;AN000;  Simple string,optional     */
       0x0002,                         /*;AN000;  Cap result by char table   */
       (WORD)&pos4_buff,               /*;AN000;*/
       (WORD)&vals1,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_con5 =          /*;AN000; describes +-A or +-R (2nd occurrance) */
     { 0x2001,                         /*;AN000;  Simple string,optional     */
       0x0002,                         /*;AN000;  Cap result by char table   */
       (WORD)&pos5_buff,               /*;AN000;*/
       (WORD)&vals1,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_con6 =          /*;AN000; describes id                */
     { 0x2001,                         /*;AN000;  Simple string,optional     */
       0x0002,                         /*;AN000;  Cap result by char table   */
       (WORD)&pos6_buff,               /*;AN000;*/
       (WORD)&noval,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_con6a =         /*;AN000; describes id     */
     { 0x2000,                         /*;AN000;  Simple string   */
       0x0002,                         /*;AN000;  Cap result by char table   */
       (WORD)&pos6_buff,               /*;AN000;*/
       (WORD)&noval,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_con6b =         /*;AN000; describes id     */
     { 0xe481,                         /*;AN000;  Quoted string   */
       0x0002,                         /*;AN000;*/
       (WORD)&pos6b_buff,              /*;AN000;*/
       (WORD)&noval,                   /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_control_blk p_swi1 =          /*;AN000; Switch control block */
     { 0x0001,                         /*;AN000;  Optional (switch) */
       0x0002,                         /*;AN000;  Cap result by char table */
       (WORD)&sw_buff,                 /*;AN000;*/
       (WORD)&noval,                   /*;AN000;*/
       1,                              /*;AN000;  one switch allowed */
       "/S" };                         /*;AN000;  /S */

struct p_parmsx p_px1 =                /*;AN000; Parser Control definition for Parm Block 1 */
     { 1,                              /*;AN000;  positionals */
       6,                              /*;AN000;*/
       &p_con1,                        /*;AN000;*/
       &p_con2,                        /*;AN000;*/
       &p_con3,                        /*;AN000;*/
       &p_con4,                        /*;AN000;*/
       &p_con5,                        /*;AN000;*/
       &p_con6,                        /*;AN000;*/
       1,                              /*;AN000;  switches */
       &p_swi1,                        /*;AN000;*/
       0,                              /*;AN000;  keywords*/
       0,                              /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_parms  p_p1 =                 /*;AN000; Parms block for line */
     { &p_px1,                         /*;AN000; Address of extended parm list */
       0,                              /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_parmsx p_px2 =                /*;AN000; Parser Control definition for Parm Block 1 */
     { 1,                              /*;AN000;  positionals */
       2,                              /*;AN000;*/
       &p_con6a,                       /*;AN000;*/
       &p_con6b,                       /*;AN000;*/
       0,                              /*;AN000;*/
       0,                              /*;AN000;*/
       0,                              /*;AN000;*/
       0,                              /*;AN000;*/
       1,                              /*;AN000;  switches */
       &p_swi1,                        /*;AN000;*/
       0,                              /*;AN000;  keywords*/
       0,                              /*;AN000;*/
       0,                              /*;AN000;*/
       0 };                            /*;AN000;*/

struct p_parms  p_p2 =                 /*;AN000; Parms block for line */
     { &p_px2,                         /*;AN000; Address of extended parm list */
       1,                              /*;AN000;*/
       1,                              /*;AN000;*/
       "=" };                          /*;AN000;*/

/* extended open structure variables */
struct parm_list plist =               /*;AN000;  Extended open parm list */
     { -1,                             /*;AN000;   ptr to attr. list    */
       1,                              /*;AN000;   number of parms      */
       6,                              /*;AN000;   id                   */
       2 };                            /*;AN000;   iomode               */

/* messgages */
#ifdef ARMDOS
/* ARM-DOS: MS C placed these eight sublists one after the other and the
   sizes (72, 60, ... = 6, 5, ... sublists of 12 bytes) are the strides from
   each one to msg_str1, its %2 - one array keeps that true */
#define SL sizeof(struct m_sublist)
struct m_sublist attrib_sublist[8] = {
  { 6*SL, 0, 0, 1, sf_unsbin2d | sf_right,              9,  9, 0,   0 }, /* msg_num   */
  { 5*SL, 0, 0, 1, sf_left | sf_char | sf_asciiz,       0,  0, ' ', 0 }, /* msg_str2  */
  { 4*SL, 0, 0, 1, sf_unsbin2d | sf_dword | sf_right,  10,  9, 0,   0 }, /* msg_dword */
  { 3*SL, 0, 0, 1, sf_date | sf_mdy2,                   9,  9, 0,   0 }, /* msg_date  */
  { 2*SL, 0, 0, 1, sf_time12 | sf_hhmm | sf_right,      9,  9, 0,   0 }, /* msg_time  */
  { 1*SL, 0, 0, 1, sf_left | sf_char | sf_asciiz,       9,  9, ' ', 0 }, /* msg_str   */
  { 1*SL, 0, 0, 2, sf_left | sf_char | sf_asciiz,       0,  0, ' ', 0 }, /* msg_str1  */
  { 1*SL, 0, 0, 0, sf_left | sf_char | sf_asciiz,       0,  0, ' ', 0 }, /* msg_error */
};
#define msg_num   attrib_sublist[0]
#define msg_str2  attrib_sublist[1]
#define msg_dword attrib_sublist[2]
#define msg_date  attrib_sublist[3]
#define msg_time  attrib_sublist[4]
#define msg_str   attrib_sublist[5]
#define msg_str1  attrib_sublist[6]
#define msg_error attrib_sublist[7]
#else
struct m_sublist msg_num =             /*;AN000; describes substitutions   */
     { 72,                             /*;AN000;   for parm one of message */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       1,                              /*;AN000;                           */
       sf_unsbin2d | sf_right,         /*;AN000;  unsigned binary to decimal*/
       9,                              /*;AN000;                           */
       9,                              /*;AN000;                           */
       0 };                            /*;AN000;                           */
struct m_sublist msg_str2 =            /*;AN000; describes substitutions   */
     { 60,                             /*;AN000;   for parm one of message */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       1,                              /*;AN000;                           */
       sf_left | sf_char | sf_asciiz,  /*;AN000;  string                   */
       0,                              /*;AN000;  null string              */
       0,                              /*;AN000;                           */
       (BYTE)" " };                    /*;AN000;                           */
struct m_sublist msg_dword =           /*;AN000; describes substitutions   */
     { 48,                             /*;AN000;   for parm one of message */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       1,                              /*;AN000;                           */
       sf_unsbin2d | sf_dword | sf_right, /*;AN000;  unsigned binary to decimal*/
       10,                             /*;AN000;                           */
       9,                              /*;AN000;                           */
       0 };                            /*;AN000;                           */
struct m_sublist msg_date =            /*;AN000; describes substitutions   */
     { 36,                             /*;AN000;   for parm one of message */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       1,                              /*;AN000;                           */
       sf_date | sf_mdy2,              /*;AN000;  unsigned binary to decimal*/
       9,                              /*;AN000;                           */
       9,                              /*;AN000;                           */
       0 };                            /*;AN000;                           */
struct m_sublist msg_time =            /*;AN000; describes substitutions   */
     { 24,                             /*;AN000;   for parm one of message */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       1,                              /*;AN000;                           */
       sf_time12 | sf_hhmm | sf_right, /*;AN000;  unsigned binary to decimal*/
       9,                              /*;AN000;  NN-NN-NNa (9 characters) */
       9,                              /*;AN000;                           */
       0 };                            /*;AN000;                           */
struct m_sublist msg_str =             /*;AN000; describes substitutions   */
     { 12,                             /*;AN000;   for parm one of message */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       1,                              /*;AN000;                           */
       sf_left | sf_char | sf_asciiz,  /*;AN000;  string                   */
       9,                              /*;AN000;  null string              */
       9,                              /*;AN000;                           */
       (BYTE)" " };                    /*;AN000;                           */
struct m_sublist msg_str1 =            /*;AN000; describes substitutions   */
     { 12,                             /*;AN000;   for parm two of message */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       2,                              /*;AN000;                           */
       sf_left | sf_char | sf_asciiz,  /*;AN000;  string                   */
       0,                              /*;AN000;  null string              */
       0,                              /*;AN000;                           */
       (BYTE)" " };                    /*;AN000;                           */
struct m_sublist msg_error =           /*;AN000; describes substitutions   */
     { 12,                             /*;AN000;   for extended error messages*/
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       0,                              /*;AN000;                           */
       sf_left | sf_char | sf_asciiz,  /*;AN000;  string                   */
       0,                              /*;AN000;  null string              */
       0,                              /*;AN000;                           */
       (BYTE)" " };                    /*;AN000;                           */
#endif

/* misc. variables */
union REGS       inregs,               /*;AN000;  Registers */
                 outregs;              /*;AN000;*/
struct SREGS     segregs;              /*;AN000;  Segment registers */

DWORD            old_int24_off;        /*;AN000;*/

WORD             descending;           /*;AN000;*/
WORD             append_x_status;      /*;AN000;*/
WORD             did_attrib_ok;        /*;AN000;*/
WORD             set_reg_attr,         /*;AN000;*/
                 set_ext_attr;         /*;AN000;*/
WORD             do_reg_attr,          /*;AN000;*/
                 do_ext_attr;          /*;AN000;*/

BYTE  far       *DBCS_ptr;             /*;AN000;*/
BYTE             ext_attr_value_type;  /*;AN000;*/
BYTE             attr;                 /*;AN000;*/
BYTE             bits[8] = { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01 }; /*;AN000;*/
BYTE             pmask,                /*;AN000;*/
                 mmask;                /*;AN000;*/

char             as[8] = { ' ',' ','A',' ',' ',' ',' ','R' };           /*;AN000;*/
char             fix_es_reg[1];        /*;AN000;*/
char             ext_attr[MAX_KEYWORD];        /*;AN000;*/
char             error_file_name[256]; /*;AN005;*/
char             fspec[256];           /*;AN000;*/
char             file[256];            /*;AN000;*/
char             str_on[3] = {"ON"};   /*;AN000;*/
char             str_off[4] = {"OFF"}; /*;AN000;*/
