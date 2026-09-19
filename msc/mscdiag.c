/* HALO: what the solver's own fatal messages mean, and what to do.
 *
 * NASTRAN-95 says "USER FATAL MESSAGE 3097, SYMMETRIC DECOMPOSITION OF
 * DATA BLOCK KLL ABORTED BECAUSE THE FOLLOWING COLUMNS ARE SINGULAR"
 * and then a table of internal column numbers, and stops. That is a
 * complete diagnosis to someone who has read the 1972 Programmer's
 * Manual and nothing at all to anyone else. MSC's message catalogue
 * (util/analysis.txt in its installation, 8,655 entries) pairs every
 * number with a "User information" paragraph saying what the condition
 * usually is and what usually fixes it; this file does the same for the
 * messages this solver actually produces, in the same three-part shape
 * the front end uses everywhere: what happened, where, what to do.
 *
 * It runs on the way out, in both executables, whenever the print file
 * carries a fatal: the first fatal message is repeated on the terminal
 * (so that nobody has to open a 20 MB print file to learn that a grid
 * was missing) followed by the explanation for its number, when there
 * is one. Numbers without an entry are repeated without comment; the
 * table grows as messages are met.
 *
 * The numbers are NASA's, not MSC's: the two catalogues diverged in the
 * 1970s and share almost none.
 */
#include "msc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *num;        /* as printed, e.g. "3097" or "2101A"       */
    const char *what;
    const char *fix;
} diag_entry;

