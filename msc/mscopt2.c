/* HALO: SOL 200 -- the design cycle itself.
 *
 * mscopt.c reads the design model and applies a design to the analysis
 * deck. This file runs the loop described at the top of that one:
 * analyse, evaluate, screen, differentiate, approximate, optimise, test.
 *
 * The analyses are child processes of this executable, started with
 * --cosmic so that they read the translated deck as it is and do not
 * translate again. Each cycle's print file is kept beside the design
 * print file (<stem>_c<cycle>.out for the analysis, _c<cycle>_d<k>.out
 * for the perturbation of design variable k) so that anything in the
 * history can be checked against the solver's own output.
 *
 * What the driver reads back from a print file, subcase by subcase:
 *   REAL EIGENVALUES         the FREQ and EIGN responses
 *   DISPLACEMENT VECTOR      the DISP responses
 *   STRESSES IN ROD ELEMENTS the STRESS responses on CROD/CONROD
 *   STRESSES IN BAR ELEMENTS the STRESS responses on CBAR
 * WEIGHT and VOLUME are computed from the model here, in closed form,
 * from the same cards the solver reads (rod and bar areas, densities,
 * lengths, shell thicknesses, concentrated masses), so their
 * sensitivities are exact and cost no analysis.
 *
 * A DRESP1 is one row in the deck and, as in MSC, as many responses as
 * it expands to: one per subcase, per grid or element, per component
 * listed in ATTA. Every one of them is a constraint of its own.
 */
#include "msc.h"
#include "mscopt.h"
#include <ctype.h>
#include <math.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* from mscopt.c */
int  opt_read_design_model(msc_deck *d, opt_model *m);
int  opt_read_case(msc_deck *d, opt_model *m);
int  opt_apply_design(msc_deck *d, opt_model *m);
void opt_record_p0(msc_deck *d, opt_model *m);

/* results are keyed by subcase and id together */
#define SUBKEY(sub, id) ((sub) * 20000000 + (id))
#define OPT_MAXSUB 64

/* ------------------------------------------------------------------ */
/* the analysis results of one solver run                              */

typedef struct {
    msc_map  freq;       /* mode -> dv[0] = Hz, dv[1] = eigenvalue      */
    msc_map  disp;       /* SUBKEY(sub,grid) -> dv[0..5]                */
    msc_map  rodstr;     /* SUBKEY(sub,eid)  -> dv[0] axial, dv[1] torsion */
    msc_map  barstr;     /* SUBKEY(sub,eid)  -> dv[0..6] SA1..4 AX MAX MIN */
    int      subs[OPT_MAXSUB];
    int      nsub;
    int      ok;
} opt_results;

static double dnum(const char *s)
{
    char b[48];
    int  i, o = 0;
    for (i = 0; s[i] && o < 46; i++) b[o++] = (s[i] == 'D') ? 'E' : s[i];
    b[o] = '\0';
    return atof(b);
}

/* split a print line into up to 16 tokens */
static int toks(const char *line, char t[16][32])
{
    int n = 0, k = 0;
    const char *p = line;
    while (*p && n < 16) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        k = 0;
        while (*p && *p != ' ' && *p != '\n' && *p != '\r' && k < 31) t[n][k++] = *p++;
        t[n][k] = '\0';
        n++;
    }
    return n;
}

static int is_number(const char *s)
{
    char b[48]; int k, o = 0;
    for (k = 0; s[k] && o < 46; k++) b[o++] = (s[k] == 'D') ? 'E' : s[k];
    b[o] = '\0';
    return msc_isnum(b);
}

static int is_integer(const char *s)
{
    if (*s == '-' || *s == '+') s++;
    if (!*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char) *s)) return 0;
    return 1;
}

static int all_numeric(char t[16][32], int from, int to)
{
    int i;
    for (i = from; i < to; i++) if (!is_number(t[i])) return 0;
    return 1;
}

static void results_init(opt_results *r)
{
    memset(r, 0, sizeof(*r));
    msc_map_init(&r->freq, 64);
    msc_map_init(&r->disp, 1024);
    msc_map_init(&r->rodstr, 256);
    msc_map_init(&r->barstr, 256);
}

static void results_free(opt_results *r)
{
    msc_map_free(&r->freq);
    msc_map_free(&r->disp);
    msc_map_free(&r->rodstr);
    msc_map_free(&r->barstr);
}

static void note_sub(opt_results *r, int sub)
{
    int i;
    for (i = 0; i < r->nsub; i++) if (r->subs[i] == sub) return;
    if (r->nsub < OPT_MAXSUB) r->subs[r->nsub++] = sub;
}

