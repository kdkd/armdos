#ifndef BOOTREC_H
#define BOOTREC_H
#include "dosutil.h"
/* s = 512-byte buffer; label11 = 11 chars or 0 for "NO NAME    " */
void make_bootrec(uint8_t *s, const struct bpb *b, int fixed, uint32_t serial,
                  const char *label11, int fat16);
uint32_t format_serial(void);
#endif
