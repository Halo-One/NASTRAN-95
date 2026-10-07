#!/usr/bin/env bash
# run_timed.sh EXE DECK OUTDIR [SECONDS]
#
# Run one deck through nastran95ase (the SOL 145 driver when it has
# several subcases, or a single run) with the module logs unbuffered, and
# record:
#   OUTDIR/timing.txt    exit code and wall seconds
#   OUTDIR/time.txt      /usr/bin/time -v (user, system, peak memory)
#   OUTDIR/console.txt   the terminal output
#   OUTDIR/modules.txt   every SECONDS (default 2) the module each log
#                        is in, one line per log: epoch, log, last BEGN/END
#
# The log's own time columns are no use for this: the CPU column is the
# process's SYSTEM time (NASA's CPUTIM) and the elapsed one rounds to the
# second at best. The sampled module line, stamped with the wall clock,
# is what says where the time went (modules_summary.sh reads it).
#
# Everything the caller exports (OMP_NUM_THREADS, N95_* switches) reaches
# the solver and, through the driver, its children. Linux (GNU time);
# run_timed.ps1 is the Windows twin.
set -u
exe="$1"; deck="$2"; out="$3"; dt="${4:-2}"
mkdir -p "$out"
export GFORTRAN_UNBUFFERED_ALL=y
export N95_TIMEOUT="${N95_TIMEOUT:-0}"

t0=$(date +%s.%N)
/usr/bin/time -v "$exe" "$deck" "$out" > "$out/console.txt" 2> "$out/time.txt" &
tpid=$!

# the sampler: the logs are <stem>.log (a single run) or s<subcase>/
# <stem>_s<subcase>.log (the driver's children); it stops when the run
# does (the PID of the time process, not a pattern - a pattern also
# matches the shell that started this)
: > "$out/modules.txt"
while kill -0 "$tpid" 2>/dev/null; do
    ts=$(date +%s.%N | cut -c1-14)
    for lg in "$out"/*.log "$out"/s*/*.log; do
        [ -f "$lg" ] || continue
        last=$(grep -E ' (BEGN|END) *$' "$lg" | tail -n 1 | awk '{print $(NF-2), $(NF-1), $NF}')
        [ -n "$last" ] && echo "$ts ${lg#"$out"/} $last" >> "$out/modules.txt"
    done
    sleep "$dt"
done
wait "$tpid"; rc=$?
t1=$(date +%s.%N)
echo "exit $rc wall $(awk -v a="$t1" -v b="$t0" 'BEGIN{print a - b}') s" | tee "$out/timing.txt"
