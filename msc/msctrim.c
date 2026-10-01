/* HALO: SOL 144, static aeroelastic trim -- the front end's part.
 *
 * COSMIC NASTRAN has no static aeroelastic solution and no cards for one.
 * A SOL 144 deck is therefore run as a DMAP program the front end writes
 * (APP DMAP, aet_write_dmap below): the inertia relief structure sequence
 * of rigid format DISP 2, the aerodynamic modules of AERO 10 (APD, GI,
 * AMG), and the new module AETRIM (mis/aetrim.f, mscaest.c) for the
 * trim. The static aeroelastic cards -- AEROS, AESTAT, AESURF, AELIST,
 * AELINK, TRIM, PARAM AUNITS -- are taken out of the bulk data
 * here and kept in aet_g for AETRIM, which runs later in this same
 * process; the case control's TRIM = n is taken out the same way. What
 * NASTRAN-95's aero modules need instead is written: an AERO card from
 * AEROS, MKAERO1 cards at the trim Machs and a reduced frequency of
 * ~0 (COSMIC rejects k = 0; see aet_kred), and a dummy EIGR so that DPD
 * has a dynamics pool to make the aero tables from.
 *
 * SOL144.md in the fork has the theory, the design and the conventions
 * (AELINK's sign among them).
 */
#include "msctrim.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

aet_model aet_g;

/* the reduced frequency the aerodynamics are computed at: COSMIC's IFP
 * rejects k = 0 on MKAERO1 (UFM 316), so a k small enough that the
 * oscillatory part of the doublet lattice kernel is below the single
 * precision of the AIC; N95_TRIM_KRED overrides it                    */
#define AET_KRED_DEFAULT 1.0e-4

static void lab_copy(char *dst, const char *src)
{
    int i;
    for (i = 0; i < AET_LABLEN - 1 && src[i]; i++)
        dst[i] = (char) toupper((unsigned char) src[i]);
    dst[i] = '\0';
}

static void free_model(void)
{
    int i;
    for (i = 0; i < aet_g.nlist; i++) free(aet_g.list[i].ids);
    for (i = 0; i < aet_g.nlink; i++) { free(aet_g.link[i].ind); free(aet_g.link[i].c); }
    for (i = 0; i < aet_g.ntrim; i++) { free(aet_g.trim[i].lab); free(aet_g.trim[i].ux); }
    free(aet_g.stat); free(aet_g.surf); free(aet_g.list); free(aet_g.link);
    free(aet_g.trim); free(aet_g.sub_id); free(aet_g.sub_trim); free(aet_g.mach);
}

void aet_reset(void)
{
    const char *e;
    free_model();
    memset(&aet_g, 0, sizeof(aet_g));
    aet_g.aunits = 1.0;
    aet_g.kred = AET_KRED_DEFAULT;
    e = getenv("N95_TRIM_KRED");
    if (e && *e && atof(e) > 0.0) aet_g.kred = atof(e);
    aet_g.aelink_sign = 1;
    e = getenv("N95_AELINK_SIGN");
    if (e && atoi(e) < 0) aet_g.aelink_sign = -1;
}

/* ------------------------------------------------------------------ */
/* the bulk data                                                       */

static void add_list_id(aet_list *L, int id)
{
    L->ids = (int *) msc_realloc(L->ids, sizeof(int) * (size_t) (L->n + 1));
    L->ids[L->n++] = id;
}

static int take_aelist(const msc_card *c)
{
    aet_list *L;
    int i, prev = 0;
    aet_g.list = (aet_list *) msc_realloc(aet_g.list, sizeof(aet_list) * (size_t) (aet_g.nlist + 1));
    L = &aet_g.list[aet_g.nlist++];
    memset(L, 0, sizeof(*L));
    L->sid = msc_fi(c, 1, 0);
    for (i = 2; i <= c->nfld; i++) {
        const char *f = msc_f(c, i);
        if (!*f) continue;
        if (msc_streq(f, "THRU")) {
            int to = msc_fi(c, i + 1, 0), k;
            for (k = prev + 1; k <= to; k++) add_list_id(L, k);
            prev = to;
            i++;
            continue;
        }
        prev = atoi(f);
        add_list_id(L, prev);
    }
    return 1;
}

