/* HALO: the MSC bulk data translation.
 *
 * What the 1970s input cannot say, and what is done about it:
 *
 *   RBE2, RBAR        -> CRIGD1/CRIGD2. The rigid elements are the same
 *                        constraint; COSMIC spells them differently and
 *                        wants the independent grid first.
 *   CBUSH + PBUSH     -> CELAS2 per non-zero stiffness when the two
 *                        grids are coincident, CONROD when they are not
 *                        and only K1 is set. A CBUSH between separated
 *                        grids with bending stiffness has no equivalent
 *                        and is a fatal, not a guess.
 *   EIGRL             -> EIGR FEER. FEER wants a shift and a root count
 *                        where Lanczos takes a frequency range.
 *   PBARL, PBEAML     -> PBAR with the section properties worked out
 *                        from the dimensions.
 *   CBEAM, PBEAM      -> CBAR, PBAR at the end-A properties, with a
 *                        warning when the section is not prismatic.
 *   CQUAD4, CTRIA3    -> CQUAD2, CTRIA2 (homogeneous plates) and PSHELL
 *                        -> PQUAD2/PTRIA2.
 *   ids over 2^24-1   -> renumbered, consistently, everywhere they are
 *                        referenced. NASTRAN packs an id and a component
 *                        into one 32-bit word.
 *   dofs nothing is
 *   attached to       -> SPC'd, which is what MSC's AUTOSPC does. The
 *                        1970s code has no such thing and gives a
 *                        singular matrix instead.
 *
 * Everything else is passed through, and a card this does not know is a
 * fatal that names it. Silently dropping a card changes the model.
 */
#include "msc.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ID_LIMIT 16777215      /* 2^24 - 1: NASTRAN packs id*10+component */

typedef struct {
    msc_deck  *d;
    msc_list   out;
    msc_map    grid;      /* grid  -> dv[0..2] = x y z                    */
    msc_map    pbush;     /* pid   -> dv[0..5] = K1..K6                   */
    msc_map    pbar;      /* pid   -> iv = 1 (a PBAR exists)              */
    msc_map    pshell;   /* pid   -> dv[0] = membrane thickness          */
    msc_map    stiff;     /* grid  -> how many stiffness elements         */
    msc_map    attach;    /* grid  -> bitmask of dofs with stiffness      */
    msc_map    constr;    /* grid  -> bitmask of dofs already constrained */
    msc_map    remap;     /* old id -> new id                             */
    msc_map    held;      /* grid  -> 1 when a SUPORT or SPC names it     */
    msc_stats *st;
    int        next_big;  /* next renumbered id, counting down            */
    int        next_eid;  /* next free element id                         */
    int        unit_mat;  /* MAT1 with E=1, used by CBUSH -> CONROD       */
    int        spc_set;   /* the SPC set the case control selects         */
    int        auto_set;  /* set id the auto-SPC is written into          */
    int        eig_sid;   /* METHOD set id seen on an EIGRL/EIGR          */
    int        nmodes;
    double     shift;
    int        have_eig;
    int        fatal;
} msc_ctx;

/* ------------------------------------------------------------------ */
/* ids                                                                 */

/* Renumber one id field if it is over the limit, remembering the
 * mapping so that every reference to it lands on the same new number. */
static void small_id(msc_ctx *x, msc_card *c, int i)
{
    const char *s = msc_f(c, i);
    long        v;
    char        buf[32];
    if (!*s || !msc_isnum(s)) return;
    v = strtol(s, NULL, 10);
    if (v <= ID_LIMIT) return;
    if (!msc_map_has(&x->remap, (int) v)) {
        msc_map_put(&x->remap, (int) v, x->next_big--);
        x->st->renumbered++;
    }
    sprintf(buf, "%d", msc_map_get(&x->remap, (int) v, 0));
    msc_set(c, i, buf);
}

static void small_ids(msc_ctx *x, msc_card *c, const int *idx, int n)
{
    int i;
    for (i = 0; i < n; i++) small_id(x, c, idx[i]);
}

/* ------------------------------------------------------------------ */
/* the model tables                                                    */

static void note_stiff(msc_ctx *x, int g)
{
    if (g > 0) msc_map_add(&x->stiff, g, 1);
}

static void attach_dofs(msc_ctx *x, int g, int mask)
{
    if (g > 0) msc_map_slot(&x->attach, g, 1)->iv |= mask;
}

static void constrain_dofs(msc_ctx *x, int g, int mask)
{
    if (g > 0) msc_map_slot(&x->constr, g, 1)->iv |= mask;
}

/* "123456" -> bit mask */
static int dof_mask(const char *s)
{
    int m = 0;
    for (; *s; s++) if (*s >= '1' && *s <= '6') m |= 1 << (*s - '1');
    return m;
}

#define ALL6 0x3F
#define XYZ3 0x07

/* ------------------------------------------------------------------ */
/* emitting                                                            */

static msc_card *emit(msc_ctx *x, const char *name)
{
    x->st->translated++;
    return msc_list_add(&x->out, name);
}

/* copy a card through unchanged */
static void pass_through(msc_ctx *x, msc_card *c)
{
    msc_card *o = emit(x, c->name);
    int i;
    for (i = 1; i <= c->nfld; i++) msc_set(o, i, msc_f(c, i));
    msc_tally("unchanged", c->name);
}

/* ------------------------------------------------------------------ */
/* PBARL / PBEAML section properties.
 *
 * The section types this repository uses are the ones a spar and a boom
 * are drawn with: TUBE, ROD, BAR, BOX, I. The formulas are the standard
 * ones; the torsion constant of a thin-walled closed section is
 * Bredt's, and of an open section the sum of b t^3 / 3.
 */
