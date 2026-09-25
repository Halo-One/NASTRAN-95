/* HALO: OUTPUT4 -- matrices out of the solver, in the file MSC writes.
 *
 * The ASE chain in this repository does not stop at the print file. The
 * vibe decks carry
 *
 *   assign output4='monarch_ff.phg', status=unknown unit=101 form=formatted
 *   assign output4='monarch_ff.mgg', status=unknown unit=102 form=formatted
 *   SOL 103
 *   COMPILE SEMODES SOUIN=MSCSOU NOLIST $
 *   ALTER 'STRAIN ENERGY'
 *      OUTPUT4 PHG//-1/101/2 $
 *      OUTPUT4 MGG//-1/102/2 $
 *   ENDALTER
 *
 * and ZAERO reads the two files back as the mode shapes and the mass
 * matrix. Three things have to line up for the same deck to do that here:
 *
 *   1  MSC alters its solution sequence by label; NASTRAN-95 alters a
 *      rigid format by DMAP statement number. In rigid format 3 the mode
 *      shapes in the g-set (PHIG, which MSC calls PHG) exist from
 *      statement 77 (SDR1) and the mass matrix MGG from statement 33, so
 *      the alter is written after 77. The number is that of the April
 *      1995 rigid format compiled into this executable; a DIAG 14 run
 *      prints the numbered listing if it is ever in doubt.
 *   2  NASTRAN-95's OUTPUT4 writes to FORTRAN units 11 to 24, named by
 *      FTN11..FTN24 in the environment. MSC's units 101, 102 are mapped
 *      to 11, 12, ... in order of appearance.
 *   3  Both codes write "formatted" OUTPUT4, and the formats differ:
 *      NASTRAN-95 writes 4I13 headers and 8D16.9 data; MSC (and the
 *      reader in utilities/jhc_library/OUTPUT4_rd.m, and ZAERO) expect
 *      4I8 headers, an A8 name, the format string, 3I8 column headers
 *      and 1P,5E16.9 data. So the file is rewritten on the way out.
 *
 * Only rigid format 3 is wired, because that is what the repository
 * asks for; another rigid format's statement numbers would be looked
 * up the same way.
 */
#include "msc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <stdlib.h>
#endif

#define OP4_MAX 8

typedef struct {
    int  unit;               /* MSC unit number (101 ...)               */
    char file[MSC_PATHLEN];  /* the name the ASSIGN gave                */
    char db[16];             /* MSC data block name (PHG)               */
    char cosmic[16];         /* its 1970s name (PHIG)                   */
    int  n95unit;            /* 11 ..                                   */
    char temp[64];           /* what NASTRAN-95 writes                  */
} op4_req;

static op4_req reqs[OP4_MAX];
static int     nreq = 0;

/* MSC data block -> COSMIC data block, for the ones a modes run has */
static const struct { const char *msc, *cosmic; } dbmap[] = {
    { "PHG",  "PHIG" }, { "PHIG", "PHIG" },
    { "MGG",  "MGG"  }, { "KGG",  "KGG"  },
    { "PHA",  "PHIA" }, { "PHIA", "PHIA" },
    { "MAA",  "MAA"  }, { "KAA",  "KAA"  },
    { NULL, NULL }
};

static void upper_copy(char *dst, const char *src, size_t n)
{
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
    msc_upper(dst);
}

/* the file name inside quotes on an ASSIGN, and unit= on the same
 * logical statement                                                   */
static int parse_assign(const char *line, char *file, int *unit)
{
    const char *q1 = strchr(line, '\''), *q2;
    char        up[MSC_LINELEN];
    const char *u;
    if (!q1) return 0;
    q2 = strchr(q1 + 1, '\'');
    if (!q2) return 0;
    if ((size_t) (q2 - q1 - 1) >= MSC_PATHLEN) return 0;
    memcpy(file, q1 + 1, (size_t) (q2 - q1 - 1));
    file[q2 - q1 - 1] = '\0';
    upper_copy(up, line, sizeof(up));
    u = strstr(up, "UNIT");
    if (!u) return 0;
    u += 4;
    while (*u == ' ' || *u == '=') u++;
    *unit = atoi(u);
    return *unit > 0;
}

