/* HALO: SOL 200 -- design sensitivity and optimisation.
 *
 * NASTRAN-95 has no optimiser. It was written before structural
 * optimisation was a production capability, and nothing in NASA's tree
 * computes a sensitivity. So this is not a translation like the rest of
 * the front end: the design cycle is implemented here, around the
 * solver, in the architecture MSC uses and for the same reasons.
 *
 * What a design cycle does, in MSC's terms and in this one:
 *
 *   1  analyse the current design (one full solver run)
 *   2  evaluate the responses and normalise the constraints
 *   3  screen: drop constraints that are far from active
 *   4  compute sensitivities of the retained responses
 *   5  build an explicit approximation of the problem
 *   6  optimise THAT, which is cheap, to get the next design
 *   7  test convergence, and go round again
 *
 * The expensive step is 4. MSC computes sensitivities semi-analytically
 * inside the solver, where the factored stiffness matrix is already to
 * hand; from outside it there is no such access, so they are computed by
 * finite difference -- one extra solver run per design variable per
 * cycle. That is the honest cost and it is printed: a design model with
 * twenty variables and ten cycles is two hundred and ten analyses. It is
 * the "black box" coupling MSC's own guide calls impractical for large
 * problems, and it is impractical for large problems here too. For a
 * sizing problem of a few dozen variables on a beam model -- which is
 * what this repository has -- it runs in minutes and it is correct.
 *
 * What is implemented, and deliberately not more:
 *
 *   DESVAR, DVPREL1, DVMREL1, DLINK      the design model
 *   DRESP1  WEIGHT VOLUME FREQ EIGN      responses
 *           DISP STRESS
 *   DCONSTR, DCONADD, DSCREEN, DOPTPRM   constraints and parameters
 *   DESOBJ, DESSUB, DESGLB, ANALYSIS     case control
 *
 * Anything else in the design model is a numbered fatal that names the
 * card. An optimiser that silently ignores a design variable or a
 * constraint produces a converged, plausible, wrong answer, which is the
 * worst thing this code could do.
 *
 * The approximation is convex linearisation (Fleury's CONLIN, which is
 * MSC's APRCOD=3): each function is linearised in x where its derivative
 * is positive and in 1/x where it is negative, which is exact for the
 * stress and displacement of a statically determinate member and
 * conservative elsewhere. The resulting subproblem is separable and
 * convex, so it is solved through its dual, which is a maximisation over
 * one multiplier per retained constraint with a closed-form primal
 * solution inside. No linear programming, no matrix factorisation, and
 * the same answer.
 */
#include "msc.h"
#include "mscopt.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* reading the design model out of the bulk data                       */

static void set_defaults(opt_param *p)
{
    p->desmax = 5;          /* MSC's default; almost always too low     */
    p->conv1  = 0.001;
    p->conv2  = 1.0e-20;
    p->convdv = 0.001;
    p->convpr = 0.001;
    p->gmax   = 0.005;
    p->delx   = 0.5;
    p->dxmin  = 0.05;
    p->delp   = 0.2;
    p->dpmin  = 0.01;
    /* MSC's DELB is 1e-4, for a semi-analytic derivative taken inside
     * the solver. Here the perturbed design goes through an eight-column
     * property card and comes back through a seven-figure print file,
     * so a step of 1e-4 is resolved to a few parts in a hundred and the
     * gradient inherits that. One per cent is resolved to 1e-5 and the
     * truncation error of a forward difference at one per cent is
     * smaller than the approximation's own. DOPTPRM DELB overrides.  */
    p->delb   = 0.01;
    p->ct     = -0.03;
    p->ctmin  = 0.003;
    p->gscal  = 0.001;
    p->aprcod = 3;          /* convex; MSC's mixed default needs both   */
    p->iprint = 0;
    p->p1     = 1;
    p->p2     = 1;
    p->trs    = -0.5;
    p->nstr   = 20;
}

