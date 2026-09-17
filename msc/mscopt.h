/* HALO: the design model -- what SOL 200 adds to an analysis deck. */
#ifndef MSCOPT_H
#define MSCOPT_H

#include "msc.h"

#define OPT_MAXDV    256    /* design variables                          */
#define OPT_MAXREL   512    /* property and material relations           */
#define OPT_MAXRESP  512    /* responses                                 */
#define OPT_MAXCON   512    /* constraints                               */
#define OPT_MAXATT   256    /* values one DRESP1 expands to             */
#define OPT_MAXLINK   64

typedef struct {
    int    id;
    char   label[16];
    double xinit, xlb, xub, delxv;
    double x;               /* current value                             */
    int    linked;          /* dependent through a DLINK: not free       */
} opt_desvar;

/* DVPREL1 and DVMREL1 are the same relation against different tables */
typedef struct {
    int    id;
    char   type[16];        /* PROD, PBAR, PSHELL, MAT1 ...              */
    int    pid;             /* property or material id                   */
    char   pname[16];       /* A, I1, T, E, RHO ...                      */
    double pmin, pmax, c0;
    int    ndv;
    int    dvid[16];
    double coef[16];
    int    material;        /* DVMREL rather than DVPREL                 */
    double p0;              /* the value the deck's own card carried     */
} opt_rel;

typedef struct {
    int    id;
    int    ddvid;           /* the dependent design variable             */
    double c0, cmult;
    int    n;
    int    idv[16];
    double c[16];
} opt_link;

typedef struct {
    int    id;
    char   label[16];
    char   rtype[16];       /* WEIGHT, VOLUME, FREQ, EIGN, DISP, STRESS  */
    char   ptype[16];
    int    region;
    int    atta;
    char   attb[16];
    int    natt;
    int    atti[OPT_MAXATT];
    double value;           /* filled in each design cycle               */
    int    nval;            /* how many values it expanded to             */
    double vals[OPT_MAXATT];
    int    vid[OPT_MAXATT];    /* grid or element of each value           */
    int    vcomp[OPT_MAXATT];  /* component or item code of each value    */
    int    vsub[OPT_MAXATT];   /* subcase of each value                   */
} opt_resp;

typedef struct {
    int    dcid;
    int    rid;
    double lallow, uallow;
} opt_con;

typedef struct {
    int    desmax;
    double conv1, conv2, convdv, convpr, gmax;
    double delx, dxmin, delp, dpmin, delb;
    double ct, ctmin, gscal;
    int    aprcod, iprint, p1, p2;
    double trs;
    int    nstr;
} opt_param;

typedef struct {
    opt_desvar dv[OPT_MAXDV];      int ndv;
    opt_rel    rel[OPT_MAXREL];    int nrel;
    opt_link   link[OPT_MAXLINK];  int nlink;
    opt_resp   resp[OPT_MAXRESP];  int nresp;
    opt_con    con[OPT_MAXCON];    int ncon;
    opt_param  prm;
    int        objid;              /* DRESP id of the objective          */
    int        objmax;             /* DESOBJ(MAX)                        */
    int        dessub, desglb;     /* constraint set ids selected        */
    char       analysis[16];
} opt_model;

int  msc_opt_run(const char *deck, const char *outdir, const char *stem,
                 const char *exepath);

#endif /* MSCOPT_H */
