/* An ARM program that tail-calls Thumb code, which calls newlib's ARM strlen:
 * the linker joins them with interworking veneers, one of which holds an
 * absolute address that elf2exe must relocate (Arm's own toolchain for macOS
 * ships a Thumb newlib, so there every C program has such veneers). */
#include <stdio.h>
#include <string.h>

__attribute__((target("thumb"), noinline)) int thumb_len(const char *s)
{
    return (int)strlen(s) + 1000;
}

__attribute__((noinline)) int arm_tail(const char *s) { return thumb_len(s); }  /* b thumb_len */

int main(void)
{
    printf("THUMB %d\n", arm_tail("hello, thumb"));
    return 0;
}
