/* HALO: the aerodynamic corrections of the dynamic solutions - the force
 * weights WKK (or WTFACT) and the extra points' downwash D1JE / D2JE in
 * SOL 145 (rigid format AERO 10) and SOL 146 (AERO 11).
 *
 * What MSC Nastran and Simcenter do with them. Their PFAERO subDMAP forms
 * WSKJF = WTFACT SKJ, WTFACT the deck's WTFACT or, when it has none, its
 * WKK (Simcenter 2606 pfaero.dat:59-97 finds the matrix, 465-469 forms the
 * product; the Aeroelastic guide's PFAERO, step 26, [SKJ1] = [WTFACT]
 * [SKJF], and eq. 1-113: WTFACT "the same weighting matrix as Wkk"), and
 * AMP builds the modal, k-set and gust aerodynamics (QHH, QKH, QHJ) on
 * it, so every box force and moment of the theory is weighted in flutter
 * and in the gust response as it is in the trim. The downwash of extra
 * points, w_j = (D1JE + i k D2JE) u_e (two DMIs, j rows and a column per
 * extra point; the guide's eqs. 1-116 to 1-119 and PFAERO step 32), gives
 * QHH its extra-point columns through the same weighted forces, the
 * classic NASTRAN way of putting a control surface into the aerodynamics
 * (the e rows stay null: the aerodynamics put no force on an extra point).
 *
 * NASTRAN-95 has the second and not the first: AMP takes D1JE and D2JE
 * (mis/ampb.f merges them behind the modes' downwash), but AERO 10 and 11
 * read them off a user tape (INPUTT2 at statement 87, when PARAM NODJE is
 * set), and nothing weights SKJ. So this front end takes the cards out of
 * a SOL 145 / 146 deck - DMI or DMIK named WKK or WTFACT, DMI or DMIJ
 * named D1JE or D2JE - and writes them back as DMIs in NASTRAN-95's own
 * set order, which the alter (mscop4.c) then uses:
 *
 *   WKK        SKJ weighted after AMG (statement 85): MPYAD WKK,SKJ, the
 *              product EQUIVed onto SKJ, so AMP's QHHL, QKHL and QHJL are
 *              all built on it, as MSC's WSKJF;
 *   D1JE/D2JE  statements 86-88 (COND NODJE, the INPUTT2, LABEL NODJE)
 *              removed, so AMP reads the DMIs.
 *
 * The orders. MSC's k-set DMI rows are the doublet-lattice boxes in
 * ascending box id, two per box (T3, the force, then R5, the moment), its
 * j-set rows the boxes in the same order (one each); DMIK and DMIJ name a
 * box and a component instead. NASTRAN-95's APD makes the boxes panel by
 * panel, the CAERO1 cards in the order of a sort on IGID (mis/apd12.f;
 * the bulk data comes sorted by EID), each panel's boxes chordwise within
 * spanwise strips with ids EID + strip * NCHORD + chord (mis/apd1.f), and
 * gives them k-set rows T3 then R5 in that order. The two orders are the
 * same when IGID never falls as EID rises, which Simcenter's Quick
 * Reference Guide asks of every deck anyway (CAERO1 remark 8: panels "in
 * an IGID increasing order ... independent of whether EID1 < or > EID2"),
 * and when no two panels' box ids overlap (duplicate ids, a fatal in
 * either code). A deck that breaks either is refused here (UFM 9232),
 * not mapped through a guess at the sort's order of ties.
 *
 * Slender bodies (CAERO2 / PAERO2). NASTRAN-95 makes a body with the
 * panels of its interference group (the CAERO1s of the same IGID: the
 * doublet lattice with bodies, DLB), after the last panel of that group,
 * or after all the panels when no CAERO1 shares its IGID (mis/apd12.f);
 * a body has NSB + NINT j-set elements and 2 NSB k-set dofs per
 * orientation (Z, Y, or both for ZY: mis/apd2.f). So when every body
 * shares its IGID with the last group of panels or with none - checked
 * here, UFM 9232 otherwise - the j and k sets are the CAERO1 boxes as
 * above, then the bodies, in both codes. The bodies' own order inside
 * their block is the two codes' business (NASTRAN-95 puts the
 * interference elements first, then the slender ones, by orientation),
 * so the bodies' block is taken only where its order does not matter:
 * WKK on it a multiple of the identity (1 keeps the bodies' forces, as
 * the repo's corrected decks give it; 0 drops them) and no term joining
 * a body to a box; D1JE / D2JE with null body rows (a control surface
 * moves boxes); WGSPAN with every body row 1 (the uniform gust: GUST2
 * weights a Z body's slender elements 1 and the rest 0 anyway), or no
 * spanwise shape at all.
 *
 * Any other CAERO (the strip theory's, Mach box, piston theory) has a k
 * set of another shape, and is refused when these cards are used.
 *
 * The extra points are the e set in ascending EPOINT id, the order
 * NASTRAN sequences them in (no SEQEP is read here).
 *
 * On a restart the model is the modes run's bulk data off its problem
 * tape plus the restart deck's own cards, so the boxes and the extra
 * points are counted over both decks (a panel of the modes deck that the
 * restart deck does not repeat is in the solver's k set all the same).
 *
 * Without any of these cards nothing here writes anything, and the deck
 * goes to the solver as it did before.
 *
 * The spanwise gust (SOL 146). A GUST card of this dialect may carry a
 * seventh field, SHAPE: 0 the uniform gust, m > 0 the m-th column of a
 * DMI named WGSPAN (the j set - the boxes in ascending id, as above - by
 * the shapes, real), a weight on each box's gust downwash:
 *
 *   w_j(omega) = WG WGSPAN(j, m) cos(gamma_j) exp(-i omega (x_j - X0) / V)
 *
 * with the card's own WG, X0 and V (VehicleDesign
 * ase/gust/spanwise_gust_scheme.md, part 2). Several GUST cards may share
 * an id: the subcase that selects it (GUST = id) is loaded by all of them
 * together, each with its shape, WG, X0 and V - a gust in spanwise
 * segments that arrive at different times is one such set, a card per
 * segment - and they share the one DLOAD of their subcase (a set whose
 * cards name different DLOADs is refused). Different ids are different
 * subcases, which RANDOM adds by their RANDPS cards (NASA's RAND2 stops
 * on a cross-spectrum between two, SFM 3002, in AERO 11). MSC's GUST
 * has neither the field nor the sets, and NASTRAN-95's IFP reads five
 * fields, so once any GUST card of a SOL 146 deck has a seventh field
 * (0 included) the GUST cards are taken out here and written back as one
 * GUST card per id (the first card's fields: its DLOAD is the subcase's),
 * with the matrix the GUST module reads: DMI WGSPAN, the j set plus four
 * rows by a column per GUST card of the deck (ascending id, the deck's
 * order within an id) - the card's shape (ones for SHAPE 0), then its WG,
 * X0, V and id, the last four written as the card spells them. The alter
 * (mscop4.c, ALTER 94) hands it to GUST as its sixth input, which the
 * rigid format leaves empty: GUST2 builds a downwash column per card at
 * every frequency, with the card's X0 and V, and GUST3 loads each subcase
 * with the sum over the cards of its id, each times its WG. Without a
 * seventh field on any GUST card nothing here writes anything either.
 */
#include "msc.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { DYN_WKK = 0, DYN_WTF, DYN_D1JE, DYN_D2JE, DYN_WGS, DYN_N };
static const char *dyn_name[DYN_N] = { "WKK", "WTFACT", "D1JE", "D2JE", "WGSPAN" };

typedef struct { int r, rc, c, cc; double v; } dyn_el;

typedef struct {
    int     kind;        /* 0 absent, 1 DMI (set positions), 2 DMIK/DMIJ (ids) */
    int     form;        /* DMI FORM, or the DMIK/DMIJ IFO                       */
    int     m, n;        /* DMI rows and columns (n: NCOL of a DMIJ IFO 9)        */
    dyn_el *e;
    int     ne, cap;
    int     line;        /* the header card's, for messages                      */
    char    file[MSC_PATHLEN];
} dyn_mat;

/* a GUST card of the deck: its id, SHAPE (field 7, 0 when blank), DLOAD,
 * the deck's order, and the text of its five fields (SID DLOAD WG X0 V) */
typedef struct {
    int    sid, shape, dload, at, line;
    char   f[5][MSC_FLDLEN];
} dyn_gust;

static struct {
    int     active;              /* rigid format 10 or 11                     */
    int     rf;
    dyn_mat dmi[DYN_N];          /* the DMI spelling                          */
    dyn_mat ids[DYN_N];          /* DMIK (WKK, WTFACT) / DMIJ (D1JE, D2JE)    */
    int     static_left;         /* DMIJ W2GJ / FA2J left out                 */
    int     span;                /* SOL 146 with a seventh GUST field         */
    dyn_gust *gust;              /* the GUST cards, as the deck gives them    */
    int     ngust, gcap;
} G;

