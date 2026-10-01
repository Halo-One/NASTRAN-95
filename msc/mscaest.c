/* HALO: SOL 144, static aeroelastic trim -- the algebra of the AETRIM
 * module (mis/aetrim.f is its GINO side; SOL144.md has the theory).
 *
 * The Fortran side reads every data block the module is given and hands
 * it over here a record or a matrix column at a time (n95tbl_, n95tmb_,
 * n95tmc_); n95trn_ then does the work of the mode, and the Fortran side
 * collects the output matrices column by column (n95tos_, n95tgc_) and
 * prints the lines formatted here (n95tnl_, n95tln_, n95tpg_).
 *
 *   mode 1 (after GI): the a, l and r sets from USET, the s-set (the l
 *          dofs the spline reaches: the non-zero rows of GTKA), and
 *          ES (l x ns, the injection of the s-set) and DG (g x r, the
 *          rigid body modes of the SUPORT dofs, for the inertia loads).
 *   mode 2: per Mach the aerodynamics on the s-set (one LU of the AIC),
 *          per subcase the restrained elastic solve on the s-set, the
 *          trim, the stability derivatives, and the loads: PLA (l x n,
 *          the aerodynamic load on l), UDDT (r x n, the support
 *          accelerations), PGT (g x n, aerodynamic minus inertial loads,
 *          OLOAD).
 *
 * Everything is double precision. The dense solves are LAPACK's when the
 * build links one (N95_LAPACK), else the partial-pivoting LU below.
 */
#include "msctrim.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* the LAPACK and BLAS this needs, or their stand-ins                  */

#ifdef N95_LAPACK
extern void dgetrf_(const int *m, const int *n, double *a, const int *lda,
                    int *ipiv, int *info);
extern void dgetrs_(const char *tr, const int *n, const int *nrhs,
                    const double *a, const int *lda, const int *ipiv,
                    double *b, const int *ldb, int *info, size_t ltr);
extern void dgemm_(const char *ta, const char *tb, const int *m, const int *n,
                   const int *k, const double *alpha, const double *a,
                   const int *lda, const double *b, const int *ldb,
                   const double *beta, double *c, const int *ldc,
                   size_t lta, size_t ltb);
extern void ampczt_(const int *nt);   /* mis/ampczs: OpenBLAS's thread count */
/* the divergence roots: every eigenvalue of a real non-symmetric matrix */
extern void dgeev_(const char *jl, const char *jr, const int *n, double *a,
                   const int *lda, double *wr, double *wi, double *vl,
                   const int *ldvl, double *vr, const int *ldvr, double *work,
                   const int *lwork, int *info, size_t ljl, size_t ljr);
#endif

/* C (m x n) = A (m x k) * B (k x n), all column-major, C overwritten  */
static void mm(int m, int n, int k, const double *a, int lda,
               const double *b, int ldb, double *c, int ldc)
{
    if (m <= 0 || n <= 0) return;
    if (k <= 0) {
        int i, j;
        for (j = 0; j < n; j++) for (i = 0; i < m; i++) c[i + (size_t) j * ldc] = 0.0;
        return;
    }
#ifdef N95_LAPACK
    {
        const double one = 1.0, zero = 0.0;
        dgemm_("N", "N", &m, &n, &k, &one, a, &lda, b, &ldb, &zero, c, &ldc, 1, 1);
    }
#else
    {
        int i, j, p;
        for (j = 0; j < n; j++) {
            double *cj = c + (size_t) j * ldc;
            for (i = 0; i < m; i++) cj[i] = 0.0;
            for (p = 0; p < k; p++) {
                double bpj = b[p + (size_t) j * ldb];
                const double *ap = a + (size_t) p * lda;
                if (bpj == 0.0) continue;
                for (i = 0; i < m; i++) cj[i] += ap[i] * bpj;
            }
        }
    }
#endif
}

/* C (m x n) = A^T * B, A (k x m)                                       */
static void mtm(int m, int n, int k, const double *a, int lda,
                const double *b, int ldb, double *c, int ldc)
{
    int i, j, p;
    if (m <= 0 || n <= 0) return;
#ifdef N95_LAPACK
    if (k > 0) {
        const double one = 1.0, zero = 0.0;
        dgemm_("T", "N", &m, &n, &k, &one, a, &lda, b, &ldb, &zero, c, &ldc, 1, 1);
        return;
    }
#endif
    for (j = 0; j < n; j++)
        for (i = 0; i < m; i++) {
            double s = 0.0;
            for (p = 0; p < k; p++) s += a[p + (size_t) i * lda] * b[p + (size_t) j * ldb];
            c[i + (size_t) j * ldc] = s;
        }
}

/* LU with partial pivoting of the n x n a in place; 0 when singular    */
static int lu(int n, double *a, int *ipiv)
{
#ifdef N95_LAPACK
    int info = 0;
    if (n <= 0) return 1;
    dgetrf_(&n, &n, a, &n, ipiv, &info);
    return info == 0;
#else
    int i, j, k;
    for (k = 0; k < n; k++) {
        int    p = k;
        double big = fabs(a[k + (size_t) k * n]);
        for (i = k + 1; i < n; i++)
            if (fabs(a[i + (size_t) k * n]) > big) { big = fabs(a[i + (size_t) k * n]); p = i; }
        ipiv[k] = p + 1;
        if (big == 0.0) return 0;
        if (p != k)
            for (j = 0; j < n; j++) {
                double t = a[k + (size_t) j * n];
                a[k + (size_t) j * n] = a[p + (size_t) j * n];
                a[p + (size_t) j * n] = t;
            }
        for (i = k + 1; i < n; i++) a[i + (size_t) k * n] /= a[k + (size_t) k * n];
        for (j = k + 1; j < n; j++) {
            double akj = a[k + (size_t) j * n];
            if (akj == 0.0) continue;
            for (i = k + 1; i < n; i++) a[i + (size_t) j * n] -= a[i + (size_t) k * n] * akj;
        }
    }
    return 1;
#endif
}

/* solve with the factors of lu(), nrhs columns of b in place          */
static void lus(int n, const double *a, const int *ipiv, double *b, int nrhs)
{
#ifdef N95_LAPACK
    int info = 0;
    if (n <= 0 || nrhs <= 0) return;
    dgetrs_("N", &n, &nrhs, a, &n, ipiv, b, &n, &info, 1);
#else
    int i, j, r;
    for (r = 0; r < nrhs; r++) {
        double *x = b + (size_t) r * n;
        for (i = 0; i < n; i++) {
            int p = ipiv[i] - 1;
            if (p != i) { double t = x[i]; x[i] = x[p]; x[p] = t; }
        }
        for (j = 0; j < n; j++) {
            double xj = x[j];
            if (xj == 0.0) continue;
            for (i = j + 1; i < n; i++) x[i] -= a[i + (size_t) j * n] * xj;
        }
        for (j = n - 1; j >= 0; j--) {
            x[j] /= a[j + (size_t) j * n];
            if (x[j] == 0.0) continue;
            for (i = 0; i < j; i++) x[i] -= a[i + (size_t) j * n] * x[j];
        }
    }
#endif
}

/* the threads the LAPACK may use: N95_BLAS_THREADS, else
 * OMP_NUM_THREADS, else the processors there are (as the SOL 145 path) */
static void blas_threads(void)
{
#ifdef N95_LAPACK
    const char *e = getenv("N95_BLAS_THREADS");
    int nt = 0;
    if (!e || !*e) e = getenv("OMP_NUM_THREADS");
    if (e && *e) nt = atoi(e);
#ifndef _WIN32
    if (nt <= 0) nt = (int) sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (nt > 0) ampczt_(&nt);
#endif
}

static void *zalloc(size_t n)
{
    void *p = calloc(n ? n : 1, 1);
    if (!p) { fprintf(stderr, "nastran: AETRIM out of memory (%lu bytes)\n",
                      (unsigned long) n); exit(3); }
    return p;
}
#define DNEW(n) ((double *) zalloc(sizeof(double) * (size_t) (n)))
#define INEW(n) ((int *) zalloc(sizeof(int) * (size_t) (n)))

/* ------------------------------------------------------------------ */
/* a sparse matrix by columns, built a column at a time               */

typedef struct {
    int     nr, nc, nnz, cap;
    int    *cp;     /* nc+1 column starts                              */
    int    *ri;
    double *v;
} smat;

static void sm_free(smat *s)
{
    free(s->cp); free(s->ri); free(s->v);
    memset(s, 0, sizeof(*s));
}

static void sm_begin(smat *s, int nr, int nc)
{
    sm_free(s);
    s->nr = nr; s->nc = nc;
    s->cp = INEW(nc + 1);
    s->cap = 1024;
    s->ri = INEW(s->cap);
    s->v  = DNEW(s->cap);
}

static void sm_col(smat *s, int j, const double *col, int stride)
{
    int i;
    s->cp[j] = s->nnz;
    for (i = 0; i < s->nr; i++) {
        double x = col[(size_t) i * stride];
        if (x == 0.0) continue;
        if (s->nnz == s->cap) {
            s->cap *= 2;
            s->ri = (int *) realloc(s->ri, sizeof(int) * (size_t) s->cap);
            s->v  = (double *) realloc(s->v, sizeof(double) * (size_t) s->cap);
            if (!s->ri || !s->v) { fprintf(stderr, "nastran: AETRIM out of memory\n"); exit(3); }
        }
        s->ri[s->nnz] = i;
        s->v[s->nnz]  = x;
        s->nnz++;
    }
    s->cp[j + 1] = s->nnz;
}

/* ------------------------------------------------------------------ */
/* the module's state, kept between mode 1 and mode 2                  */

enum {  /* the inputs, in the module's input order                     */
    K_CASECC = 1, K_USET, K_GTKA, K_GTKG, K_AJJL, K_SKJ, K_D1JK, K_ACPT,
    K_CLS, K_XM, K_MXM, K_DM, K_MR, K_MDG, K_BGPDT, K_SIL, K_CSTMA,
    K_GPLA, K_USETA,
    K_AJJLHDR = 105,
    O_ES = 21, O_DG = 22, O_PLA = 31, O_UDDT = 32, O_PGT = 33
};

typedef struct { int id, type; double o[3], t[9]; } csys;  /* t row-major, x_basic = t x + o */

static struct {
    int     bit_ua, bit_ul, bit_ur, bit_uk;
    /* sets */
    int     luset;  int *uset;
    int     na, nl, nr, ns;
    int    *a_g;            /* a -> g                                   */
    int    *l_a, *r_a;      /* l -> a, r -> a                           */
    int    *a_l, *a_r;      /* a -> l or -1, a -> r or -1               */
    int    *s_l, *l_s;      /* s -> l, l -> s or -1                     */
    /* points (internal order)                                         */
    int     npts;  int *pt_cd;  double *pt_x;  int *pt_sil;  int *pt_ext;
    int     g_pt_n;         /* entries read from GPLA                   */
    int     ncs;   csys *cs;
    int     luseta; int *useta;
    /* matrices                                                        */
    smat    gtka, gtkg, skj, d1t;
    int     nj, nk;
    int     nmk;  double *mk;            /* (m,k) pairs of AJJL         */
    double *ajj;  int ajj_nrow, ajj_ncol;
    double *css, *xms, *mxm, *dm, *mr, *mdg;
    int     have_css, have_xms, have_mxm, have_dm, have_mr, have_mdg;
    int     dm_nrow, dm_ncol;   /* DM as read (l x r)                     */
    /* the DLM geometry of the boxes, j order (ACPT)                    */
    double *bx_xic, *bx_dx, *bx_ys, *bx_zs, *bx_sg, *bx_cg, *bx_ee;
    int     nbox;
    int    *box_id;
    /* subcases (CASECC)                                               */
    int     ncase;  int *case_id;  int *case_words;   /* 96 words each */
    /* reference frame                                                 */
    double  E[9];           /* reference axes in basic, column-major    */
    double  O[3];           /* reference origin, basic                  */
    double *rbg;            /* g x 6 rigid body modes about O           */
    double  rbr[36];        /* nr x 6, leading dimension 6 (nr <= 6)    */
    /* HALO (corrections): the aerodynamic frame (AEROS ACSID), in which
     * ACPT has the boxes; the box geometry in basic                     */
    double  Ea[9], Oa[3];
    double *bx_pk, *bx_pc, *bx_n, *bx_s;  /* 3 per box: the k point (half
                                           * chord), the collocation point
                                           * (3/4 chord), the normal, the
                                           * spanwise axis (R5's)         */
    double  smask[6];       /* 1, or 0 for the components a half model
                             * about x-z cancels (SYMXZ)                 */
    /* the correction matrices in the j and k sets (msctrim.c reads them) */
    double *wg, *fa;        /* W2GJ, FA2J (nj), or NULL                  */
    int     nw;  int *w_r, *w_c;  double *w_v;   /* WKK (k x k), sparse */
    int     have_w;
    /* outputs                                                         */
    double *es, *dg, *pla, *uddt, *pgt;
    int     es_nc, nsub;
    /* the lines to print                                              */
    char  **ln;  int nln, lncap;  int *ln_page;
    int     fatal;
} T;

static const char *aet_title = "AETRIM";

/* ------------------------------------------------------------------ */
/* the print, a line at a time; a line whose page is >= 0 starts a new
 * page with that subcase's headings (n95tpg_)                         */

static void out_page_line(int page, const char *s)
{
    if (T.nln == T.lncap) {
        T.lncap = T.lncap ? 2 * T.lncap : 256;
        T.ln = (char **) realloc(T.ln, sizeof(char *) * (size_t) T.lncap);
        T.ln_page = (int *) realloc(T.ln_page, sizeof(int) * (size_t) T.lncap);
        if (!T.ln || !T.ln_page) exit(3);
    }
    T.ln[T.nln] = msc_strdup(s);
    T.ln_page[T.nln] = page;
    T.nln++;
}

static void out(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out_page_line(-1, buf);
}

static void new_page(int icase) { out_page_line(icase, ""); }

/* a user fatal message, in the solver's shape; the module stops after
 * printing it                                                         */
static void fatal(int num, const char *fmt, ...)
{
    char    buf[1024], *p, *q;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out("0*** USER FATAL MESSAGE %d (%s)", num, aet_title);
    for (p = buf; p && *p; p = q) {
        q = strchr(p, '\n');
        if (q) *q++ = '\0';
        out("     %s", p);
    }
    T.fatal = 1;
}

