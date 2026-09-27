#ifndef Z_ZONE_H
#define Z_ZONE_H
#include <stdlib.h>
#define PU_STATIC 1
#define PU_CACHE 101
#define Z_Malloc(n, tag, user) malloc(n)
#define Z_Free(p) free(p)
#endif
