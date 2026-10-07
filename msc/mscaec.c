/*
 * mscaec.c - the aerodynamic matrix cache (Halo One, 2026).
 *
 * A flutter sensitivity study solves one aircraft many times with the
 * structure changed (stiffness scale factors, masses added or taken
 * away) and the aerodynamic model the same. Every run recomputes the
 * doublet-lattice matrix AJJ of every (Mach, k) pair (AMG) and factors
 * it (AMP) - about 40 % of a monarch run's CPU - though neither depends
 * on the structure. With N95_AERO_CACHE=<directory> in the environment
 * the two keep what they computed in that directory and take it back
 * the next time the same input comes round:
 *
 *   AMG (mis/amgk.f, DLAMGK): each pair's AJJ, keyed by a hash of every
 *     input word the doublet lattice reads - the group's ACPT record
 *     (box geometry), its counts, the reference chord and symmetry
 *     flags, the Mach number and the reduced frequency;
 *   AMP (mis/ampk.f, AMPKC): each pair's LU factors and pivots (ZGETRF,
 *     the LAPACK solve only), keyed by a hash of the AJJ it factors.
 *
 * Both are content-addressed: an entry is found only for the same
 * input bits, and holds the bytes the computation would produce (the
 * doublet lattice and ZGETRF are deterministic here - the batched,
 * threaded kernel gives the serial loop's bits, and ZGETRF on one
 * thread, as AMPK calls it, the same bits every time), so a run from
 * the cache prints what a run without it prints, line for line. The
 * print file is not told: what the cache did goes to standard error.
 *
 * Entries are files <key>.<kind> (kind 1: AJJ, 2: LU) with a header
 * (magic, key, the sizes) checked on reading. Written as a temporary
 * file and renamed into place, so runs side by side share a directory:
 * two writing the same entry leave one of two identical files, and a
 * reader sees a whole file or none. Nothing is ever deleted here: empty
 * the directory when the aerodynamic model is gone. A monarch deck
 * (2,282 boxes) takes 42 MB (AJJ) + 83 MB (LU) per pair, about 19 GB
 * for its five Machs.
 *
 * Fortran:
 *   INTEGER FUNCTION N95AEO()                1: the cache is on
 *   CALL N95AEH (DATA, NBYTES, H)            H: INTEGER*8, updated with
 *                                            the NBYTES bytes of DATA
 *                                            (start from H = 0)
 *   INTEGER FUNCTION N95AEG (KEY, KIND, B1, N1, B2, N2)
 *                                            1 and the entry in B1 (N1
 *                                            bytes) and B2 (N2, may be
 *                                            0) when found, else 0
 *   CALL N95AEP (KEY, KIND, B1, N1, B2, N2)  store an entry
 *   CALL N95AES (KIND, IHIT)                 count a hit (IHIT 1) or a
 *                                            miss (0), reported at exit
 * KEY is INTEGER*8, KIND INTEGER, N1 and N2 INTEGER*8.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static const char magic[8] = {'N', '9', '5', 'A', 'E', 'C', '1', '\0'};

/* the directory, NULL when the cache is off (read once) */
static const char *cache_dir(void)
{
    static volatile int known = 0;
    static const char *dir = NULL;
    if (!known) {
        const char *e = getenv("N95_AERO_CACHE");
        dir = (e != NULL && e[0] != '\0') ? e : NULL;
        known = 1;
    }
    return dir;
}

int n95aeo_(void)
{
    return cache_dir() != NULL;
}

/* 64-bit hash: each 8-byte word mixed in with a multiply and a shift
 * (the tail zero-padded); the length goes in last. Not cryptographic -
 * the inputs are not adversarial - and every entry also records its
 * sizes */
void n95aeh_(const void *data, const int64_t *nbytes, uint64_t *h)
{
    const unsigned char *p = (const unsigned char *)data;
    uint64_t x = *h ^ 0x6A09E667F3BCC908ULL;
    int64_t n = *nbytes, i;
    for (i = 0; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        x ^= w;
        x *= 0x9E3779B97F4A7C15ULL;
        x ^= x >> 29;
    }
    if (i < n) {
        uint64_t w = 0;
        memcpy(&w, p + i, (size_t)(n - i));
        x ^= w;
        x *= 0x9E3779B97F4A7C15ULL;
        x ^= x >> 29;
    }
    x ^= (uint64_t)n;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 31;
    *h = x;
}