static int bar_section(const char *type, const double *dim, int ndim,
                       double *A, double *I1, double *I2, double *J)
{
    double d1 = ndim > 0 ? dim[0] : 0.0;
    double d2 = ndim > 1 ? dim[1] : 0.0;
    double d3 = ndim > 2 ? dim[2] : 0.0;
    double d4 = ndim > 3 ? dim[3] : 0.0;

    if (msc_streq(type, "ROD")) {
        double r = d1;
        *A  = M_PI * r * r;
        *I1 = *I2 = M_PI * r * r * r * r / 4.0;
        *J  = 2.0 * *I1;
        return 0;
    }
    if (msc_streq(type, "TUBE") || msc_streq(type, "TUBE2")) {
        double ro = d1, ri;
        if (msc_streq(type, "TUBE")) ri = d2;          /* outer, inner   */
        else                          ri = d1 - d2;    /* outer, wall    */
        *A  = M_PI * (ro * ro - ri * ri);
        *I1 = *I2 = M_PI * (ro*ro*ro*ro - ri*ri*ri*ri) / 4.0;
        *J  = 2.0 * *I1;
        return 0;
    }
    if (msc_streq(type, "BAR")) {           /* solid rectangle W x H     */
        double w = d1, h = d2, a, b, jj;
        *A  = w * h;
        *I1 = w * h * h * h / 12.0;
        *I2 = h * w * w * w / 12.0;
        a = (w > h) ? w : h;
        b = (w > h) ? h : w;
        jj = a * b * b * b * (1.0 / 3.0 - 0.21 * (b / a) *
                              (1.0 - b*b*b*b / (12.0 * a*a*a*a)));
        *J = jj;
        return 0;
    }
    if (msc_streq(type, "BOX")) {           /* W H T1(web) T2(flange)    */
        double w = d1, h = d2, t1 = d3, t2 = d4;
        double wi = w - 2.0 * t1, hi = h - 2.0 * t2;
        double am, p;
        if (wi <= 0.0 || hi <= 0.0) return 1;
        *A  = w * h - wi * hi;
        *I1 = (w * h*h*h - wi * hi*hi*hi) / 12.0;
        *I2 = (h * w*w*w - hi * wi*wi*wi) / 12.0;
        am  = (w - t1) * (h - t2);                   /* enclosed area    */
        p   = 2.0 * ((w - t1) / t1 + (h - t2) / t2); /* ds/t around      */
        *J  = 4.0 * am * am / p;
        return 0;
    }
    if (msc_streq(type, "I")) {             /* H W1 W2 T1(web) T2 T3     */
        double h = d1, w1 = d2, w2 = d3, tw = d4;
        double t2 = ndim > 4 ? dim[4] : 0.0;
        double t3 = ndim > 5 ? dim[5] : 0.0;
        double hw = h - t2 - t3;
        if (hw <= 0.0) return 1;
        *A  = w1 * t2 + w2 * t3 + hw * tw;
        *I1 = (w1 * t2*t2*t2 + w2 * t3*t3*t3 + tw * hw*hw*hw) / 12.0
            + w1 * t2 * pow((h - t2) / 2.0, 2.0)
            + w2 * t3 * pow((h - t3) / 2.0, 2.0);
        *I2 = (t2 * w1*w1*w1 + t3 * w2*w2*w2 + hw * tw*tw*tw) / 12.0;
        *J  = (w1 * t2*t2*t2 + w2 * t3*t3*t3 + hw * tw*tw*tw) / 3.0;
        return 0;
    }
    return 2;     /* section type not known here */
}

/* ------------------------------------------------------------------ */
/* first pass: what the model is made of                               */

static void scan(msc_ctx *x)
{
    int i;
    for (i = 0; i < x->d->nbulk; i++) {
        msc_card *c = &x->d->bulk[i];
        const char *n = c->name;

        if (msc_streq(n, "GRID")) {
            double *v = msc_map_vec(&x->grid, msc_fi(c, 1, 0), 1);
            v[0] = msc_fd(c, 3, 0.0);
            v[1] = msc_fd(c, 4, 0.0);
            v[2] = msc_fd(c, 5, 0.0);
        } else if (msc_streq(n, "PBUSH")) {
            /* PBUSH PID "K" K1..K6, then optionally "B"/"GE" blocks */
            int k, f = 2;
            double *v = msc_map_vec(&x->pbush, msc_fi(c, 1, 0), 1);
            while (f <= c->nfld) {
                const char *tag = msc_f(c, f);
                if (msc_streq(tag, "K")) {
                    for (k = 0; k < 6; k++) v[k] = msc_fd(c, f + 1 + k, 0.0);
                    break;
                }
                f++;
            }
        } else if (msc_streq(n, "PSHELL")) {
            double *v = msc_map_vec(&x->pshell, msc_fi(c, 1, 0), 1);
            v[0] = msc_fd(c, 3, 0.0);
        } else if (msc_streq(n, "SUPORT1")) {
            int k;
            for (k = 2; k <= c->nfld; k += 2) msc_map_put(&x->held, msc_fi(c, k, 0), 1);
        } else if (msc_streq(n, "SUPORT")) {
            int k;
            for (k = 1; k <= c->nfld; k += 2) msc_map_put(&x->held, msc_fi(c, k, 0), 1);
        } else if (msc_streq(n, "SPC1")) {
            int k;
            for (k = 3; k <= c->nfld; k++) msc_map_put(&x->held, msc_fi(c, k, 0), 1);
        } else if (msc_streq(n, "SPC")) {
            msc_map_put(&x->held, msc_fi(c, 2, 0), 1);
            if (msc_fi(c, 5, 0)) msc_map_put(&x->held, msc_fi(c, 5, 0), 1);
        } else if (msc_streq(n, "CBAR") || msc_streq(n, "CBEAM") ||
                   msc_streq(n, "CBUSH")) {
            note_stiff(x, msc_fi(c, 3, 0));
            note_stiff(x, msc_fi(c, 4, 0));
        } else if (msc_streq(n, "CROD") || msc_streq(n, "CTUBE")) {
            note_stiff(x, msc_fi(c, 3, 0));
            note_stiff(x, msc_fi(c, 4, 0));
        } else if (msc_streq(n, "CONROD")) {
            note_stiff(x, msc_fi(c, 2, 0));
            note_stiff(x, msc_fi(c, 3, 0));
        } else if (msc_streq(n, "CELAS2")) {
            note_stiff(x, msc_fi(c, 3, 0));
            note_stiff(x, msc_fi(c, 5, 0));
        } else if (msc_streq(n, "CELAS1")) {
            note_stiff(x, msc_fi(c, 3, 0));
            note_stiff(x, msc_fi(c, 5, 0));
        } else if (msc_streq(n, "CQUAD4") || msc_streq(n, "CTRIA3") ||
                   msc_streq(n, "CSHEAR") || msc_streq(n, "CQUADR")) {
            int k;
            for (k = 3; k <= 6; k++) note_stiff(x, msc_fi(c, k, 0));
        }

        /* the next free element id, so that a CBUSH can become several
         * CELAS2s without colliding with anything the deck already has */
        if (n[0] == 'C') {
            int eid = msc_fi(c, 1, 0);
            if (eid >= x->next_eid) x->next_eid = eid + 1;
        }
    }
    if (x->next_eid < 1) x->next_eid = 1;
}

/* ------------------------------------------------------------------ */
/* the card handlers                                                   */

