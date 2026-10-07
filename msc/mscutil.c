/* HALO: small shared helpers for the MSC front end. */
#include "msc.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void *msc_alloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "nastran: out of memory in the MSC front end\n");
        exit(1);
    }
    return p;
}

void *msc_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "nastran: out of memory in the MSC front end\n");
        exit(1);
    }
    return q;
}

char *msc_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char  *p = (char *) msc_alloc(n);
    memcpy(p, s, n);
    return p;
}

void msc_upper(char *s)
{
    for (; *s; s++) *s = (char) toupper((unsigned char) *s);
}

/* trim in place, both ends */
void msc_trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n > 0 && isspace((unsigned char) s[n - 1])) s[--n] = '\0';
    while (s[i] && isspace((unsigned char) s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

int msc_isblank_line(const char *s)
{
    for (; *s; s++) if (!isspace((unsigned char) *s)) return 0;
    return 1;
}

int msc_streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/* Does this field look like a number? NASTRAN's own spelling included:
 * 1.-3 and 1.5+8 mean 1.0e-3 and 1.5e8, and a bare 3 is an integer.   */
int msc_isnum(const char *s)
{
    int seen = 0;
    if (!*s) return 0;
    if (*s == '+' || *s == '-') s++;
    while (isdigit((unsigned char) *s)) { s++; seen = 1; }
    if (*s == '.') { s++; while (isdigit((unsigned char) *s)) { s++; seen = 1; } }
    if (!seen) return 0;
    if (*s == 'E' || *s == 'e' || *s == 'D' || *s == 'd') s++;
    if (*s == '+' || *s == '-') s++;
    while (isdigit((unsigned char) *s)) s++;
    return *s == '\0';
}
