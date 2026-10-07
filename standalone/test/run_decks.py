#!/usr/bin/env python3
"""run_decks.py - run the standalone test decks through nastran95ase and check
what they print. No MATLAB and no other solver needed.

    python standalone/test/run_decks.py                 the executable beside this folder's parent
    python standalone/test/run_decks.py --exe PATH      a candidate (a -NoInstall build)
    python standalone/test/run_decks.py --out DIR       keep the runs there (default: a temporary
                                                        directory, removed unless --keep)
    python standalone/test/run_decks.py --only flat_plate two_subcase

Every deck in test/decks and examples/flat_plate runs to exit 0 with END OF JOB
and no FATAL message; then the numbers:

  * the flat plate's three solutions against each other and against the
    recorded modes: SOL 144's divergence dynamic pressure against the speed
    where SOL 145's bending root, by then aperiodic, crosses to a positive
    real part; SOL 146's lowest-frequency gust response against SOL 144's
    static deflection at the same angle of attack; the gust PSD at the tip
    against |H|^2 times the von Karman spectrum of the TABRNDG; the rms
    against the integral of the PSD; the flutter crossing against the one
    recorded;
  * the typical section's divergence against K / (S c CMY_alpha), its own
    rigid pitching moment slope;
  * the Aeroelastic guide's HA144A (derivatives, trim), HA144B (every
    divergence root) and HA145C (strip-theory divergence speed) against
    Simcenter Nastran 2606's prints of the same decks in test/data, and
    HA144B with WKK = 2 I, where the two codes part by design (Simcenter's
    DIVERG leaves WKK out; nastran95ase applies it);
  * the small free-free aircraft's 1 g trims: the aerodynamic lift is the
    weight in both subcases;
  * the two-subcase flutter deck through the SOL 145 driver, cold and
    restarted off its checkpointed modes run, the same flutter summary.

Exit code 0 when every deck ran and every check passed; the table says which
did not. About a minute.
"""
import argparse
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent            # standalone/test
STANDALONE = HERE.parent                          # standalone
DECKS = HERE / "decks"
DATA = HERE / "data"
FLAT_PLATE = STANDALONE / "examples" / "flat_plate"

NUM = r"[-+]?\d*\.?\d+(?:[EeDd][-+]?\d+)?"


def fnum(s):
    return float(s.replace("D", "E").replace("d", "e"))


# --- running a deck ------------------------------------------------------------

class Run:
    def __init__(self, name, rc, seconds, print_file, console):
        self.name = name
        self.rc = rc
        self.seconds = seconds
        self.print_file = print_file
        self.console = console
        self.text = print_file.read_text(errors="replace") if print_file.is_file() else ""

    @property
    def ok(self):
        return (self.rc == 0 and self.text and "END OF JOB" in self.text
                and "FATAL" not in self.text)

    def why(self):
        if self.rc != 0:
            return f"exit code {self.rc}"
        if not self.text:
            return "no print file"
        if "END OF JOB" not in self.text:
            return "no END OF JOB"
        if "FATAL" in self.text:
            line = next(l for l in self.text.splitlines() if "FATAL" in l)
            return line.strip()
        return ""


def run_deck(exe, deck, out_dir, *keywords, name=None, env_extra=None):
    name = name or deck.stem
    out_dir.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("N95_TIMEOUT", "5")           # minutes; nothing here takes more than seconds
    env.update(env_extra or {})
    start = time.time()
    proc = subprocess.run([str(exe), str(deck), str(out_dir), *keywords],
                          capture_output=True, text=True, env=env, errors="replace")
    seconds = time.time() - start
    return Run(name, proc.returncode, seconds, out_dir / (deck.stem + ".out"),
               proc.stdout + proc.stderr)


# --- readers for the print file (the MSC layout nastran95ase and Simcenter both print)

def real_eigenvalues(text):
    """the CYCLES column of the first R E A L   E I G E N V A L U E S table"""
    rows = []
    on = False
    for line in text.splitlines():
        if "R E A L   E I G E N V A L U E S" in line:
            on = True
            continue
        if on:
            m = re.match(rf"^\s+(\d+)\s+(\d+)\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s*$", line)
            if m:
                rows.append(fnum(m.group(5)))
            elif rows:
                break
    return rows


