/* Exceptions, modes, banked registers, user-bank LDM/STM, exception returns,
   interworking. Output is compared line by line with QEMU. */
#include "lib.h"

volatile u32 rec[64];          /* written by the asm handlers */

/* vector table at 0: ldr pc,[pc,#0x18] x8, then handler addresses */
extern void h_reset(void), h_und(void), h_svc(void), h_pabt(void), h_dabt(void), h_irq(void), h_fiq(void);
__asm__(
  ".arm\n"
  /* UND: record lr, spsr, the instruction; skip it */
  "h_und:  stmfd sp!, {r0-r2}\n"
  "        ldr r0, =rec\n"
  "        str lr, [r0, #0]\n"
  "        mrs r1, spsr\n"
  "        str r1, [r0, #4]\n"
  "        tst r1, #0x20\n"
  "        ldrneh r2, [lr, #-2]\n"
  "        ldreq r2, [lr, #-4]\n"
  "        str r2, [r0, #8]\n"
  "        mrs r1, cpsr\n"
  "        str r1, [r0, #12]\n"
  "        ldr r1, [r0, #16]\n"
  "        add r1, r1, #1\n"
  "        str r1, [r0, #16]\n"
  "        ldmfd sp!, {r0-r2}\n"
  "        movs pc, lr\n"
  /* SVC: comment field -> rec[5]; r0 += number; return via ldm ^ */
  "h_svc:  stmfd sp!, {r1-r3, lr}\n"
  "        mrs r1, spsr\n"
  "        tst r1, #0x20\n"
  "        ldrneh r2, [lr, #-2]\n"
  "        bicne r2, r2, #0xff00\n"
  "        ldreq r2, [lr, #-4]\n"
  "        biceq r2, r2, #0xff000000\n"
  "        ldr r3, =rec\n"
  "        str r2, [r3, #20]\n"
  "        str r1, [r3, #24]\n"
  "        str lr, [r3, #28]\n"
  "        add r0, r0, r2\n"
  "        mrs r1, cpsr\n"
  "        str r1, [r3, #32]\n"
  "        str sp, [r3, #36]\n"
  "        ldmfd sp!, {r1-r3, pc}^\n"
  /* prefetch abort (BKPT): record, return past it */
  "h_pabt: ldr sp, =rec\n"          /* abt sp: clobber freely, restore below */
  "        str lr, [sp, #40]\n"
  "        mrs lr, spsr\n"
  "        str lr, [sp, #44]\n"
  "        mrs lr, cpsr\n"
  "        bic lr, lr, #0x100\n"          /* QEMU sets the v6 A bit on abort entry */
  "        str lr, [sp, #48]\n"
  "        ldr sp, =0x7D0000\n"
  "        ldr lr, =rec\n"
  "        ldr lr, [lr, #40]\n"
  "        movs pc, lr\n"
  "h_reset:\n"
  "h_dabt:\n"
  "h_irq:\n"
  "h_fiq:  b h_fiq\n"
  ".ltorg\n"
);

static void install(void) {
  volatile u32 *v = (volatile u32 *)0;
  void (*h[8])(void) = {h_reset, h_und, h_svc, h_pabt, h_dabt, h_reset, h_irq, h_fiq};
  for (int i = 0; i < 8; i++) { v[i] = 0xe59ff018; v[8 + i] = (u32)h[i]; }
}

static void pr(const char *what, int n) {
  printf_("%s:", what);
  for (int i = 0; i < n; i++) printf_(" %08x", rec[i]);
  printf_("\n");
  for (int i = 0; i < 64; i++) rec[i] = 0;
}