static void mat_free(dyn_mat *M)
{
    free(M->e);
    memset(M, 0, sizeof(*M));
}

void msc_dyn_reset(int rf)
{
    int k;
    for (k = 0; k < DYN_N; k++) { mat_free(&G.dmi[k]); mat_free(&G.ids[k]); }
    G.active = (rf == 10 || rf == 11);
    G.rf = rf;
    G.static_left = 0;
    G.span = 0;
    free(G.gust);
    G.gust = NULL;
    G.ngust = G.gcap = 0;
}

/* before the bulk data pass: a SOL 146 deck any of whose GUST cards has a
 * seventh field (SHAPE, 0 included) is a spanwise gust deck, whose GUST
 * cards this file writes (msc_dyn_gust_emit)                           */
void msc_dyn_scan(const msc_deck *d)
{
    int i, k;
    G.span = 0;
    if (G.rf != 11) return;
    for (i = 0; i < d->nbulk && !G.span; i++) {
        const msc_card *c = &d->bulk[i];
        if (c->dropped || !msc_streq(c->name, "GUST")) continue;
        for (k = 6; k <= c->nfld; k++) if (!msc_blank(c, k)) G.span = 1;
    }
}

static int name_index(const char *name)
{
    int k;
    for (k = 0; k < DYN_N; k++) if (msc_streq(name, dyn_name[k])) return k;
    return -1;
}

static void add_el(dyn_mat *M, int r, int rc, int c, int cc, double v)
{
    dyn_el *e;
    if (M->ne == M->cap) {
        M->cap = M->cap ? 2 * M->cap : 256;
        M->e = (dyn_el *) msc_realloc(M->e, sizeof(dyn_el) * (size_t) M->cap);
    }
    e = &M->e[M->ne++];
    e->r = r; e->rc = rc; e->c = c; e->cc = cc; e->v = v;
}

/* a field that is an integer (a row number or an id), not a real        */
static int fld_is_int(const char *f)
{
    const char *p = f;
    if (*p == '+' || *p == '-') p++;
    if (!*p) return 0;
    for (; *p; p++) if (!isdigit((unsigned char) *p)) return 0;
    return 1;
}

static void note_header(dyn_mat *M, const msc_card *c)
{
    M->line = c->line;
    strncpy(M->file, c->file ? c->file : "", sizeof(M->file) - 1);
    M->file[sizeof(M->file) - 1] = '\0';
}

/* DMI NAME 0 FORM TIN TOUT - M N (the header), then DMI NAME J I1
 * A(I1,J) A(I1+1,J) ... I2 A(I2,J) ... (a column: an integer restarts the
 * row, THRU n repeats the last value down to row n; QRG DMI)            */
static void take_dmi(const msc_card *c, dyn_mat *M, const char *name)
{
    int    j, i, row = 0, have = 0;
    double last = 0.0;
    if (msc_fi(c, 2, -1) == 0 && fld_is_int(msc_f(c, 2))) {
        int tin = msc_fi(c, 4, 1);
        if (M->kind == 1)
            msc_msg_at(MSC_WARN, 9233, c, "a second DMI %s header; the matrix starts over.", name);
        free(M->e); M->e = NULL; M->ne = M->cap = 0;
        M->kind = 1;
        M->form = msc_fi(c, 3, 2);
        M->m = msc_fi(c, 7, 0);
        M->n = msc_fi(c, 8, 1);
        note_header(M, c);
        if (tin == 3 || tin == 4)
            msc_msg_at(MSC_FATAL, 9231, c,
                "DMI %s is complex (TIN %d). %s is real: %s.\n"
                "FIX   Write it with TIN 1 or 2.", name, tin, name,
                (name[0] == 'D') ? "D1JE is the real and D2JE the imaginary part's\n"
                                   "coefficient (w = (D1JE + i k D2JE) u_e)"
                : msc_streq(name, "WGSPAN") ? "a weight on each box's gust downwash"
                                 : "a weight on each box force and moment");
        if (M->form == 3 || M->form == 8) M->n = 1;
        return;
    }
    if (M->kind != 1) {
        msc_msg_at(MSC_FATAL, 9231, c, "DMI %s column before its header (the card with 0 in\n"
                   "field 3).", name);
        return;
    }
    j = msc_fi(c, 2, 0);
    for (i = 3; i <= c->nfld; i++) {
        const char *f = msc_f(c, i);
        if (!*f) continue;
        if (msc_streq(f, "THRU")) {
            int to = msc_fi(c, i + 1, 0), r;
            if (!have || to < row) {
                msc_msg_at(MSC_FATAL, 9231, c, "DMI %s column %d: THRU %s follows no value it could\n"
                           "repeat.", name, j, msc_f(c, i + 1));
                return;
            }
            for (r = row + 1; r <= to; r++) add_el(M, r, 0, j, 0, last);
            row = to;
            i++;
            continue;
        }
        if (fld_is_int(f)) { row = atoi(f) - 1; have = 0; continue; }
        row++;
        last = msc_fd(c, i, 0.0);
        have = 1;
        add_el(M, row, 0, j, 0, last);
    }
}

/* DMIK / DMIJ NAME 0 IFO TIN TOUT POLAR - NCOL (the header), then NAME GJ
 * CJ - G1 C1 A1 B1 G2 C2 A2 B2 ... (a column at a box and component, its
 * rows boxes and components; QRG DMIJ, DMIK)                            */
static void take_dmij(const msc_card *c, dyn_mat *M, const char *name)
{
    int gj, cj, i;
    if (msc_fi(c, 2, -1) == 0 && fld_is_int(msc_f(c, 2))) {
        int tin = msc_fi(c, 4, 1);
        if (M->kind == 2)
            msc_msg_at(MSC_WARN, 9233, c, "a second %s %s header; the matrix starts over.", c->name, name);
        free(M->e); M->e = NULL; M->ne = M->cap = 0;
        M->kind = 2;
        M->form = msc_fi(c, 3, 2);
        M->m = 0;
        M->n = msc_fi(c, 8, 0);
        note_header(M, c);
        if (tin == 3 || tin == 4)
            msc_msg_at(MSC_FATAL, 9231, c, "%s %s is complex (TIN %d); it is a real matrix.\n"
                       "FIX   Write it with TIN 1 or 2.", c->name, name, tin);
        return;
    }
    if (M->kind != 2) {
        msc_msg_at(MSC_FATAL, 9231, c, "%s %s column before its header (the card with 0 in\n"
                   "field 3).", c->name, name);
        return;
    }
    gj = msc_fi(c, 2, 0);
    cj = msc_fi(c, 3, 0);
    for (i = 5; i + 2 <= c->nfld; i += 4) {
        int    gi, ci;
        double v;
        if (msc_blank(c, i)) continue;
        gi = msc_fi(c, i, 0);
        ci = msc_fi(c, i + 1, 0);
        v  = msc_fd(c, i + 2, 0.0);
        add_el(M, gi, ci, gj, cj, v);
        if (M->form == 6 && !(gi == gj && ci == cj)) add_el(M, gj, cj, gi, ci, v);
    }
}

/* one bulk card of a SOL 145 / 146 deck: 1 when it is taken here (and not
 * written as it came), 0 when it goes on to the rest of the translation  */
int msc_dyn_card(const msc_card *c)
{
    char nm[MSC_FLDLEN];
    int  k, dmi, dmik, dmij;
    if (!G.active) return 0;
    dmi  = msc_streq(c->name, "DMI");
    dmik = msc_streq(c->name, "DMIK");
    dmij = msc_streq(c->name, "DMIJ");
    if (msc_streq(c->name, "DMIJI")) {
        msc_msg_at(MSC_WARN, 9234, c, "DMIJI %s (the interference elements of CAERO2 bodies) is\n"
                   "left out: SOL 145 / 146 here read no matrix on the\n"
                   "interference elements.", msc_f(c, 1));
        return 1;
    }
    if (!dmi && !dmik && !dmij) return 0;
    strncpy(nm, msc_f(c, 1), sizeof(nm) - 1);
    nm[sizeof(nm) - 1] = '\0';
    msc_upper(nm);
    k = name_index(nm);
    if (dmi) {
        if (k < 0) return 0;                        /* any other DMI goes on */
        take_dmi(c, &G.dmi[k], nm);
        return 1;
    }
    if (dmik && (k == DYN_WKK || k == DYN_WTF)) { take_dmij(c, &G.ids[k], nm); return 1; }
    if (dmij && (k == DYN_D1JE || k == DYN_D2JE)) { take_dmij(c, &G.ids[k], nm); return 1; }
    if (dmij && (msc_streq(nm, "W2GJ") || msc_streq(nm, "FA2J"))) {
        /* the steady corrections: MSC's dynamic solutions have no use for
         * them (no intercept in a flutter or gust problem); said once   */
        if (msc_fi(c, 2, -1) == 0 && fld_is_int(msc_f(c, 2)) && !(G.static_left & (nm[0] == 'W' ? 1 : 2))) {
            G.static_left |= (nm[0] == 'W' ? 1 : 2);
            msc_msg_at(MSC_INFO, 9235, c,
                "DMIJ %s is a static aeroelastic correction (SOL 144's intercept); the\n"
                "flutter and dynamic response solutions do not use it, here as in\n"
                "MSC, and it is left out of this SOL %d deck.", nm, G.rf == 10 ? 145 : 146);
        }
        return 1;
    }
    if (msc_fi(c, 2, -1) == 0 && fld_is_int(msc_f(c, 2)))
        msc_msg_at(MSC_WARN, 9234, c,
            "%s %s is left out: SOL 145 / 146 here read %s only for %s.", c->name, nm, c->name,
            dmik ? "WKK and WTFACT (the force weights)" : "D1JE and D2JE (the extra points' downwash)");
    return 1;
}

