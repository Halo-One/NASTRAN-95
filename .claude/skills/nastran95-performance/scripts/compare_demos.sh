#!/usr/bin/env bash
# compare_demos.sh DIR_A DIR_B
#
# For each demo run_demos.sh put in both directories: whether the two
# print files are the same line for line (compare_prints.sh: clocks,
# dates and UIM 3028's decomposition statistics left out), the exit
# codes, and the wall times. Prints one line per demo that differs and a
# summary; the exit status is the number of demos that differ.
set -u
a="$1"; b="$2"
here="$(dirname "$(readlink -f "$0")")"
nd=0; ns=0; ta=0; tb=0
while read -r n rca wa; do
    [ -f "$b/$n/$n.out" ] || { echo "$n: missing in $b"; nd=$((nd+1)); continue; }
    rcb=$(awk -v n="$n" '$1 == n {print $2}' "$b/status.txt")
    wb=$(awk -v n="$n" '$1 == n {print $3}' "$b/status.txt")
    ta=$(awk -v a="$ta" -v b="$wa" 'BEGIN{print a + b}'); tb=$(awk -v a="$tb" -v b="${wb:-0}" 'BEGIN{print a + b}')
    k=$(bash "$here/compare_prints.sh" "$a/$n/$n.out" "$b/$n/$n.out" 0 2>/dev/null | head -n 1 | awk "{print \$1}")
    if [ "$k" -ne 0 ] || [ "$rca" != "$rcb" ]; then
        echo "$n: $k lines differ, exit $rca / $rcb"
        nd=$((nd+1))
    else
        ns=$((ns+1))
    fi
done < "$a/status.txt"
echo "same: $ns   differ: $nd   wall (sum): $ta s / $tb s"
exit "$nd"
