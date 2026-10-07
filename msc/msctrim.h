/* HALO: SOL 144, static aeroelastic trim -- what the front end hands the
 * AETRIM module (SOL144.md has the theory and the design).
 *
 * NASTRAN-95 has no static aeroelastic cards, so the TRIM, AEROS, AESTAT,
 * AESURF, AELIST, AELINK and DIVERG cards, PARAM AUNITS and the
 * correction matrices (DMI / DMIJ / DMIK W2GJ, FA2J, WKK) never reach its
 * input file processor: msctrim.c takes them out of the bulk data while
 * the deck is translated and keeps them here, and the AETRIM module
 * (mis/aetrim.f, the algebra in mscaest.c) reads them from here. Both run
 * in the one process, the translation first.
 */
#ifndef MSCTRIM_H
#define MSCTRIM_H

#include "msc.h"

#define AET_LABLEN 9          /* an 8-character label and its NUL       */

typedef struct { int id; char label[AET_LABLEN]; } aet_stat;      /* AESTAT */

typedef struct {                                                  /* AESURF */
    int    id;
    char   label[AET_LABLEN];
    int    cid[2], alid[2];   /* hinge frame and box list, two components */
    double eff;               /* effectiveness, scales the downwash       */
    double crefc, crefs;      /* hinge moment reference chord and area    */
} aet_surf;

typedef struct { int sid; int n; int *ids; } aet_list;            /* AELIST */

typedef struct {                                                  /* AELINK */
    int    id;                /* the TRIM set it applies to; 0 = ALWAYS   */
    char   dep[AET_LABLEN];
    int    n;
    char (*ind)[AET_LABLEN];
    double *c;
} aet_link;

typedef struct {                                                  /* TRIM   */
    int    sid;
    double mach, q, aeqr;
    int    n;
    char (*lab)[AET_LABLEN];
    double *ux;
} aet_trimc;

/* a direct matrix input by a restricted name (W2GJ, FA2J, WKK): MSC's
 * DMI (rows and columns are positions in the j or k set, 1-based, MSC's
 * order: the boxes by ascending id, the k set T3 then R5 of each box),
 * or DMIJ / DMIK (rows and columns are aerodynamic box ids and their
 * components: 3 the normalwash or force, 5 the pitching moment).
 * SOL144.md, "The correction matrices", has what each one does.        */
typedef struct { int ri, rc, ci, cc; double v; } aet_mel;

typedef struct {
    int      kind;            /* 0 none, 1 DMI, 2 DMIJ / DMIK             */
    char     name[AET_LABLEN];
    int      form, m, n;      /* the header: FORM (IFO), rows, columns    */
    int      ne, cap;
    aet_mel *e;
} aet_dmi;

typedef struct {                                                  /* DIVERG */
    int     sid, nroot, nm;
    double *m;                /* the Mach numbers                         */
} aet_div;

typedef struct {
    int     active;           /* a SOL 144 deck is being translated/run   */
    int     have_aeros;
    int     acsid, rcsid;
    double  refc, refb, refs;
    int     symxz, symxy;
    double  aunits;           /* PARAM AUNITS: accelerations / AUNITS     */
    double  kred;             /* the reduced frequency AMG is run at      */
    aet_stat  *stat;  int nstat;
    aet_surf  *surf;  int nsurf;
    aet_list  *list;  int nlist;
    aet_link  *link;  int nlink;
    aet_trimc *trim;  int ntrim;
    /* the subcases in case control order, the TRIM set and the DIVERG
     * set of each (-1: none)                                           */
    int    *sub_id, *sub_trim, *sub_div; int nsub;
    double *mach;     int nmach;    /* the distinct Machs of the TRIM and
                                     * DIVERG sets the subcases use     */
    int     want_apres, want_aerof;   /* APRES / AEROF in the case control */
    int     aelink_sign;      /* +1: u_D + sum C u_I = 0 (MSC's QRG); -1:
                               * u_D = sum C u_I (N95_AELINK_SIGN=-1)      */
    /* the correction matrices (MSC eqs. 2-104..2-108): the downwash of
     * incidence, camber and twist (W2GJ), the experimental pressure
     * coefficients (FA2J), the box force and moment weights (WKK). The
     * DMI spelling wins over the DMIJ / DMIK one, as in MSC (QRG DMIJ
     * and DMIK, remark 1)                                               */
    aet_dmi w2gj, fa2j, wkk;          /* DMI                              */
    aet_dmi w2gj_j, fa2j_j, wkk_k;    /* DMIJ, DMIJ, DMIK                 */
    aet_dmi wtf, wtf_k;       /* WTFACT (DMI, DMIK): the weights when the
                               * deck has no WKK (the guide: "WKK (or
                               * WTFACT)")                               */
    aet_div *div;  int ndiv;  /* DIVERG cards                             */
    int     have_suport;      /* SUPORT / SUPORT1 cards in the bulk data  */
} aet_model;

extern aet_model aet_g;

/* front end (msctrim.c) */
void aet_reset(void);
int  aet_bulk_card(const msc_card *c);      /* 1 when the card was taken */
void aet_case_scan(msc_deck *d);            /* SUBCASE / TRIM / DIVERG   */
int  aet_check(void);                       /* fatals when not runnable  */
void aet_emit(msc_list *out);               /* AERO, MKAERO1, a dummy EIGR */
void aet_write_dmap(FILE *fp);              /* the APP DMAP program      */
const aet_dmi *aet_matrix(int which);       /* 0 W2GJ, 1 FA2J, 2 WKK: the
                                             * spelling in force, or NULL */

#endif /* MSCTRIM_H */
