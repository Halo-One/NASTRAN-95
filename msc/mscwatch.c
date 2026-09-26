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
#define WD_WRITE write
#endif

static double wd_minutes;
static char   wd_note[1024];

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
        "The print file ends where the solver was and the scratch directory\n"
        "is left behind. A run this long is a loop in the solver, not a slow\n"
        "model: 5,000-grid models finish in two minutes. If it really is a\n"
        "big model, set N95_TIMEOUT=<minutes> in the environment (0 disables).\n",
        wd_minutes, wd_note);
    if (n > 0) WD_WRITE(2, msg, (unsigned)n);
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
    (void)p;
    usleep((useconds_t)(wd_minutes * 60.0e6));
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