def flutter_summary(text):
    """{(case, point): [(kfreq, density, mach, velocity, damping, frequency, re, im), ...]},
    the FLUTTER SUMMARY of a PK run; the driver's joined print has one set of
    points per subcase, each numbered from 1, so a point number that does not
    increase starts the next case"""
    points = {}
    point = None
    case = -1
    last = 0
    for line in text.splitlines():
        m = re.search(r"POINT =\s+(\d+)\s+METHOD = PK", line)
        if m:
            n = int(m.group(1))
            if n <= last:
                case += 1
            elif case < 0:
                case = 0
            last = n
            point = (case, n)
            points.setdefault(point, [])
            continue
        if point is None:
            continue
        m = re.match(rf"^\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s+({NUM})\s*$", line)
        if m:
            v = [fnum(g) for g in m.groups()]
            points[point].append((v[0], v[2], v[3], v[4], v[5], v[6], v[7], v[8]))
    return points


def derivative_tables(text):
    """the stability derivative tables (TRIM VARIABLE / COEFFICIENT / six columns),
    in print order: [{'mach', 'q', 'rows': {(label, coef): [6 floats or None]}}]"""
    tables = []
    mach = q = None
    table = None
    label = None
    for line in text.splitlines():
        m = re.search(rf"MACH =\s*({NUM})\s+Q =\s*({NUM})", line)
        if m:
            mach, q = fnum(m.group(1)), fnum(m.group(2))
        if "TRIM VARIABLE" in line and "COEFFICIENT" in line:
            # a header while a table is open is the same table continued on a
            # new page (Simcenter repeats it); a header after a table ended is
            # the next subcase's
            if table is None:
                table = {"mach": mach, "q": q, "rows": {}}
                tables.append(table)
            label = None
            continue
        if table is None:
            continue
        m = re.match(rf"^\s+(?:(\S.*?)\s+)?(CX|CY|CZ|CMX|CMY|CMZ)\s+((?:(?:{NUM}|N/A)\s+){{5}}(?:{NUM}|N/A))\s*$", line)
        if m:
            if m.group(1):
                label = m.group(1).strip()
            values = [None if t == "N/A" else fnum(t) for t in m.group(3).split()]
            table["rows"][(label, m.group(2))] = values
        elif any(mark in line for mark in END_OF_TABLE):
            table = None        # the next table (page headers inside a table are let through)
    return tables


# a table ends at the next one's heading, not at a page break: nastran95ase
# paginates every 60 lines, and a table may be cut anywhere by the page header
# (1 TITLE ... PAGE n), the subtitle and the SUBCASE line
END_OF_TABLE = ("TRIMMED LOAD", "HINGE MOMENT", "AEROELASTIC TRIM VARIABLES", "AEROSTATIC DATA RECOVERY",
                "D I V E R G E N C E", "D I S P L A C E M E N T", "C O M P L E X", "END OF JOB")


def trim_variables(text):
    """[{label: value}] per AEROELASTIC TRIM VARIABLES table"""
    tables = []
    table = None
    for line in text.splitlines():
        if "AEROELASTIC TRIM VARIABLES" in line:
            table = {}
            tables.append(table)
            continue
        if table is None:
            continue
        m = re.match(rf"^\s+(?:\d+\s+)?([A-Z][A-Z0-9_.]*)\s+(RIGID BODY|CONTROL SURFACE)\s+(FIXED|FREE|LINKED)\s+({NUM})", line)
        if m:
            table[m.group(1)] = fnum(m.group(4))
        elif any(mark in line for mark in END_OF_TABLE if mark != "AEROELASTIC TRIM VARIABLES"):
            table = None
    return [t for t in tables if t]


