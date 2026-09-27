/* MSC time routines are broken. These are adapted from the emx/gcc
   library routines. */

/* gmtime.c (emx+gcc) -- Copyright (c) 1990-1993 by Eberhard Mattes */

#include <stdlib.h>
#include <time.h>
#include <string.h>

typedef struct {
  unsigned long quot;
  unsigned int  rem;
} _uldiv_t;

static _uldiv_t _uldiv(time_t t0, unsigned n)
{
  _uldiv_t q;
  q.quot = t0 / n;
  q.rem = t0 % n;
  return(q);
}

static int leap (unsigned int y)
{
  return (y % 4 != 0 ? 0 : y % 100 != 0 ? 1 : y % 400 != 0 ? 0 : 1);
}


struct tm *gmtime (const time_t *t)
{
  static struct tm result;
  time_t t0, t1;
  _uldiv_t q;
  static int const mon_len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

  t0 = *t;
  if (t0 == (time_t)(-1))
    return (NULL);
  q = _uldiv (t0, 60); result.tm_sec = q.rem; t0 = q.quot;
  q = _uldiv (t0, 60); result.tm_min = q.rem; t0 = q.quot;
  q = _uldiv (t0, 24); result.tm_hour = q.rem; t0 = q.quot;
  result.tm_wday = (t0+4) % 7;  /* 01-Jan-1970 was Thursday, ie, 4 */
  result.tm_year = 70;          /* 1970 */
  for (;;)
    {
      t1 = (leap (result.tm_year+1900) ? 366 : 365);
      if (t1 > t0)
        break;
      t0 -= t1;
      ++result.tm_year;
    }
  result.tm_mon = 0;
  result.tm_yday = t0;
  for (;;)
    {
      if (result.tm_mon == 1)       /* February */
        t1 = (leap (result.tm_year+1900) ? 29 : 28);
      else
        t1 = mon_len[result.tm_mon];
      if (t1 > t0)
        break;
      t0 -= t1;
      ++result.tm_mon;
    }
  result.tm_mday = t0 + 1;
  result.tm_isdst = 0;
  return (&result);
}


/* asctime.c (emx+gcc) -- Copyright (c) 1990-1993 by Eberhard Mattes */

static char const months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static char const wdays[] = "SunMonTueWedThuFriSat";

#define digit(i) (char)(((i)%10)+'0')

char *asctime (const struct tm *t)
{
  static char result[26];

  memcpy (result+0, wdays+t->tm_wday*3, 3);
  result[3] = ' ';
  memcpy (result+4, months+t->tm_mon*3, 3);
  result[7] = ' ';
  result[8] = digit (t->tm_mday / 10);
  result[9] = digit (t->tm_mday / 1);
  result[10] = ' ';
  result[11] = digit (t->tm_hour / 10);
  result[12] = digit (t->tm_hour / 1);
  result[13] = ':';
  result[14] = digit (t->tm_min / 10);
  result[15] = digit (t->tm_min / 1);
  result[16] = ':';
  result[17] = digit (t->tm_sec / 10);
  result[18] = digit (t->tm_sec / 1);
  result[19] = ' ';
  result[20] = digit ((t->tm_year+1900) / 1000);
  result[21] = digit ((t->tm_year+1900) / 100);
  result[22] = digit ((t->tm_year+1900) / 10);
  result[23] = digit ((t->tm_year+1900) / 1);
  result[24] = '\n';
  result[25] = '\0';
  return (result);
}


/* ctime.c (emx+gcc) -- Copyright (c) 1990-1993 by Eberhard Mattes */

char *ctime (const time_t *t)
{
  struct tm *x;

  x = localtime (t);
  if (x == NULL)
    return (NULL);
  else
    return (asctime (x));
}