static int take_aelink(const msc_card *c)
{
    aet_link *L;
    int i;
    const char *idf = msc_f(c, 1);
    aet_g.link = (aet_link *) msc_realloc(aet_g.link, sizeof(aet_link) * (size_t) (aet_g.nlink + 1));
    L = &aet_g.link[aet_g.nlink++];
    memset(L, 0, sizeof(*L));
    L->id = (msc_streq(idf, "ALWAYS") || !*idf) ? 0 : atoi(idf);
    lab_copy(L->dep, msc_f(c, 2));
    for (i = 3; i + 1 <= c->nfld; i += 2) {
        if (!*msc_f(c, i)) continue;
        L->ind = (char (*)[AET_LABLEN]) msc_realloc(L->ind, AET_LABLEN * (size_t) (L->n + 1));
        L->c = (double *) msc_realloc(L->c, sizeof(double) * (size_t) (L->n + 1));
        lab_copy(L->ind[L->n], msc_f(c, i));
        L->c[L->n] = msc_fd(c, i + 1, 0.0);
        L->n++;
    }
    return 1;
}

static aet_trimc *new_trim(const msc_card *c)
{
    aet_trimc *t;
    aet_g.trim = (aet_trimc *) msc_realloc(aet_g.trim, sizeof(aet_trimc) * (size_t) (aet_g.ntrim + 1));
    t = &aet_g.trim[aet_g.ntrim++];
    memset(t, 0, sizeof(*t));
    t->sid  = msc_fi(c, 1, 0);
    t->mach = msc_fd(c, 2, 0.0);
    t->q    = msc_fd(c, 3, 0.0);
    t->aeqr = msc_blank(c, 8) ? 1.0 : msc_fd(c, 8, 1.0);
    return t;
}

static void trim_add(aet_trimc *t, const char *lab, double v)
{
    t->lab = (char (*)[AET_LABLEN]) msc_realloc(t->lab, AET_LABLEN * (size_t) (t->n + 1));
    t->ux = (double *) msc_realloc(t->ux, sizeof(double) * (size_t) (t->n + 1));
    lab_copy(t->lab[t->n], lab);
    t->ux[t->n] = v;
    t->n++;
}

/* TRIM SID MACH Q LABEL1 UX1 LABEL2 UX2 AEQR / LABEL3 UX3 ...          */
static int take_trim(const msc_card *c)
{
    aet_trimc *t = new_trim(c);
    int i;
    for (i = 4; i + 1 <= c->nfld; i += 2) {
        if (i == 8) { i = 7; continue; }          /* field 8 is AEQR     */
        if (!*msc_f(c, i)) continue;
        trim_add(t, msc_f(c, i), msc_fd(c, i + 1, 0.0));
    }
    return 1;
}

