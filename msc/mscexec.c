/* HALO: executive and case control, MSC to COSMIC.
 *
 * MSC says what to solve with one number: SOL 103. The 1970s input says
 * it with three cards -- APP DISPLACEMENT, SOL 3,0, TIME -- and calls
 * the solutions rigid formats. The mapping is nearly one to one because
 * MSC's numbering grew out of this one: 101 is rigid format 1, 103 is
 * 3, 105 is 5, and the hundreds digit is the superelement generation
 * that COSMIC never had.
 *
 * Case control is closer still: both read TITLE, SPC, METHOD, LOAD,
 * DISPLACEMENT, SET and SUBCASE, with the same meaning. What MSC has
 * added since 1995 -- RESVEC, ANALYSIS, WEIGHTCHECK, the parenthesised
 * output options -- is dropped with a message naming each one, because
 * a dropped output request is invisible in the results otherwise.
 */
#include "msc.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int         sol;        /* MSC solution number                       */
    const char *name;       /* and its word form                         */
    const char *app;        /* COSMIC APP card                           */
    int         rf;         /* COSMIC rigid format                       */
    const char *what;       /* for the message                           */
} sol_map;

static const sol_map sols[] = {
    { 101, "SESTATIC", "DISPLACEMENT",  1, "linear statics" },
    { 103, "SEMODES",  "DISPLACEMENT",  3, "normal modes" },
    { 105, "SEBUCKL",  "DISPLACEMENT",  5, "buckling" },
    { 107, "SEDCEIG",  "DISPLACEMENT",  7, "direct complex eigenvalues" },
    { 108, "SEDFREQ",  "DISPLACEMENT",  8, "direct frequency response" },
    { 109, "SEDTRAN",  "DISPLACEMENT",  9, "direct transient response" },
    { 110, "SEMCEIG",  "DISPLACEMENT", 10, "modal complex eigenvalues" },
    { 111, "SEMFREQ",  "DISPLACEMENT", 11, "modal frequency response" },
    { 112, "SEMTRAN",  "DISPLACEMENT", 12, "modal transient response" },
    { 145, "SEFLUTTR", "AERO",         10, "flutter" },
    { 146, "SEAERO",   "AERO",         11, "gust response" },
    {   1, NULL,       "DISPLACEMENT",  1, "linear statics" },
    {   3, NULL,       "DISPLACEMENT",  3, "normal modes" },
    {   0, NULL, NULL, 0, NULL }
};

/* MSC solutions with no 1970s equivalent, named so the message can say
 * what the deck asked for rather than "unknown solution".              */
static const sol_map no_map[] = {
    { 106, "NLSTATIC", NULL, 0, "nonlinear statics" },
    { 129, "NLTRAN",   NULL, 0, "nonlinear transient" },
    { 144, "SEStatic aeroelasticity", NULL, 0, "static aeroelastic trim" },
    { 153, NULL,       NULL, 0, "nonlinear heat transfer" },
    { 159, NULL,       NULL, 0, "transient heat transfer" },
    { 400, NULL,       NULL, 0, "nonlinear (Marc)" },
    { 401, NULL,       NULL, 0, "nonlinear (SOL 401)" },
    { 402, NULL,       NULL, 0, "nonlinear (SOL 402)" },
    {   0, NULL, NULL, 0, NULL }
};

