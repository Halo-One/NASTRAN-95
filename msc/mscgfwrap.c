/* HALO: a workaround for a libgfortran defect that crashed four of NASA's
 * demonstration decks depending on the process's memory layout.
 *
 * NASTRAN builds many of its output formats at run time in INTEGER arrays
 * of Hollerith words and hands the array to WRITE as the format:
 *
 *     INTEGER FMT(300)
 *     ...
 *     FMT(IFMT+1) = CPAREN
 *     WRITE (L,FMT) IHARM, ...
 *
 * gfortran accepts that (a legacy extension) and passes the runtime the
 * array's storage as a character string of its full length, 1,200 bytes
 * here, of which the format text occupies the first hundred or two and
 * the rest is whatever the static array holds: zeros, since it lives in
 * .bss and is never written past the closing parenthesis.
 *
 * libgfortran (io/format.c, parse_format) copies the format string with
 * fc_strdup_notrim, which is strndup: it stops at the first NUL byte, so
 * the copy is a few hundred bytes long. But dtp->format_len stays at
 * 1,200, and save_parsed_format then runs format_hash over the copy for
 * format_len bytes: a read of up to a kilobyte past the end of a small
 * heap block. Whether that read crosses into an unmapped page depends on
 * where malloc put the block, which depends on everything allocated
 * before it - the environment block included. That is the whole of the
 * "completes or dies with SIGSEGV depending on the size of the
 * environment" behaviour of d03021a, d03031a, d07021a and d07022a: the
 * fluid-element output goes through OFP's harmonic-point path, whose
 * format is short and whose copy therefore has the most room to overrun.
 * The defect is present in GCC 15 and on trunk (format.c, format_hash;
 * runtime/string.c, fc_strdup_notrim), and nothing in NASA's code is
 * wrong by the standard of the extension it uses.
 *
 * The fix here is at link time: -Wl,--wrap=_gfortrani_fc_strdup_notrim
 * routes libgfortran's own calls to this function, which copies all
 * src_len bytes (NULs included) and NUL-terminates. The copy is then as
 * long as the runtime believes it is, format_hash stays in bounds, the
 * parser still stops at the closing parenthesis it always stopped at,
 * and the cache comparison (strncmp, which stops at the first NUL) is
 * unchanged. env.o's use of the same routine, for environment strings
 * that hold no NULs, gets a byte-identical result.
 *
 * Both executables get it (CMakeLists.txt), because both link
 * libgfortran and both run OFP.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *__wrap__gfortrani_fc_strdup_notrim(const char *src, size_t src_len)
{
    char *p = (char *) malloc(src_len + 1);
    if (!p) {
        fputs("nastran: memory allocation failed copying a format string\n",
              stderr);
        abort();
    }
    memcpy(p, src, src_len);
    p[src_len] = '\0';
    return p;
}