def divergence_summaries(text):
    """[{'mach': m, 'q': [...], 'p': [complex, ...]}] per D I V E R G E N C E summary"""
    out = []
    cur = None
    for line in text.splitlines():
        if "D I V E R G E N C E" in line:
            cur = {"mach": None, "q": [], "p": []}
            out.append(cur)
            continue
        if cur is None:
            continue
        m = re.search(rf"MACH NUMBER\s*=\s*({NUM})", line)
        if m:
            cur["mach"] = fnum(m.group(1))
            continue
        m = re.match(rf"^\s+(\d+)\s+({NUM})\s+({NUM})\s+({NUM})\s*$", line)
        if m:
            cur["q"].append(fnum(m.group(2)))
            cur["p"].append(complex(fnum(m.group(3)), fnum(m.group(4))))
        elif any(mark in line for mark in END_OF_TABLE if mark != "D I V E R G E N C E"):
            cur = None
    return out


def load_resultants(text):
    """the AERODYNAMIC (ELASTIC) row of each TRIMMED LOAD RESULTANTS block: [[FX..MZ]]"""
    rows = []
    for line in text.splitlines():
        m = re.match(rf"^\s+AERODYNAMIC \(ELASTIC\)\s+((?:{NUM}\s+){{5}}{NUM})\s*$", line)
        if m:
            rows.append([fnum(t) for t in m.group(1).split()])
    return rows


def static_displacement(text, point):
    """T1..R3 of a point in the first D I S P L A C E M E N T   V E C T O R table"""
    on = False
    for line in text.splitlines():
        if "D I S P L A C E M E N T   V E C T O R" in line:
            on = True
            continue
        if on:
            m = re.match(rf"^\s+(\d+)\s+G\s+((?:{NUM}\s+){{5}}{NUM})", line)
            if m and int(m.group(1)) == point:
                return [fnum(t) for t in m.group(2).split()]
    return None


def complex_displacement(text, point):
    """{frequency: [T1..R3 as complex]} of a point's SORT2 COMPLEX DISPLACEMENT VECTOR pages"""
    out = {}
    on = False
    pending = None
    for line in text.splitlines():
        m = re.search(r"POINT-ID =\s+(\d+)", line)
        if m:
            on = int(m.group(1)) == point
            continue
        if not on:
            continue
        m = re.match(rf"^0\s+({NUM})\s+G\s+((?:{NUM}\s+){{5}}{NUM})\s*$", line)
        if m:
            pending = (fnum(m.group(1)), [fnum(t) for t in m.group(2).split()])
            continue
        if pending:
            m = re.match(rf"^\s+((?:{NUM}\s+){{5}}{NUM})\s*$", line)
            if m:
                imag = [fnum(t) for t in m.group(1).split()]
                out[pending[0]] = [complex(r, i) for r, i in zip(pending[1], imag)]
            pending = None
    return out


def xy_psdf(text):
    """{(point, component): {'rms', 'n0', 'f': [...], 'psd': [...]}} from the XY output"""
    curves = {}
    rms = n0 = None
    key = None
    for line in text.splitlines():
        m = re.search(rf"ROOT MEAN SQUARE VALUE =\s+({NUM})", line)
        if m:
            rms = fnum(m.group(1))
            continue
        m = re.search(rf"ZERO CROSSINGS \(N ZERO\) =\s+({NUM})", line)
        if m:
            n0 = fnum(m.group(1))
            continue
        m = re.search(r"CURVE\s+(\d+)\(\s*(\d+)\)", line)
        if m and rms is not None:
            k = (int(m.group(1)), int(m.group(2)))
            curves.setdefault(k, {"f": [], "psd": []}).update(rms=rms, n0=n0)
            rms = n0 = None
            continue
        m = re.search(r"CURVE\s+ID =\s+(\d+)\s+COMPONENT =\s+(\d+)", line)
        if m:
            key = (int(m.group(1)), int(m.group(2)))
            curves.setdefault(key, {"f": [], "psd": []})
            continue
        if key:
            m = re.match(rf"^\s+(\d+)\s+({NUM})\s+({NUM})\s*$", line)
            if m:
                curves[key]["f"].append(fnum(m.group(2)))
                curves[key]["psd"].append(fnum(m.group(3)))
    return curves


# --- the checks -----------------------------------------------------------------

