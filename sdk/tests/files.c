/* files.c - argv, getenv, malloc, fopen/fgets/fprintf, text vs binary. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static const char *names[] = { "zero", "one", "two", "three" };   /* ABS32 in .data */

int main(int argc, char **argv)
{
    const char *fn = argc > 1 ? argv[1] : "TEST.TXT";
    char line[128];
    FILE *f;
    int n = 0;

    printf("argv[0] = %s, argc = %d\n", argv[0], argc);
    for (int i = 1; i < argc; i++)
        printf("  argv[%d] = \"%s\"\n", i, argv[i]);
    printf("PATH = %s\n", getenv("PATH") ? getenv("PATH") : "(unset)");
    printf("COMSPEC = %s\n", getenv("COMSPEC") ? getenv("COMSPEC") : "(unset)");

    /* text mode: \n becomes CR LF on disk */
    if (!(f = fopen(fn, "w"))) { perror(fn); return 1; }
    for (int i = 0; i < 4; i++)
        fprintf(f, "line %d: %s\n", i, names[i]);
    fclose(f);

    /* binary mode: we should see the CRs */
    if (!(f = fopen(fn, "rb"))) { perror(fn); return 1; }
    int c, crs = 0;
    while ((c = getc(f)) != EOF) if (c == '\r') crs++;
    fclose(f);
    printf("%d CRs in binary read (expect 4)\n", crs);

    /* text mode read: CR LF back to \n */
    if (!(f = fopen(fn, "r"))) { perror(fn); return 1; }
    while (fgets(line, sizeof line, f)) {
        char *copy = malloc(strlen(line) + 1);
        if (!copy) { puts("malloc failed"); return 1; }
        strcpy(copy, line);
        if (strchr(copy, '\r')) printf("CR leaked into text read!\n");
        fputs(copy, stdout);
        free(copy);
        n++;
    }
    fclose(f);
    printf("%d lines\n", n);

    /* heap growth: allocate until it fails or 2 MB (exercises _sbrk -> XMS) */
    size_t total = 0;
    while (total < 2u * 1024 * 1024) {
        void *p = malloc(64 * 1024);
        if (!p) break;
        total += 64 * 1024;
    }
    printf("heap: got %u KB\n", (unsigned)(total / 1024));

    if (remove(fn) != 0) printf("remove: %s\n", strerror(errno));
    return 0;
}