static void info(int num, const char *fmt, ...)
{
    char    buf[1024], *p, *q;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out("0*** USER INFORMATION MESSAGE %d (%s)", num, aet_title);
    for (p = buf; p && *p; p = q) {
        q = strchr(p, '\n');
        if (q) *q++ = '\0';
        out("     %s", p);
    }
}

/* ------------------------------------------------------------------ */
/* the Fortran side's calls                                            */

void n95tbt_(const int *ua, const int *ul, const int *ur, const int *uk)
{
    T.bit_ua = *ua; T.bit_ul = *ul; T.bit_ur = *ur; T.bit_uk = *uk;
}

void n95ta0_(const int *mode)
{
    int i;
    for (i = 0; i < T.nln; i++) free(T.ln[i]);
    T.nln = 0;
    T.fatal = 0;
    if (*mode == 2) {
        /* mode 2 reads its own copies of what it needs                  */
        sm_free(&T.gtkg); sm_free(&T.skj); sm_free(&T.d1t);
        free(T.ajj); T.ajj = NULL; free(T.mk); T.mk = NULL; T.nmk = 0;
        free(T.case_id); free(T.case_words); T.case_id = NULL; T.case_words = NULL;
        T.ncase = 0;
        T.nbox = 0;
        T.have_css = T.have_xms = T.have_mxm = T.have_dm = T.have_mr = T.have_mdg = 0;
    }
    if (*mode == 1) { T.have_dm = 0; T.dm_nrow = T.dm_ncol = 0; }
}

static float wf(const int *w) { float f; memcpy(&f, w, sizeof(f)); return f; }

/* one record of a table data block                                    */
void n95tbl_(const int *kind, const int *irec, const int *n, const int *w)
{
    int i, k = *kind, nw = *n;
    switch (k) {
    case K_CASECC:
        T.case_id = (int *) realloc(T.case_id, sizeof(int) * (size_t) (T.ncase + 1));
        T.case_words = (int *) realloc(T.case_words, sizeof(int) * 96 * (size_t) (T.ncase + 1));
        T.case_id[T.ncase] = nw > 0 ? w[0] : T.ncase + 1;
        for (i = 0; i < 96; i++)
            T.case_words[96 * T.ncase + i] = (38 + i < nw) ? w[38 + i] : 0x20202020;
        T.ncase++;
        break;
    case K_USET:
        if (*irec != 1) break;
        free(T.uset);
        T.luset = nw;
        T.uset = INEW(nw);
        memcpy(T.uset, w, sizeof(int) * (size_t) nw);
        break;
    case K_USETA:
        if (*irec != 1) break;
        free(T.useta);
        T.luseta = nw;
        T.useta = INEW(nw);
        memcpy(T.useta, w, sizeof(int) * (size_t) nw);
        break;
    case K_BGPDT:
        if (*irec != 1) break;
        free(T.pt_cd); free(T.pt_x);
        T.npts = nw / 4;
        T.pt_cd = INEW(T.npts);
        T.pt_x = DNEW(3 * T.npts);
        for (i = 0; i < T.npts; i++) {
            T.pt_cd[i] = w[4 * i];
            T.pt_x[3 * i]     = wf(&w[4 * i + 1]);
            T.pt_x[3 * i + 1] = wf(&w[4 * i + 2]);
            T.pt_x[3 * i + 2] = wf(&w[4 * i + 3]);
        }
        break;
    case K_SIL:
        if (*irec != 1) break;
        free(T.pt_sil);
        T.pt_sil = INEW(nw > 0 ? nw : 1);
        memcpy(T.pt_sil, w, sizeof(int) * (size_t) nw);
        break;
    case K_GPLA:
        if (*irec != 1) break;
        free(T.pt_ext);
        T.g_pt_n = nw;
        T.pt_ext = INEW(nw > 0 ? nw : 1);
        memcpy(T.pt_ext, w, sizeof(int) * (size_t) nw);
        break;
    case K_CSTMA:
        if (*irec != 1) break;
        free(T.cs);
        T.ncs = nw / 14;
        T.cs = (csys *) zalloc(sizeof(csys) * (size_t) (T.ncs ? T.ncs : 1));
        for (i = 0; i < T.ncs; i++) {
            int j;
            T.cs[i].id = w[14 * i];
            T.cs[i].type = w[14 * i + 1];
            for (j = 0; j < 3; j++) T.cs[i].o[j] = wf(&w[14 * i + 2 + j]);
            for (j = 0; j < 9; j++) T.cs[i].t[j] = wf(&w[14 * i + 5 + j]);
        }
        break;
    case K_ACPT: {
        /* a doublet lattice group: METHOD, NP, NSTRIP, NTP, F, NC(NP),
         * NB(NP), YS ZS EE SG CG (NSTRIP each), XIC DELX XLAM (NTP each) */
        int method, np, nstrip, ntp, p, s, b, base;
        if (nw < 5) break;
        method = w[0];
        if (method != 1) {
            fatal(9620, "the aerodynamic model has a group that is not the doublet\n"
                  "lattice (ACPT method %d). SOL 144 here takes CAERO1 panels\n"
                  "only.", method);
            break;
        }
        np = w[1]; nstrip = w[2]; ntp = w[3];
        base = 5;
        {
            const int *nc = w + base, *nb = w + base + np;
            const int *ys = nb + np, *zs = ys + nstrip, *ee = zs + nstrip,
                      *sg = ee + nstrip, *cg = sg + nstrip, *xic = cg + nstrip,
                      *dx = xic + ntp;
            int n0 = T.nbox;
            T.bx_xic = (double *) realloc(T.bx_xic, sizeof(double) * (size_t) (n0 + ntp));
            T.bx_dx  = (double *) realloc(T.bx_dx,  sizeof(double) * (size_t) (n0 + ntp));
            T.bx_ys  = (double *) realloc(T.bx_ys,  sizeof(double) * (size_t) (n0 + ntp));
            T.bx_zs  = (double *) realloc(T.bx_zs,  sizeof(double) * (size_t) (n0 + ntp));
            T.bx_sg  = (double *) realloc(T.bx_sg,  sizeof(double) * (size_t) (n0 + ntp));
            T.bx_cg  = (double *) realloc(T.bx_cg,  sizeof(double) * (size_t) (n0 + ntp));
            T.bx_ee  = (double *) realloc(T.bx_ee,  sizeof(double) * (size_t) (n0 + ntp));
            /* boxes go chordwise within a strip, strips spanwise within a
             * panel (NC boxes a strip; NB is the running count of boxes
             * through the panel, APD1's NBOX), panels in order            */
            b = 0; s = 0;
            for (p = 0; p < np; p++) {
                int nbp = nb[p] - (p ? nb[p - 1] : 0), ncp = nc[p], q;
                for (q = 0; q < nbp; q++, b++) {
                    int st = s + q / ncp;
                    T.bx_xic[n0 + b] = wf(&xic[b]);
                    T.bx_dx[n0 + b]  = wf(&dx[b]);
                    T.bx_ys[n0 + b]  = wf(&ys[st]);
                    T.bx_zs[n0 + b]  = wf(&zs[st]);
                    T.bx_ee[n0 + b]  = wf(&ee[st]);
                    T.bx_sg[n0 + b]  = wf(&sg[st]);
                    T.bx_cg[n0 + b]  = wf(&cg[st]);
                }
                s += nbp / ncp;
            }
            T.nbox = n0 + ntp;
            (void) irec;
        }
        break;
    }
    case K_AJJLHDR:
        /* NJ, NK, NMK, (m,k) pairs, ... (the name already skipped)     */
        if (nw < 3) break;
        T.nj = w[0]; T.nk = w[1]; T.nmk = w[2];
        free(T.mk);
        T.mk = DNEW(2 * (T.nmk > 0 ? T.nmk : 1));
        for (i = 0; i < 2 * T.nmk && 3 + i < nw; i++) T.mk[i] = wf(&w[3 + i]);
        break;
    default:
        break;
    }
}

/* the start of a matrix: rows, columns, complex or not (nrow 0: purged) */
static int mat_kind, mat_nrow, mat_ncol, mat_cplx;

void n95tmb_(const int *kind, const int *nrow, const int *ncol, const int *cplx)
{
    mat_kind = *kind; mat_nrow = *nrow; mat_ncol = *ncol; mat_cplx = *cplx;
    switch (mat_kind) {
    case K_GTKA:  sm_begin(&T.gtka, mat_nrow, mat_ncol); break;
    case K_GTKG:  sm_begin(&T.gtkg, mat_nrow, mat_ncol); break;
    case K_SKJ:   sm_begin(&T.skj,  mat_nrow, mat_ncol); break;
    case K_D1JK:  sm_begin(&T.d1t,  mat_nrow, mat_ncol); break;
    case K_AJJL:
        free(T.ajj);
        T.ajj_nrow = mat_nrow; T.ajj_ncol = mat_ncol;
        T.ajj = DNEW((size_t) mat_nrow * (size_t) mat_ncol);
        break;
    case K_CLS:  free(T.css); T.css = DNEW((size_t) T.ns * T.ns);  T.have_css = mat_nrow > 0; break;
    case K_XM:   free(T.xms); T.xms = DNEW((size_t) T.ns * T.nr);  T.have_xms = mat_nrow > 0; break;
    case K_MXM:  free(T.mxm); T.mxm = DNEW(36);                    T.have_mxm = mat_nrow > 0; break;
    case K_DM:
        free(T.dm);
        T.dm_nrow = mat_nrow; T.dm_ncol = mat_ncol;
        T.dm = DNEW((size_t) mat_nrow * (size_t) (mat_ncol > 0 ? mat_ncol : 1));
        T.have_dm = mat_nrow > 0;
        break;
    case K_MR:   free(T.mr);  T.mr  = DNEW(36);                    T.have_mr  = mat_nrow > 0; break;
    case K_MDG:  free(T.mdg); T.mdg = DNEW((size_t) T.luset * T.nr); T.have_mdg = mat_nrow > 0; break;
    default: break;
    }
}

