/*
 * mscisa.c - which build of the hot kernels to run (Halo One, 2026).
 *
 * The PK method's QR and products (mis/fa1pkq.f), its eigenvector solve
 * (mis/egnvct.f) and the doublet-lattice kernel (mis/tkerv.f) are each
 * compiled twice: for the baseline x86-64 the executable promises to run
 * on, and for x86-64-v3 (AVX2, 2013 onwards) under a V3 name, which
 * CMakeLists.txt makes from the same source. Both are -ffp-contract=off
 * with no -ffast-math, so neither fuses a multiply-add nor reorders a
 * sum; the wider vectors change the speed and not a bit (checked on each
 * kernel against the baseline). Each kernel asks N95ISA() on entry and
 * hands over to its V3 build when the answer is 3.
 *
 * Fortran: INTEGER FUNCTION N95ISA() - 3 when the processor has
 * x86-64-v3, else 0; N95_ISA=0 in the environment keeps the baseline.
 * Worked out once; the race between threads asking first is harmless
 * (they all get the same answer).
 */
#include <stdlib.h>

/*
 * Fortran: INTEGER FUNCTION N95DLQ() - 1 when the doublet lattice is to
 * use the quartic kernel (mis/dlmq.f: Rodden, Taylor and McIntosh 1998,
 * what MSC and Simcenter Nastran call QUARTICDLM, NASTRAN SYSTEM(270)=1),
 * 0 for NASA's parabolic one. N95_DLM_QUARTIC in the environment: the
 * front end sets it to 1 when the deck asks for SYSTEM(270)=1 (and
 * leaves a value the user set alone), and the SOL 145 driver's children
 * inherit it. Read once, as N95ISA is.
 */
int n95dlq_(void)
{
    static volatile int q = -1;
    if (q < 0) {
        const char *e = getenv("N95_DLM_QUARTIC");
        q = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    return q;
}

int n95isa_(void)
{
    static volatile int level = -1;
    if (level < 0) {
        int l = 0;
        const char *e = getenv("N95_ISA");
#if defined(__GNUC__) && defined(__x86_64__) && !defined(N95_NO_ISA_V3)
        __builtin_cpu_init();
        if (__builtin_cpu_supports("x86-64-v3"))
            l = 3;
#endif
        if (e != NULL && e[0] == '0')
            l = 0;
        level = l;
    }
    return level;
}