class Checks:
    def __init__(self):
        self.rows = []

    def check(self, name, ok, detail=""):
        self.rows.append((name, bool(ok), detail))
        return ok

    def close(self, name, got, want, rel=0.0, abs_=0.0, unit=""):
        diff = abs(got - want)
        tol = rel * abs(want) + abs_
        return self.check(name, diff <= tol,
                          f"{got:.6g}{unit} against {want:.6g}{unit} ({diff / abs(want) * 100:.3g} %)"
                          if want else f"{got:.6g} against {want:.6g}")

    def note(self, name, detail):
        self.rows.append((name, None, detail))

    @property
    def failed(self):
        return [r for r in self.rows if r[1] is False]

    def print(self):
        width = max(len(r[0]) for r in self.rows) if self.rows else 10
        for name, ok, detail in self.rows:
            tag = "    " if ok is None else ("ok  " if ok else "FAIL")
            print(f"  {tag} {name.ljust(width)}  {detail}")


def interpolate_zero(xs, ys):
    """the first x where y crosses from negative to positive, linear between points"""
    for (x0, y0), (x1, y1) in zip(zip(xs, ys), zip(xs[1:], ys[1:])):
        if y0 <= 0 < y1:
            return x0 + (x1 - x0) * (-y0) / (y1 - y0)
    return None


def von_karman_psd(f, l_over_v, sigma):
    """TABRNDG TYPE 1: the von Karman gust spectrum per Hz, which integrates to sigma^2"""
    x = 1.339 * 2 * math.pi * f * l_over_v
    return 2 * l_over_v * sigma ** 2 * (1 + 8.0 / 3.0 * x * x) / (1 + x * x) ** (11.0 / 6.0)