static const diag_entry catalogue[] = {
    { "300",
      "A field of a bulk data card could not be read as the type the card "
      "expects; the underlined field in the print file is the one.",
      "A real needs a decimal point (1.0 not 1), an exponent is written "
      "1.5E+3 or 1.5+3 with no space, an integer has no point. A field "
      "over eight characters wide was cut: NASTRAN reads eight columns." },
    { "305",
      "A bulk data card name is not one this solver knows.",
      "Check the spelling, and the list of cards NASTRAN-95 has (a 1995 "
      "code: no CQUAD4, CBUSH, RBE2, EIGRL, PBARL). nastran95ase "
      "translates the common MSC cards; nastran95 reads only the 1970s "
      "ones." },
    { "311",
      "Two cards of the same type carry the same identification number.",
      "Element, property, material, grid and set ids must each be unique "
      "within their kind. The sorted echo shows both cards." },
    { "316",
      "A card has illegal data: a field holds a value outside what the "
      "card allows (a negative id, a component list with a digit above "
      "6, a continuation that does not match its parent).",
      "Compare the card in the sorted echo with its description in the "
      "NASTRAN User's Manual; the field is not always the obvious one." },
    { "505",
      "The NASTRAN card (system parameters) was found somewhere other "
      "than the very first line of the deck.",
      "NASTRAN BANDIT=-1 and its kind must be line 1, ahead of every "
      "comment." },
    { "615",
      "The case control could not be parsed.",
      "A command is misspelled or has an option the 1970s solver does not "
      "have (the parenthesised MSC options: DISP(PLOT), STRESS(VONM)). "
      "nastran95ase strips those; check the translated deck." },
    { "2015",
      "A grid point has no element attached to it, or only rigid "
      "elements, so its stiffness is zero.",
      "Constrain the grid (SPC1), give it an element, or remove it. A "
      "grid held only by CONM2 mass is a mechanism." },
    { "2050",
      "A SUPORT names a grid point that does not exist in the model.",
      "The grid id on the SUPORT (or SUPORT1) card is wrong, or the grid "
      "was renumbered: ids above 16,777,215 cannot be used." },
    { "2101A",
      "A degree of freedom is in two mutually exclusive sets: typically "
      "dependent on a rigid element (m-set) and also on a SUPORT (r-set) "
      "or an SPC (s-set).",
      "The independent end of the rigid element must be the grid that "
      "carries the SUPORT or SPC. On an RBAR, swap GA and GB, or move the "
      "123456 from CNB to CNA." },
    { "2140A",
      "Grid ids larger than the resequencer can handle are present, so "
      "bandwidth resequencing was not done.",
      "Harmless for the answer; the decomposition is slower. Renumber "
      "below 1,000,000 to get it back, or ignore." },
    { "3005",
      "A matrix decomposition found a singularity that the singularity "
      "check did not: the model is a mechanism or a set of grids is "
      "unattached.",
      "Look for the UFM 3097 column list just above, and the grid point "
      "singularity table earlier in the print file: those are the "
      "degrees of freedom to constrain or connect." },
    { "3019",
      "The print file passed the MAXLINES limit and the job was stopped.",
      "Add MAXLINES = 999999 to the case control (nastran95ase does), or "
      "print less: DISPLACEMENT = ALL on a large model is thousands of "
      "pages." },
    { "3031",
      "A load set the case control selects is not in the bulk data, or "
      "holds nothing the static load table can use.",
      "Check the LOAD = n against the FORCE/MOMENT/PLOAD/GRAV set ids. A "
      "set with only SPCD cards is 'not found' when other sets carry "
      "forces: give it a FORCE as well." },
    { "3037",
      "The bandwidth resequencer (BANDIT) ran out of its scratch array.",
      "Turn it off: NASTRAN BANDIT=-1 as the very first line of the deck "
      "(nastran95ase does this for models over 300 grids). The answer "
      "does not depend on it." },
    { "3097",
      "The stiffness matrix is singular in the listed columns: those "
      "degrees of freedom have no stiffness, or the model is a mechanism.",
      "Constrain them with SPC1, or connect them. Rotations of grids that "
      "only rods or springs attach to, and every dof of a grid that only "
      "a mass attaches to, are the usual ones. MSC's AUTOSPC does this "
      "silently; nastran95ase does it and reports it; nastran95 does "
      "not." },
    { "6206",
      "The substructure operating file (SOF) exists and was written by a "
      "different job (its password does not match).",
      "Point SOF1 in the environment at a file of your own, or delete the "
      "old one. Each substructuring phase must see the SOF the previous "
      "phase left." },
    { "2386",
      "The eigensolver (FEER) could not make the shifted stiffness matrix "
      "non-singular by moving the shift: the model has a mechanism, or "
      "degrees of freedom with neither stiffness nor mass.",
      "Find the singular columns with a statics run of the same model "
      "(UFM 3097 lists them), then constrain or connect them." },
    { "2391",
      "FEER's tridiagonal reduction produced no usable rows, so there is "
      "no reduced problem to solve. Since the 2394 guard, this follows a "
      "start vector with no positive mass norm: the mass matrix is "
      "singular or indefinite along it.",
      "Look for UWM 2394 just above. Give the massless degrees of freedom "
      "mass (CONM2 rotary inertia terms I11, I22, I33) or remove them "
      "(ASET/OMIT), and check that no CONM2 carries a negative mass or "
      "an inertia tensor that is not positive definite." },
    { "2395",
      "The QR iteration on FEER's reduced tridiagonal matrix did not "
      "converge, or was handed a NaN. Before this guard it spun forever; "
      "the root cause is a trial vector that lost its mass norm (UWM "
      "2394) on a semi-definite mass matrix.",
      "Look for UWM 2394 earlier in the print file and treat it as that "
      "message says: rotary inertia on the lumped masses, or ASET/OMIT "
      "the massless rotations. DIAG 16 prints every row of the "
      "reduction." },
    { "GINO",
      "The solver's own file system (GINO) found a scratch file in a state "
      "its writer never left it in: a read past the end, or a record type "
      "it did not expect. That is a defect in the solver's logic, not in "
      "the model; the last module named in the log file is where.",
      "Report it with the deck and the log. One such path, FEER's reseed "
      "after a null trial vector with the vectors held in core, is fixed "
      "in this fork; a new one is a bug to chase in the module the log "
      "names." },
    { "-8",
      "A module ran out of open core (the solver's fixed working memory).",
      "Set OCMEM in the environment higher (nastran95 defaults to "
      "2,000,000 words, nastran95ase to three quarters of the build's "
      "open core); rebuild with a larger -DNASTRAN_OPEN_CORE_WORDS if "
      "that is not enough." },
    { NULL, NULL, NULL }
};

