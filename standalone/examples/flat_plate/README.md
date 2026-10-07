# A flat plate three ways: SOL 144, 145 and 146 through nastran95ase

One wing, three aeroelastic solutions, each checking the others. The decks are in the
MSC dialect (`CQUAD4`, `PSHELL`, `EIGRL`, `PKNL`, `TRIM`, `DIVERG`, `GUST`, `TABRNDG`)
and run through `nastran95ase` in a few seconds each; the print files a Windows build of
commit 821d508 produced are in `out/`, so a new build can be diffed against them, and
`../../test/run_decks.py` runs the three and holds them to each other.

## The model

A 6061-like aluminium plate (E 70 GPa, nu 0.33, 2700 kg/m^3), chord 0.2 m along x (the
flow), span 0.5 m along y, 2 mm thick, clamped along its root at y = 0, lying in the x-y
plane. 4 x 8 `CQUAD4` on 45 grids (grid 1 + i + 5 j, i = 0..4 along the chord, j = 0..8
along the span; the tip is grids 41-45), in-plane freedoms and the drilling rotation
constrained on the `GRID` cards. One `CAERO1` doublet-lattice panel of the same 4 x 8
boxes (1001-1032) on a surface spline through every grid; the wall at the root is a
reflection plane (`SYMXZ = 1`), so the aerodynamics see a wing of aspect ratio 5. SI
units, sea level air (1.225 kg/m^3). The flight point the three decks share is 40 m/s,
q = 980 Pa, Mach 0.1.

Its six lowest modes: 6.74 Hz (first bending), 33.1 (first torsion), 41.7 (second
bending), 105.5, 117.4 and 194.2 Hz. The rigid lift slope by the doublet lattice,
CZ_alpha = 4.126 per rad, sits 3 % under Helmbold's 4.25 for aspect ratio 5 - four
chordwise boxes.

## The three decks

| deck | solution | asks for |
|---|---|---|
| `flat_plate_sol144.dat` | static aeroelastic trim and divergence | subcase 1: `TRIM = 1`, the plate at ANGLEA = 0.025 rad (what a 1 m/s vertical gust is at 40 m/s), no `SUPORT`, so every trim variable is fixed and only the elastic deflection and the loads are solved for; subcase 2: `DIVERG = 10`, the three lowest divergence dynamic pressures at Mach 0.1 |
| `flat_plate_sol145.dat` | PK flutter on matched points | `FLUTTER ... PKNL` at sea level, Mach 0.1, 10 to 110 m/s, on the three lowest modes (`PARAM,LMODES,3`) |
| `flat_plate_sol146.dat` | gust response | the harmonic response to a 1 m/s vertical gust at 40 m/s from 0.5 to 80 Hz (`GUST` with WG = 1/40, `RLOAD1` with a `TABLED1` of 1.0), and through `RANDOM` the response to a von Karman gust of 1 m/s rms and 2.5 m scale length (`TABRNDG` TYPE 1, L/V = 0.0625 s): the PSD and rms of the tip's deflection, 2 % structural damping (`TABDMP1`) |

Why `LMODES 3`: modes 4-6 (105-194 Hz) would need reduced frequencies of 7 to 12 at
10 m/s, past the `MKAERO1` list and past what four chordwise boxes resolve (about one box
per wavelength), and the PK roots found for them there carried a spurious positive
damping. The three modes kept carry the flutter mechanism and the divergence.

## Run them

From the repository root, after building (`../../build/`):

```
standalone\nastran95ase standalone\examples\flat_plate\flat_plate_sol144.dat C:\runs
standalone\nastran95ase standalone\examples\flat_plate\flat_plate_sol145.dat C:\runs
standalone\nastran95ase standalone\examples\flat_plate\flat_plate_sol146.dat C:\runs
```

(Linux: `standalone/nastran95ase ... /tmp/runs`.) Each writes `<stem>.out` (the print,
in MSC's layout), `<stem>_n95.dat` (the deck the solver actually read, in the 1970s
dialect) and `<stem>_xlat.txt` (every translation decision) into the output directory,
and ends with `nastran: <stem> -> <stem>.out` and exit code 0. Or all three with their
checks: `python standalone/test/run_decks.py --only flat_plate`.

## What they print, and how they agree