int msc_sol_lookup(int sol, const char *name, const char **app, int *rf,
                   const char **what)
{
    int i;
    for (i = 0; sols[i].app; i++) {
        if ((sol > 0 && sols[i].sol == sol) ||
            (name && *name && sols[i].name && msc_streq(name, sols[i].name))) {
            *app  = sols[i].app;
            *rf   = sols[i].rf;
            *what = sols[i].what;
            return 0;
        }
    }
    for (i = 0; no_map[i].sol; i++) {
        if (sol > 0 && no_map[i].sol == sol) {
            *what = no_map[i].what;
            return 2;      /* known, and known to be impossible here     */
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* case control                                                        */

/* commands NASTRAN-95 reads with the same meaning */
static const char *case_keep[] = {
    "TITLE", "SUBTITLE", "SUBTITL", "LABEL", "ECHO", "MAXLINES", "LINES",
    "SPC", "MPC", "LOAD", "DEFORM", "TEMPERATURE", "TEMP",
    "METHOD", "CMETHOD", "FMETHOD", "SDAMPING", "FREQUENCY", "OFREQUENCY", "TSTEP",
    "DLOAD", "IC", "NONLINEAR", "GUST", "RANDOM",
    "XYPRINT", "XYPLOT", "XYPEAK", "XYPAPLOT", "XTITLE", "YTITLE",
    "XAXIS", "YAXIS", "XGRID", "YGRID", "TCURVE", "CURVELINESYMBOL",
    "DISPLACEMENT", "VELOCITY", "ACCELERATION", "SPCFORCES", "OLOAD",
    "STRESS", "ELFORCE", "FORCE", "SET", "SUBCASE", "SUBCOM",
    "SUBSEQ", "SYMMETRY", "REPCASE", "OUTPUT", "AXISYMMETRIC",
    "MODES", "SVECTOR", "THERMAL", "FLUX", "TRIM",
    NULL
};

/* commands that are MSC's and mean nothing to the 1970s solver. Each
 * is listed with what its absence costs, because "dropped" on its own
 * is not enough to judge whether the answer is still the right one.   */
typedef struct { const char *name; const char *cost; } case_drop;

static const case_drop case_drops[] = {
    { "RESVEC",      "MSC adds residual vectors to a modal basis by default; "
                     "NASTRAN-95 never did, so both codes now use the modes "
                     "alone" },
    { "ANALYSIS",    "the analysis type inside a SOL 200 design cycle; the "
                     "optimiser reads it separately" },
    { "WEIGHTCHECK", "a printed mass summary, not a result" },
    { "GROUNDCHECK", "a printed rigid-body check, not a result" },
    { "AUTOSPC",     "the front end applies the equivalent constraints and "
                     "reports them" },
    { "MEFFMASS",    "modal effective mass is printed, not solved for" },
    { "MODALSE",     "modal strain energy output" },
    { "SDISPLACEMENT", "the modal coordinates printed per output time; the "
                     "response itself is unaffected (UFM 614 in the solver)" },
    { "ESE",         "element strain energy output, which the 1970s solver "
                     "does not compute; the modes and frequencies are "
                     "unaffected" },
    { "ECHOON",      "input echo control" },
    { "ECHOOFF",     "input echo control" },
    { "DESOBJ",      "the design objective, read by the SOL 200 driver" },
    { "DESSUB",      "subcase design constraints, read by the driver" },
    { "DESGLB",      "global design constraints, read by the driver" },
    { "DSAPRT",      "sensitivity print control" },
    { "SEALL",       "superelement processing, which this solver has none of" },
    { "SUPER",       "superelement selection" },
    { "K2GG",        "direct matrix input to g-set stiffness" },
    { "M2GG",        "direct matrix input to g-set mass" },
    { "B2GG",        "direct matrix input to g-set damping" },
    { NULL, NULL }
};

/* split "DISPLACEMENT(SORT2,PHASE) = ALL" into name, options, value */
static void split_case(const char *line, char *name, char *opts, char *val)
{
    const char *p = line;
    char       *o = name;
    int         n = 0;

    name[0] = opts[0] = val[0] = '\0';
    while (*p && isspace((unsigned char) *p)) p++;
    while (*p && !isspace((unsigned char) *p) && *p != '(' && *p != '=') {
        if (n < MSC_FLDLEN * 2 - 1) { *o++ = (char) toupper((unsigned char) *p); n++; }
        p++;
    }
    *o = '\0';
    while (*p && isspace((unsigned char) *p)) p++;
    if (*p == '(') {
        const char *q = strchr(p, ')');
        size_t      k;
        p++;
        k = q ? (size_t) (q - p) : strlen(p);
        if (k > MSC_LINELEN - 1) k = MSC_LINELEN - 1;
        memcpy(opts, p, k);
        opts[k] = '\0';
        p = q ? q + 1 : p + k;
    }
    while (*p && (isspace((unsigned char) *p) || *p == '=')) p++;
    strncpy(val, p, MSC_LINELEN - 1);
    val[MSC_LINELEN - 1] = '\0';
    msc_trim(val);
}

/* whether a case control line ends with a comma, ignoring blanks */
static int ends_with_comma(const char *s)
{
    size_t k = strlen(s);
    while (k > 0 && (s[k - 1] == ' ' || s[k - 1] == '\t' ||
                     s[k - 1] == '\r' || s[k - 1] == '\n')) k--;
    return k > 0 && s[k - 1] == ',';
}

/* NASTRAN reads a case control command by as much of its name as is
 * unambiguous, and every deck in the wild uses the short forms: DISP,
 * SPCF, ELFO, SUBT. So a name matches when it is a prefix of a known
 * command and is at least four characters, which is the rule the
 * solver's own parser uses.                                           */
static const char *match_name(const char *n, const char **list)
{
    int i;
    size_t k = strlen(n);
    for (i = 0; list[i]; i++) if (msc_streq(n, list[i])) return list[i];
    if (k < 4) return NULL;
    for (i = 0; list[i]; i++)
        if (strncmp(n, list[i], k) == 0) return list[i];
    return NULL;
}

static int in_names(const char *n, const char **list)
{
    return match_name(n, list) != NULL;
}

/* Write the case control, translated. Returns the SPC set the deck
 * selects, or 0, through *spc_sel.                                    */
void msc_case_write(FILE *fp, msc_deck *d, int *spc_sel, int *method_sel,
                    int suppress_spc, int suppress_title)
{
    char name[MSC_FLDLEN * 2], opts[MSC_LINELEN], val[MSC_LINELEN];
    const char *full;
    int  i, j, cont = 0;

    *spc_sel = 0;
    *method_sel = 0;

    for (i = 0; i < d->ncase; i++) {
        /* the continuation lines of a SET (a trailing comma continues
         * it onto the next line, in both dialects) are ids, not
         * commands, and go through as written                       */
        if (cont) {
            const char *p = d->cases[i];
            while (*p == ' ' || *p == '\t') p++;
            fprintf(fp, "     %s\n", p);
            cont = ends_with_comma(p);
            continue;
        }
        split_case(d->cases[i], name, opts, val);
        if (!name[0]) continue;

        for (j = 0; case_drops[j].name; j++) {
            if (msc_streq(name, case_drops[j].name)) {
                msc_msg(MSC_INFO, 9101,
                        "case control %s is MSC's and has no NASTRAN-95 form;\n"
                        "dropped. What it did: %s.",
                        name, case_drops[j].cost);
                break;
            }
        }
        if (case_drops[j].name) continue;

        full = match_name(name, case_keep);
        if (!full) {
            msc_msg(MSC_WARN, 9102,
                "case control %s is not one this front end knows. It is left\n"
                "out of the translated deck; if it changes the answer rather\n"
                "than the printout, the result is not comparable.\n"
                "LINE  %s", name, d->cases[i]);
            continue;
        }

        /* from here the command is written under its full name, so a
         * deck that says DISP and one that says DISPLACEMENT produce
         * the same translated deck                                    */
        strcpy(name, full);
        if (msc_streq(name, "SPC"))    *spc_sel    = atoi(val);
        if (msc_streq(name, "METHOD")) *method_sel = atoi(val);
        if (msc_streq(name, "SPC")   && suppress_spc)   continue;
        if (msc_streq(name, "TITLE") && suppress_title) continue;

        /* the XY output requests are the 1970s solver's own language
         * (XYPRINT DISP PSDF / 12(T3)) and go through as written, as does
         * OUTPUT(XYPLOT) / OUTPUT(XYOUT) that opens them                 */
        if (strncmp(name, "XY", 2) == 0 || msc_streq(name, "XTITLE") ||
            msc_streq(name, "YTITLE") || msc_streq(name, "XAXIS") ||
            msc_streq(name, "YAXIS") || msc_streq(name, "XGRID") ||
            msc_streq(name, "YGRID") || msc_streq(name, "TCURVE") ||
            msc_streq(name, "CURVELINESYMBOL")) {
            const char *p = d->cases[i];
            while (*p == ' ' || *p == '\t') p++;
            fprintf(fp, "%s\n", p);
            continue;
        }
        if (msc_streq(name, "OUTPUT") && opts[0]) {
            char up[MSC_LINELEN];
            strncpy(up, opts, sizeof(up) - 1);
            up[sizeof(up) - 1] = '\0';
            msc_upper(up);
            if (strstr(up, "XY")) {
                fprintf(fp, "OUTPUT(%s)\n", up);
                continue;
            }
        }
        /* the parenthesised options are MSC's; SORT2 is the only one
         * the 1970s output has a form of, and it is spelled the same  */
        if (opts[0]) {
            char up[MSC_LINELEN];
            strncpy(up, opts, sizeof(up) - 1);
            up[sizeof(up) - 1] = '\0';
            msc_upper(up);
            if (strstr(up, "SORT2")) {
                fprintf(fp, "%s(SORT2) = %s\n", name, val);
                continue;
            }
            msc_msg(MSC_INFO, 9103,
                    "%s(%s): the options are MSC's and are dropped; the "
                    "request itself is kept.", name, opts);
        }
        /* SET n = list: the set number is part of the command, not a
         * value (SET = 103 = ... is UFM 614), and a list that runs on
         * continues on the lines that follow                        */
        if (msc_streq(name, "SET")) {
            fprintf(fp, "SET %s\n", val);
            cont = ends_with_comma(val);
            continue;
        }
        if (msc_streq(name, "SUBCASE") || msc_streq(name, "OUTPUT"))
            fprintf(fp, "%s %s\n", name, val);
        else if (val[0])
            fprintf(fp, "%s = %s\n", name, val);
        else
            fprintf(fp, "%s\n", name);
    }
}

/* Whether the case control already asks for something to be printed;
 * a deck that asks for nothing gets DISPLACEMENT = ALL, because a run
 * with no output is never what anyone meant.                          */
int msc_case_has_output(msc_deck *d)
{
    static const char *out[] = { "DISPLACEMENT", "VELOCITY", "ACCELERATION",
                                 "SPCFORCES", "OLOAD", "STRESS", "ELFORCE",
                                 "FORCE", "ESE", "SVECTOR", "THERMAL",
                                 "FLUX", NULL };
    char name[MSC_FLDLEN * 2], opts[MSC_LINELEN], val[MSC_LINELEN];
    int  i;
    for (i = 0; i < d->ncase; i++) {
        split_case(d->cases[i], name, opts, val);
        if (in_names(name, out)) return 1;
    }
    return 0;
}

int msc_case_find(msc_deck *d, const char *want, char *val_out)
{
    char name[MSC_FLDLEN * 2], opts[MSC_LINELEN], val[MSC_LINELEN];
    int  i;
    for (i = 0; i < d->ncase; i++) {
        split_case(d->cases[i], name, opts, val);
        if (msc_streq(name, want)) {
            if (val_out) strcpy(val_out, val);
            return 1;
        }
    }
    return 0;
}
