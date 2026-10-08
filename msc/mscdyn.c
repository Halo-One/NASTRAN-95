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
 * not mapped through a guess at the sort's order of ties. CAERO1 only:
 * the strip theory's and the bodies' k sets are other shapes.
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
 */
#include "msc.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { DYN_WKK = 0, DYN_WTF, DYN_D1JE, DYN_D2JE, DYN_N };
static const char *dyn_name[DYN_N] = { "WKK", "WTFACT", "D1JE", "D2JE" };

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

static struct {
    int     active;              /* rigid format 10 or 11                     */
    int     rf;
    dyn_mat dmi[DYN_N];          /* the DMI spelling                          */
    dyn_mat ids[DYN_N];          /* DMIK (WKK, WTFACT) / DMIJ (D1JE, D2JE)    */
    int     static_left;         /* DMIJ W2GJ / FA2J left out                 */
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
                   "left out: SOL 145 / 146 here have no bodies.", msc_f(c, 1));
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

/* every CAERO1 box id of the model, ascending (MSC's order, and - checked
 * here - NASTRAN-95's); the count, or -1 after a fatal. `also` is the
 * modes deck of a restart (its panels are the solver's too), or NULL    */
static int deck_boxes(const msc_deck *d, const msc_deck *also, int **ids_out)
{
    dyn_panel *p = NULL;
    int np = 0, cap = 0, nb = 0, i, j, w, other = 0, *ids;
    char other_name[MSC_FLDLEN] = "";
    *ids_out = NULL;
    for (w = 0; w < 2; w++) {
        const msc_deck *e = w ? also : d;
        if (!e) continue;
        for (i = 0; i < e->nbulk; i++) {
            const msc_card *c = &e->bulk[i];
            int eid, q, dup = 0;
            if (c->dropped) continue;
            if (!msc_streq(c->name, "CAERO1")) {
                if (!strncmp(c->name, "CAERO", 5)) {
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
            "doublet-lattice boxes of CAERO1 panels only; another\n"
            "theory's k set has another shape.", other_name);
        free(p);
        return -1;
    }
    if (np == 0) {
        free(p);
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
            return -1;
        }
    }
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
    int           *box = NULL, *ep = NULL, nb, ne, k;

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
    nb = deck_boxes(d, also, &box);
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
        int nk = 2 * nb, nt, nbad, ndup, ndiag = 0, i;
        dyn_term *t;
        double lo = 0.0, hi = 0.0;
        if (W->kind == 1 && (W->m != nk || (W->form != 3 && W->form != 8 && W->n != nk))) {
            msc_msg(MSC_FATAL, 9232,
                "DMI %s is %d x %d and the k set of the model's %d CAERO1\n"
                "boxes is %d (two a box: the force T3 and the moment R5,\n"
                "the boxes in ascending id).\n"
                "FIX   Write it on the k set, or as a DMIK by box id and\n"
                "      component.", wn, W->m, W->n, nb, nk);
            free(box); free(ep);
            return 1;
        }
        t = to_positions(W, 0, box, nb, ep, ne, nk, nk, &nt, &nbad, &ndup);
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
            if (D[k] && D[k]->kind == 1 && (D[k]->m != nb || D[k]->n > ne)) {
                msc_msg(MSC_FATAL, 9232,
                    "DMI %s is %d x %d; the j set of the model's CAERO1 boxes\n"
                    "is %d (one a box, ascending id) and it has %d extra\n"
                    "point%s (EPOINT, a column each).\n"
                    "FIX   Write it with %d rows and %d column%s.", dn, D[k]->m,
                    D[k]->n, nb, ne, ne == 1 ? "" : "s", nb, ne, ne == 1 ? "" : "s");
                free(box); free(ep);
                return 1;
            }
            if (D[k]) t = to_positions(D[k], 1, box, nb, ep, ne, nb, ne, &nt, &nbad, &ndup);
            emit_dmi(out, dn, 2, nb, ne, t, nt);
            if (D[k])
                msc_msg(MSC_INFO, 9230,
                    "%s %s (%d terms): the downwash of the %d extra point%s,\n"
                    "w_j = (D1JE + i k D2JE) u_e. AMP puts it behind the\n"
                    "modes' downwash, so QHHL gets a column per extra point\n"
                    "(its row null), through the weighted forces as in MSC.\n"
                    "Written as DMI %s, %d rows (the boxes in ascending id) by\n"
                    "%d.", sd[k], dn, nt, ne, ne == 1 ? "" : "s", dn, nb, ne);
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
