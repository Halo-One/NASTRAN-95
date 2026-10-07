/* HALO: reading an MSC Nastran deck.
 *
 * Three input formats have to be read, because this repository's decks
 * use two of them in one file and NASA's demos use the third:
 *
 *   small field   eight columns a field, name in 1-8, data in 9-72,
 *                 continuation marker in 73-80
 *   large field   name ends in '*', sixteen columns a field, four data
 *                 fields a line
 *   free field    commas
 *
 * A logical card is its first line plus every continuation line. The
 * fields are held flat: field 1 is the first data field (what the Quick
 * Reference Guide calls field 2, the one after the card name), and the
 * continuation markers are not stored at all -- they are an artefact of
 * the line layout, and nothing downstream wants them.
 *
 * The one trap worth naming: a line contributes a FIXED number of field
 * slots, eight in small and free field, four in large field, whether or
 * not they were written. A free-field line with six fields on it still
 * advances the count by eight. Getting this wrong puts the next line's
 * first field two slots early -- on a PBAR that silently moves I12 into
 * the K2 column, and the model runs, and the answers are wrong.
 */
#include "msc.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* the deck being built, plus where we are in it                       */

typedef enum { SEC_EXEC = 0, SEC_CASE = 1, SEC_BULK = 2 } msc_section;

typedef struct {
    msc_deck   *d;
    msc_section sec;
    msc_card   *cur;          /* card a continuation line attaches to   */
    char        curtag[MSC_FLDLEN];
    int         depth;
    const char *files[MSC_MAXINC];
} msc_rdr;

static void push_line(char ***arr, int *n, int *cap, const char *s)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *arr = (char **) msc_realloc(*arr, (size_t) *cap * sizeof(char *));
    }
    (*arr)[(*n)++] = msc_strdup(s);
}

static msc_card *push_card(msc_deck *d, const char *name, int line,
                           const char *file);

/* a card added to a deck read already (the drivers edit a deck before
 * translating it); no line or file to name in messages               */
msc_card *msc_bulk_add(msc_deck *d, const char *name)
{
    return push_card(d, name, 0, NULL);
}

static msc_card *push_card(msc_deck *d, const char *name, int line,
                           const char *file)
{
    msc_card *c;
    if (d->nbulk == d->bulkcap) {
        int i;
        int old = d->bulkcap;
        d->bulkcap = d->bulkcap ? d->bulkcap * 2 : 256;
        d->bulk = (msc_card *) msc_realloc(d->bulk,
                      (size_t) d->bulkcap * sizeof(msc_card));
        for (i = old; i < d->bulkcap; i++) memset(&d->bulk[i], 0, sizeof(msc_card));
    }
    c = &d->bulk[d->nbulk++];
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, MSC_FLDLEN - 1);
    c->line = line;
    c->file = file;
    return c;
}

/* append one field slot to a card */
static void card_add(msc_card *c, const char *v)
{
    if (c->nfld == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->fld = (char (*)[MSC_FLDLEN]) msc_realloc(c->fld,
                     (size_t) c->cap * MSC_FLDLEN);
    }
    if (c->nfld >= MSC_MAXFLD) return;      /* checked by the caller */
    strncpy(c->fld[c->nfld], v, MSC_FLDLEN - 1);
    c->fld[c->nfld][MSC_FLDLEN - 1] = '\0';
    c->nfld++;
}

/* ------------------------------------------------------------------ */
/* field access                                                        */

static const char *s_empty = "";

const char *msc_f(const msc_card *c, int i)
{
    if (!c || i < 1 || i > c->nfld) return s_empty;
    return c->fld[i - 1];
}

int msc_blank(const msc_card *c, int i)
{
    return msc_f(c, i)[0] == '\0';
}

/* HALO: a NASTRAN statement's system cells that change the solution. Of
 * them only SYSTEM(270) (QUARTICDLM), the doublet-lattice kernel, is
 * honoured: 1 is the quartic kernel of Rodden, Taylor and McIntosh 1998,
 * 0 the parabolic one NASA's code has (mis/dlmq.f). It reaches the solver
 * as N95_DLM_QUARTIC in the environment - inherited by the SOL 145
 * driver's children - unless the user has set that variable, which then
 * wins. Every other NASTRAN statement is dropped, as it always was. */
