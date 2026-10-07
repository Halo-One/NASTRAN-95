#!/usr/bin/env bash
# run_sampled.sh EXE DECK OUTDIR [SECONDS]
#
# Run a translated (COSMIC) deck as one of the SOL 145 driver's children
# would - nastran95ase --cosmic, N95_CHILD=1 for the child's open-core
# split - and sample its MAIN thread from /proc every SECONDS (default
# 0.5): state, user and system jiffies (100 per second), the thread count,
# and the module the log is in. Writes OUTDIR/samples.txt, then prints a
# per-module table of wall seconds and main-thread user / system seconds.
#
# This is the profile to trust when the threads make gprof lie: a module
# whose main thread shows user + system ~ wall is serial work on the
# critical path; system time there is GINO I/O (a k-squared FWDREC walk
# showed up as exactly that); state S with little CPU is the main thread
# waiting on its workers. Linux only (/proc).
#
# EXE, DECK and OUTDIR may be relative: they are made absolute before the
# script changes into OUTDIR.
set -u
exe="$(readlink -f "$(command -v "$1" || echo "$1")")"; deck="$(readlink -f "$2")"; out="$3"; dt="${4:-0.5}"
mkdir -p "$out"; out="$(readlink -f "$out")"
cd "$out" || exit 1
N95_TIMEOUT="${N95_TIMEOUT:-0}" N95_CHILD=1 GFORTRAN_UNBUFFERED_ALL=y \
    "$exe" --cosmic "$deck" "$out" > console.txt 2>&1 &
pid=$!
log="$out/$(basename "${deck%.*}").log"
: > samples.txt
while [ -d "/proc/$pid" ]; do
    read -r -a m < "/proc/$pid/task/$pid/stat" 2>/dev/null || break
    read -r -a p < "/proc/$pid/stat" 2>/dev/null || break
    mod=$(grep -E ' (BEGN|END) *$' "$log" 2>/dev/null | tail -n 1 | awk '{print $(NF-2), $(NF-1)}')
    echo "$(date +%s.%N | cut -c1-14) ${m[2]} ${m[13]} ${m[14]} ${p[19]} $mod" >> samples.txt
    sleep "$dt"
done
wait "$pid"
# the table: consecutive samples in the same module, merged; the module
# is the last word of the log line, which for a sub-step (MMA1 inside
# AMP) is the sub-step's, so read it with the sequence in mind
awk 'NR == 1 { t0 = $1; u0 = $3; s0 = $4 }
     { mod = $NF; if (mod != cur) { if (cur != "") printf "%-8s %7.1f s wall  user %6.1f  sys %6.1f\n", cur, $1 - ts, ($3 - us) / 100, ($4 - ss) / 100
                                   cur = mod; ts = $1; us = $3; ss = $4 } }
     END { printf "%-8s %7.1f s wall  user %6.1f  sys %6.1f\n", cur, $1 - ts, ($3 - us) / 100, ($4 - ss) / 100
           printf "total    %7.1f s wall  user %6.1f  sys %6.1f (main thread)\n", $1 - t0, ($3 - u0) / 100, ($4 - s0) / 100 }' samples.txt