def check_flat_plate(c, r144, r145, r146):
    # the recorded modes (this fork, both platforms print them to every digit)
    modes = real_eigenvalues(r145.text)
    recorded = [6.735380, 33.07752, 41.72403, 105.4510, 117.4024, 194.2191]
    c.check("flat plate: six modes", len(modes) >= 6, f"{len(modes)} found")
    for i, (got, want) in enumerate(zip(modes, recorded), 1):
        c.close(f"flat plate: mode {i}", got, want, rel=1e-4, unit=" Hz")

    # SOL 145: the roots over velocity. Bending (6.7 Hz at rest) goes aperiodic
    # near 80 m/s and its real root crosses zero at the divergence speed;
    # torsion (33 Hz) couples with bending and crosses to flutter
    rho = 1.225
    fs = flutter_summary(r145.text)
    c.check("flat plate SOL 145: three roots", len(fs) == 3, f"{len(fs)} points")
    by_f0 = {round(rows[0][5]): rows for rows in fs.values()}
    bending = min(fs.values(), key=lambda rows: abs(rows[0][5] - 6.7))
    torsion = min(fs.values(), key=lambda rows: abs(rows[0][5] - 33.0))
    vel = [r[3] for r in torsion]
    v_flutter = interpolate_zero(vel, [r[4] for r in torsion])
    f_flutter = None
    if v_flutter is not None:
        i = max(i for i, v in enumerate(vel) if v <= v_flutter)
        f0, f1 = torsion[i][5], torsion[i + 1][5]
        f_flutter = f0 + (f1 - f0) * (v_flutter - vel[i]) / (vel[i + 1] - vel[i])
    c.check("flat plate SOL 145: flutter crossing", v_flutter is not None and abs(v_flutter - 70.0) <= 0.7,
            f"torsion root g = 0 at {v_flutter:.1f} m/s, {f_flutter:.1f} Hz (recorded 70.0 m/s, 22.2 Hz)"
            if v_flutter else "no crossing")
    aperiodic = [r for r in bending if r[5] == 0.0]
    v_div_pk = interpolate_zero([r[3] for r in aperiodic], [r[6] for r in aperiodic]) if aperiodic else None

    # SOL 144: the divergence dynamic pressure, as a speed at sea level
    div = divergence_summaries(r144.text)
    c.check("flat plate SOL 144: divergence summary", len(div) == 1 and len(div[0]["q"]) >= 1,
            f"{len(div)} summaries")
    if div and div[0]["q"]:
        q_div = div[0]["q"][0]
        v_div = math.sqrt(2 * q_div / rho)
        if v_div_pk is None:
            c.check("flat plate: divergence, SOL 144 against SOL 145", False, "SOL 145's bending root never went aperiodic")
        else:
            c.close("flat plate: divergence, SOL 144 against SOL 145", v_div_pk, v_div, rel=0.02, unit=" m/s")
            c.note("", f"SOL 144: q_div = {q_div:.1f} Pa = {v_div:.1f} m/s; SOL 145: the real root crosses zero at {v_div_pk:.1f} m/s"
                   " (its zero-frequency aerodynamics extrapolated from k = 0.001)")

    # SOL 144's trim against SOL 146's lowest frequency: a 1 m/s gust at 40 m/s
    # is an angle of attack of 0.025 rad, and at 0.5 Hz the plate is quasi-static
    tables = derivative_tables(r144.text)
    u144 = static_displacement(r144.text, 41)
    h146 = complex_displacement(r146.text, 41)
    c.check("flat plate SOL 144: derivative table", bool(tables) and ("ANGLEA", "CZ") in tables[0]["rows"], "")
    c.check("flat plate SOL 146: tip response", bool(h146), f"{len(h146)} frequencies at grid 41")
    if u144 and h146:
        f_low = min(h146)
        got = h146[f_low][2].real
        c.close("flat plate: tip T3, SOL 146 at 0.5 Hz against SOL 144", got, u144[2], rel=0.01, unit=" m")
        f1 = bending[0][5] if bending else 6.7
        c.note("", f"the quasi-static magnification at {f_low} Hz below the {f1:.2f} Hz bending mode is "
                   f"1/(1 - (f/f1)^2) = {1 / (1 - (f_low / f1) ** 2):.4f}")
    if tables and ("ANGLEA", "CZ") in tables[0]["rows"] and div and div[0]["q"]:
        cz = tables[0]["rows"][("ANGLEA", "CZ")]
        q = tables[0]["q"]
        c.note("flat plate SOL 144: CZ_alpha",
               f"rigid {cz[1]:.4f}, elastic restrained {cz[2]:.4f}: a ratio of {cz[2] / cz[1]:.4f} at q = {q:.0f} Pa, "
               f"against 1/(1 - q/q_div) = {1 / (1 - q / div[0]['q'][0]):.4f} for a single divergence mode")

    # SOL 146's random response: the PSD is |H|^2 times the gust spectrum, and
    # the rms the square root of its integral over frequency
    curves = xy_psdf(r146.text)
    tip = curves.get((41, 3))
    c.check("flat plate SOL 146: PSDF curve at the tip", tip is not None and len(tip["f"]) > 100,
            f"{len(tip['f']) if tip else 0} frequencies")
    if tip and h146:
        worst = 0.0
        peak = max(tip["psd"])
        for f, psd in zip(tip["f"], tip["psd"]):
            if psd < 1e-6 * peak:
                continue
            h = h146.get(f) or h146.get(min(h146, key=lambda x: abs(x - f)))
            want = abs(h[2]) ** 2 * von_karman_psd(f, 0.0625, 1.0)
            worst = max(worst, abs(psd - want) / want)
        c.check("flat plate SOL 146: PSD = |H|^2 x von Karman", worst <= 1e-3,
                f"worst relative difference {worst:.2e} over {len(tip['f'])} frequencies")
        integral = sum(0.5 * (p0 + p1) * (f1 - f0) for (f0, p0), (f1, p1)
                       in zip(zip(tip["f"], tip["psd"]), zip(tip["f"][1:], tip["psd"][1:])))
        c.close("flat plate SOL 146: rms against the PSD's integral", tip["rms"], math.sqrt(integral), rel=0.02, unit=" m")
        f_peak = tip["f"][tip["psd"].index(peak)]
        c.note("", f"rms {tip['rms']:.4e} m, N0 {tip['n0']:.3f} Hz, the PSD's peak at {f_peak} Hz "
                   f"(SOL 145's bending root at 40 m/s: {bending[3][5]:.2f} Hz, g = {bending[3][4]:.3f})"
               if len(bending) > 3 else "")


