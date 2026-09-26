/* HALO: the wall-clock watchdog, for both executables.
 *
 * NASTRAN's own TIME card is checked between modules (TMTOGO), so a
 * loop inside a module runs until someone kills the process. That is
 * how a 1995 eigensolver spun for hours on a NaN (see mis/ferxtd.f,
 * label 480). The NaN is guarded now, but "does not hang" has to hold
 * for the loops nobody has found yet, so a second thread sleeps for
 * the allowed wall-clock time and then ends the process with a message
 * that says what happened and how to raise the limit.
 *
 * It ends the process with _exit, not exit: the main thread is inside
 * the solver, possibly inside a Fortran WRITE with a unit locked, and
 * running the Fortran runtime's clean-up from another thread on top
 * of that is not safe. The costs are the ones the message states: the
 * print file stops where the solver was, and the scratch directory is
 * left for the user to delete. Exit code 2, the same as "no END OF
 * JOB", which is what the print file will show.
 *
 * Which limit applies is the main program's decision (bin/nastrn.f.in):
 * N95_TIMEOUT in the environment (minutes, 0 disables), else the TIME
 * card for the 1970s executable (NASA's own default of 5 when the
 * card is missing), else 30 minutes for nastran95ase, whose decks
 * carry no meaningful TIME.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define WD_WRITE _write
#else
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/syscall.h>
#define WD_WRITE write
#endif

static double wd_minutes;
static char   wd_note[1024];

/* HALO: the scratch directory this run made (bin/nastrn.f.in hands it over
 * through HMSCSD), removed on the ways out the exit handler (mds/hexit.f,
 * HCLEAN on every exit() - a normal end and a fatal alike) does not see:
 * the watchdog's _exit; SIGTERM, SIGINT and SIGHUP (the SOL 145 driver's
 * children get SIGTERM when the driver dies; a run stopped from a
 * terminal or a test harness gets one of them); and SIGSEGV, SIGBUS,
 * SIGFPE and SIGILL, after which the handler that was there before (the
 * Fortran runtime's backtrace) still runs. Without it every such run left
 * <TMPDIR or /tmp>/n95_<pid> behind, gigabytes each; on a machine whose
 * /tmp is RAM (a tmpfs) a morning's worth filled it and the next runs
 * stopped at a GINO I/O error (2026-09-26). Only the one directory this
 * process made, and only the files in it (NASTRAN's scratch is flat; a
 * subdirectory is never touched): never a pattern, never a DIRCTY from the
 * environment. N95_KEEP_SCRATCH=1 keeps it (NASTRAN has no keep option of
 * its own: NASA's csh wrapper always removed the directory).
 *
 * What a process cannot do for itself - a SIGKILL; on Windows any end
 * while its scratch files are open, since Windows does not delete an open
 * file (the watchdog's _exit, the job object's TerminateProcess, a
 * console close) - the SOL 145 driver does for its children once it has
 * waited for them (msc/mscflut.c): the child writes its directory's exact
 * path into the file N95_SCRATCH_NOTE names, and the driver removes that
 * directory with msc_remove_scratch below. A top-level run that is
 * SIGKILLed or crashes on Windows still leaves its directory.             */
static char   wd_scratch[512];
static int    wd_scratch_made;

#ifndef _WIN32
struct wd_dirent64 {              /* the kernel's getdents64 record */
    unsigned long long d_ino;
    long long          d_off;
    unsigned short     d_reclen;
    unsigned char      d_type;
    char               d_name[];
};
#endif

/* N95_KEEP_SCRATCH=1 (anything but empty or 0): keep every scratch
 * directory, for debugging                                             */
int msc_keep_scratch(void)
{
    const char *keep = getenv("N95_KEEP_SCRATCH");
    return keep && *keep && *keep != '0';
}

