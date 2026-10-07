"""numdiff.py A B - for prints that are NOT expected to agree bit for bit: the largest relative difference between the numbers of
two print files, their lines aligned by their text with the numbers masked
(difflib), page headers, blank lines and the out-of-core decomposition's
UIM 3027/3028 messages (which the in-core solve does not print) removed.
The relative difference of two numbers is taken against the largest
magnitude on their line, so that a near-zero entry beside large ones does
not count as a large relative change."""
import re, sys, difflib
num = re.compile(r'[-+]?\d*\.\d+(?:E[-+]?\d+)?')
def lines(p):
    out, skip = [], 0
    for ln in open(p, errors='replace').read().replace('\r', '').split('\n'):
        if skip: skip -= 1; continue
        if 'MESSAGE 3027' in ln or 'MESSAGE 3028' in ln: skip = 1; continue
        if re.match(r'^\s+R = +\d+\s*$', ln) or 'PAGE' in ln or 'DATE' in ln or 'TIME' in ln: continue
        if not ln.strip(): continue
        out.append(ln)
    return out
a, b = lines(sys.argv[1]), lines(sys.argv[2])
ka, kb = [num.sub('#', l) for l in a], [num.sub('#', l) for l in b]
sm = difflib.SequenceMatcher(None, ka, kb, autojunk=False)
worst, where, n, npairs = 0.0, '', 0, 0
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag != 'equal': continue
    for i, j in zip(range(i1, i2), range(j1, j2)):
        xa = [float(x) for x in num.findall(a[i])]; xb = [float(x) for x in num.findall(b[j])]
        if not xa: continue
        npairs += 1
        scale = max(max(abs(x) for x in xa), max(abs(x) for x in xb))
        if scale == 0: continue
        for u, v in zip(xa, xb):
            n += 1
            r = abs(u - v) / scale
            if r > worst: worst, where = r, a[i].strip()[:100] + '  |  ' + b[j].strip()[:100]
unmatched = sum(i2 - i1 for t, i1, i2, j1, j2 in sm.get_opcodes() if t != 'equal')
print(f'{len(a)}/{len(b)} lines, {npairs} numeric lines aligned ({unmatched} unaligned), {n} numbers; largest difference {worst:.2e} of the line\'s largest value')
if where: print('  at:', where)