def check_typical_section(c, run):
    tables = derivative_tables(run.text)
    div = divergence_summaries(run.text)
    ok = bool(tables) and ("ANGLEA", "CMY") in tables[0]["rows"] and len(div) == 2
    c.check("typical section: tables", ok, f"{len(tables)} derivative tables, {len(div)} divergence summaries")
    if not ok:
        return
    cmy_alpha = tables[0]["rows"][("ANGLEA", "CMY")][1]       # rigid splined, about the axis
    K, S, chord = 1000.0, 4.0, 1.0
    q_d = K / (S * chord * cmy_alpha)
    c.close("typical section: q_div = K / (S c CMY_alpha)", div[0]["q"][0], q_d, rel=2e-6, unit=" Pa")
    c.check("typical section: p = i sqrt(q)", abs(div[0]["p"][0] - 1j * math.sqrt(div[0]["q"][0])) <= 1e-6 * div[0]["q"][0] ** 0.5, "")
    c.check("typical section: compressibility lowers it", div[1]["mach"] == 0.5 and div[1]["q"][0] < div[0]["q"][0],
            f"{div[1]['q'][0]:.6g} Pa at Mach 0.5 against {div[0]['q'][0]:.6g} at Mach 0")
    restrained = tables[0]["rows"][("ANGLEA", "CMY")][2]
    c.close("typical section: restrained CMY_alpha = rigid / (1 - q/q_div)", restrained,
            cmy_alpha / (1 - 1.0 / div[0]["q"][0]), rel=1e-5)