/* remove the files of directory DIR (not its subdirectories) and then the
 * directory: 0 when it is gone (or was not there). Async-signal-safe on
 * Linux (raw system calls, a static buffer), because the signal handlers
 * call it; not re-entrant. A file still open elsewhere survives on
 * Windows, and so then does the directory.                             */
int msc_remove_scratch(const char *dir)
{
    if (!dir || !*dir) return 0;
#ifdef _WIN32
    {
        char pat[600], f[600];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        if (strlen(dir) > 500) return 1;
        snprintf(pat, sizeof pat, "%s\\*", dir);
        h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            snprintf(f, sizeof f, "%s\\%s", dir, fd.cFileName);
            DeleteFileA(f);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        return RemoveDirectoryA(dir) ? 0 : 1;
    }
#else
    {
        static char buf[8192];
        long n, off;
        int  fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0) return 0;
        while ((n = syscall(SYS_getdents64, fd, buf, sizeof buf)) > 0) {
            for (off = 0; off < n; ) {
                struct wd_dirent64 *d = (struct wd_dirent64 *)(buf + off);
                off += d->d_reclen;
                if (d->d_type == DT_DIR) continue;
                unlinkat(fd, d->d_name, 0);
            }
        }
        close(fd);
        return rmdir(dir) == 0 ? 0 : 1;
    }
#endif
}

/* this run's own scratch directory, when it made it */
static void wd_clean(void)
{
    if (wd_scratch_made && wd_scratch[0]) msc_remove_scratch(wd_scratch);
}

#ifndef _WIN32
/* the handlers there were before ours, for the crash signals: the Fortran
 * runtime's backtrace still prints after the scratch is gone          */
static struct sigaction wd_old[32];

static void wd_on_signal(int sig)
{
    wd_clean();
    /* the handler before ours (SIG_DFL for the stop signals), delivered
     * when this one returns: raise() while the signal is blocked leaves it
     * pending, and a crash signal's instruction faults again anyway       */
    if (sig > 0 && sig < 32) sigaction(sig, &wd_old[sig], NULL);
    raise(sig);
}
#endif

/* the scratch directory and whether this run made it (bin/nastrn.f.in).
 * A child of the SOL 145 driver also writes the path into the file the
 * driver named in N95_SCRATCH_NOTE, so that the driver can remove it
 * after an end the child could not clean up after itself              */
void msc_scratch_dir(const char *dir, int made)
{
    if (msc_keep_scratch()) return;
    if (!made || !dir || !*dir || strlen(dir) >= sizeof wd_scratch) return;
    strcpy(wd_scratch, dir);
    wd_scratch_made = 1;
    {
        const char *note = getenv("N95_SCRATCH_NOTE");
        if (note && *note) {
            FILE *f = fopen(note, "w");
            if (f) {
                fprintf(f, "%s\n", dir);
                fclose(f);
            }
        }
    }
#ifndef _WIN32
    {
        static const int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGSEGV, SIGBUS, SIGFPE, SIGILL };
        struct sigaction sa;
        size_t i;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = wd_on_signal;
        sigemptyset(&sa.sa_mask);
        for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
            /* a signal the process was started ignoring stays ignored: a
             * background job's SIGINT, nohup's SIGHUP                    */
            if (sigaction(sigs[i], NULL, &wd_old[sigs[i]]) != 0) continue;
            if (wd_old[sigs[i]].sa_handler == SIG_IGN) continue;
            sigaction(sigs[i], &sa, NULL);
        }
    }
#endif
}

#ifdef _WIN32
/* HALO: Windows 11 runs a process whose window is minimised or in the
 * background - a solver started by MATLAB's system(), a study driver's
 * children - under EcoQoS when it may: efficiency cores, lowered clocks
 * (the "power throttling" of execution speed). A solver wants the
 * opposite, so every executable of this build opts itself out before
 * main (SetProcessInformation, ProcessPowerThrottling, the execution
 * speed bit controlled and cleared; no administrator needed, per
 * process: each child does it for itself). Looked up at run time, so the
 * executable still starts on a Windows without the call (before 8), and
 * a failure changes nothing. N95_THROTTLE=1 leaves Windows' choice.     */
