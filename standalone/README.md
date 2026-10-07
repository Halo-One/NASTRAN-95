# NASTRAN-95, standalone

NASA's NASTRAN-95 (the 1995 COSMIC release, open-sourced by NASA in 2015), as Halo One's
fork builds it: two executables for **Windows and Linux** that run on any 64-bit machine
of that kind on their own. No installer, no licence server, no compiler, no runtime
libraries, no companion files: the Windows build imports only `KERNEL32.dll` and
`msvcrt.dll`, which every Windows has, and the Linux build is statically linked and asks
for no dynamic loader at all. Each carries its rigid format library inside and makes and
removes its own scratch directory. Copy one file anywhere, run a deck.

* **`nastran95`** - the solver as NASA wrote it, reading the 1970s COSMIC input.
* **`nastran95ase`** - the same solver behind a front end that reads **the MSC dialect**
  (the one MSC Nastran and Simcenter Nastran read): `SOL 1xx`, `INCLUDE`, free field,
  `EIGRL`, `RBAR`, `CBUSH`, `CQUAD4`, `PARAM,AUTOSPC`, MSC's launcher keywords, `ASSIGN
  OUTPUT4`; plus **SOL 144** static aeroelastic trim (which COSMIC never had), **SOL
  145** flutter on matched points with one child run per subcase, **SOL 146** gust
  response, **SOL 200** design optimisation, and a print file in MSC's layout.

The file names are the only difference between the two platforms: `nastran95.exe` and
`nastran95ase.exe` on Windows, `nastran95` and `nastran95ase` on Linux, built from one
checkout by `build/build_nastran95.ps1` and `build/build_nastran95.sh`. On a 1,400-grid
beam model the two builds print the same twenty frequencies to every digit. Below, a
name written without an extension means either of them.

Neither is a drop-in replacement for the commercial codes, which have forty years of
elements, solution sequences and checking this code does not. They are licence-free
solvers for a beam, truss, shear-panel, plate or modes model, and for the aeroelastic
chain - modes, flutter, trim, gust - of models built from those. `HALO.md` at the
repository root identifies every change against NASA's tree, as the NASA Open Source
Agreement requires, and is the engineering log of how each piece was checked;
`doc/nastran95ase_users_manual.md` is the manual for running decks and
`doc/nastran95ase_theoretical_manual.md` says what the numbers mean.

## What is here

| path | what |
|---|---|
| `nastran95(.exe)`, `nastran95ase(.exe)`, `BUILD_INFO.txt`, `BUILD_INFO_linux.txt` | the executables and their build records, **written here by the build scripts and gitignored**: they are build products of this repository. `nastran95 deck.inp [outdir]`; `nastran95ase deck.dat [outdir]`; `--version` says which commit and compiler built it; `--cosmic` turns the front end off |
| `build/build_nastran95.ps1` | builds both Windows executables from this checkout, reproducibly: a pinned portable gfortran (winlibs 16.2, by URL and SHA-256), OpenBLAS 0.3.34 built once from its pinned release, CMake and Ninja. `-Commit <sha>` builds that commit of the fork from GitHub instead (a release build whose record names a commit anyone can fetch); `-Source <dir>` another local tree; `-InstallDir <dir>` where the executables go; `-WorkDir <dir>` the toolchain cache; `-NoInstall`; `-Clean`. Its header says what every choice is for |
| `build/build_nastran95.sh` | the same on Linux with the machine's own gfortran (13 or newer): `--commit`, `--source`, `--install-dir`, `--work-dir`, `--no-install`, `--clean`, `NASTRAN_BUILD_JOBS`. Its header has only what differs from the Windows build |
| `build/work/` | the toolchain, OpenBLAS and the CMake build trees (gitignored; delete to start over) |
| `nastran.bat`, `nastran` | a stand-in for the licensed solver's launcher: put this folder ahead of the licensed `bin` on the PATH and `nastran <deck>` runs `nastran95ase`. It names itself on every run. Nothing uses it unless the PATH says so |
| `run_nastran95.bat`, `run_nastran95.sh` | a convenience for people who prefer Explorer: drag a deck onto it and the print file appears beside the deck with the window held open. Deliberately not named `nastran95.bat` (cmd.exe would resolve `nastran95` to the exe first anyway) |
| `examples/cantilever/` | two 1970s-dialect decks (statics and modes) with their print files and a README saying what numbers to expect. Start here on a new machine |
| `examples/flat_plate/` | **one wing three ways**: a cantilevered plate through SOL 144 (trim and divergence), SOL 145 (PK flutter) and SOL 146 (gust response), each solution checking the others, with the prints and a README that reads them |
| `test/run_decks.py` | **the checks, without MATLAB or another solver**: every test deck to exit 0, then the numbers - the flat plate's three solutions against each other, a typical section's divergence against its closed form, the Aeroelastic guide's HA144A, HA144B and HA145C against Simcenter Nastran 2606's prints of the same decks, a free-free aircraft's lift against its weight, the flutter driver cold and restarted. About a minute |
| `test/decks/` | the test decks: a two-subcase flutter deck and its modes deck, a plate flutter deck, a small free-free aircraft for SOL 144, a typical section for divergence, and the guide's examples rebuilt from the guide (`aeroelastic_guide_examples/`) |
| `test/data/` | Simcenter Nastran 2606's prints of the guide decks (2026-10-01), cut to the pages the checks read |
| `test/n95_platform.m`, `test/test_nastran95ase_*.m` | MATLAB tests of the SOL 145 driver (the joined print, the eigenvectors at the marked points, the restart, the threaded solve against the serial one), SOL 144 (the readers, the physics) and its corrections (W2GJ / WKK / FA2J, divergence, the guide against Simcenter). They read the prints through VehicleDesign's `utilities/jhc_library` readers, found through `VEHICLEDESIGN_ROOT` or a checkout beside this one; without them they are incomplete, not failed |

## Building

```
powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1      (Windows)
bash standalone/build/build_nastran95.sh                                           (Linux)
```

A first Windows build downloads the toolchain (260 MB) and builds OpenBLAS: about 40
minutes; afterwards a few minutes, and a changed C file seconds. Linux needs `gfortran`
(13+), `make`, `cmake`, `ninja`, `curl`, `unzip`, `perl` and `binutils`, and takes a few
minutes. The script builds, strips, checks that each executable really is standalone
(the Windows imports; the Linux `INTERP` segment and the top of the image below 2 GB),
runs `--version`, installs both into this folder and writes the build record. Then:

```
python standalone/test/run_decks.py
```

Why it is built the way it is - `-O0` with a short fenced list of optimised files, the
static link, the fixed image base and `-no-pie`, the 1 GB open core, the kernels built
twice and chosen at run time - is in the scripts' headers and, at length, in the skill
`.claude/skills/build-nastran95/SKILL.md`, which also says how to change the solver or
the front end and how to verify a build.

## Running a deck

```
standalone\nastran95 model.inp            -> model.out next to the deck
standalone\nastran95 model.inp C:\runs    -> C:\runs\model.out
standalone\nastran95ase model.dat out=C:\runs scr=yes    MSC's launcher spellings
standalone\nastran95 --version
```

(Linux: the same without `.exe` and with `/`.) Giving it to someone else: send them the
one file. Any 64-bit Windows 10 or 11 machine or 64-bit Linux, no installation, no admin
rights, any folder.

Exit code 0 means the print file reached `END OF JOB` with no fatal message; 1 bad
arguments or a missing deck; 2 no `END OF JOB` (the watchdog's end too); 3 a `USER` or
`SYSTEM FATAL MESSAGE`, GINO's `I/O ERROR`, or a module that stopped without a numbered
message. (NASA's solver exits 0 after a fatal; this one reads its own print file back on
the way out and sets the code from what it says, on every exit path.) A `model.log` is
written beside the print file; punch, plot, dictionary and checkpoint files only when the
deck asks for them. `nastran95ase` takes MSC's launcher keywords after the deck:
`out=<dir>`; `scr=no` keeps the checkpoint (`<stem>.nptp` and the dictionary `<stem>.dic`
stay beside the print); `restart=<modes deck>` restarts off that run's checkpoint, with
`optp=<dir>` naming the output directory that run was given - the modes once, each
flutter deck off them. Everything else MSC's launcher takes is accepted and ignored with
a note.

Neither executable can hang: a wall-clock watchdog ends the process with a message and
exit code 2 when a run passes `N95_TIMEOUT` minutes (else the deck's `TIME` card for
`nastran95`, else 30 minutes for `nastran95ase`). Scratch files go in a fresh directory
under `TEMP` (`TMPDIR` on Linux), named after the process, and are removed when the run
ends, on every way out the process lives through. The environment variables - the
threads, the aerodynamic cache, the kernel switches, `OCMEM` / `DBMEM` for the core
split - are in the user's manual §3.

The 1970s dialect `nastran95` reads (`ID`, `APP`, `SOL 1,0`, `TIME`, `CEND`, fixed
8-column fields, no `INCLUDE`) is what NASA's 132 demonstration decks in `inp/` are
written in; `examples/cantilever/` has two small ones and the things learned writing
them. Everything written for MSC or Simcenter Nastran goes to `nastran95ase`.

## The MSC dialect: `nastran95ase`

Given a deck in MSC's dialect, `nastran95ase` rewrites it into the 1970s COSMIC input
NASA's solver reads, runs it, and rewrites the print file into MSC's layout on the way
out. NASA's numerical code is untouched: everything new is C, in `msc/`, and it never
runs inside the solver. A run leaves, beside the print:

| file | what |
|---|---|
| `<stem>.out` | the print file, in MSC's layout |
| `<stem>_n95.dat` | **the deck it actually solved**, in the COSMIC dialect |
| `<stem>_xlat.txt` | every translation decision, numbered, and a table of what became of each card type |
| `<stem>.log` | NASTRAN-95's own module log |
| `<name>.phg`, `.mgg`, `.op4` | the `ASSIGN OUTPUT4` matrices, in MSC's formatted OP4 |

Read `<stem>_xlat.txt` after any run you have not seen before. It is the record of what
the front end decided, and a decision it got wrong is a wrong answer that looks right.
The user's manual §5 has the card-by-card table; in short: `SOL 101/103/105/107-112/
145/146` become rigid formats 1/3/5/7-12 and AERO 10/11, `SOL 144` a DMAP program around
the fork's module `AETRIM`, `SOL 200` a design loop around child runs; `EIGRL` becomes
`EIGR FEER` with a shift (0.5 Hz when the card gives none - a zero shift on a free-free
model loses every mode); `RBAR`/`RBE2` become `CRIGD1`, `CBUSH` six `CELAS2` (coincident
grids) or a `CONROD` (apart), `CQUAD4`/`CTRIA3` the 1970s `CQUAD2`/`CTRIA2`, `PBARL` a
`PBAR`; ids up to 99,999,999 are renumbered onto an ordered block under 2^24 and put
back in the print; a degree of freedom nothing is attached to is constrained and named
(what `AUTOSPC` does); MSC-only parameters and commands are dropped with the cost named;
a bulk data card the front end does not know is **a fatal that names it**, never a
silent omission.

**SOL 144, static aeroelastic trim.** `AEROS`, `AESTAT`, `AESURF`, `AELIST`, `AELINK`,
`TRIM`, `DIVERG`, the correction matrices `W2GJ` / `FA2J` / `WKK` by `DMI`, `DMIJ`,
`DMIK`, half models, 0 to 6 `SUPORT` freedoms, strip theory (`CAERO4`) for divergence.
The print carries the six-column stability derivative tables, the hinge moments, the
trim variables, the load resultants, `OLOAD`, `AEROF` / `APRES` and the divergence
summary in Simcenter's layout. Held to Simcenter 2606 on the Aeroelastic guide's
examples (`test/run_decks.py`; `SOL144.md` has the theory and the design).

**SOL 145, flutter.** MSC's `PKNL` (matched points) through `PARAM,PKMATCH,1`; several
subcases run as one child process per subcase, side by side, the prints joined; a
negative velocity in the `FLFACT` marks that point for eigenvector output, and
`PARAM,PKVECT,1` prints every root's modal vector at every point; `AEROF` box loads;
`OUTPUT4` of the generalised aerodynamics and modal matrices for a state-space plant;
the modes checkpointed once and every flutter deck restarted off them (`scr=no`,
`restart=`); an aerodynamic cache (`N95_AERO_CACHE`) across runs sharing an aerodynamic
model; the doublet-lattice and PK kernels threaded and built twice (any x86-64, and
AVX2 chosen at run time), each checked line for line against the unmodified solver.
`HALO.md`, "Flutter" and "Linux, and the flutter run in a minute and a quarter".

**SOL 146, gust.** `GUST`, `RANDPS`, `TABRNDG`, `TABRND1`, `RANDOM`, the `XY` output
(`XYPRINT ... PSDF`, with rms and N0), `PARAM Q` / `MACH`; `GUSTAERO`'s sign is MSC's on
the way in. Matches Simcenter 2606 on the PSD, 1-cos and turbulence decks it was checked
on (`HALO.md`, "SOL 146 random response").

**SOL 200.** `DESVAR`, `DVPREL1`, `DVMREL1`, `DLINK`, `DRESP1` (weight, volume, frequency,
eigenvalue, displacement, stress), `DCONSTR`, `DCONADD`, `DSCREEN`, `DOPTPRM`, `DESOBJ`,
`ANALYSIS`: sensitivities by forward difference through child `--cosmic` runs, CONLIN
solved through its dual, move limits, `CONV1`/`CONV2`/`GMAX`/`DESMAX`. On MSC's
three-bar truss example it converged to 2.7020 against the closed form 2.7000.

**OUTPUT4.** `ASSIGN OUTPUT4` plus the `OUTPUT4` alter writes `.phg` / `.mgg` from a
modes run and the aerodynamic and modal matrices from a flutter or gust run, in the
`4I8 / A8 / 1P,5E16.9` layout MSC's readers and ZAERO take.

**When something goes wrong.** Every fatal the solver prints is repeated on the
terminal with what it means and what to do about it (`msc/mscdiag.c`, the catalogue;
the front end's own messages are a 9000 series in the same three-part shape). Two
things to know: on a semi-definite mass matrix (lumped masses with no rotary inertia)
the FEER eigensolver may return fewer modes than asked and says so (`UWM 2390`; the
modes it returns are real - give the masses rotary inertia, or ask for fewer); and a
solution that needs a mass matrix and has none ends with `END OF JOB` and no
eigenvalue table, which the terminal says. The user's manual §13 is the troubleshooting
list.

**Known deviations.** Shells are the 1970s `CQUAD2`/`CTRIA2` (no membrane-bending
coupling, no transverse shear); a separated `CBUSH` carries K1 only (a fatal if its
`PBUSH` sets more); `CBEAM` is not translated (write `CBAR`); a SOL 145 or 146 deck with
several subcases in one process solves the first (the driver exists for this); heat
transfer, cyclic symmetry, substructuring, superelements and the nonlinear solutions
are not translated; with `WKK` in a SOL 144 deck the divergence roots differ from
Simcenter's by design (Simcenter's `DIVERG` leaves WKK out; this one applies it).

## Running it in place of the licensed solver

`nastran95ase` takes the same command line as the licensed launcher - `nastran deck.dat
[out=dir]`, `scr=`, `bat=`, `old=`, `append=` accepted - reads the same deck and writes
`<stem>.out` beside it in the same layout, so scripts written for Simcenter or MSC
Nastran run on it unchanged once `nastran.bat` (`nastran` on Linux) is found first on
the PATH:

```
set PATH=C:\...\NASTRAN-95\standalone;%PATH%                           (Windows)
PATH="$(git rev-parse --show-toplevel)/standalone:$PATH"; export PATH   (Linux)
```

The shim prints two lines naming itself on every run, deliberately: someone with a
licensed seat who put this folder on the PATH by accident must not be able to mistake
one solver for the other.

## Verification

`test/run_decks.py` is the check anyone can run. What it holds the build to, with the
tolerances of the MATLAB tests it stands in for:

* the flat plate's three solutions against each other (`examples/flat_plate/README.md`):
  SOL 144's divergence dynamic pressure against the speed where SOL 145's aperiodic
  bending root crosses to a positive real part (95.0 against 95.7 m/s; 2 %), SOL 146's
  response at 0.5 Hz against SOL 144's static deflection at the same angle of attack
  (0.5 %; 1 %), the gust PSD against |H|^2 times the von Karman spectrum (1e-3), the rms
  against the PSD's integral (2 %), the modes and the flutter crossing (70.0 m/s,
  22.2 Hz) against the recorded ones;
* a typical section's divergence against K / (S c CMY_alpha), its own rigid pitching
  moment slope, to 2e-6;
* the Aeroelastic guide's HA144A - the CZ and CMY rows of both subcases in all six
  columns within 1.5e-6 of each row's largest coefficient and the trims within 1e-6 -
  HA144B's divergence roots within 3e-6 and HA145C's strip-theory divergence speed
  within 0.5 ft/s, against Simcenter Nastran 2606's prints of the same decks
  (`test/data`); and HA144B with `WKK = 2 I`, the roll trim Simcenter's and the
  divergence roots half of Simcenter's, by design;
* the small free-free aircraft's two 1 g trims: the aerodynamic lift is the weight to
  1e-4;
* the two-subcase flutter deck through the driver, cold and restarted off its
  checkpointed modes run: the same flutter summary.

The MATLAB tests in `test/` go further where VehicleDesign's readers are available
(the eigenvectors at the marked points, a mode animated, the threaded solve against the
serial one to the bit, the SOL 144 readers and OLOAD balance, the W2GJ / WKK / FA2J
equivalences). The acceptance tests on a real aircraft - a five-Mach SOL 145 deck, a
23-subcase SOL 144 deck and three SOL 146 gust decks against Simcenter Nastran 2606,
root by root and derivative by derivative - live in VehicleDesign with the model they
need; their results are recorded in `HALO.md` (the flutter crossings within 0.1 m/s,
the derivatives within 2e-5 of each variable's largest, the gust rms within 0.3 %).
NASA's 132 demonstration decks (`inp/`) against NASA's 1995 prints (`demoout/`) are the
check of the solver itself: 85 match every table, and `HALO.md` accounts for each of
the rest (`.claude/skills/nastran95-performance/scripts/run_demos.sh` and
`compare_demos.sh` run and compare them).

## Licence

NASA Open Source Agreement 1.3, unchanged (the agreement is at the repository root). It
permits modification and use, and requires modifications to be identified, which
`HALO.md` does.