static void doptprm(opt_model *m, msc_card *c)
{
    int i;
    for (i = 1; i + 1 <= c->nfld; i += 2) {
        char n[16];
        double v;
        strncpy(n, msc_f(c, i), sizeof(n) - 1);
        n[sizeof(n) - 1] = '\0';
        msc_trim(n);
        msc_upper(n);
        if (!n[0]) continue;
        v = msc_fd(c, i + 1, 0.0);
        if      (msc_streq(n, "DESMAX")) m->prm.desmax = (int) v;
        else if (msc_streq(n, "CONV1"))  m->prm.conv1  = v;
        else if (msc_streq(n, "CONV2"))  m->prm.conv2  = v;
        else if (msc_streq(n, "CONVDV")) m->prm.convdv = v;
        else if (msc_streq(n, "CONVPR")) m->prm.convpr = v;
        else if (msc_streq(n, "GMAX"))   m->prm.gmax   = v;
        else if (msc_streq(n, "DELX"))   m->prm.delx   = v;
        else if (msc_streq(n, "DXMIN"))  m->prm.dxmin  = v;
        else if (msc_streq(n, "DELP"))   m->prm.delp   = v;
        else if (msc_streq(n, "DPMIN"))  m->prm.dpmin  = v;
        else if (msc_streq(n, "DELB"))   m->prm.delb   = v;
        else if (msc_streq(n, "CT"))     m->prm.ct     = v;
        else if (msc_streq(n, "CTMIN"))  m->prm.ctmin  = v;
        else if (msc_streq(n, "GSCAL"))  m->prm.gscal  = v;
        else if (msc_streq(n, "APRCOD")) m->prm.aprcod = (int) v;
        else if (msc_streq(n, "IPRINT")) m->prm.iprint = (int) v;
        else if (msc_streq(n, "P1"))     m->prm.p1     = (int) v;
        else if (msc_streq(n, "P2"))     m->prm.p2     = (int) v;
        else if (msc_streq(n, "METHOD") || msc_streq(n, "OPTCOD") ||
                 msc_streq(n, "PENAL")  || msc_streq(n, "ISCAL")  ||
                 msc_streq(n, "IGMAX")  || msc_streq(n, "NASPR0") ||
                 msc_streq(n, "DISCOD") || msc_streq(n, "DISBEG") ||
                 msc_streq(n, "FSDMAX") || msc_streq(n, "FSDALP") ||
                 msc_streq(n, "PLVIOL") || msc_streq(n, "PTOL")   ||
                 msc_streq(n, "TREGION")) {
            msc_msg(MSC_INFO, 9401,
                "DOPTPRM %s is read and not used: this driver has one\n"
                "optimiser (convex linearisation solved through its dual) and\n"
                "no discrete, trust-region or fully-stressed mode.", n);
        } else {
            msc_msg(MSC_WARN, 9402, "DOPTPRM %s is not a parameter this "
                    "driver knows; ignored.", n);
        }
    }
}

