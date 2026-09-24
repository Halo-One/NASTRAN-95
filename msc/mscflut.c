/* HALO: SOL 145 with several subcases, run side by side.
 *
 * NASTRAN-95's flutter rigid format (AERO 10) solves one subcase: FA1
 * reads the first CASECC record for its FMETHOD and never comes back for
 * another. The repo's decks carry one subcase per mach. Rather than keep
 * the first and drop the rest, this driver writes one translated deck per
 * subcase, runs them as child processes of this executable (each with
 * --cosmic, as the SOL 200 driver does), as many at a time as there are
 * processors (N95_JOBS overrides), and joins their print files into the
 * one the caller asked for, in subcase order, so that read_nastran_flutter
 * sees the subcases it would see from MSC.
 *
 * Each subcase recomputes the modes and its own aerodynamics; nothing is
 * shared, because the aerodynamics depend on the mach anyway and the
 * solver has no way to hand a datablock from one process to another.
 * Splitting finer - one child per matched point (density, mach, velocity)
 * - would repeat the modes and the whole aerodynamic matrix set for a few
 * seconds of PK iteration each, forty times over, so it is not offered.  */
#include "msc.h"
#include <direct.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define FLUT_MAXSUB 64

/* the SUBCASE lines of the case control, in order */
static int find_subcases(const msc_deck *d, int *at, int cap)
{
    int  i, n = 0;
    char up[MSC_LINELEN];
    for (i = 0; i < d->ncase; i++) {
        const char *p = d->cases[i];
        while (*p == ' ' || *p == '\t') p++;
        strncpy(up, p, sizeof(up) - 1);
        up[sizeof(up) - 1] = '\0';
        msc_upper(up);
        if (strncmp(up, "SUBCASE", 7) == 0 && n < cap) at[n++] = i;
    }
    return n;
}

/* how many subcases a SOL 145 deck has; the driver runs when there is
 * more than one and this is not already a child                        */
int msc_flutter_subcases(const msc_deck *d)
{
    int at[FLUT_MAXSUB];
    if (d->sol != 145) return 0;
    if (getenv("N95_CHILD")) return 0;
    return find_subcases(d, at, FLUT_MAXSUB);
}

/* the deck with only subcase k of n: the lines above the first SUBCASE
 * and that subcase's block                                            */
static void keep_subcase(msc_deck *d, const int *at, int n, int k)
{
    int i, j = 0, first = at[0], from = at[k], to = (k + 1 < n) ? at[k + 1] : d->ncase;
    char **kept = (char **) malloc(sizeof(char *) * (size_t) (d->ncase + 1));
    for (i = 0; i < first; i++) kept[j++] = d->cases[i];
    for (i = from; i < to; i++) kept[j++] = d->cases[i];
    for (i = first; i < from; i++) free(d->cases[i]);
    for (i = to; i < d->ncase; i++) free(d->cases[i]);
    free(d->cases);
    d->cases = kept;
    d->ncase = j;
}

static int copy_file(FILE *to, const char *path)
{
    char  buf[65536];
    size_t n;
    FILE *from = fopen(path, "rb");
    if (!from) return 1;
    while ((n = fread(buf, 1, sizeof(buf), from)) > 0) fwrite(buf, 1, n, to);
    fclose(from);
    return 0;
}

