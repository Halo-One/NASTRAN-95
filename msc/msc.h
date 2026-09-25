/* HALO: the MSC dialect front end -- shared types.
 *
 * NASTRAN-95 reads the 1970s COSMIC input: fixed eight-column fields, an
 * executive control deck of APP/SOL/TIME, and the element and property
 * cards of that decade. Every deck this repository generates is written
 * for MSC Nastran instead: free field with commas, nested INCLUDEs, SOL
 * 103, EIGRL, RBE2, CBUSH, PBARL. The two are close enough that a deck
 * can be rewritten from one into the other, and far enough apart that
 * doing it by hand is a day's work per model.
 *
 * So this front end reads the MSC deck, rewrites it, and hands the
 * result to the solver. It is deliberately a translation and not an
 * emulation: everything it does is visible in the translated deck it
 * leaves in the output directory, and anything it cannot translate is a
 * numbered message that says what the card was and what was done about
 * it, rather than a silently different model.
 *
 * Nothing here is NASA's. The whole directory is new in this fork.
 */
#ifndef MSC_H
#define MSC_H

#include <stddef.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* limits. Generous, checked, and reported when hit -- a deck that runs
 * into one gets a message naming the limit, never a truncated model.  */
#define MSC_MAXFLD   256   /* fields on one logical card (continuations) */
#define MSC_FLDLEN    17   /* one field: large field is 16 characters    */
#define MSC_LINELEN 1024   /* one input line                             */
#define MSC_PATHLEN 1024
#define MSC_MAXINC    16   /* INCLUDE nesting                            */

/* ------------------------------------------------------------------ */
/* one logical card: a name and its fields, continuations already
 * joined on. Field 1 of the card is fld[0]; the name is separate.
 * `line` and `file` are where it came from, for messages.            */
typedef struct {
    char   name[MSC_FLDLEN];
    char (*fld)[MSC_FLDLEN];
    int    nfld;
    int    cap;
    int    line;
    const char *file;
    int    dropped;          /* set by the translator: not written out  */
} msc_card;

/* a deck, split the way NASTRAN splits it */
typedef struct {
    char   **exec;      int nexec;   int execcap;   /* before CEND        */
    char   **cases;     int ncase;   int casecap;   /* CEND .. BEGIN BULK */
    msc_card *bulk;     int nbulk;   int bulkcap;
    char     title[73];
    int      sol;             /* SOL number as written, -1 if absent      */
    char     solname[32];     /* SOL name when written as a word          */
    int      diag[32];        int ndiag;
} msc_deck;

/* ------------------------------------------------------------------ */
/* messages. MSC numbers its diagnostics and so does this: the number
 * is the first thing anyone pastes into a search, and a message with
 * no number is a message nobody can look up. The catalogue is in
 * mscmsg.c; the numbers are this front end's own 9000 series, so that
 * they can never be confused with a message from the solver.         */
typedef enum { MSC_INFO = 0, MSC_WARN = 1, MSC_FATAL = 2 } msc_sev;

void msc_msg(msc_sev sev, int num, const char *fmt, ...);
void msc_msg_quiet(int on);                /* the messages held (a side translation) */
void msc_msg_at(msc_sev sev, int num, const msc_card *c, const char *fmt, ...);
int  msc_nfatal(void);
int  msc_nwarn(void);
void msc_msg_open(const char *path);   /* also copy messages to a file  */
void msc_msg_close(void);
void msc_msg_summary(void);
void msc_tally(const char *kind, const char *name);
void msc_tally_print(void);
int  msc_watchdog(double minutes, const char *note);   /* msc/mscwatch.c */

/* ------------------------------------------------------------------ */
/* reading (mscread.c) */
int  msc_read(const char *path, msc_deck *d);
msc_card *msc_bulk_add(msc_deck *d, const char *name);   /* a card appended */
void msc_free(msc_deck *d);

/* field access: never out of range, always NUL terminated, trimmed.
 * An absent field reads as "" so that callers can compare without
 * checking a count first.                                            */
const char *msc_f(const msc_card *c, int i);
double      msc_fd(const msc_card *c, int i, double dflt);
int         msc_fi(const msc_card *c, int i, int dflt);
int         msc_blank(const msc_card *c, int i);
void        msc_set(msc_card *c, int i, const char *v);
void        msc_setd(msc_card *c, int i, double v);
void        msc_seti(msc_card *c, int i, int v);

/* writing (mscwrite.c) */
int  msc_r8(double v, char out[9]);        /* a real in eight columns    */
void msc_write_card(FILE *fp, const msc_card *c);
void msc_write_name(FILE *fp, const char *name, const char *fields[], int n);