static void do_rigid_bar(msc_ctx *x, msc_card *c)
{
    /* RBAR EID GA GB CNA CNB CMA CMB */
    int ga = msc_fi(c, 2, 0), gb = msc_fi(c, 3, 0);
    const char *cna = msc_f(c, 4), *cnb = msc_f(c, 5);
    const char *cma = msc_f(c, 6), *cmb = msc_f(c, 7);
    int ref, dep, nref, ndep;
    msc_card *o;

    if (msc_streq(cna, "123456") && (msc_streq(cmb, "123456") || !*cmb)) {
        ref = ga; dep = gb;
    } else if (msc_streq(cnb, "123456") && (msc_streq(cma, "123456") || !*cma)) {
        ref = gb; dep = ga;
    } else {
        msc_msg_at(MSC_FATAL, 9201, c,
            "This RBAR makes %s of grid %d depend on %s of grid %d. The 1970s\n"
            "rigid elements join all six components or none, so there is no\n"
            "CRIGD form of it.\n"
            "FIX   Write it as an MPC, or make the dependent set 123456.",
            *cma ? cma : "(none)", ga, *cmb ? cmb : "(none)", gb);
        x->fatal = 1;
        return;
    }
    /* A rigid link is the same constraint whichever end is called
     * independent. NASTRAN-95 checks for singularities on the element
     * stiffness BEFORE the rigid elements are applied, so an
     * independent grid with no element of its own (an aero reference
     * grid hanging off the wing root) is flagged. Put the end that
     * carries elements first.                                          */
    nref = msc_map_get(&x->stiff, ref, 0);
    ndep = msc_map_get(&x->stiff, dep, 0);
    /* ... unless the swap would make a grid dependent that a SUPORT or
     * an SPC names. A dependent (m-set) grid cannot also be in the r-set
     * or the s-set -- UFM 2101A, "ILLEGALLY DEFINED IN SETS UM UAUR" --
     * and the free-free vibe decks put their SUPORT on exactly this
     * reference grid.                                                 */
    if (nref == 0 && ndep > 0 && !msc_map_has(&x->held, ref)) {
        int t = ref; ref = dep; dep = t;
    }

    o = emit(x, "CRIGD1");
    msc_set(o, 1, msc_f(c, 1));
    msc_seti(o, 2, ref);
    msc_seti(o, 3, dep);
    msc_tally("RBAR -> CRIGD1", "RBAR");
    attach_dofs(x, ref, ALL6);
    attach_dofs(x, dep, ALL6);
}

static void do_rbe2(msc_ctx *x, msc_card *c)
{
    /* RBE2 EID GN CM GM1 GM2 ... : one independent grid, many dependent,
     * all with the same component list. CRIGD1 is exactly that when the
     * components are all six.                                          */
    int gn = msc_fi(c, 2, 0);
    const char *cm = msc_f(c, 3);
    msc_card *o;
    int i, k;

    if (!msc_streq(cm, "123456")) {
        msc_msg_at(MSC_FATAL, 9202, c,
            "This RBE2 makes only components %s dependent. CRIGD1, the 1970s\n"
            "equivalent, joins all six.\n"
            "FIX   Use 123456, or write the partial constraint as MPCs.", cm);
        x->fatal = 1;
        return;
    }
    o = emit(x, "CRIGD1");
    msc_set(o, 1, msc_f(c, 1));
    msc_seti(o, 2, gn);
    msc_tally("RBE2 -> CRIGD1", "RBE2");
    attach_dofs(x, gn, ALL6);
    k = 3;
    for (i = 4; i <= c->nfld; i++) {
        int g = msc_fi(c, i, 0);
        const char *s = msc_f(c, i);
        if (!*s) continue;
        if (!msc_isnum(s)) continue;          /* the ALPHA field, if any */
        msc_seti(o, k++, g);
        attach_dofs(x, g, ALL6);
    }
}

static void do_cbush(msc_ctx *x, msc_card *c)
{
    int   eid = msc_fi(c, 1, 0);
    int   pid = msc_fi(c, 2, 0);
    int   ga  = msc_fi(c, 3, 0), gb = msc_fi(c, 4, 0);
    double *k = msc_map_vec(&x->pbush, pid, 0);
    double *xa = msc_map_vec(&x->grid, ga, 0);
    double *xb = msc_map_vec(&x->grid, gb, 0);
    double L = 0.0;
    int j;

    if (!k) {
        msc_msg_at(MSC_FATAL, 9203, c,
            "PBUSH %d is missing, or gives no K row, so this CBUSH has no\n"
            "stiffness to translate.\n"
            "FIX   Give the PBUSH a K row with K1..K6.", pid);
        x->fatal = 1;
        return;
    }
    if (!xa || !xb) {
        msc_msg_at(MSC_FATAL, 9204, c,
            "One of grids %d and %d is not in the deck.", ga, gb);
        x->fatal = 1;
        return;
    }
    {
        double dx = xb[0] - xa[0], dy = xb[1] - xa[1], dz = xb[2] - xa[2];
        L = sqrt(dx*dx + dy*dy + dz*dz);
    }
    if (L < 1e-9) {
        /* coincident: a scalar spring per non-zero stiffness, which is
         * what a CBUSH between coincident grids in the basic system is */
        const char *cid = msc_f(c, 8);
        if (*cid && !msc_streq(cid, "0")) {
            msc_msg_at(MSC_WARN, 9205, c,
                "CBUSH %d is in coordinate system %s; its springs are written\n"
                "in the basic system. Check the orientation if the system is\n"
                "not parallel to basic.", eid, cid);
        }
        for (j = 0; j < 6; j++) {
            msc_card *o;
            if (k[j] == 0.0) continue;
            o = emit(x, "CELAS2");
            msc_seti(o, 1, x->next_eid++);
            msc_setd(o, 2, k[j]);
            msc_seti(o, 3, ga);
            msc_seti(o, 4, j + 1);
            msc_seti(o, 5, gb);
            msc_seti(o, 6, j + 1);
            attach_dofs(x, ga, 1 << j);
            attach_dofs(x, gb, 1 << j);
            msc_tally("CBUSH -> CELAS2", "CBUSH");
        }
        return;
    }
    /* separated: only an axial stiffness has a rod equivalent */
    for (j = 1; j < 6; j++) {
        if (k[j] != 0.0) {
            msc_msg_at(MSC_FATAL, 9206, c,
                "CBUSH %d joins grids %d and %d, which are %g apart, and its\n"
                "PBUSH sets K%d as well as K1. A bush with bending or shear\n"
                "stiffness over a finite length has no 1970s equivalent.\n"
                "FIX   Make the grids coincident (springs), or model the\n"
                "      connection with a CBAR of the equivalent stiffness.",
                eid, ga, gb, L, j + 1);
            x->fatal = 1;
            return;
        }
    }
    {
        /* CONROD with E*A/L = K1: a unit-modulus material carries the
         * stiffness in the area field, so the number is visible         */
        msc_card *o = emit(x, "CONROD");
        msc_seti(o, 1, eid);
        msc_seti(o, 2, ga);
        msc_seti(o, 3, gb);
        msc_seti(o, 4, x->unit_mat);
        msc_setd(o, 5, k[0] * L);
        attach_dofs(x, ga, XYZ3);
        attach_dofs(x, gb, XYZ3);
        msc_tally("CBUSH -> CONROD", "CBUSH");
    }
}