/* ---- tests written in asm, each returns something in r0 */
u32 t_svc_arm(u32 x);          /* from SYS mode */
u32 t_svc_thumb(u32 x);
u32 t_und_arm(void);
u32 t_und_thumb(void);
u32 t_und_cp(void);
u32 t_bkpt(void);
u32 t_banked(u32 *out);
u32 t_ldmuser(u32 *out);
u32 t_interwork(u32 *out);
__asm__(
  ".arm\n"
  ".global t_svc_arm\n.type t_svc_arm,%function\n"
  "t_svc_arm: stmfd sp!, {r4, lr}\n"
  "        msr cpsr_c, #0xdf\n"           /* SYS */
  "        msr cpsr_f, #0xa0000000\n"
  "        svc #0x123\n"
  "        mrs r4, cpsr\n"
  "        msr cpsr_c, #0xd3\n"
  "        ldr r1, =rec\n"
  "        str r4, [r1, #52]\n"
  "        ldmfd sp!, {r4, pc}\n"
  ".global t_svc_thumb\n.type t_svc_thumb,%function\n"
  "t_svc_thumb: stmfd sp!, {r4, lr}\n"
  "        msr cpsr_c, #0xdf\n"
  "        adr r1, 1f+1\n"
  "        bx r1\n"
  ".thumb\n"
  "1:      svc #0x77\n"
  "        svc #0x01\n"
  "        mov r1, pc\n"
  "        bx r1\n"
  "        nop\n"
  ".arm\n"
  "        msr cpsr_c, #0xd3\n"
  "        ldmfd sp!, {r4, pc}\n"
  ".global t_und_arm\n.type t_und_arm,%function\n"
  "t_und_arm: mov r0, #5\n"
  "        .word 0xe7f000f0\n"           /* permanently undefined */
  "        add r0, r0, #1\n"
  "        .word 0xe6000010\n"           /* media space (v6) -> undefined on v5 */
  "        add r0, r0, #1\n"
  "        bx lr\n"
  ".global t_und_cp\n.type t_und_cp,%function\n"
  "t_und_cp: mov r0, #9\n"
  "        mcr p5, 0, r0, c1, c2, 3\n"   /* no coprocessor 5 */
  "        add r0, r0, #1\n"
  "        cdp p7, 1, c1, c2, c3, 4\n"
  "        add r0, r0, #1\n"
  "        bx lr\n"
  ".global t_und_thumb\n.type t_und_thumb,%function\n"
  "t_und_thumb: adr r1, 1f+1\n"
  "        mov r0, #3\n"
  "        bx r1\n"
  ".thumb\n"
  "1:      .short 0xde42\n"              /* undefined in Thumb */
  "        add r0, #1\n"
  "        bx lr\n"
  ".arm\n"
  ".global t_bkpt\n.type t_bkpt,%function\n"
  "t_bkpt: mov r0, #7\n"
  "        bkpt #0x1234\n"
  "        add r0, r0, #1\n"
  "        bx lr\n"
  /* banked registers: give every mode distinct r8-r14 and read them back */
  ".global t_banked\n.type t_banked,%function\n"
  "t_banked: stmfd sp!, {r4-r11, lr}\n"
  "        mov r12, r0\n"
  "        mrs r11, cpsr\n"
  "        mov r1, sp\n"
  "        msr cpsr_c, #0xd1\n"            /* FIQ */
  "        mov r2, sp\n"
  "        mov r8, #0x81\n  mov r9, #0x91\n  mov r10, #0xa1\n  mov r11, #0xb1\n  mov r12, #0xc1\n  mov sp, #0xd1\n  mov lr, #0xe1\n"
  "        msr cpsr_c, #0xd2\n  mov r3, sp\n  mov sp, #0xd2\n  mov lr, #0xe2\n"
  "        msr cpsr_c, #0xd7\n  mov r4, sp\n  mov sp, #0xd7\n  mov lr, #0xe7\n"
  "        msr cpsr_c, #0xdb\n  mov r5, sp\n  mov sp, #0xdb\n  mov lr, #0xeb\n"
  "        msr cpsr_c, #0xdf\n  mov r6, sp\n  mov r7, lr\n  mov sp, #0xdf\n  mov lr, #0xef\n"
  "        msr cpsr_c, #0xd3\n"
  "        mov r8, #0x83\n"             /* note: r8-r12 of non-FIQ modes are shared */
  "        msr cpsr_c, #0xd1\n"
  "        stmia r0!, {r8-r12, sp, lr}\n"
  "        mov sp, r2\n"
  "        msr cpsr_c, #0xd2\n  stmia r0!, {r8, sp, lr}\n  mov sp, r3\n"
  "        msr cpsr_c, #0xd7\n  stmia r0!, {r8, sp, lr}\n  mov sp, r4\n"
  "        msr cpsr_c, #0xdb\n  stmia r0!, {r8, sp, lr}\n  mov sp, r5\n"
  "        msr cpsr_c, #0xdf\n  stmia r0!, {r8, sp, lr}\n  mov sp, r6\n  mov lr, r7\n"
  "        msr cpsr_c, #0xd3\n"
  "        mrs r1, cpsr\n  str r1, [r0], #4\n"
  "        ldmfd sp!, {r4-r11, pc}\n"
  /* ldm/stm with ^ from SVC: user bank */
  ".global t_ldmuser\n.type t_ldmuser,%function\n"
  "t_ldmuser: stmfd sp!, {r4-r7, lr}\n"
  "        adr r1, 2f\n"
  "        ldmia r1, {r13, r14}^\n"        /* load user sp/lr */
  "        nop\n"
  "        mov r4, sp\n  mov r5, lr\n"
  "        stmia r0!, {r4, r5}\n"
  "        msr cpsr_c, #0xdf\n  mov r6, sp\n  mov r7, lr\n  ldr sp, =0x7B0000\n  msr cpsr_c, #0xd3\n"
  "        stmia r0!, {r6, r7}\n"
  "        stmia r0, {r13, r14}^\n"        /* store user sp/lr */
  "        nop\n"
  "        add r0, r0, #8\n"
  "        msr cpsr_c, #0xd1\n"            /* FIQ: r8 banked; ^ must use user r8 */
  "        mov r8, #0x55\n"
  "        adr r1, 2f\n"
  "        ldmia r1, {r8}^\n"
  "        nop\n"
  "        str r8, [r0], #4\n"
  "        msr cpsr_c, #0xd3\n"
  "        str r8, [r0], #4\n"
  "        ldmfd sp!, {r4-r7, pc}\n"
  "2:      .word 0x11112222, 0x33334444\n"
  /* interworking: ldr pc / ldm pc / blx reg / blx imm / pop pc from thumb */
  ".global t_interwork\n.type t_interwork,%function\n"
  "t_interwork: stmfd sp!, {r4-r6, lr}\n"
  "        mov r4, r0\n"
  "        adr r1, 3f\n  ldr r2, =th1+1\n  str r2, [r1]\n"
  "        ldr pc, [r1]\n"                  /* -> thumb */
  "back1:  str r0, [r4], #4\n"
  "        ldr r2, =th2+1\n  ldr r3, =back2\n  stmfd sp!, {r2, r3}\n"
  "        ldmfd sp!, {r3, pc}\n"           /* -> thumb, r3 = back2 */
  "back2:  str r0, [r4], #4\n"
  "        ldr r2, =th3+1\n"
  "        blx r2\n"
  "        str r0, [r4], #4\n"
  "        blx th4\n"
  "        str r0, [r4], #4\n"
  "        mov r0, r4\n"
  "        ldmfd sp!, {r4-r6, pc}\n"
  "3:      .word 0\n"
  ".thumb\n.thumb_func\n"
  "th1:    mov r0, #0x31\n  ldr r1, =back1\n  bx r1\n"
  ".thumb_func\n"
  "th2:    mov r0, #0x32\n  bx r3\n"
  ".thumb_func\n"
  "th3:    push {lr}\n  mov r0, #0x33\n  bl th5\n  pop {pc}\n"          /* pop pc -> ARM (bit0 clear) */
  ".thumb_func\n"
  "th4:    mov r0, #0x34\n  bx lr\n"
  ".thumb_func\n"
  "th5:    add r0, #0x10\n  mov r1, lr\n  blx arm6\n  bx r1\n"
  ".arm\n"
  "arm6:   add r0, r0, #0x100\n  bx lr\n"
  ".ltorg\n"
);

