/* HALO: what the Fortran main program calls.
 *
 * One function. It reads the MSC deck, writes the COSMIC deck beside
 * the print file so that anyone can read what was actually solved, and
 * puts the messages in a file of their own as well as on the terminal.
 */
#include "msc.h"
#include "mscopt.h"
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

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
        int is200;
        if (msc_read(in, &d)) { msc_msg_close(); return 1; }
        is200 = (d.sol == 200);
        msc_free(&d);
        if (is200) {
            msc_msg(MSC_INFO, 9400, "SOL 200: design optimisation. The analyses "
                    "are run one by one below.");
            msc_msg_close();
            if (rf) *rf = 200;
            return 200;
        }
    }

    rc = msc_translate(in, out, &st);

    if (rc == 0) {
        msc_msg(MSC_INFO, 9001,
            "SOL %d became rigid format %d (APP %s). %d cards written, %d\n"
            "dropped, %d ids renumbered, %d degrees of freedom constrained\n"
            "for want of stiffness. The translated deck is %s.",
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
    rc = msc_opt_run(full, ".", stem, exe);
    msc_tally_print();
    msc_msg_summary();
    msc_msg_close();
    fprintf(stderr, "nastran: %s -> %s.out\n", stem, stem);
    return rc ? 3 : 0;
}