static void nastran_statement(const char *up)
{
    const char *p = strstr(up, "SYSTEM(270)");
    size_t skip = 11;
    int v;
    const char *had;
    char value[2];
    if (p == NULL) { p = strstr(up, "QUARTICDLM"); skip = 10; }
    if (p == NULL) return;
    p += skip;
    while (*p == ' ' || *p == '\t' || *p == '=') p++;
    if (!isdigit((unsigned char) *p)) return;
    v = atoi(p) != 0;
    had = getenv("N95_DLM_QUARTIC");
    if (had != NULL && had[0] != '\0') {
        msc_msg(MSC_INFO, 9470, "NASTRAN SYSTEM(270)=%d in the deck; N95_DLM_QUARTIC=%s in the "
                "environment wins: the %s doublet-lattice kernel.", v, had,
                had[0] == '1' ? "quartic" : "parabolic");
        return;
    }
    value[0] = v ? '1' : '0';
    value[1] = '\0';
#ifdef _WIN32
    {
        char buf[40];
        snprintf(buf, sizeof buf, "N95_DLM_QUARTIC=%s", value);
        _putenv(buf);
    }
#else
    setenv("N95_DLM_QUARTIC", value, 1);
#endif
    msc_msg(MSC_INFO, 9470, "NASTRAN SYSTEM(270)=%d: the %s doublet-lattice kernel%s.", v,
            v ? "quartic" : "parabolic",
            v ? " (Rodden, Taylor and McIntosh 1998, with Desmarais' kernel integrals)" : "");
}

/* NASTRAN reals: 1.-3 and 1.5+8 are 1.0e-3 and 1.5e8. strtod does not
 * read those, so the exponent is put back before it is called.        */
double msc_fd(const msc_card *c, int i, double dflt)
{
    const char *s = msc_f(c, i);
    char buf[MSC_FLDLEN * 2];
    char *o = buf;
    int   k, seen_digit = 0;
    if (!*s) return dflt;
    for (k = 0; s[k] && (size_t)(o - buf) < sizeof(buf) - 3; k++) {
        char ch = s[k];
        if ((ch == '+' || ch == '-') && k > 0 && seen_digit &&
            !(s[k-1] == 'E' || s[k-1] == 'e' || s[k-1] == 'D' ||
              s[k-1] == 'd' || s[k-1] == '+' || s[k-1] == '-')) {
            *o++ = 'E';
        }
        if (ch == 'D' || ch == 'd') ch = 'E';
        if (isdigit((unsigned char) ch)) seen_digit = 1;
        *o++ = ch;
    }
    *o = '\0';
    return strtod(buf, NULL);
}

int msc_fi(const msc_card *c, int i, int dflt)
{
    const char *s = msc_f(c, i);
    if (!*s) return dflt;
    return (int) strtol(s, NULL, 10);
}

void msc_set(msc_card *c, int i, const char *v)
{
    while (c->nfld < i) card_add(c, "");
    strncpy(c->fld[i - 1], v, MSC_FLDLEN - 1);
    c->fld[i - 1][MSC_FLDLEN - 1] = '\0';
}

void msc_seti(msc_card *c, int i, int v)
{
    char b[32];
    sprintf(b, "%d", v);
    msc_set(c, i, b);
}

void msc_setd(msc_card *c, int i, double v)
{
    char b[9];
    msc_r8(v, b);
    msc_trim(b);
    msc_set(c, i, b);
}

/* ------------------------------------------------------------------ */
/* line handling                                                       */

/* strip a trailing comment. A '$' starts one anywhere on the line. */
static void strip_comment(char *s)
{
    char *p = strchr(s, '$');
    if (p) *p = '\0';
}

static void rstrip(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) s[--n] = '\0';
}