/* a small growable card list, used by the translator for what it emits */
typedef struct { msc_card *c; int n; int cap; } msc_list;
void msc_list_init(msc_list *l);
msc_card *msc_list_add(msc_list *l, const char *name);
void msc_list_free(msc_list *l);

/* ------------------------------------------------------------------ */
/* translation (mscxlat.c) */
typedef struct {
    int  sol;              /* MSC solution number seen                  */
    int  rf;               /* COSMIC rigid format chosen                */
    char app[16];          /* DISPLACEMENT / HEAT / AERO                */
    int  nmodes;           /* modes asked for, when the deck says       */
    double shift;          /* eigenvalue shift used (Hz)                */
    int  dropped;          /* cards dropped                             */
    int  translated;       /* cards rewritten                           */
    int  autospc;          /* dofs constrained because nothing held them*/
    int  renumbered;       /* ids moved below the 24-bit limit          */
    int  conm2;            /* CONM2 cards seen                          */
    int  conm2_norot;      /* ... of which carry no rotary inertia      */
} msc_stats;

int msc_translate_deck(msc_deck *d, const char *outpath, msc_stats *st);

/* checkpoint and restart (mscxlat.c): a checkpointed run writes CHKPNT
 * YES,DISK; a restart writes the RESTART dictionary of the modes run
 * and only the bulk cards that run did not have                       */
void msc_chkpnt_set(int on);
extern char msc_tag_letter;   /* continuation tags' letter, 'R' on a restart */
int  msc_restart_set(const char *modes_deck, const char *optp);
const char *msc_restart_print(void);   /* the modes run's print, on a restart */
const char *msc_restart_optp(void);

int  msc_sol200(const char *deck, const char *outdir, const char *stem);

/* the operating system calls the two child-process drivers share
 * (mscmain.c): this executable's own path, a path made absolute, and
 * make / enter a directory - one spelling per OS behind these          */
void msc_self_path(char *buf, size_t n);
void msc_abs_path(char *buf, size_t n, const char *in);
int  msc_mkdir(const char *path);
int  msc_chdir(const char *path);

/* the SOL 145 driver (mscflut.c): one child run per subcase, side by side */
int  msc_flutter_subcases(const msc_deck *d);
int  msc_sol145(const char *deck, const char *outdir, const char *stem);

/* the whole job: read `in`, translate, write the COSMIC deck to `out`.
 * Returns 0 when the deck can be run.                                 */
int msc_translate(const char *in, const char *out, msc_stats *st);

/* ------------------------------------------------------------------ */
/* utilities shared across the front end (mscutil.c) */
void  msc_upper(char *s);
void  msc_trim(char *s);
int   msc_isblank_line(const char *s);
int   msc_streq(const char *a, const char *b);
char *msc_strdup(const char *s);
void *msc_alloc(size_t n);
void *msc_realloc(void *p, size_t n);
int   msc_isnum(const char *s);

/* ------------------------------------------------------------------ */
/* integer-keyed model tables (mscmap.c) */
#define MSC_MAPVAL 8
typedef struct { int key; int used; int iv; double dv[MSC_MAPVAL]; } msc_slot;
typedef struct { msc_slot *s; int cap; int n; } msc_map;
void      msc_map_init(msc_map *m, int hint);
void      msc_map_free(msc_map *m);
msc_slot *msc_map_slot(msc_map *m, int key, int make);
int       msc_map_get(msc_map *m, int key, int dflt);
void      msc_map_put(msc_map *m, int key, int val);
int       msc_map_has(msc_map *m, int key);
void      msc_map_add(msc_map *m, int key, int inc);
double   *msc_map_vec(msc_map *m, int key, int make);
int       msc_map_count(msc_map *m);
int       msc_map_next(msc_map *m, int *iter, int *key, int *val);

/* ------------------------------------------------------------------ */
/* executive and case control (mscexec.c) */
int  msc_sol_lookup(int sol, const char *name, const char **app, int *rf,
                    const char **what);
void msc_case_write(FILE *fp, msc_deck *d, int *spc_sel, int *method_sel,
                    int suppress_spc, int suppress_title);
int  msc_case_has_output(msc_deck *d);
int  msc_case_find(msc_deck *d, const char *want, char *val_out);

/* the solver's own fatal messages, explained (mscdiag.c) */
int  msc_diag(const char *prt);

/* the print file, rewritten into MSC's layout (mscf06.c) */
int  msc_f06(const char *path);
void msc_f06_modes(int n);
void msc_f06_remap(int new_id, int old_id);
int  msc_f06_get_modes(void);

/* OUTPUT4 matrices (mscop4.c) */
int  msc_op4_scan(msc_deck *d);
int  msc_op4_count(void);
void msc_op4_alter(FILE *fp, int rf);
int  msc_op4_finish(void);

#endif /* MSC_H */