static int read_results(const char *prt, opt_results *r)
{
    FILE *fp = fopen(prt, "r");
    char  line[512];
    int   table = 0;      /* 1 eigen, 2 disp, 3 rod, 4 bar               */
    int   ended = 0, sub = 1;
    if (!fp) return 1;
    while (fgets(line, sizeof(line), fp)) {
        char t[16][32];
        int  n;
        if (strstr(line, "END OF JOB")) ended = 1;
        if (line[0] == '1') { table = 0; continue; }
        /* the second line of a page heading carries "SUBCASE n" at the
         * right; it is the only place the subcase of a table is said   */
        if (line[0] == '0') {
            const char *p = strstr(line, "SUBCASE");
            if (p && isdigit((unsigned char) *(p + 8))) sub = atoi(p + 8);
            continue;
        }
        if (strstr(line, "R E A L   E I G E N V A L U E S"))            { table = 1; continue; }
        if (strstr(line, "D I S P L A C E M E N T   V E C T O R"))     { table = 2; continue; }
        if (strstr(line, "S T R E S S E S   I N   R O D   E L E M E N T S")) { table = 3; continue; }
        if (strstr(line, "S T R E S S E S   I N   B A R   E L E M E N T S")) { table = 4; continue; }
        if (strstr(line, "*** ")) { table = 0; continue; }
        if (!table) continue;
        n = toks(line, t);
        if (table == 1 && n == 7 && all_numeric(t, 0, 7)) {
            double *v = msc_map_vec(&r->freq, atoi(t[0]), 1);
            v[0] = dnum(t[4]);      /* cycles */
            v[1] = dnum(t[2]);      /* eigenvalue */
        } else if (table == 2 && n == 8 && msc_streq(t[1], "G") && all_numeric(t, 2, 8)) {
            double *v = msc_map_vec(&r->disp, SUBKEY(sub, atoi(t[0])), 1);
            int k;
            for (k = 0; k < 6; k++) v[k] = dnum(t[2 + k]);
            note_sub(r, sub);
        } else if (table == 3 && n >= 2 && is_integer(t[0]) && all_numeric(t, 0, n)) {
            /* two rods a line: ID AXIAL [MS] TORSION [MS] ID ... The
             * margins are absent without an allowable, so the reals
             * between one id and the next are taken in order          */
            int k = 0;
            while (k < n && is_integer(t[k])) {
                double *v = msc_map_vec(&r->rodstr, SUBKEY(sub, atoi(t[k])), 1);
                int j = k + 1, got = 0;
                while (j < n && !is_integer(t[j])) {
                    if (got == 0) v[0] = dnum(t[j]);
                    else if (got == 1) v[1] = dnum(t[j]);
                    got++; j++;
                }
                note_sub(r, sub);
                k = j;
            }
        } else if (table == 4 && n >= 8 && is_integer(t[0]) && all_numeric(t, 0, n)) {
            /* end A line: ID SA1 SA2 SA3 SA4 AXIAL SA-MAX SA-MIN [MS-T] */
            double *v = msc_map_vec(&r->barstr, SUBKEY(sub, atoi(t[0])), 1);
            int k;
            for (k = 0; k < 7 && 1 + k < n; k++) v[k] = dnum(t[1 + k]);
            note_sub(r, sub);
        }
    }
    fclose(fp);
    if (r->nsub == 0) note_sub(r, 1);
    r->ok = ended;
    return ended ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* weight and volume from the model                                    */

typedef struct { double m, vol; } wv;

static wv model_weight(msc_deck *d)
{
    wv      out = { 0.0, 0.0 };
    msc_map grid, mat, prod, pbar, pshell;
    int     i;
    double  wtmass = 1.0;

    msc_map_init(&grid, 1024); msc_map_init(&mat, 32);
    msc_map_init(&prod, 128);  msc_map_init(&pbar, 128); msc_map_init(&pshell, 32);
    for (i = 0; i < d->nbulk; i++) {
        msc_card *c = &d->bulk[i];
        if (c->dropped) continue;
        if (msc_streq(c->name, "GRID")) {
            double *v = msc_map_vec(&grid, msc_fi(c, 1, 0), 1);
            v[0] = msc_fd(c, 3, 0); v[1] = msc_fd(c, 4, 0); v[2] = msc_fd(c, 5, 0);
        } else if (msc_streq(c->name, "MAT1")) {
            msc_map_vec(&mat, msc_fi(c, 1, 0), 1)[0] = msc_fd(c, 5, 0.0);   /* RHO */
        } else if (msc_streq(c->name, "PROD")) {
            double *v = msc_map_vec(&prod, msc_fi(c, 1, 0), 1);
            v[0] = msc_fi(c, 2, 0); v[1] = msc_fd(c, 3, 0); v[2] = msc_fd(c, 6, 0);   /* MID A NSM */
        } else if (msc_streq(c->name, "PBAR")) {
            double *v = msc_map_vec(&pbar, msc_fi(c, 1, 0), 1);
            v[0] = msc_fi(c, 2, 0); v[1] = msc_fd(c, 3, 0); v[2] = msc_fd(c, 7, 0);   /* MID A NSM */
        } else if (msc_streq(c->name, "PSHELL")) {
            double *v = msc_map_vec(&pshell, msc_fi(c, 1, 0), 1);
            v[0] = msc_fi(c, 2, 0); v[1] = msc_fd(c, 3, 0); v[2] = msc_fd(c, 8, 0);   /* MID T NSM */
        } else if (msc_streq(c->name, "PARAM")) {
            char n[16]; strncpy(n, msc_f(c, 1), 15); n[15] = '\0'; msc_upper(n);
            if (msc_streq(n, "WTMASS")) wtmass = msc_fd(c, 2, 1.0);
        }
    }
    for (i = 0; i < d->nbulk; i++) {
        msc_card *c = &d->bulk[i];
        double   *a, *b, L, rho = 0.0, A = 0.0, nsm = 0.0;
        if (c->dropped) continue;
        if (msc_streq(c->name, "CROD") || msc_streq(c->name, "CBAR") ||
            msc_streq(c->name, "CBEAM") || msc_streq(c->name, "CONROD")) {
            int ga, gb;
            double *p = NULL;
            if (msc_streq(c->name, "CONROD")) {
                ga = msc_fi(c, 2, 0); gb = msc_fi(c, 3, 0);
                A = msc_fd(c, 5, 0); nsm = msc_fd(c, 8, 0);
                { double *mm = msc_map_vec(&mat, msc_fi(c, 4, 0), 0); rho = mm ? mm[0] : 0.0; }
            } else {
                ga = msc_fi(c, 3, 0); gb = msc_fi(c, 4, 0);
                p = msc_map_vec(msc_streq(c->name, "CROD") ? &prod : &pbar, msc_fi(c, 2, 0), 0);
                if (p) {
                    double *mm = msc_map_vec(&mat, (int) p[0], 0);
                    A = p[1]; nsm = p[2]; rho = mm ? mm[0] : 0.0;
                }
            }
            a = msc_map_vec(&grid, ga, 0); b = msc_map_vec(&grid, gb, 0);
            if (!a || !b) continue;
            L = sqrt((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2]));
            out.m   += (A * rho + nsm) * L;
            out.vol += A * L;
        } else if (msc_streq(c->name, "CQUAD4") || msc_streq(c->name, "CTRIA3")) {
            int nn = msc_streq(c->name, "CQUAD4") ? 4 : 3, k;
            double *g[4], area = 0.0;
            double *p = msc_map_vec(&pshell, msc_fi(c, 2, 0), 0);
            for (k = 0; k < nn; k++) { g[k] = msc_map_vec(&grid, msc_fi(c, 3 + k, 0), 0); if (!g[k]) break; }
            if (k < nn || !p) continue;
            for (k = 1; k + 1 < nn; k++) {
                double u[3], v[3], w[3];
                int j;
                for (j = 0; j < 3; j++) { u[j] = g[k][j] - g[0][j]; v[j] = g[k+1][j] - g[0][j]; }
                w[0] = u[1]*v[2] - u[2]*v[1]; w[1] = u[2]*v[0] - u[0]*v[2]; w[2] = u[0]*v[1] - u[1]*v[0];
                area += 0.5 * sqrt(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);
            }
            { double *mm = msc_map_vec(&mat, (int) p[0], 0); rho = mm ? mm[0] : 0.0; }
            out.m   += (p[1] * rho + p[2]) * area;
            out.vol += p[1] * area;
        } else if (msc_streq(c->name, "CONM2")) {
            out.m += msc_fd(c, 4, 0.0);
        } else if (msc_streq(c->name, "CMASS2")) {
            out.m += msc_fd(c, 2, 0.0);
        }
    }
    out.m *= wtmass;
    msc_map_free(&grid); msc_map_free(&mat); msc_map_free(&prod);
    msc_map_free(&pbar); msc_map_free(&pshell);
    return out;
}

