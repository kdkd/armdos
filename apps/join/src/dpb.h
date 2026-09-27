#ifdef ARMDOS
 struct DPBType {                     /* ARM-DOS: DOS 4's DPB, flat pointers */
        char     drive ;
        char     unit ;
        unsigned short cbSector ;
        char     mask ;
        char     shift ;
        unsigned short secFAT ;
        char     cFAT ;
        unsigned short cDirEnt ;
        unsigned short secData ;
        unsigned short cCluster ;
        unsigned short csecFAT ;
        unsigned short secDir ;
        long     pDevice ;
        char     media ;
        char     fFirst ;
        long     nextDPB ;
        unsigned short clusFree ;
        unsigned short FreeCnt ;
        } __attribute__((packed)) ;
#else
 struct DPBType {
        char     drive ;        /* physical drive number                   */
        char     unit ;         /* unit within device                      */
        unsigned cbSector ;     /* bytes per sector                        */
        char     mask ;         /* sectors/alloc unit - 1                  */
        char     shift ;        /* bit to shift                            */
        unsigned secFAT ;       /* sector number of first FAT              */
        char     cFAT ;         /* count of FATs                           */
        unsigned cDirEnt ;      /* count of root directory entries         */
        unsigned secData ;      /* first data sector                       */
        unsigned cCluster ;     /* max number of clusters on drive         */
        unsigned csecFAT ;      /* sectors in each FAT                     */
        unsigned secDir ;       /* first sector of root dir                */
        long     pDevice ;      /* pointer to device header                */
        char     media ;        /* last media in drive                     */
        char     fFirst ;       /* TRUE => media check needed              */
        long     nextDPB ;      /* pointer to next dpb                     */
        unsigned clusFree ;     /* cluster number of last alloc            */
        unsigned FreeCnt ;      /* count of free clusters, -1 if unk       */
/*      char     SyncFlg ;      /* sync flags, (see below)                 */
        } ;
#endif

/*      Definitions of SyncFlg values from DPB.INC  */

#define DPB_ABUSY  1        /* some process is allocating clusters */
#define DPB_AWANT  2        /* some process waiting to allocate    */
#define DPB_FBUSY  4        /* some process is reading FAT         */
#define DPB_FWANT  8        /* some process waiting to read FAT    */

