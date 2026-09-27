/*
 * tinystr.c - small versions of the C string functions for SHELLB.COM (the
 * resident loader), which uses only a few of them: newlib's word-at-a-time
 * versions cost ~2.6 KB of resident memory, these ~0.4 KB. Linked before
 * the C library, so these are the ones used. Not used by SHELLC.
 */
#include <stddef.h>

void *memcpy(void *d, const void *s, size_t n)
{
    char *a = d;
    const char *b = s;
    while (n--) *a++ = *b++;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    char *a = d;
    while (n--) *a++ = (char)c;
    return d;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

int strcmp(const char *a, const char *b) { return strncmp(a, b, (size_t)-1); }

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n && *s) { *d++ = *s++; n--; }
    while (n--) *d++ = 0;
    return r;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return 0;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = 0;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

char *strpbrk(const char *s, const char *set)
{
    for (; *s; s++)
        if (strchr(set, *s)) return (char *)s;
    return 0;
}