/* ------------------------------------------------------------------ */
/* one analysis: write the deck for the current design, run it         */

typedef struct {
    const char *exe, *outdir, *stem;
    int  nruns;
} opt_runner;

static int run_analysis(opt_runner *R, msc_deck *d, const char *tag,
                        opt_results *res, msc_stats *st)
{
    char deck[MSC_PATHLEN], prt[MSC_PATHLEN];
    int  rc;
    snprintf(deck, sizeof(deck), "%s_%s.dat", R->stem, tag);
    snprintf(prt,  sizeof(prt),  "%s_%s.out", R->stem, tag);
    if (msc_translate_deck(d, deck, st)) return 1;
    R->nruns++;
    {
        char q1[MSC_PATHLEN + 4], q2[MSC_PATHLEN + 4];
        snprintf(q1, sizeof(q1), "\"%s\"", deck);
        snprintf(q2, sizeof(q2), "\"%s\"", R->outdir);
        rc = (int) _spawnl(_P_WAIT, R->exe, "nastran95ase", "--cosmic", q1, q2, NULL);
    }
    if (rc < 0) {
        msc_msg(MSC_FATAL, 9410, "could not start the analysis %s: is %s "
                "runnable?", deck, R->exe);
        return 1;
    }
    results_init(res);
    if (read_results(prt, res)) {
        msc_msg(MSC_FATAL, 9411,
            "the analysis %s did not reach END OF JOB (exit code %d). Its print\n"
            "file %s says why; the design cycle cannot continue without it.",
            deck, rc, prt);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* responses                                                           */

/* The element ids a STRESS response refers to: PTYPE=ELEM gives them
 * directly; a property name gives property ids and every element on
 * them is a response.                                                 */
static void expand_elements(opt_model *m, msc_deck *d)
{
    int i, j, k;
    for (i = 0; i < m->nresp; i++) {
        opt_resp *r = &m->resp[i];
        int ids[OPT_MAXATT], n = 0;
        if (!msc_streq(r->rtype, "STRESS") && !msc_streq(r->rtype, "STRAIN")) continue;
        if (msc_streq(r->ptype, "ELEM") || !r->ptype[0]) continue;
        for (k = 0; k < r->natt; k++) {
            int pid = r->atti[k];
            for (j = 0; j < d->nbulk && n < OPT_MAXATT; j++) {
                msc_card *c = &d->bulk[j];
                if ((msc_streq(c->name, "CROD") || msc_streq(c->name, "CBAR") ||
                     msc_streq(c->name, "CQUAD4") || msc_streq(c->name, "CTRIA3")) &&
                    msc_fi(c, 2, 0) == pid)
                    ids[n++] = msc_fi(c, 1, 0);
            }
        }
        if (n == 0) {
            msc_msg(MSC_FATAL, 9412, "DRESP1 %d (%s) names %s %d, and no element "
                    "uses that property.", r->id, r->label, r->ptype, r->atti[0]);
        }
        r->natt = n;
        memcpy(r->atti, ids, (size_t) n * sizeof(int));
        strcpy(r->ptype, "ELEM");
    }
}

/* the components ATTA lists: "12" is 1 and 2 */
static int comps_of(int atta, int *comp)
{
    char digits[16];
    int  k, n = 0;
    if (atta <= 0) { comp[0] = 1; return 1; }
    sprintf(digits, "%d", atta);
    for (k = 0; digits[k]; k++) {
        int c = digits[k] - '0';
        if (c >= 1 && c <= 6) comp[n++] = c;
    }
    return n;
}

/* one value of an expanded response, or ok = 0 */
static double one_value(opt_resp *r, opt_results *res, msc_deck *d,
                        int sub, int id, int comp, int *ok)
{
    *ok = 1;
    if (msc_streq(r->rtype, "WEIGHT") || msc_streq(r->rtype, "VOLUME")) {
        wv w = model_weight(d);
        return msc_streq(r->rtype, "WEIGHT") ? w.m : w.vol;
    }
    if (msc_streq(r->rtype, "FREQ") || msc_streq(r->rtype, "EIGN")) {
        double *v = res ? msc_map_vec(&res->freq, r->atta, 0) : NULL;
        if (!v) { *ok = 0; return 0.0; }
        return msc_streq(r->rtype, "FREQ") ? v[0] : v[1];
    }
    if (msc_streq(r->rtype, "DISP")) {
        double *v = res ? msc_map_vec(&res->disp, SUBKEY(sub, id), 0) : NULL;
        if (!v || comp < 1 || comp > 6) { *ok = 0; return 0.0; }
        return v[comp - 1];
    }
    if (msc_streq(r->rtype, "STRESS")) {
        double *v = res ? msc_map_vec(&res->rodstr, SUBKEY(sub, id), 0) : NULL;
        if (v) {
            if (r->atta == 2 || r->atta == 0) return v[0];     /* axial   */
            if (r->atta == 4) return v[1];                     /* torsion */
            *ok = 0; return 0.0;
        }
        v = res ? msc_map_vec(&res->barstr, SUBKEY(sub, id), 0) : NULL;
        if (v) {
            /* CBAR item codes: 2-5 SA1..SA4, 6 axial, 7 SA-max, 8 SA-min */
            if (r->atta >= 2 && r->atta <= 8) return v[r->atta - 2];
            *ok = 0; return 0.0;
        }
    }
    *ok = 0;
    return 0.0;
}

/* every value of every response, for the current results */
static int eval_responses(opt_model *m, opt_results *res, msc_deck *d)
{
    int i, k, s, c, ok;
    for (i = 0; i < m->nresp; i++) {
        opt_resp *r = &m->resp[i];
        int comp[6], ncomp, per_sub;
        r->nval = 0;
        if (msc_streq(r->rtype, "DISP") || msc_streq(r->rtype, "STRESS")) {
            ncomp = msc_streq(r->rtype, "DISP") ? comps_of(r->atta, comp) : 1;
            per_sub = m->analysis[0] && !msc_streq(m->analysis, "MODES");
            for (s = 0; s < (per_sub ? res->nsub : 1); s++) {
                for (k = 0; k < r->natt; k++) {
                    for (c = 0; c < ncomp; c++) {
                        int sub = per_sub ? res->subs[s] : 1;
                        if (r->nval >= OPT_MAXATT) {
                            msc_msg(MSC_FATAL, 9416, "DRESP1 %d expands to more than "
                                    "%d responses.", r->id, OPT_MAXATT);
                            return 1;
                        }
                        r->vals[r->nval] = one_value(r, res, d, sub, r->atti[k],
                                                     msc_streq(r->rtype, "DISP") ? comp[c] : 0, &ok);
                        r->vid[r->nval]  = r->atti[k];
                        r->vcomp[r->nval] = msc_streq(r->rtype, "DISP") ? comp[c] : r->atta;
                        r->vsub[r->nval] = sub;
                        if (!ok) {
                            msc_msg(MSC_FATAL, 9413,
                                "DRESP1 %d (%s): no %s value for id %d, component/item %d,\n"
                                "subcase %d in the analysis output. The grid or element may\n"
                                "not exist, the item code may not be one this driver reads,\n"
                                "or the case control does not ask for the output.",
                                r->id, r->label, r->rtype, r->atti[k], r->vcomp[r->nval], sub);
                            return 1;
                        }
                        r->nval++;
                    }
                }
            }
        } else {
            r->vals[0] = one_value(r, res, d, 1, 0, 0, &ok);
            r->vid[0] = r->atta; r->vcomp[0] = 0; r->vsub[0] = 0;
            r->nval = 1;
            if (!ok) {
                msc_msg(MSC_FATAL, 9413,
                    "DRESP1 %d (%s): no %s response could be read (mode %d?).",
                    r->id, r->label, r->rtype, r->atta);
                return 1;
            }
        }
        r->value = r->vals[0];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* constraints, normalised the way MSC normalises them                 */

typedef struct {
    int    ci;          /* which DCONSTR                                */
    int    ri;          /* which response                               */
    int    k;           /* which value of a vector response             */
    int    upper;       /* 1 upper bound, 0 lower                       */
    double g;           /* normalised value                             */
    double bound;
    double gnorm;
    int    keep;
} opt_gcon;

static double gnorm_of(double bound, double gscal)
{
    return fabs(bound) > gscal ? fabs(bound) : gscal;
}

static int build_constraints(opt_model *m, opt_gcon *gc, int maxg)
{
    int i, j, k, n = 0;
    for (i = 0; i < m->ncon; i++) {
        opt_con *c = &m->con[i];
        if (c->dcid != m->dessub && c->dcid != m->desglb) continue;
        for (j = 0; j < m->nresp; j++) {
            opt_resp *r = &m->resp[j];
            if (r->id != c->rid) continue;
            for (k = 0; k < r->nval; k++) {
                if (c->uallow < 1.0e19 && n < maxg) {
                    gc[n].ci = i; gc[n].ri = j; gc[n].k = k; gc[n].upper = 1;
                    gc[n].bound = c->uallow;
                    gc[n].gnorm = gnorm_of(c->uallow, m->prm.gscal);
                    gc[n].g = (r->vals[k] - c->uallow) / gc[n].gnorm;
                    n++;
                }
                if (c->lallow > -1.0e19 && n < maxg) {
                    gc[n].ci = i; gc[n].ri = j; gc[n].k = k; gc[n].upper = 0;
                    gc[n].bound = c->lallow;
                    gc[n].gnorm = gnorm_of(c->lallow, m->prm.gscal);
                    gc[n].g = (c->lallow - r->vals[k]) / gc[n].gnorm;
                    n++;
                }
            }
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* the convex approximation and its dual                               */

typedef struct {
    double x0, lo, hi;
    double gf;                 /* objective gradient                   */
} opt_var;

/* x minimising a x + b / x on [lo, hi] */
static double argmin_ab(double a, double b, double x0, double lo, double hi)
{
    double x;
    if (a > 0.0 && b > 0.0)      x = sqrt(b / a);
    else if (a > 0.0)            x = lo;
    else if (b > 0.0)            x = hi;
    else                         x = x0;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return x;
}

/* the approximate value of a function with gradient row grad at x */
static double approx(double f0, const double *grad, const opt_var *v,
                     const double *x, int n)
{
    double f = f0;
    int i;
    for (i = 0; i < n; i++) {
        double g = grad[i];
        if (g >= 0.0) f += g * (x[i] - v[i].x0);
        else          f += g * v[i].x0 * v[i].x0 * (1.0 / v[i].x0 - 1.0 / x[i]);
    }
    return f;
}

/* primal x for multipliers lam */
static void primal(const opt_var *v, int n, const double *gg, int ng,
                   const double *lam, double *x)
{
    int i, j;
    for (i = 0; i < n; i++) {
        double a = 0.0, b = 0.0, g;
        g = v[i].gf;
        if (g >= 0.0) a += g; else b += -g * v[i].x0 * v[i].x0;
        for (j = 0; j < ng; j++) {
            g = gg[j * n + i];
            if (g >= 0.0) a += lam[j] * g; else b += lam[j] * (-g) * v[i].x0 * v[i].x0;
        }
        x[i] = argmin_ab(a, b, v[i].x0, v[i].lo, v[i].hi);
    }
}

/* Maximise the dual by coordinate-wise bisection: for each multiplier
 * in turn, find the value at which its constraint's approximation is
 * exactly zero at the primal minimiser (or zero, if the constraint is
 * slack). The dual is concave, and on a sizing problem a few sweeps
 * settle it.                                                          */
static void solve_subproblem(const opt_var *v, int n, const double *g0,
                             const double *gg, int ng, double *x, double *lam)
{
    int sweep, j;
    for (j = 0; j < ng; j++) lam[j] = 0.0;
    for (sweep = 0; sweep < 60; sweep++) {
        double maxmove = 0.0;
        for (j = 0; j < ng; j++) {
            double lo = 0.0, hi = 1.0, old = lam[j], gval;
            int it;
            lam[j] = 0.0;
            primal(v, n, gg, ng, lam, x);
            gval = approx(g0[j], gg + j * n, v, x, n);
            if (gval <= 0.0) { if (fabs(old) > maxmove) maxmove = fabs(old); continue; }
            for (it = 0; it < 60; it++) {
                lam[j] = hi;
                primal(v, n, gg, ng, lam, x);
                if (approx(g0[j], gg + j * n, v, x, n) <= 0.0) break;
                lo = hi; hi *= 4.0;
            }
            for (it = 0; it < 50; it++) {
                double mid = 0.5 * (lo + hi);
                lam[j] = mid;
                primal(v, n, gg, ng, lam, x);
                if (approx(g0[j], gg + j * n, v, x, n) > 0.0) lo = mid; else hi = mid;
            }
            lam[j] = hi;
            if (fabs(lam[j] - old) > maxmove) maxmove = fabs(lam[j] - old);
        }
        if (maxmove < 1e-10) break;
    }
    primal(v, n, gg, ng, lam, x);
}

/* ------------------------------------------------------------------ */
/* the design cycle                                                    */

static void print_design(FILE *fp, opt_model *m, int cycle, double f,
                         opt_gcon *gc, int ng)
{
    int i;
    double gmax = -1.0e30;
    for (i = 0; i < ng; i++) if (gc[i].g > gmax) gmax = gc[i].g;
    fprintf(fp, "\n  DESIGN CYCLE %d\n", cycle);
    fprintf(fp, "    objective %s= %.8g    max constraint %.5f\n",
            m->objmax ? "(maximised) " : "", f, ng ? gmax : 0.0);
    fprintf(fp, "    %-8s %-10s %14s %14s %14s\n", "DESVAR", "LABEL", "VALUE", "LOWER", "UPPER");
    for (i = 0; i < m->ndv; i++)
        fprintf(fp, "    %-8d %-10s %14.7g %14.7g %14.7g%s\n", m->dv[i].id, m->dv[i].label,
                m->dv[i].x, m->dv[i].xlb, m->dv[i].xub, m->dv[i].linked ? "  (DLINK)" : "");
    if (ng) {
        fprintf(fp, "    %-8s %-10s %-7s %4s %9s %4s %14s %14s %10s\n", "DRESP1", "LABEL",
                "BOUND", "SUB", "ID", "COMP", "VALUE", "ALLOWABLE", "G");
        for (i = 0; i < ng; i++) {
            opt_resp *r = &m->resp[gc[i].ri];
            int k = gc[i].k;
            fprintf(fp, "    %-8d %-10s %-7s %4d %9d %4d %14.7g %14.7g %10.5f%s\n", r->id, r->label,
                    gc[i].upper ? "upper" : "lower", r->vsub[k], r->vid[k], r->vcomp[k],
                    r->vals[k], gc[i].bound, gc[i].g, gc[i].g > 0.0 ? "  VIOLATED" : "");
        }
    }
    fflush(fp);
}

static int is_closed_form(const opt_resp *r)
{
    return msc_streq(r->rtype, "WEIGHT") || msc_streq(r->rtype, "VOLUME");
}

int msc_opt_run(const char *deck, const char *outdir, const char *stem,
                const char *exepath)
{
    msc_deck   d;
    opt_model *m;
    opt_runner R;
    msc_stats  st;
    FILE      *rep = NULL;
    char       reppath[MSC_PATHLEN], finalpath[MSC_PATHLEN];
    opt_gcon  *gc;
    int        ng = 0, nfree = 0, i, j, cycle = 0, rc = 1;
    double     fobj = 0.0, fprev = 0.0;
    opt_results res;
    int        converged = 0;

    m  = (opt_model *) msc_alloc(sizeof(opt_model));
    gc = (opt_gcon *) msc_alloc(sizeof(opt_gcon) * (OPT_MAXCON * 4));

    if (msc_read(deck, &d)) return 1;
    if (opt_read_design_model(&d, m)) return 1;
    if (opt_read_case(&d, m)) return 1;
    if (m->ndv == 0) {
        msc_msg(MSC_FATAL, 9414, "SOL 200 with no DESVAR: nothing can change.");
        return 1;
    }
    opt_record_p0(&d, m);
    expand_elements(m, &d);
    if (msc_nfatal()) return 1;

    /* the analyses are ordinary solutions: the translator is told which
     * by the ANALYSIS command, since the deck itself says SOL 200      */
    if      (strncmp(m->analysis, "STATIC", 6) == 0) d.sol = 101;
    else if (strncmp(m->analysis, "MODES", 5) == 0)  d.sol = 103;
    else if (strncmp(m->analysis, "BUCK", 4) == 0)   d.sol = 105;
    else {
        msc_msg(MSC_FATAL, 9417,
            "ANALYSIS = %s: this driver runs STATICS, MODES and BUCK design "
            "cycles.\nThe dynamic and aeroelastic responses are not implemented.",
            m->analysis);
        return 1;
    }

    R.exe = exepath; R.outdir = outdir; R.stem = stem; R.nruns = 0;
    snprintf(reppath, sizeof(reppath), "%s.out", stem);
    rep = fopen(reppath, "w");
    if (!rep) return 1;
    fprintf(rep, "1  NASTRAN-95 (halo fork) SOL 200 DESIGN OPTIMISATION\n");
    fprintf(rep, "   deck %s\n   %d design variables, %d relations, %d responses, %d constraint rows, analysis %s\n",
            deck, m->ndv, m->nrel, m->nresp, m->ncon, m->analysis);
    fprintf(rep, "   sensitivities by forward finite difference, relative step DELB = %g\n",
            m->prm.delb);
    fprintf(rep, "   approximation: convex linearisation (CONLIN), solved through its dual\n");
    fprintf(rep, "   move limits DELX = %g (DXMIN %g); convergence CONV1 %g CONV2 %g GMAX %g; DESMAX %d\n",
            m->prm.delx, m->prm.dxmin, m->prm.conv1, m->prm.conv2, m->prm.gmax, m->prm.desmax);

    for (cycle = 0; cycle <= m->prm.desmax; cycle++) {
        char      tag[32];
        opt_var  *v;
        double   *gg, *g0, *xnew, *lam, *gradf;
        int       idx_free[OPT_MAXDV];
        int       k, ok = 1;
        opt_resp *robj = NULL;

        if (opt_apply_design(&d, m) < 0) goto done;
        snprintf(tag, sizeof(tag), "c%d", cycle);
        if (run_analysis(&R, &d, tag, &res, &st)) goto done;
        if (eval_responses(m, &res, &d)) { results_free(&res); goto done; }
        results_free(&res);
        for (i = 0; i < m->nresp; i++) if (m->resp[i].id == m->objid) robj = &m->resp[i];
        if (!robj) {
            msc_msg(MSC_FATAL, 9415, "DESOBJ = %d names no DRESP1.", m->objid);
            goto done;
        }
        fobj = robj->value;
        ng = build_constraints(m, gc, OPT_MAXCON * 4);
        print_design(rep, m, cycle, fobj, gc, ng);

        /* hard convergence: this analysis against the last one */
        if (cycle > 0) {
            double gmax = -1.0e30, chg = fabs(fobj - fprev);
            for (i = 0; i < ng; i++) if (gc[i].g > gmax) gmax = gc[i].g;
            if ((chg < m->prm.conv1 * fabs(fprev) || chg < m->prm.conv2) &&
                (ng == 0 || gmax < m->prm.gmax)) {
                fprintf(rep, "\n  HARD CONVERGENCE at cycle %d: objective changed by %.3g "
                        "(%.2e relative), max constraint %.5f\n", cycle, chg,
                        fabs(fprev) > 0 ? chg / fabs(fprev) : 0.0, ng ? gmax : 0.0);
                converged = 1;
                break;
            }
        }
        if (cycle == m->prm.desmax) {
            fprintf(rep, "\n  DESMAX = %d design cycles reached without convergence.\n",
                    m->prm.desmax);
            break;
        }
        fprev = fobj;

        /* screening: which constraints are worth a gradient */
        {
            int kept = 0;
            for (i = 0; i < ng; i++) {
                gc[i].keep = gc[i].g > m->prm.trs;
                if (gc[i].keep) kept++;
            }
            fprintf(rep, "    %d of %d constraints retained (TRS = %g)\n", kept, ng, m->prm.trs);
        }

        nfree = 0;
        for (i = 0; i < m->ndv; i++) if (!m->dv[i].linked) idx_free[nfree++] = i;
        v     = (opt_var *) msc_alloc(sizeof(opt_var) * (size_t) nfree);
        gradf = (double *) msc_alloc(sizeof(double) * (size_t) nfree);
        gg    = (double *) msc_alloc(sizeof(double) * (size_t) nfree * (size_t) (ng ? ng : 1));
        g0    = (double *) msc_alloc(sizeof(double) * (size_t) (ng ? ng : 1));
        xnew  = (double *) msc_alloc(sizeof(double) * (size_t) nfree);
        lam   = (double *) msc_alloc(sizeof(double) * (size_t) (ng ? ng : 1));

        /* sensitivities: forward differences, one analysis per variable
         * unless everything retained is closed-form                    */
        for (k = 0; k < nfree; k++) {
            opt_desvar *dv = &m->dv[idx_free[k]];
            double  x0 = dv->x, h, save_obj;
            double *save_vals;
            int     needs_run = !is_closed_form(robj);
            for (i = 0; i < ng; i++)
                if (gc[i].keep && !is_closed_form(&m->resp[gc[i].ri])) needs_run = 1;
            h = m->prm.delb * fabs(x0);
            if (h < 1e-12) h = m->prm.delb;
            save_vals = (double *) msc_alloc(sizeof(double) * (size_t) m->nresp * OPT_MAXATT);
            for (i = 0; i < m->nresp; i++)
                memcpy(save_vals + i * OPT_MAXATT, m->resp[i].vals, sizeof(double) * OPT_MAXATT);
            save_obj = fobj;

            dv->x = x0 + h;
            if (opt_apply_design(&d, m) < 0) { ok = 0; free(save_vals); break; }
            if (needs_run) {
                snprintf(tag, sizeof(tag), "c%d_d%d", cycle, dv->id);
                if (run_analysis(&R, &d, tag, &res, &st)) { ok = 0; free(save_vals); break; }
                if (eval_responses(m, &res, &d)) { results_free(&res); ok = 0; free(save_vals); break; }
                results_free(&res);
            } else {
                for (i = 0; i < m->nresp; i++) {
                    opt_resp *r = &m->resp[i];
                    int o;
                    if (is_closed_form(r)) { r->value = one_value(r, NULL, &d, 1, 0, 0, &o); r->vals[0] = r->value; }
                }
            }
            gradf[k] = (robj->value - save_obj) / h;
            for (i = 0; i < ng; i++) {
                opt_resp *r = &m->resp[gc[i].ri];
                double dg = (r->vals[gc[i].k] - save_vals[gc[i].ri * OPT_MAXATT + gc[i].k]) / h;
                gg[i * nfree + k] = (gc[i].upper ? dg : -dg) / gc[i].gnorm;
            }
            dv->x = x0;
            for (i = 0; i < m->nresp; i++)
                memcpy(m->resp[i].vals, save_vals + i * OPT_MAXATT, sizeof(double) * OPT_MAXATT);
            robj->value = save_obj;
            free(save_vals);
        }
        if (!ok) { free(v); free(gradf); free(gg); free(g0); free(xnew); free(lam); goto done; }

        /* the subproblem: objective and retained constraints only */
        {
            int     nkeep = 0, nact = 0;
            double *ggk = (double *) msc_alloc(sizeof(double) * (size_t) nfree * (size_t) (ng ? ng : 1));
            double *g0k = (double *) msc_alloc(sizeof(double) * (size_t) (ng ? ng : 1));
            double  sgn = m->objmax ? -1.0 : 1.0;
            for (i = 0; i < ng; i++) {
                if (!gc[i].keep) continue;
                g0k[nkeep] = gc[i].g;
                memcpy(ggk + nkeep * nfree, gg + i * nfree, sizeof(double) * (size_t) nfree);
                nkeep++;
            }
            for (k = 0; k < nfree; k++) {
                opt_desvar *dv = &m->dv[idx_free[k]];
                double move = m->prm.delx * fabs(dv->x);
                if (dv->delxv > 0.0) move = dv->delxv * fabs(dv->x);
                if (move < m->prm.dxmin) move = m->prm.dxmin;
                v[k].x0 = dv->x;
                v[k].lo = dv->x - move; if (v[k].lo < dv->xlb) v[k].lo = dv->xlb;
                v[k].hi = dv->x + move; if (v[k].hi > dv->xub) v[k].hi = dv->xub;
                if (v[k].lo <= 0.0) v[k].lo = 1e-12;
                v[k].gf = sgn * gradf[k];
            }
            solve_subproblem(v, nfree, g0k, ggk, nkeep, xnew, lam);
            for (i = 0; i < nkeep; i++) if (lam[i] > 0.0) nact++;
            fprintf(rep, "    approximate optimum: objective %.8g, %d active constraint%s\n",
                    approx(fobj, gradf, v, xnew, nfree), nact, nact == 1 ? "" : "s");
            for (k = 0; k < nfree; k++) {
                fprintf(rep, "      DESVAR %-6d %14.7g -> %14.7g   (df/dx %.5g)\n",
                        m->dv[idx_free[k]].id, m->dv[idx_free[k]].x, xnew[k], gradf[k]);
                m->dv[idx_free[k]].x = xnew[k];
            }
            free(ggk); free(g0k);
        }
        free(v); free(gradf); free(gg); free(g0); free(xnew); free(lam);
    }

    /* the final design, written as the MSC deck with its properties
     * updated, so that it can be analysed by anything                 */
    if (opt_apply_design(&d, m) >= 0) {
        FILE *fo;
        snprintf(finalpath, sizeof(finalpath), "%s_final.dat", stem);
        fo = fopen(finalpath, "w");
        if (fo) {
            fprintf(fo, "$ final design from nastran95ase SOL 200, %s\n", deck);
            for (i = 0; i < d.nexec; i++) fprintf(fo, "%s\n", d.exec[i]);
            fprintf(fo, "CEND\n");
            for (i = 0; i < d.ncase; i++) fprintf(fo, "%s\n", d.cases[i]);
            fprintf(fo, "BEGIN BULK\n");
            for (i = 0; i < d.nbulk; i++) {
                msc_card *c = &d.bulk[i];
                int nf = c->nfld;
                if (c->dropped) continue;
                while (nf > 0 && !*msc_f(c, nf)) nf--;
                fprintf(fo, "%s", c->name);
                for (j = 1; j <= nf; j++) fprintf(fo, ",%s", msc_f(c, j));
                fprintf(fo, "\n");
            }
            fprintf(fo, "ENDDATA\n");
            fclose(fo);
            fprintf(rep, "\n  final design written to %s\n", finalpath);
        }
    }
    fprintf(rep, "\n  %d solver runs in %d design cycle%s. %s\n", R.nruns, cycle,
            cycle == 1 ? "" : "s", converged ? "CONVERGED." : "NOT CONVERGED.");
    fprintf(rep, "\n                                        * * * END OF JOB * * *\n");
    rc = 0;
done:
    if (rep) fclose(rep);
    free(gc); free(m);
    msc_free(&d);
    return rc;
}