typedef BOOL (WINAPI *wd_setinfo_fn)(HANDLE, int, LPVOID, DWORD);
__attribute__((constructor)) static void wd_no_throttle(void)
{
    struct { ULONG Version, ControlMask, StateMask; } state;   /* PROCESS_POWER_THROTTLING_STATE */
    const char   *e = getenv("N95_THROTTLE");
    HMODULE       k = GetModuleHandleA("kernel32.dll");
    wd_setinfo_fn set = k ? (wd_setinfo_fn) (void (*)(void)) GetProcAddress(k, "SetProcessInformation") : NULL;
    if (set == NULL || (e != NULL && e[0] == '1')) return;
    state.Version = 1;          /* PROCESS_POWER_THROTTLING_CURRENT_VERSION */
    state.ControlMask = 0x1;    /* PROCESS_POWER_THROTTLING_EXECUTION_SPEED */
    state.StateMask = 0;        /* not throttled */
    set(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &state, sizeof(state));
}
#endif

static void wd_fire(void)
{
    char msg[1600];
    int  n = snprintf(msg, sizeof msg,
        "\nnastran: stopped after %g minutes of wall clock, the limit for this run.\n"
        "%s"
        "The print file ends where the solver was; the scratch directory is\n"
        "removed (N95_KEEP_SCRATCH=1 keeps it). A run this long is a loop in\n"
        "the solver, not a slow model: 5,000-grid models finish in two\n"
        "minutes. If it really is a big model, set N95_TIMEOUT=<minutes> in\n"
        "the environment (0 disables).\n",
        wd_minutes, wd_note);
    if (n > 0) WD_WRITE(2, msg, (unsigned)n);
    wd_clean();
    _exit(2);
}

#ifdef _WIN32
static DWORD WINAPI wd_thread(LPVOID p)
{
    (void)p;
    Sleep((DWORD)(wd_minutes * 60000.0));
    wd_fire();
    return 0;
}
#else
static void *wd_thread(void *p)
{
    /* HALO: nanosleep on whole seconds, restarted after a signal. The
     * usleep this replaces takes a 32-bit useconds_t, so any limit over
     * 71.58 minutes wrapped: N95_TIMEOUT=300 fired after 820 s, and the
     * benchmark runner's 14400 after 712 s (2026-09-26). */
    struct timespec left;
    double s = wd_minutes * 60.0;
    (void)p;
    left.tv_sec = (time_t)s;
    left.tv_nsec = (long)((s - (double)left.tv_sec) * 1.0e9);
    while (nanosleep(&left, &left) != 0 && errno == EINTR) { }
    wd_fire();
    return NULL;
}
#endif

/* start the watchdog: minutes of wall clock, and a note (which limit
 * applied, where the scratch directory is) for the message. Returns 0
 * when the thread is running, 1 when it could not be started, and does
 * nothing for a limit of zero or less. */
int msc_watchdog(double minutes, const char *note)
{
    if (minutes <= 0.0) return 0;
    wd_minutes = minutes;
    if (note) {
        strncpy(wd_note, note, sizeof wd_note - 2);
        wd_note[sizeof wd_note - 2] = '\0';
        if (wd_note[0] && wd_note[strlen(wd_note) - 1] != '\n')
            strcat(wd_note, "\n");
    } else {
        wd_note[0] = '\0';
    }
#ifdef _WIN32
    {
        HANDLE h = CreateThread(NULL, 0, wd_thread, NULL, 0, NULL);
        if (h == NULL) return 1;
        CloseHandle(h);
    }
#else
    {
        pthread_t t;
        if (pthread_create(&t, NULL, wd_thread, NULL) != 0) return 1;
        pthread_detach(t);
    }
#endif
    return 0;
}
