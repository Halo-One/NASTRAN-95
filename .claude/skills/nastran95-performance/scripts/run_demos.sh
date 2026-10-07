#!/usr/bin/env bash
# run_demos.sh EXE OUTDIR [JOBS] [PATTERN]
#
# Run NASA's demonstration decks (the repository's inp/<name>.inp that
# have a NASA print in demoout/) through one executable, JOBS at a time
# (default 16), each in its own directory OUTDIR/<name>/, with the
# watchdog at 3 minutes. PATTERN (a shell glob, default *) narrows the
# set. Writes OUTDIR/<name>/<name>.out and .log, and OUTDIR/status.txt
# (name, exit code, wall seconds).
#
# EXE is the COSMIC executable: standalone/nastran95, or a CMake tree's
# bin/nastran. Not nastran95ase, which reads a deck in MSC's dialect, and
# not the repository's bin/nastran, NASA's 1995 csh launcher.
#
# The decks are those of the checkout this script sits in (four levels
# above scripts/); NASTRAN_FORK=<checkout> takes another checkout's.
#
# Two builds' OUTDIRs are compared by compare_demos.sh - that, and not
# NASA's 1995 prints, is the test of an optimised build: the same source
# at -O0 against the same source optimised, line for line.
set -u
here="$(dirname "$(readlink -f "$0")")"
exe="$(readlink -f "$(command -v "$1" || echo "$1")")"; out="$2"; jobs="${3:-16}"; pat="${4:-*}"
fork="${NASTRAN_FORK:-$(readlink -f "$here/../../../..")}"
[ -d "$fork/inp" ] && [ -d "$fork/demoout" ] || { echo "run_demos.sh: no inp/ and demoout/ in $fork (set NASTRAN_FORK)" >&2; exit 1; }
mkdir -p "$out"; out="$(readlink -f "$out")"
: > "$out/status.txt"
export exe out fork
ls "$fork"/demoout/$pat.out 2>/dev/null | xargs -n 1 basename | sed 's/\.out$//' |
xargs -P "$jobs" -I{} bash -c '
    n={}; d="$out/$n"; mkdir -p "$d"
    cp "$fork/inp/$n.inp" "$d/"
    t0=$(date +%s.%N)
    ( cd "$d" && N95_TIMEOUT=3 TMPDIR="$d" timeout 400 "$exe" "$n.inp" "$d" > console.txt 2>&1 )
    rc=$?
    t1=$(date +%s.%N)
    echo "$n $rc $(awk -v a="$t1" -v b="$t0" "BEGIN{print a - b}")" >> "$out/status.txt"
'
sort -o "$out/status.txt" "$out/status.txt"
echo "$(wc -l < "$out/status.txt") decks, $(awk '$2 != 0' "$out/status.txt" | wc -l) with a non-zero exit"