int msc_op4_scan(msc_deck *d)
{
    char joined[MSC_LINELEN * 2];
    int  i, k;

    nreq = 0;
    joined[0] = '\0';
    for (i = 0; i < d->nexec; i++) {
        char up[MSC_LINELEN];
        upper_copy(up, d->exec[i], sizeof(up));
        msc_trim(up);

        /* an ASSIGN may continue over lines; join until no trailing comma */
        if (strncmp(up, "ASSIGN", 6) == 0 || joined[0]) {
            size_t n = strlen(joined);
            if (n + strlen(d->exec[i]) + 2 < sizeof(joined)) {
                strcat(joined, " ");
                strcat(joined, d->exec[i]);
            }
            msc_trim(joined);
            if (joined[strlen(joined) - 1] == ',') continue;
            upper_copy(up, joined, sizeof(up));
            if (strstr(up, "OUTPUT4") && nreq < OP4_MAX) {
                op4_req *r = &reqs[nreq];
                memset(r, 0, sizeof(*r));
                if (parse_assign(joined, r->file, &r->unit)) nreq++;
                else msc_msg(MSC_WARN, 9120,
                        "this ASSIGN OUTPUT4 could not be read (a quoted file\n"
                        "name and UNIT=n are needed): %s", joined);
            } else if (strstr(up, "ASSIGN")) {
                msc_msg(MSC_INFO, 9121, "ASSIGN statement ignored (only OUTPUT4 "
                        "is used here): %s", joined);
            }
            joined[0] = '\0';
            continue;
        }

        /* OUTPUT4 PHG//-1/101/2 $ inside the ALTER: which data block
         * goes to which unit                                          */
        if (strncmp(up, "OUTPUT4", 7) == 0) {
            char *p = up + 7, *slash;
            char  db[16];
            int   unit = 0, n = 0;
            while (*p == ' ') p++;
            while (*p && *p != ',' && *p != '/' && *p != ' ' && n < 15) db[n++] = *p++;
            db[n] = '\0';
            slash = strstr(p, "//");
            if (slash) {
                /* //P1/P2/P3: P2 is the unit */
                char *s2 = strchr(slash + 2, '/');
                if (s2) unit = atoi(s2 + 1);
            }
            for (k = 0; k < nreq; k++) {
                if (reqs[k].unit == unit) {
                    int m;
                    strncpy(reqs[k].db, db, sizeof(reqs[k].db) - 1);
                    for (m = 0; dbmap[m].msc; m++)
                        if (msc_streq(db, dbmap[m].msc)) {
                            strcpy(reqs[k].cosmic, dbmap[m].cosmic);
                            break;
                        }
                    if (!reqs[k].cosmic[0])
                        msc_msg(MSC_FATAL, 9122,
                            "OUTPUT4 of %s: this front end does not know what\n"
                            "NASTRAN-95 calls that data block. It knows PHG (the\n"
                            "g-set mode shapes), MGG, KGG, PHA, MAA and KAA.\n"
                            "FIX   Ask for one of those, or add the name to mscop4.c.",
                            db);
                }
            }
        }
    }

    /* units: 11, 12, ... in order; names the solver reads from FTNnn */
    for (k = 0; k < nreq; k++) {
        char env[128];
        if (!reqs[k].cosmic[0]) {
            msc_msg(MSC_WARN, 9123,
                "ASSIGN OUTPUT4 unit %d (%s) is never written to by an OUTPUT4\n"
                "statement in the alter; no file is produced for it.",
                reqs[k].unit, reqs[k].file);
            continue;
        }
        reqs[k].n95unit = 11 + k;
        snprintf(reqs[k].temp, sizeof(reqs[k].temp), "op4_unit%d.tmp", reqs[k].n95unit);
#ifdef _WIN32
        snprintf(env, sizeof(env), "FTN%d=%s", reqs[k].n95unit, reqs[k].temp);
        _putenv(env);
#else
        /* HALO: setenv, not putenv. POSIX putenv keeps the caller's
         *   string rather than copying it, and this one is a local
         *   buffer that is gone long before the solver reads FTNnn. */
        snprintf(env, sizeof(env), "FTN%d", reqs[k].n95unit);
        setenv(env, reqs[k].temp, 1);
#endif
        msc_msg(MSC_INFO, 9124,
            "OUTPUT4 %s -> unit %d -> %s: written by NASTRAN-95 as %s (unit %d)\n"
            "and rewritten into MSC's formatted OUTPUT4 layout on the way out.",
            reqs[k].db, reqs[k].unit, reqs[k].file, reqs[k].cosmic, reqs[k].n95unit);
    }
    return msc_nfatal() ? 1 : 0;
}