int aet_bulk_card(const msc_card *c)
{
    const char *n = c->name;
    if (msc_streq(n, "AEROS")) {
        aet_g.have_aeros = 1;
        aet_g.acsid = msc_fi(c, 1, 0);
        aet_g.rcsid = msc_fi(c, 2, 0);
        aet_g.refc  = msc_fd(c, 3, 1.0);
        aet_g.refb  = msc_fd(c, 4, 1.0);
        aet_g.refs  = msc_fd(c, 5, 1.0);
        aet_g.symxz = msc_fi(c, 6, 0);
        aet_g.symxy = msc_fi(c, 7, 0);
        return 1;
    }
    if (msc_streq(n, "AESTAT")) {
        aet_stat *s;
        aet_g.stat = (aet_stat *) msc_realloc(aet_g.stat, sizeof(aet_stat) * (size_t) (aet_g.nstat + 1));
        s = &aet_g.stat[aet_g.nstat++];
        s->id = msc_fi(c, 1, 0);
        lab_copy(s->label, msc_f(c, 2));
        return 1;
    }
    if (msc_streq(n, "AESURF")) {
        aet_surf *s;
        aet_g.surf = (aet_surf *) msc_realloc(aet_g.surf, sizeof(aet_surf) * (size_t) (aet_g.nsurf + 1));
        s = &aet_g.surf[aet_g.nsurf++];
        memset(s, 0, sizeof(*s));
        s->id = msc_fi(c, 1, 0);
        lab_copy(s->label, msc_f(c, 2));
        s->cid[0]  = msc_fi(c, 3, 0);
        s->alid[0] = msc_fi(c, 4, 0);
        s->cid[1]  = msc_fi(c, 5, 0);
        s->alid[1] = msc_fi(c, 6, 0);
        s->eff     = msc_blank(c, 7) ? 1.0 : msc_fd(c, 7, 1.0);
        s->crefc   = msc_blank(c, 9) ? 1.0 : msc_fd(c, 9, 1.0);
        s->crefs   = msc_blank(c, 10) ? 1.0 : msc_fd(c, 10, 1.0);
        if (*msc_f(c, 8) && !msc_streq(msc_f(c, 8), "LDW")) {
            msc_msg_at(MSC_FATAL, 9605, c,
                "AESURF %s asks for %s: its forces would have to come from\n"
                "AEDW/AEPRESS/AEFORCE, which SOL 144 here does not read.",
                s->label, msc_f(c, 8));
        }
        return 1;
    }
    if (msc_streq(n, "AELIST")) return take_aelist(c);
    if (msc_streq(n, "AELINK")) return take_aelink(c);
    if (msc_streq(n, "TRIM"))   return take_trim(c);
    if (msc_streq(n, "TRIM2")) {
        msc_msg_at(MSC_FATAL, 9604, c,
            "TRIM2 is not supported here yet; write the trim as a TRIM card\n"
            "(every fixed variable listed, the rest free).");
        return 1;
    }
    if (msc_streq(n, "PARAM")) {
        char pn[MSC_FLDLEN];
        strncpy(pn, msc_f(c, 1), sizeof(pn) - 1);
        pn[sizeof(pn) - 1] = '\0';
        msc_upper(pn);
        if (msc_streq(pn, "AUNITS")) {
            aet_g.aunits = msc_fd(c, 2, 1.0);
            if (aet_g.aunits == 0.0) aet_g.aunits = 1.0;
            return 1;
        }
        return 0;
    }
    /* what NASTRAN-95's aero modules get from these is written below,
     * from the trim cards; a deck's own goes                          */
    if (msc_streq(n, "AERO") || msc_streq(n, "MKAERO1") || msc_streq(n, "MKAERO2")) {
        msc_msg_at(MSC_INFO, 9606, c,
            "SOL 144: %s is not used; the aerodynamics are computed at the\n"
            "Mach numbers of the TRIM cards and zero reduced frequency.", n);
        return 1;
    }
    if (msc_streq(n, "AESURFS")) {
        msc_msg_at(MSC_INFO, 9607, c,
            "SOL 144: AESURFS (control surface inertia for the hinge moments)\n"
            "is not used; no hinge moments are computed.");
        return 1;
    }
    if (msc_streq(n, "AEPARM") || msc_streq(n, "AEDW") || msc_streq(n, "AEFORCE") ||
        msc_streq(n, "AEPRESS") || msc_streq(n, "CSSCHD") || msc_streq(n, "DIVERG") ||
        msc_streq(n, "UXVEC") || msc_streq(n, "AECOMP") || msc_streq(n, "AECOMPL") ||
        msc_streq(n, "MONPNT1") || msc_streq(n, "MONPNT2") || msc_streq(n, "MONPNT3")) {
        if (msc_streq(n, "MONPNT1") || msc_streq(n, "MONPNT2") || msc_streq(n, "MONPNT3") ||
            msc_streq(n, "AECOMP") || msc_streq(n, "AECOMPL")) {
            msc_msg_at(MSC_INFO, 9608, c,
                "SOL 144: %s (monitor points) is not used; there is no monitor\n"
                "point output.", n);
            return 1;
        }
        msc_msg_at(MSC_FATAL, 9609, c,
            "SOL 144 here has no %s: the trim takes the AESTAT rigid body\n"
            "variables and the AESURF control surfaces (linear downwash).", n);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* the case control: the subcases in order and the TRIM set of each    */

void aet_case_scan(msc_deck *d)
{
    int  i, global_trim = -1, cur = -1;
    char up[MSC_LINELEN];

    free(aet_g.sub_id); free(aet_g.sub_trim);
    aet_g.sub_id = NULL; aet_g.sub_trim = NULL; aet_g.nsub = 0;
    for (i = 0; i < d->ncase; i++) {
        char *p, *q;
        strncpy(up, d->cases[i], sizeof(up) - 1);
        up[sizeof(up) - 1] = '\0';
        msc_upper(up);
        p = up;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '$') continue;
        if (strncmp(p, "SUBCASE", 7) == 0 && !isalnum((unsigned char) p[7])) {
            aet_g.sub_id = (int *) msc_realloc(aet_g.sub_id, sizeof(int) * (size_t) (aet_g.nsub + 1));
            aet_g.sub_trim = (int *) msc_realloc(aet_g.sub_trim, sizeof(int) * (size_t) (aet_g.nsub + 1));
            aet_g.sub_id[aet_g.nsub] = atoi(p + 7);
            aet_g.sub_trim[aet_g.nsub] = global_trim;
            cur = aet_g.nsub++;
            continue;
        }
        /* applied loads: SOL 144's intercept (PZ) would carry them, and
         * this one has none                                            */
        if ((strncmp(p, "LOAD", 4) == 0 && !isalnum((unsigned char) p[4])) ||
            (strncmp(p, "TEMP", 4) == 0 && !isalnum((unsigned char) p[4])) ||
            strncmp(p, "TEMPERATURE", 11) == 0 || strncmp(p, "DEFORM", 6) == 0) {
            msc_msg(MSC_FATAL, 9603,
                "SOL 144 here trims on the aerodynamic and inertia loads alone;\n"
                "an applied static load (case control %s) is not supported yet.\n"
                "LINE  %s", strtok(p, " =("), d->cases[i]);
            continue;
        }
        if (strncmp(p, "TRIM", 4) == 0 && !isalnum((unsigned char) p[4])) {
            q = strchr(p, '=');
            if (!q) continue;
            if (cur < 0) global_trim = atoi(q + 1);
            else aet_g.sub_trim[cur] = atoi(q + 1);
        }
    }
    if (aet_g.nsub == 0) {
        aet_g.sub_id = (int *) msc_alloc(sizeof(int));
        aet_g.sub_trim = (int *) msc_alloc(sizeof(int));
        aet_g.sub_id[0] = 1;
        aet_g.sub_trim[0] = global_trim;
        aet_g.nsub = 1;
    }
}

/* ------------------------------------------------------------------ */

static int known_label(const char *lab)
{
    int i;
    for (i = 0; i < aet_g.nstat; i++) if (strcmp(aet_g.stat[i].label, lab) == 0) return 1;
    for (i = 0; i < aet_g.nsurf; i++) if (strcmp(aet_g.surf[i].label, lab) == 0) return 1;
    return 0;
}

int aet_check(void)
{
    int i, j, k, bad = 0;

    if (!aet_g.have_aeros) {
        msc_msg(MSC_FATAL, 9610,
            "SOL 144 needs an AEROS card (the reference chord, span and area,\n"
            "the aerodynamic and reference coordinate systems).");
        return 1;
    }
    if (aet_g.acsid != 0) {
        msc_msg(MSC_FATAL, 9611,
            "AEROS ACSID %d: SOL 144 here takes the aerodynamic coordinate\n"
            "system as basic (ACSID 0, the flow along basic x).", aet_g.acsid);
        bad = 1;
    }
    if (aet_g.symxz != 0 || aet_g.symxy != 0) {
        msc_msg(MSC_FATAL, 9612,
            "AEROS SYMXZ %d SYMXY %d: SOL 144 here takes full models only\n"
            "(symmetric or antisymmetric half models are not supported yet).",
            aet_g.symxz, aet_g.symxy);
        bad = 1;
    }
    if (aet_g.nstat + aet_g.nsurf == 0) {
        msc_msg(MSC_FATAL, 9613, "SOL 144 needs trim variables: AESTAT and AESURF cards.");
        bad = 1;
    }
    for (i = 0; i < aet_g.ntrim; i++) {
        const aet_trimc *t = &aet_g.trim[i];
        if (t->mach >= 1.0) {
            msc_msg(MSC_FATAL, 9614,
                "TRIM %d is at Mach %g: supersonic aerodynamics (ZONA51) are not\n"
                "available; the doublet lattice is subsonic.", t->sid, t->mach);
            bad = 1;
        }
        if (t->q <= 0.0) {
            msc_msg(MSC_FATAL, 9615, "TRIM %d has no dynamic pressure.", t->sid);
            bad = 1;
        }
        for (k = 0; k < t->n; k++)
            if (!known_label(t->lab[k])) {
                msc_msg(MSC_FATAL, 9616,
                    "TRIM %d names %s, which no AESTAT or AESURF defines.",
                    t->sid, t->lab[k]);
                bad = 1;
            }
    }
    for (i = 0; i < aet_g.nlink; i++) {
        const aet_link *L = &aet_g.link[i];
        if (!known_label(L->dep)) {
            msc_msg(MSC_FATAL, 9617, "AELINK %d: %s is not a trim variable.", L->id, L->dep);
            bad = 1;
        }
        for (k = 0; k < L->n; k++)
            if (!known_label(L->ind[k])) {
                msc_msg(MSC_FATAL, 9617, "AELINK %d: %s is not a trim variable.", L->id, L->ind[k]);
                bad = 1;
            }
    }
    for (i = 0; i < aet_g.nsub; i++) {
        int found = 0;
        for (j = 0; j < aet_g.ntrim; j++) if (aet_g.trim[j].sid == aet_g.sub_trim[i]) found = 1;
        if (!found && aet_g.sub_trim[i] < 0) {
            msc_msg(MSC_FATAL, 9618,
                "subcase %d selects no TRIM set: every SOL 144 subcase needs\n"
                "TRIM = n naming a TRIM card of the bulk data.", aet_g.sub_id[i]);
            bad = 1;
        } else if (!found) {
            msc_msg(MSC_FATAL, 9618,
                "subcase %d selects TRIM = %d, and the bulk data has no TRIM %d.",
                aet_g.sub_id[i], aet_g.sub_trim[i], aet_g.sub_trim[i]);
            bad = 1;
        }
    }
    /* the distinct Machs of the TRIM sets the subcases use              */
    free(aet_g.mach);
    aet_g.mach = (double *) msc_alloc(sizeof(double) * (size_t) (aet_g.ntrim + 1));
    aet_g.nmach = 0;
    for (i = 0; i < aet_g.nsub; i++)
        for (j = 0; j < aet_g.ntrim; j++) {
            if (aet_g.trim[j].sid != aet_g.sub_trim[i]) continue;
            for (k = 0; k < aet_g.nmach; k++)
                if (fabs(aet_g.mach[k] - aet_g.trim[j].mach) <= 1e-9 * (1.0 + aet_g.trim[j].mach)) break;
            if (k == aet_g.nmach) aet_g.mach[aet_g.nmach++] = aet_g.trim[j].mach;
        }
    if (aet_g.aelink_sign < 0)
        msc_msg(MSC_WARN, 9619,
            "N95_AELINK_SIGN=-1: AELINK is read as u_D = sum C_i u_i, not as MSC's\n"
            "QRG writes it (u_D + sum C_i u_i = 0).");
    if (!bad)
        msc_msg(MSC_INFO, 9620,
            "SOL 144: %d subcase%s, %d trim variable%s (%d AESTAT, %d AESURF), %d\n"
            "AELINK%s, %d Mach number%s; the aerodynamics at k = %g (COSMIC's\n"
            "MKAERO1 takes no k = 0). Run as a DMAP program: the structure as\n"
            "rigid format DISP 2 (inertia relief), the aerodynamics as AERO 10,\n"
            "and the trim in module AETRIM.",
            aet_g.nsub, aet_g.nsub == 1 ? "" : "s",
            aet_g.nstat + aet_g.nsurf, aet_g.nstat + aet_g.nsurf == 1 ? "" : "s",
            aet_g.nstat, aet_g.nsurf, aet_g.nlink, aet_g.nlink == 1 ? "" : "s",
            aet_g.nmach, aet_g.nmach == 1 ? "" : "s", aet_g.kred);
    return bad;
}

/* ------------------------------------------------------------------ */
/* what the 1970s aero modules get instead                             */

void aet_emit(msc_list *out)
{
    msc_card *o;
    int i, k;

    /* AERO ACSID VELOCITY REFC RHOREF SYMXZ SYMXY                       */
    o = msc_list_add(out, "AERO");
    msc_seti(o, 1, aet_g.acsid);
    msc_setd(o, 3, aet_g.refc);
    msc_set (o, 4, "1.0");
    msc_seti(o, 5, aet_g.symxz);
    msc_seti(o, 6, aet_g.symxy);

    /* MKAERO1: up to eight Machs a card, the one k                     */
    for (i = 0; i < aet_g.nmach; i += 8) {
        o = msc_list_add(out, "MKAERO1");
        for (k = 0; k < 8 && i + k < aet_g.nmach; k++) msc_setd(o, 1 + k, aet_g.mach[i + k]);
        msc_setd(o, 9, aet_g.kred);
    }

    /* DPD makes the tables APD reads only from a dynamics pool; a deck
     * with no dynamics cards has none, so an eigenvalue card that no
     * METHOD selects                                                   */
    o = msc_list_add(out, "EIGR");
    msc_seti(o, 1, 9997);
    msc_set (o, 2, "FEER");
    msc_set (o, 3, "0.0");
    msc_seti(o, 6, 1);
}

/* ------------------------------------------------------------------ */
/* the DMAP program                                                    */

static const char *aet_dmap[] = {
"BEGIN    $ SOL 144 STATIC AEROELASTIC TRIM, NASTRAN95ASE (FORK SOL144.MD)",
"PARAM    //*MPY*/CARDNO/0/0 $",
"GP1      GEOM1,GEOM2,/GPL,EQEXIN,GPDT,CSTM,BGPDT,SIL/S,N,LUSET/",
"         S,N,NOGPDT/ALWAYS=-1 $",
"GP2      GEOM2,EQEXIN/ECT $",
"GP3      GEOM3,EQEXIN,GEOM2/SLT,GPTT/NOGRAV $",
"TA1      ECT,EPT,BGPDT,SIL,GPTT,CSTM,MPT,EQEXIN/EST,GEI,GPECT,,,MPTX,",
"         PCOMPS,EPTX/LUSET/S,N,NOSIMP/1/S,N,NOGENL/GENEL/S,N,COMPS $",
"EQUIV    MPTX,MPT/COMPS/EPTX,EPT/COMPS $",
"PARAM    //*ADD*/NOKGGX/1/0 $",
"PARAM    //*ADD*/NOMGG/1/0 $",
"EMG      EST,CSTM,MPT,DIT,GEOM2,/KELM,KDICT,MELM,MDICT,,,/S,N,NOKGGX/",
"         S,N,NOMGG////C,Y,COUPMASS/C,Y,CPBAR/",
"         C,Y,CPROD/C,Y,CPQUAD1/C,Y,CPQUAD2/C,Y,CPTRIA1/C,Y,CPTRIA2/",
"         C,Y,CPTUBE/C,Y,CPQDPLT/C,Y,CPTRPLT/C,Y,CPTRBSC/",
"         C,Y,VOLUME/C,Y,SURFACE $",
"EMA      GPECT,KDICT,KELM/KGGX $",
"EMA      GPECT,MDICT,MELM/MGG/-1/C,Y,WTMASS=1.0 $",
"COND     LGPWG,GRDPNT $",
"GPWG     BGPDT,CSTM,EQEXIN,MGG/OGPWG/V,Y,GRDPNT=-1/C,Y,WTMASS $",
"OFP      OGPWG,,,,,//S,N,CARDNO $",
"LABEL    LGPWG $",
"EQUIV    KGGX,KGG/NOGENL $",
"COND     LBL11A,NOGENL $",
"SMA3     GEI,KGGX/KGG/LUSET/NOGENL/NOSIMP $",
"LABEL    LBL11A $",
"GPSTGEN  KGG,SIL/GPST $",
"PARAM    //*MPY*/NSKIP/0/0 $",
"GP4      CASECC,GEOM4,EQEXIN,GPDT,BGPDT,CSTM,GPST/RG,YS,USET,",
"         ASET,OGPST/LUSET/S,N,MPCF1/S,N,MPCF2/S,N,SINGLE/S,N,OMIT/",
"         S,N,REACT/S,N,NSKIP/S,N,REPEAT/S,N,NOSET/S,N,NOL/S,N,NOA/",
"         C,Y,ASETOUT/C,Y,AUTOSPC $",
"OFP      OGPST,,,,,//S,N,CARDNO $",
"PURGE    GM/MPCF1/GO,KOO,LOO,MOO,MOA,PO,UOOV,RUOV/OMIT/KSS,KFS,PS/",
"         SINGLE $",
"EQUIV    KGG,KNN/MPCF1/MGG,MNN/MPCF1 $",
"COND     LBL2,MPCF2 $",
"MCE1     USET,RG/GM $",
"MCE2     USET,GM,KGG,MGG,,/KNN,MNN,, $",
"LABEL    LBL2 $",
"EQUIV    KNN,KFF/SINGLE/MNN,MFF/SINGLE $",
"COND     LBL3,SINGLE $",
"SCE1     USET,KNN,MNN,,/KFF,KFS,KSS,MFF,, $",
"LABEL    LBL3 $",
"EQUIV    KFF,KAA/OMIT/MFF,MAA/OMIT $",
"COND     LBL5,OMIT $",
"SMP1     USET,KFF,MFF,,/GO,KAA,KOO,LOO,MAA,MOO,MOA,, $",
"LABEL    LBL5 $",
"RBMG1    USET,KAA,MAA/KLL,KLR,KRR,MLL,MLR,MRR $",
"RBMG2    KLL/LLL $",
"RBMG3    LLL,KLR,KRR/DM $",
"RBMG4    DM,MLL,MLR,MRR/MR $",
"DPD      DYNAMICS,GPL,SIL,USET/GPLD,SILD,USETD,TFPOOL,,,,,,EED,EQDYN/",
"         LUSET/S,N,LUSETD/NOTFL/NODLT/NOPSDL/NOFRL/",
"         NONLFT/NOTRL/S,N,NOEED/123/S,N,NOUE $",
"APD      EDT,EQDYN,ECT,BGPDT,SILD,USETD,CSTM,GPLD/EQAERO,ECTA,BGPA,SILA,",
"         USETA,SPLINE,AERO,ACPT,FLIST,CSTMA,GPLA,SILGA/S,N,NK/S,N,NJ/",
"         S,N,LUSETA/S,N,BOV $",
"GI       SPLINE,USET,CSTMA,BGPA,SIL,,GM,GO/GTKA/NK/LUSET $",
"GI       SPLINE,,CSTMA,BGPA,SIL,,,/GTKG/NK/LUSET $",
"PARAM    //*ADD*/DESTRY/0/1/ $",
"AMG      AERO,ACPT/AJJL,SKJ,D1JK,D2JK/NK/NJ/S,N,DESTRY $",
"AETRIM   CASECC,USET,GTKA,,,,,,,,,,,,BGPDT,SIL,CSTMA,GPLA,USETA/ES,DG,,/",
"         1 $",
"FBS      LLL,,ES/CLS/1/1/2 $",
"MPYAD    MLL,DM,MLR/MDL/0/1/1/2 $",
"FBS      LLL,,MDL/XM/1/1/2 $",
"MPYAD    MDL,XM,/MXM/1/1/1/2 $",
"MPYAD    MGG,DG,/MDG/0/1/1/2 $",
"AETRIM   CASECC,USET,GTKA,GTKG,AJJL,SKJ,D1JK,ACPT,CLS,XM,MXM,DM,MR,MDG,",
"         BGPDT,SIL,CSTMA,GPLA,USETA/PLA,UDDT,PGT,/2 $",
"MPYAD    MDL,UDDT,PLA/PLT/0/-1/1/2 $",
"FBS      LLL,,PLT/ULV/1/1/2 $",
"SDR1     USET,PGT,ULV,,YS,GO,GM,,KFS,KSS,/UGV,PGG,QG/NSKIP/*STATICS* $",
"SDR2     CASECC,CSTM,MPT,DIT,EQEXIN,SIL,GPTT,EDT,BGPDT,,QG,UGV,EST,,PGG,",
"         PCOMPS/OPG1,OQG1,OUGV1,OES1,OEF1,PUGV1,OES1L,OEF1L/*STATICS*////",
"         COMPS $",
"OFP      OUGV1,OPG1,OQG1,OEF1,OES1,//S,N,CARDNO $",
"OFP      OEF1L,OES1L,,,,//S,N,CARDNO $",
"END      $",
NULL
};

void aet_write_dmap(FILE *fp)
{
    int i;
    fprintf(fp, "APP     DMAP\n");
    fprintf(fp, "TIME    600\n");
    for (i = 0; aet_dmap[i]; i++) fprintf(fp, "%s\n", aet_dmap[i]);
}
