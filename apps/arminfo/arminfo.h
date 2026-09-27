/* arminfo.h - shared declarations for ARMINFO.EXE */
#ifndef ARMINFO_H
#define ARMINFO_H
#include <stdint.h>

/* armasm.S */
uint32_t cp15_id(void);
uint32_t cp15_cachetype(void);
uint32_t cp15_control(void);
uint32_t read_cpsr(void);
uint32_t read_sp(void);
int probe_thumb(int x);
uint32_t probe_qadd(uint32_t a, uint32_t b, uint32_t *qflag);
uint32_t probe_clz(uint32_t x);
int32_t probe_smulbb(int32_t a, int32_t b);
int32_t probe_smlawb(int32_t a, int32_t b, int32_t acc);
uint64_t probe_ldrd(const uint64_t *p);
uint32_t probe_umull_hi(uint32_t a, uint32_t b);
uint32_t probe_swp(uint32_t *p, uint32_t v);
void bs_separate(const int32_t *x, int32_t *y, unsigned n);
void bs_barrel(const int32_t *x, int32_t *y, unsigned n);
uint32_t gcd_branchy(uint32_t a, uint32_t b);
uint32_t gcd_cond(uint32_t a, uint32_t b);
void move_bytes(void *dst, const void *src, unsigned n);
void move_ldmstm(void *dst, const void *src, unsigned n);

/* bench.c */
void timer_start(void);
void timer_stop(void);
uint32_t timer_pit(void);
uint32_t pit_to_us(uint32_t pit);
uint32_t ci_work(uint32_t n);
uint32_t bench_ci_x10(void);
uint32_t bench_di_x10(uint8_t *buf, uint32_t *seek_us, uint32_t *kbs);

struct armbench {
    uint32_t bs_sep, bs_bar, bs_c;      /* M elements/s x100 */
    uint32_t gcd_b, gcd_c, gcd_cc;      /* GCDs/s */
    uint32_t mv_b, mv_c, mv_m;          /* KB/s */
    int bs_ok, gcd_ok;
};
void bench_arm(struct armbench *b, uint8_t *buf32k);

#endif
