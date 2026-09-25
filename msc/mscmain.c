/* HALO: what the Fortran main program calls.
 *
 * One function. It reads the MSC deck, writes the COSMIC deck beside
 * the print file so that anyone can read what was actually solved, and
 * puts the messages in a file of their own as well as on the terminal.
 */
#include "msc.h"
#include "mscopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define msc_mkdir(p) _mkdir(p)
#define msc_chdir(p) _chdir(p)
#else
/* HALO: the POSIX spellings of what this file asks the operating system
 *   for: its own path, an absolute path for the deck, and make and enter
 *   a directory. 0777 on the mkdir and let the umask decide, as mkdir(1)
 *   does -- these are the user's own output files, unlike the scratch
 *   directory mds/hosunx.f makes at 0700. */
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#define msc_mkdir(p) mkdir((p), 0777)
#define msc_chdir(p) chdir(p)
#endif

/* HALO: this executable's own path, which SOL 200 needs because it runs
 *   each analysis as a child of itself and argv[0] need not be a path at
 *   all. Windows asks the loader, Linux reads the link the kernel keeps
 *   for every process, macOS has a call of its own. */
static void msc_self_path(char *buf, size_t n)
{
#if defined(_WIN32)
    GetModuleFileNameA(NULL, buf, (DWORD) n);
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t) n;
    if (_NSGetExecutablePath(buf, &sz) != 0) buf[0] = '\0';
#else
    ssize_t k = readlink("/proc/self/exe", buf, n - 1);
    buf[k > 0 ? (size_t) k : (size_t) 0] = '\0';
#endif
}

/* HALO: the deck as an absolute path, because the caller is about to
 *   change directory. Failing that, the name as it was given, which is
 *   what the Windows side did as well. */
static void msc_abs_path(char *buf, size_t n, const char *in)
{
#ifdef _WIN32
    if (_fullpath(buf, in, n)) return;
#else
    char tmp[PATH_MAX];
    if (realpath(in, tmp)) {
        strncpy(buf, tmp, n - 1);
        buf[n - 1] = '\0';
        return;
    }
#endif
    strncpy(buf, in, n - 1);
    buf[n - 1] = '\0';
}

int msc_run(const char *in, const char *out, const char *msgfile, int *rf,
            int *nmodes)
{
    msc_stats st;
    int       rc;

    if (msgfile && *msgfile) msc_msg_open(msgfile);
    msc_msg(MSC_INFO, 9000, "reading %s in the MSC dialect", in);

    /* SOL 200 is not translated: it is a loop around translations, run
     * by msc_sol200 below. The caller is told with a code of its own. */
    {
        msc_deck d;
        int is200, n145;
        if (msc_read(in, &d)) { msc_msg_close(); return 1; }
        is200 = (d.sol == 200);
        n145  = msc_flutter_subcases(&d);
        msc_free(&d);
        if (is200) {
            msc_msg(MSC_INFO, 9400, "SOL 200: design optimisation. The analyses "
                    "are run one by one below.");
            msc_msg_close();
            if (rf) *rf = 200;
            return 200;
        }
        /* SOL 145 with several subcases: NASTRAN-95 solves one per run, so
         * the driver runs one child per subcase side by side (mscflut.c) */
        if (n145 > 1) {
            msc_msg(MSC_INFO, 9449, "SOL 145 with %d subcases: one child run per "
                    "subcase, in parallel, joined below.", n145);
            msc_msg_close();
            if (rf) *rf = 10;
            return 145;
        }
    }

    rc = msc_translate(in, out, &st);

    if (rc == 0) {
        msc_msg(MSC_INFO, 9001,
            "SOL %d became rigid format %d (APP %s).\n"
            "%d cards written, %d dropped, %d ids renumbered, %d degrees\n"
            "of freedom constrained for want of stiffness.\n"
            "The translated deck is %s.",
            st.sol, st.rf, st.app, st.translated, st.dropped,
            st.renumbered, st.autospc, out);
    } else {
        msc_msg(MSC_FATAL, 9002,
            "the deck was not translated, so nothing was solved. The messages\n"
            "above say which cards stopped it.");
    }
    msc_tally_print();
    msc_msg_summary();
    msc_msg_close();
    if (rf) *rf = st.rf;
    if (nmodes) *nmodes = st.nmodes;
    msc_f06_modes(st.nmodes);
    return rc;
}

/* the SOL 200 entry: from inside the output directory, with the deck
 * made absolute first so that it is still found from there            */
int msc_sol200(const char *deck, const char *outdir, const char *stem)
{
    char exe[MAX_PATH], full[MAX_PATH], msg[MSC_PATHLEN];
    int  rc;
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
    rc = msc_opt_run(full, ".", stem, exe);
    msc_tally_print();
    msc_msg_summary();
    msc_msg_close();
    fprintf(stderr, "nastran: %s -> %s.out\n", stem, stem);
    return rc ? 3 : 0;
}
