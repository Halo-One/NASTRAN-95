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
 * Each child runs in a directory of its own, s<subcase> under the output
 * directory: the solver keeps files under fixed names beside its print
 * file (the checkpoint dictionary, the plot and 'none' files), and two
 * children in one directory write over each other's and die in GP4.
 *
 * Each subcase recomputes the modes and its own aerodynamics; nothing is
 * shared, because the aerodynamics depend on the mach anyway and the
 * solver has no way to hand a datablock from one process to another.
 * Splitting finer - one child per matched point (density, mach, velocity)
 * - would repeat the modes and the whole aerodynamic matrix set for a few
 * seconds of PK iteration each, forty times over, so it is not offered.  */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE                 /* sched_getaffinity, CPU_COUNT */
#endif
#include "msc.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define FLUT_SEP "\\"
typedef HANDLE flut_proc;
#define FLUT_NOPROC NULL
#else
/* HALO: the POSIX spellings. A child is started with fork + execv (as
 *   the SOL 200 driver's are, mscopt2.c) and waited for with waitpid on
 *   whichever finishes first; the processors are the ones this process
 *   may run on (sched_getaffinity, so taskset and a container's CPU set
 *   are honoured), and on Linux each child asks to be sent SIGTERM when
 *   the driver dies, so a driver stopped by its watchdog or by the user
 *   does not leave its children solving for nobody.                   */
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#define FLUT_SEP "/"
typedef pid_t flut_proc;
#define FLUT_NOPROC ((pid_t) 0)
#endif

#define FLUT_MAXSUB 64

/* the processors this process may use */
static int flut_processors(void)
{
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int) si.dwNumberOfProcessors : 1;
#else
    long n;
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0 && CPU_COUNT(&set) > 0)
        return CPU_COUNT(&set);
#endif
    n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int) n : 1;
#endif
}

/* an environment variable for the children. Windows' _putenv copies the
 * string; POSIX putenv would keep the caller's, so setenv there       */
static void flut_setenv(const char *name, const char *value)
{
#ifdef _WIN32
    char buf[80];
    snprintf(buf, sizeof(buf), "%s=%s", name, value);
    _putenv(buf);
#else
    setenv(name, value, 1);
#endif
}

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

/* the Mach of a one-subcase deck: its FMETHOD's FLUTTER card names the
 * Mach FLFACT list, and one value there (the matched-point decks, PKNL
 * / PARAM PKMATCH) is the Mach of every point of the child. -1 when the
 * list has several Machs, a THRU form, or the subcase has no FMETHOD   */
static double subcase_mach(const msc_deck *d)
{
    int    i, j, fmethod = 0, mach_set = 0;
    double m = -1.0;
    for (i = 0; i < d->ncase; i++) {
        char up[MSC_LINELEN];
        const char *p = d->cases[i], *e;
        while (*p == ' ' || *p == '\t') p++;
        strncpy(up, p, sizeof(up) - 1);
        up[sizeof(up) - 1] = '\0';
        msc_upper(up);
        if (strncmp(up, "FMETHOD", 7) == 0 && (e = strchr(up, '=')) != NULL)
            fmethod = atoi(e + 1);
    }
    if (!fmethod) return -1.0;
    for (i = 0; i < d->nbulk; i++) {
        const msc_card *c = &d->bulk[i];
        if (msc_streq(c->name, "FLUTTER") && msc_fi(c, 1, 0) == fmethod) {
            mach_set = msc_fi(c, 4, 0);
            break;
        }
    }
    if (!mach_set) return -1.0;
    for (i = 0; i < d->nbulk; i++) {
        const msc_card *c = &d->bulk[i];
        if (!msc_streq(c->name, "FLFACT") || msc_fi(c, 1, 0) != mach_set) continue;
        for (j = 2; j <= c->nfld; j++) {
            const char *f = msc_f(c, j);
            double v;
            if (!f[0]) continue;
            if (!isdigit((unsigned char) f[0]) && f[0] != '.' && f[0] != '-' && f[0] != '+')
                return -1.0;                       /* F1 THRU FNF NF FMID */
            v = msc_fd(c, j, -1.0);
            if (m < 0.0) m = v;
            else if (fabs(v - m) > 1e-6) return -1.0;
        }
    }
    return m;
}

/* the child's deck at its Mach: PARAM MACH for the aerodynamic loads
 * (ADR takes the Mach of the MKAERO1 list closest to PARAM MACH; NASA's
 * default 0.0 takes the lowest, the wrong one for every other subcase),
 * and the MKAERO1 / MKAERO2 lists cut to that Mach, so the child computes
 * the aerodynamic matrices it uses rather than every Mach of the deck -
 * on a five-Mach deck five times the doublet lattice and the solves    */