/* copy columns [a,b) of `line` into `out`, trimmed */
static void cols(const char *line, int a, int b, char *out)
{
    int n = (int) strlen(line);
    int i, k = 0;
    for (i = a; i < b && i < n; i++) out[k++] = line[i];
    out[k] = '\0';
    msc_trim(out);
}

/* Split a free-field line into at most 10 tokens. Returns the count. */
static int split_free(const char *line, char tok[10][MSC_FLDLEN])
{
    int n = 0, k = 0;
    const char *p = line;
    memset(tok, 0, 10 * MSC_FLDLEN);
    while (*p && n < 10) {
        if (*p == ',') { tok[n][k] = '\0'; msc_trim(tok[n]); n++; k = 0; p++; continue; }
        if (k < MSC_FLDLEN - 1) tok[n][k++] = *p;
        p++;
    }
    tok[n][k] = '\0';
    msc_trim(tok[n]);
    if (k > 0 || n > 0) n++;
    return n;
}

/* Is this bulk-data line a continuation of the card before it?
 * In every format the test is the same: the first field is blank, or
 * it starts with '+' or '*'.                                         */
static int is_continuation(const char *line, int freefield)
{
    char first[MSC_FLDLEN];
    if (freefield) {
        char tok[10][MSC_FLDLEN];
        split_free(line, tok);
        strncpy(first, tok[0], MSC_FLDLEN - 1);
        first[MSC_FLDLEN - 1] = '\0';
    } else {
        cols(line, 0, 8, first);
    }
    if (first[0] == '\0') return 1;
    if (first[0] == '+' || first[0] == '*') return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* the bulk data reader                                                */

static void read_bulk_line(msc_rdr *r, const char *line, int lineno,
                           const char *file)
{
    char tok[10][MSC_FLDLEN];
    char name[MSC_FLDLEN];
    int  freefield = strchr(line, ',') != NULL;
    int  large, i, cont, nslot;

    cont = is_continuation(line, freefield);

    if (freefield) {
        int n = split_free(line, tok);
        /* A line may stop short of ten fields and still end in its
         * continuation marker: "PBAR,1,2,3.,4.,5.,6.,0.,+". The marker
         * is field 10 whatever position it lands in, so a last token
         * that starts with + or * and is not a number (+0.8 is a
         * number) is the marker and not data.                        */
        if (n >= 2 && n <= 9 && (tok[n-1][0] == '+' || tok[n-1][0] == '*') &&
            !msc_isnum(tok[n-1])) {
            tok[n-1][0] = '\0';
            n--;
        }
        strncpy(name, tok[0], MSC_FLDLEN - 1);
        name[MSC_FLDLEN - 1] = '\0';
        large = (strchr(name, '*') != NULL);
        nslot = large ? 4 : 8;
        if (!cont) {
            char *star = strchr(name, '*');
            if (star) *star = '\0';
            msc_trim(name);
            msc_upper(name);
            r->cur = push_card(r->d, name, lineno, file);
        }
        if (!r->cur) {
            msc_msg(MSC_WARN, 9001,
                    "%s line %d: a continuation with no card before it, ignored",
                    file, lineno);
            return;
        }
        for (i = 1; i <= nslot; i++) card_add(r->cur, tok[i]);
        return;
    }

    /* fixed field */
    cols(line, 0, 8, name);
    large = (strchr(name, '*') != NULL);
    nslot = large ? 4 : 8;
    if (!cont) {
        char *star = strchr(name, '*');
        if (star) *star = '\0';
        msc_trim(name);
        msc_upper(name);
        r->cur = push_card(r->d, name, lineno, file);
    }
    if (!r->cur) {
        msc_msg(MSC_WARN, 9001,
                "%s line %d: a continuation with no card before it, ignored",
                file, lineno);
        return;
    }
    for (i = 0; i < nslot; i++) {
        char f[MSC_FLDLEN];
        int  a = large ? 8 + 16 * i : 8 + 8 * i;
        int  b = large ? a + 16      : a + 8;
        cols(line, a, b, f);
        card_add(r->cur, f);
    }
}

/* ------------------------------------------------------------------ */
/* INCLUDE                                                             */

static int include_path(const char *line, const char *parent, char *out)
{
    const char *p = line;
    const char *q;
    char        raw[MSC_PATHLEN];
    size_t      n;

    while (*p && *p != '\'' && *p != '"') p++;
    if (!*p) {
        /* INCLUDE without quotes: the rest of the line is the name */
        p = strstr(line, "INCLUDE");
        if (!p) return 1;
        p += 7;
        while (*p == ' ' || *p == '\t') p++;
        strncpy(raw, p, sizeof(raw) - 1);
        raw[sizeof(raw) - 1] = '\0';
    } else {
        char quote = *p++;
        q = strchr(p, quote);
        if (!q) return 1;
        n = (size_t) (q - p);
        if (n >= sizeof(raw)) n = sizeof(raw) - 1;
        memcpy(raw, p, n);
        raw[n] = '\0';
    }
    msc_trim(raw);
    if (!raw[0]) return 1;

    /* absolute, or relative to the including file's directory */
    if (raw[0] == '/' || raw[0] == '\\' ||
        (raw[0] && raw[1] == ':')) {
        strncpy(out, raw, MSC_PATHLEN - 1);
        out[MSC_PATHLEN - 1] = '\0';
        return 0;
    }
    {
        const char *slash = strrchr(parent, '/');
        const char *bslash = strrchr(parent, '\\');
        const char *cut = slash > bslash ? slash : bslash;
        if (cut) {
            size_t k = (size_t) (cut - parent) + 1;
            if (k >= MSC_PATHLEN) k = MSC_PATHLEN - 1;
            memcpy(out, parent, k);
            out[k] = '\0';
            strncat(out, raw, MSC_PATHLEN - strlen(out) - 1);
        } else {
            strncpy(out, raw, MSC_PATHLEN - 1);
            out[MSC_PATHLEN - 1] = '\0';
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static int read_file(msc_rdr *r, const char *path);

static int handle_line(msc_rdr *r, char *line, int lineno, const char *file)
{
    char up[MSC_LINELEN];
    char work[MSC_LINELEN];

    strncpy(work, line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';
    strip_comment(work);
    rstrip(work);
    if (msc_isblank_line(work)) return 0;

    strncpy(up, work, sizeof(up) - 1);
    up[sizeof(up) - 1] = '\0';
    msc_upper(up);
    msc_trim(up);

    if (strncmp(up, "INCLUDE", 7) == 0) {
        char inc[MSC_PATHLEN];
        if (include_path(work, file, inc)) {
            msc_msg(MSC_FATAL, 9010, "%s line %d: cannot read the file name "
                    "out of this INCLUDE: %s", file, lineno, work);
            return 1;
        }
        if (r->depth >= MSC_MAXINC) {
            msc_msg(MSC_FATAL, 9011, "INCLUDE nested more than %d deep at %s "
                    "line %d", MSC_MAXINC, file, lineno);
            return 1;
        }
        return read_file(r, inc);
    }

    if (r->sec == SEC_EXEC) {
        if (strncmp(up, "CEND", 4) == 0) { r->sec = SEC_CASE; return 0; }
        if (strncmp(up, "NASTRAN", 7) == 0) nastran_statement(up);
        if (strncmp(up, "SOL", 3) == 0 && (up[3] == ' ' || up[3] == '\t')) {
            const char *p = up + 3;
            while (*p == ' ' || *p == '\t' || *p == '=') p++;
            if (isdigit((unsigned char) *p)) r->d->sol = atoi(p);
            else {
                strncpy(r->d->solname, p, sizeof(r->d->solname) - 1);
                r->d->solname[sizeof(r->d->solname) - 1] = '\0';
            }
        }
        push_line(&r->d->exec, &r->d->nexec, &r->d->execcap, work);
        return 0;
    }

    if (r->sec == SEC_CASE) {
        if (strncmp(up, "BEGIN BULK", 10) == 0 ||
            strncmp(up, "BEGINBULK", 9) == 0) { r->sec = SEC_BULK; return 0; }
        if (strncmp(up, "TITLE", 5) == 0 && !r->d->title[0]) {
            const char *p = strchr(work, '=');
            if (p) {
                strncpy(r->d->title, p + 1, sizeof(r->d->title) - 1);
                r->d->title[sizeof(r->d->title) - 1] = '\0';
                msc_trim(r->d->title);
            }
        }
        push_line(&r->d->cases, &r->d->ncase, &r->d->casecap, work);
        return 0;
    }

    /* bulk data */
    if (strncmp(up, "ENDDATA", 7) == 0) return 0;
    read_bulk_line(r, work, lineno, file);
    return 0;
}

static int read_file(msc_rdr *r, const char *path)
{
    FILE *fp = fopen(path, "r");
    char  line[MSC_LINELEN];
    int   lineno = 0;
    const char *held;

    if (!fp) {
        msc_msg(MSC_FATAL, 9012, "cannot open %s", path);
        return 1;
    }
    held = msc_strdup(path);
    r->files[r->depth] = held;
    r->depth++;
    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        /* An INCLUDE whose quoted name runs on to the next line. MSC
         * allows it and this repository's writers use it, because a
         * path plus the keyword is longer than the 72 columns the
         * file management section is read in:
         *
         *   INCLUDE'../include_files/nastran/monarch_gvt/
         *   full_aircraft.bdf'
         *
         * The pieces join with nothing between them.                 */
        {
            char up[MSC_LINELEN];
            strncpy(up, line, sizeof(up) - 1);
            up[sizeof(up) - 1] = '\0';
            msc_trim(up);
            msc_upper(up);
            if (strncmp(up, "INCLUDE", 7) == 0) {
                char *q = strpbrk(line, "'\"");
                while (q && !strchr(q + 1, *q)) {
                    char more[MSC_LINELEN];
                    if (!fgets(more, sizeof(more), fp)) break;
                    lineno++;
                    rstrip(line);
                    msc_trim(more);
                    if (strlen(line) + strlen(more) >= sizeof(line) - 1) break;
                    strcat(line, more);
                    q = strpbrk(line, "'\"");
                }
            }
        }
        if (handle_line(r, line, lineno, held)) { fclose(fp); return 1; }
    }
    fclose(fp);
    r->depth--;
    return 0;
}

int msc_read(const char *path, msc_deck *d)
{
    msc_rdr r;
    memset(&r, 0, sizeof(r));
    memset(d, 0, sizeof(*d));
    d->sol = -1;
    r.d = d;
    r.sec = SEC_EXEC;
    if (read_file(&r, path)) return 1;
    return 0;
}

void msc_free(msc_deck *d)
{
    int i;
    for (i = 0; i < d->nexec; i++) free(d->exec[i]);
    for (i = 0; i < d->ncase; i++) free(d->cases[i]);
    free(d->exec);
    free(d->cases);
    for (i = 0; i < d->nbulk; i++) free(d->bulk[i].fld);
    free(d->bulk);
    memset(d, 0, sizeof(*d));
}

/* ------------------------------------------------------------------ */
/* a growable list of cards the translator emits                       */

void msc_list_init(msc_list *l) { memset(l, 0, sizeof(*l)); }

msc_card *msc_list_add(msc_list *l, const char *name)
{
    msc_card *c;
    if (l->n == l->cap) {
        int i, old = l->cap;
        l->cap = l->cap ? l->cap * 2 : 64;
        l->c = (msc_card *) msc_realloc(l->c, (size_t) l->cap * sizeof(msc_card));
        for (i = old; i < l->cap; i++) memset(&l->c[i], 0, sizeof(msc_card));
    }
    c = &l->c[l->n++];
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, MSC_FLDLEN - 1);
    return c;
}

void msc_list_free(msc_list *l)
{
    int i;
    for (i = 0; i < l->n; i++) free(l->c[i].fld);
    free(l->c);
    memset(l, 0, sizeof(*l));
}