/* one column, double (or double complex, re/im interleaved)           */
void n95tmc_(const int *kind, const int *jcol, const double *col)
{
    int j = *jcol - 1, i, stride = mat_cplx ? 2 : 1;
    switch (*kind) {
    case K_GTKA: sm_col(&T.gtka, j, col, stride); break;
    case K_GTKG: sm_col(&T.gtkg, j, col, stride); break;
    case K_SKJ:  sm_col(&T.skj,  j, col, stride); break;
    case K_D1JK: sm_col(&T.d1t,  j, col, stride); break;
    case K_AJJL:
        for (i = 0; i < mat_nrow; i++)
            T.ajj[(size_t) j * mat_nrow + i] = col[(size_t) i * stride];
        break;
    case K_CLS:
        /* C_ls's s rows: C_ss                                           */
        if (j < T.ns && mat_nrow == T.nl)
            for (i = 0; i < T.ns; i++)
                T.css[(size_t) j * T.ns + i] = col[(size_t) T.s_l[i] * stride];
        break;
    case K_XM:
        if (j < T.nr && mat_nrow == T.nl)
            for (i = 0; i < T.ns; i++)
                T.xms[(size_t) j * T.ns + i] = col[(size_t) T.s_l[i] * stride];
        break;
    case K_DM:
        if (j < T.dm_ncol)
            for (i = 0; i < T.dm_nrow; i++) T.dm[(size_t) j * T.dm_nrow + i] = col[(size_t) i * stride];
        break;
    case K_MXM:
        if (j < 6 && mat_nrow <= 6)
            for (i = 0; i < mat_nrow; i++) T.mxm[j * 6 + i] = col[(size_t) i * stride];
        break;
    case K_MR:
        if (j < 6 && mat_nrow <= 6)
            for (i = 0; i < mat_nrow; i++) T.mr[j * 6 + i] = col[(size_t) i * stride];
        break;
    case K_MDG:
        if (j < T.nr && mat_nrow == T.luset)
            for (i = 0; i < T.luset; i++) T.mdg[(size_t) j * T.luset + i] = col[(size_t) i * stride];
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* small vector helpers                                                */

static void cross(const double *a, const double *b, double *c)
{
    c[0] = a[1] * b[2] - a[2] * b[1];
    c[1] = a[2] * b[0] - a[0] * b[2];
    c[2] = a[0] * b[1] - a[1] * b[0];
}
static double dot(const double *a, const double *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static const csys *find_cs(int id)
{
    int i;
    for (i = 0; i < T.ncs; i++) if (T.cs[i].id == id) return &T.cs[i];
    return NULL;
}

/* the axes (columns, basic components) of a rectangular system        */
static int cs_axes(int id, double *ax, double *org)
{
    const csys *c;
    int i, j;
    if (id == 0) {
        for (i = 0; i < 9; i++) ax[i] = (i % 4 == 0) ? 1.0 : 0.0;
        if (org) org[0] = org[1] = org[2] = 0.0;
        return 1;
    }
    c = find_cs(id);
    if (!c || c->type != 1) return 0;
    /* t row-major, x_basic = t x_local + o: the local axes are t's columns */
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) ax[i + 3 * j] = c->t[3 * i + j];
    if (org) for (i = 0; i < 3; i++) org[i] = c->o[i];
    return 1;
}

/* HALO (corrections): the pseudo-inverse of a symmetric 6 x 6 (Jacobi
 * rotations; the directions whose eigenvalue is below 1e-10 of the
 * largest are left out). Used for the rigid body fit of a model whose a
 * set does not see all six rigid body motions (a half model).          */
static void pinv6(const double *a, double *ai)
{
    double A[36], V[36], d[6], big = 0.0;
    int i, j, k, sweep;
    memcpy(A, a, sizeof(A));
    for (i = 0; i < 36; i++) V[i] = (i % 7 == 0) ? 1.0 : 0.0;
    for (sweep = 0; sweep < 60; sweep++) {
        double off = 0.0;
        for (i = 0; i < 6; i++) for (j = i + 1; j < 6; j++) off += A[i + 6 * j] * A[i + 6 * j];
        if (off < 1e-300) break;
        for (i = 0; i < 6; i++)
            for (j = i + 1; j < 6; j++) {
                double apq = A[i + 6 * j], app, aqq, th, t, c, s;
                if (fabs(apq) < 1e-300) continue;
                app = A[i + 6 * i]; aqq = A[j + 6 * j];
                th = 0.5 * (aqq - app) / apq;
                t = (th >= 0.0 ? 1.0 : -1.0) / (fabs(th) + sqrt(th * th + 1.0));
                c = 1.0 / sqrt(t * t + 1.0); s = t * c;
                for (k = 0; k < 6; k++) {
                    double akp = A[k + 6 * i], akq = A[k + 6 * j];
                    A[k + 6 * i] = c * akp - s * akq;
                    A[k + 6 * j] = s * akp + c * akq;
                }
                for (k = 0; k < 6; k++) {
                    double apk = A[i + 6 * k], aqk = A[j + 6 * k];
                    A[i + 6 * k] = c * apk - s * aqk;
                    A[j + 6 * k] = s * apk + c * aqk;
                }
                for (k = 0; k < 6; k++) {
                    double vkp = V[k + 6 * i], vkq = V[k + 6 * j];
                    V[k + 6 * i] = c * vkp - s * vkq;
                    V[k + 6 * j] = s * vkp + c * vkq;
                }
            }
    }
    for (i = 0; i < 6; i++) { d[i] = A[i + 6 * i]; if (fabs(d[i]) > big) big = fabs(d[i]); }
    for (i = 0; i < 36; i++) ai[i] = 0.0;
    for (k = 0; k < 6; k++) {
        if (fabs(d[k]) <= 1e-10 * big) continue;
        for (i = 0; i < 6; i++)
            for (j = 0; j < 6; j++) ai[i + 6 * j] += V[i + 6 * k] * V[j + 6 * k] / d[k];
    }
}

/* ------------------------------------------------------------------ */
/* mode 1: the sets, the s-set, ES and DG                              */

static int mode1(void)
{
    int g, a, i, j, k, m;

    if (!T.uset || T.luset <= 0) { fatal(9601, "USET was not available."); return 1; }

    /* the a set in g order, split into l and r                         */
    free(T.a_g); free(T.l_a); free(T.r_a); free(T.a_l); free(T.a_r);
    T.a_g = INEW(T.luset); T.l_a = INEW(T.luset); T.r_a = INEW(T.luset);
    T.na = T.nl = T.nr = 0;
    for (g = 0; g < T.luset; g++) if (T.uset[g] & T.bit_ua) T.a_g[T.na++] = g;
    T.a_l = INEW(T.na); T.a_r = INEW(T.na);
    for (a = 0; a < T.na; a++) {
        int w = T.uset[T.a_g[a]];
        T.a_l[a] = T.a_r[a] = -1;
        if (w & T.bit_ur) { T.a_r[a] = T.nr; T.r_a[T.nr++] = a; }
        else              { T.a_l[a] = T.nl; T.l_a[T.nl++] = a; }
    }
    /* HALO (corrections): a half model is SUPORTed on its symmetric (or
     * antisymmetric) rigid body dofs, a wind-tunnel model on none        */
    if (T.nr > 6) {
        fatal(9602, "SOL 144 here takes at most six SUPORT degrees of freedom (one\n"
              "set of rigid body motions), and the model has %d.", T.nr);
        return 1;
    }
    if (T.gtka.nr != T.na) {
        fatal(9603, "GTKA has %d rows and the a-set %d dofs.", T.gtka.nr, T.na);
        return 1;
    }

    /* the s set: the l dofs the spline reaches                        */
    free(T.s_l); free(T.l_s);
    T.s_l = INEW(T.nl); T.l_s = INEW(T.nl);
    for (i = 0; i < T.nl; i++) T.l_s[i] = -1;
    {
        char *hit = (char *) zalloc((size_t) T.na);
        for (k = 0; k < T.gtka.nc; k++)
            for (m = T.gtka.cp[k]; m < T.gtka.cp[k + 1]; m++) hit[T.gtka.ri[m]] = 1;
        T.ns = 0;
        for (i = 0; i < T.nl; i++)
            if (hit[T.l_a[i]]) { T.l_s[i] = T.ns; T.s_l[T.ns++] = i; }
        free(hit);
    }
    if (T.ns == 0) {
        fatal(9604, "no structural degree of freedom is splined to the aerodynamic\n"
              "boxes (GTKA has no non-zero row in the l set).");
        return 1;
    }

    /* ES: l x ns, a 1 at (s_l(i), i)                                   */
    free(T.es);
    T.es_nc = T.ns;
    T.es = NULL;   /* written a column at a time in n95tgc_            */

    /* the reference frame: AEROS RCSID                                 */
    if (!cs_axes(aet_g.rcsid, T.E, T.O)) {
        fatal(9605, "AEROS RCSID %d is not a rectangular coordinate system of the\n"
              "model.", aet_g.rcsid);
        return 1;
    }
    /* the aerodynamic frame: AEROS ACSID (its x axis is the flow)      */
    if (!cs_axes(aet_g.acsid, T.Ea, T.Oa)) {
        fatal(9660, "AEROS ACSID %d is not a rectangular coordinate system of the\n"
              "model.", aet_g.acsid);
        return 1;
    }
    for (i = 0; i < 6; i++) T.smask[i] = 1.0;
    if (aet_g.symxz > 0) T.smask[1] = T.smask[3] = T.smask[5] = 0.0;
    if (aet_g.symxz < 0) T.smask[0] = T.smask[2] = T.smask[4] = 0.0;

    /* the rigid body modes of the g set about the reference point      */
    free(T.rbg);
    T.rbg = DNEW((size_t) T.luset * 6);
    for (i = 0; i < T.npts; i++) {
        int    sil = T.pt_sil[i] - 1, cd = T.pt_cd[i];
        double tcd[9], d[3];
        if (cd < 0) continue;                        /* a scalar point  */
        if (!cs_axes(cd, tcd, NULL)) {
            const csys *c = find_cs(cd);
            fatal(9606, "grid %d has its displacements in coordinate system %d, which\n"
                  "is %s; SOL 144 here takes rectangular systems only.",
                  i < T.g_pt_n ? T.pt_ext[i] : i + 1, cd,
                  c ? (c->type == 2 ? "cylindrical" : "spherical") : "not defined");
            return 1;
        }
        for (k = 0; k < 3; k++) d[k] = T.pt_x[3 * i + k] - T.O[k];
        for (m = 0; m < 6; m++) {
            double e[3], u[3], r[3];
            for (k = 0; k < 3; k++) e[k] = T.E[k + 3 * (m % 3)];
            if (m < 3) { u[0] = e[0]; u[1] = e[1]; u[2] = e[2]; r[0] = r[1] = r[2] = 0.0; }
            else { cross(e, d, u); r[0] = e[0]; r[1] = e[1]; r[2] = e[2]; }
            /* components in the grid's displacement system: tcd^T u    */
            for (k = 0; k < 3; k++) {
                double cu = tcd[0 + 3 * k] * u[0] + tcd[1 + 3 * k] * u[1] + tcd[2 + 3 * k] * u[2];
                double cr = tcd[0 + 3 * k] * r[0] + tcd[1 + 3 * k] * r[1] + tcd[2 + 3 * k] * r[2];
                if (sil + k < T.luset)     T.rbg[(size_t) m * T.luset + sil + k] = cu;
                if (sil + 3 + k < T.luset) T.rbg[(size_t) m * T.luset + sil + 3 + k] = cr;
            }
        }
    }
    /* RB_r (nr x 6): its rows are the r dofs                           */
    memset(T.rbr, 0, sizeof(T.rbr));
    for (i = 0; i < T.nr; i++)
        for (m = 0; m < 6; m++)
            T.rbr[i + 6 * m] = T.rbg[(size_t) m * T.luset + T.a_g[T.r_a[i]]];
    free(T.dg);
    T.dg = NULL;
    if (T.nr == 6) {
        double lr[36];
        int    ip[6];
        memcpy(lr, T.rbr, sizeof(lr));
        if (!lu(6, lr, ip) || fabs(lr[0] * lr[7] * lr[14] * lr[21] * lr[28] * lr[35]) < 1e-12) {
            fatal(9607, "the SUPORT degrees of freedom do not define the six rigid body\n"
                  "motions independently (their rigid body matrix is singular).");
            return 1;
        }
        /* DG = RB_g RB_r^-1: (RB_r^-T RB_g^T)^T; solve RB_r^T X = RB_g^T */
        T.dg = DNEW((size_t) T.luset * 6);
        {
            double rt[36], *bt = DNEW((size_t) 6 * T.luset);
            int    ip2[6];
            for (i = 0; i < 6; i++) for (j = 0; j < 6; j++) rt[i + 6 * j] = T.rbr[j + 6 * i];
            lu(6, rt, ip2);
            for (g = 0; g < T.luset; g++)
                for (m = 0; m < 6; m++) bt[m + 6 * (size_t) g] = T.rbg[(size_t) m * T.luset + g];
            lus(6, rt, ip2, bt, T.luset);
            for (g = 0; g < T.luset; g++)
                for (i = 0; i < 6; i++) T.dg[(size_t) i * T.luset + g] = bt[i + 6 * (size_t) g];
            free(bt);
        }
    } else if (T.nr > 0) {
        /* HALO (corrections): fewer than six SUPORT dofs (a half model):
         * the rigid body modes of the r dofs are the structure's own DM
         * on the a set ([DM; I]); in the g set they are the geometric
         * modes RB_g C, C (6 x nr) the least-squares fit RB_a C = [DM; I]
         * (exact when the SUPORT dofs are rigid body motions)           */
        int    nr = T.nr, na = T.na;
        double N[36], *R6 = DNEW((size_t) 6 * nr), res = 0.0, nrm = 0.0;
        int    ipn[6];
        if (!T.have_dm || T.dm_nrow != T.nl || T.dm_ncol != nr) {
            fatal(9661, "the rigid body modes DM (%d x %d) do not match the l and r\n"
                  "sets (%d x %d).", T.dm_nrow, T.dm_ncol, T.nl, nr);
            free(R6);
            return 1;
        }
        memset(N, 0, sizeof(N));
        for (a = 0; a < na; a++) {
            int    ga = T.a_g[a];
            double ra[6], da[6];
            for (m = 0; m < 6; m++) ra[m] = T.rbg[(size_t) m * T.luset + ga];
            for (j = 0; j < nr; j++)
                da[j] = T.a_l[a] >= 0 ? T.dm[(size_t) j * T.nl + T.a_l[a]] : (T.a_r[a] == j ? 1.0 : 0.0);
            for (m = 0; m < 6; m++) {
                for (k = 0; k < 6; k++) N[m + 6 * k] += ra[m] * ra[k];
                for (j = 0; j < nr; j++) R6[m + 6 * j] += ra[m] * da[j];
            }
        }
        {
            /* C = pinv(RB_a^T RB_a) RB_a^T D_a: the minimum-norm fit, so
             * a rigid body motion the a set cannot see (a half model's
             * other symmetry) stays out of the g-set modes             */
            double Ni[36], *t = DNEW((size_t) 6 * nr);
            pinv6(N, Ni);
            mm(6, nr, 6, Ni, 6, R6, 6, t, 6);
            memcpy(R6, t, sizeof(double) * (size_t) 6 * nr);
            free(t);
            (void) ipn;
        }
        /* how well the fit holds: [DM; I] - RB_a C, relative            */
        for (a = 0; a < na; a++) {
            int ga = T.a_g[a];
            for (j = 0; j < nr; j++) {
                double da = T.a_l[a] >= 0 ? T.dm[(size_t) j * T.nl + T.a_l[a]] : (T.a_r[a] == j ? 1.0 : 0.0);
                double f = 0.0;
                for (m = 0; m < 6; m++) f += T.rbg[(size_t) m * T.luset + ga] * R6[m + 6 * j];
                res += (da - f) * (da - f);
                nrm += da * da;
            }
        }
        if (nrm > 0.0 && sqrt(res / nrm) > 1e-6)
            info(9663, "the SUPORT dofs' rigid body modes (DM) differ from the geometric\n"
                 "rigid body motions by %.2e (relative): the inertia loads of the\n"
                 "dofs outside the a set (OLOAD) are approximate.", sqrt(res / nrm));
        T.dg = DNEW((size_t) T.luset * nr);
        for (j = 0; j < nr; j++)
            for (g = 0; g < T.luset; g++) {
                double f = 0.0;
                for (m = 0; m < 6; m++) f += T.rbg[(size_t) m * T.luset + g] * R6[m + 6 * j];
                T.dg[(size_t) j * T.luset + g] = f;
            }
        free(R6);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* mode 2: the trim                                                    */

/* the trim variables, in ID order (AESTAT and AESURF share the space)  */
typedef struct {
    int    id;
    char   label[AET_LABLEN];
    int    surf;          /* index into aet_g.surf, or -1               */
    int    kind;          /* 0 ANGLEA 1 SIDES 2 ROLL 3 PITCH 4 YAW
                           * 5..10 URDD1..6, 11 surface, -1 unknown       */
} xvar;

static xvar *X;
static int   NX;          /* the trim variables                         */
static int   NXI;         /* and the intercept: column NX of every
                           * aerodynamic matrix is the user's downwash
                           * and pressures (W2GJ, FA2J), fixed at 1    */

static int var_kind(const char *lab)
{
    static const char *names[] = { "ANGLEA", "SIDES", "ROLL", "PITCH", "YAW",
        "URDD1", "URDD2", "URDD3", "URDD4", "URDD5", "URDD6", NULL };
    int i;
    for (i = 0; names[i]; i++) if (strcmp(lab, names[i]) == 0) return i;
    return -1;
}

static int xcmp(const void *a, const void *b)
{
    return ((const xvar *) a)->id - ((const xvar *) b)->id;
}

static int find_x(const char *lab)
{
    int i;
    for (i = 0; i < NX; i++) if (strcmp(X[i].label, lab) == 0) return i;
    return -1;
}

static int is_urdd(int c) { return c < NX && X[c].kind >= 5 && X[c].kind <= 10; }

static int build_vars(void)
{
    int i;
    free(X);
    NX = aet_g.nstat + aet_g.nsurf;
    NXI = NX + 1;
    X = (xvar *) zalloc(sizeof(xvar) * (size_t) (NX ? NX : 1));
    for (i = 0; i < aet_g.nstat; i++) {
        X[i].id = aet_g.stat[i].id;
        strcpy(X[i].label, aet_g.stat[i].label);
        X[i].surf = -1;
        X[i].kind = var_kind(X[i].label);
        if (X[i].kind < 0) {
            fatal(9621, "AESTAT %d %s is not one of the standard rigid body labels\n"
                  "(ANGLEA, SIDES, ROLL, PITCH, YAW, URDD1-6); a user label needs\n"
                  "AEPARM/AEDW/AEFORCE data, which SOL 144 here does not read.",
                  X[i].id, X[i].label);
            return 1;
        }
    }
    for (i = 0; i < aet_g.nsurf; i++) {
        xvar *v = &X[aet_g.nstat + i];
        v->id = aet_g.surf[i].id;
        strcpy(v->label, aet_g.surf[i].label);
        v->surf = i;
        v->kind = 11;
    }
    qsort(X, (size_t) NX, sizeof(xvar), xcmp);
    return 0;
}

/* the box index (j) of each box id                                    */
static int box_index(int id)
{
    int j;
    /* box ids are ascending within a panel but not across all panels:
     * a plain search                                                    */
    for (j = 0; j < T.nbox; j++) if (T.box_id[j] == id) return j;
    return -1;
}

/* the box ids in j order: the aero points of GPLA whose component 3 is
 * in the k set (USETA), in the order APD wrote them                    */
static int find_box_ids(void)
{
    int p, n = 0;
    free(T.box_id);
    T.box_id = INEW(T.nbox > 0 ? T.nbox : 1);
    if (!T.pt_ext || !T.useta) return 0;
    for (p = T.npts; p < T.g_pt_n; p++) {
        int dof = T.luset + 6 * (p - T.npts) + 2;     /* component 3 */
        if (dof >= T.luseta) break;
        if (T.useta[dof] & T.bit_uk) {
            if (n < T.nbox) T.box_id[n] = T.pt_ext[p];
            n++;
        }
    }
    return n;
}

/* HALO (corrections): the boxes in basic. ACPT has them in the
 * aerodynamic frame (APD1/APDCS: x_aero = Ta^T (x_basic - Oa)), the
 * k point at the half chord (XIC is the box's quarter chord), the
 * collocation point at three quarters, the normal (0, -sin, cos) and
 * the hinge (R5) axis (0, cos, sin) of the strip's dihedral            */
static void box_geometry(void)
{
    int j, k;
    free(T.bx_pk); free(T.bx_pc); free(T.bx_n); free(T.bx_s);
    T.bx_pk = DNEW(3 * T.nbox); T.bx_pc = DNEW(3 * T.nbox);
    T.bx_n  = DNEW(3 * T.nbox); T.bx_s  = DNEW(3 * T.nbox);
    for (j = 0; j < T.nbox; j++) {
        double pk[3] = { T.bx_xic[j] + 0.25 * T.bx_dx[j], T.bx_ys[j], T.bx_zs[j] };
        double pc[3] = { T.bx_xic[j] + 0.50 * T.bx_dx[j], T.bx_ys[j], T.bx_zs[j] };
        double n[3]  = { 0.0, -T.bx_sg[j], T.bx_cg[j] };
        double sv[3] = { 0.0,  T.bx_cg[j], T.bx_sg[j] };
        for (k = 0; k < 3; k++) {
            T.bx_pk[3 * j + k] = T.Oa[k] + T.Ea[k] * pk[0] + T.Ea[k + 3] * pk[1] + T.Ea[k + 6] * pk[2];
            T.bx_pc[3 * j + k] = T.Oa[k] + T.Ea[k] * pc[0] + T.Ea[k + 3] * pc[1] + T.Ea[k + 6] * pc[2];
            T.bx_n[3 * j + k]  = T.Ea[k] * n[0]  + T.Ea[k + 3] * n[1]  + T.Ea[k + 6] * n[2];
            T.bx_s[3 * j + k]  = T.Ea[k] * sv[0] + T.Ea[k + 3] * sv[1] + T.Ea[k + 6] * sv[2];
        }
    }
}

/* HALO (corrections): W2GJ and FA2J in the j set, WKK in the k set.
 * DMI rows are MSC's set positions: the boxes by ascending box id (the
 * guide, example HA144F: "the numbering begins with the lowest numbered
 * CAERO1"), and in the k set T3 then R5 of each box; DMIJ / DMIK rows
 * are box ids and components (3; 3 or 5). Returns 1 on a fatal.         */
static int *sorted_boxes;     /* MSC's j position -> j index           */

static int bxcmp(const void *a, const void *b)
{
    int ia = *(const int *) a, ib = *(const int *) b;
    return T.box_id[ia] - T.box_id[ib];
}

static int k_index(const aet_dmi *M, int pos_or_id, int comp)
{
    int jb;
    if (M->kind == 1) {
        int p = pos_or_id - 1;
        if (p < 0 || p >= 2 * T.nbox) return -1;
        return 2 * sorted_boxes[p / 2] + (p % 2);
    }
    jb = box_index(pos_or_id);
    if (jb < 0 || (comp != 3 && comp != 5)) return -1;
    return 2 * jb + (comp == 5 ? 1 : 0);
}

static int j_index(const aet_dmi *M, int pos_or_id, int comp)
{
    if (M->kind == 1) {
        int p = pos_or_id - 1;
        if (p < 0 || p >= T.nbox) return -1;
        return sorted_boxes[p];
    }
    if (comp != 3 && comp != 0) return -1;
    return box_index(pos_or_id);
}

static int corrections(void)
{
    int i, w;
    free(T.wg); free(T.fa); T.wg = T.fa = NULL;
    free(T.w_r); free(T.w_c); free(T.w_v); T.w_r = T.w_c = NULL; T.w_v = NULL;
    T.nw = 0; T.have_w = 0;
    free(sorted_boxes);
    sorted_boxes = INEW(T.nbox);
    for (i = 0; i < T.nbox; i++) sorted_boxes[i] = i;
    qsort(sorted_boxes, (size_t) T.nbox, sizeof(int), bxcmp);

    for (w = 0; w < 2; w++) {
        const aet_dmi *M = aet_matrix(w);
        double *v;
        int nbad = 0;
        if (!M) continue;
        if (M->kind == 1 && M->m != T.nj) {
            fatal(9664, "DMI %s has %d rows and the j set %d (one per box).",
                  M->name, M->m, T.nj);
            return 1;
        }
        v = DNEW(T.nj);
        for (i = 0; i < M->ne; i++) {
            int j = j_index(M, M->e[i].ri, M->e[i].rc);
            if (M->e[i].ci != 1) continue;
            if (j < 0) { nbad++; continue; }
            v[j] = M->e[i].v;
        }
        if (nbad)
            info(9665, "%s %s: %d term%s name%s no box (or a component other than\n"
                 "3) and %s left out.", M->kind == 1 ? "DMI" : "DMIJ", M->name, nbad,
                 nbad == 1 ? "" : "s", nbad == 1 ? "s" : "", nbad == 1 ? "is" : "are");
        if (w == 0) T.wg = v; else T.fa = v;
    }
    {
        const aet_dmi *M = aet_matrix(2);
        int nbad = 0;
        if (M) {
            if (M->kind == 1 && M->m != T.nk) {
                fatal(9664, "DMI %s has %d rows and the k set %d (two per box).",
                      M->name, M->m, T.nk);
                return 1;
            }
            T.w_r = INEW(M->ne + T.nk); T.w_c = INEW(M->ne + T.nk);
            T.w_v = DNEW(M->ne + T.nk);
            if (M->kind == 1 && M->form == 8) {
                for (i = 0; i < T.nk; i++) {
                    T.w_r[T.nw] = T.w_c[T.nw] = i; T.w_v[T.nw++] = 1.0;
                }
            }
            for (i = 0; i < M->ne; i++) {
                int r, c;
                if (M->kind == 1 && M->form == 3) {
                    if (M->e[i].ci != 1) continue;      /* the diagonal */
                    r = c = k_index(M, M->e[i].ri, 0);
                } else {
                    r = k_index(M, M->e[i].ri, M->e[i].rc);
                    c = k_index(M, M->e[i].ci, M->e[i].cc);
                }
                if (r < 0 || c < 0) { nbad++; continue; }
                T.w_r[T.nw] = r; T.w_c[T.nw] = c; T.w_v[T.nw++] = M->e[i].v;
            }
            T.have_w = 1;
            if (nbad)
                info(9665, "%s %s: %d term%s name%s no box component of the k set (3 or\n"
                     "5) and %s left out.", M->kind == 1 ? "DMI" : "DMIK", M->name, nbad,
                     nbad == 1 ? "" : "s", nbad == 1 ? "s" : "", nbad == 1 ? "is" : "are");
        }
    }
    if (T.wg || T.fa || T.have_w) {
        double wmin = 0.0, wmax = 0.0, gmin = 0.0, gmax = 0.0;
        int nd = 0, ng = 0;
        for (i = 0; i < T.nw; i++)
            if (T.w_r[i] == T.w_c[i]) {
                if (!nd || T.w_v[i] < wmin) wmin = T.w_v[i];
                if (!nd || T.w_v[i] > wmax) wmax = T.w_v[i];
                nd++;
            }
        if (T.wg) for (i = 0; i < T.nj; i++) {
            if (!ng || T.wg[i] < gmin) gmin = T.wg[i];
            if (!ng || T.wg[i] > gmax) gmax = T.wg[i];
            ng++;
        }
        info(9666, "SOL 144 corrections: W2GJ %s (%.4e .. %.4e rad), FA2J %s,\n"
             "WKK %s (%d terms, diagonal %.4f .. %.4f).",
             T.wg ? "on" : "off", gmin, gmax, T.fa ? "on" : "off",
             T.have_w ? "on" : "off", T.nw, wmin, wmax);
    }
    return 0;
}

/* the AJJ of a Mach (columns of AJJL are rows of A), column-major      */
static double *aic_of_mach(double mach, int *ok)
{
    int i, r, c, nj = T.nj;
    double *a;
    *ok = 0;
    for (i = 0; i < T.nmk; i++) {
        if (fabs(T.mk[2 * i] - mach) <= 1e-5 * (1.0 + fabs(mach))) break;
    }
    if (i == T.nmk || (size_t) (i + 1) * nj > (size_t) T.ajj_ncol) return NULL;
    a = DNEW((size_t) nj * nj);
    for (r = 0; r < nj; r++) {
        const double *col = T.ajj + (size_t) (i * nj + r) * T.ajj_nrow;
        for (c = 0; c < nj; c++) a[r + (size_t) c * nj] = col[c];
    }
    *ok = 1;
    return a;
}

/* per Mach: everything the aerodynamics give on the s set. The force
 * rows (Fs, Fr, Fx) are the r dofs' generalised forces, D_a^T G_a^T P,
 * or with no SUPORT (nr = 0) the six forces at the reference point,
 * RB_a^T G_a^T P (nq rows). Column NX of the x matrices is the
 * intercept: W2GJ's downwash and FA2J's pressures.                     */
typedef struct {
    double mach;
    int    nj, nk, ns, nr, nq, nx;
    double *Qss, *Qsr, *Qsx;      /* G_s W S A^-1 D1 G_s^T etc. (ns rows) */
    double *Fs, *Fr, *Fx;         /* B W S A^-1 ... (nq rows)            */
    double *Ux;                   /* 6 x NXI, unsplined, at O, ref axes  */
    double *Ps, *Px, *Pr;         /* W S A^-1 ... at the k set (k rows)  */
    double *Ws, *Wx;              /* A^-1 ...: the box pressures (j rows) */
    double *CQss, *CQsx, *CQsr;   /* C_ss Q_ss, C_ss Q_sx, C_ss Q_sr     */
} aero_mach;

static int nq_rows(void) { return T.nr > 0 ? T.nr : 6; }

static int aero_for_mach(double mach, aero_mach *am, const double *djx)
{
    int nj = T.nj, nk = T.nk, ns = T.ns, nr = T.nr, nx = NXI, nq = nq_rows();
    int ncol = ns + nr + nx, i, j, m, okm, ok;
    double *A, *R, *P, *Gs, *B, *U;
    int    *ip;

    memset(am, 0, sizeof(*am));
    am->mach = mach; am->nj = nj; am->nk = nk; am->ns = ns; am->nr = nr;
    am->nq = nq; am->nx = nx;

    A = aic_of_mach(mach, &okm);
    if (!okm) {
        fatal(9630, "AJJL has no AIC at Mach %g.", mach);
        return 1;
    }
    ip = INEW(nj);
    ok = lu(nj, A, ip);
    if (!ok) { fatal(9631, "the AIC at Mach %g is singular.", mach); free(A); free(ip); return 1; }

    /* the right hand sides: D1 G_s^T (ns), D1 G_a^T D_a (nr), D_jx (nx,
     * the last the intercept's W2GJ)                                    */
    R = DNEW((size_t) nj * ncol);
    {
        /* G^T columns: for an a dof (row of GTKA), its k-set motion is
         * GTKA's row; D1 (j x k) is the stored k x j transposed        */
        double *gk = DNEW(nk);     /* one k vector                     */
        int     c;
        /* row access to GTKA: make a dense ns x nk copy (G_s) and the
         * nq x nk products with D_a (or RB_a) on the fly               */
        Gs = DNEW((size_t) ns * nk);
        B  = DNEW((size_t) nq * nk);
        for (j = 0; j < nk; j++) {
            for (m = T.gtka.cp[j]; m < T.gtka.cp[j + 1]; m++) {
                int arow = T.gtka.ri[m];
                double v = T.gtka.v[m];
                int l = T.a_l[arow], r = T.a_r[arow];
                if (l >= 0) {
                    int s = T.l_s[l];
                    if (s >= 0) Gs[s + (size_t) j * ns] = v;
                }
                if (nr > 0) {
                    if (l >= 0)
                        for (i = 0; i < nr; i++) B[i + (size_t) j * nq] += T.dm[(size_t) i * T.nl + l] * v;
                    else if (r >= 0)
                        B[r + (size_t) j * nq] += v;
                }
            }
            /* no SUPORT (a wind-tunnel model): the box forces splined to
             * the g set - the constrained dofs' share too, which is the
             * mount's reaction - summed through the six geometric rigid
             * body modes to the reference point                          */
            if (nr == 0)
                for (m = T.gtkg.cp[j]; m < T.gtkg.cp[j + 1]; m++) {
                    int g = T.gtkg.ri[m];
                    double v = T.gtkg.v[m];
                    for (i = 0; i < 6; i++)
                        B[i + (size_t) j * nq] += T.rbg[(size_t) i * T.luset + g] * v;
                }
        }
        /* D1 G^T x for each needed column x of G^T (s and r)          */
        for (c = 0; c < ns + nr; c++) {
            for (j = 0; j < nk; j++)
                gk[j] = (c < ns) ? Gs[c + (size_t) j * ns] : B[(c - ns) + (size_t) j * nq];
            for (j = 0; j < nj; j++) {
                double s = 0.0;
                for (m = T.d1t.cp[j]; m < T.d1t.cp[j + 1]; m++) s += T.d1t.v[m] * gk[T.d1t.ri[m]];
                R[j + (size_t) c * nj] = s;
            }
        }
        for (c = 0; c < NX; c++)
            for (j = 0; j < nj; j++) R[j + (size_t) (ns + nr + c) * nj] = djx[j + (size_t) c * nj];
        if (T.wg)
            for (j = 0; j < nj; j++) R[j + (size_t) (ns + nr + NX) * nj] = T.wg[j];
        free(gk);
    }
    lus(nj, A, ip, R, ncol);         /* R <- A^-1 R: the pressures     */
    free(A); free(ip);

    /* P = S R (k x ncol), S stored k x j by columns                   */
    P = DNEW((size_t) nk * ncol);
    for (j = 0; j < nj; j++)
        for (m = T.skj.cp[j]; m < T.skj.cp[j + 1]; m++) {
            int kk = T.skj.ri[m];
            double v = T.skj.v[m];
            int c;
            for (c = 0; c < ncol; c++) P[kk + (size_t) c * nk] += v * R[j + (size_t) c * nj];
        }
    /* HALO (corrections): WKK weights every box force and moment of the
     * theory (2-106..2-108: Q = G^T W S A^-1 D); FA2J's experimental
     * pressures (per unit q) are added unweighted, q S FA2J (2-106)     */
    if (T.have_w) {
        double *PW = DNEW((size_t) nk * ncol);
        int c, t;
        for (t = 0; t < T.nw; t++) {
            int r = T.w_r[t], cc = T.w_c[t];
            double v = T.w_v[t];
            for (c = 0; c < ncol; c++) PW[r + (size_t) c * nk] += v * P[cc + (size_t) c * nk];
        }
        free(P);
        P = PW;
    }
    if (T.fa)
        for (j = 0; j < nj; j++)
            for (m = T.skj.cp[j]; m < T.skj.cp[j + 1]; m++)
                P[T.skj.ri[m] + (size_t) (ns + nr + NX) * nk] += T.skj.v[m] * T.fa[j];
    /* the pressure coefficients, kept for APRES (the intercept's with
     * FA2J's)                                                          */
    am->Ws = DNEW((size_t) nj * ns); am->Wx = DNEW((size_t) nj * nx);
    memcpy(am->Ws, R, sizeof(double) * (size_t) nj * ns);
    memcpy(am->Wx, R + (size_t) nj * (ns + nr), sizeof(double) * (size_t) nj * nx);
    if (T.fa) for (j = 0; j < nj; j++) am->Wx[j + (size_t) nj * NX] += T.fa[j];
    free(R);

    /* the splined generalised forces on the s set and at the SUPORT    */
    {
        double *Q = DNEW((size_t) ns * ncol), *F = DNEW((size_t) nq * ncol);
        mm(ns, ncol, nk, Gs, ns, P, nk, Q, ns);
        mm(nq, ncol, nk, B, nq, P, nk, F, nq);
        am->Qss = DNEW((size_t) ns * ns); am->Qsr = DNEW((size_t) ns * nr); am->Qsx = DNEW((size_t) ns * nx);
        am->Fs  = DNEW((size_t) nq * ns); am->Fr  = DNEW((size_t) nq * nr); am->Fx  = DNEW((size_t) nq * nx);
        memcpy(am->Qss, Q, sizeof(double) * (size_t) ns * ns);
        memcpy(am->Qsr, Q + (size_t) ns * ns, sizeof(double) * (size_t) ns * nr);
        memcpy(am->Qsx, Q + (size_t) ns * (ns + nr), sizeof(double) * (size_t) ns * nx);
        memcpy(am->Fs, F, sizeof(double) * (size_t) nq * ns);
        memcpy(am->Fr, F + (size_t) nq * ns, sizeof(double) * (size_t) nq * nr);
        memcpy(am->Fx, F + (size_t) nq * (ns + nr), sizeof(double) * (size_t) nq * nx);
        free(Q); free(F);
    }
    am->Ps = DNEW((size_t) nk * ns); am->Px = DNEW((size_t) nk * nx);
    am->Pr = DNEW((size_t) nk * nr);
    memcpy(am->Ps, P, sizeof(double) * (size_t) nk * ns);
    memcpy(am->Pr, P + (size_t) nk * ns, sizeof(double) * (size_t) nk * nr);
    memcpy(am->Px, P + (size_t) nk * (ns + nr), sizeof(double) * (size_t) nk * nx);

    /* unsplined: the box forces straight to the reference point         */
    U = DNEW((size_t) 6 * nx);
    for (j = 0; j < nj && 2 * j + 1 < nk; j++) {
        const double *n = &T.bx_n[3 * j], *s = &T.bx_s[3 * j];
        double d[3], mn[3];
        int c, k2;
        for (k2 = 0; k2 < 3; k2++) d[k2] = T.bx_pk[3 * j + k2] - T.O[k2];
        cross(d, n, mn);
        for (c = 0; c < nx; c++) {
            double f = P[(2 * j) + (size_t) (ns + nr + c) * nk];
            double mom = P[(2 * j + 1) + (size_t) (ns + nr + c) * nk];
            double fb[3], mb[3];
            for (k2 = 0; k2 < 3; k2++) { fb[k2] = f * n[k2]; mb[k2] = f * mn[k2] + mom * s[k2]; }
            /* in reference axes                                         */
            for (k2 = 0; k2 < 3; k2++) {
                U[k2 + 6 * c]     += dot(&T.E[3 * k2], fb);
                U[3 + k2 + 6 * c] += dot(&T.E[3 * k2], mb);
            }
        }
    }
    am->Ux = U;
    /* C_ss times the aerodynamics: the same for every subcase of the Mach */
    am->CQss = DNEW((size_t) ns * ns); am->CQsx = DNEW((size_t) ns * nx);
    am->CQsr = DNEW((size_t) ns * nr);
    mm(ns, ns, ns, T.css, ns, am->Qss, ns, am->CQss, ns);
    mm(ns, nx, ns, T.css, ns, am->Qsx, ns, am->CQsx, ns);
    mm(ns, nr, ns, T.css, ns, am->Qsr, ns, am->CQsr, ns);
    free(P); free(Gs); free(B);
    return 0;
}

static void aero_free(aero_mach *am)
{
    free(am->Qss); free(am->Qsr); free(am->Qsx);
    free(am->Fs); free(am->Fr); free(am->Fx);
    free(am->Ux); free(am->Ps); free(am->Px); free(am->Pr); free(am->Ws); free(am->Wx);
    free(am->CQss); free(am->CQsx); free(am->CQsr);
    memset(am, 0, sizeof(*am));
}

/* the hinge moment of each control surface per k-set force: the box
 * forces' moments about the hinge line (the y axis of the surface's
 * coordinate system, through its origin), nsurf x nk                   */
static double *hinge_rows(void)
{
    int is, comp, li, k, nk = T.nk;
    double *H;
    if (aet_g.nsurf == 0) return NULL;
    H = DNEW((size_t) aet_g.nsurf * nk);
    for (is = 0; is < aet_g.nsurf; is++) {
        const aet_surf *sf = &aet_g.surf[is];
        for (comp = 0; comp < 2; comp++) {
            double ax[9], org[3], h[3];
            const aet_list *L = NULL;
            if (sf->alid[comp] <= 0) continue;
            if (!cs_axes(sf->cid[comp], ax, org)) continue;
            for (k = 0; k < 3; k++) h[k] = ax[k + 3];
            for (li = 0; li < aet_g.nlist; li++)
                if (aet_g.list[li].sid == sf->alid[comp]) { L = &aet_g.list[li]; break; }
            if (!L) continue;
            for (li = 0; li < L->n; li++) {
                int jb = box_index(L->ids[li]);
                double d[3], t[3];
                if (jb < 0 || 2 * jb + 1 >= nk) continue;
                for (k = 0; k < 3; k++) d[k] = T.bx_pk[3 * jb + k] - org[k];
                cross(d, &T.bx_n[3 * jb], t);
                H[(size_t) is * nk + 2 * jb]     += dot(t, h);
                H[(size_t) is * nk + 2 * jb + 1] += dot(&T.bx_s[3 * jb], h);
            }
        }
    }
    return H;
}

/* D_jx: the downwash of a unit value of every trim variable           */
static double *downwash(void)
{
    int nj = T.nj, c, j, k;
    double *D = DNEW((size_t) nj * (NX ? NX : 1));
    const double *xh = &T.Ea[0];            /* the flow: ACSID's x axis */
    for (c = 0; c < NX; c++) {
        xvar *v = &X[c];
        if (v->kind >= 5 && v->kind <= 10) continue;     /* accelerations */
        if (v->kind == 11) {
            const aet_surf *sf = &aet_g.surf[v->surf];
            int comp;
            for (comp = 0; comp < 2; comp++) {
                double ax[9], h[3], hx[3];
                const aet_list *L = NULL;
                int li;
                if (sf->alid[comp] <= 0) continue;
                if (!cs_axes(sf->cid[comp], ax, NULL)) {
                    fatal(9623, "AESURF %s: coordinate system %d is not a rectangular\n"
                          "system of the model.", sf->label, sf->cid[comp]);
                    free(D);
                    return NULL;
                }
                for (k = 0; k < 3; k++) h[k] = ax[k + 3];    /* its y axis */
                cross(h, xh, hx);
                for (li = 0; li < aet_g.nlist; li++)
                    if (aet_g.list[li].sid == sf->alid[comp]) { L = &aet_g.list[li]; break; }
                if (!L) {
                    fatal(9624, "AESURF %s names AELIST %d, which is not in the deck.",
                          sf->label, sf->alid[comp]);
                    free(D);
                    return NULL;
                }
                for (li = 0; li < L->n; li++) {
                    int jb = box_index(L->ids[li]);
                    if (jb < 0) {
                        fatal(9625, "AELIST %d names box %d, which no CAERO1 makes.",
                              L->sid, L->ids[li]);
                        free(D);
                        return NULL;
                    }
                    /* w = -n . (delta h x V_hat), scaled by EFF            */
                    D[jb + (size_t) c * nj] += -sf->eff * dot(&T.bx_n[3 * jb], hx);
                }
            }
            continue;
        }
        for (j = 0; j < nj; j++) {
            const double *n = &T.bx_n[3 * j];
            double r[3] = { T.bx_pc[3 * j] - T.O[0], T.bx_pc[3 * j + 1] - T.O[1],
                            T.bx_pc[3 * j + 2] - T.O[2] };
            double e[3], t[3], w = 0.0;
            int    axis = 0;
            double scale = 1.0;
            switch (v->kind) {
            case 0: axis = 1; break;                     /* ANGLEA: R2    */
            case 1: axis = 2; break;                     /* SIDES: R3     */
            case 2: axis = 0; scale = 2.0 / aet_g.refb; break;   /* ROLL  */
            case 3: axis = 1; scale = 2.0 / aet_g.refc; break;   /* PITCH */
            case 4: axis = 2; scale = 2.0 / aet_g.refb; break;   /* YAW   */
            }
            for (k = 0; k < 3; k++) e[k] = T.E[k + 3 * axis];
            /* HALO (corrections): SIDES is the sideslip angle, the wind
             * from the reference y side: a rotation about V_hat x y_ref,
             * which is +z_ref when x_ref is the flow (z up) and -z_ref in
             * MSC's NACA axes (x forward, z down; example HA144D's CY of
             * SIDES is negative, a fin's)                              */
            if (v->kind == 1) {
                double ax2[3], nn;
                cross(xh, &T.E[3], ax2);
                nn = sqrt(dot(ax2, ax2));
                if (nn > 0.0) for (k = 0; k < 3; k++) e[k] = ax2[k] / nn;
            }
            if (v->kind <= 1) {
                cross(e, xh, t);                         /* phi x V_hat   */
                w = -dot(n, t);
            } else {
                cross(e, r, t);                          /* Omega x r     */
                w = -scale * dot(n, t);
            }
            D[j + (size_t) c * nj] = w;
        }
    }
    return D;
}

/* ------------------------------------------------------------------ */
/* the print                                                           */

static void case_header_words(int icase, int *w96)
{
    int i;
    char lab[129], num[32];
    for (i = 0; i < 96; i++) w96[i] = T.case_words[96 * icase + i];
    /* the label line carries SUBCASE n at the right, as the OFP pages do */
    memcpy(lab, &w96[64], 128);
    lab[128] = '\0';
    sprintf(num, "SUBCASE %d", T.case_id[icase]);
    for (i = 104; i < 128; i++) lab[i] = ' ';
    memcpy(lab + 104, num, strlen(num) < 24 ? strlen(num) : 24);
    memcpy(&w96[64], lab, 128);
}

static const char *sym_word(int s)
{
    return s > 0 ? "SYMMETRIC" : (s < 0 ? "ANTISYMMETRIC" : "ASYMMETRIC");
}

static void print_header_block(double mach, double q, int recovery)
{
    if (recovery == 2) {
        out("");
        out("          N O N - D I M E N S I O N A L    H I N G E    M O M E N T    D E R I V A T I V E   C O E F F I C I E N T S");
        out("");
        out("");
        out("");
        out("                         CONFIGURATION = AEROSG2D     XY-SYMMETRY = %-10s   XZ-SYMMETRY = %s",
            sym_word(aet_g.symxy), sym_word(aet_g.symxz));
        out("                                         MACH = %10.4E                    Q = %10.4E", mach, q);
        return;
    }
    if (!recovery) {
        out("");
        out("    N O N - D I M E N S I O N A L   S T A B I L I T Y   A N D   C O N T R O L   D E R I V A T I V E   C O E F F I C I E N T S");
        out("");
        out("                         CONFIGURATION = AEROSG2D     XY-SYMMETRY = %-10s   XZ-SYMMETRY = %s",
            sym_word(aet_g.symxy), sym_word(aet_g.symxz));
        out("                                         MACH = %10.4E                    Q = %10.4E", mach, q);
        out("                         CHORD = %10.4E           SPAN = %10.4E            AREA = %10.4E",
            aet_g.refc, aet_g.refb, aet_g.refs);
    } else {
        out("");
        out("                                               A E R O S T A T I C   D A T A   R E C O V E R Y   O U T P U T   T A B L E S");
        out("                                         CONFIGURATION = AEROSG2D     XY-SYMMETRY = %-10s   XZ-SYMMETRY = %s",
            sym_word(aet_g.symxy), sym_word(aet_g.symxz));
        out("                                                           MACH = %13.6E                 Q = %13.6E", mach, q);
        out("                                         CHORD = %10.4E           SPAN = %10.4E             AREA = %10.4E",
            aet_g.refc, aet_g.refb, aet_g.refs);
    }
}

static void print_transform(void)
{
    int i;
    double off[3];
    /* x_ref = E^T (x_basic - O)                                       */
    for (i = 0; i < 3; i++) off[i] = -dot(&T.E[3 * i], T.O) + 0.0;
    out("");
    out("");
    out("                TRANSFORMATION FROM BASIC TO REFERENCE COORDINATES:");
    out("");
    out("              { X }        [%8.4f %8.4f %8.4f  ]  { X }         { %10.4E }",
        T.E[0], T.E[1], T.E[2], off[0]);
    out("              { Y }    =   [%8.4f %8.4f %8.4f  ]  { Y }      +  { %10.4E }",
        T.E[3], T.E[4], T.E[5], off[1]);
    out("              { Z }REF     [%8.4f %8.4f %8.4f  ]  { Z }BAS      { %10.4E }",
        T.E[6], T.E[7], T.E[8], off[2]);
    out("");
    out("");
    out("");
    out("");
}

/* one number of a derivative table, or N/A (a column that does not
 * exist: the unrestrained ones of a model with no SUPORT, as MSC)      */
static const char *num13(char *buf, double v, int na)
{
    if (na) strcpy(buf, "     N/A     ");
    else sprintf(buf, "%13.6E", v);
    return buf;
}

/* one variable's six rows; cols[6][6] are the six coefficients (rows)
 * of the six columns; unr_na: the unrestrained columns are N/A          */
static void print_var_rows(const char *label, double cols[6][6], int unr_na)
{
    static const char *cn[6] = { "CX", "CY", "CZ", "CMX", "CMY", "CMZ" };
    int r;
    char b3[16], b5[16];
    for (r = 0; r < 6; r++)
        out("    %-17s%-10s%13.6E   %13.6E    %13.6E   %s    %13.6E   %s",
            r == 0 ? label : "", cn[r], cols[0][r], cols[1][r], cols[2][r],
            num13(b3, cols[3][r], unr_na), cols[4][r], num13(b5, cols[5][r], unr_na));
    out("");
}

/* forces at the r dofs -> the six reference-axis coefficients (TR =
 * RB_r^T); with no SUPORT the forces are the six at the reference point
 * already                                                              */
static void to_coeffs(const double *fr, double q, double *c6)
{
    int i, k;
    double f[6];
    if (T.nr > 0) {
        for (i = 0; i < 6; i++) {
            double s = 0.0;
            for (k = 0; k < T.nr; k++) s += T.rbr[k + 6 * i] * fr[k];
            f[i] = s;
        }
    } else {
        for (i = 0; i < 6; i++) f[i] = fr[i];
    }
    for (i = 0; i < 6; i++) c6[i] = T.smask[i] != 0.0 ? f[i] / (q * aet_g.refs) : 0.0;
    c6[3] /= aet_g.refb; c6[4] /= aet_g.refc; c6[5] /= aet_g.refb;
}

static void ref_coeffs(const double *fref, double q, double *c6)
{
    int i;
    for (i = 0; i < 6; i++) c6[i] = T.smask[i] != 0.0 ? fref[i] / (q * aet_g.refs) : 0.0;
    c6[3] /= aet_g.refb; c6[4] /= aet_g.refc; c6[5] /= aet_g.refb;
}

/* ------------------------------------------------------------------ */

static int trim_index(int sid)
{
    int i;
    for (i = 0; i < aet_g.ntrim; i++) if (aet_g.trim[i].sid == sid) return i;
    return -1;
}

static int div_index(int sid)
{
    int i;
    for (i = 0; i < aet_g.ndiv; i++) if (aet_g.div[i].sid == sid) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* HALO (corrections): static aeroelastic divergence (MSC eqs. 2-143 ..
 * 2-145): [K_ll - q Q_ll] u_l = 0, Q_ll the aerodynamics of the
 * restrained vehicle (the l set; WKK weighted, as PFAERO's WSKJ). Q_ll
 * is zero outside the splined set s, so with C_ss the s rows of
 * K_ll^-1 E_s the nonzero roots are exactly those of
 *
 *     (C_ss Q_ss) u_s = (1/q) u_s
 *
 * a dense ns x ns real eigenproblem (LAPACK DGEEV): every root, where
 * MSC asks a complex Lanczos for some. MSC writes the problem as CEAD's
 * [K + p^2 Q] (DIVERGRS step 9) and reports p, with q = -p^2 (the
 * guide's eq. 5-1); p = i sqrt(q) here, so a positive q is a positive
 * imaginary p as in MSC's listing 5-1 and a negative one a negative
 * real p. The physically meaningful roots are the real positive q.     */
typedef struct { double qr, qi, pr, pi, mag; int order; } droot;

static int drcmp(const void *a, const void *b)
{
    double x = ((const droot *) a)->mag, y = ((const droot *) b)->mag;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void divergence(int isub, const aet_div *dv, aero_mach *AM, int nmach)
{
    int im, ns = T.ns;
    for (im = 0; im < dv->nm; im++) {
        aero_mach *am = NULL;
        int k, nroot = 0, npos = 0, nprint;
        droot *rt;
        double *wr, *wi;
        for (k = 0; k < nmach; k++)
            if (fabs(aet_g.mach[k] - dv->m[im]) <= 1e-9 * (1.0 + dv->m[im])) am = &AM[k];
        if (!am) { fatal(9667, "no aerodynamics at Mach %g (DIVERG %d).", dv->m[im], dv->sid); return; }
        wr = DNEW(ns); wi = DNEW(ns);
#ifdef N95_LAPACK
        {
            double *a = DNEW((size_t) ns * ns), wq, *work, dum = 0.0;
            int lwork = -1, info_ev = 0, one = 1;
            memcpy(a, am->CQss, sizeof(double) * (size_t) ns * ns);
            dgeev_("N", "N", &ns, a, &ns, wr, wi, &dum, &one, &dum, &one, &wq, &lwork, &info_ev, 1, 1);
            lwork = (int) wq + 1;
            work = DNEW(lwork);
            dgeev_("N", "N", &ns, a, &ns, wr, wi, &dum, &one, &dum, &one, work, &lwork, &info_ev, 1, 1);
            free(work); free(a);
            if (info_ev != 0) {
                fatal(9668, "DIVERG %d at Mach %g: the eigenvalue solution did not converge\n"
                      "(DGEEV INFO %d).", dv->sid, dv->m[im], info_ev);
                free(wr); free(wi);
                return;
            }
        }
#else
        fatal(9668, "DIVERG needs the build's LAPACK (N95_LAPACK).");
        free(wr); free(wi);
        return;
#endif
        /* q = 1/mu for every nonzero eigenvalue mu of C_ss Q_ss          */
        rt = (droot *) zalloc(sizeof(droot) * (size_t) (ns ? ns : 1));
        {
            double big = 0.0;
            for (k = 0; k < ns; k++) {
                double a = sqrt(wr[k] * wr[k] + wi[k] * wi[k]);
                if (a > big) big = a;
            }
            for (k = 0; k < ns; k++) {
                double a2 = wr[k] * wr[k] + wi[k] * wi[k];
                double qr, qi, r, ph, sr, si;
                if (sqrt(a2) <= 1e-12 * big || a2 == 0.0) continue;
                qr = wr[k] / a2; qi = -wi[k] / a2;
                /* p = i sqrt(q), the principal root                      */
                r = sqrt(sqrt(qr * qr + qi * qi));
                ph = 0.5 * atan2(qi, qr);
                sr = r * cos(ph); si = r * sin(ph);
                rt[nroot].qr = qr; rt[nroot].qi = qi;
                rt[nroot].pr = -si; rt[nroot].pi = sr;
                /* a real negative q: the negative real p, as MSC prints it */
                if (qr < 0.0 && fabs(qi) <= 1e-9 * fabs(qr)) {
                    rt[nroot].pr = -sqrt(-qr); rt[nroot].pi = 0.0;
                }
                if (qr > 0.0 && fabs(qi) <= 1e-9 * fabs(qr)) {
                    rt[nroot].pr = 0.0; rt[nroot].pi = sqrt(qr);
                }
                rt[nroot].mag = sqrt(qr * qr + qi * qi);
                rt[nroot].order = k + 1;
                nroot++;
            }
        }
        qsort(rt, (size_t) nroot, sizeof(droot), drcmp);
        /* the complex table: the roots of least |q| (twice NROOT, at
         * least ten), as a complex eigensolver would have found them      */
        nprint = 2 * dv->nroot;
        if (nprint < 10) nprint = 10;
        if (nprint > nroot) nprint = nroot;
        new_page(isub);
        out("");
        out("                                       C O M P L E X   E I G E N V A L U E   S U M M A R Y");
        out("0                ROOT     EXTRACTION                  EIGENVALUE                     FREQUENCY              DAMPING");
        out("                 NO.        ORDER             (REAL)           (IMAG)                (CYCLES)            COEFFICIENT");
        for (k = 0; k < nprint; k++) {
            double f = rt[k].pi / (2.0 * 3.14159265358979323846), d = 0.0;
            if (fabs(rt[k].pi) > 1e-12 * rt[k].mag) d = -2.0 * rt[k].pr / rt[k].pi + 0.0;
            else f = 0.0;
            if (d == 0.0) d = 0.0;
            out("                 %8d   %8d       %13.6E    %13.6E          %13.6E         %13.6E",
                k + 1, rt[k].order, rt[k].pr, rt[k].pi, f, d);
        }
        out("");
        /* the divergence table: the real positive q, the NROOT lowest     */
        out("");
        out("                                        D I V E R G E N C E   S U M M A R Y");
        out("");
        out("                                 MACH NUMBER = %12.6f       METHOD = DGEEV, SPLINED SET (%d)", dv->m[im], ns);
        out("");
        out("                                 ROOT           DIVERGENCE                EIGENVALUE");
        out("                                  NO.        DYNAMIC PRESSURE         REAL            IMAGINARY");
        out("");
        for (k = 0; k < nroot && npos < dv->nroot; k++) {
            if (rt[k].qr <= 0.0 || fabs(rt[k].qi) > 1e-6 * rt[k].mag) continue;
            out("                                %4d          %13.6E      %13.6E     %13.6E",
                k + 1, rt[k].qr, rt[k].pr, rt[k].pi);
            npos++;
        }
        if (npos == 0)
            out("                                  NO DIVERGENCE ROOT: NO REAL POSITIVE DYNAMIC PRESSURE OF THE %d ROOTS", nroot);
        out("");
        free(rt); free(wr); free(wi);
    }
}

static int mode2(void)
{
    int ns = T.ns, nr = T.nr, nq, nk, nj, nsub, isub, i, j, k, c, im;
    double *djx = NULL, *HM = NULL;
    aero_mach *AM = NULL;
    int nmach = aet_g.nmach;

    nk = T.nk; nj = T.nj;
    nq = nq_rows();
    /* AMG writes SKJ once per (Mach, k) pair, NJ columns each; the
     * doublet lattice's is geometry alone, so the first block serves     */
    if (T.gtka.nc != nk || T.skj.nr != nk || nj <= 0 || T.skj.nc < nj ||
        T.skj.nc % nj != 0 || T.d1t.nr != nk || T.d1t.nc != nj) {
        fatal(9632, "the aerodynamic matrices do not agree in size (GTKA %d x %d,\n"
              "SKJ %d x %d, D1JK %d x %d, NJ %d, NK %d).",
              T.gtka.nr, T.gtka.nc, T.skj.nr, T.skj.nc, T.d1t.nr, T.d1t.nc, nj, nk);
        return 1;
    }
    if (T.nbox != nj) {
        fatal(9633, "ACPT describes %d boxes and AJJL %d.", T.nbox, nj);
        return 1;
    }
    if (find_box_ids() != nj) {
        fatal(9634, "the box points of the aerodynamic model (GPLA, USETA) do not\n"
              "number NJ = %d.", nj);
        return 1;
    }
    if (!T.have_css || (nr > 0 && (!T.have_xms || !T.have_mxm || !T.have_dm || !T.have_mr))) {
        fatal(9635, "a structural matrix the trim needs was not available (CLS %d,\n"
              "XM %d, MXM %d, DM %d, MR %d).", T.have_css, T.have_xms, T.have_mxm,
              T.have_dm, T.have_mr);
        return 1;
    }
    if (nr > 0 && (T.dm_nrow != T.nl || T.dm_ncol != nr)) {
        fatal(9661, "the rigid body modes DM (%d x %d) do not match the l and r\n"
              "sets (%d x %d).", T.dm_nrow, T.dm_ncol, T.nl, nr);
        return 1;
    }
    if (build_vars()) return 1;
    box_geometry();
    if (corrections()) return 1;
    info(9650, "SOL 144: the a set has %d dofs (l %d, r %d), %d of them splined (the s\n"
         "set); %d boxes, %d k-set dofs; %d trim variables, %d subcases, %d Mach\n"
         "number%s.", T.na, T.nl, T.nr, T.ns, nj, nk, NX, T.ncase, aet_g.nmach,
         aet_g.nmach == 1 ? "" : "s");
    djx = downwash();
    if (!djx) return 1;
    HM = hinge_rows();
    blas_threads();

    /* the aerodynamics of each Mach                                   */
    AM = (aero_mach *) zalloc(sizeof(aero_mach) * (size_t) (nmach ? nmach : 1));
    for (im = 0; im < nmach; im++)
        if (aero_for_mach(aet_g.mach[im], &AM[im], djx)) { free(djx); return 1; }

    nsub = T.ncase;
    T.nsub = nsub;
    free(T.pla); free(T.uddt); free(T.pgt);
    T.pla  = DNEW((size_t) T.nl * nsub);
    T.uddt = DNEW((size_t) (nr ? nr : 1) * nsub);
    T.pgt  = DNEW((size_t) T.luset * nsub);

    for (isub = 0; isub < nsub; isub++) {
        int sid = -1, dsid = -1, it, nfix = 0, nlnk = 0, neq, info_ok, have_sub = 0;
        const aet_trimc *tc;
        aero_mach *am = NULL;
        double q, qe, *Z, *RHS, *Usx, *Usr, *Usa;
        int *ipz;
        double Ax[6 * 65];            /* RB_r e_x / AUNITS, nr x NXI       */
        double *FR, *IR, *UNR, *ux, *Mx, *Mr, *Ma, *Fr, *Fa;
        int    *status;               /* 0 free 1 fixed 2 linked           */
        double *cpk = NULL;           /* APRES/AEROF: cp (nj), forces (nk)  */
        double *URU = NULL;           /* unrestrained u_r, u''_r per variable */
        int     unr_na = (nr == 0);   /* no SUPORT: no unrestrained columns */

        for (i = 0; i < aet_g.nsub; i++)
            if (aet_g.sub_id[i] == T.case_id[isub]) {
                sid = aet_g.sub_trim[i]; dsid = aet_g.sub_div[i]; have_sub = 1; break;
            }
        if (!have_sub && aet_g.nsub == nsub) { sid = aet_g.sub_trim[isub]; dsid = aet_g.sub_div[isub]; }

        /* a divergence subcase without a trim                            */
        if (sid < 0 && dsid >= 0) {
            int id = div_index(dsid);
            if (id < 0) {
                fatal(9669, "subcase %d selects DIVERG = %d, which the deck does not define.",
                      T.case_id[isub], dsid);
                free(djx); return 1;
            }
            divergence(isub, &aet_g.div[id], AM, nmach);
            if (T.fatal) { free(djx); return 1; }
            continue;
        }
        it = trim_index(sid);
        if (it < 0) {
            fatal(9640, "subcase %d selects no TRIM set the deck defines (TRIM = %d).",
                  T.case_id[isub], sid);
            free(djx); return 1;
        }
        tc = &aet_g.trim[it];
        for (im = 0; im < nmach; im++)
            if (fabs(aet_g.mach[im] - tc->mach) <= 1e-9 * (1.0 + tc->mach)) am = &AM[im];
        if (!am) { fatal(9641, "no aerodynamics at Mach %g.", tc->mach); free(djx); return 1; }
        q  = tc->q;
        qe = tc->aeqr * q;
        if (NX > 64) { fatal(9642, "more than 64 trim variables."); free(djx); return 1; }

        /* the accelerations: u''_r = RB_r (URDD / AUNITS)               */
        memset(Ax, 0, sizeof(Ax));
        for (c = 0; c < NX; c++)
            if (is_urdd(c))
                for (i = 0; i < nr; i++)
                    Ax[i + nr * c] = T.rbr[i + 6 * (X[c].kind - 5)] / aet_g.aunits;

        /* the restrained elastic solves on the s set                      */
        Z = DNEW((size_t) ns * ns);
        memcpy(Z, am->CQss, sizeof(double) * (size_t) ns * ns);
        for (j = 0; j < ns; j++)
            for (i = 0; i < ns; i++)
                Z[i + (size_t) j * ns] = (i == j ? 1.0 : 0.0) - qe * Z[i + (size_t) j * ns];
        ipz = INEW(ns);
        if (!lu(ns, Z, ipz)) {
            fatal(9643, "subcase %d: K_ll - q Q_ll is singular at q = %g - the\n"
                  "structure diverges at or below this dynamic pressure.",
                  T.case_id[isub], q);
            free(Z); free(ipz); free(djx); return 1;
        }
        RHS = DNEW((size_t) ns * (NXI + nr + nr));
        memcpy(RHS, am->CQsx, sizeof(double) * (size_t) ns * NXI);
        memcpy(RHS + (size_t) ns * NXI, am->CQsr, sizeof(double) * (size_t) ns * nr);
        for (j = 0; j < ns * NXI; j++) RHS[j] *= q;
        for (j = 0; j < ns * nr; j++) RHS[(size_t) ns * NXI + j] *= qe;
        for (j = 0; j < ns * nr; j++) RHS[(size_t) ns * (NXI + nr) + j] = -T.xms[j];
        lus(ns, Z, ipz, RHS, NXI + nr + nr);
        Usx = RHS; Usr = RHS + (size_t) ns * NXI; Usa = RHS + (size_t) ns * (NXI + nr);

        /* restrained elastic forces at the r dofs (or the reference point
         * when there is no SUPORT), per variable and the intercept         */
        FR = DNEW((size_t) nq * NXI); IR = DNEW((size_t) nq * NXI);
        Fa = DNEW((size_t) nq * nr); Fr = DNEW((size_t) nq * nr);
        mm(nq, nr, ns, am->Fs, nq, Usa, ns, Fa, nq);
        for (j = 0; j < nq * nr; j++) Fa[j] *= qe;
        mm(nq, nr, ns, am->Fs, nq, Usr, ns, Fr, nq);
        for (j = 0; j < nq * nr; j++) Fr[j] = qe * (am->Fr[j] + Fr[j]);
        mm(nq, NXI, ns, am->Fs, nq, Usx, ns, FR, nq);
        for (c = 0; c < NXI; c++) {
            if (is_urdd(c)) {
                for (i = 0; i < nq; i++) {
                    double s = 0.0, si = 0.0;
                    for (k = 0; k < nr; k++) {
                        s  += Fa[i + nq * k] * Ax[k + nr * c];
                        si += T.mr[i + 6 * k] * Ax[k + nr * c];
                    }
                    FR[i + nq * c] = s;
                    IR[i + nq * c] = si;
                }
            } else {
                for (i = 0; i < nq; i++)
                    FR[i + nq * c] = q * am->Fx[i + nq * c] + qe * FR[i + nq * c];
            }
        }

        /* the trim: equilibrium of the restrained vehicle, the fixed
         * values and the links; the intercept (column NX, fixed at 1)
         * goes to the right hand side                                     */
        ux = DNEW(NX ? NX : 1);
        status = INEW(NX ? NX : 1);
        {
            double *M = DNEW((size_t) NX * NX + 1), *rhs = DNEW(NX + 1);
            int     row = 0, *ipm = INEW(NX + 1);
            for (c = 0; c < NX; c++) status[c] = 0;
            for (i = 0; i < nr && row < NX; i++, row++) {
                for (c = 0; c < NX; c++) M[row + (size_t) NX * c] = FR[i + nq * c] - IR[i + nq * c];
                rhs[row] = -FR[i + nq * NX];
            }
            for (k = 0; k < tc->n; k++) {
                int x = find_x(tc->lab[k]);
                if (x < 0) {
                    fatal(9644, "TRIM %d names %s, which no AESTAT or AESURF defines.",
                          tc->sid, tc->lab[k]);
                    free(djx); return 1;
                }
                if (row >= NX) { row++; nfix++; continue; }
                M[row + (size_t) NX * x] = 1.0;
                rhs[row] = tc->ux[k];
                status[x] = 1;
                row++; nfix++;
            }
            for (k = 0; k < aet_g.nlink; k++) {
                const aet_link *L = &aet_g.link[k];
                int xd, li;
                if (L->id != 0 && L->id != tc->sid) continue;
                xd = find_x(L->dep);
                if (xd < 0) {
                    fatal(9645, "AELINK %d: %s is not a trim variable.", L->id, L->dep);
                    free(djx); return 1;
                }
                if (row >= NX) { row++; nlnk++; continue; }
                M[row + (size_t) NX * xd] = 1.0;
                for (li = 0; li < L->n; li++) {
                    int xi = find_x(L->ind[li]);
                    if (xi < 0) {
                        fatal(9645, "AELINK %d: %s is not a trim variable.", L->id, L->ind[li]);
                        free(djx); return 1;
                    }
                    M[row + (size_t) NX * xi] += aet_g.aelink_sign * L->c[li];
                }
                status[xd] = 2;
                row++; nlnk++;
            }
            neq = nr + nfix + nlnk;
            if (neq != NX) {
                fatal(9646, "subcase %d (TRIM %d): %d SUPORT dofs + %d fixed trim\n"
                      "variables + %d AELINKs = %d equations for %d trim variables.\n"
                      "A trim needs as many of each (MSC eq. 3-2: nr + nt + nael = nx).",
                      T.case_id[isub], tc->sid, nr, nfix, nlnk, neq, NX);
                free(djx); return 1;
            }
            info_ok = NX == 0 ? 1 : lu(NX, M, ipm);
            if (!info_ok) {
                fatal(9647, "subcase %d (TRIM %d): the trim equations are singular: the\n"
                      "free variables cannot balance the rigid body equations (e.g.\n"
                      "no free variable acts on one of them).",
                      T.case_id[isub], tc->sid);
                free(djx); return 1;
            }
            memcpy(ux, rhs, sizeof(double) * (size_t) NX);
            if (NX > 0) lus(NX, M, ipm, ux, 1);
            /* a fixed variable is its TRIM value, not the solve's roundoff */
            for (k = 0; k < tc->n; k++) {
                int x = find_x(tc->lab[k]);
                if (x >= 0) ux[x] = tc->ux[k] + 0.0;
            }
            free(M); free(rhs); free(ipm);
        }

        /* the accelerations and the deformation at trim                    */
        {
            double ur[6] = { 0 }, *us = DNEW(ns), *y = DNEW(ns), *pk = DNEW(nk);
            for (c = 0; c < NX; c++)
                for (i = 0; i < nr; i++) ur[i] += Ax[i + nr * c] * ux[c];
            for (c = 0; c < NXI; c++) {
                double u = c < NX ? ux[c] : 1.0;
                if (is_urdd(c)) continue;
                for (i = 0; i < ns; i++) us[i] += Usx[i + (size_t) ns * c] * u;
            }
            for (k = 0; k < nr; k++)
                for (i = 0; i < ns; i++) us[i] += Usa[i + (size_t) ns * k] * ur[k];
            /* PLA = E (q Q_sx u_x + qe Q_ss u_s)                           */
            for (i = 0; i < ns; i++) {
                double s = 0.0;
                for (c = 0; c < NXI; c++) s += q * am->Qsx[i + (size_t) ns * c] * (c < NX ? ux[c] : 1.0);
                for (j = 0; j < ns; j++) s += qe * am->Qss[i + (size_t) ns * j] * us[j];
                y[i] = s;
            }
            for (i = 0; i < ns; i++) T.pla[(size_t) isub * T.nl + T.s_l[i]] = y[i];
            for (i = 0; i < nr; i++) T.uddt[(size_t) isub * nr + i] = ur[i];
            /* the k set forces at trim, and the g set loads (OLOAD)          */
            for (k = 0; k < nk; k++) {
                double s = 0.0;
                for (c = 0; c < NXI; c++) s += q * am->Px[k + (size_t) nk * c] * (c < NX ? ux[c] : 1.0);
                for (j = 0; j < ns; j++) s += qe * am->Ps[k + (size_t) nk * j] * us[j];
                pk[k] = s;
            }
            /* APRES / AEROF: the box pressure coefficients at trim, printed
             * after the trim variables                                     */
            if (aet_g.want_apres || aet_g.want_aerof) {
                cpk = DNEW(nj + nk);
                for (j = 0; j < nj; j++) {
                    double s = 0.0;
                    for (c = 0; c < NXI; c++) s += am->Wx[j + (size_t) nj * c] * (c < NX ? ux[c] : 1.0);
                    for (i = 0; i < ns; i++) s += (qe / q) * am->Ws[j + (size_t) nj * i] * us[i];
                    cpk[j] = s;
                }
                for (k = 0; k < nk; k++) cpk[nj + k] = pk[k];
            }
            {
                double *pg = T.pgt + (size_t) isub * T.luset;
                for (k = 0; k < nk; k++) {
                    int m2;
                    if (pk[k] == 0.0) continue;
                    for (m2 = T.gtkg.cp[k]; m2 < T.gtkg.cp[k + 1]; m2++)
                        pg[T.gtkg.ri[m2]] += T.gtkg.v[m2] * pk[k];
                }
                if (T.have_mdg)
                    for (k = 0; k < nr; k++)
                        for (i = 0; i < T.luset; i++)
                            pg[i] -= T.mdg[(size_t) k * T.luset + i] * ur[k];
            }
            free(us); free(y); free(pk);
        }

        /* the unrestrained (mean axis) derivatives, per aero variable and
         * the intercept                                                    */
        Mx = DNEW((size_t) nr * NXI); Mr = DNEW((size_t) nr * nr); Ma = DNEW((size_t) nr * nr);
        UNR = DNEW((size_t) nq * NXI);
        URU = DNEW((size_t) 2 * nr * NXI);
        if (nr > 0) {
            double *t1 = DNEW((size_t) ns * (NXI > nr ? NXI : nr));
            /* Mx = XM_s^T (q Q_sx + qe Q_ss Us_x)                          */
            mm(ns, NXI, ns, am->Qss, ns, Usx, ns, t1, ns);
            for (j = 0; j < ns * NXI; j++) t1[j] = q * am->Qsx[j] + qe * t1[j];
            for (c = 0; c < NXI; c++)
                if (is_urdd(c))
                    for (i = 0; i < ns; i++) t1[i + (size_t) ns * c] = 0.0;
            mtm(nr, NXI, ns, T.xms, ns, t1, ns, Mx, nr);
            /* Mr = XM_s^T qe (Q_sr + Q_ss Us_r)                            */
            mm(ns, nr, ns, am->Qss, ns, Usr, ns, t1, ns);
            for (j = 0; j < ns * nr; j++) t1[j] = qe * (am->Qsr[j] + t1[j]);
            mtm(nr, nr, ns, T.xms, ns, t1, ns, Mr, nr);
            /* Ma = -MXM + qe XM_s^T Q_ss Us_a                              */
            mm(ns, nr, ns, am->Qss, ns, Usa, ns, t1, ns);
            for (j = 0; j < ns * nr; j++) t1[j] *= qe;
            mtm(nr, nr, ns, T.xms, ns, t1, ns, Ma, nr);
            for (j = 0; j < nr; j++)
                for (i = 0; i < nr; i++) Ma[i + nr * j] -= T.mxm[i + 6 * j];
            free(t1);
        }
        if (nr > 0) {
            /* [m_r + Mr, Ma; -Fr, m_r - Fa] [u_r; u''_r] = [-Mx; FR] u_x   */
            int    n2 = 2 * nr, ip2[12];
            double S[144], rhs2[12];
            memset(S, 0, sizeof(S));
            for (i = 0; i < nr; i++)
                for (j = 0; j < nr; j++) {
                    S[i + n2 * j]               = T.mr[i + 6 * j] + Mr[i + nr * j];
                    S[i + n2 * (nr + j)]        = Ma[i + nr * j];
                    S[(nr + i) + n2 * j]        = -Fr[i + nq * j];
                    S[(nr + i) + n2 * (nr + j)] = T.mr[i + 6 * j] - Fa[i + nq * j];
                }
            if (!lu(n2, S, ip2)) {
                info(9648, "subcase %d: the mean axis system is singular; the\n"
                     "unrestrained derivatives are left zero.", T.case_id[isub]);
            } else {
                for (c = 0; c < NXI; c++) {
                    if (is_urdd(c)) continue;
                    for (i = 0; i < nr; i++) {
                        rhs2[i] = -Mx[i + nr * c];
                        rhs2[nr + i] = FR[i + nq * c];
                    }
                    lus(n2, S, ip2, rhs2, 1);
                    for (i = 0; i < 2 * nr; i++) URU[i + 2 * nr * c] = rhs2[i];
                    /* m_r u''_r: the force on the free vehicle             */
                    for (i = 0; i < nr; i++) {
                        double s = 0.0;
                        for (k = 0; k < nr; k++) s += T.mr[i + 6 * k] * rhs2[nr + k];
                        UNR[i + nq * c] = s;
                    }
                }
            }
        }

        /* ---- the print of the subcase -------------------------------- */
        new_page(isub);
        print_header_block(tc->mach, q, 0);
        print_transform();
        out("    CONTROLLER STATE: INTERCEPT ONLY, ALL CONTROLLERS ARE ZERO");
        out("");
        out("    TRIM VARIABLE   COEFFICIENT              RIGID                         ELASTIC                          INERTIAL");
        out("                                   UNSPLINED        SPLINED       RESTRAINED      UNRESTRAINED     RESTRAINED      UNRESTRAINED");
        out("");
        {
            double cols[6][6];
            /* the intercept (REF. COEFF.) first, then the variables       */
            int ic;
            for (ic = 0; ic <= NX; ic++) {
                double fr[6];
                int urdd;
                c = ic == 0 ? NX : ic - 1;
                urdd = is_urdd(c);
                memset(cols, 0, sizeof(cols));
                if (!urdd) {
                    ref_coeffs(&am->Ux[6 * c], 1.0, cols[0]);
                    for (i = 0; i < nq; i++) fr[i] = am->Fx[i + nq * c];
                    to_coeffs(fr, 1.0, cols[1]);
                }
                for (i = 0; i < nq; i++) fr[i] = FR[i + nq * c];
                to_coeffs(fr, q, cols[2]);
                if (!urdd) {
                    if (nr > 0) {
                        for (i = 0; i < nq; i++) fr[i] = UNR[i + nq * c];
                        to_coeffs(fr, q, cols[3]);
                        to_coeffs(fr, q, cols[5]);
                    }
                } else {
                    for (i = 0; i < nq; i++) fr[i] = IR[i + nq * c];
                    to_coeffs(fr, q, cols[4]);
                }
                print_var_rows(c == NX ? "REF. COEFF." : X[c].label, cols, unr_na);
            }
        }

        /* the hinge moment derivatives of each control surface              */
        if (HM) {
            int is;
            double *fk = DNEW(nk), *tv = DNEW(ns);
            char b1[16];
            print_header_block(tc->mach, q, 2);
            for (is = 0; is < aet_g.nsurf; is++) {
                const double *h = HM + (size_t) is * nk;
                double den = q * aet_g.surf[is].crefc * aet_g.surf[is].crefs;
                out("");
                out("          CONTROL SURFACE = %-8s       REFERENCE CHORD LENGTH = %13.6E     REFERENCE AREA = %13.6E",
                    aet_g.surf[is].label, aet_g.surf[is].crefc, aet_g.surf[is].crefs);
                out("");
                out("              TRIM VARIABLE               RIGID                             ELASTIC                            INERTIAL");
                out("                                                                 RESTRAINED      UNRESTRAINED         RESTRAINED      UNRESTRAINED");
                int ic;
                for (ic = 0; ic <= NX; ic++) {
                    int urdd;
                    double hr = 0.0, he = 0.0, hu = 0.0;
                    c = ic == 0 ? NX : ic - 1;
                    urdd = is_urdd(c);
                    /* rigid: the trim variable's box forces (per unit q)      */
                    if (!urdd) for (k = 0; k < nk; k++) hr += h[k] * am->Px[k + (size_t) nk * c] * q;
                    /* elastic restrained: with the deformation they make       */
                    for (i = 0; i < ns; i++) {
                        double s = 0.0;
                        if (urdd) for (k = 0; k < nr; k++) s += Usa[i + (size_t) ns * k] * Ax[k + nr * c];
                        else s = Usx[i + (size_t) ns * c];
                        tv[i] = s;
                    }
                    for (k = 0; k < nk; k++) {
                        double s = urdd ? 0.0 : q * am->Px[k + (size_t) nk * c];
                        for (i = 0; i < ns; i++) s += qe * am->Ps[k + (size_t) nk * i] * tv[i];
                        fk[k] = s;
                        he += h[k] * s;
                    }
                    /* elastic unrestrained: about the mean axes                */
                    if (!urdd && nr > 0) {
                        const double *ur = URU + 2 * nr * c, *ua = ur + nr;
                        for (i = 0; i < ns; i++) {
                            double s = Usx[i + (size_t) ns * c];
                            for (k = 0; k < nr; k++)
                                s += Usr[i + (size_t) ns * k] * ur[k] + Usa[i + (size_t) ns * k] * ua[k];
                            tv[i] = s;
                        }
                        for (k = 0; k < nk; k++) {
                            double s = q * am->Px[k + (size_t) nk * c];
                            int m2;
                            for (i = 0; i < ns; i++) s += qe * am->Ps[k + (size_t) nk * i] * tv[i];
                            for (m2 = 0; m2 < nr; m2++) s += qe * am->Pr[k + (size_t) nk * m2] * ur[m2];
                            hu += h[k] * s;
                        }
                    }
                    out("              %-19s%14.6E%29.6E   %s%21.6E%16.6E",
                        c == NX ? "AT REFERENCE" : X[c].label,
                        hr / den, he / den, num13(b1, hu / den, unr_na), 0.0, 0.0);
                }
            }
            out("");
            free(fk); free(tv);
        }

        /* the trim variables                                                */
        print_header_block(tc->mach, q, 1);
        out("");
        out("                     TRIM ALGORITHM USED: LINEAR TRIM SOLUTION WITHOUT REDUNDANT CONTROL SURFACES.");
        out("");
        out("");
        out("");
        out("                                                   AEROELASTIC TRIM VARIABLES");
        out("");
        out("                         ID     LABEL               TYPE        TRIM STATUS       VALUE OF UX");
        out("");
        out("");
        out("                                INTERCEPT     RIGID BODY            FIXED     %13.6E", 1.0);
        for (c = 0; c < NX; c++) {
            static const char *units[] = { "RADIANS", "RADIANS", "NONDIMEN. RATE",
                "NONDIMEN. RATE", "NONDIMEN. RATE", "LOAD FACTOR", "LOAD FACTOR",
                "LOAD FACTOR", "RAD/S/S PER G", "RAD/S/S PER G", "RAD/S/S PER G",
                "RADIANS" };
            const char *st = status[c] == 1 ? "FIXED" : (status[c] == 2 ? "LINKED" : "FREE");
            out("                   %8d     %-8s %15s %16s     %13.6E     %s",
                X[c].id, X[c].label, X[c].kind == 11 ? "CONTROL SURFACE" : "RIGID BODY",
                st, ux[c], units[X[c].kind]);
        }
        out("");
        out("");

        /* the load resultants at the reference point (aerodynamic,
         * inertial, sum): what balances                                     */
        {
            double fa[6] = { 0 }, fi[6] = { 0 }, ref_a[6], ref_i[6], ur[6] = { 0 };
            for (i = 0; i < nr; i++) ur[i] = T.uddt[(size_t) isub * nr + i];
            for (c = 0; c < NXI; c++)
                for (i = 0; i < nq; i++) fa[i] += FR[i + nq * c] * (c < NX ? ux[c] : 1.0);
            for (i = 0; i < nr; i++) {
                double s = 0.0;
                for (k = 0; k < nr; k++) s += T.mr[i + 6 * k] * ur[k];
                fi[i] = -s;
            }
            for (i = 0; i < 6; i++) {
                double sa = 0.0, si = 0.0;
                if (nr > 0) {
                    for (k = 0; k < nr; k++) { sa += T.rbr[k + 6 * i] * fa[k]; si += T.rbr[k + 6 * i] * fi[k]; }
                } else {
                    sa = fa[i];
                }
                ref_a[i] = T.smask[i] != 0.0 ? sa : 0.0;
                ref_i[i] = T.smask[i] != 0.0 ? si : 0.0;
            }
            out("                         TRIMMED LOAD RESULTANTS ABOUT THE REFERENCE POINT, IN REFERENCE AXES");
            out("");
            out("      %-22s%15s%15s%15s%15s%15s%15s", "", "FX", "FY", "FZ", "MX", "MY", "MZ");
            out("      %-22s%15.6E%15.6E%15.6E%15.6E%15.6E%15.6E", "AERODYNAMIC (ELASTIC)",
                ref_a[0], ref_a[1], ref_a[2], ref_a[3], ref_a[4], ref_a[5]);
            out("      %-22s%15.6E%15.6E%15.6E%15.6E%15.6E%15.6E", "INERTIAL (-M U'')",
                ref_i[0], ref_i[1], ref_i[2], ref_i[3], ref_i[4], ref_i[5]);
            out("      %-22s%15.6E%15.6E%15.6E%15.6E%15.6E%15.6E", "SUM",
                ref_a[0] + ref_i[0], ref_a[1] + ref_i[1], ref_a[2] + ref_i[2],
                ref_a[3] + ref_i[3], ref_a[4] + ref_i[4], ref_a[5] + ref_i[5]);
            out("");
        }

        if (cpk) {
            if (aet_g.want_apres) {
                print_header_block(tc->mach, q, 1);
                out("");
                out("                                                          AERODYNAMIC PRESSURES ON THE AERODYNAMIC ELEMENTS");
                out("");
                out("                                                                                 AERODYNAMIC PRES.          AERODYNAMIC");
                out("                                                        GRID   LABEL              COEFFICIENTS                 PRESSURES");
                for (j = 0; j < nj; j++)
                    out("                                                    %8d     LS              %13.6E             %13.6E",
                        T.box_id[j], cpk[j], q * cpk[j]);
                out("");
                out("                      *** LABEL NOTATIONS:     LS = LIFTING SURFACE");
                out("");
            }
            if (aet_g.want_aerof) {
                print_header_block(tc->mach, q, 1);
                out("");
                out("                                                               AERODYNAMIC FORCES ON THE AERODYNAMIC ELEMENTS");
                out("");
                out("              GROUP    GRID ID   LABEL           T1                      T2                   T3                    R1                   R2              R3");
                for (j = 0; j < nj; j++)
                    out("                  1   %8d    LS      %13.6E        %13.6E         %13.6E        %13.6E        %13.6E  %13.6E",
                        T.box_id[j], 0.0, 0.0, cpk[nj + 2 * j], 0.0, cpk[nj + 2 * j + 1], 0.0);
                out("");
            }
            free(cpk);
        }
        free(Z); free(ipz); free(RHS); free(FR); free(IR); free(Fa); free(Fr);
        free(ux); free(status); free(Mx); free(Mr); free(Ma); free(UNR); free(URU);

        /* a divergence analysis in the same subcase                        */
        if (dsid >= 0) {
            int id = div_index(dsid);
            if (id < 0) {
                fatal(9669, "subcase %d selects DIVERG = %d, which the deck does not define.",
                      T.case_id[isub], dsid);
                free(djx); return 1;
            }
            divergence(isub, &aet_g.div[id], AM, nmach);
            if (T.fatal) { free(djx); return 1; }
        }
    }
    for (im = 0; im < nmach; im++) aero_free(&AM[im]);
    free(AM);
    free(djx);
    free(HM);
    return 0;
}

void n95trn_(const int *mode, int *ierr)
{
    int rc = 0;
    if (T.fatal) { *ierr = 1; return; }
    if (!aet_g.active) {
        fatal(9600, "AETRIM runs only in a SOL 144 deck translated by nastran95ase\n"
              "(the trim cards are read by its front end).");
        *ierr = 1;
        return;
    }
    if (*mode == 1) rc = mode1();
    else if (*mode == 2) rc = mode2();
    else { fatal(9609, "AETRIM mode %d.", *mode); rc = 1; }
    *ierr = (rc || T.fatal) ? 1 : 0;
}

/* the size of an output (nrow 0: none)                                 */
void n95tos_(const int *kind, int *nrow, int *ncol, int *form)
{
    *nrow = *ncol = 0; *form = 2;
    switch (*kind) {
    case O_ES:   *nrow = T.nl;    *ncol = T.ns;   break;
    case O_DG:   *nrow = T.luset; *ncol = T.nr;   break;
    case O_PLA:  *nrow = T.nl;    *ncol = T.nsub; break;
    case O_UDDT: *nrow = T.nr;    *ncol = T.nsub; break;
    case O_PGT:  *nrow = T.luset; *ncol = T.nsub; break;
    default: break;
    }
}

void n95tgc_(const int *kind, const int *jcol, double *col)
{
    int j = *jcol - 1, i;
    switch (*kind) {
    case O_ES:
        for (i = 0; i < T.nl; i++) col[i] = 0.0;
        if (j < T.ns) col[T.s_l[j]] = 1.0;
        break;
    case O_DG:
        for (i = 0; i < T.luset; i++) col[i] = T.dg[(size_t) j * T.luset + i];
        break;
    case O_PLA:
        for (i = 0; i < T.nl; i++) col[i] = T.pla[(size_t) j * T.nl + i];
        break;
    case O_UDDT:
        for (i = 0; i < T.nr; i++) col[i] = T.uddt[(size_t) j * T.nr + i];
        break;
    case O_PGT:
        for (i = 0; i < T.luset; i++) col[i] = T.pgt[(size_t) j * T.luset + i];
        break;
    default: break;
    }
}

/* the lines: how many; line i into 50 words (200 characters, blank
 * padded); page >= 0 when the line starts a new page                   */
void n95tnl_(int *n) { *n = T.nln; }

void n95tln_(const int *i, int *w50, int *page)
{
    char buf[201];
    int  k = *i - 1;
    memset(buf, ' ', 200);
    buf[200] = '\0';
    if (k >= 0 && k < T.nln) {
        size_t n = strlen(T.ln[k]);
        if (n > 200) n = 200;
        memcpy(buf, T.ln[k], n);
        *page = T.ln_page[k];
    } else *page = -1;
    memcpy(w50, buf, 200);
}

/* the title, subtitle and label words (96) of the page of subcase
 * (0-based) icase, SUBCASE n on the label line                          */
void n95tpg_(const int *icase, int *w96)
{
    if (*icase >= 0 && *icase < T.ncase) case_header_words(*icase, w96);
    else memset(w96, ' ', 96 * 4);
}
