/* HALO: the print file, rewritten into the layout MSC prints.
 *
 * Both codes print the same tables -- they are the same tables, from
 * the same lineage -- but MSC has moved things by a few columns in
 * thirty years, and every reader in this repository reads by column:
 *
 *   read_nastran_eig    the eigenvector banner carries the frequency in
 *                       columns 20-32 and the mode number in the last
 *                       six characters of the line. NASTRAN-95 prints
 *                       the frequency on a line of its own above the
 *                       banner, and leaves columns 1-40 blank.
 *   read_nastran_eig    a displacement of exactly zero is "0.0" here
 *                       and "0.000000E+00" in MSC, and the reader's
 *                       regular expression wants the exponent: a mode
 *                       shape with one exact zero in it returns five
 *                       numbers for six components.
 *   read_khh            the generalized stiffness table is found by the
 *                       "(BEFORE AUGMENTATION OF RESIDUAL VECTORS)"
 *                       line MSC prints under the banner, and the rows
 *                       are counted from it: banner, sub-banner, two
 *                       header lines, then data with no blank between.
 *   read_nastran_mass   the weight generator's mass and c.g. sit at
 *                       fixed columns in MSC's table.
 *
 * So this rewrites the print file in place on the way out. It changes
 * layout and never a number: every value written is the value the
 * solver printed, and a line this does not recognise is copied through
 * untouched.
 */
#include "msc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define F6LINE 400

typedef struct {
    char **line;
    int    n;
    int    cap;
} f6buf;

static void put(f6buf *b, const char *s)
{
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4096;
        b->line = (char **) msc_realloc(b->line, (size_t) b->cap * sizeof(char *));
    }
    b->line[b->n++] = msc_strdup(s);
}