int msc_sol145(const char *deck, const char *outdir, const char *stem)
{
    char   exe[MAX_PATH], full[MAX_PATH], msg[MSC_PATHLEN];
    char   child_stem[FLUT_MAXSUB][MSC_PATHLEN];
    int    at[FLUT_MAXSUB], id[FLUT_MAXSUB], code[FLUT_MAXSUB];
    HANDLE proc[FLUT_MAXSUB];
    int    n, k, running = 0, jobs, worst = 0;
    const char *env;
    msc_deck d;

    GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!_fullpath(full, deck, sizeof(full))) strncpy(full, deck, sizeof(full) - 1);
    if (outdir && *outdir && !(outdir[0] == '.' && outdir[1] == '\0')) {
        _mkdir(outdir);
        if (_chdir(outdir) != 0) {
            fprintf(stderr, "nastran: cannot use output directory %s\n", outdir);
            return 1;
        }
    }
    snprintf(msg, sizeof(msg), "%s_xlat.txt", stem);
    msc_msg_open(msg);

    if (msc_read(full, &d)) { msc_msg_close(); return 3; }
    n = find_subcases(&d, at, FLUT_MAXSUB);
    for (k = 0; k < n; k++) {
        const char *p = d.cases[at[k]] + 7;
        id[k] = atoi(p);
        if (id[k] <= 0) id[k] = k + 1;
    }
    msc_free(&d);

    jobs = 0;
    env = getenv("N95_JOBS");
    if (env) jobs = atoi(env);
    if (jobs <= 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        jobs = (int) si.dwNumberOfProcessors;
        if (jobs < 1) jobs = 1;
    }
    if (jobs > n) jobs = n;
    msc_msg(MSC_INFO, 9450,
        "SOL 145 with %d subcases, one FMETHOD each: NASTRAN-95 solves one per\n"
        "run, so each becomes a child run of this executable, %d at a time\n"
        "(N95_JOBS sets it). Their print files are joined into %s.out in\n"
        "subcase order; each child's own is %s_s<subcase>.out.",
        n, jobs, stem, stem);

    /* one translated deck per subcase, then the children */
    _putenv("N95_CHILD=1");
    for (k = 0; k < n; k++) {
        msc_stats st;
        char cosmic[MSC_PATHLEN], q1[MSC_PATHLEN + 4], q2[8];
        intptr_t h;

        snprintf(child_stem[k], sizeof(child_stem[k]), "%s_s%d", stem, id[k]);
        snprintf(cosmic, sizeof(cosmic), "%s_n95.dat", child_stem[k]);
        if (msc_read(full, &d)) { msc_msg_close(); return 3; }
        find_subcases(&d, at, FLUT_MAXSUB);
        keep_subcase(&d, at, n, k);
        if (msc_translate_deck(&d, cosmic, &st)) {
            msc_free(&d);
            msc_msg(MSC_FATAL, 9451, "subcase %d was not translated; see the messages above.", id[k]);
            msc_msg_close();
            return 3;
        }
        msc_free(&d);
        if (k == 0) msc_tally_print();

        /* wait for a slot */
        while (running >= jobs) {
            DWORD w = WaitForMultipleObjects((DWORD) running, proc, FALSE, INFINITE);
            int   i = (int) (w - WAIT_OBJECT_0), j;
            DWORD ec = 0;
            if (i < 0 || i >= running) break;
            GetExitCodeProcess(proc[i], &ec);
            CloseHandle(proc[i]);
            for (j = 0; j < n; j++) if (proc[j] == proc[i] && code[j] == -1) code[j] = (int) ec;
            for (j = i; j < running - 1; j++) proc[j] = proc[j + 1];
            running--;
        }
        snprintf(q1, sizeof(q1), "\"%s\"", cosmic);
        snprintf(q2, sizeof(q2), ".");
        h = _spawnl(_P_NOWAIT, exe, "nastran95ase", "--cosmic", q1, q2, NULL);
        if (h == -1) {
            msc_msg(MSC_FATAL, 9452, "could not start the child run for subcase %d: is %s runnable?", id[k], exe);
            msc_msg_close();
            return 3;
        }
        code[k] = -1;
        proc[running++] = (HANDLE) h;
        fprintf(stderr, "nastran: subcase %d -> %s.out (running)\n", id[k], child_stem[k]);
    }
    /* the handles were kept in start order in proc[] only while running;
     * finish the rest and take every exit code by waiting on each        */
    while (running > 0) {
        DWORD w = WaitForMultipleObjects((DWORD) running, proc, FALSE, INFINITE);
        int   i = (int) (w - WAIT_OBJECT_0), j;
        DWORD ec = 0;
        if (i < 0 || i >= running) break;
        GetExitCodeProcess(proc[i], &ec);
        CloseHandle(proc[i]);
        for (j = 0; j < n; j++) if (code[j] == -1) { code[j] = (int) ec; break; }
        for (j = i; j < running - 1; j++) proc[j] = proc[j + 1];
        running--;
    }

    /* the joined print file, and the verdict */
    {
        char prt[MSC_PATHLEN];
        FILE *out;
        snprintf(prt, sizeof(prt), "%s.out", stem);
        out = fopen(prt, "wb");
        for (k = 0; k < n; k++) {
            char child_prt[MSC_PATHLEN];
            snprintf(child_prt, sizeof(child_prt), "%s.out", child_stem[k]);
            if (code[k] > worst) worst = code[k];
            if (out && copy_file(out, child_prt))
                msc_msg(MSC_WARN, 9453, "subcase %d left no print file (%s, exit code %d).",
                        id[k], child_prt, code[k]);
            fprintf(stderr, "nastran: subcase %d -> %s (exit code %d)\n", id[k], child_prt, code[k]);
        }
        if (out) fclose(out);
    }
    msc_msg_summary();
    msc_msg_close();
    fprintf(stderr, "nastran: %s -> %s.out (%d subcases joined)\n", stem, stem, n);
    return worst;
}