static void child_at_mach(msc_deck *d, int id)
{
    double m = subcase_mach(d);
    int    i, j, cut = 0;
    msc_card *p;
    if (m < 0.0) return;
    p = msc_bulk_add(d, "PARAM");
    msc_set(p, 1, "MACH");
    msc_setd(p, 2, m);
    for (i = 0; i < d->nbulk; i++) {
        msc_card *c = &d->bulk[i];
        if (msc_streq(c->name, "MKAERO1")) {
            /* fields 1-8 the Machs, 9-16 the k list shared by all of them */
            char keep[MSC_FLDLEN] = "";
            for (j = 1; j <= 8 && j <= c->nfld; j++) {
                if (msc_blank(c, j)) continue;
                if (fabs(msc_fd(c, j, -9.0) - m) <= 1e-6) strncpy(keep, msc_f(c, j), MSC_FLDLEN - 1);
                else cut++;
            }
            for (j = 1; j <= 8 && j <= c->nfld; j++) msc_set(c, j, "");
            if (keep[0]) msc_set(c, 1, keep);
            else c->dropped = 1;                   /* no Mach of this child on it */
        } else if (msc_streq(c->name, "MKAERO2")) {
            /* pairs m k, the pairs at this Mach packed to the front */
            int n = 0;
            for (j = 1; j + 1 <= c->nfld; j += 2) {
                if (msc_blank(c, j)) continue;
                if (fabs(msc_fd(c, j, -9.0) - m) <= 1e-6) {
                    char mk[MSC_FLDLEN], kk[MSC_FLDLEN];
                    strncpy(mk, msc_f(c, j), MSC_FLDLEN - 1);     mk[MSC_FLDLEN - 1] = '\0';
                    strncpy(kk, msc_f(c, j + 1), MSC_FLDLEN - 1); kk[MSC_FLDLEN - 1] = '\0';
                    msc_set(c, 2 * n + 1, mk);
                    msc_set(c, 2 * n + 2, kk);
                    n++;
                } else cut++;
            }
            for (j = 2 * n + 1; j <= c->nfld; j++) msc_set(c, j, "");
            if (!n) c->dropped = 1;
        }
    }
    msc_msg(MSC_INFO, 9455,
        "subcase %d runs at Mach %g: PARAM MACH %g for the aerodynamic loads\n"
        "(AEROF), and the MKAERO1 / MKAERO2 lists cut to that Mach (%d other\n"
        "Mach entries dropped), so this child computes only the aerodynamic\n"
        "matrices it uses.", id, m, m, cut);
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

/* translate subcase k into its own COSMIC deck, in its own directory, and
 * start the child that solves it there; the child, or FLUT_NOPROC      */
static flut_proc start_child(const char *full, const char *exe, const char *dir,
                             const char *child_stem, int k, int n, int id)
{
    msc_deck  d;
    msc_stats st;
    char      deck[MSC_PATHLEN];
    int       at[FLUT_MAXSUB];
    flut_proc proc;

    msc_mkdir(dir);
    snprintf(deck, sizeof(deck), "%s" FLUT_SEP "%s.dat", dir, child_stem);
    if (msc_read(full, &d)) return FLUT_NOPROC;
    find_subcases(&d, at, FLUT_MAXSUB);
    keep_subcase(&d, at, n, k);
    child_at_mach(&d, id);
    /* msc_translate zeroes the tallies before translating; a translation
     * called directly must too, or a stray autospc count selects an SPC
     * set that was never written and the child dies in GP4            */
    memset(&st, 0, sizeof(st));
    if (msc_translate_deck(&d, deck, &st)) {
        msc_free(&d);
        msc_msg(MSC_FATAL, 9451, "subcase %d was not translated; see the messages above.", id);
        return FLUT_NOPROC;
    }
    msc_free(&d);
    if (k == 0) msc_tally_print();
#ifdef _WIN32
    {
        /* the child re-parses its own command line: quote the paths */
        char     q1[MSC_PATHLEN + 4], q2[MSC_PATHLEN + 4];
        intptr_t h;
        snprintf(q1, sizeof(q1), "\"%s\"", deck);
        snprintf(q2, sizeof(q2), "\"%s\"", dir);
        h = _spawnl(_P_NOWAIT, exe, "nastran95ase", "--cosmic", q1, q2, NULL);
        proc = (h == -1) ? FLUT_NOPROC : (HANDLE) h;
    }
#else
    /* the arguments arrive as written - nothing re-parses them - so they
     * are not quoted (a quote would become part of the file name). The
     * driver's messages are flushed first so the child cannot inherit
     * and repeat them. */
    fflush(NULL);
    proc = fork();
    if (proc == 0) {
        char *argv[5];
        argv[0] = (char *) "nastran95ase";
        argv[1] = (char *) "--cosmic";
        argv[2] = deck;
        argv[3] = (char *) dir;
        argv[4] = NULL;
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(3);          /* the driver is gone already */
#endif
        execv(exe, argv);
        _exit(127);
    }
    if (proc < 0) proc = FLUT_NOPROC;
#endif
    if (proc == FLUT_NOPROC) {
        msc_msg(MSC_FATAL, 9452, "could not start the child run for subcase %d: is %s runnable?", id, exe);
        return FLUT_NOPROC;
    }
    fprintf(stderr, "nastran: subcase %d -> %s" FLUT_SEP "%s.out (running)\n", id, dir, child_stem);
    return proc;
}

/* wait for whichever running child finishes first: its index, with its
 * exit code in *code (3 for a child ended by a signal, as for a fatal),
 * or -1 when there is nothing to wait for                               */
static int wait_any(flut_proc *hproc, const int *code, const int *id, int n, int *exit_code)
{
#ifdef _WIN32
    HANDLE active[FLUT_MAXSUB];
    int    which[FLUT_MAXSUB], n_active = 0, i, k;
    DWORD  w, ec = 0;
    (void) id;
    for (k = 0; k < n; k++)
        if (hproc[k] && code[k] == -1) { active[n_active] = hproc[k]; which[n_active] = k; n_active++; }
    if (n_active == 0) return -1;
    w = WaitForMultipleObjects((DWORD) n_active, active, FALSE, INFINITE);
    i = (int) (w - WAIT_OBJECT_0);
    if (i < 0 || i >= n_active) return -1;
    k = which[i];
    GetExitCodeProcess(hproc[k], &ec);
    CloseHandle(hproc[k]);
    hproc[k] = FLUT_NOPROC;
    *exit_code = (int) ec;
    return k;
#else
    int k, status, n_active = 0;
    pid_t pid;
    for (k = 0; k < n; k++)
        if (hproc[k] != FLUT_NOPROC && code[k] == -1) n_active++;
    if (n_active == 0) return -1;
    for (;;) {
        pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        for (k = 0; k < n; k++)
            if (hproc[k] == pid && code[k] == -1) break;
        if (k < n) break;                          /* one of ours */
    }
    hproc[k] = FLUT_NOPROC;
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else {
        *exit_code = 3;
        fprintf(stderr, "nastran: the child run of subcase %d was ended by signal %d\n",
                id[k], WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
    return k;
#endif
}

int msc_sol145(const char *deck, const char *outdir, const char *stem)
{
    char   exe[MAX_PATH], full[MAX_PATH], msg[MSC_PATHLEN];
    char   child_stem[FLUT_MAXSUB][MSC_PATHLEN], child_dir[FLUT_MAXSUB][32];
    int    at[FLUT_MAXSUB], id[FLUT_MAXSUB], code[FLUT_MAXSUB];
    flut_proc hproc[FLUT_MAXSUB];
    int    n, k, jobs, worst = 0, started = 0, done = 0, failed = 0;
    const char *env;
    msc_deck d;

    msc_self_path(exe, sizeof(exe));
    msc_abs_path(full, sizeof(full), deck);
    if (outdir && *outdir && !(outdir[0] == '.' && outdir[1] == '\0')) {
        msc_mkdir(outdir);
        if (msc_chdir(outdir) != 0) {
            fprintf(stderr, "nastran: cannot use output directory %s\n", outdir);
            return 1;
        }
    }
    snprintf(msg, sizeof(msg), "%s_xlat.txt", stem);
    msc_msg_open(msg);

    if (msc_read(full, &d)) { msc_msg_close(); return 3; }
    n = find_subcases(&d, at, FLUT_MAXSUB);
    for (k = 0; k < n; k++) {
        const char *p = d.cases[at[k]];
        while (*p == ' ' || *p == '\t') p++;
        id[k] = atoi(p + 7);
        if (id[k] <= 0) id[k] = k + 1;
        snprintf(child_stem[k], sizeof(child_stem[k]), "%s_s%d", stem, id[k]);
        snprintf(child_dir[k], sizeof(child_dir[k]), "s%d", id[k]);
        code[k] = -1;
        hproc[k] = FLUT_NOPROC;
    }
    msc_free(&d);

    jobs = 0;
    env = getenv("N95_JOBS");
    if (env) jobs = atoi(env);
    if (jobs <= 0) jobs = flut_processors();
    if (jobs > n) jobs = n;
    msc_msg(MSC_INFO, 9450,
        "SOL 145 with %d subcases, one FMETHOD each: NASTRAN-95 solves one per\n"
        "run, so each becomes a child run of this executable, %d at a time\n"
        "(N95_JOBS sets it), in a directory of its own, s<subcase>. Their print\n"
        "files are joined into %s.out in subcase order.",
        n, jobs, stem);

    /* the threads of the children. What a child threads is of two kinds:
     * coarse work shared out a task at a time - the doublet lattice rows
     * (mis/gendp.f) and the PK loops (mis/fa1pkp.f) - and the in-core LU
     * of the aerodynamic matrix (mis/ampczs), fine-grained, a parallel
     * region per block. The children finish at different times (the
     * lowest Mach has the most reduced frequencies, and its loops take
     * the longest), so an even split leaves the finished children's
     * processors idle while the last one works on its share. Each child
     * is given every processor for the coarse work instead, and waits
     * for work passively: the scheduler shares the processors among the
     * children while they all run and hands them to whoever is left.
     * The LU is held to the even share (N95_BLAS_THREADS), because a
     * fine-grained parallel region oversubscribed spends its time
     * waiting. Measured on the monarch deck (five Machs, 32 processors):
     * 102 s with the even split, 87 s this way; spinning waits instead
     * of passive ones, 400 s. A caller's OMP_NUM_THREADS is taken as it
     * is, with the LU following it as before.                          */
    if (!getenv("OMP_NUM_THREADS")) {
        char num[40];
        int  procs = flut_processors();
        int  share = procs / jobs;
        if (share < 1) share = 1;
        snprintf(num, sizeof(num), "%d", procs);
        flut_setenv("OMP_NUM_THREADS", num);
        if (!getenv("OMP_WAIT_POLICY")) flut_setenv("OMP_WAIT_POLICY", "PASSIVE");
        if (!getenv("N95_BLAS_THREADS")) {
            snprintf(num, sizeof(num), "%d", share);
            flut_setenv("N95_BLAS_THREADS", num);
        }
    }

    /* the children, jobs at a time, each translated as it starts */
    flut_setenv("N95_CHILD", "1");
    while (done < n) {
        int ec = 0;

        while (started < n && started - done < jobs) {
            hproc[started] = start_child(full, exe, child_dir[started], child_stem[started],
                                         started, n, id[started]);
            if (hproc[started] == FLUT_NOPROC) { code[started] = 3; done++; failed = 1; }
            started++;
        }
        k = wait_any(hproc, code, id, n, &ec);
        if (k < 0) {
            if (done < n) failed = 1;
            break;
        }
        /* a child that crashed on Windows exits with an NTSTATUS, which is
         * negative as an int: a fatal all the same                      */
        code[k] = ec < 0 ? 3 : ec;
        done++;
        fprintf(stderr, "nastran: subcase %d -> %s" FLUT_SEP "%s.out (exit code %d)\n",
                id[k], child_dir[k], child_stem[k], code[k]);
    }

    /* the joined print file, and the verdict */
    {
        char prt[MSC_PATHLEN];
        FILE *out;
        snprintf(prt, sizeof(prt), "%s.out", stem);
        out = fopen(prt, "wb");
        for (k = 0; k < n; k++) {
            char child_prt[MSC_PATHLEN];
            snprintf(child_prt, sizeof(child_prt), "%s" FLUT_SEP "%s.out", child_dir[k], child_stem[k]);
            if (code[k] > worst) worst = code[k];
            if (!out || copy_file(out, child_prt))
                msc_msg(MSC_WARN, 9453, "subcase %d left no print file (%s, exit code %d).",
                        id[k], child_prt, code[k]);
        }
        if (out) fclose(out);
        /* the children ran --cosmic and wrote NASTRAN-95's own layout;
         * the joined print is rewritten into MSC's, as a single run's is
         * (mds/hmsc.f): the sorted echo's title read_nastran_grids looks
         * for, the renumbered ids put back, blank lines as one space   */
        if (out && msc_f06(prt))
            msc_msg(MSC_WARN, 9454, "the joined print file %s was not rewritten into MSC's layout.", prt);
    }
    if (failed && worst < 3) worst = 3;
    msc_msg_summary();
    msc_msg_close();
    fprintf(stderr, "nastran: %s -> %s.out (%d subcases joined)\n", stem, stem, n);
    return worst;
}