/* the message number as printed: the token after "MESSAGE", digits and
 * a trailing letter, comma or period stripped                          */
static void message_number(const char *line, char *num, size_t n)
{
    const char *p = strstr(line, "MESSAGE");
    size_t k = 0;
    num[0] = '\0';
    if (!p) return;
    p += 7;
    while (*p == ' ') p++;
    while (*p && k < n - 1 && (isalnum((unsigned char) *p) || *p == '-')) num[k++] = *p++;
    num[k] = '\0';
}

static void wrap(FILE *fp, const char *indent, const char *text)
{
    const char *p = text;
    int col = 0;
    /* the caller has written the label the first line hangs from */
    while (*p) {
        const char *q = p;
        int wl = 0;
        while (*q && *q != ' ') { q++; wl++; }
        if (col + wl > 70 && col > 0) { fprintf(fp, "\n%s", indent); col = 0; }
        fwrite(p, 1, (size_t) wl, fp);
        col += wl;
        p = q;
        while (*p == ' ') p++;
        if (*p) { fputc(' ', fp); col++; }
    }
    fputc('\n', fp);
}

/* Scan the print file; on the first fatal, repeat it and explain it.
 * Returns 1 if a fatal was found.                                     */
int msc_diag(const char *prt)
{
    FILE *fp = fopen(prt, "r");
    char  line[512], num[16];
    int   found = 0, shown = 0, core = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        int fatal = (strstr(line, "FATAL MESSAGE") != NULL &&
                     strstr(line, "***") != NULL);
        /* GINO, the solver's file system, stops a run with "I/O
         * SUBSYSTEM ERROR NUMBER nnn" and no numbered message at all;
         * the exit handler counts it as a fatal and so does this     */
        int gino = (strstr(line, "I/O SUBSYSTEM ERROR NUMBER") != NULL);
        if (gino) fatal = 1;
        /* an out-of-core stop prints no message at all: ERRTRC after a
         * data block table is its signature                          */
        if (strstr(line, "ERRTRC CALLED")) core = 1;
        if (!fatal) continue;
        found = 1;
        if (shown) continue;
        shown = 1;
        if (gino) strcpy(num, "GINO");
        else      message_number(line, num, sizeof(num));
        fprintf(stderr, "\nnastran: the solver stopped on this message:\n");
        {
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        }
        fprintf(stderr, "    %s\n", line + (line[0] == '0' ? 1 : 0));
        /* the continuation lines of the message, up to a blank line */
        {
            int k;
            for (k = 0; k < 3 && fgets(line, sizeof(line), fp); k++) {
                size_t n = strlen(line);
                while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
                if (msc_isblank_line(line)) break;
                fprintf(stderr, "    %s\n", line);
            }
        }
        {
            int i;
            for (i = 0; catalogue[i].num; i++) {
                if (msc_streq(catalogue[i].num, num)) {
                    fprintf(stderr, "  WHAT  ");
                    wrap(stderr, "        ", catalogue[i].what);
                    fprintf(stderr, "  FIX   ");
                    wrap(stderr, "        ", catalogue[i].fix);
                    break;
                }
            }
            if (!catalogue[i].num)
                fprintf(stderr, "  (no explanation on file for message %s; "
                        "the NASTRAN User's Manual, section 6, has the "
                        "list)\n", num);
        }
    }
    fclose(fp);
    if (!found && core) {
        int i;
        fprintf(stderr, "\nnastran: the solver stopped without a numbered "
                "message, which is how it runs out of open core.\n");
        for (i = 0; catalogue[i].num; i++)
            if (msc_streq(catalogue[i].num, "-8")) {
                fprintf(stderr, "  WHAT  "); wrap(stderr, "        ", catalogue[i].what);
                fprintf(stderr, "  FIX   "); wrap(stderr, "        ", catalogue[i].fix);
            }
        return 1;
    }
    return found;
}