static void rstripn(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

static int has(const char *s, const char *what)
{
    return strstr(s, what) != NULL;
}

/* HALO: a restart run's print has no REAL EIGENVALUES table: READ and
 * the OFP after it are the modes run's, off the problem tape. read_khh
 * (and rank_flutter_modes through it) read the generalized stiffness
 * from that table in the flutter print, so the modes run's pages of it
 * - the consecutive pages from the first that carries the banner, in
 * the layout that print was already rewritten into - are spliced into
 * this print after each sorted echo, where a run of its own prints
 * them. Loaded once per process.                                     */
static f6buf eig_pages;
static int   eig_loaded = 0;

static void load_restart_eigenvalues(const char *modes_prt)
{
    FILE  *fp;
    f6buf  page;
    char   line[F6LINE * 2];
    int    k, seen = 0, stop = 0;

    eig_loaded = 1;
    if (!modes_prt) return;
    fp = fopen(modes_prt, "r");
    if (!fp) return;
    memset(&page, 0, sizeof(page));
    while (!stop) {
        int got = fgets(line, sizeof(line), fp) != NULL;
        if (got) rstripn(line);
        /* a page ends at the next page eject, or at the end of the file */
        if ((!got || line[0] == '1') && page.n > 0) {
            int is_table = 0;
            for (k = 0; k < page.n && !is_table; k++)
                if (has(page.line[k], "R E A L   E I G E N V A L U E S")) is_table = 1;
            if (is_table) {
                for (k = 0; k < page.n; k++) put(&eig_pages, page.line[k]);
                seen = 1;
            } else if (seen) {
                stop = 1;
            }
            for (k = 0; k < page.n; k++) free(page.line[k]);
            page.n = 0;
        }
        if (!got) break;
        put(&page, line);
    }
    for (k = 0; k < page.n; k++) free(page.line[k]);
    free(page.line);
    fclose(fp);
}

/* "0.0" and "-0.0" as MSC writes them. The column width is kept: the
 * token is replaced in place and the line stays aligned.              */
static void zeros_to_e(char *s)
{
    char out[F6LINE * 2];
    int  i = 0, o = 0;
    int  n = (int) strlen(s);

    while (i < n && o < (int) sizeof(out) - 16) {
        /* a zero token is  optional sign, '0', '.', '0', and then a
         * non-digit: anything else is a real number already          */
        int start = i;
        int neg = (s[i] == '-');
        int j = i + (neg ? 1 : 0);
        if ((i == 0 || s[i-1] == ' ') && s[j] == '0' && s[j+1] == '.' &&
            s[j+2] == '0' && !isdigit((unsigned char) s[j+3]) &&
            s[j+3] != 'E' && s[j+3] != '+' && s[j+3] != '-') {
            /* how much room there is before the next token */
            int k = j + 3, pad = 0;
            while (s[k] == ' ') { k++; pad++; }
            {
                const char *rep = neg ? "-0.000000E+00" : "0.000000E+00";
                int len = (int) strlen(rep);
                int room = (j + 3 - start) + pad;
                if (room >= len) {
                    memcpy(out + o, rep, (size_t) len);
                    o += len;
                    memset(out + o, ' ', (size_t) (room - len));
                    o += room - len;
                    i = k;
                    continue;
                }
            }
        }
        out[o++] = s[i++];
    }
    out[o] = '\0';
    strncpy(s, out, F6LINE * 2 - 1);
    s[F6LINE * 2 - 1] = '\0';
}

/* the cyclic frequency out of NASTRAN-95's "EIGENVALUE = ... (CYCLIC
 * FREQUENCY = 1.302902E+01 HZ)" line                                  */
static int cyclic_from(const char *s, char *out)
{
    const char *p = strstr(s, "CYCLIC FREQUENCY");
    const char *q;
    int         k = 0;
    if (!p) return 0;
    p = strchr(p, '=');
    if (!p) return 0;
    p++;
    while (*p == ' ') p++;
    q = p;
    while (*q && *q != ' ' && *q != 'H') q++;
    k = (int) (q - p);
    if (k <= 0 || k > 20) return 0;
    memcpy(out, p, (size_t) k);
    out[k] = '\0';
    return 1;
}

/* How many modes the deck asked for. FEER finds more roots than it is
 * asked for -- a reduced problem of twice the size, and every root of
 * it that passes its accuracy test is printed, so a request for 120
 * modes prints 250. MSC prints the number requested, and ZAERO counts
 * the modes it finds in the print file, so the extra ones are cut. The
 * count is set by the translation, which read the EIGRL.            */
static int f6_nmodes = 0;

void msc_f06_modes(int n) { f6_nmodes = n; }

/* Ids the translation renumbered (above 2^24-1, see mscxlat.c) are put
 * back in the print file: the echo and every POINT ID column say the
 * number the deck used, as MSC's print file would, so that a reader
 * matching this output against MSC's by grid id finds every grid.    */
#define F6_MAXREMAP 1024
static int f6_remap_from[F6_MAXREMAP], f6_remap_to[F6_MAXREMAP], f6_nremap = 0;

void msc_f06_remap(int new_id, int old_id)
{
    if (f6_nremap < F6_MAXREMAP) {
        f6_remap_from[f6_nremap] = new_id;
        f6_remap_to[f6_nremap]   = old_id;
        f6_nremap++;
    }
}

/* If the integer token at s[a..b) is a renumbered id, replace it with
 * the original, right-justified in the same columns. Returns 1 if the
 * line was changed.                                                   */
static int unmap_field(char *s, int a, int b)
{
    char tok[32];
    int  i, k = 0, id, len = (int) strlen(s);
    if (a >= len) return 0;
    if (b > len) b = len;
    for (i = a; i < b && k < 31; i++) tok[k++] = s[i];
    tok[k] = '\0';
    msc_trim(tok);
    if (!tok[0]) return 0;
    for (i = 0; tok[i]; i++) if (!isdigit((unsigned char) tok[i])) return 0;
    id = atoi(tok);
    for (i = 0; i < f6_nremap; i++) {
        if (f6_remap_from[i] == id) {
            char rep[32];
            int  n;
            sprintf(rep, "%d", f6_remap_to[i]);
            n = (int) strlen(rep);
            if (n > b - a) return 0;
            memset(s + a, ' ', (size_t) (b - a));
            memcpy(s + b - n, rep, (size_t) n);
            return 1;
        }
    }
    return 0;
}
int  msc_f06_get_modes(void) { return f6_nmodes; }

/* the mode number at the end of an eigenvector banner, or of a row of
 * the eigenvalue table (first token)                                  */
static int banner_mode(const char *s)
{
    const char *p = strstr(s, "N O .");
    if (!p) return 0;
    return atoi(p + 5);
}

static int table_mode(const char *s)
{
    /* a data row of the REAL EIGENVALUES table: seven numeric tokens,
     * the first two integers */
    int  m, o;
    char rest[F6LINE];
    if (sscanf(s, " %d %d %s", &m, &o, rest) != 3) return 0;
    if (!isdigit((unsigned char) rest[0]) && rest[0] != '-') return 0;
    return m;
}

/* the weight generator's three rows, from NASTRAN-95's layout into
 * MSC's, which read_nastran_mass reads by column:
 *   axis at column 35, mass in 47-59, c.g. in 65-77, 79-91, 93-105     */
static int gpwg_row(const char *in, char *out)
{
    char   axis[4], w[4][40];
    double v[4];
    int    k;
    if (sscanf(in, " %1s %39s %39s %39s %39s", axis, w[0], w[1], w[2], w[3]) != 5)
        return 0;
    if (axis[0] != 'X' && axis[0] != 'Y' && axis[0] != 'Z') return 0;
    for (k = 0; k < 4; k++) {
        char *d = strchr(w[k], 'D');
        if (d) *d = 'E';
        if (!msc_isnum(w[k])) return 0;
        v[k] = atof(w[k]);
    }
    sprintf(out, "%34s%c%11s%13.6E     %13.6E %13.6E %13.6E",
            "", axis[0], "", v[0], v[1], v[2], v[3]);
    return 1;
}

int msc_f06(const char *path)
{
    FILE  *fp;
    f6buf  b;
    char   line[F6LINE * 2];
    char   cyc[32];
    int    i, in_vector = 0, have_cyc = 0;
    int    skip_vector = 0, in_table = 0, gpwg_rows = 0;
    int    is_state = 0, is_rows = 0;      /* the I(S) block under the weights */
    int    after_enddata = 0;              /* the modes run's eigenvalue pages go here */
    const char *modes_prt = msc_restart_print();

    memset(&b, 0, sizeof(b));
    cyc[0] = '\0';

    fp = fopen(path, "r");
    if (!fp) return 1;
    while (fgets(line, sizeof(line), fp)) {
        rstripn(line);

        /* ---- a restart: the modes run's eigenvalue table ---------- */
        if (modes_prt) {
            if (after_enddata && line[0] == '1') {
                int k;
                if (!eig_loaded) load_restart_eigenvalues(modes_prt);
                for (k = 0; k < eig_pages.n; k++) put(&b, eig_pages.line[k]);
                after_enddata = 0;
            }
            {
                const char *t = line;
                while (*t == ' ') t++;
                if (strcmp(t, "ENDDATA") == 0) after_enddata = 1;
            }
        }

        if (cyclic_from(line, cyc)) have_cyc = 1;

        /* ---- the sorted echo banner ------------------------------- */
        /* one space more between the words here than in MSC's, and
         * read_nastran_grids finds the echo by the exact string       */
        if (has(line, "S O R T E D   B U L K    D A T A    E C H O")) {
            put(&b, "                                                  S O R T E D   B U L K   D A T A   E C H O");
            continue;
        }

        /* ---- the weight generator --------------------------------- */
        if (has(line, "MASS AXIS SYSTEM (S)")) {
            put(&b, "                          MASS AXIS SYSTEM (S)     MASS              X-C.G.        Y-C.G.        Z-C.G.");
            gpwg_rows = 3;
            continue;
        }
        if (gpwg_rows > 0) {
            char out[F6LINE];
            if (gpwg_row(line, out)) {
                put(&b, out);
                if (--gpwg_rows == 0) is_state = 1;
                continue;
            }
            gpwg_rows = 0;
        }
        /* ---- the inertia block under it ---------------------------- */
        /* MSC prints "I(S)" and three rows at fixed columns; NASTRAN-95
         * prints a blank, a longer label, a border of asterisks, the
         * rows and another border. read_nastran_mass counts rows from
         * the MASS AXIS line, so the block is reshaped to MSC's:
         *   I(S) at +4, the three rows at +5, +6, +7                  */
        if (is_state == 1) {
            if (msc_isblank_line(line)) continue;
            if (has(line, "I(S)")) {
                put(&b, "                                                                I(S)");
                is_state = 2; is_rows = 0;
                continue;
            }
            is_state = 0;
        } else if (is_state == 2) {
            char w[3][40];
            double v[3];
            int k;
            if (has(line, "***")) continue;                  /* the borders */
            if (sscanf(line, " * %39s %39s %39s", w[0], w[1], w[2]) == 3) {
                for (k = 0; k < 3; k++) {
                    char *dd = strchr(w[k], 'D');
                    if (dd) *dd = 'E';
                    v[k] = atof(w[k]);
                }
                {
                    char out[F6LINE];
                    sprintf(out, "%43s* %13.6E %13.6E %13.6E *", "", v[0], v[1], v[2]);
                    put(&b, out);
                }
                if (++is_rows == 3) is_state = 3;
                continue;
            }
            is_state = 0;
        } else if (is_state == 3) {
            is_state = 0;
            if (has(line, "***")) continue;
        }

        /* ---- modes past the number requested are not printed ------ */
        if (f6_nmodes > 0 && has(line, "R E A L   E I G E N V E C T O R   N O .")) {
            skip_vector = banner_mode(line) > f6_nmodes;
        }
        if (skip_vector) {
            /* the block runs to the page eject; a new page for a mode
             * that is wanted turns printing back on above. A message
             * or the end-of-job banner ends it too: those must never
             * be dropped, whatever block they interrupt.             */
            if (line[0] == '1' || has(line, "END OF JOB") ||
                has(line, "*** ")) skip_vector = 0;
            else continue;
        }
        if (in_table && f6_nmodes > 0) {
            int m = table_mode(line);
            if (m > f6_nmodes) continue;
            if (line[0] == '1') in_table = 0;
        }

        /* ---- the eigenvector banner ------------------------------- */
        if (has(line, "R E A L   E I G E N V E C T O R   N O .")) {
            char out[F6LINE * 2];
            const char *tail = strstr(line, "R E A L");
            char        freq[16];
            if (have_cyc) {
                /* MSC writes it as a thirteen character E field with
                 * two leading blanks: columns 20-32 of the line       */
                double v = atof(cyc);
                snprintf(freq, sizeof(freq), "%13.6E", v);
            } else {
                strcpy(freq, "             ");
            }
            snprintf(out, sizeof(out), "          CYCLES = %s         %s",
                     freq, tail);
            put(&b, out);
            in_vector = 1;
            continue;
        }
        if (in_vector) {
            /* a page eject or a blank page header ends the table */
            if (line[0] == '1' || has(line, "*** USER") ||
                has(line, "*** SYSTEM")) in_vector = 0;
            else if (isdigit((unsigned char) line[6]) || line[6] == ' ')
                zeros_to_e(line);
        }

        /* ---- the eigenvalue table --------------------------------- */
        if (has(line, "R E A L   E I G E N V A L U E S")) {
            char   banner[F6LINE * 2];
            char **held = NULL;
            int    nheld = 0, k;
            in_table = 1;
            strcpy(banner, line);
            /* read up to the first data row; keep what is neither a
             * header line nor blank (a message box) to write first   */
            while (fgets(line, sizeof(line), fp)) {
                rstripn(line);
                if (table_mode(line) > 0) break;
                if (has(line, "MODE") || has(line, "NO.") ||
                    msc_isblank_line(line)) continue;
                held = (char **) msc_realloc(held, (size_t) (nheld + 1) * sizeof(char *));
                held[nheld++] = msc_strdup(line);
            }
            for (k = 0; k < nheld; k++) { put(&b, held[k]); free(held[k]); }
            free(held);
            put(&b, banner);
            put(&b, "                                         (BEFORE AUGMENTATION OF RESIDUAL VECTORS)");
            put(&b, "   MODE    EXTRACTION      EIGENVALUE            RADIANS             CYCLES            GENERALIZED         GENERALIZED");
            put(&b, "    NO.       ORDER                                                                       MASS              STIFFNESS");
            /* the first data row, unless it is past the mode count */
            if (table_mode(line) > 0 &&
                !(f6_nmodes > 0 && table_mode(line) > f6_nmodes)) {
                zeros_to_e(line);
                put(&b, line);
            }
            continue;
        }

        if (f6_nremap) {
            if (in_vector) unmap_field(line, 0, 14);
            else if (has(line, "-        ")) unmap_field(line, 38, 46);
        }
        put(&b, line);
    }
    fclose(fp);

    fp = fopen(path, "w");
    if (!fp) return 1;
    for (i = 0; i < b.n; i++) {
        /* MSC never writes a zero-length line: a blank line is one
         * space. A reader that tests the first character of every
         * line (read_nastran_eig does, for the page eject) indexes
         * past the end of an empty one.                              */
        fprintf(fp, "%s\n", b.line[i][0] ? b.line[i] : " ");
        free(b.line[i]);
    }
    fclose(fp);
    free(b.line);
    /* the matrices the deck asked for, in MSC's layout */
    msc_op4_finish();
    return 0;
}
