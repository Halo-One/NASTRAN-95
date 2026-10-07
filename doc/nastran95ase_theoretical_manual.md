# nastran95ase Theoretical Manual

What the numbers in a `nastran95ase` print file and its OUTPUT4 export mean: which NASA
rigid formats run, how the doublet-lattice aerodynamics, the generalized matrices, the
splines, the PK flutter solution, the modes and the gust response are computed in this
fork, what the fork changed in them, and what the results have been checked against. It
is compiled from the fork's `HALO.md`, the sources in `mis/`, `msc/`, `rf/` and
`bin/nastrn.f.in`, the VehicleDesign skills `nastran-analysis` (with its distillation of
NASA's User's Manual Volume II, `references/users-manual-vol2-flutter-gust-modes.md`,
cited below as "User's Manual Vol. II"), `nastran-flutter`, `nastran95-performance` and
`build-nastran95`. Every mechanism names the routine that does it. Where the sources are
silent the text says "not documented". The companion `nastran95ase_users_manual.md` covers
running decks.

## 1. The rigid formats used

NASTRAN-95 has no solution sequences in MSC's sense; it has rigid formats, DMAP programs
in `rf/` selected by `APP` and `SOL n,0`. The front end's map (`msc/mscexec.c`):

| MSC | rigid format | what the fork uses it for |
|---|---|---|
| SOL 101, 105, 107-112 | DISPLACEMENT 1, 5, 7-12 | statics, buckling, complex eigenvalues, frequency and transient response |
| SOL 103 | DISPLACEMENT 3 (`rf/DISP03`) | normal modes; the modes run of every flutter restart; the `.phg`/`.mgg` export |
| SOL 145 | AERO 10 (`rf/AERO10`) | modal flutter |
| SOL 146 | AERO 11 (`rf/AERO11`) | modal aeroelastic response (gust) |