static void do_pbarl(msc_ctx *x, msc_card *c)
{
    /* PBARL PID MID GROUP TYPE - - - - , then DIM1..DIMn, NSM */
    const char *type = msc_f(c, 4);
    double dim[8], A, I1, I2, J;
    int    i, ndim = 0, rc;
    char   t[MSC_FLDLEN];
    msc_card *o;

    strncpy(t, type, sizeof(t) - 1);
    t[sizeof(t) - 1] = '\0';
    msc_upper(t);
    msc_trim(t);

    for (i = 9; i <= c->nfld && ndim < 8; i++) {
        const char *s = msc_f(c, i);
        if (!*s) continue;
        dim[ndim++] = msc_fd(c, i, 0.0);
    }
    rc = bar_section(t, dim, ndim, &A, &I1, &I2, &J);
    if (rc == 2) {
        msc_msg_at(MSC_FATAL, 9207, c,
            "PBARL section type %s is not one this front end can work out the\n"
            "properties of. It knows ROD, TUBE, TUBE2, BAR, BOX and I.\n"
            "FIX   Write the property as a PBAR with A, I1, I2 and J.", t);
        x->fatal = 1;
        return;
    }
    if (rc == 1) {
        msc_msg_at(MSC_FATAL, 9208, c,
            "PBARL %s has dimensions that leave no material (a wall thicker\n"
            "than the section).", msc_f(c, 1));
        x->fatal = 1;
        return;
    }
    o = emit(x, "PBAR");
    msc_set(o, 1, msc_f(c, 1));           /* PID */
    msc_set(o, 2, msc_f(c, 2));           /* MID */
    msc_setd(o, 3, A);
    msc_setd(o, 4, I1);
    msc_setd(o, 5, I2);
    msc_setd(o, 6, J);
    msc_msg(MSC_INFO, 9209,
            "PBARL %s (%s) became PBAR: A %.6g, I1 %.6g, I2 %.6g, J %.6g",
            msc_f(c, 1), t, A, I1, I2, J);
    msc_map_put(&x->pbar, msc_fi(c, 1, 0), 1);
}

static void do_eigrl(msc_ctx *x, msc_card *c)
{
    /* EIGRL SID V1 V2 ND MSGLVL MAXSET SHFSCL NORM */
    int    nd = msc_fi(c, 4, 0);
    double v1 = msc_fd(c, 2, 0.0);
    double shift;
    msc_card *o;

    x->eig_sid = msc_fi(c, 1, 0);
    x->have_eig = 1;
    if (nd <= 0) {
        msc_msg_at(MSC_FATAL, 9210, c,
            "This EIGRL asks for every mode in a frequency range. FEER, the\n"
            "1970s eigensolver, is told how many roots to find, not a range.\n"
            "FIX   Put the number of modes in field 4 (ND) of the EIGRL.");
        x->fatal = 1;
        return;
    }
    /* The shift. FEER converges on the roots nearest F1, and a zero
     * shift on a free-free model whose rigid body modes are only nearly
     * zero returns them and little else -- measured on the monarch GVT
     * model, where a zero shift lost every mode and 0.5 Hz found them
     * all. So: the bottom of the requested range when one is given,
     * otherwise half a hertz, and say so.                              */
    shift = (v1 > 0.0) ? v1 : 0.5;
    x->shift  = shift;
    x->nmodes = nd;
    o = emit(x, "EIGR");
    msc_seti(o, 1, x->eig_sid);
    msc_set (o, 2, "FEER");
    msc_setd(o, 3, shift);
    msc_seti(o, 6, nd);
    msc_set (o, 9, "MASS");
    msc_msg(MSC_INFO, 9211,
            "EIGRL %d became EIGR FEER for %d roots about %.4g Hz.%s",
            x->eig_sid, nd, shift,
            (v1 > 0.0) ? "" :
            "\nThe EIGRL gave no lower frequency, so half a hertz is used: a"
            "\nzero shift on a free-free model returns the rigid body modes"
            "\nand little else.");
}

static void do_param(msc_ctx *x, msc_card *c)
{
    char n[MSC_FLDLEN];
    strncpy(n, msc_f(c, 1), sizeof(n) - 1);
    n[sizeof(n) - 1] = '\0';
    msc_upper(n);

    /* the parameters both codes have and that change the answer */
    if (msc_streq(n, "WTMASS") || msc_streq(n, "COUPMASS") ||
        msc_streq(n, "GRDPNT")  || msc_streq(n, "G")        ||
        msc_streq(n, "W3")      || msc_streq(n, "W4")       ||
        msc_streq(n, "LMODES")  || msc_streq(n, "LFREQ")    ||
        msc_streq(n, "HFREQ")   || msc_streq(n, "MAXRATIO")) {
        pass_through(x, c);
        return;
    }
    /* the ones that only mean something to MSC: dropping them changes
     * nothing about the model, but say which went                      */
    x->st->dropped++;
    {
        char label[64];
        snprintf(label, sizeof(label), "PARAM,%s", n);
        msc_tally("dropped: MSC only", label);
    }
}

/* SPC1 in free field arrives with blanks where the padding was; the
 * fixed-field card wants the grid list contiguous.                     */
