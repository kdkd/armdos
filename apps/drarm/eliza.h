/* eliza.h - Dr. ARMitso's conversation engine */
#ifndef DRARM_ELIZA_H
#define DRARM_ELIZA_H
enum { ELIZA_NONE, ELIZA_PARITY };
void eliza_init(const char *name);
/* The doctor's reply (upper case) to one line of input. *action = ELIZA_PARITY
 * when the patient has sworn once too often (the crash gag). */
const char *eliza_reply(const char *input, int *action);
#endif