/* ------------------------------------------------------------------ */
/* the boxes and the extra points of the deck                          */

typedef struct { int eid, ns, nc, igid; } dyn_panel;

/* a CAERO2 body: its slender (NSB) and interference (NINT) elements, its
 * orientations (1: Z or Y, 2: ZY) and its interference group          */
typedef struct { int eid, nsb, nint, norient, igid; } dyn_body;

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *) a, y = *(const int *) b;
    return (x > y) - (x < y);
}

static int cmp_panel(const void *a, const void *b)
{
    const dyn_panel *p = (const dyn_panel *) a, *q = (const dyn_panel *) b;
    return (p->eid > q->eid) - (p->eid < q->eid);
}

/* the number of values on AEFACT sid (the divisions of a CAERO1 that
 * names LSPAN / LCHORD), -1 when neither deck has such a card            */
static int aefact_count(const msc_deck *d, const msc_deck *also, int sid)
{
    int i, k, n, w;
    for (w = 0; w < 2; w++) {
        const msc_deck *e = w ? also : d;
        if (!e) continue;
        for (i = 0; i < e->nbulk; i++) {
            const msc_card *c = &e->bulk[i];
            if (c->dropped || !msc_streq(c->name, "AEFACT") || msc_fi(c, 1, 0) != sid) continue;
            n = 0;
            for (k = 2; k <= c->nfld; k++) if (!msc_blank(c, k)) n++;
            return n;
        }
    }
    return -1;
}

/* the number of orientations of PAERO2 pid (Z or Y: 1, ZY: 2), 0 when
 * neither deck has it                                                    */
static int paero2_orients(const msc_deck *d, const msc_deck *also, int pid)
{
    int i, w;
    for (w = 0; w < 2; w++) {
        const msc_deck *e = w ? also : d;
        if (!e) continue;
        for (i = 0; i < e->nbulk; i++) {
            const msc_card *c = &e->bulk[i];
            char o[MSC_FLDLEN];
            if (c->dropped || !msc_streq(c->name, "PAERO2") || msc_fi(c, 1, 0) != pid) continue;
            strncpy(o, msc_f(c, 2), sizeof(o) - 1);
            o[sizeof(o) - 1] = '\0';
            msc_upper(o);
            return msc_streq(o, "ZY") || msc_streq(o, "YZ") ? 2 : 1;
        }
    }
    return 0;
}

/* the CAERO2 bodies of the model (both decks of a restart): their count
 * into *nbody, the array, or NULL after a fatal (with *nbody -1)        */
static dyn_body *deck_bodies(const msc_deck *d, const msc_deck *also, int *nbody)
{
    dyn_body *y = NULL;
    int n = 0, cap = 0, i, w;
    *nbody = 0;
    for (w = 0; w < 2; w++) {
        const msc_deck *e = w ? also : d;
        if (!e) continue;
        for (i = 0; i < e->nbulk; i++) {
            const msc_card *c = &e->bulk[i];
            int eid, q, dup = 0;
            if (c->dropped || !msc_streq(c->name, "CAERO2")) continue;
            eid = msc_fi(c, 1, 0);
            for (q = 0; q < n; q++) if (y[q].eid == eid) { dup = 1; break; }
            if (dup) continue;
            if (n == cap) {
                cap = cap ? 2 * cap : 16;
                y = (dyn_body *) msc_realloc(y, sizeof(dyn_body) * (size_t) cap);
            }
            y[n].eid  = eid;
            y[n].nsb  = msc_fi(c, 4, 0);
            y[n].nint = msc_fi(c, 5, 0);
            y[n].igid = msc_fi(c, 8, 0);
            /* AEFACT LSB / LINT: the divisions, one more than the elements */
            if (y[n].nsb <= 0) {
                int m = aefact_count(d, also, msc_fi(c, 6, 0));
                y[n].nsb = m > 1 ? m - 1 : 0;
            }
            if (y[n].nint <= 0 && msc_fi(c, 7, 0) > 0) {
                int m = aefact_count(d, also, msc_fi(c, 7, 0));
                y[n].nint = m > 1 ? m - 1 : 0;
            }
            y[n].norient = paero2_orients(d, also, msc_fi(c, 2, 0));
            if (y[n].nsb <= 0 || y[n].norient == 0) {
                msc_msg_at(MSC_FATAL, 9232, c,
                    "CAERO2 %d: its elements cannot be counted (NSB %s, no\n"
                    "AEFACT %s with the divisions, or no PAERO2 %s).",
                    eid, msc_f(c, 4), msc_f(c, 6), msc_f(c, 2));
                free(y);
                *nbody = -1;
                return NULL;
            }
            n++;
        }
    }
    *nbody = n;
    return y;
}

/* every CAERO1 box id of the model, ascending (MSC's order, and - checked
 * here - NASTRAN-95's); the count, or -1 after a fatal. `also` is the
 * modes deck of a restart (its panels are the solver's too), or NULL.
 * *bk and *bj (when not NULL): the k-set dofs and j-set elements of the
 * CAERO2 bodies, which follow the boxes in both sets (checked here)     */
static int deck_boxes(const msc_deck *d, const msc_deck *also, int **ids_out, int *bk, int *bj)
{
    dyn_panel *p = NULL;
    dyn_body  *y;
    int np = 0, cap = 0, nb = 0, i, j, w, other = 0, *ids, ny, nk_b = 0, nj_b = 0;
    char other_name[MSC_FLDLEN] = "";
    *ids_out = NULL;
    if (bk) *bk = 0;
    if (bj) *bj = 0;
    y = deck_bodies(d, also, &ny);
    if (ny < 0) return -1;
    for (w = 0; w < 2; w++) {
        const msc_deck *e = w ? also : d;
        if (!e) continue;
        for (i = 0; i < e->nbulk; i++) {
            const msc_card *c = &e->bulk[i];
            int eid, q, dup = 0;
            if (c->dropped) continue;
            if (!msc_streq(c->name, "CAERO1")) {
                if (!strncmp(c->name, "CAERO", 5) && !msc_streq(c->name, "CAERO2")) {
                    other = 1;
                    strncpy(other_name, c->name, sizeof(other_name) - 1);
                }
                continue;
            }
            /* a panel both decks have is one panel                      */
            eid = msc_fi(c, 1, 0);
            for (q = 0; q < np; q++) if (p[q].eid == eid) { dup = 1; break; }
            if (dup) continue;
            if (np == cap) {
                cap = cap ? 2 * cap : 64;
                p = (dyn_panel *) msc_realloc(p, sizeof(dyn_panel) * (size_t) cap);
            }
            p[np].eid  = eid;
            p[np].ns   = msc_fi(c, 4, 0);
            p[np].nc   = msc_fi(c, 5, 0);
            p[np].igid = msc_fi(c, 8, 0);
            if (p[np].ns <= 0) {
                int n = aefact_count(d, also, msc_fi(c, 6, 0));
                p[np].ns = n > 1 ? n - 1 : 0;
            }
            if (p[np].nc <= 0) {
                int n = aefact_count(d, also, msc_fi(c, 7, 0));
                p[np].nc = n > 1 ? n - 1 : 0;
            }
            if (p[np].ns <= 0 || p[np].nc <= 0) {
                msc_msg_at(MSC_FATAL, 9232, c,
                    "CAERO1 %d: its boxes cannot be counted (NSPAN %s,\n"
                    "NCHORD %s; no AEFACT %s / %s with the divisions).",
                    eid, msc_f(c, 4), msc_f(c, 5), msc_f(c, 6), msc_f(c, 7));
                free(p);
                free(y);
                return -1;
            }
            nb += p[np].ns * p[np].nc;
            np++;
        }
    }
    if (other) {
        msc_msg(MSC_FATAL, 9232,
            "The model has %s panels beside its CAERO1s. The force\n"
            "weights and the extra points' downwash are put on the\n"
            "doublet-lattice boxes of CAERO1 panels (and CAERO2 bodies)\n"
            "only; another theory's k set has another shape.", other_name);
        free(p);
        free(y);
        return -1;
    }
    if (np == 0) {
        free(p);
        free(y);
        return 0;
    }
    qsort(p, (size_t) np, sizeof(dyn_panel), cmp_panel);
    for (i = 1; i < np; i++) {
        if (p[i].igid < p[i - 1].igid) {
            msc_msg(MSC_FATAL, 9232,
                "CAERO1 %d has IGID %d and CAERO1 %d, the panel before it\n"
                "in id order, IGID %d. NASTRAN-95 makes the boxes in the\n"
                "order of a sort on IGID and MSC numbers its set rows by\n"
                "box id; the two agree only when IGID never falls as the\n"
                "panel id rises, which Simcenter's QRG asks of a deck\n"
                "anyway (CAERO1, remark 8). The weights and the downwash\n"
                "cannot be put in the solver's order without guessing.\n"
                "FIX   Number the panels so that IGID rises with the id.",
                p[i].eid, p[i].igid, p[i - 1].eid, p[i - 1].igid);
            free(p);
            free(y);
            return -1;
        }
    }
    /* the bodies follow every box when none shares its IGID with a group
     * of panels before the last one (NASTRAN-95 makes a body with the
     * last panel of its own group, mis/apd12.f)                        */
    for (i = 0; i < ny; i++) {
        for (j = 0; j < np; j++) {
            if (p[j].igid == y[i].igid && y[i].igid != p[np - 1].igid) {
                msc_msg(MSC_FATAL, 9232,
                    "CAERO2 %d has IGID %d, the interference group of CAERO1\n"
                    "%d, and the panels of a later group (IGID %d) follow it.\n"
                    "NASTRAN-95 makes a body after the last panel of its group,\n"
                    "between that group's boxes and the next group's, where\n"
                    "MSC's sets put every body after every box: the weights\n"
                    "and the downwash cannot be put in the solver's order.\n"
                    "FIX   Give the bodies the IGID of the last group of panels\n"
                    "      (the largest), or an IGID of their own.",
                    y[i].eid, y[i].igid, p[j].eid, p[np - 1].igid);
                free(p);
                free(y);
                return -1;
            }
        }
        nk_b += 2 * y[i].nsb * y[i].norient;
        nj_b += (y[i].nsb + y[i].nint) * y[i].norient;
    }
    if (bk) *bk = nk_b;
    if (bj) *bj = nj_b;
    free(y);
    ids = (int *) msc_alloc(sizeof(int) * (size_t) (nb > 0 ? nb : 1));
    for (i = 0, nb = 0; i < np; i++)
        for (j = 0; j < p[i].ns * p[i].nc; j++) ids[nb++] = p[i].eid + j;
    /* the solver's order (panel by panel, id order within a panel) must be
     * ascending overall: then it is MSC's                               */
    for (i = 1; i < nb; i++) {
        if (ids[i] <= ids[i - 1]) {
            msc_msg(MSC_FATAL, 9232,
                "Box %d of one CAERO1 panel does not follow box %d of the\n"
                "panel before it: the panels' box ids overlap (a panel\n"
                "numbers its boxes EID, EID+1, ...).\n"
                "FIX   Space the CAERO1 ids so that no panel's boxes reach\n"
                "      the next panel's id.", ids[i], ids[i - 1]);
            free(ids);
            free(p);
            return -1;
        }
    }
    free(p);
    *ids_out = ids;
    return nb;
}