static void do_spc1(msc_ctx *x, msc_card *c)
{
    msc_card *o = emit(x, "SPC1");
    int i, k = 3, mask, prev = 0;
    msc_set(o, 1, msc_f(c, 1));
    msc_set(o, 2, msc_f(c, 2));
    mask = dof_mask(msc_f(c, 2));
    for (i = 3; i <= c->nfld; i++) {
        const char *s = msc_f(c, i);
        if (!*s) continue;
        if (msc_streq(s, "THRU")) {
            /* G1 THRU G2: written out one by one, so that the map of
             * what is held is right and the reader has nothing to
             * interpret. A range that is not a range is a fatal.     */
            int g2 = msc_fi(c, i + 1, 0), g;
            if (g2 < prev || g2 - prev > 100000) {
                msc_msg_at(MSC_FATAL, 9213, c,
                    "SPC1: %d THRU %d is not a range this front end will "
                    "expand.", prev, g2);
                x->fatal = 1;
                return;
            }
            for (g = prev + 1; g <= g2; g++) {
                msc_seti(o, k++, g);
                constrain_dofs(x, g, mask);
            }
            i++;
            continue;
        }
        msc_set(o, k++, s);
        prev = msc_fi(c, i, 0);
        constrain_dofs(x, prev, mask);
    }
}

/* ------------------------------------------------------------------ */
/* the pass over the bulk data                                         */

typedef enum {
    P_COPY = 0,       /* pass through unchanged                          */
    P_DROP,           /* drop, silently -- it means nothing here         */
    P_SPECIAL         /* has a handler                                   */
} policy;

/* cards NASTRAN-95 reads with the same name, fields and meaning */
static const char *copy_cards[] = {
    "GRID", "GRDSET", "SPOINT", "EPOINT", "SEQGP",
    "CORD1R", "CORD1C", "CORD1S", "CORD2R", "CORD2C", "CORD2S",
    "MAT1", "MAT2", "MAT3", "MAT4", "MAT5",
    "PBAR", "PROD", "PTUBE", "PSHEAR", "PELAS", "PDAMP", "PMASS", "PVISC",
    "CBAR", "CROD", "CTUBE", "CONROD", "CSHEAR", "CVISC",
    "CELAS1", "CELAS2", "CELAS3", "CELAS4",
    "CDAMP1", "CDAMP2", "CDAMP3", "CDAMP4",
    "CMASS1", "CMASS2", "CMASS3", "CMASS4",
    "CONM1", "CONM2",
    "MPC", "MPCADD", "SPC", "SPCADD", "SPCD", "SUPORT", "OMIT", "OMIT1",
    "ASET", "ASET1",
    "FORCE", "FORCE1", "FORCE2", "MOMENT", "MOMENT1", "MOMENT2",
    "GRAV", "LOAD", "PLOAD", "PLOAD1", "PLOAD2",
    "TEMP", "TEMPD", "TEMPRB", "TEMPP1",
    "EIGR", "EIGB", "EIGC", "EIGP",
    "FREQ", "FREQ1", "FREQ2", "TSTEP",
    "TLOAD1", "TLOAD2", "RLOAD1", "RLOAD2", "DLOAD", "DAREA", "DELAY",
    "DPHASE", "TABDMP1", "TABLED1", "TABLED2", "TABLED3", "TABLED4",
    "TABLEM1", "TABLEM2", "TABLEM3", "TABLEM4", "TABLES1", "TABLEST",
    "TF", "TIC", "NOLIN1", "NOLIN2", "NOLIN3", "NOLIN4",
    "AERO", "AEROS", "CAERO1", "PAERO1", "SPLINE1", "SPLINE2", "SET1",
    "FLUTTER", "FLFACT", "MKAERO1", "MKAERO2", "TRIM", "AESTAT", "AESURF",
    "DMI", "DMIG",
    NULL
};

/* cards that describe how MSC should draw or post-process a model and
 * have no bearing on the answer */
static const char *drop_cards[] = {
    "PLOTEL", "PBUSHT", "RESVEC", "MDLPRM", "CBARAO", "MONPNT1",
    NULL
};

static int in_list(const char *n, const char **list)
{
    int i;
    for (i = 0; list[i]; i++) if (msc_streq(n, list[i])) return 1;
    return 0;
}

