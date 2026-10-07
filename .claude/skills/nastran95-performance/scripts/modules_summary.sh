#!/usr/bin/env bash
# modules_summary.sh OUTDIR [MODULE ...]
#
# From run_timed.sh's modules.txt: for each log (each child of the SOL 145
# driver), the wall second, counted from the first sample, at which each
# named module was first seen, and the last sample. Default modules: the
# ones a flutter run spends its time in.
#
#   AMG  the doublet lattice (GEND's rows)     AMP  the aerodynamic solve
#   FA1  the PK loops                          EXIT the end of the run
#
# Resolution is the sampling interval; a module shorter than that may not
# show at all.
out="$1"; shift
mods="${*:-READ AMG AMP FA1 FA2 SDR2 EXIT}"
awk -v mods="$mods" '
    NR == 1 { t0 = $1 }
    { log_ = $2; t = $1 - t0; logs[log_] = 1; last[log_] = t
      for (i = 3; i <= NF; i++) if (index(" " mods " ", " " $i " ") && !((log_, $i) in seen)) seen[log_, $i] = t }
    END {
      n = split(mods, m, " ")
      for (l in logs) {
        line = sprintf("%-50s", l)
        for (i = 1; i <= n; i++) line = line sprintf(" %s@%s", m[i], ((l, m[i]) in seen) ? sprintf("%.0f", seen[l, m[i]]) : "-")
        print line sprintf("  last@%.0f", last[l])
      }
    }' "$out/modules.txt" | sort