static void entry_path(char *path, size_t size, uint64_t key, int kind)
{
    snprintf(path, size, "%s/%016llx.%d", cache_dir(),
             (unsigned long long)key, kind);
}

int n95aeg_(const uint64_t *key, const int *kind, void *b1, const int64_t *n1,
            void *b2, const int64_t *n2)
{
    char path[4096];
    char head[8];
    uint64_t k;
    int64_t s1, s2;
    FILE *f;
    int ok = 0;
    if (cache_dir() == NULL)
        return 0;
    entry_path(path, sizeof path, *key, *kind);
    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    if (fread(head, 1, 8, f) == 8 && memcmp(head, magic, 8) == 0 &&
        fread(&k, sizeof k, 1, f) == 1 && k == *key &&
        fread(&s1, sizeof s1, 1, f) == 1 && s1 == *n1 &&
        fread(&s2, sizeof s2, 1, f) == 1 && s2 == *n2 &&
        fread(b1, 1, (size_t)s1, f) == (size_t)s1 &&
        (s2 == 0 || fread(b2, 1, (size_t)s2, f) == (size_t)s2))
        ok = 1;
    fclose(f);
    return ok;
}

void n95aep_(const uint64_t *key, const int *kind, const void *b1,
             const int64_t *n1, const void *b2, const int64_t *n2)
{
    static volatile long serial = 0;
    char path[4096], temp[4200];
    FILE *f;
    int ok;
    long s;
    if (cache_dir() == NULL)
        return;
    entry_path(path, sizeof path, *key, *kind);
#if defined(__GNUC__)
    s = __sync_fetch_and_add(&serial, 1);
#else
    s = serial++;
#endif
    snprintf(temp, sizeof temp, "%s.tmp%ld.%ld", path, (long)getpid(), s);
    f = fopen(temp, "wb");
    if (f == NULL)
        return;
    ok = fwrite(magic, 1, 8, f) == 8 &&
         fwrite(key, sizeof *key, 1, f) == 1 &&
         fwrite(n1, sizeof *n1, 1, f) == 1 &&
         fwrite(n2, sizeof *n2, 1, f) == 1 &&
         fwrite(b1, 1, (size_t)*n1, f) == (size_t)*n1 &&
         (*n2 == 0 || fwrite(b2, 1, (size_t)*n2, f) == (size_t)*n2);
    ok = (fclose(f) == 0) && ok;
    /* rename over an entry another run wrote first: the same bytes (POSIX
       replaces it; Windows refuses, and the temporary goes) */
    if (!ok || rename(temp, path) != 0)
        remove(temp);
}

/* what the cache did, per kind, told on standard error at exit */
static volatile long hits[3], misses[3];
static void report(void)
{
    int k;
    for (k = 1; k <= 2; k++)
        if (hits[k] + misses[k] > 0)
            fprintf(stderr, "N95_AERO_CACHE: %s %ld of %ld pairs from the cache\n",
                    k == 1 ? "AMG (AJJ)" : "AMP (LU)", hits[k], hits[k] + misses[k]);
}

void n95aes_(const int *kind, const int *ihit)
{
    static volatile int registered = 0;
    if (*kind < 1 || *kind > 2)
        return;
#if defined(__GNUC__)
    if (__sync_bool_compare_and_swap(&registered, 0, 1))
        atexit(report);
#else
    if (!registered) {
        registered = 1;
        atexit(report);
    }
#endif
#if defined(__GNUC__)
    if (*ihit)
        __sync_fetch_and_add(&hits[*kind], 1);
    else
        __sync_fetch_and_add(&misses[*kind], 1);
#else
    if (*ihit)
        hits[*kind]++;
    else
        misses[*kind]++;
#endif
}
