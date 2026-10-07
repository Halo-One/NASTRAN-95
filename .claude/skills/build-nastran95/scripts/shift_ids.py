#!/usr/bin/env python3
"""shift_ids.py - the bit-for-bit check of nastran95ase's id renumbering.

NASTRAN-95 holds a grid or element id in 24 bits, so the front end
(msc/mscxlat.c, renumber_pass) moves every id over 16,777,215 onto a block
that ends there, in the ids' own order. A deck whose ids are all under the
limit and a copy with some of them shifted over it - every reference moved
by the same amount, the order of the grids kept - must then solve to the
same bits. This script makes the copy and compares the two print files.

  shift   rewrite free-field (comma) cards in place: every field after the
          card name that is an integer in one of the ranges moves by --by.
          Comment lines and lines without a comma (fixed-field cards) are
          left as they are, so the ids to move must be on free-field cards
  compare diff two print files after undoing the shift in the second, the
          sorted bulk echo and the UIM 3113 line of the ELAS2 elements left
          out (the springs a CBUSH becomes are numbered below the
          renumbered block, so their ids, and hence the echo's order and
          that line, differ by design); prints the count and the first
          differences, exit 1 when any

Keep the order: shift every id range that sits between the ones you want
over the limit and the next id above them (for example point masses at
6000000.. AND bungee anchors at 8000000.., under an aero reference grid
99999999), or NASTRAN sequences the grids differently and the comparison
shows round-off, not a renumbering fault. CAERO1 box ids are not
renumbered by the front end: leave the aero box block alone.

shift rewrites the files it is given, so give it a copy of the deck and
every file the deck INCLUDEs, run nastran95ase on both decks, then compare:

  python3 shift_ids.py shift   --ranges 6000000:6300000,8000000:9000000 \\
          --by 60000000 shifted/<deck>.dat shifted/<its INCLUDE files>
  python3 shift_ids.py compare --ranges 6000000:6300000,8000000:9000000 \\
          --by 60000000 orig/<stem>.out shifted/<stem>.out

The skill build-nastran95 (the SKILL.md above this folder, "Ids up to
99,999,999") and the fork's HALO.md say what the check is for and what it
showed.
"""
import argparse
import difflib
import re
import sys


def parse_ranges(text):
    out = []
    for part in text.split(','):
        lo, hi = part.split(':')
        out.append((int(lo), int(hi)))
    return out


def shift(args):
    ranges = parse_ranges(args.ranges)
    moved = 0
    for path in args.files:
        with open(path) as f:
            lines = f.readlines()
        out = []
        for line in lines:
            if line.startswith('$') or ',' not in line:
                out.append(line)
                continue
            toks = line.rstrip('\n').split(',')
            for i, t in enumerate(toks):
                s = t.strip()
                if i > 0 and s.isdigit() and \
                        any(lo <= int(s) < hi for lo, hi in ranges):
                    toks[i] = str(int(s) + args.by)
                    moved += 1
            out.append(','.join(toks) + '\n')
        with open(path, 'w') as f:
            f.writelines(out)
    print(f'{moved} fields shifted by {args.by} in {len(args.files)} files')


def compare(args):
    ranges = [(lo + args.by, hi + args.by) for lo, hi in parse_ranges(args.ranges)]

    def back(m):
        v = int(m.group(0))
        if any(lo <= v < hi for lo, hi in ranges):
            s = str(v - args.by)
            return ' ' * (len(m.group(0)) - len(s)) + s
        return m.group(0)

    ignore = re.compile(args.ignore) if args.ignore else None

    def keep(line):
        # the sorted echo: its count ends at column 21, '-' and 8 blanks
        if len(line) > 30 and line[21:30] == '-        ':
            return False
        return not (ignore and ignore.search(line))

    with open(args.a, errors='replace') as f:
        a = [l.rstrip('\n') for l in f if keep(l)]
    with open(args.b, errors='replace') as f:
        b = [re.sub(r'\b\d{7,8}\b', back, l.rstrip('\n')) for l in f if keep(l)]
    if len(a) == len(b):
        # the usual case, and the only fast one on a flutter print (11
        # million lines; difflib takes hours): line against line
        diff = []
        for x, y in zip(a, b):
            if x != y:
                diff += ['-' + x, '+' + y]
    else:
        diff = [l for l in difflib.unified_diff(a, b, lineterm='', n=0)
                if not l.startswith(('---', '+++', '@@'))]
    print(f'{len(a)} and {len(b)} lines outside the echo; {len(diff)} differ')
    for l in diff[:args.show]:
        print(l[:150])
    return 1 if diff else 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = p.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('shift')
    s.add_argument('--ranges', required=True, help='lo:hi,lo:hi (hi excluded)')
    s.add_argument('--by', type=int, required=True)
    s.add_argument('files', nargs='+')
    c = sub.add_parser('compare')
    c.add_argument('--ranges', required=True, help='the ranges given to shift')
    c.add_argument('--by', type=int, required=True)
    c.add_argument('--show', type=int, default=30)
    c.add_argument('--ignore', default=r'ELAS2 +ELEMENTS .*STARTING WITH ID|END TIME:',
                   help='regex of lines left out (default: UIM 3113 naming the '
                        'first CBUSH spring, numbered below the block by design, '
                        'and the clock)')
    c.add_argument('a', help='print file of the original deck')
    c.add_argument('b', help='print file of the shifted deck')
    args = p.parse_args()
    if args.cmd == 'shift':
        shift(args)
        return 0
    return compare(args)


if __name__ == '__main__':
    sys.exit(main())
