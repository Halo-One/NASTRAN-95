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
    double  rbr[36];        /* nr x 6 (nr = 6)                          */
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
    case K_DM:   free(T.dm);  T.dm  = DNEW((size_t) T.nl * T.nr);  T.have_dm  = mat_nrow > 0; break;
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
        if (j < T.nr && mat_nrow == T.nl)
            for (i = 0; i < T.nl; i++) T.dm[(size_t) j * T.nl + i] = col[(size_t) i * stride];
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

/* ------------------------------------------------------------------ */
/* mode 1: the sets, the s-set, ES and DG                              */

static int mode1(void)
{
    int g, a, i, j, k, m;
    double ax[9];

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
    if (T.nr != 6) {
        fatal(9602, "SOL 144 here needs a free-flying model SUPORTed on six\n"
              "independent rigid body degrees of freedom (SUPORT or SUPORT1),\n"
              "and the model has %d. Half models (SYMXZ) are not supported yet.",
              T.nr);
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
    /* RB_r (6 x 6): its rows are the r dofs                            */
    for (i = 0; i < 6; i++)
        for (m = 0; m < 6; m++)
            T.rbr[i + 6 * m] = T.rbg[(size_t) m * T.luset + T.a_g[T.r_a[i]]];
    {
        double lr[36];
        int    ip[6];
        memcpy(lr, T.rbr, sizeof(lr));
        if (!lu(6, lr, ip) || fabs(lr[0] * lr[7] * lr[14] * lr[21] * lr[28] * lr[35]) < 1e-12) {
            fatal(9607, "the SUPORT degrees of freedom do not define the six rigid body\n"
                  "motions independently (their rigid body matrix is singular).");
            return 1;
        }
        /* DG = RB_g RB_r^-1: (RB_r^-T RB_g^T)^T; solve RB_r^T X = RB_g^T */
        free(T.dg);
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
    }
    (void) ax;
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
static int   NX;

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

static int build_vars(void)
{
    int i;
    free(X);
    NX = aet_g.nstat + aet_g.nsurf;
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
    if (NX == 0) { fatal(9622, "there are no AESTAT or AESURF trim variables."); return 1; }
    return 0;
}

/* the box index (j) of each box id                                    */
static int box_index(int id)
{
    int lo = 0, hi = T.nbox - 1;
    /* box ids are ascending within a panel but not across all panels:
     * a plain search                                                    */
    (void) lo; (void) hi;
    {
        int j;
        for (j = 0; j < T.nbox; j++) if (T.box_id[j] == id) return j;
    }
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

/* per Mach: everything the aerodynamics give on the s set             */
typedef struct {
    double mach;
    int    nj, nk, ns, nr, nx;
    double *Qss, *Qsr, *Qsx;      /* G_s S A^-1 D1 G_s^T etc. (ns rows)  */
    double *Fs, *Fr, *Fx;         /* B S A^-1 ... (nr rows)              */
    double *Ux;                   /* 6 x nx, unsplined, at O, ref axes   */
    double *Ps, *Px, *Pr;         /* S A^-1 ... at the k set (k rows)    */
    double *Ws, *Wx;              /* A^-1 ...: the box pressures (j rows) */
    double *CQss, *CQsx, *CQsr;   /* C_ss Q_ss, C_ss Q_sx, C_ss Q_sr     */
} aero_mach;

static int aero_for_mach(double mach, aero_mach *am, const double *djx)
{
    int nj = T.nj, nk = T.nk, ns = T.ns, nr = T.nr, nx = NX;
    int ncol = ns + nr + nx, i, j, m, okm, ok;
    double *A, *R, *P, *Gs, *B, *U;
    int    *ip;

    memset(am, 0, sizeof(*am));
    am->mach = mach; am->nj = nj; am->nk = nk; am->ns = ns; am->nr = nr; am->nx = nx;

    A = aic_of_mach(mach, &okm);
    if (!okm) {
        fatal(9630, "AJJL has no AIC at Mach %g.", mach);
        return 1;
    }
    ip = INEW(nj);
    ok = lu(nj, A, ip);
    if (!ok) { fatal(9631, "the AIC at Mach %g is singular.", mach); free(A); free(ip); return 1; }

    /* the right hand sides: D1 G_s^T (ns), D1 G_a^T D_a (nr), D_jx (nx) */
    R = DNEW((size_t) nj * ncol);
    {
        /* G^T columns: for an a dof (row of GTKA), its k-set motion is
         * GTKA's row; D1 (j x k) is the stored k x j transposed        */
        double *gk = DNEW(nk);     /* one k vector                     */
        int     c;
        /* row access to GTKA: make a dense ns x nk copy (G_s) and the
         * a x nk products with D_a on the fly                          */
        Gs = DNEW((size_t) ns * nk);
        B  = DNEW((size_t) nr * nk);           /* D_a^T G_a              */
        for (j = 0; j < nk; j++) {
            for (m = T.gtka.cp[j]; m < T.gtka.cp[j + 1]; m++) {
                int arow = T.gtka.ri[m];
                double v = T.gtka.v[m];
                int l = T.a_l[arow], r = T.a_r[arow];
                if (l >= 0) {
                    int s = T.l_s[l];
                    if (s >= 0) Gs[s + (size_t) j * ns] = v;
                    for (i = 0; i < nr; i++) B[i + (size_t) j * nr] += T.dm[(size_t) i * T.nl + l] * v;
                } else if (r >= 0) {
                    B[r + (size_t) j * nr] += v;
                }
            }
        }
        /* D1 G^T x for each needed column x of G^T                     */
        for (c = 0; c < ns + nr; c++) {
            for (j = 0; j < nk; j++)
                gk[j] = (c < ns) ? Gs[c + (size_t) j * ns] : B[(c - ns) + (size_t) j * nr];
            for (j = 0; j < nj; j++) {
                double s = 0.0;
                for (m = T.d1t.cp[j]; m < T.d1t.cp[j + 1]; m++) s += T.d1t.v[m] * gk[T.d1t.ri[m]];
                R[j + (size_t) c * nj] = s;
            }
        }
        for (c = 0; c < nx; c++)
            for (j = 0; j < nj; j++) R[j + (size_t) (ns + nr + c) * nj] = djx[j + (size_t) c * nj];
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
    /* the pressure coefficients, kept for APRES                       */
    am->Ws = DNEW((size_t) nj * ns); am->Wx = DNEW((size_t) nj * nx);
    memcpy(am->Ws, R, sizeof(double) * (size_t) nj * ns);
    memcpy(am->Wx, R + (size_t) nj * (ns + nr), sizeof(double) * (size_t) nj * nx);
    free(R);

    /* the splined generalised forces on the s set and at the SUPORT    */
    {
        double *Q = DNEW((size_t) ns * ncol), *F = DNEW((size_t) nr * ncol);
        mm(ns, ncol, nk, Gs, ns, P, nk, Q, ns);
        mm(nr, ncol, nk, B, nr, P, nk, F, nr);
        am->Qss = DNEW((size_t) ns * ns); am->Qsr = DNEW((size_t) ns * nr); am->Qsx = DNEW((size_t) ns * nx);
        am->Fs  = DNEW((size_t) nr * ns); am->Fr  = DNEW((size_t) nr * nr); am->Fx  = DNEW((size_t) nr * nx);
        memcpy(am->Qss, Q, sizeof(double) * (size_t) ns * ns);
        memcpy(am->Qsr, Q + (size_t) ns * ns, sizeof(double) * (size_t) ns * nr);
        memcpy(am->Qsx, Q + (size_t) ns * (ns + nr), sizeof(double) * (size_t) ns * nx);
        memcpy(am->Fs, F, sizeof(double) * (size_t) nr * ns);
        memcpy(am->Fr, F + (size_t) nr * ns, sizeof(double) * (size_t) nr * nr);
        memcpy(am->Fx, F + (size_t) nr * (ns + nr), sizeof(double) * (size_t) nr * nx);
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
        double n[3] = { 0.0, -T.bx_sg[j], T.bx_cg[j] };
        double s[3] = { 0.0,  T.bx_cg[j], T.bx_sg[j] };
        double rc[3] = { T.bx_xic[j] + 0.25 * T.bx_dx[j], T.bx_ys[j], T.bx_zs[j] };
        double d[3], mn[3];
        int c, k2;
        for (k2 = 0; k2 < 3; k2++) d[k2] = rc[k2] - T.O[k2];
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
                double n[3], sv[3], d[3], t[3];
                if (jb < 0 || 2 * jb + 1 >= nk) continue;
                n[0] = 0.0; n[1] = -T.bx_sg[jb]; n[2] = T.bx_cg[jb];
                sv[0] = 0.0; sv[1] = T.bx_cg[jb]; sv[2] = T.bx_sg[jb];
                d[0] = T.bx_xic[jb] + 0.25 * T.bx_dx[jb] - org[0];
                d[1] = T.bx_ys[jb] - org[1];
                d[2] = T.bx_zs[jb] - org[2];
                cross(d, n, t);
                H[(size_t) is * nk + 2 * jb]     += dot(t, h);
                H[(size_t) is * nk + 2 * jb + 1] += dot(sv, h);
            }
        }
    }
    return H;
}

/* D_jx: the downwash of a unit value of every trim variable           */
static double *downwash(void)
{
    int nj = T.nj, c, j, k;
    double *D = DNEW((size_t) nj * NX);
    double xh[3] = { 1.0, 0.0, 0.0 };       /* the flow (ACSID 0)        */
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
                    double n[3];
                    if (jb < 0) {
                        fatal(9625, "AELIST %d names box %d, which no CAERO1 makes.",
                              L->sid, L->ids[li]);
                        free(D);
                        return NULL;
                    }
                    n[0] = 0.0; n[1] = -T.bx_sg[jb]; n[2] = T.bx_cg[jb];
                    /* w = -n . (delta h x V_hat), scaled by EFF            */
                    D[jb + (size_t) c * nj] += -sf->eff * dot(n, hx);
                }
            }
            continue;
        }
        for (j = 0; j < nj; j++) {
            double n[3] = { 0.0, -T.bx_sg[j], T.bx_cg[j] };
            double r[3] = { T.bx_xic[j] + 0.5 * T.bx_dx[j] - T.O[0],
                            T.bx_ys[j] - T.O[1], T.bx_zs[j] - T.O[2] };
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

/* one variable's six rows; cols[6][6] are the six coefficients (rows)
 * of the six columns                                                  */
static void print_var_rows(const char *label, double cols[6][6])
{
    static const char *cn[6] = { "CX", "CY", "CZ", "CMX", "CMY", "CMZ" };
    int r;
    for (r = 0; r < 6; r++)
        out("    %-17s%-10s%13.6E   %13.6E    %13.6E   %13.6E    %13.6E   %13.6E",
            r == 0 ? label : "", cn[r], cols[0][r], cols[1][r], cols[2][r],
            cols[3][r], cols[4][r], cols[5][r]);
    out("");
}

/* forces at the r dofs -> the six reference-axis coefficients         */
static void to_coeffs(const double *fr, double q, double *c6)
{
    int i, k;
    double f[6];
    /* TR = RB_r^T: F_ref = RB_r^T F_r                                  */
    for (i = 0; i < 6; i++) {
        double s = 0.0;
        for (k = 0; k < 6; k++) s += T.rbr[k + 6 * i] * fr[k];
        f[i] = s;
    }
    for (i = 0; i < 6; i++) c6[i] = f[i] / (q * aet_g.refs);
    c6[3] /= aet_g.refb; c6[4] /= aet_g.refc; c6[5] /= aet_g.refb;
}

static void ref_coeffs(const double *fref, double q, double *c6)
{
    int i;
    for (i = 0; i < 6; i++) c6[i] = fref[i] / (q * aet_g.refs);
    c6[3] /= aet_g.refb; c6[4] /= aet_g.refc; c6[5] /= aet_g.refb;
}

/* ------------------------------------------------------------------ */

static int trim_index(int sid)
{
    int i;
    for (i = 0; i < aet_g.ntrim; i++) if (aet_g.trim[i].sid == sid) return i;
    return -1;
}

static int mode2(void)
{
    int ns = T.ns, nr = T.nr, nk, nj, nsub, isub, i, j, k, c, im;
    double *djx = NULL, *HM = NULL;
    aero_mach *AM = NULL;
    int nmach = aet_g.nmach;

    nk = T.nk; nj = T.nj;
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
    if (!T.have_css || !T.have_xms || !T.have_mxm || !T.have_dm || !T.have_mr) {
        fatal(9635, "a structural matrix the trim needs was not available (CLS %d,\n"
              "XM %d, MXM %d, DM %d, MR %d).", T.have_css, T.have_xms, T.have_mxm,
              T.have_dm, T.have_mr);
        return 1;
    }
    if (build_vars()) return 1;
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
    T.uddt = DNEW((size_t) nr * nsub);
    T.pgt  = DNEW((size_t) T.luset * nsub);

    for (isub = 0; isub < nsub; isub++) {
        int sid = -1, it, nfix = 0, nlnk = 0, neq, info_ok;
        const aet_trimc *tc;
        aero_mach *am = NULL;
        double q, qe, *Z, *RHS, *Usx, *Usr, *Usa;
        int *ipz;
        double Ax[6 * 64];            /* RB_r e_x / AUNITS, nr x nx (nx <= 64) */
        double *FR, *IR, *UNR, *ux, *Mx, *Mr, *Ma, *Fr, *Fa;
        int    *status;               /* 0 free 1 fixed 2 linked           */
        double *cpk = NULL;           /* APRES/AEROF: cp (nj), forces (nk)  */
        double *URU = NULL;           /* unrestrained u_r, u''_r per variable */

        for (i = 0; i < aet_g.nsub; i++)
            if (aet_g.sub_id[i] == T.case_id[isub]) { sid = aet_g.sub_trim[i]; break; }
        if (sid < 0 && aet_g.nsub == nsub) sid = aet_g.sub_trim[isub];
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
            if (X[c].kind >= 5 && X[c].kind <= 10)
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
        RHS = DNEW((size_t) ns * (NX + nr + nr));
        memcpy(RHS, am->CQsx, sizeof(double) * (size_t) ns * NX);
        memcpy(RHS + (size_t) ns * NX, am->CQsr, sizeof(double) * (size_t) ns * nr);
        for (j = 0; j < ns * NX; j++) RHS[j] *= q;
        for (j = 0; j < ns * nr; j++) RHS[(size_t) ns * NX + j] *= qe;
        for (j = 0; j < ns * nr; j++) RHS[(size_t) ns * (NX + nr) + j] = -T.xms[j];
        lus(ns, Z, ipz, RHS, NX + nr + nr);
        Usx = RHS; Usr = RHS + (size_t) ns * NX; Usa = RHS + (size_t) ns * (NX + nr);

        /* restrained elastic forces at the r dofs, per variable           */
        FR = DNEW((size_t) nr * NX); IR = DNEW((size_t) nr * NX);
        Fa = DNEW((size_t) nr * nr); Fr = DNEW((size_t) nr * nr);
        mm(nr, nr, ns, am->Fs, nr, Usa, ns, Fa, nr);
        for (j = 0; j < nr * nr; j++) Fa[j] *= qe;
        mm(nr, nr, ns, am->Fs, nr, Usr, ns, Fr, nr);
        for (j = 0; j < nr * nr; j++) Fr[j] = qe * (am->Fr[j] + Fr[j]);
        mm(nr, NX, ns, am->Fs, nr, Usx, ns, FR, nr);
        for (c = 0; c < NX; c++) {
            if (X[c].kind >= 5 && X[c].kind <= 10) {
                for (i = 0; i < nr; i++) {
                    double s = 0.0, si = 0.0;
                    for (k = 0; k < nr; k++) {
                        s  += Fa[i + nr * k] * Ax[k + nr * c];
                        si += T.mr[i + 6 * k] * Ax[k + nr * c];
                    }
                    FR[i + nr * c] = s;
                    IR[i + nr * c] = si;
                }
            } else {
                for (i = 0; i < nr; i++)
                    FR[i + nr * c] = q * am->Fx[i + nr * c] + qe * FR[i + nr * c];
            }
        }

        /* the trim: equilibrium of the restrained vehicle, the fixed
         * values and the links                                            */
        ux = DNEW(NX);
        status = INEW(NX);
        {
            double *M = DNEW((size_t) NX * NX), *rhs = DNEW(NX);
            int     row = 0, *ipm = INEW(NX);
            for (c = 0; c < NX; c++) status[c] = 0;
            for (i = 0; i < nr && row < NX; i++, row++)
                for (c = 0; c < NX; c++) M[row + (size_t) NX * c] = FR[i + nr * c] - IR[i + nr * c];
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
            info_ok = lu(NX, M, ipm);
            if (!info_ok) {
                fatal(9647, "subcase %d (TRIM %d): the trim equations are singular: the\n"
                      "free variables cannot balance the six rigid body equations\n"
                      "(e.g. no free variable acts on one of them).",
                      T.case_id[isub], tc->sid);
                free(djx); return 1;
            }
            memcpy(ux, rhs, sizeof(double) * (size_t) NX);
            lus(NX, M, ipm, ux, 1);
            free(M); free(rhs); free(ipm);
        }

        /* the accelerations and the deformation at trim                    */
        {
            double ur[6] = { 0 }, *us = DNEW(ns), *y = DNEW(ns), *pk = DNEW(nk);
            for (c = 0; c < NX; c++)
                for (i = 0; i < nr; i++) ur[i] += Ax[i + nr * c] * ux[c];
            for (c = 0; c < NX; c++) {
                if (X[c].kind >= 5 && X[c].kind <= 10) continue;
                for (i = 0; i < ns; i++) us[i] += Usx[i + (size_t) ns * c] * ux[c];
            }
            for (k = 0; k < nr; k++)
                for (i = 0; i < ns; i++) us[i] += Usa[i + (size_t) ns * k] * ur[k];
            /* PLA = E (q Q_sx u_x + qe Q_ss u_s)                           */
            for (i = 0; i < ns; i++) {
                double s = 0.0;
                for (c = 0; c < NX; c++) s += q * am->Qsx[i + (size_t) ns * c] * ux[c];
                for (j = 0; j < ns; j++) s += qe * am->Qss[i + (size_t) ns * j] * us[j];
                y[i] = s;
            }
            for (i = 0; i < ns; i++) T.pla[(size_t) isub * T.nl + T.s_l[i]] = y[i];
            for (i = 0; i < nr; i++) T.uddt[(size_t) isub * nr + i] = ur[i];
            /* the k set forces at trim, and the g set loads (OLOAD)          */
            for (k = 0; k < nk; k++) {
                double s = 0.0;
                for (c = 0; c < NX; c++) s += q * am->Px[k + (size_t) nk * c] * ux[c];
                for (j = 0; j < ns; j++) s += qe * am->Ps[k + (size_t) nk * j] * us[j];
                pk[k] = s;
            }
            /* APRES / AEROF: the box pressure coefficients at trim, printed
             * after the trim variables                                     */
            if (aet_g.want_apres || aet_g.want_aerof) {
                cpk = DNEW(nj + nk);
                for (j = 0; j < nj; j++) {
                    double s = 0.0;
                    for (c = 0; c < NX; c++) s += am->Wx[j + (size_t) nj * c] * ux[c];
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

        /* the unrestrained (mean axis) derivatives, per aero variable      */
        Mx = DNEW((size_t) nr * NX); Mr = DNEW((size_t) nr * nr); Ma = DNEW((size_t) nr * nr);
        UNR = DNEW((size_t) nr * NX);
        URU = DNEW((size_t) 2 * nr * NX);
        {
            double *t1 = DNEW((size_t) ns * (NX > nr ? NX : nr));
            /* Mx = XM_s^T (q Q_sx + qe Q_ss Us_x)                          */
            mm(ns, NX, ns, am->Qss, ns, Usx, ns, t1, ns);
            for (j = 0; j < ns * NX; j++) t1[j] = q * am->Qsx[j] + qe * t1[j];
            for (c = 0; c < NX; c++)
                if (X[c].kind >= 5 && X[c].kind <= 10)
                    for (i = 0; i < ns; i++) t1[i + (size_t) ns * c] = 0.0;
            mtm(nr, NX, ns, T.xms, ns, t1, ns, Mx, nr);
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
        {
            /* [m_r + Mr, Ma; -Fr, m_r - Fa] [u_r; u''_r] = [-Mx; FR] u_x   */
            int    n2 = 2 * nr, ip2[12];
            double S[144], rhs2[12];
            memset(S, 0, sizeof(S));
            for (i = 0; i < nr; i++)
                for (j = 0; j < nr; j++) {
                    S[i + n2 * j]               = T.mr[i + 6 * j] + Mr[i + nr * j];
                    S[i + n2 * (nr + j)]        = Ma[i + nr * j];
                    S[(nr + i) + n2 * j]        = -Fr[i + nr * j];
                    S[(nr + i) + n2 * (nr + j)] = T.mr[i + 6 * j] - Fa[i + nr * j];
                }
            if (!lu(n2, S, ip2)) {
                info(9648, "subcase %d: the mean axis system is singular; the\n"
                     "unrestrained derivatives are left zero.", T.case_id[isub]);
            } else {
                for (c = 0; c < NX; c++) {
                    if (X[c].kind >= 5 && X[c].kind <= 10) continue;
                    for (i = 0; i < nr; i++) {
                        rhs2[i] = -Mx[i + nr * c];
                        rhs2[nr + i] = FR[i + nr * c];
                    }
                    lus(n2, S, ip2, rhs2, 1);
                    for (i = 0; i < 2 * nr; i++) URU[i + 2 * nr * c] = rhs2[i];
                    /* m_r u''_r: the force on the free vehicle             */
                    for (i = 0; i < nr; i++) {
                        double s = 0.0;
                        for (k = 0; k < nr; k++) s += T.mr[i + 6 * k] * rhs2[nr + k];
                        UNR[i + nr * c] = s;
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
            memset(cols, 0, sizeof(cols));
            print_var_rows("REF. COEFF.", cols);
            for (c = 0; c < NX; c++) {
                double fr[6];
                int urdd = X[c].kind >= 5 && X[c].kind <= 10;
                memset(cols, 0, sizeof(cols));
                if (!urdd) {
                    ref_coeffs(&am->Ux[6 * c], 1.0, cols[0]);
                    for (i = 0; i < nr; i++) fr[i] = am->Fx[i + nr * c];
                    to_coeffs(fr, 1.0, cols[1]);
                }
                for (i = 0; i < nr; i++) fr[i] = FR[i + nr * c];
                to_coeffs(fr, q, cols[2]);
                if (!urdd) {
                    for (i = 0; i < nr; i++) fr[i] = UNR[i + nr * c];
                    to_coeffs(fr, q, cols[3]);
                    to_coeffs(fr, q, cols[5]);
                } else {
                    for (i = 0; i < nr; i++) fr[i] = IR[i + nr * c];
                    to_coeffs(fr, q, cols[4]);
                }
                print_var_rows(X[c].label, cols);
            }
        }

        /* the hinge moment derivatives of each control surface              */
        if (HM) {
            int is;
            double *fk = DNEW(nk), *tv = DNEW(ns);
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
                out("              %-19s%14.6E%29.6E%16.6E%21.6E%16.6E", "AT REFERENCE", 0.0, 0.0, 0.0, 0.0, 0.0);
                for (c = 0; c < NX; c++) {
                    int urdd = X[c].kind >= 5 && X[c].kind <= 10;
                    double hr = 0.0, he = 0.0, hu = 0.0;
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
                    if (!urdd) {
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
                    out("              %-19s%14.6E%29.6E%16.6E%21.6E%16.6E", X[c].label,
                        hr / den, he / den, hu / den, 0.0, 0.0);
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
            double fa[6] = { 0 }, fi[6] = { 0 }, ref_a[6], ref_i[6], ur[6];
            for (i = 0; i < nr; i++) ur[i] = T.uddt[(size_t) isub * nr + i];
            for (c = 0; c < NX; c++) {
                if (X[c].kind >= 5 && X[c].kind <= 10) {
                    for (i = 0; i < nr; i++) fa[i] += FR[i + nr * c] * ux[c];
                    continue;
                }
                for (i = 0; i < nr; i++) fa[i] += FR[i + nr * c] * ux[c];
            }
            for (i = 0; i < nr; i++) {
                double s = 0.0;
                for (k = 0; k < nr; k++) s += T.mr[i + 6 * k] * ur[k];
                fi[i] = -s;
            }
            for (i = 0; i < 6; i++) {
                double sa = 0.0, si = 0.0;
                for (k = 0; k < 6; k++) { sa += T.rbr[k + 6 * i] * fa[k]; si += T.rbr[k + 6 * i] * fi[k]; }
                ref_a[i] = sa; ref_i[i] = si;
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
