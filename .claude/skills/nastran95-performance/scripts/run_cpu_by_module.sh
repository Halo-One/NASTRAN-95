#!/usr/bin/env bash
# run_cpu_by_module.sh EXE DECK OUTDIR [SECONDS]
#
# Run a deck through nastran95ase (the SOL 145 driver, or a single run)
# and every SECONDS (default 0.5) read, for every nastran95ase process of
# the run, its whole-process user and system jiffies (all threads) and
# the module its log is in. Then print, per child and per module, the
# wall seconds and the CPU seconds (user + system over all threads) spent
# there, and the machine-wide total per module.
#
# This answers "where does the CPU go" when the children share the
# machine: wall time on the critical path is one question, the CPU the
# run burns (and so what the other children are denied) is another.
# Linux only (/proc); run_cpu_by_module.ps1 is the Windows twin.
#
# The driver starts its children from its own executable, so they are
# found by the executable's file name (the kernel's process name: the
# first 15 characters of it), whatever the build under test is called.
set -u
exe="$1"; deck="$2"; out="$3"; dt="${4:-0.5}"
name="$(basename "$exe")"; name="${name:0:15}"
mkdir -p "$out"
export GFORTRAN_UNBUFFERED_ALL=y
export N95_TIMEOUT="${N95_TIMEOUT:-0}"
t0=$(date +%s.%N)
"$exe" "$deck" "$out" > "$out/console.txt" 2>&1 &
top=$!
: > "$out/cpu_samples.txt"
while kill -0 "$top" 2>/dev/null; do
    ts=$(date +%s.%N | cut -c1-14)
    for pid in $top $(pgrep -P "$top" -x "$name"); do
        read -r -a p < "/proc/$pid/stat" 2>/dev/null || continue
        # the child's deck is the 3rd word of its command line
        dk=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | awk '{print $3}')
        lg="${out}/${dk%.*}.log"
        [ "$pid" = "$top" ] && lg="$out/$(basename "${deck%.*}").log"
        for c in "$lg" "$out"/*/"$(basename "${dk%.*}").log"; do
            [ -f "$c" ] && { lg="$c"; break; }
        done
        # the DMAP module (a line with its statement number), not a
        # sub-step inside it (MMA3 1 inside AMP)
        mod=$(grep -E ' [0-9]+ +[A-Z][A-Z0-9]* +(BEGN|END) *$' "$lg" 2>/dev/null | tail -n 1 | awk '{print $(NF-1)}')
        echo "$ts $pid $(basename "${dk:-driver}") ${p[13]} ${p[14]} ${mod:-?}" >> "$out/cpu_samples.txt"
    done
    sleep "$dt"
done
wait "$top"; rc=$?
t1=$(date +%s.%N)
echo "exit $rc wall $(awk -v a="$t1" -v b="$t0" 'BEGIN{print a - b}') s" | tee "$out/timing.txt"
awk '{ k = $2; if (k in lt) { dw = $1 - lt[k]; dc = ($4 + $5 - lc[k]) / 100
                               wall[nm[k] " " lm[k]] += dw; cpu[nm[k] " " lm[k]] += dc
                               mw[lm[k]] += dw; mc[lm[k]] += dc }
       lt[k] = $1; lc[k] = $4 + $5; lm[k] = $6; nm[k] = $3 }
     END { for (x in wall) printf "%-48s wall %7.1f  cpu %8.1f\n", x, wall[x], cpu[x]
           for (m in mc) printf "ALL %-8s cpu %8.1f\n", m, mc[m] }' "$out/cpu_samples.txt" | sort