def check_guide_examples(c, runs):
    # HA144A: the derivative tables of the two subcases and the trim against
    # Simcenter's print of the same deck (the guide's Table 6-4 to 1e-6)
    sim = (DATA / "ha144a_simcenter2606.f06").read_text(errors="replace")
    mine, theirs = derivative_tables(runs["ha144a"].text), derivative_tables(sim)
    mine = [t for t in mine if t["q"]]
    theirs = [t for t in theirs if t["q"]]
    c.check("HA144A: two derivative tables, q = 40 and 1200", [t["q"] for t in mine] == [40.0, 1200.0] and len(theirs) == 2,
            f"mine {[t['q'] for t in mine]}, Simcenter's {[t['q'] for t in theirs]}")
    worst = 0.0
    for a, b in zip(mine, theirs):
        for var in ("REF. COEFF.", "ANGLEA", "PITCH", "URDD3", "URDD5", "ELEV"):
            for coef in ("CZ", "CMY"):
                got, want = a["rows"].get((var, coef)), b["rows"].get((var, coef))
                if got is None or want is None:
                    c.check(f"HA144A: {var} {coef}", False, "row missing")
                    continue
                scale = max(abs(w) for w in want if w is not None) or 1.0
                for g, w in zip(got, want):
                    if (g is None) != (w is None):
                        c.check(f"HA144A: {var} {coef}", False, "N/A pattern differs")
                    elif g is not None:
                        worst = max(worst, abs(g - w) / scale)
    c.check("HA144A: CZ and CMY rows against Simcenter", worst <= 1.5e-6,
            f"worst {worst:.2e} of each row's largest coefficient")
    tv, tv_sim = trim_variables(runs["ha144a"].text), trim_variables(sim)
    c.check("HA144A: two trims", len(tv) == 2 and len(tv_sim) == 2, f"{len(tv)} mine, {len(tv_sim)} Simcenter's")
    for k, (a, b) in enumerate(zip(tv, tv_sim), 1):
        for var in ("ANGLEA", "ELEV"):
            c.close(f"HA144A: subcase {k} {var} against Simcenter", a[var], b[var], rel=1e-6, unit=" rad")

    # HA144B's divergence request (the guide's listing 5-1): every root
    sim = (DATA / "ha144b_diverg_symmetric_simcenter2606.f06").read_text(errors="replace")
    d, d_sim = divergence_summaries(runs["ha144b_diverg_symmetric"].text), divergence_summaries(sim)
    ok = len(d) == 1 and len(d_sim) == 1 and len(d[0]["q"]) == len(d_sim[0]["q"])
    c.check("HA144B: divergence roots", ok, f"{len(d[0]['q']) if d else 0} roots, Simcenter {len(d_sim[0]['q']) if d_sim else 0}")
    if ok:
        worst = max(abs(a - b) / abs(b) for a, b in zip(sorted(d[0]["q"]), sorted(d_sim[0]["q"])))
        c.check("HA144B: divergence roots against Simcenter", worst <= 3e-6, f"worst {worst:.2e} relative; "
                f"lowest {d[0]['q'][0]:.6g} psi")

    # HA145C: the BAH wing by strip theory, the static divergence speed
    sim = (DATA / "ha145c_diverg_simcenter2606.f06").read_text(errors="replace")
    d, d_sim = divergence_summaries(runs["ha145c_diverg"].text), divergence_summaries(sim)
    ok = len(d) == 1 and len(d_sim) == 1 and len(d[0]["q"]) >= 3 and len(d_sim[0]["q"]) >= 3
    c.check("HA145C: divergence roots", ok, "")
    if ok:
        rho = 1.1468e-7
        v = [math.sqrt(2 * q / rho) / 12 for q in sorted(d[0]["q"])]
        v_sim = [math.sqrt(2 * q / rho) / 12 for q in sorted(d_sim[0]["q"])]
        c.close("HA145C: divergence speed against Simcenter", v[0], v_sim[0], abs_=0.5, unit=" ft/s")
        c.check("HA145C: the next two roots", all(abs(a - b) <= 1e-3 * b for a, b in zip(v[1:3], v_sim[1:3])),
                f"{v[1]:.0f}, {v[2]:.0f} ft/s against {v_sim[1]:.0f}, {v_sim[2]:.0f}")

    # HA144B with WKK = 2 I: the roll trim agrees (both codes weight the trim's
    # box forces); the divergence does not, by design - Simcenter's DIVERG
    # leaves WKK out, nastran95ase applies it, so its roots are half
    sim = (DATA / "ha144b_trim_diverg_wkk2_simcenter2606.f06").read_text(errors="replace")
    run = runs["ha144b_trim_diverg_wkk2"]
    tv, tv_sim = trim_variables(run.text), trim_variables(sim)
    ok = bool(tv) and bool(tv_sim) and "ROLL" in tv[0] and "ROLL" in tv_sim[0]
    c.check("HA144B WKK=2I: roll trim read", ok, "")
    if ok:
        c.close("HA144B WKK=2I: roll trim against Simcenter", tv[0]["ROLL"], tv_sim[0]["ROLL"], rel=2e-6)
    d, d_sim = divergence_summaries(run.text), divergence_summaries(sim)
    ok = len(d) == 1 and len(d_sim) == 1 and len(d[0]["q"]) == len(d_sim[0]["q"])
    c.check("HA144B WKK=2I: divergence roots", ok, "")
    if ok:
        worst = max(abs(a - b / 2) / abs(b / 2) for a, b in zip(sorted(d[0]["q"]), sorted(d_sim[0]["q"])))
        c.check("HA144B WKK=2I: roots are half of Simcenter's (WKK applied, by design)", worst <= 5e-6,
                f"worst {worst:.2e} relative")


def check_small_aircraft(c, run):
    rows = load_resultants(run.text)
    weight = 20.9 / 0.101937                      # kg at the deck's g (1 / PARAM AUNITS)
    c.check("small aircraft: two trims", len(rows) == 2, f"{len(rows)} load resultant blocks")
    for k, row in enumerate(rows, 1):
        c.close(f"small aircraft: subcase {k} aerodynamic lift = weight", row[2], weight, rel=1e-4, unit=" N")


def check_two_subcase(c, cold, restart):
    a, b = flutter_summary(cold.text), flutter_summary(restart.text)
    c.check("two-subcase flutter: 12 roots in the joined print (6 per subcase)", len(a) == 12, f"{len(a)} points")
    same = a.keys() == b.keys() and all(
        len(a[k]) == len(b[k]) and all(
            abs(x - y) <= 1e-6 * max(abs(x), abs(y), 1e-30) for ra, rb in zip(a[k], b[k]) for x, y in zip(ra, rb))
        for k in a)
    c.check("two-subcase flutter: the restart prints the cold run's flutter summary", same, "")
    c.check("two-subcase flutter: the modes run left its checkpoint",
            (restart.print_file.parent.parent / "modes" / "two_subcase_modes.nptp").is_file(), "")


