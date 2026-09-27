/* resend.c - linked right after res.c and resasm.S: marks the end of PRINT's
 * resident part (see res.c) */
char r_end[4] __attribute__((section(".text.unlikely.przz,\"ax\",%progbits @"))) = { 'E', 'N', 'D', 0 };