/* the extra points of the model (both decks of a restart), ascending   */
static int deck_epoints(const msc_deck *d, const msc_deck *also, int **ids_out)
{
    int *e = NULL, n = 0, cap = 0, i, k, w;
    *ids_out = NULL;
    for (w = 0; w < 2; w++) {
    const msc_deck *dk = w ? also : d;
    if (!dk) continue;
    for (i = 0; i < dk->nbulk; i++) {
        const msc_card *c = &dk->bulk[i];
        if (c->dropped || !msc_streq(c->name, "EPOINT")) continue;
        for (k = 1; k <= c->nfld; k++) {
            int a, b, v;
            if (msc_blank(c, k)) continue;
            if (msc_streq(msc_f(c, k + 1), "THRU")) {
                a = msc_fi(c, k, 0);
                b = msc_fi(c, k + 2, a);
                k += 2;
            } else {
                a = b = msc_fi(c, k, 0);
            }
            for (v = a; v <= b; v++) {
                if (n == cap) {
                    cap = cap ? 2 * cap : 16;
                    e = (int *) msc_realloc(e, sizeof(int) * (size_t) cap);
                }
                e[n++] = v;
            }
        }
    }
    }
    if (n > 1) {
        int m = 1;
        qsort(e, (size_t) n, sizeof(int), cmp_int);
        for (i = 1; i < n; i++) if (e[i] != e[m - 1]) e[m++] = e[i];
        n = m;
    }
    *ids_out = e;
    return n;
}

static int find_id(const int *ids, int n, int id)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ids[mid] == id) return mid;
        if (ids[mid] < id) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* the bodies a PAERO1 associates with its panels                       */

/* a card of deck d (or of `also`) by name and id, NULL when neither has it */
static const msc_card *find_card(const msc_deck *d, const msc_deck *also, const char *name, int id)
{
    int i, w;
    for (w = 0; w < 2; w++) {
        const msc_deck *e = w ? also : d;
        if (!e) continue;
        for (i = 0; i < e->nbulk; i++) {
            const msc_card *c = &e->bulk[i];
            if (!c->dropped && msc_streq(c->name, name) && msc_fi(c, 1, 0) == id) return c;
        }
    }
    return NULL;
}

/* the distance, in the body's interference radii (WIDTH across, WIDTH AR
 * up), from the axis (yb, zb) to the line through (y1, z1) and (y4, z4) in
 * the y-z plane: the panel's plane seen down the stream                */
static double tube_distance(double yb, double zb, double a, double ar, double y1, double z1,
                            double y4, double z4)
{
    double u1 = (y1 - yb) / a, v1 = (z1 - zb) / (a * ar);
    double u4 = (y4 - yb) / a, v4 = (z4 - zb) / (a * ar);
    double du = u4 - u1, dv = v4 - v1, l2 = du * du + dv * dv, t = 0.0;
    if (l2 > 0.0) t = -(u1 * du + v1 * dv) / l2;
    return sqrt((u1 + t * du) * (u1 + t * du) + (v1 + t * dv) * (v1 + t * dv));
}

/* the bodies a PAERO1 lists (B1..B6, its fields 2-7) are the bodies its
 * panels are attached to: the doublet lattice with bodies puts each of
 * their sending points' images inside each such body (mis/fwmw.f, SUBI).
 * A point r from the axis has its image a^2 / r from it, of strength
 * ~ a^2 / r^2, and the image's force on that body's own slender elements
 * goes as one over the square of its distance from the axis - their
 * product ~ 1 / a^2 whatever r is, so every box of a panel far from the
 * body adds a term of the same large size: on the monarch (wing panels
 * listing the booms) QHH came out 10^3 - 10^5 times too large, where
 * Simcenter, given the same deck, changed it by a few percent. NASA's
 * check of where the sending point is (fwmw.f 5100-5600) is a stub.
 * Associated is meant for a panel attached to its body - a wing on its
 * fuselage, a fin on its boom - whose plane runs into the body: then the
 * panel's images lie along the plane, and the monarch's fins associated
 * with their booms give Simcenter's answer (both fin panels, the upper
 * one 17 radii up, its plane through the axis). Panels whose planes pass
 * the body by (the monarch's wing and stabs, 5.5 radii above the booms)
 * gave QHH 6 - 40 times too large even beside the fins on one PAERO1.
 * So a PAERO1 is refused here when one of its panels' planes - the line
 * of the leading edge in the y-z plane - passes more than two
 * interference radii from the axis of a body it lists, or the panel
 * misses the body in x. Panels in a coordinate system
 * (CP), bodies with one, or a PAERO1 with no body: not checked.
 * 1 after a fatal                                                      */
