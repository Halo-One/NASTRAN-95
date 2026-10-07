#!/usr/bin/env bash
# compare_prints.sh A.out B.out [N]
#
# The bit-for-bit check: diff two print files (.out / .f06) with the only
# things that legitimately differ between two runs of the same solve left
# out - the end time, the wall clock total, the module time estimate, and
# the date in every page header (a run that crosses midnight, or a
# comparison against yesterday's baseline, differs on every page), and
# UIM 3028's decomposition statistics (B, BBAR, C, CBAR, R), which vary from
# run to run in the unmodified solver itself, and the RESTART line a
# checkpointing run prints (its last field is the time of day), and in a
# fatal run's data-base dump the addresses (IDBBAS, IDBADR, IBASBF: they
# move with the executable's layout) and the scratch directory's process
# id. grep -a:
# a few NASA prints hold a stray binary byte, and plain grep then prints
# "binary file matches" instead of the lines. - NASA demo d10022a through
# the same executable six times, with and without address randomisation,
# printed two different sets, and nothing else in the print moved.
# Prints the number of differing lines and the first N (default 20) of
# the diff. 0 means every printed number agrees.
a="$1"; b="$2"; n="${3:-20}"
filt() { tr -d '\r' < "$1" | grep -a -v -E 'END TIME|WALL CLOCK|TIME ESTIMATE|^ +DATE: |MESSAGE 3028 +B =|^ +C = +[0-9]+ +CBAR =|^ +R = +[0-9]+ *$|^[0 ] +RESTART  |^ (IDBBAS|MAXBLK|IBASBF) =' |
         sed -E 's#/ [A-Z]{3} [ 0-9]{2}, [0-9]{2} / PAGE#/ DATE / PAGE#; s#/n95_[0-9]+/#/n95_PID/#'; }
d=$(diff <(filt "$a") <(filt "$b"))
echo "$(printf '%s\n' "$d" | grep -c '^[<>]') differing lines ($(wc -l < "$a") and $(wc -l < "$b") lines)"
[ -n "$d" ] && printf '%s\n' "$d" | head -n "$n"
exit 0
