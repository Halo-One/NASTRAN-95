/* HALO: diagnostics for the MSC front end.
 *
 * Written to look like the messages the solver itself prints, because
 * anyone reading them has just been reading those:
 *
 *   *** USER FATAL MESSAGE 9203 (MSCXLAT)
 *       CBEAM 40021 has a non-uniform cross section (PBEAM 40021 gives
 *       different properties at its two ends).
 *       CARD  CBEAM 40021, ase/vibe/monarch_gvt_beams.dat line 118
 *       FIX   Split it into two CBARs, or accept the end-A properties
 *             with PARAM,MSCBEAM,AVERAGE.
 *
 * Three things every message carries, and the third is the one usually
 * missing from a solver: what happened, where it happened, and what to
 * do about it. A message that only says a card is illegal leaves the
 * reader to guess which of its twenty fields was meant.
 *
 * The numbers are this front end's own 9000 series so that they can
 * never collide with a message from NASTRAN itself:
 *
 *   9000-9099  reading the deck (files, INCLUDE, continuations)
 *   9100-9199  executive and case control
 *   9200-9299  bulk data translation
 *   9300-9399  the model as a whole (constraints, ids, mass)
 *   9400-9499  design optimisation (SOL 200)
 *   9500-9599  results and the f06 shim
 */
#include "msc.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int   n_fatal = 0;
static int   n_warn  = 0;
static int   n_info  = 0;
static FILE *logfp   = NULL;

static const char *sev_text(msc_sev s)
{
    switch (s) {
    case MSC_FATAL: return "USER FATAL MESSAGE";
    case MSC_WARN:  return "USER WARNING MESSAGE";
    default:        return "USER INFORMATION MESSAGE";
    }
}

void msc_msg_open(const char *path)
{
    logfp = fopen(path, "w");
}

void msc_msg_close(void)
{
    if (logfp) { fclose(logfp); logfp = NULL; }
}

/* Wrap the body at 72 columns and indent it under the banner, the way
 * NASTRAN lays its own messages out. A newline in the text is a hard
 * break, so a message that has already laid itself out comes through
 * unchanged; anything longer folds on a space. A single word wider
 * than the margin - an element name, a file path - is left whole and
 * allowed to run past it, being easier to read that way than folded. */
#define MSC_WRAP 72

static void emit(msc_sev sev, int num, const char *body)
{
    char        line[MSC_LINELEN];
    const char *p = body;
    FILE       *out[2];
    int         k, nout = 1;

    out[0] = stderr;
    if (logfp) { out[1] = logfp; nout = 2; }

    for (k = 0; k < nout; k++) {
        fprintf(out[k], "*** %s %d (MSCXLAT)\n", sev_text(sev), num);
    }
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t      n  = nl ? (size_t) (nl - p) : strlen(p);
        const char *q  = p;

        if (n == 0)                       /* a blank line stays blank */
            for (k = 0; k < nout; k++) fprintf(out[k], "\n");

        while (n > 0) {
            size_t take = n, w;
            if (take > MSC_WRAP) {
                take = MSC_WRAP;
                while (take > 0 && q[take] != ' ') take--;
                if (take == 0) {          /* one long word: keep it whole */
                    take = MSC_WRAP;
                    while (take < n && q[take] != ' ') take++;
                }
            }
            w = take;
            while (w > 0 && q[w-1] == ' ') w--;
            if (w >= sizeof(line)) w = sizeof(line) - 1;
            memcpy(line, q, w);
            line[w] = '\0';
            for (k = 0; k < nout; k++) fprintf(out[k], "    %s\n", line);
            q += take; n -= take;
            while (n > 0 && *q == ' ') { q++; n--; }
        }
        if (!nl) break;
        p = nl + 1;
    }
    for (k = 0; k < nout; k++) fflush(out[k]);
}

void msc_msg(msc_sev sev, int num, const char *fmt, ...)
{
    char    body[MSC_LINELEN * 2];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (sev == MSC_FATAL) n_fatal++;
    else if (sev == MSC_WARN) n_warn++;
    else n_info++;
    emit(sev, num, body);
}

void msc_msg_at(msc_sev sev, int num, const msc_card *c, const char *fmt, ...)
{
    char    body[MSC_LINELEN * 2];
    char    where[MSC_LINELEN];
    va_list ap;
    size_t  n;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    if (c) {
        snprintf(where, sizeof(where), "\nCARD  %s %s, %s line %d",
                 c->name, msc_f(c, 1),
                 c->file ? c->file : "(deck)", c->line);
        n = strlen(body);
        if (n + strlen(where) < sizeof(body)) strcat(body, where);
    }
    if (sev == MSC_FATAL) n_fatal++;
    else if (sev == MSC_WARN) n_warn++;
    else n_info++;
    emit(sev, num, body);
}

int msc_nfatal(void) { return n_fatal; }
int msc_nwarn(void)  { return n_warn;  }

void msc_msg_summary(void)
{
    FILE *out[2];
    int   k, nout = 1;
    out[0] = stderr;
    if (logfp) { out[1] = logfp; nout = 2; }
    for (k = 0; k < nout; k++) {
        fprintf(out[k],
                "*** MSC DIALECT TRANSLATION: %d fatal, %d warning, "
                "%d information message%s\n",
                n_fatal, n_warn, n_info, (n_info == 1) ? "" : "s");
    }
}

/* ------------------------------------------------------------------ */
/* A tally, so that a deck with three thousand identical decisions in
 * it says so once with a count rather than three thousand times. What
 * was done to every card name is printed in one table at the end; the
 * numbered messages above it are for the decisions that are not
 * routine.                                                            */

#define MSC_TALLY 128

typedef struct {
    char kind[24];
    char name[40];
    long n;
} tally_row;

static tally_row tally[MSC_TALLY];
static int       ntally = 0;

void msc_tally(const char *kind, const char *name)
{
    int i;
    for (i = 0; i < ntally; i++) {
        if (strcmp(tally[i].kind, kind) == 0 &&
            strcmp(tally[i].name, name) == 0) { tally[i].n++; return; }
    }
    if (ntally >= MSC_TALLY) return;
    strncpy(tally[ntally].kind, kind, sizeof(tally[0].kind) - 1);
    strncpy(tally[ntally].name, name, sizeof(tally[0].name) - 1);
    tally[ntally].n = 1;
    ntally++;
}

void msc_tally_print(void)
{
    FILE *out[2];
    int   k, nout = 1, i;
    if (!ntally) return;
    out[0] = stderr;
    if (logfp) { out[1] = logfp; nout = 2; }
    for (k = 0; k < nout; k++) {
        fprintf(out[k], "\n    WHAT BECAME OF EACH CARD\n");
        fprintf(out[k], "      %-32s %-22s %6s\n", "CARD", "WHAT BECAME OF IT", "COUNT");
        for (i = 0; i < ntally; i++)
            fprintf(out[k], "      %-32s %-22s %6ld\n",
                    tally[i].name, tally[i].kind, tally[i].n);
        fprintf(out[k], "\n");
    }
}
