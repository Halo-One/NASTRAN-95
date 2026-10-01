/* HALO: SOL 144, static aeroelastic trim -- what the front end hands the
 * AETRIM module (SOL144.md has the theory and the design).
 *
 * NASTRAN-95 has no static aeroelastic cards, so the TRIM, AEROS, AESTAT,
 * AESURF, AELIST and AELINK cards and PARAM AUNITS never reach its input
 * file processor: msctrim.c takes them out of the bulk data while the
 * deck is translated and keeps them here, and the AETRIM module
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
    /* the subcases in case control order, and the TRIM set of each     */
    int    *sub_id, *sub_trim; int nsub;
    double *mach;     int nmach;    /* the distinct Machs of the TRIM sets */
    int     want_apres, want_aerof;   /* APRES / AEROF in the case control */
    int     aelink_sign;      /* +1: u_D + sum C u_I = 0 (MSC's QRG); -1:
                               * u_D = sum C u_I (N95_AELINK_SIGN=-1)      */
} aet_model;

extern aet_model aet_g;

/* front end (msctrim.c) */
void aet_reset(void);
int  aet_bulk_card(const msc_card *c);      /* 1 when the card was taken */
void aet_case_scan(msc_deck *d);            /* SUBCASE / TRIM pairs      */
int  aet_check(void);                       /* fatals when not runnable  */
void aet_emit(msc_list *out);               /* AERO, MKAERO1, a dummy EIGR */
void aet_write_dmap(FILE *fp);              /* the APP DMAP program      */

#endif /* MSCTRIM_H */
