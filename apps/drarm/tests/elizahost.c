/* tests/elizahost.c - the conversation engine on the host: one reply per stdin line */
#include <stdio.h>
#include <string.h>
#include "../eliza.h"
int main(void)
{
    char line[256]; int act;
    eliza_init("TESTER");
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        { const char *r = eliza_reply(line, &act); printf("> %s\n%s%s\n", line, r, act ? "  [PARITY]" : ""); }
    }
    return 0;
}
