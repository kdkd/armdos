/* t_hello.c - milestone 2: a program EXEC'd by the test shell prints through
   several INT 21h paths and checks its start-up state */
#include "t.h"
#include <dos.h>

int main(int argc, char **argv)
{
    struct armregs r;
    t_begin("hello");
    printf("Hello from printf (newlib -> AH=40h)\n");
    t_dos3(0x0900, 0, 0, (uint32_t)"Hello via AH=09h\r\n$", &r);
    const char *s = "AH=02h ok\r\n";
    for (; *s; s++) t_dos3(0x0200, 0, 0, (uint8_t)*s, &r);
    t_dos3(0x3000, 0, 0, 0, &r);
    T_EQ(r.r0 & 0xFFFF, 0x0004);
    T_EQ((r.r1 >> 8) & 0xFF, 0xFF);
    T_EQ(_osmajor, 4);
    T_EQ(argc, 3);
    if (argc == 3) {
        T_CHECK(!strcmp(argv[1], "ONE"), "argv[1] = %s", argv[1]);
        T_CHECK(!strcmp(argv[2], "two"), "argv[2] = %s", argv[2]);
    }
    T_CHECK(strstr(argv[0], "K_HELLO.EXE") != 0, "argv[0] = %s", argv[0]);
    /* PSP */
    struct psp *p = _armdos_psp;
    T_EQ(p->int20, 0xDF20);
    T_EQ(*(uint32_t *)((uint8_t *)p + 0x50), 0xEF000021);
    T_EQ(p->fcb1[1], 'O');
    T_EQ(p->fcb2[1], 'T');
    t_dos3(0x6200, 0, 0, 0, &r);
    T_EQ(r.r1 & 0xFFFF, (uint32_t)p >> 4);
    /* COMSPEC is in the environment the shell passed down */
    const char *cs = getenv("COMSPEC");
    T_CHECK(cs && strstr(cs, "TSHELL"), "COMSPEC=%s", cs ? cs : "(none)");
    return t_end();
}