u32 t_flags_ret2(u32 *out);
__asm__(
  ".arm\n"
  ".global t_flags_ret2\n.type t_flags_ret2,%function\n"
  "t_flags_ret2: stmfd sp!, {r4-r6, lr}\n"
  "        mov r4, r0\n"
  "        ldr r1, =0x600000f3\n  msr spsr_cxsf, r1\n"
  "        adr lr, 6f+1\n"
  "        movs pc, lr\n"
  ".thumb\n"
  "6:      mov r2, #0\n"
  "        bcc 7f\n"          /* C set by the restored flags -> not taken */
  "        add r2, #1\n"
  "7:      bne 8f\n"          /* Z set -> not taken */
  "        add r2, #2\n"
  "8:      str r2, [r4]\n"
  "        add r4, #4\n"
  "        ldr r1, =9f\n"
  "        bx r1\n"
  ".arm\n"
  "9:      mrs r1, cpsr\n  str r1, [r4], #4\n"
  "        mov r0, r4\n"
  "        ldmfd sp!, {r4-r6, pc}\n"
  ".ltorg\n"
);

int main(void) {
  install();
  u32 out[64];
  printf_("excpt start\n");
  for (int i = 0; i < 64; i++) rec[i] = 0;
  u32 r = t_svc_arm(0x1000); printf_("svc arm r0=%08x\n", r); pr("rec", 14);
  r = t_svc_thumb(0x2000); printf_("svc thumb r0=%08x\n", r); pr("rec", 10);
  r = t_und_arm(); printf_("und arm r0=%x\n", r); pr("rec", 5);
  r = t_und_cp(); printf_("und cp r0=%x\n", r); pr("rec", 5);
  r = t_und_thumb(); printf_("und thumb r0=%x\n", r); pr("rec", 5);
  r = t_bkpt(); printf_("bkpt r0=%x\n", r); pr("rec", 13);
  for (int i = 0; i < 64; i++) out[i] = 0;
  t_banked(out); printf_("banked:"); for (int i = 0; i < 20; i++) printf_(" %x", out[i]); printf_("\n");
  for (int i = 0; i < 64; i++) out[i] = 0;
  t_ldmuser(out); printf_("ldmuser:"); for (int i = 0; i < 8; i++) printf_(" %x", out[i]); printf_("\n");
  for (int i = 0; i < 64; i++) out[i] = 0;
  t_interwork(out); printf_("interwork:"); for (int i = 0; i < 4; i++) printf_(" %x", out[i]); printf_("\n");
  for (int i = 0; i < 64; i++) out[i] = 0;
  t_flags_ret2(out); printf_("flagsret2: %x %x\n", out[0], out[1]);
  printf_("excpt done\n");
  return 0;
}