int msc_op4_count(void) { return nreq; }

/* the alter, into the executive control */
void msc_op4_alter(FILE *fp, int rf)
{
    int k, any = 0;
    for (k = 0; k < nreq; k++) if (reqs[k].cosmic[0]) any = 1;
    if (!any) return;
    if (rf != 3) {
        msc_msg(MSC_WARN, 9125,
            "OUTPUT4 is wired for rigid format 3 (SOL 103) only; this deck's\n"
            "rigid format %d gets no matrix output.", rf);
        return;
    }
    /* after SDR1, statement 77 of DISP3: PHIG and MGG both exist */
    fprintf(fp, "ALTER   77 $\n");
    for (k = 0; k < nreq; k++)
        if (reqs[k].cosmic[0])
            fprintf(fp, "OUTPUT4 %s,,,,//-1/%d/2 $\n", reqs[k].cosmic, reqs[k].n95unit);
    fprintf(fp, "ENDALTER $\n");
}

/* ------------------------------------------------------------------ */
/* NASTRAN-95's formatted OUTPUT4 into MSC's                           */

/* NASTRAN-95's formatted records are fixed-width Fortran output, not
 * whitespace-separated tokens: single precision is 1X,10E13.6 and double
 * 1X,8D16.9, and a negative number fills its field to the edge, so two
 * adjacent negatives touch ("-1.111420E-01-2.222220E-02"). They are
 * read by slicing the line at the field width.                        */
static int read_values(FILE *fp, double *v, int n, int width)
{
    char line[512];
    int  got = 0;
    while (got < n) {
        size_t len, pos;
        if (!fgets(line, sizeof(line), fp)) return got;
        len = strlen(line);
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        for (pos = 1; pos + 1 <= len && got < n; pos += (size_t) width) {
            char tok[32], *d;
            size_t w = (len - pos < (size_t) width) ? len - pos : (size_t) width;
            memcpy(tok, line + pos, w);
            tok[w] = '\0';
            for (d = tok; *d; d++) if (*d == 'D' || *d == 'd') *d = 'E';
            if (msc_isblank_line(tok)) continue;
            v[got++] = atof(tok);
        }
    }
    return got;
}