Both aero formats begin with the whole of rigid format 3 in line: GP1 .. GP4, MCE/SCE/SMP,
RBMG1-4 (the `SUPORT` rigid-body partition), DPD, READ (the real modes), MTRXIN
(`K2PP`/`M2PP`/`B2PP` direct input), GKAD, GKAM (modal matrices), APD (the aerodynamic
model), GI (the spline), AMG (the aerodynamic influence matrices), AMP (the generalized
aerodynamics); then AERO 10 runs the flutter loop FA1 -> (CEAD, K method only) -> VDR ->
FA2 -> `REPT LOOPTOP,100`, the V-g plots, MODACC, ADR (aerodynamic loads) and the
physical recovery DDR1/SDR1/MPYAD/UMERGE/SDR2; AERO 11 runs FRLG, GUST, FRRD2, IFT,
MODACC, ADR, VDR/SDR1-3, XYTRAN/XYPLOT and RANDOM (User's Manual Vol. II §3, §4). COSMIC
has no file-based modal transfer: the only cross-run carry is checkpoint/restart
(section 11), and the fork's OUTPUT4 alter is the only route for matrices to ZAERO or to
the state-space plant (section 10).

`DIAG 14` prints the numbered DMAP listing (how an ALTER point is re-found), `DIAG 16` the
FEER diagnostics, `DIAG 39` traces the PK iteration from inside FA1PKA.

## 2. The structural model as the solver receives it

The translation (`msc/mscxlat.c`) changes no numbers but it changes the model's
description, and the print reflects that:

- Rigid bars and `RBE2`s are `CRIGD1` constraints (the same constraint; COSMIC wants the
  independent grid first, chosen so a `SUPORT`ed or `SPC`ed grid is never dependent).
- A coincident `CBUSH` is six `CELAS2` springs, one per non-zero stiffness; a separated one
  is a `CONROD` with K1 only, with a unit-modulus `MAT1` made for it.
- Degrees of freedom nothing is attached to are constrained (what MSC's AUTOSPC does);
  NASTRAN-95 would otherwise factor a singular stiffness and stop with UFM 3097.
- Ids above 2^24 - 1 are renumbered everywhere, `PARAM,GRDPNT` included, and put back in
  the print.
- `WTMASS`, `COUPMASS`, `GRDPNT` are applied as in MSC; `WTMASS` in EMA.
- A `SUPORT` (from `SUPORT1`) sets `REACT`: RBMG1-4 build `[D] = -[Kll]^-1[Klr]`, the
  rigid-body error ratio and `[mr]`, and READ constructs the rigid-body modes rather than
  extracting them (User's Manual Vol. II §2). The rigid-body mass matrix on the `SUPORT`
  freedoms must be non-singular (UFM 2200): with `CONM2`s carrying no rotary inertia, a
  rotation about an axis all the masses lie on has none.

The mass matrix of every repo model is only semi-definite - lumped `CONM2`s with no I11,
I22, I33 leave the rotations massless - and that is the one property of the model the
eigensolver was not written for (section 3.2).

## 3. Normal modes

### 3.1 FEER

`EIGRL` becomes `EIGR FEER` with the shift at V1, or 0.5 Hz when V1 is blank
(`do_eigrl`, `msc/mscxlat.c`): FEER ("Fast Eigenvalue Extraction Routine") is a
tridiagonal reduction (`mis/ferxtd.f`, `mis/ferxts.f`) followed by
a QR iteration on the reduced matrix (`mis/fqrwv.f`, `mis/fqrw.f`). It works in the mass
metric: trial vectors are mass-orthogonalised and the norm that goes under the square
root at label 480 of `FERXTD` is the mass norm of the new trial vector. A zero shift on a
free-free model whose rigid-body modes are only nearly zero loses every mode; the
manual's "small positive internal shift" and up to three shifts exist to remove stiffness
singularities, and the user shift is given in Hz and converted to radians squared (User's
Manual Vol. II §2; skill `build-nastran95`). FEER returns more roots than asked (a reduced
problem of twice the size); the print rewrite and the `.phg` writer cut to the number
requested. Modes are numbered by algebraic eigenvalue, and with `MASS` normalisation the
generalized stiffness equals the eigenvalue, which is what `read_khh` reads off the
`REAL EIGENVALUES` table. The summary `OEIGS` gives the reason for termination: 0 normal,
1 fewer than requested, 3 the problem size was reduced; a mode whose theoretical error
bound exceeds the `EIGR` allowable (default 0.001/n) is marked FAIL and dropped - a second
way to come back short of the count. The `TIME` card is an eigensolver input in NASA's
code: an undersized `TIME` truncates the mode count with a clean `END OF JOB` (User's
Manual Vol. II §2); the translated decks carry a large `TIME` and the wall-clock watchdog
instead.

### 3.2 The reseeding and "fewer modes" (the fork's guards)

On a semi-definite mass matrix roundoff put the mass norm at -1e-17 on row 88 of 250 of
an 830-grid deck, `DSQRT` returned NaN, every test after it was a comparison a NaN fails,
and rows 88 to 250 came out NaN: the solver spun for hours. The fork (`HALO.md`, "Never
hang"):

- treats a norm that is not positive (or is NaN) as the null trial vector it is: the
  reduction reseeds through its existing restart loop (`FEER3`, capped at MORD reseeds),
  and `mis/fernpd.f` prints UWM 2394 once (`FEER TRIAL VECTOR n HAS NO POSITIVE MASS NORM
  ... THE VECTOR IS DISCARDED AND THE REDUCTION RESEEDED; IF FEWER MODES THAN REQUESTED
  FOLLOW, SEE MESSAGE 2390`). The same guard is on the two start-vector normalisations
  (labels 40 and 240) and a NaN test joins the "problem size reduced" exits (150, 310);
- writes the in-memory trial vectors (NASA's 1994 `NIDORV` in-core mode) to scratch file 7
  before every return to the reseed loop, because the reseed read them from there and
  found nothing (`I/O SUBSYSTEM ERROR NUMBER 110 ... SCRATCH7` after `END OF JOB`, exit 0,
  no modes). On a modern open core the vectors always fit, so the reseed path had never
  worked in this build;
- bounds the QR sweep (`FQRWV`/`FQRW` label 70) at 200 sweeps per eigenvalue plus 1,000,
  or a NaN in its input, with UFM 2395 and `MESAGE -37`.

What the model then gets: after a reseed on this metric the next vector's
orthogonalisation converges by a factor of 0.6 a pass and each vector after it slower
still; raising NASA's cap of 14 passes to 60 bought four rows and nothing else, so the
cap stays and the shortfall is reported (UWM 2390 gives the count). The 830-grid deck
returns 89 accurate modes of 120 in 7 seconds; MSC's Lanczos is not affected by the metric.
The translator counts the `CONM2`s without rotary inertia and warns up front (UWM 9133)
with the cure: rotary inertia on the lumped masses, or fewer modes.

### 3.3 Mass normalisation and the exported matrices

With `MASS` normalisation `phi' M phi` is the identity, which is the check VehicleDesign's
four-way test applied to the `.phg` and `.mgg` export against the weight generator
(`HALO.md`, "What the front end is checked against"): a mass matrix with one spurious entry
per empty degree of freedom still factors, still gives modes, and still looks like a mass
matrix - which is why null columns are left out of the OUTPUT4 file (section 10).

## 4. The modal matrices (GKAM)

`GKAM USETD,PHIA,,LAMA,DIT,M2DD,B2DD,K2DD,CASECC/MHH,BHH,KHH,PHIDH/NOUE/C,Y,LMODES=0/
C,Y,LFREQ=0./C,Y,HFREQ=-1.0/.../C,Y,KDAMP` (`rf/AERO10` line 422) forms the generalized
mass, damping and stiffness and the modal matrix `PHIDH` on the d-set (User's Manual
Vol. II §3):

- `[Mhh] = diag(m_i) + phi' M2dd phi`, `[Khh]`, `[Bhh]` likewise with the `K2PP`/`B2PP`
  direct input matrices the case control names (kept by the front end since 2026-09-26;
  the trim-load study's per-subcase steady-lift `K2PP`/`B2PP` DMIGs reach the children).
- The modes kept are `PARAM LMODES` (the n lowest) or `LFREQ`/`HFREQ` (a window in Hz,
  with `LMODES=0`); the DMAP defaults select nothing, so one of them is required. The
  `EIGR` count only sets how many modes READ finds.
- Damping from `TABDMP1` (`SDAMPING`): with `KDAMP` = -1 (NASTRAN-95's default)
  `b_i = m_i 2 pi f_i g(f_i)`, viscous equivalents, in `[Bhh]`; with `KDAMP` = +1
  `k_i = (1 + i g(f_i)) (2 pi f_i)^2 m_i`, complex stiffness and no B (the fork's `GKAM1A`
  does exactly that). MSC and Simcenter number it the other way round (their default +1 is
  viscous), so without the parameter both codes are viscous and the same, and an explicit
  `PARAM,KDAMP` means the opposite in the two; the front end passes it unchanged and no
  repository deck sets it.

## 5. The aerodynamics

### 5.1 APD: the aerodynamic model and the (Mach, k) list

APD builds the aerodynamic model from the `EDT` bulk data (`CAERO1`/`PAERO1` boxes,
`AERO`, `MKAERO1`/`MKAERO2`, `SPLINE1`/`SPLINE2`, `SET1`, `FLFACT`, `FLUTTER`) and emits the
`SPLINE`, `AERO`, `ACPT`, `FLIST` blocks, `NK` aero degrees of freedom, `NJ` boxes and
`BOV` = b/V from the `AERO` card's velocity field (User's Manual Vol. II §3; `HALO.md`
quotes 4,564 k-set rows against the monarch's 2,282 boxes). The (Mach, k) pairs of the `MKAERO1` and
`MKAERO2` cards are collected, sorted by Mach then k (`APDMKS`, a stable insertion sort at
the end of `mis/apd.f`) and written to the `AERO` data block once, and the list is printed
as UIM 9457. NASA wrote the pairs card by card in sorted bulk data order, and `MKAERO1`
cards of one Mach set tie on every sorted field, so a k list split over several cards came
out with the cards shuffled - the same in Simcenter, which does not sort. The flutter and
gust interpolations never cared (they interpolate over the pairs of a Mach wherever they
sit); the order matters only to a reader of the exported `QHHL`/`QHJL`/`QKHL` blocks,
which follow this list. PK roots are unchanged to round-off by the sort (`HALO.md`,
"The order of the (Mach, k) pairs").

The reduced frequency throughout is k = omega b / V with b = REFC / 2, the semichord: FA1
halves REFC itself, `BREF` in `FA1PKE` is b, and `BOV = BREF/VEL` (`mis/fa1pke.f`; skill
`nastran-flutter` §8).

### 5.2 GI: the spline

GI forms `[Gka]`, the spline from the structural a-set to the aerodynamic k-set, from the
`SPLINE1` (surface) and `SPLINE2` (beam) cards and their `SET1` lists; `GTKA` is its
d-set form used by AMP (`rf/AERO10` line 546) and `MPYAD GTKA,CPHIA,/CPHIK` puts a
structural eigenvector through it onto the boxes for the `COMPLEX EIGENVECTOR` tables
(plunge in T3, pitch in R2, in the box's frame). The spline formulas themselves are in
NASA's Theoretical Manual, not in the sources here: not documented. The front end strips
MSC's METH and USAGE fields from `SPLINE1`.

### 5.3 AMG: the doublet lattice

`AMG AERO,ACPT/AJJL,SKJ,D1JK,D2JK/NK/NJ` (`rf/AERO10` line 516) computes, for every (Mach,
k) pair, the aerodynamic influence matrix `AJJ` (boxes by boxes, complex), the integration
(area) matrix `SKJ` and the downwash matrices `D1JK` (the displacement part) and `D2JK`
(the velocity part, multiplied by ik in AMP). The lattice is built a row (receiving box)
at a time by `GEND`: for each receiving box `DPPS` sums the kernel of every sending box
(`SUBP` -> `SNPDF` the steady part, `INCRO` -> `TKER` the incremental oscillatory kernel,
`IDF1`/`IDF2` its integrals) (`mis/gendp.f` header).

**NASA's parabolic kernel (1971).** `INCRO` fits the numerators of the planar (P1) and
nonplanar (P2) incremental kernels across the sending box's doublet line by a parabola
through three points, and `TKER` approximates the kernel integrals I1, I2 with Laschka's
11 exponential terms. One arctangent is in the wrong quadrant: `IDF1`/`IDF2` take
`ATAN(2e|zeta|/(r^2 - e^2))` (Rodden 1971) without Rodden's 1972 correction, so for a
receiving point off the sending box's plane (|zeta|/e > 0.001) and within r < e of its
centre the angle belongs in the second quadrant and NASA's element is off by order one at
199 of 3,000 random geometries. A tail box lined up spanwise with a wing box a little above
or below it is that case. It is left as NASA wrote it, so the parabolic path stays NASA's
to the bit (`HALO.md`, "The quartic doublet-lattice kernel").

**The quartic kernel (`NASTRAN SYSTEM(270)=1`, `mis/dlmq.f`).** What MSC and Simcenter call
QUARTICDLM, "the new quartic formulation of the doublet lattice kernel (N5KQ)": Rodden,
Taylor and McIntosh, "Further Refinement of the Subsonic Doublet-Lattice Method", J.
Aircraft 35(5), 720-727, 1998. The numerators of P1 and P2 are fitted by a quartic through
five points (eta = -e, -e/2, 0, e/2, e), the closed-form integrals extended to match (the
paper's eqs. 15-19, 28-32 for the fit, 20-25, 33-34 for the integrals), and I1, I2
approximated with Desmarais' 12 exponential terms. Both parts are needed to reproduce
Simcenter. `INCROQ` is `INCRO` for a batch of reduced frequencies, `TKERQ` is `TKER` for a
batch with the series chosen (Laschka or Desmarais), `DLMQFT` the fit, `DLMQNP` the
nonplanar integral; double precision, no COMMON, no SAVE, the geometry, regime, logarithm
and arctangent once per element for the whole batch, the arctangent in its quadrant. The
closed forms are those DLR's PanelAero writes (BSD-3), checked against mpmath quadrature:
exact to 1e-13 where the paper's forms are exact and 1e-4 where its series stands in. One
change from PanelAero: in the coplanar limit (|zeta|/e <= 0.001) the nonplanar term takes
the series alpha of eq. 25, which the quadrature bears out; PanelAero's value there,
2e/(eta^2 - e^2), does not, and NASA's `IDF2` uses a third. It matters little, that
numerator carrying zeta and being near zero. With the Laschka series `TKERQ` reproduces
NASA's `TKER` to its single precision. `INCRO` and AMGK's `INCROK` hand over to `INCROQ`
when `N95DLQ()` is 1 (`msc/mscisa.c`); the aerodynamic cache keys carry the kernel
(`IAECV` 1 parabolic, 3 quartic). On the benchmark HALE wing the parabolic kernel alone
put the fork's flutter 3 % below Simcenter's (30.62 against 31.57 m/s). Cost on the
five-Mach monarch deck: AMG 110 CPU s parabolic, 615 quartic (five kernel evaluations in
double precision against three in single).

**How the fork computes it faster without moving a bit** (`HALO.md`, "Linux" and "The
second pass"; skill `nastran95-performance` §4): the rows are computed a block at a time on
OpenMP threads and packed in GEND's order (`mis/gendp.f`; `/DLM/` and `/KDS/` THREADPRIVATE
in TKER, INCRO, FLLD, SUBB); up to 8 pairs of one Mach are done in a batch with the steady
part, the geometry, TKER's branches, square roots and exponential and IDF1/IDF2's logarithm
and arctangent computed once and every k-dependent statement a loop over the batch
(`mis/amgk.f`, `mis/tkerv.f`: TKERV, INCROK, SUBPK, IDF1V, IDF2V, DPPSK); the kernel
routines are compiled `-O2 -fautomatic -ffp-contract=off`, with `!GCC$ NOVECTOR` on the
loops that call SIN, COS and EXP because gfortran would otherwise call glibc's libmvec,
which rounds differently (194,796 of 200,000 test geometries differed). `AJJ` dumped as AMP
reads it is byte for byte the serial NASA loop's.

### 5.4 AMP: the generalized aerodynamic matrices

`AMP AJJL,SKJ,D1JK,D2JK,GTKA,PHIDH,D1JE,D2JE,USETD,AERO/QHHL,QKHL,QHJL/NOUE/S,N,XQHHL/
V,Y,GUSTAERO=-1` (`rf/AERO10` line 546). Per (Mach, k) pair, as `mis/ampk.f`'s header
transcribes the serial path:

1. `GKI = GTKA' PHIDH` - the modes on the k-set (structure-dependent).
2. `DJH = D1JK GKI + i k D2JK GKI` - the modal downwash (`SSG2C`'s two multiply-adds with
   multipliers (1, 0) and (0, k)).
3. `QJH = (AJJ')^-1 DJH` - the modal pressures: AMPC transposes AJJ (`TRANP1`), decomposes
   the transpose out of core (`CFACTR`) and solves the NOH right-hand sides (`CFBSOR`) in
   NASA's path; the fork's `mis/ampcz.f` unpacks AJJ transposed into open core as complex
   double precision, factors it in place with partial pivoting (the fork's LU, `AMPCZB`, or
   LAPACK's ZGETRF/ZGETRS through OpenBLAS, `mis/ampczs/`) and solves the right-hand sides,
   falling back to NASA's path when there is no core, a pivot vanishes or
   `N95_INCORE_AJJ=0`.
4. `QKH = SKJ QJH` - the box forces per mode (MMA214's "A not transposed", summed over
   SKJ's columns in order).
5. `QHH = GKI' QKH` (`QIH` in the code) - the generalized aerodynamic force matrix
   (MMA104's sums in their order).

The pairs' blocks are written side by side into `QHHL` (modes by modes per pair), `QKHL`
(k-set by modes per pair) and, with `GUSTAERO` = +1, `QHJL` (the gust columns, section 9).
In the expanded form of the task statement, `QHHL = PHIDH' GTKA' SKJ (AJJ')^-1 (D1JK + ik
D2JK) GTKA PHIDH` per pair, per unit dynamic pressure: Q multiplies the modal displacements
to give the modal aerodynamic force per unit dynamic pressure (skill `nastran-flutter`
§8). The transpose on AJJ is AMPC's; whether NASA's AJJ is the pressure-to-downwash or the
downwash-to-pressure relation is not documented in the sources beyond that.

The fork's AMP changes (`HALO.md`): the pair loop is a pipeline of OpenMP tasks - a read
task per pair (its AJJ and SKJ off the files, kept open and read in order), a compute task
(DJH, the solve, QKH, QIH) and a write task (QJHL, QHHL), every GINO call chained on one
dependence in pair order, up to 16 pairs in core (`N95_AMP_SLOTS`, `N95_AMP_MB`), each
solve on one thread (ZGETRF gives the same bits on 1 thread as on 32); AJJL and SKJ are no
longer rewound and walked k-squared (`mis/ampc.f`, `mis/ampd.f`); the serial path skips
terms with an unstored (zero) factor and the pipeline keeps them, and a zero of either sign
added to a sum that started at +0 changes nothing. The OpenBLAS solve is the one change
that is not bit-identical: a blocked LU rounds differently in double precision, and where
that flips a single-precision bit of QJH a marginal PK root lands elsewhere - 108 of the
monarch print's 170,034 lines, no crossing.

**The aerodynamic cache** (`msc/mscaec.c`): what of a SOL 145 run depends on the structure
is the modes and what is built on them (GKI, DJH, QJH, QKH, QHH, FA1); what does not is
AJJ per pair (AMG) and its LU factors (AMP). With `N95_AERO_CACHE` set, `DLAMGK` in
`mis/amgk.f` keeps each pair's AJJ under a 64-bit multiply-xorshift hash of every input
word (the ACPT record, the counts, NJ, the symmetry flags, REFC, Mach, k, the kernel
word) and `AMPKC` in `mis/ampk.f` keeps the LU factors and pivots under a hash of the AJJ;
a hit goes straight to ZGETRS. Simcenter's PFAERO subDMAP makes the same split, and MSC and
ZAERO keep the aerodynamic matrices for restarts with a changed structure.

## 6. The PK flutter method as FA1 does it

`FA1 KHH,BHH,MHH,QHHL,CASECC,FLIST/FSAVE,KXHH,BXHH,MXHH/S,N,FLOOP/S,N,TSTART/S,N,NOCEAD/
V,Y,PKMATCH=0/V,Y,PKVECT=0` (`rf/AERO10` line 566). NASA's PK is in `mis/fa1.f` (the loop
list), `mis/fa1pki.f` (the interpolation tables), `mis/fa1pke.f` (the iteration),
`mis/fa1pka.f` + `HSBG` + `ATEIG` (the eigenvalues) and `mis/fa1pkv.f` (the eigenvectors);
`mis/fa2.f` prints. Everything in this section except where marked "fork" is NASA's
arithmetic.

### 6.1 The flutter loops

FA1 reads the `FLUTTER` card the `FMETHOD` selects off the `FLIST` record (`FLUT(1..10)`:
`FLUT(1)` the id, `FLUT(2)` the method, `FLUT(4)`, `FLUT(5)`, `FLUT(6)` the density, Mach
and velocity `FLFACT` ids, `FLUT(7)` `IMETH`, `FLUT(9)` `NVALUE` (`NEIW`, the roots wanted
per loop), `FLUT(10)` `EPS`; `mis/fa1.f` labels 30-97) and writes the loop list to `FSAVE`,
three words per loop: Mach, velocity, density ratio. For PK it wrote every (density, Mach,
velocity) combination, the velocity innermost, with the 100-loop stop in the DMAP (`REPT
LOOPTOP,100`, Flutter Error 3). **Fork:** for `PKNL`, or `PK` with `PARAM PKMATCH 1`
(`PKMATC` in `/BLANK/`), one loop per entry of the density and velocity lists taken
together, the Mach list walked alongside with its last entry repeated when it is shorter
(`mis/fa1.f` label 98); the ninth word of the `FSAVE` header (`REC0(9) = 1`) tells FA2 the
run was matched. Everything after that is NASA's PK untouched: a matched point is just a
loop with its own density and velocity. For PK `SMETH = 2` always: NASA's comment reads
"PK METHOD HAS LINEAR SPLINE ONLY", meaning the one-dimensional (infinite beam) spline of
section 6.2, which is cubic in k, and not a piecewise-linear interpolation; the front end's
UIM 9117 ("IMETH L, linear interpolation in k") is the same thing in looser words.

### 6.2 FA1PKI: the interpolation of Q in k

Rebuilt whenever the Mach changes (`IFLAG`). FA1PKI takes the Mach of the `QHHL` list
closest to the loop's Mach, counts its NK reduced frequencies k_i, and builds the
(NK + 1)-square matrix

    A_ij = |k_i - k_j|^3 + (k_i + k_j)^3      (i, j <= NK)
    A_i,NK+1 = A_NK+1,j = 1,  A_NK+1,NK+1 = 0

in double precision and inverts it (`INVERD`; a singular A is UFM 2427). The second term
is the image of the kernel in k = 0, which makes the fit even in k. The tables hold, for
each k_j, `Re Q(k_j)` and `Im Q(k_j) / k_j` - the imaginary parts are divided by their own
k before interpolation (`mis/fa1pki.f` label 120). At an arbitrary k, FA1PKE forms
P_i = |k - k_i|^3 + (k + k_i)^3, P_NK+1 = 1, C = A^-1 P, and

    Q_R(k) = sum_j Re Q(k_j) C_j,     Q_I(k) = sum_j [Im Q(k_j) / k_j] C_j

(`mis/fa1pke.f` labels 115-145). Q_I is thus the interpolated `Im Q / k`, not `Im Q`. The
same kernel, scaled by 1/12 and with a smoothing term, is MINTRP's `LSPLIN` of the gust path
(section 9.3).

### 6.3 FA1PKE: the state matrix and the iteration

At the start of a Mach group FA1PKE unpacks `KHH`, `BHH` (zero when absent) and `MHH` into
core and inverts `MHH` (`INVERS`; a singular `MHH` is `MESAGE -7`). For each loop (Mach,
velocity V, density ratio) it sets

    rho = RREF * ratio / 2          (RREF the reference density of the AERO card)
    BOV = b / V,   RBV = rho b V,   RVS = rho V^2

and, at the trial reduced frequency k (`KINT`, starting from 0 for the first root of each
loop), forms the real matrices (comments at `mis/fa1pke.f` label 145)

    K  = -KHH + (rho V^2 / 2) Q_R(k)         [the code's RVS * QR, rho already halved]
    B  = -BHH + (rho b V / 2) Q_I(k)         [RBV * QI, Q_I being Im Q / k]

and the 2N-square state matrix

    A = | 0        I      |
        | M^-1 K   M^-1 B |

so that, with K_eff = KHH - (rho V^2/2) Re Q and B_eff = BHH - (rho b V/2) Im Q / k, the
blocks are -M^-1 K_eff and -M^-1 B_eff of the first-order form of
`[M p^2 + B_eff p + K_eff] q = 0`. (`GMMATS` forms the products; **fork:** `FA1PKG` and
`FA1PKQ` in `mis/fa1pkq.f` form the same products and eigenvalues with the same arithmetic
in the same order, without the argument aliasing `FA1PKA`/`HSBG`/`ATEIG` are built on, at
-O3; NASA's path is kept under DIAG 39.) The eigenvalues come from Hessenberg reduction
(`HSBG`) and the QR algorithm (`ATEIG`): a 240-square QR per iteration per root on a
120-mode basis, 4-5 ms each, nine tenths of FA1's time. `ATEIG` reads one double outside
its matrix (its search for a small subdiagonal element steps past (2,1) to `A(1-IA)`), and
whether that double is below EPS decides where the QR sweep starts, which moves roots in
the sixth digit; the fork's twin keeps the read and lays each thread's arrays out as open
core is so the same bytes are there.

The 2N eigenvalues p = sigma + i omega are sorted by ascending imaginary part (`RSORT`),
those with negative imaginary part discarded, and the loop walks up the list:

- a root with omega = 0 (a real, aperiodic root) is accepted at once, with k = 0, f = 0
  and the damping `g = b sigma / (0.34657 V)` (label 200; the literal 0.34657 equals
  ln 2 / 2, and its origin is not documented);
- for an oscillatory root the test value is `RKTST = BOV * omega`, the reduced frequency
  the root implies. If `|RKTST - KINT| < EPS` (EPS from the `FLUTTER` card, 0.001 when
  blank or zero) the root is accepted (label 230): the five words kept are sigma, omega,
  k = RKTST, f = omega / 2 pi, g = 2 sigma / omega. Otherwise `KINT = RKTST` and the
  matrices are rebuilt and the QR repeated (NASA's fixed-point iteration on k, label 220);
- after an acceptance the walk continues down the same eigenvalue list at the k the last
  root converged at, so the next root's first estimate comes free and is often accepted
  without another QR (the `N95_PK_STATS` histogram's bin 0);
- at 10 iterations without convergence (`NIT = 10`, label 240) a straight line is fitted
  by least squares through the (trial k, resulting k) pairs from the second trial on and
  its fixed point `k* = -A10 / (A11 - 1)` taken, with UWM `PK METHOD FIALED TO CONVERGE,
  n ITERATIONS ON LOOP l, FOUND m, ROOTS WANTED n / LEAST SQUARES FIT APPROXIMATION
  IMPLEMENTED` (the misspelling is NASA's);
- the loop ends when `NVALUE` roots are accepted (`NROOT >= NEIW`); running out of
  eigenvalues first is a system fatal (`ERROR IN FA1PKE/@ 200`).

**Fork, the secant step (`N95_PK_SECANT=1`):** once two iterates (k_prev, k) with their
results (g_prev = k_from_root(k_prev), g = k_from_root(k)) exist, the next trial is the
secant on the residual h(k) = k_from_root(k) - k, `k_next = k - h1 (k - k_prev) / (h1 -
h0)`, reverting to NASA's step when the denominator is tiny, the step non-positive, or
more than twice NASA's (label 225; the same arithmetic in `FA1PKL`). Off by default: it
moves bits (section 12).

**Fork, the loops side by side (`mis/fa1pkp.f`):** since each loop is solved from k = 0
with nothing carried over, the loops of one Mach are independent; on the first loop of a
group `FA1PKP` solves them all, one OpenMP thread per loop (`FA1PKL`, FA1PKE's labels 100
to 300 transcribed onto arrays of their own), and FA1PKE then writes loop by loop in order
what the serial solve would have written (`FA1PKR`). `N95_PK_THREADS=1`, DIAG 39 and a
basis of fewer than four modes keep the serial solve. The print is the serial run's to
the bit, and that is how it is checked.

### 6.4 Matched points, damping and the quantities printed

Each accepted root is five words: Re p, Im p, k, f, g, appended per loop to `BXHH`
(`CLAMA` in the PK path; `mis/fa1pke.f` label 310) and copied into `FSAVE` on the last
loop. FA2 (`mis/fa2.f`) prints the `FLUTTER SUMMARY` from them, for PK reading the five
words back (label 3200): `KFREQ` = k, `1./KFREQ`, `DENSITY` (the row's own density ratio
on a matched-point run), `MACH NO.` (the row's own), `VELOCITY` = |V| / `VREF` (`PARAM
VREF`, default 1.0), `DAMPING` = g = 2 sigma / omega, `FREQUENCY` = f, and the `COMPLEX
EIGENVALUE` sigma, omega (format 1070: `F9.4` then eight `E13.6`). **Fork:** with the
ninth `FSAVE` word set, the summary is one block per root (`POINT = n METHOD = PK
(MATCHED POINTS)`, format 1043) with a row per matched point and the density and Mach in
every row, MSC's PKNL layout; COSMIC's own PK layout is one block per (Mach, density)
group. The K method's g and f are formed from the complex eigenvalue of CEAD the other way
(`VOUT = |Im| / VREF`, `F = KFREQ * Im / (pi b)`), and the KE method returns no
eigenvectors (User's Manual Vol. II §3).

g is the structural damping that would have to be added to make the root neutrally
stable; a flutter crossing is where a root's g crosses the threshold the plotter uses
(VehicleDesign's `plot_nastran_flutter`; the crossing rules, `drop_isolated` and the
two-points-above requirement are the reader's, not the solver's).

### 6.5 Root tracking (fork)

FA1 accepts the roots of a loop in ascending frequency, so POINT n of the summary is the
n-th lowest root at that speed, and where two roots cross in frequency the columns swap:
the V-g curves kink. `FA1PKT` (`mis/fa1pkt.f`) keeps every accepted root's modal
eigenvector on scratch 302 - the vector of `p^2 I + p M^-1 B + M^-1 K` at the root, with
the matrices of the reduced frequency the root converged at, solved by `EGNVCT` - and
`FA1PKU` assigns the roots of the next loop to the previous loop's places greedily over
increasing cost, the cost of pairing previous root i with current root j being
`(1 - MAC) + 0.25 x the relative distance of the eigenvalues`, MAC the modal assurance
criterion between the two PK eigenvectors; roots the previous loop did not have take the
places left, in their own order; the previous loop's vectors travel on `MXHH` (data block
204). The tracking restarts on the first loop and when the Mach changes, and is skipped
with a note when the work space is too small. The first version compared vectors alone
and never moved anything. On the reduced monarch deck it lowers the elastic roots' mean
|delta g| per step from 0.078 to 0.054 at equal frequency continuity, the same as the
plotter's re-sort after the fact, and the two agree on the order. On the full deck a third
to two thirds of the 57 roots change place at most loops, most at the low-speed end where
the aerodynamic and rigid-body roots wander.

### 6.6 Eigenvectors and the aerodynamic loads

For a marked loop (a negative `FLFACT` velocity) `FA1PKV` computes, for every accepted root,
the modal vector of the state matrix at the root's converged k (`EIGENVECTOR FROM THE PK
METHOD`: the complex coefficient of each mode of the basis) and writes it to the recovery
scratch 301; `PARAM PKVECT 1` prints it for every loop (**fork**; the file, the print and
the pass count are `FA1PKW`'s so the threaded solve can compute in a thread and write in
order). The physical vector of any point is `Phi q` with the modes run's `Phi`. The rigid
format recovers the physical complex eigenvectors of the marked loops through MODACC
(matching `OFREQUENCY`'s velocities on the imaginary part), DDR1, SDR1, `MPYAD
GTKA,CPHIA,/CPHIK` (the boxes), UMERGE and SDR2/OFP as `COMPLEX EIGENVECTOR` tables; **fork:**
AERO 10's restart tables give every statement from `LABEL VDR` to `LABEL FINIS`, the
`GO,GOD/GM,GMD` EQUIV and the PFILE PARAM the flutter cards' bits (34-40), so a restart
recovers the vectors like a direct run (`HALO.md`, "The restart tables").

ADR (`ADR CPHIH1,CASEZZ,QKHL,CLAMAL1,SPLINE,SILA,USETA/PKF/BOV/C,Y,MACH = 0.0/*FLUTTER*`,
`rf/AERO10` line 722) forms the box loads of every recovered root, `P_kf = Q_kh(k, Mach)
u_h`, interpolating `QKHL` over k (through ADRI, MINTRP and the spline of section 9.3) at
the root's reduced frequency `k = omega BOV` and at the Mach of the `MKAERO1` list closest
to `PARAM MACH` (NASA's default 0.0 took the lowest Mach for every subcase; the driver
writes each child's own). `ADRPRT` prints them as `AERODYNAMIC LOADS (UNIT DYNAMIC
PRESSURE)`: per box the real and imaginary parts of T1..T3 then R1..R3, per unit dynamic
pressure, in the box's frame. `BOV` comes from the `AERO` card's velocity, one number per
run, which is why one marked loop per subcase gives every recovered root its own k and
several marked loops give only the first loop's loads at the right k (UWM 9456). On the
two-subcase test deck the beam's in-plane modes show 1e-17 and the out-of-plane ones order
one to sixty, which is the physics.

## 7. What a SOL 145 child costs, and where the fork's time goes

One child (one Mach) runs the setup (GP1-GP4, MCE1/MCE2 with a DECOMP, READ, GKAD/GKAM,
APD, GI; about 6 s serial at -O0), AMG, AMP, FA1, FA2 and, for marked loops, the physical
recovery (OFP formatting of a 1.7-million-line print took 23 s). On the five-Mach monarch
deck at 40.5 s wall the CPU is FA1 about 650 s of 956 (nearly all NASA's QR), AMP 170, AMG
100; FA1 is bound by its own dependences and the order of its operations is the answer -
a faster QR (LAPACK's DHSEQR was tried: slower at n = 220 and 7 % of eigenvalues differing
in the last single-precision bit) would not be NASA's roots. Fewer QR solves per root is
the remaining lever (`N95_PK_SECANT`), and fewer roots (`NVALUE`) at deck level (skill
`nastran95-performance` §2, §6, §9).

## 8. The checkpoint and the restart

NASTRAN's restart tables re-execute a DMAP statement on a modified restart when a card of
one of the statement's `****CARD` bits changed (the table under `$*CARD BITS` in
`rf/AERO10`: bit 36 is FLFACT/FLUTTER, 34-40 the flutter cards together; bit 25 `MACH`,
54 `GUSTAERO`, 62 `HFREQ LFREQ LMODES KDAMP`). A rigid-format-switch restart (RF3 ->
AERO10, UIM 4145) off a `scr=no` modes run skips GP1 through READ and runs GKAM, APD, GI,
AMG, AMP, FA1, FA2 when the restart deck's own cards are aero and flutter cards only; any
other new card is a change to the structure and GP1 runs again. The modal basis off the
tape and the one a direct run's READ computes in the same job are FEER's answer to the
last bit, not to the last printed digit, so the restarted summaries agree with a direct
run's to six significant figures on the monarch (258 of 1,830 rows of a 30-mode deck differ
in the seventh digit) and digit for digit on the two-subcase test deck. An ALTERed OUTPUT4
has no restart-table entry and is not executed on a restart, so the export needs a cold
run (`HALO.md`, "The modes once"; "Known (2026-10-01)").

## 9. The gust response (AERO 11)

### 9.1 The chain

RF3 front end -> DPD (the dynamic loads table `DLT`, PSD list `PSDL`, frequency list `FRL`,
transient list `TRL`) -> READ -> GKAM -> APD -> GI -> AMG -> AMP (`QHHL`, `QKHL`, and
`QHJL` only with `GUSTAERO` = +1) -> FRLG (the direct loads in modal coordinates per
frequency; transient loads Fourier-transformed) -> GUST ("forms the loading due to gusts
and adds to the direct loads": inputs `DLT`, `FRL`, `DIT`, `QHJL`, `ACPT`, `CSTMA`, `BOV`,
`MACH`, `Q`) -> FRRD2 solving

    [ -Mhh omega^2 + i Bhh omega + Khh + q Qhh(k) ] u_h = P_h(omega)

-> IFT (the inverse Fourier transform for a transient formulation; `PARAM IFTM` 0
rectangular, 1 trapezoidal, 2 cubic spline) -> MODACC -> ADR (frequency only) -> the
recovery -> XYTRAN/XYPLOT -> RANDOM (PSD and autocorrelation from the `RANDPS` cards and
the XY requests; the RMS, the expected zero-crossing frequency N0) (User's Manual Vol. II
§4, §5). `PARAM Q` (dynamic pressure, required) and `PARAM MACH` are `C,Y` constants: one
each per execution. The gust aerodynamics are computed for `GUSTAERO` = +1 in NASTRAN-95
and -1 in MSC, so the front end flips the sign. What the `GUST` card defines (profile,
amplitude, reference velocity, lag) and whether a spanwise-varying gust exists are in
Vol. I, not on disk: not documented. The repo's PSD decks carry the von Karman spectrum as
MSC's `TABRNDG`, which the front end passes through unchanged; the 1-cos and turbulence
decks are `TLOAD`/`TABLED1` time histories through the Fourier/IFT pair.

### 9.2 The gust solve in core (fork)

AMPF solved `RJH = AJJ^-1 S(k)` per pair through `CFACTR` + `CFBSOR` after the same
k-squared file walks as AMPC; it now takes the in-core solve without the transpose
(`AMPCZN` in `mis/ampcz.f`) and keeps its file positions. NASA's AERO 11 demos d11031a and
d11032a agree with the out-of-core path to 1.4e-4 and 1e-5 of the largest value on each
line.

### 9.3 The spline in k, and why it is solved in double precision (fork)

ADRI interpolates the gust matrix `QHJ` (and ADR's `QKH`) over reduced frequency, FRD2I
interpolates `QHH`, both through MINTRP -> `LSPLIN`: for target k_d the weights of the NI
tabulated k_i are the first NI components of `A^-1 R(d)`, with

    A = | K(y_i, y_j) + DZ delta_ij   1 |     R(d) = | K(y_d, y_i) |
        | 1                           0 |            | 1           |

    K(a, b) = ( |a - b|^3 + |a + b|^3 ) / 12

- the kernel and its image in k = 0, so the fit is even in k; KY = 1, KD = 0, KT = 1, no
slope terms, one constant term (`mis/lsplnd.f` header). `LSPLIN` solved the system by
`INVERS` in single precision, and with k values close together (the monarch's list starts
0.005, 0.01, 0.02, and every value sits next to its image) the system is ill-conditioned:
the weights came out of order 1-20, oscillating, and 0.988 at a tabulated k that is a node.
Rows of `QHJ` whose imaginary part bends at low k (boxes far aft) came out up to 70 % off
at the tabulated k themselves, the gust response was jagged from frequency to frequency
above 1.5 Hz, and against Simcenter 2606 accelerations and forces were 3-24 % off in rms
and N0 up to 48 % off, while the point-force response through the smoother `QHH` was
within 0.3 %. `LSPLND` solves the same system (the same kernel, constant term and weights)
by Gaussian elimination with partial pivoting in double precision; ADRI and FRD2I switch
it on (`MINTDQ`) and FA1's K method keeps `LSPLIN`. An `MKAERO` list holding every
analysis k exactly now gives the interpolated run's answers (`HALO.md`, "SOL 146 random
response").

### 9.4 The column that stopped at row 65,536 (fork)

`UNPACK`'s first-to-last mode (the row range left to the column) ended at NASA's `LARGE =
65536`, silently closing a column there, and MPYAD method 10 (`MMA1`) reads every column
of "A" that way. ADRI's interpolation hands MINTRP `QHJL` as one column per k of boxes x
modes rows (2,282 x 57 = 130,074 on the monarch), so the interpolated `QHJK` summed to
2.5e-6 where its input summed to 7,174 and the gust loads were zero; a 1-cos run answered
only its dummy `DAREA`. The same limit zeroed ADR's `AERODYNAMIC LOADS` (`QKHL`, 4,564 x 57
rows). `LARGE` is now 2^30 (`mds/unpack.f`); any product with "A" taller than 65,536 rows
(a model past about 10,900 grids, or a flattened matrix of that height) was affected on an
older executable (`HALO.md`, "Tall columns").

## 10. OUTPUT4: the matrices exported and their conventions

`msc/mscop4.c` writes the alter after SDR1 (statement 77 of rigid format 3, where `PHIG`
and `MGG` both exist) or after AMP (statement 90 of AERO 10 and AERO 11, where `QHHL`,
`QHJL`, `QKHL`, `MHH`, `KHH`, `BHH` and `PHIDH` exist), as `OUTPUT4 <block>,,,,//-1/<unit>/2`,
and rewrites NASTRAN-95's formatted file into MSC's layout.

| block | rows x columns | meaning |
|---|---|---|
| `PHIG` (`PHG`) | g-set dof x modes (cut to the modes requested) | the real eigenvectors, `MASS`-normalised |
| `MGG`, `KGG` | g-set x g-set | the mass and stiffness, `WTMASS` applied; null columns left out as MSC does (the monarch_ff `.phg` has 150,166 non-zeros to MSC's 150,240, all exact zeros in the rigid-body columns; the 5,777 mass non-zeros match exactly) |
| `PHIA`, `MAA`, `KAA` | a-set | the same on the analysis set |
| `QHHL` (`QHH`) | n_h x (n_h x n_k) per Mach | the generalized aerodynamic force matrices of every (Mach, k) pair of the child's list, blocks side by side in the sorted order of UIM 9457, per unit dynamic pressure, complex (type 4, real/imaginary pairs) |
| `QKHL` (`QKH`) | k-set x (n_h x n_k) | the box forces per mode per pair |
| `QHJL` (`QHJ`) | gust columns per pair | the gust aerodynamics (with `GUSTAERO` = +1) |
| `MHH`, `KHH`, `BHH` | n_h x n_h | the modal mass, stiffness and damping of GKAM |
| `PHIDH` (`PHDH`) | d-set x n_h | the modal matrix on the d-set |

Conventions and checks:

- Q multiplies the modal displacements to give the modal aerodynamic force per unit
  dynamic pressure; k = omega c / 2V, b = REFC / 2. The aerodynamic stiffness enters the
  flutter equation as `K_eff = KHH - (rho V^2 / 2) Re Q`, the aerodynamic damping as
  `B_eff = BHH - (rho b V / 2) Im Q / k` (section 6.3).
- The export was checked by replaying FA1's own arithmetic on it: the cubic spline in k
  through `Re Q` and `Im Q / k`, the two matrices above, at each printed root's own reduced
  frequency, gives the printed PK roots back to 1e-4 (VehicleDesign's `test_ase_plant`).
- Simcenter's export of the same deck agrees with the fork's to 5.5 % of the matrix at low
  k (a steady offset of the plunge mode's small real part) and under 1 % at k = 3.2, once
  the modes' signs are matched (each child solves its own modes; `modal_sign_match`).
- A Simcenter deck's alter inside the AMP loop appends `QHH` once per pair, every subcase
  over again; the fork ignores the alter text and maps `QHH`/`QHJ`/`QKH` to the whole
  lists, one file per subcase (`<name>_s<subcase>.op4`), each child's `QHHL` holding its
  own Mach's blocks in that Mach's `MKAERO1` order after APD's sort.
- The file: header `NCOL NROW FORM TYPE` (4I8), name (A8), `1P,5E16.9`; FORM 1 square, 2
  rectangular, 6 symmetric (NASTRAN-95's negative form made positive); real out as type
  2, complex as type 4; per column `IC IR NW` (3I8) then NW values five to a line; the
  trailer column `NCOL+1 1 1` and one `1.0`. The three record traps in NASTRAN-95's own
  file, all from `mis/outpt4.f`: records are fixed-width (`1X,3I13` / `1X,10E13.6` single,
  `1X,3I16` / `1X,8D16.9` double) and must be sliced at the field width because adjacent
  negatives touch; `JJ` counts single-precision words, so a double column announces twice
  the values; a null column comes back with `II` zero and the previous column's words in
  the buffer and is read past and left out.
- Restart limitation: the alter is not executed on a modified restart (section 8).

## 11. Sign conventions summarised

| quantity | convention | source |
|---|---|---|
| k | omega b / V, b = REFC / 2 | `mis/fa1pke.f` (`BOV = BREF/VEL`), skill `nastran-flutter` §8 |
| g (oscillatory root) | 2 sigma / omega | `mis/fa1pke.f` label 230 |
| g (real root) | b sigma / (0.34657 V) | `mis/fa1pke.f` label 200 |
| f | omega / 2 pi | `mis/fa1pke.f` |
| VELOCITY printed | V / `PARAM VREF` | `mis/fa2.f` |
| Q | force per unit dynamic pressure on the modal displacements; `Im Q / k` is what FA1 tabulates and interpolates | `mis/fa1pki.f` |
| `GUSTAERO` | +1 computes the gust aerodynamics in NASTRAN-95, -1 in MSC/Simcenter; the front end flips it | `msc/mscxlat.c` UIM 9119 |
| `KDAMP` | -1 (default) viscous `b_i`; +1 complex stiffness in NASTRAN-95 - the opposite of MSC's numbering; passed unchanged | User's Manual Vol. II §3, fork `GKAM1A` |
| CPHIK rows | plunge in T3, pitch in R2, in the box's frame | `HALO.md` |
| AEROF | real and imaginary T1..T3, R1..R3 per box per unit dynamic pressure, at the root's k | `HALO.md`, ADRPRT |
| ids above 2^24 - 1 | renumbered in the deck, restored in the print and the OUTPUT4 names | `msc/mscxlat.c`, `msc/mscf06.c` |

## 12. The verification record

Every number here is quoted from the sources named; "line for line" means
`compare_prints.sh`'s 0 differing lines with only clock and date lines excluded.

### 12.1 The solver and the front end against NASA and MSC

| check | result | source |
|---|---|---|
| NASA's 132 demonstration decks against NASA's 1995 prints | 70 reproduce NASA's output after the `RFDIR` truncation fix (55 before); the four-way test's verdicts: 85 match, 19 nothing to compare, 18 fatal, 10 differ, none unstable since the libgfortran format-copy wrapper | `HALO.md`; skill `build-nastran95` |
| the four fluid demos (d03021a, d03031a, d07021a, d07022a) | 48 of 48 runs at twelve environment sizes complete and match NASA with the `--wrap` of `fc_strdup_notrim`; 4 of 36 failed without it | `HALO.md` |
| Linux against Windows | every number of the licence-free test the same, except demo d03031a where FEER returns 30 of NASA's 35 roots on Linux (a convergence decision on the last bit, deterministic) | skill `build-nastran95` |
| the weight generator against MSC Nastran 2025.1 | digit for digit, including `PARAM,GRDPNT` after renumbering (a missed renumbering had put the two c.g.s 0.15 m apart) | `HALO.md` |
| the repo's modal decks against MSC | flexible modes paired at MAC > 0.99 with frequencies within 1e-4 relative; `phi' M phi` the identity through the exported `.phg`/`.mgg` | skill `nastran-analysis` §6; `HALO.md` |
| SOL 200 on MSC's three-bar truss | MSC's design-cycle history within half a percent at every cycle | `HALO.md` |

### 12.2 Flutter

| check | result | source |
|---|---|---|
| NASA's doublet-lattice demos d10021a-d10023a (KE and PK, 15-degree swept wing), in-core against out-of-core solve | every flutter summary number to 1e-5 relative or better (single against double precision); the optimised kernel bit for bit with -O0 | `HALO.md` |
| the reduced monarch deck (Mach 0.10, 31 k, 30 modes solved, 20 kept) | the 30 modes agree with MSC's to every printed digit; the first flexible root's frequency and damping to three digits at every sampled point (the first `flutter_vs_msc` run) | `HALO.md`; skill `nastran-flutter` §7 |
| `K2PP`/`B2PP` steady-lift DMIGs (the trim-load study, 8 subcases x 3 points) against Simcenter 2606 | the outer-boom pair agrees at every airspeed to the printed digits; the crossing 18.037 m/s at 4.185 Hz in both; the zero-load twin gives the no-load numbers | `HALO.md` |
| the five-Mach `monarch_demo_asm1083` deck against Simcenter 2606 (benchmark records) | the lowest crossings agree within 0.1 % on the same (quartic) kernel (the VehicleDesign repo guide, 2026-09-30: crossing speeds within 0.1 %, frequencies within 0.02 %); the quartic kernel moves Mach 0.40's lowest crossing from 16.17 m/s at 38.4 Hz (a PK artefact) to 19.77 m/s at 10.70 Hz, Simcenter's root | skill `nastran-flutter` §7; skill `build-nastran95` |
| the same deck through ZAERO's g-method on the ZAERO chain's deck | the same mechanisms 1-4 % lower in EAS | VehicleDesign repo guide (`CLAUDE.md`) |
| HALE wing (Patil, Hodges, Cesnik): flutter, frequency, divergence | quartic 31.5679 m/s, 3.7329 Hz, 39.8809 m/s against Simcenter 31.5679, 3.7329, 39.8810; parabolic (both) 30.6187 / 30.6188, 3.8225, 39.8805 / 39.8806 | `HALO.md` |
| Goland wing | 155.2405 m/s, 10.9352 Hz against Simcenter 155.2401, 10.9352; parabolic 154.4496 / 154.4491, 10.9729 | `HALO.md` |
| `TKERQ` against PanelAero's kernel, 3,000 random geometries | 1.6e-7 (P1), 9.5e-6 (P2); `INCROQ` against a Python element 1.1e-4 (single-precision output) | `HALO.md` |
| the lowest crossings per Mach held by `test_nastran95ase_aeroelastic` (EAS m/s / Hz, Mach 0.10 .. 0.40) | parabolic 33.326/3.721, 29.900/3.812, 22.331/10.738, 20.116/10.782, 19.754/10.735; quartic 33.306/3.710, 29.844/3.799, 22.410/10.699, 20.152/10.741, 19.784/10.699 (0.06-0.35 % apart) | skill `nastran95-performance` §1 |
| `NVALUE` 83 of 110 against all 110 on the 3 x 3 study | the roots found in 0.6-20 Hz the same bits: 1,836 of 1,836 rows identical | skill `nastran95-performance` §4 |

### 12.3 The speed-ups, the cache, the kernels, the restart

| check | result | source |
|---|---|---|
| every speed-up but the OpenBLAS LU, the five-Mach deck (170,034 lines) | identical to the unmodified source; with `N95_AJJ_SOLVE=builtin` identical to Jon's `halo-ase-sol145` 8cd363e line for line | `HALO.md`, "Linux" |
| the OpenBLAS LU | 108 of 170,034 lines (marginal PK roots); none of the 35 crossings (29 with `drop_isolated`); on the 110-mode PKVECT deck 48 of 22,447 summary rows plus eigenvector last digits, every crossing the same | `HALO.md`; skill `nastran95-performance` |
| the aerodynamic cache, filling and hitting, against the cache off | 0 differing lines on the 110-mode PKVECT deck (10,070,885 lines) and the study deck (154,119 lines) | `HALO.md` |
| the quartic path off (`N95_DLM_QUARTIC=0`) against the build before it | 0 of 10,070,885 lines; NASA's 132 demos as before | `HALO.md` |
| the secant off against the build without it | identical, the demos too; on, serial and threaded bit-identical; the lowest crossing moves at most 0.001 m/s | `HALO.md` |
| the `UNPACK` fix | the five-Mach print identical outside the `AERODYNAMIC LOADS` pages (6,764,231 lines); the demos as before | `HALO.md` |
| the double-precision spline | of the 132 demos only d11031a and d11032a move, below 1.4e-4 of a line's largest number | `HALO.md` |
| the restart flow against the direct run | six significant figures on the monarch; digit for digit on the two-subcase deck; the speed-ups reproduce each flow exactly | `HALO.md` |
| Windows parity build against Jon's exe (63e15d64), the 110-mode deck | `N95_AJJ_SOLVE=builtin N95_DLM_QUARTIC=0`: 0 of 10,035,296 lines in 673.7 s against 1768.6 s; OpenBLAS parabolic 8 lines at 1e-21 in 305.8 s, the same crossings | skill `build-nastran95` |
| the unmodified solver's own nondeterminism | d01002a prints the GINO timing constants it measures; d07021a/d07022a take 10 complex decompositions instead of 13 in 1-3 runs of 40 on every build; UIM 3028's decomposition statistics vary | skill `nastran95-performance` §5 |

### 12.4 Gust

| check | result | source |
|---|---|---|
| von Karman PSD deck `monarch_demo_asm1083_gust_h0km_eas8p5` against Simcenter 2606 | every curve's rms within 0.3 % (the root bar's item 3 2.6 %; Simcenter adds its default residual vector, the fork none), N0 within 3.5 %; the tip transfer function within 0.3 % to 2.7 Hz and within 9 % and 10 degrees at the 4.3 Hz mode pair | `HALO.md`; skill `nastran-flutter` §7 |
| 1-cos deck `..._gust1cos_h0km_eas8p5` | tip extremes above grid 1010001 within 0.05 % (-94.19 against -94.17 mm; 0.8-1.7 % before the spline fix, -2.3 mm before the `UNPACK` fix); plunge -8.120..8.071 m against -8.120..8.070 m; correlation 0.9996 over the 6 s before the spline fix | `HALO.md`; skill `nastran-flutter` §7 |
| turbulence deck `..._gustvk_h0km_eas8p5_s1` | tip rms 29.652 against 29.663 mm, extremes within 0.4 %, correlation 0.99999, plunge within 0.7 mm | skill `nastran-flutter` §7 |
| the gust in-core solve, NASA's d11031a / d11032a | 1.4e-4 / 1e-5 of each line's largest value against the out-of-core path; d11031a nearer NASA's 1995 print than before | `HALO.md` |

## 13. Not implemented, and open

- **The g-method** (ZAERO's; Chen, AIAA J. 38(9), 2000): `HALO.md` sets out what it takes in
  FA1's PK branch - `Q'(ik)` per pair by central differences of the interpolated `QHH`,
  the quadratic-in-g eigenproblem linearised and swept in k through CEAD, FA2 summaries
  per root of the sweep, a sixth `FLUTTER` method name - and records it as not started.
- **SOL 144** (static aeroelastic trim): no COSMIC counterpart.
- **CAERO4** (strip theory): not in the front end.
- **KE and K methods**: NASA's, untouched; `PKMATCH` applies to PK only.
- **Residual vectors**: NASTRAN-95 never had them; Simcenter's default residual vector is
  the stated reason for the 2.6 % on the root bar's PSD item.
- **AMG's drivers at -O2**: the loops over boxes in `amgb1a`/`amgb1` and the GINO packing of
  a 2.6 GB AJJL stay at -O0 (an -O2 driver produced an unusable AJJ); the whole tree at -O2
  prints 124 of 132 demos differently and segfaults on 51, because open core is
  overlapping dummy arguments.
- **Blocked ATEIG, bigger GINO buffers, thread priorities, x86-64-v4, more AMP slots**: tried
  and dropped (skill `nastran95-performance` §6).

## 14. Where to read further

| topic | file |
|---|---|
| the fork's engineering log, every change with its reason and check | `HALO.md` |
| the PK iteration, the loop list, the summary | `mis/fa1pke.f`, `mis/fa1.f`, `mis/fa2.f`, `mis/fa1pki.f` |
| the fork's PK additions: threads, the QR twin, tracking, vectors | `mis/fa1pkp.f`, `mis/fa1pkq.f`, `mis/fa1pkt.f`, `mis/fa1pkv.f` |
| the doublet lattice and its batched, threaded, quartic forms | `mis/gendp.f`, `mis/amgk.f`, `mis/tkerv.f`, `mis/dlmq.f`, `mis/incro.f` |
| the generalized aerodynamics and the in-core solves | `mis/ampk.f`, `mis/ampcz.f`, `mis/ampczs/`, `mis/ampf.f` |
| the aerodynamic cache | `msc/mscaec.c` |
| FEER's guards | `mis/ferxtd.f`, `mis/ferxts.f`, `mis/fqrwv.f`, `mis/fqrw.f`, `mis/fernpd.f` |
| the gust spline and the tall-column fix | `mis/lsplnd.f`, `mis/mintrp.f`, `mis/adri.f`, `mis/frd2i.f`, `mds/unpack.f` |
| the (Mach, k) sort | `mis/apd.f` (`APDMKS`) |
| the rigid formats, their parameters and card bits | `rf/AERO10`, `rf/AERO11`, `rf/DISP03` |
| the translation, the driver, the OUTPUT4 rewrite, the print rewrite | `msc/mscxlat.c`, `msc/mscexec.c`, `msc/mscflut.c`, `msc/mscop4.c`, `msc/mscf06.c` |
| NASA's own account of the formats | `NASTRAN Users Manual 2.pdf` (Vol. II, SP-222(08)), distilled in VehicleDesign's `nastran-analysis` skill; the Theoretical and Programmer's Manuals are not distilled |