int opt_read_design_model(msc_deck *d, opt_model *m)
{
    int i, k;
    set_defaults(&m->prm);

    for (i = 0; i < d->nbulk; i++) {
        msc_card   *c = &d->bulk[i];
        const char *n = c->name;

        if (msc_streq(n, "DESVAR")) {
            opt_desvar *v;
            if (m->ndv >= OPT_MAXDV) {
                msc_msg_at(MSC_FATAL, 9403, c, "more than %d DESVAR entries",
                           OPT_MAXDV);
                return 1;
            }
            v = &m->dv[m->ndv++];
            memset(v, 0, sizeof(*v));
            v->id = msc_fi(c, 1, 0);
            strncpy(v->label, msc_f(c, 2), sizeof(v->label) - 1);
            v->xinit = msc_fd(c, 3, 0.0);
            v->xlb   = msc_fd(c, 4, -1.0e20);
            v->xub   = msc_fd(c, 5,  1.0e20);
            v->delxv = msc_fd(c, 6, 0.0);
            v->x     = v->xinit;
            if (!msc_blank(c, 7)) {
                msc_msg_at(MSC_FATAL, 9404, c,
                    "DESVAR %d names a DDVAL: discrete design variables are\n"
                    "not implemented.\n"
                    "FIX   Leave field 8 blank and round the converged\n"
                    "      continuous design to the sizes you can buy.",
                    v->id);
                return 1;
            }
            c->dropped = 1;
        } else if (msc_streq(n, "DVPREL1") || msc_streq(n, "DVMREL1")) {
            opt_rel *r;
            if (m->nrel >= OPT_MAXREL) {
                msc_msg_at(MSC_FATAL, 9403, c, "too many design relations");
                return 1;
            }
            r = &m->rel[m->nrel++];
            memset(r, 0, sizeof(*r));
            r->material = msc_streq(n, "DVMREL1");
            r->id  = msc_fi(c, 1, 0);
            strncpy(r->type, msc_f(c, 2), sizeof(r->type) - 1);
            msc_upper(r->type);
            r->pid = msc_fi(c, 3, 0);
            strncpy(r->pname, msc_f(c, 4), sizeof(r->pname) - 1);
            msc_upper(r->pname);
            r->pmin = msc_fd(c, 5, -1.0e35);
            r->pmax = msc_fd(c, 6,  1.0e20);
            r->c0   = msc_fd(c, 7, 0.0);
            for (k = 9; k + 1 <= c->nfld && r->ndv < 16; k += 2) {
                if (msc_blank(c, k)) continue;
                r->dvid[r->ndv] = msc_fi(c, k, 0);
                r->coef[r->ndv] = msc_fd(c, k + 1, 0.0);
                r->ndv++;
            }
            if (r->ndv == 0) {
                msc_msg_at(MSC_FATAL, 9405, c,
                    "%s %d names no design variable. The continuation entry\n"
                    "carrying DVID1 and COEF1 is required.", n, r->id);
                return 1;
            }
            c->dropped = 1;
        } else if (msc_streq(n, "DLINK")) {
            opt_link *l;
            if (m->nlink >= OPT_MAXLINK) return 1;
            l = &m->link[m->nlink++];
            memset(l, 0, sizeof(*l));
            l->id    = msc_fi(c, 1, 0);
            l->ddvid = msc_fi(c, 2, 0);
            l->c0    = msc_fd(c, 3, 0.0);
            l->cmult = msc_fd(c, 4, 1.0);
            for (k = 5; k + 1 <= c->nfld && l->n < 16; k += 2) {
                if (msc_blank(c, k)) continue;
                l->idv[l->n] = msc_fi(c, k, 0);
                l->c[l->n]   = msc_fd(c, k + 1, 0.0);
                l->n++;
            }
            c->dropped = 1;
        } else if (msc_streq(n, "DRESP1")) {
            opt_resp *r;
            if (m->nresp >= OPT_MAXRESP) return 1;
            r = &m->resp[m->nresp++];
            memset(r, 0, sizeof(*r));
            r->id = msc_fi(c, 1, 0);
            strncpy(r->label, msc_f(c, 2), sizeof(r->label) - 1);
            strncpy(r->rtype, msc_f(c, 3), sizeof(r->rtype) - 1);
            msc_upper(r->rtype);
            strncpy(r->ptype, msc_f(c, 4), sizeof(r->ptype) - 1);
            msc_upper(r->ptype);
            r->region = msc_fi(c, 5, 0);
            r->atta   = msc_fi(c, 6, 0);
            strncpy(r->attb, msc_f(c, 7), sizeof(r->attb) - 1);
            for (k = 8; k <= c->nfld && r->natt < OPT_MAXATT; k++) {
                if (msc_blank(c, k)) continue;
                r->atti[r->natt++] = msc_fi(c, k, 0);
            }
            c->dropped = 1;
        } else if (msc_streq(n, "DCONSTR")) {
            opt_con *o;
            if (m->ncon >= OPT_MAXCON) return 1;
            o = &m->con[m->ncon++];
            o->dcid   = msc_fi(c, 1, 0);
            o->rid    = msc_fi(c, 2, 0);
            o->lallow = msc_fd(c, 3, -1.0e20);
            o->uallow = msc_fd(c, 4,  1.0e20);
            c->dropped = 1;
        } else if (msc_streq(n, "DCONADD")) {
            /* flattened: every DCONSTR it names joins this set */
            int dcid = msc_fi(c, 1, 0);
            for (k = 2; k <= c->nfld; k++) {
                int sub = msc_fi(c, k, 0), j;
                if (!sub) continue;
                for (j = 0; j < m->ncon; j++)
                    if (m->con[j].dcid == sub) {
                        if (m->ncon >= OPT_MAXCON) return 1;
                        m->con[m->ncon] = m->con[j];
                        m->con[m->ncon].dcid = dcid;
                        m->ncon++;
                    }
            }
            c->dropped = 1;
        } else if (msc_streq(n, "DOPTPRM")) {
            doptprm(m, c);
            c->dropped = 1;
        } else if (msc_streq(n, "DSCREEN")) {
            m->prm.trs  = msc_fd(c, 2, -0.5);
            m->prm.nstr = msc_fi(c, 3, 20);
            c->dropped = 1;
        } else if (msc_streq(n, "DRESP2") || msc_streq(n, "DRESP3") ||
                   msc_streq(n, "DVPREL2") || msc_streq(n, "DVMREL2") ||
                   msc_streq(n, "DVCREL1") || msc_streq(n, "DVCREL2") ||
                   msc_streq(n, "DVGRID")  || msc_streq(n, "DEQATN")  ||
                   msc_streq(n, "DTABLE")  || msc_streq(n, "DDVAL")   ||
                   msc_streq(n, "MODTRAK") || msc_streq(n, "DVSHAP")) {
            msc_msg_at(MSC_FATAL, 9406, c,
                "%s is part of the design model and this driver does not\n"
                "implement it, so the problem it would pose is not the problem\n"
                "that would be solved.\n"
                "FIX   %s", n,
                msc_streq(n, "DRESP2") || msc_streq(n, "DEQATN") ?
                  "Write the response as a DRESP1, or compute it outside." :
                msc_streq(n, "DVGRID") || msc_streq(n, "DVSHAP") ?
                  "Shape optimisation is not implemented; size the properties."
                : "Use the type-1 form of the card.");
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* case control                                                        */

int opt_read_case(msc_deck *d, opt_model *m)
{
    char val[MSC_LINELEN];
    int  i;

    m->objid = 0;
    strcpy(m->analysis, "STATICS");

    for (i = 0; i < d->ncase; i++) {
        char  up[MSC_LINELEN];
        char *eq;
        strncpy(up, d->cases[i], sizeof(up) - 1);
        up[sizeof(up) - 1] = '\0';
        msc_upper(up);
        msc_trim(up);
        eq = strchr(up, '=');
        if (!eq) continue;
        if (strncmp(up, "DESOBJ", 6) == 0) {
            m->objid  = atoi(eq + 1);
            m->objmax = (strstr(up, "(MAX") != NULL);
        } else if (strncmp(up, "DESSUB", 6) == 0) {
            m->dessub = atoi(eq + 1);
        } else if (strncmp(up, "DESGLB", 6) == 0) {
            m->desglb = atoi(eq + 1);
        } else if (strncmp(up, "ANALYSIS", 8) == 0) {
            char *p = eq + 1;
            while (*p == ' ') p++;
            strncpy(m->analysis, p, sizeof(m->analysis) - 1);
            msc_trim(m->analysis);
        }
    }
    (void) val;
    if (!m->objid) {
        msc_msg(MSC_FATAL, 9407,
            "SOL 200 with no DESOBJ: the deck says what may change and what\n"
            "it must satisfy, but not what to make smaller.\n"
            "FIX   Add DESOBJ(MIN) = <DRESP1 id> to the case control.");
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* design variables -> properties                                      */

/* which data field of a property card a name refers to */
static int prop_field(const char *type, const char *name)
{
    if (msc_streq(type, "PROD")) {
        if (msc_streq(name, "A"))   return 3;
        if (msc_streq(name, "J"))   return 4;
        if (msc_streq(name, "NSM")) return 6;
    } else if (msc_streq(type, "PBAR")) {
        if (msc_streq(name, "A"))   return 3;
        if (msc_streq(name, "I1"))  return 4;
        if (msc_streq(name, "I2"))  return 5;
        if (msc_streq(name, "J"))   return 6;
        if (msc_streq(name, "NSM")) return 7;
    } else if (msc_streq(type, "PSHELL")) {
        if (msc_streq(name, "T"))   return 3;
        if (msc_streq(name, "NSM")) return 8;
    } else if (msc_streq(type, "PSHEAR")) {
        if (msc_streq(name, "T"))   return 3;
        if (msc_streq(name, "NSM")) return 4;
    } else if (msc_streq(type, "PTUBE")) {
        if (msc_streq(name, "OD"))  return 3;
        if (msc_streq(name, "T"))   return 4;
        if (msc_streq(name, "NSM")) return 5;
    } else if (msc_streq(type, "PELAS")) {
        if (msc_streq(name, "K"))   return 2;
    } else if (msc_streq(type, "MAT1")) {
        if (msc_streq(name, "E"))   return 2;
        if (msc_streq(name, "G"))   return 3;
        if (msc_streq(name, "NU"))  return 4;
        if (msc_streq(name, "RHO")) return 5;
        if (msc_streq(name, "A"))   return 6;
    }
    return 0;
}

static msc_card *find_card(msc_deck *d, const char *type, int id)
{
    int i;
    for (i = 0; i < d->nbulk; i++) {
        msc_card *c = &d->bulk[i];
        if (msc_streq(c->name, type) && msc_fi(c, 1, 0) == id) return c;
    }
    return NULL;
}

/* the dependent variables, from the DLINKs */
static void apply_links(opt_model *m)
{
    int i, j, k;
    for (i = 0; i < m->nlink; i++) {
        opt_link *l = &m->link[i];
        double    v = 0.0;
        for (k = 0; k < l->n; k++)
            for (j = 0; j < m->ndv; j++)
                if (m->dv[j].id == l->idv[k]) v += l->c[k] * m->dv[j].x;
        v = l->c0 + l->cmult * v;
        for (j = 0; j < m->ndv; j++)
            if (m->dv[j].id == l->ddvid) { m->dv[j].x = v; m->dv[j].linked = 1; }
    }
}

/* returns 0, or 1 when a property hit a bound (reported by the caller) */
int opt_apply_design(msc_deck *d, opt_model *m)
{
    int i, k, j, clipped = 0;
    apply_links(m);
    for (i = 0; i < m->nrel; i++) {
        opt_rel  *r = &m->rel[i];
        msc_card *c = find_card(d, r->type, r->pid);
        double    p = r->c0;
        int       f;
        if (!c) {
            msc_msg(MSC_FATAL, 9408,
                "%s %d, named by a design relation, is not in the deck.",
                r->type, r->pid);
            return -1;
        }
        f = prop_field(r->type, r->pname);
        if (!f) {
            msc_msg(MSC_FATAL, 9409,
                "a design relation names %s of a %s, which this driver cannot\n"
                "place. It knows A, I1, I2, J, NSM on a PBAR; A, J on a PROD;\n"
                "T on a PSHELL or PSHEAR; OD, T on a PTUBE; E, G, NU, RHO on\n"
                "a MAT1.", r->pname, r->type);
            return -1;
        }
        for (k = 0; k < r->ndv; k++)
            for (j = 0; j < m->ndv; j++)
                if (m->dv[j].id == r->dvid[k]) p += r->coef[k] * m->dv[j].x;
        if (p < r->pmin) { p = r->pmin; clipped = 1; }
        if (p > r->pmax) { p = r->pmax; clipped = 1; }
        msc_setd(c, f, p);
    }
    return clipped;
}

/* the property values as the deck carried them, for PTOL-style checking */
void opt_record_p0(msc_deck *d, opt_model *m)
{
    int i;
    for (i = 0; i < m->nrel; i++) {
        opt_rel  *r = &m->rel[i];
        msc_card *c = find_card(d, r->type, r->pid);
        int       f = prop_field(r->type, r->pname);
        r->p0 = (c && f) ? msc_fd(c, f, 0.0) : 0.0;
    }
}
