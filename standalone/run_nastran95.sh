#!/bin/sh
# ---------------------------------------------------------------------------
# run_nastran95.sh - run a deck through nastran95, which lives next to this
# file. NASTRAN/run_nastran95.bat is the same thing for Windows.
#
# The executable is the whole solver: nothing to install, no other files, no
# environment to set. Copy nastran95 anywhere and `nastran95 deck.dat` works.
# This wrapper is only for convenience:
#
#   run_nastran95.sh deck.dat [output_dir]
#       -> deck.out and deck.log are written next to the deck, or in
#          output_dir when one is given
#
# Exit code: 0 ran to END OF JOB with no fatal message, 1 bad arguments or a
# missing file, 2 no END OF JOB banner, 3 a USER or SYSTEM FATAL MESSAGE.
# ---------------------------------------------------------------------------

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exe="$here/nastran95"

if [ ! -x "$exe" ]; then
    echo "run_nastran95.sh: nastran95 is not next to this file in $here" >&2
    echo "  build it with: bash $here/build/build_nastran95.sh" >&2
    exit 1
fi

if [ $# -eq 0 ]; then
    "$exe" --help
    echo
    echo "Usage:  run_nastran95.sh deck.dat [output_dir]"
    exit 1
fi

exec "$exe" "$@"
