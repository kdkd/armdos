/* structure of a CDS */

#define DIRSTRLEN   64+3

#ifdef ARMDOS
/* ARM-DOS: the kernel's CDS (INC/CURDIR.INC, 58h bytes): MS C's 16-bit
   unsigned spelled out, the far pointers 4-byte flat addresses */
struct CDSType
{
        char            text[DIRSTRLEN] ;
        unsigned short  flags ;
        long            pDPB ;
        long            ID ;
        unsigned short  wUser ;
        unsigned short  cbEnd ;
        char            type ;
        long            ifs_hdr ;
        char            fsda[2] ;
} __attribute__((packed)) ;
#else
struct CDSType
{
        char            text[DIRSTRLEN] ;
        unsigned        flags ;
        long            pDPB ;
        long            ID ;
        unsigned        wUser ;
        unsigned        cbEnd ;
        char            type ;
        long            ifs_hdr ;
        char            fsda[2] ;
} ;
#endif

#define CDSNET          0x8000
#define CDSINUSE        0x4000
#define CDSSPLICE       0x2000
#define CDSLOCAL        0x1000

extern char fGetCDS() ;
extern char fPutCDS() ;