/* id fields, by card, that hold a grid or element number */
static void renumber_ids(msc_ctx *x, msc_card *c)
{
    static const int f1[]     = { 1 };
    static const int f12[]    = { 1, 2 };
    static const int f134[]   = { 1, 3, 4 };
    static const int f123[]   = { 1, 2, 3 };
    const char *n = c->name;

    if (msc_streq(n, "GRID") || msc_streq(n, "SPOINT"))
        small_ids(x, c, f1, 1);
    else if (msc_streq(n, "CONM2") || msc_streq(n, "CONM1"))
        small_ids(x, c, f12, 2);
    else if (msc_streq(n, "CBAR") || msc_streq(n, "CBEAM") ||
             msc_streq(n, "CBUSH") || msc_streq(n, "CROD") ||
             msc_streq(n, "CTUBE"))
        small_ids(x, c, f134, 3);
    else if (msc_streq(n, "CONROD") || msc_streq(n, "CELAS2") ||
             msc_streq(n, "RBAR"))
        small_ids(x, c, f123, 3);
    else if (msc_streq(n, "CQUAD4") || msc_streq(n, "CQUAD") ||
             msc_streq(n, "CTRIA3") || msc_streq(n, "CQUADR") ||
             msc_streq(n, "CTRIAR")) {
        static const int fq[] = { 1, 3, 4, 5, 6 };
        small_ids(x, c, fq, 5);
    }
    else if (msc_streq(n, "SPC1") || msc_streq(n, "RBE2")) {
        int i;
        for (i = 3; i <= c->nfld; i++) small_id(x, c, i);
    } else if (msc_streq(n, "SPC")) {
        static const int fs[] = { 2, 5 };
        small_ids(x, c, fs, 2);
    }
    /* the rest, as a table: which fields hold a grid id, and from which
     * field onwards every field does (a list card). A card in neither
     * place holds no grid ids, and a new card type that does needs a
     * row here or its grids go unrenumbered -- which the solver reports
     * as an undefined grid point, not silently.                      */
    else {
        static const struct { const char *name; int f[6]; int from; int step; } tab[] = {
            { "SUPORT1", { 0 },          2, 2 },    /* SID G1 C1 G2 C2 ...     */
            { "SUPORT",  { 0 },          1, 2 },    /* G1 C1 G2 C2 ...         */
            { "ASET",    { 0 },          1, 2 },    /* G1 C1 G2 C2 ...         */
            { "OMIT",    { 0 },          1, 2 },
            { "ASET1",   { 0 },          2, 1 },    /* C G1 G2 ...             */
            { "OMIT1",   { 0 },          2, 1 },
            { "SET1",    { 0 },          2, 1 },    /* SID G1 G2 ...           */
            { "SPCD",    { 2, 5 },       0, 0 },
            { "FORCE",   { 2 },          0, 0 },
            { "MOMENT",  { 2 },          0, 0 },
            { "FORCE1",  { 2, 3, 4 },    0, 0 },
            { "MOMENT1", { 2, 3, 4 },    0, 0 },
            { "MPC",     { 0 },          2, 3 },    /* SID G1 C1 A1 G2 C2 A2   */
            { "CELAS1",  { 3, 5 },       0, 0 },
            { "CDAMP1",  { 3, 5 },       0, 0 },
            { "CDAMP2",  { 3, 5 },       0, 0 },
            { "CMASS1",  { 3, 5 },       0, 0 },
            { "CMASS2",  { 3, 5 },       0, 0 },
            { "CVISC",   { 1, 3, 4 },    0, 0 },
            { "CSHEAR",  { 1, 3, 4, 5, 6 }, 0, 0 },
            { "TEMP",    { 0 },          2, 2 },    /* SID G1 T1 G2 T2         */
            { "PLOAD",   { 3, 4, 5, 6 }, 0, 0 },
            { "PLOAD1",  { 2 },          0, 0 },    /* element id              */
            { "GRDSET",  { 0 },          0, 0 },
            { NULL,      { 0 },          0, 0 }
        };
        int k;
        for (k = 0; tab[k].name; k++) {
            if (!msc_streq(n, tab[k].name)) continue;
            {
                int j;
                for (j = 0; j < 6 && tab[k].f[j]; j++) small_id(x, c, tab[k].f[j]);
                if (tab[k].from)
                    for (j = tab[k].from; j <= c->nfld; j += tab[k].step)
                        small_id(x, c, j);
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Plates.
 *
 * MSC's CQUAD4/CTRIA3 with a PSHELL become COSMIC's CQUAD2/CTRIA2 with
 * a PQUAD2/PTRIA2, which are the homogeneous-plate elements of the same
 * formulation.
 *
 * With one exception that matters here. This repository skins its beam
 * models with panels whose PSHELL thickness is 1e-9: they exist so that
 * a post-processor has a surface to draw, and are written thin enough to
 * carry no stiffness and no mass. Carried across literally they
 * contribute a stiffness of the same order as the round-off in the
 * decomposition, and NASTRAN-95 has neither MSC's AUTOSPC nor its
 * MAXRATIO to survive that. So a panel thinner than a millionth of a
 * length unit is dropped, its grids are left to the constraint pass to
 * hold (which is what AUTOSPC does, and these decks ask for AUTOSPC),
 * and the count is reported. A panel with a real thickness is
 * translated.
 */
#define MSC_TMIN 1.0e-6

static int plate_negligible(msc_ctx *x, int pid)
{
    double *t = msc_map_vec(&x->pshell, pid, 0);
    if (!t) return 0;
    return t[0] > 0.0 && t[0] < MSC_TMIN;
}

static void do_plate(msc_ctx *x, msc_card *c, int nnode)
{
    int       pid = msc_fi(c, 2, 0);
    msc_card *o;
    int       i;

    if (plate_negligible(x, pid)) {
        x->st->dropped++;
        msc_tally("dropped: no stiffness", c->name);
        return;
    }
    o = emit(x, nnode == 4 ? "CQUAD2" : "CTRIA2");
    msc_set(o, 1, msc_f(c, 1));
    msc_set(o, 2, msc_f(c, 2));
    for (i = 0; i < nnode; i++) {
        msc_set(o, 3 + i, msc_f(c, 3 + i));
        attach_dofs(x, msc_fi(c, 3 + i, 0), ALL6);
    }
    msc_set(o, 3 + nnode, msc_f(c, 3 + nnode));   /* THETA */
    msc_tally(nnode == 4 ? "CQUAD4 -> CQUAD2" : "CTRIA3 -> CTRIA2", c->name);
}

static void do_pshell(msc_ctx *x, msc_card *c)
{
    int       pid = msc_fi(c, 1, 0);
    msc_card *o;

    if (plate_negligible(x, pid)) {
        x->st->dropped++;
        msc_tally("dropped: no stiffness", "PSHELL");
        return;
    }
    if (!msc_blank(c, 4) && msc_fi(c, 4, 0) != msc_fi(c, 2, 0)) {
        msc_msg_at(MSC_WARN, 9215, c,
            "PSHELL %d gives different materials for membrane (%s) and "
            "bending (%s).\nPQUAD2 is a homogeneous plate and takes one; the "
            "membrane material is used.", pid, msc_f(c, 2), msc_f(c, 4));
    }
    o = emit(x, "PQUAD2");
    msc_set(o, 1, msc_f(c, 1));
    msc_set(o, 2, msc_f(c, 2));
    msc_set(o, 3, msc_f(c, 3));
    msc_set(o, 4, msc_f(c, 8));
    msc_tally("PSHELL -> PQUAD2", "PSHELL");
    /* a triangular companion, in case the deck hangs CTRIA3s off the
     * same PSHELL: the property ids live in different tables here     */
    o = emit(x, "PTRIA2");
    msc_set(o, 1, msc_f(c, 1));
    msc_set(o, 2, msc_f(c, 2));
    msc_set(o, 3, msc_f(c, 3));
    msc_set(o, 4, msc_f(c, 8));
}

static void do_suport1(msc_ctx *x, msc_card *c);

/* Ids first, over the whole deck, before anything reads one.
 *
 * NASTRAN packs a grid id and its component into one 32-bit word as
 * id*10 + component, so an id over 2^24-1 does not survive the trip.
 * This repository numbers its aero reference grid 99999999, which is
 * eight digits and one of those. Renumbering has to happen before the
 * model tables are built, or the tables are keyed by the old number
 * and every later lookup misses -- which is not a crash, it is a
 * reference grid that quietly looks unattached and gets constrained.
 */
static void renumber_pass(msc_ctx *x)
{
    int i;
    for (i = 0; i < x->d->nbulk; i++) renumber_ids(x, &x->d->bulk[i]);
    if (x->st->renumbered)
        msc_msg(MSC_INFO, 9302,
            "%d identification number%s above %d had to be renumbered: "
            "NASTRAN\npacks an id and a component into one 32-bit word. "
            "Every reference to\nthem moved with them; the translated deck "
            "shows the numbers used.",
            x->st->renumbered, x->st->renumbered == 1 ? "" : "s", ID_LIMIT);
}

static void translate_bulk(msc_ctx *x)
{
    int i;
    for (i = 0; i < x->d->nbulk; i++) {
        msc_card   *c = &x->d->bulk[i];
        const char *n = c->name;

        /* the SOL 200 design model was consumed by the optimiser */
        if (c->dropped) continue;

        if (msc_streq(n, "RBAR"))        { do_rigid_bar(x, c); continue; }
        if (msc_streq(n, "RBE2"))        { do_rbe2(x, c);      continue; }
        if (msc_streq(n, "CBUSH"))       { do_cbush(x, c);     continue; }
        if (msc_streq(n, "PBUSH"))       { continue; }   /* used by CBUSH */
        if (msc_streq(n, "PBARL"))       { do_pbarl(x, c);     continue; }
        if (msc_streq(n, "EIGRL"))       { do_eigrl(x, c);     continue; }
        if (msc_streq(n, "PARAM"))       { do_param(x, c);     continue; }
        if (msc_streq(n, "SPC1"))        { do_spc1(x, c);      continue; }
        if (msc_streq(n, "SUPORT1"))     { do_suport1(x, c);   continue; }
        if (msc_streq(n, "CQUAD4") || msc_streq(n, "CQUAD") ||
            msc_streq(n, "CQUADR"))      { do_plate(x, c, 4);  continue; }
        if (msc_streq(n, "CTRIA3") || msc_streq(n, "CTRIAR"))
                                         { do_plate(x, c, 3);  continue; }
        if (msc_streq(n, "PSHELL"))      { do_pshell(x, c);    continue; }

        if (in_list(n, copy_cards)) {
            pass_through(x, c);
            /* elements hold their grids up: the auto-SPC needs to know  */
            if (msc_streq(n, "CBAR"))
                { attach_dofs(x, msc_fi(c, 3, 0), ALL6);
                  attach_dofs(x, msc_fi(c, 4, 0), ALL6); }
            else if (msc_streq(n, "CROD") || msc_streq(n, "CTUBE"))
                { attach_dofs(x, msc_fi(c, 3, 0), XYZ3);
                  attach_dofs(x, msc_fi(c, 4, 0), XYZ3); }
            else if (msc_streq(n, "CONROD"))
                { attach_dofs(x, msc_fi(c, 2, 0), XYZ3);
                  attach_dofs(x, msc_fi(c, 3, 0), XYZ3); }
            else if (msc_streq(n, "CELAS2") || msc_streq(n, "CELAS1")) {
                attach_dofs(x, msc_fi(c, 3, 0), 1 << (msc_fi(c, 4, 1) - 1));
                if (msc_fi(c, 5, 0))
                    attach_dofs(x, msc_fi(c, 5, 0), 1 << (msc_fi(c, 6, 1) - 1));
            } else if (msc_streq(n, "SPC")) {
                constrain_dofs(x, msc_fi(c, 2, 0), dof_mask(msc_f(c, 3)));
            }
            continue;
        }
        if (in_list(n, drop_cards)) {
            x->st->dropped++;
            continue;
        }
        msc_msg_at(MSC_FATAL, 9299, c,
            "This front end does not know the card %s, so the model it would\n"
            "build is not the model the deck describes.\n"
            "FIX   Write the same thing with a card NASTRAN-95 has, or say\n"
            "      what %s should become and it can be added to mscxlat.c.",
            n, n);
        x->fatal = 1;
    }
}

/* ------------------------------------------------------------------ */
/* what MSC's AUTOSPC does: hold the dofs nothing is attached to.      */

static void auto_spc(msc_ctx *x)
{
    int iter = 0, g, dummy;
    msc_card *o = NULL;
    int n = 0;

    while (msc_map_next(&x->grid, &iter, &g, &dummy)) {
        int has = msc_map_get(&x->attach, g, 0);
        int con = msc_map_get(&x->constr, g, 0);
        int free_dofs = (~has) & ALL6 & (~con);
        int j;
        char list[8];
        int  k = 0;
        if (!free_dofs) continue;
        for (j = 0; j < 6; j++) if (free_dofs & (1 << j)) list[k++] = (char) ('1' + j);
        list[k] = '\0';
        o = emit(x, "SPC1");
        msc_seti(o, 1, x->auto_set);
        msc_set (o, 2, list);
        msc_seti(o, 3, g);
        x->st->autospc += k;
        n++;
    }
    if (n) {
        msc_msg(MSC_INFO, 9301,
            "%d grid%s had components with no stiffness attached to them; they\n"
            "are constrained in SPC set %d, which is what MSC's AUTOSPC would\n"
            "do. NASTRAN-95 has no AUTOSPC and would give a singular matrix.",
            n, n == 1 ? "" : "s", x->auto_set);
    }
}

/* ------------------------------------------------------------------ */
/* SUPORT1 -> SUPORT: the 1970s card is not set-selected.              */

static void do_suport1(msc_ctx *x, msc_card *c)
{
    /* SUPORT1 SID G1 C1 G2 C2 ... */
    msc_card *o = emit(x, "SUPORT");
    int i, k = 1;
    for (i = 2; i + 1 <= c->nfld; i += 2) {
        if (!*msc_f(c, i)) continue;
        msc_set(o, k++, msc_f(c, i));
        msc_set(o, k++, msc_f(c, i + 1));
    }
    msc_msg(MSC_INFO, 9214,
        "SUPORT1 %s became a SUPORT. NASTRAN-95 has no set-selected form, so\n"
        "the reference degrees of freedom are always in force; the case\n"
        "control SUPORT request is dropped with it.", msc_f(c, 1));
}

/* ------------------------------------------------------------------ */

int msc_translate_deck(msc_deck *d, const char *outpath, msc_stats *st)
{
    msc_ctx     x;
    FILE       *fp;
    const char *app = "DISPLACEMENT";
    const char *what = "";
    int         rf = 0, rc, i;
    int         spc_sel = 0, method_sel = 0;
    int         ngrid;

    memset(&x, 0, sizeof(x));
    x.d = d;
    x.st = st;
    x.next_big = ID_LIMIT;
    x.unit_mat = 9990;
    x.auto_set = 9998;
    x.shift = 0.5;
    msc_list_init(&x.out);
    msc_map_init(&x.grid, 4096);
    msc_map_init(&x.pbush, 64);
    msc_map_init(&x.pbar, 256);
    msc_map_init(&x.pshell, 64);
    msc_map_init(&x.stiff, 4096);
    msc_map_init(&x.attach, 4096);
    msc_map_init(&x.constr, 1024);
    msc_map_init(&x.remap, 256);
    msc_map_init(&x.held, 256);

    /* which solution */
    rc = msc_sol_lookup(d->sol, d->solname, &app, &rf, &what);
    if (rc == 2) {
        msc_msg(MSC_FATAL, 9110,
            "The deck asks for SOL %d, %s. NASTRAN-95 has no equivalent: the\n"
            "capability was added to MSC Nastran after the 1995 source this\n"
            "solver is built from.\n"
            "FIX   Use one of the solutions it does have: 101 statics, 103\n"
            "      normal modes, 105 buckling, 107-112 the dynamic solutions,\n"
            "      145 flutter, 146 gust.", d->sol, what);
        return 1;
    }
    if (rc == 1) {
        msc_msg(MSC_FATAL, 9111,
            "The deck's executive control names no solution this front end\n"
            "recognises (SOL %d%s%s).\n"
            "FIX   Write SOL 103 for normal modes, 101 for statics.",
            d->sol, d->solname[0] ? " " : "", d->solname);
        return 1;
    }
    st->sol = d->sol;
    st->rf = rf;
    strncpy(st->app, app, sizeof(st->app) - 1);

    renumber_pass(&x);
    scan(&x);
    ngrid = msc_map_count(&x.grid);
    translate_bulk(&x);
    if (x.fatal) return 1;
    auto_spc(&x);

    /* the SPC the case control selects, plus the auto-SPC set */
    msc_case_find(d, "SPC", NULL);

    fp = fopen(outpath, "w");
    if (!fp) {
        msc_msg(MSC_FATAL, 9112, "cannot write the translated deck to %s",
                outpath);
        return 1;
    }

    /* ---- executive control ---------------------------------------- */
    /* The NASTRAN card has to be the very first thing in the file, ahead
     * of any comment. BANDIT, the bandwidth resequencer, works in a
     * scratch array sized from the grid count and stops the run in
     * SCHEME on a model of a few thousand grids with many rigid
     * elements; the decomposition does not need it at this size.      */
    if (ngrid > 300) {
        fprintf(fp, "NASTRAN BANDIT=-1\n");
        msc_msg(MSC_INFO, 9113,
            "%d grids: the bandwidth resequencer is turned off (NASTRAN\n"
            "BANDIT=-1). Its scratch array is sized for 1970s models and\n"
            "stops the run above a few hundred grids with many rigid\n"
            "elements.", ngrid);
    }
    fprintf(fp, "$ translated from MSC dialect by nastran95ase\n");
    fprintf(fp, "$ this file is written by the solver; edit the MSC deck, not this\n");
    fprintf(fp, "ID      HALO,ASE\n");
    fprintf(fp, "APP     %s\n", app);
    fprintf(fp, "SOL     %d,0\n", rf);
    fprintf(fp, "TIME    600\n");
    msc_op4_alter(fp, rf);
    fprintf(fp, "CEND\n");

    /* ---- case control --------------------------------------------- */
    if (!d->title[0]) fprintf(fp, "TITLE = TRANSLATED MSC DECK\n");
    /* NASTRAN-95 stops a job when the print file passes 20,000 lines
     * (UFM 3019). A hundred modes on a few thousand grids is far past
     * that and the limit is not a resource, it is a 1970s guard.      */
    fprintf(fp, "MAXLINES = 99999999\n");

    /* Whatever this front end adds to the case control goes ABOVE the
     * first SUBCASE, where it applies to every subcase. Written after
     * the deck's own lines it would fall inside the last subcase, and
     * the first subcase would run without its constraints -- which is
     * not an error message, it is a singular matrix.                  */
    {
        char val[MSC_LINELEN];
        int  own_spc = 0, own_method = 0;
        if (msc_case_find(d, "SPC", val)) own_spc = atoi(val);
        if (msc_case_find(d, "METHOD", val)) own_method = atoi(val);
        if (!own_method && x.have_eig) fprintf(fp, "METHOD = %d\n", x.eig_sid);
        if (!msc_case_has_output(d)) {
            fprintf(fp, "DISPLACEMENT = ALL\n");
            msc_msg(MSC_INFO, 9114, "the case control asked for no output, so "
                    "DISPLACEMENT = ALL is added.");
        }
        if (st->autospc > 0) {
            /* the deck's own set and the auto-SPC set, combined; the
             * deck's own SPC request is held back below so that there
             * is one selection, not two                               */
            if (own_spc > 0) {
                msc_card *o = msc_list_add(&x.out, "SPCADD");
                msc_seti(o, 1, 9999);
                msc_seti(o, 2, own_spc);
                msc_seti(o, 3, x.auto_set);
                fprintf(fp, "SPC = 9999\n");
            } else {
                fprintf(fp, "SPC = %d\n", x.auto_set);
            }
        }
    }
    msc_case_write(fp, d, &spc_sel, &method_sel, st->autospc > 0, 0);
    fprintf(fp, "BEGIN BULK\n");

    /* ---- bulk data ------------------------------------------------- */
    for (i = 0; i < x.out.n; i++) msc_write_card(fp, &x.out.c[i]);
    /* the unit-modulus material a translated CBUSH rod refers to */
    {
        msc_card m;
        memset(&m, 0, sizeof(m));
        strcpy(m.name, "MAT1");
        msc_seti(&m, 1, x.unit_mat);
        msc_set (&m, 2, "1.0");
        msc_set (&m, 3, "1.0");
        msc_set (&m, 5, "0.0");
        msc_write_card(fp, &m);
        free(m.fld);
    }
    fprintf(fp, "ENDDATA\n");
    fclose(fp);

    st->nmodes = x.nmodes;
    st->shift  = x.shift;
    {
        int iter = 0, old, new_id;
        while (msc_map_next(&x.remap, &iter, &old, &new_id))
            msc_f06_remap(new_id, old);
    }

    msc_list_free(&x.out);
    msc_map_free(&x.grid);
    msc_map_free(&x.pbush);
    msc_map_free(&x.pbar);
    msc_map_free(&x.pshell);
    msc_map_free(&x.stiff);
    msc_map_free(&x.attach);
    msc_map_free(&x.constr);
    msc_map_free(&x.remap);
    msc_map_free(&x.held);
    return msc_nfatal() ? 1 : 0;
}

int msc_translate(const char *in, const char *out, msc_stats *st)
{
    msc_deck d;
    int      rc;

    memset(st, 0, sizeof(*st));
    if (msc_read(in, &d)) return 1;
    if (msc_op4_scan(&d)) { msc_free(&d); return 1; }
    if (d.nbulk == 0) {
        msc_msg(MSC_FATAL, 9013,
            "%s has no bulk data. Either the deck is empty or BEGIN BULK is\n"
            "missing, and without it every card reads as case control.", in);
        msc_free(&d);
        return 1;
    }
    rc = msc_translate_deck(&d, out, st);
    msc_free(&d);
    return rc;
}
