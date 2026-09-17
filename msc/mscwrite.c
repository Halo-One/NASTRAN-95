/* HALO: writing the translated deck in the input NASTRAN-95 reads.
 *
 * Small field, eight columns, continuations tagged so that the reader
 * pairs them unambiguously. Nothing is written in free field: the 1970s
 * reader accepts it, but it accepts it by a different path with its own
 * limits, and the translated deck is meant to be readable next to the
 * original when a number disagrees.
 *
 * The hard part is a real number in eight columns. -1.2345678e-05 is
 * thirteen characters and has to become eight without losing a digit
 * that matters. NASTRAN's own convention, which its reader has always
 * understood, is to drop the E: 1.23457-5. That buys two characters
 * over %g and is what this writes.
 */
#include "msc.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NASTRAN's own reading of a real: 1.5-3 is 1.5e-3, 1.+6 is a million. */
static double read_nas(const char *s)
{
    char buf[32];
    int  i, o = 0, digit = 0;
    for (i = 0; s[i] && o < 30; i++) {
        char ch = s[i];
        if ((ch == '+' || ch == '-') && i > 0 && digit &&
            s[i-1] != 'E' && s[i-1] != 'e') buf[o++] = 'E';
        if (isdigit((unsigned char) ch)) digit = 1;
        buf[o++] = ch;
    }
    buf[o] = '\0';
    return atof(buf);
}

/* Eight characters, never more, holding as much of `v` as eight
 * characters can.
 *
 * Every spelling that fits is tried -- plain decimals with 0 to 7
 * places, and NASTRAN's exponent form (the E dropped: 1.23457-5) with 0
 * to 6 mantissa digits -- and the one that reads back closest to the
 * value wins; on a tie the shorter one. A whole-number mantissa keeps
 * its decimal point, because without one the reader takes the field
 * for an integer and "1+6" on a CELAS2 is a data error where "1.+6" is
 * a million. Returns 1 when the closest spelling is still more than
 * single precision away from the value, which is the caller's cue to
 * say so.                                                             */
int msc_r8(double v, char out[9])
{
    char   b[64], best[9];
    double besterr = -1.0;
    int    i;

    if (v == 0.0) { strcpy(out, "0.0"); return 0; }

    for (i = 0; i <= 7; i++) {
        double err;
        snprintf(b, sizeof(b), "%.*f", i, v);
        if (strlen(b) > 8) continue;
        if (!strchr(b, '.')) strcat(b, ".");
        if (strlen(b) > 8) continue;
        err = fabs(read_nas(b) - v);
        if (besterr < 0.0 || err < besterr ||
            (err == besterr && strlen(b) < strlen(best))) {
            besterr = err; strcpy(best, b);
        }
    }
    for (i = 6; i >= 0; i--) {
        char  *e, mant[32], ex[16];
        size_t n;
        double err;
        snprintf(b, sizeof(b), "%.*E", i, v);
        e = strchr(b, 'E');
        if (!e) continue;
        n = (size_t) (e - b);
        if (n >= sizeof(mant)) n = sizeof(mant) - 1;
        memcpy(mant, b, n);
        mant[n] = '\0';
        if (strchr(mant, '.')) {
            size_t m = strlen(mant);
            while (m > 1 && mant[m-1] == '0') mant[--m] = '\0';
        }
        if (!strchr(mant, '.')) strcat(mant, ".");
        snprintf(ex, sizeof(ex), "%+d", atoi(e + 1));
        if (strlen(mant) + strlen(ex) > 8) continue;
        snprintf(b, sizeof(b), "%s%s", mant, ex);
        err = fabs(read_nas(b) - v);
        if (besterr < 0.0 || err < besterr ||
            (err == besterr && strlen(b) < strlen(best))) {
            besterr = err; strcpy(best, b);
        }
    }
    if (besterr < 0.0) {           /* not reachable for a finite double */
        snprintf(out, 9, "%.1E", v);
        out[8] = '\0';
        return 1;
    }
    strcpy(out, best);
    return besterr > 1e-6 * fabs(v);
}

/* One field, in eight columns, on its way out.
 *
 * A real number written by a modern tool comes through as 0.00e+00 and
 * the 1970s reader stops on it with "POSSIBLE ERROR IN EXPONENT": it
 * reads E, not e. Numbers are therefore upper-cased on the way out.
 * Only numbers -- a label or a name is written exactly as it arrived,
 * because the deck may be matching it against something.
 */
