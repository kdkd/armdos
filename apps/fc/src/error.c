/* error.c - return text of error corresponding to the most recent DOS error */

#include "tools.h"

#ifdef ARMDOS
#include <errno.h>          /* newlib: errno is a macro */
#else
extern int errno;
#endif
extern sys_nerr;
extern char *sys_errlist[];
extern char UnKnown[];

char *error ()
{
    if (errno < 0 || errno >= sys_nerr)
	return UnKnown;
    else
	return sys_errlist[errno];
}
