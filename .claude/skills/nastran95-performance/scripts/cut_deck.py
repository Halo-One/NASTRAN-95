"""cut_deck.py IN OUT --points N [--mark I,J,...]

Cut a translated SOL 145 deck - the COSMIC deck the front end writes,
<stem>_n95.dat, or a SOL 145 driver child's s<subcase>/<stem>_s<subcase>.dat
(fixed 8-column small field) - to its first N matched points, for timing
and bit-for-bit checks that take a minute instead of the full run: every
FLFACT list keeps its first N values. With --mark, the matched points I,
J, ... (1-based, after the cut) get a negative velocity in every FLUTTER
card's velocity list - the request for the eigenvectors of those loops,
which is what exercises the PK eigenvector print and the physical vector
recovery (FA1PKV, MODACC/DDR1/SDR2/OFP) as the marked decks do.

Run the result as a child is run (scripts/run_sampled.sh does this):

    N95_CHILD=1 nastran95ase --cosmic OUT.dat <outdir>

Continuations are rewritten with tags of their own (+Qnnnnn), unique across
the deck: a tag used twice is UFM 208, 'previous card is a duplicate
parent', and the run stops in the IFP.
"""
import argparse


def split_cards(lines):
    cards, cur = [], None
    for ln in lines:
        body = ln.rstrip('\r\n')
        cont = body[:1] in ('+', '*') or (body[:8].strip() == '' and cur is not None and body.strip())
        if cont and cur is not None:
            cur.append(body)
        else:
            if cur is not None:
                cards.append(cur)
            cur = [body]
    if cur is not None:
        cards.append(cur)
    return cards


def fields(card):
    """name, then the data fields 2..9 of every line (field 10 is the tag)"""
    out = []
    for i, ln in enumerate(card):
        ln = ln.ljust(80)
        f = [ln[j:j + 8] for j in range(0, 80, 8)]
        if i == 0:
            out.append(f[0])
        out.extend(f[1:9])
    return out


class Tags:
    n = 0

    @classmethod
    def next(cls):
        cls.n += 1
        return '+Q%05d' % cls.n


def fit8(v):
    """a number spelled in at most 8 columns (a negated 8-character
    velocity would otherwise lose its last digit to the field edge)"""
    if len(v) <= 8:
        return v
    x = float(v.replace('D', 'E'))
    for digits in range(7, 0, -1):
        s = ('%.*g' % (digits, x)).replace('e+0', '+').replace('e-0', '-').replace('e+', '+').replace('e', '')
        if len(s) <= 8:
            return s
    raise ValueError(v)


def write_card(name, data):
    lines, row = [], name.ljust(8)
    for i, v in enumerate(data):
        if i and i % 8 == 0:
            tag = Tags.next()
            lines.append(row + tag)
            row = tag.ljust(8)
        row += v.strip().rjust(8)[:8] if v.strip() else ' ' * 8
    lines.append(row)
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--points', type=int, required=True)
    ap.add_argument('--mark', default='')
    a = ap.parse_args()
    marks = [int(x) for x in a.mark.split(',') if x.strip()]

    text = open(a.src, newline='').read()
    eol = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(eol)
    i0 = next(i for i, l in enumerate(lines) if l.upper().startswith('BEGIN BULK'))
    head, cards = lines[:i0 + 1], split_cards(lines[i0 + 1:])

    # the velocity lists: field 6 of every FLUTTER card (SID, METHOD,
    # DENS, MACH, RFREQ/VEL, IMETH, NVALUE, EPS)
    vel_ids = set()
    for c in cards:
        if c[0][:8].strip().upper() == 'FLUTTER':
            vel_ids.add(fields(c)[5].strip())

    out = list(head)
    for c in cards:
        name = c[0][:8].strip().upper()
        if name == 'FLFACT':
            f = fields(c)
            sid = f[1].strip()
            vals = [v.strip() for v in f[2:] if v.strip()][:a.points]
            if sid in vel_ids:
                for m in marks:
                    if 1 <= m <= len(vals) and not vals[m - 1].startswith('-'):
                        vals[m - 1] = fit8('-' + vals[m - 1])
            out += write_card('FLFACT', [sid] + vals)
        else:
            out += c
    open(a.dst, 'w', newline='').write(eol.join(out))


main()