# --- main ---------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="nastran95ase to test (default: the one in standalone/)")
    ap.add_argument("--out", help="directory for the runs (default: a temporary one)")
    ap.add_argument("--keep", action="store_true", help="keep the temporary directory")
    ap.add_argument("--only", nargs="*", help="run the decks whose stem contains any of these")
    args = ap.parse_args()

    exe = Path(args.exe) if args.exe else STANDALONE / ("nastran95ase.exe" if os.name == "nt" else "nastran95ase")
    if not exe.is_file():
        sys.exit(f"run_decks.py: {exe} is not there; build it (standalone/build) or name one with --exe")
    version = subprocess.run([str(exe), "--version"], capture_output=True, text=True, errors="replace")
    print((version.stdout + version.stderr).strip().splitlines()[0] if (version.stdout + version.stderr).strip() else exe)

    if args.out:
        out = Path(args.out).resolve()
        out.mkdir(parents=True, exist_ok=True)
        keep = True
    else:
        out = Path(tempfile.mkdtemp(prefix="n95_decks_"))
        keep = args.keep
    print(f"runs in {out}")

    def wanted(stem):
        return not args.only or any(s in stem for s in args.only)

    runs = {}
    failed_runs = []

    def go(deck, *keywords, sub=None, name=None, env_extra=None):
        name = name or deck.stem
        if not wanted(name):
            return None
        r = run_deck(exe, deck, out / (sub or name), *keywords, name=name, env_extra=env_extra)
        runs[name] = r
        (out / (sub or name) / "console.txt").write_text(r.console)
        print(f"  {'ok  ' if r.ok else 'FAIL'} {name:40s} {r.seconds:5.1f} s  {r.why()}")
        if not r.ok:
            failed_runs.append(r)
        return r

    print("decks:")
    for deck in sorted((DECKS / "aeroelastic_guide_examples").glob("*.dat")):
        go(deck)
    go(DECKS / "divergence_typical_section.dat")
    go(DECKS / "trim_small_aircraft.dat")
    go(DECKS / "plate_flutter.dat")
    cold = go(DECKS / "two_subcase_flutter.dat", name="two_subcase_flutter")
    modes = go(DECKS / "two_subcase_modes.dat", "scr=no", sub="two_subcase/modes", name="two_subcase_modes")
    restart = None
    if modes and modes.ok:
        # restart=<the modes deck> optp=<where its checkpoint tape and dictionary are>
        restart = go(DECKS / "two_subcase_flutter.dat", f"restart={DECKS / 'two_subcase_modes.dat'}",
                     f"optp={out / 'two_subcase' / 'modes'}", sub="two_subcase/restart",
                     name="two_subcase_flutter restarted")
    r144 = go(FLAT_PLATE / "flat_plate_sol144.dat")
    r145 = go(FLAT_PLATE / "flat_plate_sol145.dat")
    r146 = go(FLAT_PLATE / "flat_plate_sol146.dat")

    c = Checks()
    print("checks:")
    if r144 and r145 and r146 and all(r.ok for r in (r144, r145, r146)):
        check_flat_plate(c, r144, r145, r146)
    if "divergence_typical_section" in runs and runs["divergence_typical_section"].ok:
        check_typical_section(c, runs["divergence_typical_section"])
    guide = ("ha144a", "ha144b_diverg_symmetric", "ha145c_diverg", "ha144b_trim_diverg_wkk2")
    if all(g in runs and runs[g].ok for g in guide):
        check_guide_examples(c, runs)
    if "trim_small_aircraft" in runs and runs["trim_small_aircraft"].ok:
        check_small_aircraft(c, runs["trim_small_aircraft"])
    if cold and restart and cold.ok and restart.ok:
        check_two_subcase(c, cold, restart)
    c.print()

    n_fail = len(failed_runs) + len(c.failed)
    print(f"{len(runs)} decks, {len(failed_runs)} failed; {len([r for r in c.rows if r[1] is not None])} checks, "
          f"{len(c.failed)} failed")
    if not keep:
        shutil.rmtree(out, ignore_errors=True)
    sys.exit(1 if n_fail else 0)


if __name__ == "__main__":
    main()