static void put_field(FILE *fp, const char *s, const msc_card *c)
{
    char b[MSC_FLDLEN];
    int  i;
    if (msc_isnum(s)) {
        /* A free-field deck writes numbers as wide as it likes:
         * -6.89e+04 is nine characters. Cut to eight it reads as
         * -6.89E+0, which is -6.89, and the model is quietly wrong by
         * four orders of magnitude (it was a PBAR's product of inertia,
         * and the eigensolver spent ten minutes on the result). So a
         * number that does not fit is re-spelled to fit, not cut.    */
        if (strlen(s) > 8 || strchr(s, 'e') || strchr(s, 'd')) {
            double v = read_nas(s);
            if (strlen(s) > 8) {
                if (msc_r8(v, b))
                    msc_msg_at(MSC_WARN, 9221, c,
                        "%s does not fit in eight columns and its closest "
                        "eight-column\nspelling, %s, is more than single "
                        "precision away from it.", s, b);
                fprintf(fp, "%-8.8s", b);
                return;
            }
        }
        for (i = 0; s[i] && i < MSC_FLDLEN - 1; i++)
            b[i] = (char) ((s[i] == 'e') ? 'E' : (s[i] == 'd') ? 'D' : s[i]);
        b[i] = '\0';
        fprintf(fp, "%-8.8s", b);
        return;
    }
    if (strlen(s) > 8) {
        /* a name or label that is too long cannot be shortened without
         * changing what it refers to                                   */
        msc_msg_at(MSC_FATAL, 9222, c,
            "the field \"%s\" is %d characters and the 1970s input has room\n"
            "for eight. It is not a number, so it cannot be re-spelled.\n"
            "FIX   Shorten it in the MSC deck.", s, (int) strlen(s));
    }
    fprintf(fp, "%-8.8s", s);
}

/* Does any number on this card lose more than single precision in
 * eight columns? Then the card is written in large field, where it
 * loses nothing.                                                      */
static int needs_large(const msc_card *c)
{
    int i;
    char b[9];
    for (i = 1; i <= c->nfld; i++) {
        const char *s = msc_f(c, i);
        double v, err;
        if (strlen(s) <= 8 || !msc_isnum(s)) continue;
        v = read_nas(s);
        msc_r8(v, b);
        err = fabs(read_nas(b) - v);
        /* One part in a hundred thousand is the line. Eight columns hold
         * six or seven figures, which is more than any coordinate or
         * property in this repository carries meaning to; a card is
         * only worth the large-field layout (two echo lines, which the
         * repository's echo readers do not parse) when eight columns
         * would actually change the model.                            */
        if (err > 1e-5 * fabs(v)) return 1;
    }
    return 0;
}

/* one sixteen-column field: the number as it came, exponent in upper
 * case, which sixteen columns always have room for                    */
static void put_field16(FILE *fp, const char *s)
{
    char b[MSC_FLDLEN];
    int  i;
    for (i = 0; s[i] && i < MSC_FLDLEN - 1; i++)
        b[i] = (char) ((s[i] == 'e') ? 'E' : (s[i] == 'd') ? 'D' : s[i]);
    b[i] = '\0';
    fprintf(fp, "%-16.16s", b);
}

/* the large-field card: NAME* in columns 1-8, four fields of sixteen,
 * a marker beginning with '*' in 73-80 and again in 1-8 of the next
 * line. Every card's field count is a multiple of eight (the reader
 * pads), so a large-field card is always an even number of lines.    */
static void write_large(FILE *fp, const msc_card *c, unsigned long *tagno)
{
    int  i, k, nf = c->nfld;
    int  first = 1;
    char tag[9], next[9], name[10];

    while (nf > 0 && msc_f(c, nf)[0] == '\0') nf--;
    snprintf(name, sizeof(name), "%.7s*", c->name);
    tag[0] = '\0';
    for (i = 1; i <= nf; i += 4) {
        int last = (i + 4 > nf);
        snprintf(next, sizeof(next), "*%07lX", ++*tagno);
        fprintf(fp, "%-8.8s", first ? name : tag);
        for (k = i; k < i + 4; k++) put_field16(fp, msc_f(c, k));
        if (!last) fprintf(fp, "%-8.8s", next);
        fprintf(fp, "\n");
        strcpy(tag, next);
        first = 0;
    }
}

/* one card, small field, with continuations.
 *
 * NASTRAN pairs a continuation line with its parent by the tag in the
 * parent's field 10 and the continuation's field 1, so a tag has to be
 * unique across the whole deck, not just within one card. One counter
 * hands them out: +0000001, +0000002, ... Seven hex digits is 268
 * million continuation lines, which no deck this side of the open core
 * limit can reach.
 */
void msc_write_card(FILE *fp, const msc_card *c)
{
    static unsigned long tagno = 0;
    int  i, k, nf = c->nfld;
    int  first = 1;
    char tag[9], next[9];

    /* trailing blank fields are not written */
    while (nf > 0 && msc_f(c, nf)[0] == '\0') nf--;

    if (needs_large(c)) {
        msc_tally("written in large field", c->name);
        write_large(fp, c, &tagno);
        return;
    }

    if (nf <= 8) {
        fprintf(fp, "%-8.8s", c->name);
        for (i = 1; i <= nf; i++) put_field(fp, msc_f(c, i), c);
        fprintf(fp, "\n");
        return;
    }

    tag[0] = '\0';
    for (i = 1; i <= nf; i += 8) {
        int last = (i + 8 > nf);
        snprintf(next, sizeof(next), "+%07lX", ++tagno);
        fprintf(fp, "%-8.8s", first ? c->name : tag);
        for (k = i; k < i + 8; k++) put_field(fp, msc_f(c, k), c);
        if (!last) fprintf(fp, "%-8.8s", next);
        fprintf(fp, "\n");
        strcpy(tag, next);
        first = 0;
    }
}

void msc_write_name(FILE *fp, const char *name, const char *fields[], int n)
{
    int i;
    fprintf(fp, "%-8.8s", name);
    for (i = 0; i < n && i < 8; i++) fprintf(fp, "%-8.8s", fields[i]);
    fprintf(fp, "\n");
}