int msc_body_assoc_check(const msc_deck *d)
{
    int i, k, nbad = 0;
    for (i = 0; i < d->nbulk; i++) {
        const msc_card *pa = &d->bulk[i];
        int pid;
        if (pa->dropped || !msc_streq(pa->name, "PAERO1")) continue;
        pid = msc_fi(pa, 1, 0);
        for (k = 2; k <= 7 && k <= pa->nfld; k++) {
            const msc_card *b2, *p2;
            double xb, yb, zb, lb, a, ar, best = 1e30;
            int bid, j, npan = 0, nfar = 0, far_eid = 0, cp = 0;
            if (msc_blank(pa, k)) continue;
            bid = msc_fi(pa, k, 0);
            b2 = find_card(d, NULL, "CAERO2", bid);
            if (!b2 || msc_fi(b2, 3, 0) != 0) continue;
            p2 = find_card(d, NULL, "PAERO2", msc_fi(b2, 2, 0));
            if (!p2) continue;
            xb = msc_fd(b2, 9, 0.0);
            yb = msc_fd(b2, 10, 0.0);
            zb = msc_fd(b2, 11, 0.0);
            lb = msc_fd(b2, 12, 0.0);
            a  = msc_fd(p2, 3, 0.0);
            ar = msc_fd(p2, 4, 1.0);
            if (a <= 0.0 || ar <= 0.0) continue;
            for (j = 0; j < d->nbulk; j++) {
                const msc_card *c = &d->bulk[j];
                double x1, y1, z1, c12, x4, y4, z4, c43, lo, hi, r;
                if (c->dropped || !msc_streq(c->name, "CAERO1") || msc_fi(c, 2, 0) != pid) continue;
                if (msc_fi(c, 3, 0) != 0) { cp = 1; break; }
                npan++;
                x1 = msc_fd(c, 9, 0.0);  y1 = msc_fd(c, 10, 0.0); z1 = msc_fd(c, 11, 0.0);
                c12 = msc_fd(c, 12, 0.0);
                x4 = msc_fd(c, 13, 0.0); y4 = msc_fd(c, 14, 0.0); z4 = msc_fd(c, 15, 0.0);
                c43 = msc_fd(c, 16, 0.0);
                lo = x1 < x4 ? x1 : x4;
                hi = x1 + c12 > x4 + c43 ? x1 + c12 : x4 + c43;
                r = tube_distance(yb, zb, a, ar, y1, z1, y4, z4);
                if (r <= 2.0 && hi >= xb && lo <= xb + lb) continue;
                if (!nfar || r < best) { best = r; far_eid = msc_fi(c, 1, 0); }
                nfar++;
            }
            if (cp || nfar == 0) continue;
            msc_msg_at(MSC_FATAL, 9241, pa,
                "PAERO1 %d lists CAERO2 %d as an associated body, and %d of\n"
                "its %d panels are not attached to it: the nearest of them,\n"
                "CAERO1 %d, lies in a plane %.3g interference radii from its\n"
                "axis, or beside it in x\n"
                "(PAERO2 WIDTH %.4g, AR %.4g). NASTRAN-95 puts the image of\n"
                "every box of an associated panel inside the body (mis/fwmw.f),\n"
                "and the images of boxes far from it add terms of the same\n"
                "large size however far they are: the aerodynamic matrices\n"
                "come out wrong by orders of magnitude, where MSC's change\n"
                "a little. An associated body is one the panel is attached\n"
                "to (a wing root on a fuselage, a fin on its boom).\n"
                "FIX   List on a panel's PAERO1 only the bodies it touches -\n"
                "      a PAERO1 per group of panels - and leave the rest out;\n"
                "      every body still interferes with every panel of its\n"
                "      IGID group.",
                pid, bid, nfar, npan, far_eid, best, a, ar);
            nbad++;
        }
    }
    return nbad ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* the matrices in the solver's order, written as DMIs                  */

typedef struct { int r, c, at; double v; } dyn_term;

/* by column, row, then the order the terms were given in             */
static int cmp_term(const void *a, const void *b)
{
    const dyn_term *p = (const dyn_term *) a, *q = (const dyn_term *) b;
    if (p->c != q->c) return (p->c > q->c) - (p->c < q->c);
    if (p->r != q->r) return (p->r > q->r) - (p->r < q->r);
    return (p->at > q->at) - (p->at < q->at);
}

/* the k-set position (1-based) of a box component, MSC's order: two per
 * box, T3 then R5; 0 for anything else                                  */
static int kpos(const int *box, int nb, int id, int comp)
{
    int b = find_id(box, nb, id);
    if (b < 0 || (comp != 3 && comp != 5)) return 0;
    return 2 * b + (comp == 5 ? 2 : 1);
}

/* the terms of matrix M (DMI set positions, or DMIK / DMIJ ids) in the
 * solver's positions; rows nr, columns nc; *nbad the terms that named no
 * position. Duplicates: the last one stands (MSC calls a repeat a fatal) */
static dyn_term *to_positions(const dyn_mat *M, int jset, const int *box, int nb,
                              const int *ep, int ne, int nr, int nc, int *nt, int *nbad, int *ndup)
{
    dyn_term *t = (dyn_term *) msc_alloc(sizeof(dyn_term) * (size_t) (M->ne + nr + 1));
    int i, n = 0, j;
    *nbad = 0;
    *ndup = 0;
    if (M->kind == 1 && M->form == 8) {           /* identity            */
        for (i = 1; i <= nr && i <= nc; i++) { t[n].r = t[n].c = i; t[n].at = n; t[n].v = 1.0; n++; }
    }
    for (i = 0; i < M->ne; i++) {
        const dyn_el *e = &M->e[i];
        int r, c;
        if (M->kind == 1) {
            r = e->r;
            c = e->c;
            if (M->form == 3) {                       /* diagonal: column 1 */
                if (c != 1) { (*nbad)++; continue; }
                c = r;
            }
            if (M->form == 6 && r != c) {              /* symmetric          */
                if (r >= 1 && r <= nr && c >= 1 && c <= nc) {
                    t[n].r = c; t[n].c = r; t[n].at = n; t[n].v = e->v; n++;
                }
            }
        } else if (!jset) {
            r = kpos(box, nb, e->r, e->rc);
            c = kpos(box, nb, e->c, e->cc);
        } else {
            int b = find_id(box, nb, e->r);
            r = (b >= 0 && (e->rc == 3 || e->rc == 0)) ? b + 1 : 0;
            c = find_id(ep, ne, e->c);
            c = (c >= 0) ? c + 1 : ((e->c >= 1 && e->c <= nc) ? e->c : 0);
        }
        if (r < 1 || r > nr || c < 1 || c > nc) { (*nbad)++; continue; }
        t[n].r = r; t[n].c = c; t[n].at = n; t[n].v = e->v; n++;
    }
    qsort(t, (size_t) n, sizeof(dyn_term), cmp_term);
    /* a repeat: the one given last stands                               */
    for (i = 0, j = 0; i < n; i++) {
        if (j > 0 && t[j - 1].r == t[i].r && t[j - 1].c == t[i].c) {
            (*ndup)++;
            t[j - 1] = t[i];
            continue;
        }
        t[j++] = t[i];
    }
    *nt = j;
    return t;
}

/* DMI NAME 0 FORM 1 0 - NR NC, then a card per non-null column: the row
 * of each term written before it (an integer restarts the row)          */
static void emit_dmi(msc_list *out, const char *name, int form, int nr, int nc,
                     const dyn_term *t, int nt)
{
    msc_card *o = msc_list_add(out, "DMI");
    int i = 0;
    msc_set (o, 1, name);
    msc_seti(o, 2, 0);
    msc_seti(o, 3, form);
    msc_seti(o, 4, 1);
    msc_seti(o, 5, 0);
    msc_set (o, 6, "");
    msc_seti(o, 7, nr);
    msc_seti(o, 8, nc);
    if (nt == 0) {
        /* NASTRAN-95's IFP refuses a DMI with no column (UFM 325): a
         * null matrix is one zero term                                 */
        o = msc_list_add(out, "DMI");
        msc_set (o, 1, name);
        msc_seti(o, 2, 1);
        msc_seti(o, 3, 1);
        msc_set (o, 4, "0.0");
        return;
    }
    while (i < nt) {
        int c = t[i].c, f = 3;
        o = msc_list_add(out, "DMI");
        msc_set (o, 1, name);
        msc_seti(o, 2, c);
        for (; i < nt && t[i].c == c; i++) {
            msc_seti(o, f++, t[i].r);
            msc_setd(o, f++, t[i].v);
        }
    }
}

static const dyn_mat *pick(int k, const char **spelling)
{
    if (G.dmi[k].kind) {
        if (G.ids[k].kind)
            msc_msg(MSC_INFO, 9230, "%s is given as a DMI and as a %s; the DMI is used,\n"
                    "as in MSC (QRG DMIJ / DMIK remark 1).", dyn_name[k],
                    k < DYN_D1JE ? "DMIK" : "DMIJ");
        *spelling = "DMI";
        return &G.dmi[k];
    }
    if (G.ids[k].kind) { *spelling = k < DYN_D1JE ? "DMIK" : "DMIJ"; return &G.ids[k]; }
    return NULL;
}

/* after the bulk data pass: the matrices this deck gave, written back
 * into `out` in the solver's order. *wkk and *dje say what the alter has
 * to do with them; *ne_out the deck's extra points. 1 after a fatal.   */
int msc_dyn_emit(const msc_deck *d, const msc_deck *also, msc_list *out, int *wkk,
                 int *dje, int *ne_out)
{
    const dyn_mat *W, *D[2];
    const char    *sw = "", *sd[2] = { "", "" };
    int           *box = NULL, *ep = NULL, nb, ne, k, bk = 0, bj = 0;

    *wkk = *dje = 0;
    ne = deck_epoints(d, also, &ep);
    if (ne_out) *ne_out = ne;
    if (!G.active) { free(ep); return 0; }
    if (G.dmi[DYN_WTF].kind || G.ids[DYN_WTF].kind) {
        W = pick(DYN_WTF, &sw);
        if (G.dmi[DYN_WKK].kind || G.ids[DYN_WKK].kind)
            msc_msg(MSC_INFO, 9230, "The deck has WTFACT and WKK: WTFACT weights the\n"
                    "unsteady box forces, as in MSC (WKK serves when there is\n"
                    "no WTFACT); WKK itself is a SOL 144 matrix and is left\n"
                    "out here.");
    } else {
        W = pick(DYN_WKK, &sw);
    }
    D[0] = pick(DYN_D1JE, &sd[0]);
    D[1] = pick(DYN_D2JE, &sd[1]);
    if (!W && !D[0] && !D[1]) { free(ep); return 0; }
    if ((D[0] || D[1]) && ne == 0) {
        msc_msg(MSC_WARN, 9236,
            "D1JE / D2JE (the downwash of extra points) are given and\n"
            "the model has no EPOINT: the matrices have no columns to\n"
            "go to, and AMP leaves them out, as MSC's does. They are\n"
            "not written.");
        D[0] = D[1] = NULL;
        if (!W) { free(ep); return 0; }
    }
    nb = deck_boxes(d, also, &box, &bk, &bj);
    if (nb < 0) { free(ep); return 1; }
    if (nb == 0) {
        msc_msg(MSC_WARN, 9236, "The deck has %s%s and no CAERO1 panel to apply it\n"
                "to; it is left out.",
                W ? (W == &G.dmi[DYN_WTF] || W == &G.ids[DYN_WTF] ? "WTFACT" : "WKK") : "",
                (D[0] || D[1]) ? (W ? ", D1JE / D2JE" : "D1JE / D2JE") : "");
        free(ep);
        return 0;
    }

    if (W) {
        const char *wn = (W == &G.dmi[DYN_WTF] || W == &G.ids[DYN_WTF]) ? "WTFACT" : "WKK";
        int nk = 2 * nb + bk, nt, nbad, ndup, ndiag = 0, i, nbd = 0, ncross = 0, mixed = 0;
        dyn_term *t;
        double lo = 0.0, hi = 0.0, bv = 0.0;
        if (W->kind == 1 && (W->m != nk || (W->form != 3 && W->form != 8 && W->n != nk))) {
            msc_msg(MSC_FATAL, 9232,
                "DMI %s is %d x %d and the k set of the model's %d CAERO1\n"
                "boxes is %d (two a box: the force T3 and the moment R5,\n"
                "the boxes in ascending id)%s.\n"
                "FIX   Write it on the k set, or as a DMIK by box id and\n"
                "      component.", wn, W->m, W->n, nb, nk,
                bk ? ", then the CAERO2 bodies' k dofs" : "");
            free(box); free(ep);
            return 1;
        }
        t = to_positions(W, 0, box, nb, ep, ne, nk, nk, &nt, &nbad, &ndup);
        /* the bodies' block: a multiple of the identity, joined to no box
         * (its order inside the block is each code's own)                */
        for (i = 0; bk && i < nt; i++) {
            if (t[i].r <= 2 * nb && t[i].c <= 2 * nb) continue;
            if (t[i].r != t[i].c) { ncross++; continue; }
            if (nbd && t[i].v != bv) mixed = 1;
            bv = t[i].v;
            nbd++;
        }
        if (ncross || mixed || (nbd && nbd != bk)) {
            msc_msg(MSC_FATAL, 9232,
                "%s %s on the %d k dofs of the model's CAERO2 bodies (rows\n"
                "and columns %d to %d) is not a multiple of the identity:\n"
                "%d term%s off the diagonal or joining a body to a box, %d\n"
                "of the %d diagonal terms given%s. The bodies' dofs are not\n"
                "in the same order in NASTRAN-95 and MSC, so only a weight\n"
                "that is the same on every one of them can be put in the\n"
                "solver's order.\n"
                "FIX   Give every body dof one weight (1 keeps the bodies'\n"
                "      forces as they are) and no off-diagonal term.",
                sw, wn, bk, 2 * nb + 1, nk, ncross, ncross == 1 ? "" : "s", nbd, bk,
                mixed ? ", not all the same" : "");
            free(t); free(box); free(ep);
            return 1;
        }
        for (i = 0; i < nt; i++) {
            if (t[i].r == t[i].c) {
                if (!ndiag || t[i].v < lo) lo = t[i].v;
                if (!ndiag || t[i].v > hi) hi = t[i].v;
                ndiag++;
            }
        }
        emit_dmi(out, "WKK", 1, nk, nk, t, nt);
        msc_msg(MSC_INFO, 9230,
            "%s %s weights every box force and moment of the doublet\n"
            "lattice (%d terms, %d of them on the diagonal, %.6g to\n"
            "%.6g): SKJ is multiplied by it after AMG - MSC's WSKJF =\n"
            "WTFACT SKJ - so QHHL, QKHL and QHJL carry it. Written as\n"
            "DMI WKK on the solver's k set: the %d boxes in ascending id,\n"
            "T3 then R5 of each (%d rows). A k-set dof it does not name\n"
            "gets no force.%s",
            sw, wn, nt, ndiag, lo, hi, nb, nk,
            (W->kind == 2 && G.rf == 10)
                ? "\nSimcenter 2606's SOL 145 does not find the DMIK spelling\n"
                  "and leaves its forces unweighted; to compare, give it a\n"
                  "DMI WKK on the k set."
                : "");
        if (bk)
            msc_msg(MSC_INFO, 9230,
                "%s %s: the model's CAERO2 bodies have %d k dofs, after the\n"
                "boxes (rows %d to %d), weighted %.6g each%s.", sw, wn, bk, 2 * nb + 1, nk,
                nbd ? bv : 0.0,
                nbd ? "" : ": the matrix names none of them, so the bodies carry\n"
                           "no force (MSC's reading of a dof a WKK leaves out)");
        if (nbad)
            msc_msg(MSC_INFO, 9230, "%s %s: %d term%s named no box component of the k\n"
                    "set (a box id with component 3 or 5) and %s left out.",
                    sw, wn, nbad, nbad == 1 ? "" : "s", nbad == 1 ? "is" : "are");
        if (ndup)
            msc_msg(MSC_WARN, 9233, "%s %s names %d term%s twice; the last value given\n"
                    "is kept (MSC stops on a repeated term).", sw, wn, ndup,
                    ndup == 1 ? "" : "s");
        free(t);
        *wkk = 1;
    }

    if (D[0] || D[1]) {
        for (k = 0; k < 2; k++) {
            const char *dn = k ? "D2JE" : "D1JE";
            int nt = 0, nbad = 0, ndup = 0;
            dyn_term *t = NULL;
            int nj = nb + bj, i, nbody = 0;
            if (D[k] && D[k]->kind == 1 && (D[k]->m != nj || D[k]->n > ne)) {
                msc_msg(MSC_FATAL, 9232,
                    "DMI %s is %d x %d; the j set of the model's CAERO1 boxes\n"
                    "is %d (one a box, ascending id)%s and it has %d extra\n"
                    "point%s (EPOINT, a column each).\n"
                    "FIX   Write it with %d rows and %d column%s.", dn, D[k]->m,
                    D[k]->n, nj, bj ? ", then the CAERO2 bodies' elements" : "", ne,
                    ne == 1 ? "" : "s", nj, ne, ne == 1 ? "" : "s");
                free(box); free(ep);
                return 1;
            }
            if (D[k]) t = to_positions(D[k], 1, box, nb, ep, ne, nj, ne, &nt, &nbad, &ndup);
            for (i = 0; i < nt; i++) if (t[i].r > nb && t[i].v != 0.0) nbody++;
            if (nbody) {
                msc_msg(MSC_FATAL, 9232,
                    "%s %s puts downwash on %d term%s of the CAERO2 bodies' rows\n"
                    "(%d to %d). The bodies' elements are not in the same order\n"
                    "in NASTRAN-95 and MSC, so their rows cannot be put in the\n"
                    "solver's order.\n"
                    "FIX   Leave the bodies' rows null (a control surface moves\n"
                    "      boxes).", sd[k], dn, nbody, nbody == 1 ? "" : "s", nb + 1, nj);
                free(t); free(box); free(ep);
                return 1;
            }
            emit_dmi(out, dn, 2, nj, ne, t, nt);
            if (D[k])
                msc_msg(MSC_INFO, 9230,
                    "%s %s (%d terms): the downwash of the %d extra point%s,\n"
                    "w_j = (D1JE + i k D2JE) u_e. AMP puts it behind the\n"
                    "modes' downwash, so QHHL gets a column per extra point\n"
                    "(its row null), through the weighted forces as in MSC.\n"
                    "Written as DMI %s, %d rows (the boxes in ascending id%s)\n"
                    "by %d.", sd[k], dn, nt, ne, ne == 1 ? "" : "s", dn, nj,
                    bj ? ", then the bodies' null rows" : "", ne);
            else
                msc_msg(MSC_INFO, 9230, "%s is not given; a null %s is written beside\n"
                        "%s.", dn, dn, k ? "D1JE" : "D2JE");
            if (nbad)
                msc_msg(MSC_INFO, 9230, "%s %s: %d term%s named no box (component 3) or\n"
                        "extra point and %s left out.", sd[k], dn, nbad,
                        nbad == 1 ? "" : "s", nbad == 1 ? "is" : "are");
            if (ndup)
                msc_msg(MSC_WARN, 9233, "%s %s names %d term%s twice; the last value\n"
                        "given is kept.", sd[k], dn, ndup, ndup == 1 ? "" : "s");
            free(t);
        }
        *dje = 1;
    }
    free(box);
    free(ep);
    return msc_nfatal() ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* the spanwise gust: GUST's SHAPE (field 7), sets of GUST cards, WGSPAN */

static void add_gust(const msc_card *c, int shape)
{
    dyn_gust *g;
    int k;
    if (G.ngust == G.gcap) {
        G.gcap = G.gcap ? 2 * G.gcap : 16;
        G.gust = (dyn_gust *) msc_realloc(G.gust, sizeof(dyn_gust) * (size_t) G.gcap);
    }
    g = &G.gust[G.ngust];
    memset(g, 0, sizeof(*g));
    g->sid   = msc_fi(c, 1, 0);
    g->dload = msc_fi(c, 2, 0);
    g->shape = shape;
    g->at    = G.ngust++;
    g->line  = c->line;
    for (k = 0; k < 5; k++) {
        strncpy(g->f[k], msc_f(c, k + 1), MSC_FLDLEN - 1);
        g->f[k][MSC_FLDLEN - 1] = '\0';
    }
}

/* the SHAPE of a GUST card (field 7 of the card, msc_f 6): 0 when blank;
 * a fatal for anything but a whole number >= 0                           */
static int gust_shape(const msc_card *c)
{
    const char *f = msc_f(c, 6);
    if (!*f) return 0;
    if (!fld_is_int(f) || atoi(f) < 0) {
        msc_msg_at(MSC_FATAL, 9240, c,
            "GUST %d: SHAPE (field 7) is \"%s\"; it is a column of DMI\n"
            "WGSPAN, a whole number (0: the uniform gust).",
            msc_fi(c, 1, 0), f);
        return 0;
    }
    return atoi(f);
}

/* one GUST card of a SOL 145 / 146 deck, noted: 0 when it goes through as
 * it is, 1 when this file writes it (a spanwise SOL 146 deck), 2 when it
 * is to be written without its fields past V (a SOL 145 deck: the
 * flutter solution has no gust)                                         */
int msc_dyn_gust(const msc_card *c)
{
    int i, extra = 0;
    if (!G.active) return 0;
    add_gust(c, gust_shape(c));
    for (i = 6; i <= c->nfld; i++) if (!msc_blank(c, i)) extra = 1;
    for (i = 7; i <= c->nfld; i++)
        if (!msc_blank(c, i)) {
            msc_msg_at(MSC_WARN, 9239, c, "GUST %d: fields past SHAPE (field 7) are not read;\n"
                       "\"%s\" in field %d is left out.", msc_fi(c, 1, 0), msc_f(c, i), i + 1);
            break;
        }
    if (G.span) return 1;
    return extra ? 2 : 0;
}

/* by id, then the deck's order                                           */
static int cmp_gust(const void *a, const void *b)
{
    const dyn_gust *p = (const dyn_gust *) a, *q = (const dyn_gust *) b;
    if (p->sid != q->sid) return (p->sid > q->sid) - (p->sid < q->sid);
    return (p->at > q->at) - (p->at < q->at);
}

/* a GUST field written into WGSPAN as the card spells it, but as a real:
 * an integer in a DMI column is a row number                            */
static void real_text(char *out, const char *f)
{
    snprintf(out, MSC_FLDLEN, "%s", *f ? f : "0.0");
    if (fld_is_int(out) && strlen(out) + 1 < MSC_FLDLEN) strcat(out, ".");
}

/* the solver's DMI WGSPAN: the header, nb + bj + 4 rows by ncol (bj the
 * CAERO2 bodies' j-set elements, after the boxes)                       */
static void wgspan_header(msc_list *out, int nb, int bj, int ncol)
{
    msc_card *o = msc_list_add(out, "DMI");
    msc_set (o, 1, "WGSPAN");
    msc_seti(o, 2, 0);
    msc_seti(o, 3, 2);
    msc_seti(o, 4, 1);
    msc_seti(o, 5, 0);
    msc_set (o, 6, "");
    msc_seti(o, 7, nb + bj + 4);
    msc_seti(o, 8, ncol);
}

/* one column of it, a card: shape `shape` of the user's WGSPAN W (ones
 * for 0, on the bodies' bj rows too) on the nb boxes - the row before each
 * value, zero weights left out, the last given of a repeated row standing -
 * then rows nb+bj+1 .. nb+bj+4, WG, X0, V and the id, as given           */
static void wgspan_column(msc_list *out, int col, int nb, int bj, const dyn_mat *W, int shape,
                          const char *wg, const char *x0, const char *v, const char *id)
{
    msc_card *o = msc_list_add(out, "DMI");
    char      txt[MSC_FLDLEN];
    int       f = 3, last = 0, j;
    msc_set (o, 1, "WGSPAN");
    msc_seti(o, 2, col);
    if (shape == 0) {
        msc_seti(o, f++, 1);
        for (j = 1; j <= nb + bj; j++) msc_set(o, f++, "1.0");
    } else {
        double *c = (double *) msc_alloc(sizeof(double) * (size_t) (nb + 1));
        char   *has = (char *) msc_alloc((size_t) (nb + 1));
        memset(has, 0, (size_t) (nb + 1));
        for (j = 0; j <= nb; j++) c[j] = 0.0;
        for (j = 0; j < W->ne; j++) {
            const dyn_el *e = &W->e[j];
            if (e->c != shape || e->r < 1 || e->r > nb) continue;
            c[e->r] = e->v;
            has[e->r] = 1;
        }
        /* the first value's row is always given (the card's I1), a later
         * one's when it does not follow the row before                 */
        for (j = 1; j <= nb; j++) {
            if (!has[j] || c[j] == 0.0) continue;
            if (last == 0 || j != last + 1) msc_seti(o, f++, j);
            msc_setd(o, f++, c[j]);
            last = j;
        }
        free(c);
        free(has);
    }
    msc_seti(o, f++, nb + bj + 1);
    real_text(txt, wg); msc_set(o, f++, txt);
    real_text(txt, x0); msc_set(o, f++, txt);
    real_text(txt, v);  msc_set(o, f++, txt);
    real_text(txt, id); msc_set(o, f++, txt);
}

/* OUTPUT4 QHG of a deck that is no spanwise gust deck (SOL 145, or SOL 146
 * without a SHAPE field): the gust columns GUSTQ forms per (Mach, k) pair,
 * one per column of the user's DMI WGSPAN at X0 = 0 (the basic origin,
 * the plant's reference), written as the solver's WGSPAN (WG 1, X0 0, V 1,
 * the column's number); no WGSPAN: the uniform column, GUSTQ's default.
 * *wgspan: written. 1 after a fatal                                      */
static int export_wgspan(const msc_deck *d, const msc_deck *also, msc_list *out, int *wgspan)
{
    const dyn_mat *W = &G.dmi[DYN_WGS];
    int           *box = NULL, nb, c, bk = 0, bj = 0;
    char           id[MSC_FLDLEN];
    if (!W->kind) {
        msc_msg(MSC_INFO, 9239,
            "OUTPUT4 QHG: the gust column of the uniform gust at X0 = 0 per\n"
            "(Mach, k) pair, Q_hg = QJHL' w (module GUSTQ). A DMI WGSPAN (a\n"
            "row per CAERO1 box, a column per shape or segment) gives a\n"
            "column per shape instead.");
        return 0;
    }
    if (W->form != 1 && W->form != 2) {
        msc_msg(MSC_FATAL, 9240,
            "DMI WGSPAN has FORM %d. It is a rectangular matrix (FORM 2),\n"
            "a row per box and a column per shape.", W->form);
        return 1;
    }
    nb = deck_boxes(d, also, &box, &bk, &bj);
    if (nb < 0) return 1;
    free(box);
    if (bj) {
        msc_msg(MSC_FATAL, 9240,
            "DMI WGSPAN (spanwise gust shapes for OUTPUT4 QHG) and CAERO2\n"
            "bodies: the shapes weight the boxes, and the bodies' j-set\n"
            "elements have no place in them here.\n"
            "FIX   Leave WGSPAN out (QHG then has the uniform gust's\n"
            "      column), or the bodies.");
        return 1;
    }
    if (W->m != nb) {
        msc_msg(MSC_FATAL, 9240,
            "DMI WGSPAN is %d x %d and the j set of the model's CAERO1\n"
            "boxes is %d (a row per box, the boxes in ascending id).\n"
            "FIX   Write it with %d rows.", W->m, W->n, nb, nb);
        return 1;
    }
    wgspan_header(out, nb, 0, W->n);
    for (c = 1; c <= W->n; c++) {
        snprintf(id, sizeof(id), "%d", c);
        wgspan_column(out, c, nb, 0, W, c, "1.0", "0.0", "1.0", id);
    }
    *wgspan = 1;
    msc_msg(MSC_INFO, 9239,
        "OUTPUT4 QHG: a gust column per column of DMI WGSPAN (%d), per\n"
        "(Mach, k) pair, at X0 = 0: Q_hg(c) = QJHL' (WGSPAN(:, c)\n"
        "cos(gamma) exp(-i k x / b)) (module GUSTQ after AMP). Written as\n"
        "DMI WGSPAN, %d x %d (the shapes, then WG 1, X0 0, V 1 and the\n"
        "column's number).", W->n, nb + 4, W->n);
    return msc_nfatal() ? 1 : 0;
}

/* after the bulk data pass of a spanwise SOL 146 deck: one GUST card per
 * id, and the GUST module's DMI WGSPAN (the j set plus WG, X0, V and the
 * id, a column per card), written into `out`; of any other SOL 145 / 146
 * deck with OUTPUT4 QHG, the WGSPAN GUSTQ reads. *gust says the alter has
 * to pass WGSPAN to GUST (ALTER 94), *wgspan that the deck has the
 * solver's WGSPAN. `also`: a restart's modes deck. 1 after a fatal       */
int msc_dyn_gust_emit(const msc_deck *d, const msc_deck *also, msc_list *out, int *gust,
                      int *wgspan)
{
    const dyn_mat *W = &G.dmi[DYN_WGS];
    int           *box = NULL, nb, ng, i, nshaped = 0, maxshape = 0, nsets = 0, bk = 0, bj = 0;
    int            want = msc_op4_wants("QHGL");

    *gust = 0;
    *wgspan = 0;
    if (!G.active) return 0;
    for (i = 0; i < G.ngust; i++) {
        if (G.gust[i].shape > 0) nshaped++;
        if (G.gust[i].shape > maxshape) maxshape = G.gust[i].shape;
    }
    if (G.rf != 11 || !G.span) {
        if (G.rf != 11 && nshaped)
            msc_msg(MSC_INFO, 9239, "%d GUST card%s name%s a SHAPE (field 7); SOL 145 has no\n"
                    "gust, and the field is left out.", nshaped, nshaped == 1 ? "" : "s",
                    nshaped == 1 ? "s" : "");
        if (want) return export_wgspan(d, also, out, wgspan);
        if (W->kind)
            msc_msg(MSC_WARN, 9239, "DMI WGSPAN (the spanwise gust's shapes) is given, and no\n"
                    "GUST card has a SHAPE (field 7) and no OUTPUT4 QHG asks\n"
                    "for the gust columns: it is left out.");
        return msc_nfatal() ? 1 : 0;
    }
    /* a restart's modes deck with GUST cards: not a spanwise deck's case */
    if (also) {
        for (i = 0; i < also->nbulk; i++)
            if (!also->bulk[i].dropped && msc_streq(also->bulk[i].name, "GUST")) {
                msc_msg(MSC_FATAL, 9240,
                    "The restart's modes deck has GUST cards too. A spanwise\n"
                    "gust deck's GUST cards are all written by the front end;\n"
                    "give them in the gust deck alone.");
                return 1;
            }
    }
    if (maxshape > 0 && !W->kind) {
        for (i = 0; i < G.ngust; i++) if (G.gust[i].shape > 0) break;
        msc_msg(MSC_FATAL, 9240,
            "GUST %d names SHAPE %d (field 7: a column of DMI WGSPAN,\n"
            "the weight on each box's gust downwash) and the deck has no\n"
            "DMI WGSPAN.\n"
            "FIX   Give the shapes as DMI WGSPAN, a row per CAERO1 box in\n"
            "      ascending id and a column per shape.",
            G.gust[i].sid, G.gust[i].shape);
        return 1;
    }
    /* the cards by id (the DIT's order), the deck's order within an id   */
    qsort(G.gust, (size_t) G.ngust, sizeof(dyn_gust), cmp_gust);
    ng = G.ngust;
    for (i = 0; i < ng; i++) {
        if (i > 0 && G.gust[i].sid == G.gust[i - 1].sid) {
            if (G.gust[i].dload != G.gust[i - 1].dload) {
                msc_msg(MSC_FATAL, 9240,
                    "The GUST cards of id %d name DLOAD %d and %d. The cards\n"
                    "of one id load one subcase together, and a subcase has\n"
                    "one load: its frequency (or time) function.\n"
                    "FIX   Give the cards of an id the same DLOAD, or give each\n"
                    "      its own id (a subcase each).",
                    G.gust[i].sid, G.gust[i - 1].dload, G.gust[i].dload);
                return 1;
            }
            continue;
        }
        nsets++;
    }
    if (W->kind && W->form != 1 && W->form != 2) {
        msc_msg(MSC_FATAL, 9240,
            "DMI WGSPAN has FORM %d. It is a rectangular matrix (FORM 2),\n"
            "a row per box and a column per shape.", W->form);
        return 1;
    }
    nb = deck_boxes(d, also, &box, &bk, &bj);
    if (nb < 0) return 1;
    free(box);
    if (nb == 0) {
        msc_msg(MSC_FATAL, 9240, "The GUST cards have a SHAPE field and the model has no\n"
                "CAERO1 panel: there is no box to weight.");
        return 1;
    }
    if (bj && maxshape > 0) {
        for (i = 0; i < ng; i++) if (G.gust[i].shape == maxshape) break;
        msc_msg(MSC_FATAL, 9240,
            "GUST %d names SHAPE %d and the model has CAERO2 bodies: a\n"
            "shape weights the boxes, and the bodies' j-set elements have\n"
            "no place in it here (their order is not MSC's).\n"
            "FIX   Use SHAPE 0 (the uniform gust, on the bodies too), or\n"
            "      leave the bodies out.", G.gust[i].sid, maxshape);
        return 1;
    }
    if (W->kind && W->m != nb) {
        msc_msg(MSC_FATAL, 9240,
            "DMI WGSPAN is %d x %d and the j set of the model's CAERO1\n"
            "boxes is %d (a row per box, the boxes in ascending id).\n"
            "FIX   Write it with %d rows.", W->m, W->n, nb, nb);
        return 1;
    }
    if (W->kind && maxshape > W->n) {
        for (i = 0; i < ng; i++) if (G.gust[i].shape == maxshape) break;
        msc_msg(MSC_FATAL, 9240, "GUST %d names SHAPE %d and DMI WGSPAN has %d column%s.",
                G.gust[i].sid, maxshape, W->n, W->n == 1 ? "" : "s");
        return 1;
    }

    /* one GUST card per id, the first card's fields (its DLOAD is the
     * subcase's; in the module the cards' own WG, X0, V are WGSPAN's)   */
    for (i = 0; i < ng; i++) {
        msc_card *o;
        int k;
        if (i > 0 && G.gust[i].sid == G.gust[i - 1].sid) continue;
        o = msc_list_add(out, "GUST");
        for (k = 0; k < 5; k++) msc_set(o, k + 1, G.gust[i].f[k]);
    }

    /* DMI WGSPAN: nb + 4 rows by a column per card - the weights (the
     * shape's column, ones for SHAPE 0), then WG, X0, V and the id as the
     * card spells them                                                   */
    wgspan_header(out, nb, bj, ng);
    for (i = 0; i < ng; i++) {
        const dyn_gust *g = &G.gust[i];
        wgspan_column(out, i + 1, nb, bj, W, g->shape, g->f[2], g->f[3], g->f[4], g->f[0]);
    }
    {
        char list[600];
        size_t at = 0;
        list[0] = '\0';
        for (i = 0; i < ng && at < sizeof(list) - 60; i++)
            at += (size_t) snprintf(list + at, sizeof(list) - at, "%s%d:%d", i ? ", " : "",
                                    G.gust[i].sid, G.gust[i].shape);
        if (i < ng) snprintf(list + at, sizeof(list) - at, ", ...");
        msc_msg(MSC_INFO, 9239,
            "The spanwise gust: %d GUST card%s in %d set%s (id:SHAPE %s).\n"
            "Each card weights each box's gust downwash by its shape\n"
            "(SHAPE m: column m of DMI WGSPAN%s; 0 the uniform gust) with\n"
            "its own WG, X0 and V, w_j = WG psi(j) cos(gamma_j)\n"
            "exp(-i w (x_j - X0) / V), and the cards of one id load the\n"
            "subcase that selects it together. Written as one GUST card\n"
            "per id and DMI WGSPAN, %d x %d: the boxes in ascending id%s,\n"
            "then WG, X0, V and the id, by a column per card; GUST's sixth\n"
            "input (ALTER 94).",
            ng, ng == 1 ? "" : "s", nsets, nsets == 1 ? "" : "s", list,
            W->kind ? "" : ", not given", nb + bj + 4, ng,
            bj ? " (then the CAERO2 bodies' elements, 1 each)" : "");
    }
    *gust = 1;
    *wgspan = 1;
    return msc_nfatal() ? 1 : 0;
}
