/* malloc.h - newlib's malloc.h plus Microsoft C's near/far/huge names. */
#ifndef _ARMDOS_MALLOC_H
#define _ARMDOS_MALLOC_H
#include_next <malloc.h>
#include <stdlib.h>

#define _fmalloc   malloc
#define _nmalloc   malloc
#define _ffree     free
#define _nfree     free
#define _frealloc  realloc
#define _nrealloc  realloc
#define _fcalloc   calloc
#define halloc(n, size) calloc((n), (size))
#define hfree      free
#define _halloc    halloc
#define _hfree     hfree
#define farmalloc  malloc
#define farfree    free
#define farcalloc  calloc
#define farrealloc realloc

#ifdef __cplusplus
extern "C" {
#endif
/* Bytes the heap could still grow by without XMS (like _memavl), and the
 * size of the largest free DOS block. */
unsigned long _memavl(void);
unsigned long _memmax(void);
#ifdef __cplusplus
}
#endif
#endif