**SOL 144** (`out/flat_plate_sol144.out`). The stability derivative table
(`TRIM VARIABLE   COEFFICIENT`) has, in the ANGLEA row, CZ = 4.125980 rigid (unsplined
and splined alike: every box is splined) and 5.039527 elastic restrained, CMY = -0.98135
and -1.24719: the plate twists nose-up under load and its lift grows by 22 % at q = 980 Pa.
The unrestrained columns are `N/A`: no SUPORT. `AEROELASTIC TRIM VARIABLES` lists ANGLEA
fixed at 0.025 rad; `TRIMMED LOAD RESULTANTS` the half model's lift, 12.35 N, and the
`D I S P L A C E M E N T   V E C T O R` the tip's deflection, T3 = 1.809318E-02 m at grid
41. The `D I V E R G E N C E   S U M M A R Y` of subcase 2 has the three roots at Mach
0.1: q = 5529.3 Pa, 21578 Pa and 1.88e6 Pa. The first is the divergence dynamic pressure:
V = sqrt(2 q / rho) = 95.0 m/s at sea level. A one-mode estimate of the lift
magnification, 1/(1 - q/q_div) = 1.2154, sits beside the table's 5.0395/4.1260 = 1.2214.

**SOL 145** (`out/flat_plate_sol145.out`). Three `FLUTTER  SUMMARY` blocks, one per
root, a row per matched point (velocity, damping g, frequency, the complex eigenvalue):

| V (m/s) | bending root: g, f (Hz) | torsion root: g, f (Hz) |
|---:|---|---|
| 10 | -0.063, 6.66 | -0.0071, 32.83 |
| 40 | -0.350, 6.52 | -0.0395, 30.09 |
| 60 | -0.815, 6.11 | -0.0484, 25.61 |
| 70 | -1.533, 5.24 | -0.0003, 22.20 |
| 75 | -2.758, 3.81 | +0.0729, 20.25 |
| 80 | aperiodic (f = 0), real part -24.2 | +0.201, 18.39 |
| 95 | aperiodic, real part -0.66 | +0.800, 14.05 |
| 100 | aperiodic, real part +4.03 | +1.055, 12.67 |

The torsion root crosses g = 0 between 70 and 75 m/s, at 70.0 m/s and 22.2 Hz: the
bending-torsion flutter. The bending root's frequency falls to zero near 80 m/s and the
root splits into two real ones; the one printed crosses into the right half-plane
between 95 and 100 m/s, at 95.7 m/s. That is divergence seen by the PK method, and it
agrees with SOL 144's 95.0 m/s within 0.8 % - the PK's zero-frequency aerodynamics are
extrapolated from the smallest reduced frequency in the `MKAERO1` list, 0.001. The
second bending root stays damped throughout.

**SOL 146** (`out/flat_plate_sol146.out`). The `C O M P L E X   D I S P L A C E M E N T
V E C T O R` pages give the tip's response to the 1 m/s gust at each of the 160
frequencies: at 0.5 Hz, T3 = 1.819108E-02 - 7.6E-04 i m at grid 41. SOL 144's static
deflection at the same angle of attack is 1.809318E-02 m; the 0.54 % between them is the
quasi-static magnification below the bending mode, 1/(1 - (0.5/6.52)^2) = 1.0059 (6.52 Hz
is the PK's bending frequency at 40 m/s). The response peaks at 6.0-6.5 Hz with the
bending mode. The `X Y - O U T P U T   S U M M A R Y` for curve 41(3) gives the random
response: rms 2.084420E-02 m, N0 5.32 Hz, and the PSD table beneath it, peaking at
8.86e-5 m^2/Hz at 6.0 Hz. At every frequency that PSD is |H(f)|^2 times the `TABRNDG`
spectrum

    S(f) = 2 (L/V) sigma^2 [1 + (8/3)(1.339 omega L/V)^2] / [1 + (1.339 omega L/V)^2]^(11/6),   omega = 2 pi f,

a density per Hz that integrates to sigma^2 (for L/V = 0.0625 s and sigma = 1 m/s,
S(0.5 Hz) = 0.1310), and the rms is the square root of the PSD's integral over
frequency. `run_decks.py` checks all of this: the modes against the recorded ones, the
flutter crossing, SOL 144's divergence against SOL 145's real root (2 %), SOL 146 at
0.5 Hz against SOL 144 (1 %), the PSD against |H|^2 S(f) (1e-3), the rms against the
integral (2 %).

## Changing it

The decks are small enough to edit by hand. Things that matter: the `MKAERO1` list must
cover k = omega c / 2V for every mode kept and every velocity (the SOL 145 comment says
how far); `PARAM,LMODES` chooses the modes the flutter and gust solutions carry, the
`EIGRL` count only how many the modes run finds; a SOL 146 run takes one `PARAM,Q` and
one `PARAM,MACH`, so a second flight point is a second deck; `PARAM,GUSTAERO,-1` is MSC's
spelling for "compute the gust aerodynamics" and the front end flips its sign for
NASTRAN-95 (the user's manual, `doc/nastran95ase_users_manual.md` §8). The translation
log `<stem>_xlat.txt` says what became of every card.