static int convert_one(const op4_req *r)
{
    FILE  *in, *out;
    char   line[512];
    long   nc, nr, form, type;
    char   name[16];
    int    k, width;

    in = fopen(r->temp, "r");
    if (!in) {
        msc_msg(MSC_WARN, 9126,
            "NASTRAN-95 wrote no %s for OUTPUT4 %s; the run probably stopped\n"
            "before statement 77 of the rigid format.", r->temp, r->db);
        return 1;
    }
    /* header: 1X,4I13,5X,2A4 */
    if (!fgets(line, sizeof(line), in)) { fclose(in); return 1; }
    if (sscanf(line, "%ld %ld %ld %ld %15s", &nc, &nr, &form, &type, name) < 4) {
        fclose(in);
        msc_msg(MSC_WARN, 9127, "%s: not an OUTPUT4 file this reader understands",
                r->temp);
        return 1;
    }
    /* NASTRAN-95 flags a symmetric matrix with a negative form; MSC's
     * reader wants 1 (square), 2 (rectangular) or 6 (symmetric)      */
    form = labs(form);
    if (form != 1 && form != 2 && form != 6) form = 2;
    if (type != 1 && type != 2) {
        fclose(in);
        msc_msg(MSC_WARN, 9128, "%s: %s is complex (type %ld); only real "
                "matrices are rewritten.", r->temp, r->db, type);
        return 1;
    }
    /* The mode shapes: FEER finds and keeps more roots than the deck
     * asked for (see mscf06.c), and PHIG has one column per root kept.
     * MSC's PHG has one per mode requested, and ZAERO pairs the columns
     * with the modes in the print file, so the file is cut to the same
     * count the print file was.                                       */
    {
        long want = msc_f06_get_modes();
        if (want > 0 && want < nc &&
            (msc_streq(r->cosmic, "PHIG") || msc_streq(r->cosmic, "PHIA"))) {
            msc_msg(MSC_INFO, 9131, "%s: %ld modes kept of the %ld the "
                    "eigensolver returned, to match the print file.",
                    r->file, want, nc);
            nc = want;
        }
    }
    out = fopen(r->file, "w");
    if (!out) {
        fclose(in);
        msc_msg(MSC_WARN, 9129, "cannot write %s", r->file);
        return 1;
    }
    /* MSC: NCOL NROW FORM TYPE (4I8) NAME (A8) then the format          */
    fprintf(out, "%8ld%8ld%8ld%8ld%-8s1P,5E16.9\n", nc, nr, form, 2L, r->db);

    /* the value field width, from outpt4.f's own formats: single
     * precision 1X,10E13.6 and double 1X,8D16.9                      */
    width = (type == 2) ? 16 : 13;

    for (;;) {
        double *vals;
        long    ic, ir, nw;
        /* K II JJ, a record of its own: 1X,3I13 single, 1X,3I16 double */
        if (!fgets(line, sizeof(line), in)) break;
        if (sscanf(line, "%ld %ld %ld", &ic, &ir, &nw) != 3) break;
        if (ic > nc) {
            /* columns past the cut still have to be read past */
            /* the trailer column: MSC writes it as NCOL+1 1 1 and one 1.0 */
            fprintf(out, "%8ld%8d%8d\n%16.9E\n", nc + 1, 1, 1, 1.0);
            break;
        }
        if (nw < 0) nw = 0;
        /* A null column: outpt4.f zeroes II before UNPACK and UNPACK's
         * alternate return leaves it there, so II = 0 means "no terms
         * in this column". The record still carries JJ words, but they
         * are the previous column's, left in the unpack buffer -- read
         * past them and write nothing. MSC's own OUTPUT4 leaves null
         * columns out of the file the same way (on the monarch_ff MGG
         * that is 2611 of 7512 columns: without this the mass matrix
         * grows a spurious row-1 entry per empty degree of freedom). */
        if (ir == 0) {
            double *skip;
            long    ns = (type == 2) ? nw / 2 : nw;
            if (ns > 0) {
                skip = (double *) msc_alloc((size_t) ns * sizeof(double));
                if (read_values(in, skip, (int) ns, width) != ns) {
                    free(skip);
                    break;
                }
                free(skip);
            }
            continue;
        }
        /* NASTRAN-95's third word is the length in single-precision
         * WORDS (outpt4.f: "NW is based on S.P. word count"), so a
         * double-precision column announces twice the values it holds;
         * MSC's is the number of values                               */
        if (type == 2) nw /= 2;
        vals = (double *) msc_alloc((size_t) (nw > 0 ? nw : 1) * sizeof(double));
        if (read_values(in, vals, (int) nw, width) != nw) {
            free(vals);
            msc_msg(MSC_WARN, 9132,
                "%s: %s ends inside column %ld; the matrix is written short.",
                r->file, r->db, ic);
            break;
        }
        fprintf(out, "%8ld%8ld%8ld\n", ic, ir, nw);
        for (k = 0; k < nw; k++) {
            fprintf(out, "%16.9E", vals[k]);
            if ((k + 1) % 5 == 0 || k + 1 == nw) fprintf(out, "\n");
        }
        free(vals);
    }
    fclose(in);
    fclose(out);
    if (!getenv("N95_KEEP_OP4")) remove(r->temp);
    msc_msg(MSC_INFO, 9130, "%s: %s, %ld columns by %ld rows, in MSC's "
            "formatted OUTPUT4 layout", r->file, r->db, nc, nr);
    return 0;
}

int msc_op4_finish(void)
{
    int k, rc = 0;
    for (k = 0; k < nreq; k++)
        if (reqs[k].cosmic[0] && convert_one(&reqs[k])) rc = 1;
    return rc;
}
