# nastran95ase User's Manual

Halo One's fork of NASA's NASTRAN-95 (the April 1995 COSMIC release, NASA Open Source
Agreement 1.3). This manual is for an engineer running decks. It is compiled from the
fork's engineering log (`HALO.md`), the sources in `msc/`, `bin/nastrn.f.in` and `mis/`,
and the VehicleDesign skills `nastran-analysis`, `nastran-flutter`,
`nastran95-performance` and `build-nastran95`. Where a statement rests on one file, that
file is named so it can be checked. Where the sources say nothing, the text says "not
documented". The companion `nastran95ase_theoretical_manual.md` says what the numbers
mean.

## 1. The executables

| executable | reads | use it for |
|---|---|---|
| `nastran95ase` (`nastran95ase.exe` on Windows) | MSC Nastran's dialect: `SOL 1xx`, free, small and large field, nested and split `INCLUDE`, `EIGRL`, `RBAR`, `RBE2`, `CBUSH`, `PBARL`, `CQUAD4`/`CTRIA3`/`PSHELL`, `PARAM,AUTOSPC`, `ASSIGN OUTPUT4`, MSC launcher keywords, `SOL 200` | every deck written for MSC or Simcenter Nastran, including all of VehicleDesign's ASE decks |
| `nastran95` (`nastran95.exe`) | the 1970s COSMIC input only: `ID`/`APP`/`SOL n,0`/`TIME`, fixed 8-column bulk data, no `INCLUDE` | NASA's 132 demonstration decks (`inp/`), decks written in that dialect |

Both are built from one library and one main-program template, `bin/nastrn.f.in`,
configured twice (`NASTRAN_ASE_MODE` in `CMakeLists.txt`). The front end of
`nastran95ase` rewrites the deck before NASA's solver sees it and rewrites the print file
after the solver is done; NASA's solver reads what it always read (`HALO.md`, "The MSC
dialect front end"). Given an MSC deck, `nastran95` stops on the first executive control
line and the exit handler says to use `nastran95ase` (`mds/hexit.f`; the start-up pass
notices a three-digit `SOL` or an `INCLUDE`). Both print the same MSC-layout print file.

Both executables are statically linked: on Windows they import only `KERNEL32.dll` and
`msvcrt.dll`; on Linux they are built `-no-pie` so the 1 GB open core sits below 2 GB
(skill `build-nastran95`). The rigid format library is compiled in (`utility/hrfgen.f`
generates `hrflib.f` from `rf/`) and written into the scratch directory at start-up, so
nothing has to be installed beside the executable (`HALO.md`, "Making the executable
stand alone").

`--version` prints `NASTRAN_BUILD_ID`, a configure-time string naming the commit,
compiler and date (`HVERS` in `bin/nastrn.f.in`). Since 2026-09-26 the Windows and Linux
executables build from one fork branch, `halo-ase-sol145-perf` (skill `build-nastran95`).

## 2. Command line

```
nastran95ase deck.dat [output_dir] [keyword=value ...]
nastran95ase --cosmic deck.inp [output_dir]
nastran95ase --version        (also -v)
nastran95ase --help           (also -h, /?)
nastran95ase < deck.inp > deck.out    (NASA's original stdin/stdout contract, unchanged)
```

Argument handling is in `bin/nastrn.f.in`:

- The first argument is the deck. It is opened on unit 5; `<stem>.out` (the print file)
  and `<stem>.log` (the module log) go beside it, or into the output directory. Punch,
  plot, dictionary and checkpoint files appear only if the deck asks for them (`HUSAGE`).
- A bare second argument is the output directory. An argument containing `=` is a
  launcher keyword (next section).
- No argument on a terminal prints the usage and exits 1 instead of waiting on stdin.
- A missing deck: `nastran: deck not found: <path>`, exit 1.
- The deck stem must be 60 characters or shorter, because every output name lives in a
  `CHARACTER*72` and `<stem>.nptp` must fit: `nastran: deck name too long`, exit 1.
- `--cosmic` reads the deck as it is, whichever executable this is, without translation.
  The SOL 200 driver and the SOL 145 driver run their child analyses this way on decks
  they have already translated.

### 2.1 Launcher keywords

The scripts VehicleDesign carries call the solver as
`nastran deck.dat scr=yes bat=no old=no append=no out=DIR`, so MSC's keywords are
accepted (`bin/nastrn.f.in`, label 462):

| keyword | effect |
|---|---|
| `out=<dir>` | the output directory (honoured) |
| `scr=no` (or `scratch=no`) | keep the checkpoint: the translated deck gets `CHKPNT YES,DISK`, the problem tape `<stem>.nptp` and the dictionary `<stem>.dic` stay beside the print file (UIM 9464). `CHKPNT YES` without `DISK` is UFM 508 on this build (`HALO.md`, "The modes once") |
| `restart=<modes deck>` | start a run off that deck's checkpoint: the flutter deck off its modes (section 7.6) |
| `optp=<dir>` | where the modes run left `<stem>.nptp` and `<stem>.dic`, when that run had an output directory. `optp=<file>` alone names the old problem tape of a `--cosmic` run, which is how the flutter children get it |
| anything else with `=` (`bat=`, `old=`, `append=`, ...) | `nastran: <arg> is an MSC launcher keyword; accepted and ignored` on standard error |

### 2.2 What a run leaves behind

| file | what |
|---|---|
| `<stem>.out` | the print file, rewritten into MSC's layout (section 11) |
| `<stem>.log` | NASA's module log (`BEGN`/`END` lines per DMAP module). Its CPU column is the process's *system* time (`mds/cputim.f` returns `ETIME`'s second element, as NASA shipped it, kept because a real clock would arm the `TIME` card's limit) and its wall column is zero; do not profile from it (skill `nastran-flutter` §7, skill `nastran95-performance` §3) |
| `<stem>_n95.dat` | the translated deck, the one actually solved (`bin/nastrn.f.in` line 298). Read it when in doubt |
| `<stem>_xlat.txt` | the front end's messages: every card translated, dropped or renumbered, and the summary line (`msc_msg_open` in `msc/mscmsg.c`) |
| `<stem>.nptp`, `<stem>.dic` | checkpoint tape and dictionary, with `scr=no` only |
| `<stem>.pch`, `.plt`, `.dic`, `.nptp` | removed by the exit handler when left empty (`mds/hexit.f`) |
| `s<subcase>/` | the SOL 145 driver's child directories, one per subcase (section 7.2) |
| `<name>.op4`, `<name>_s<subcase>.op4`, `.phg`, `.mgg` | the OUTPUT4 files the deck asked for (section 9) |

VehicleDesign gitignores `*_n95.dat`, `*_xlat.txt`, `.nptp`, `.dic`, `s<subcase>/` and
solver outputs; do not commit them (skill `nastran-analysis` §1, §8).

### 2.3 Scratch

Each run works in a scratch directory of its own, `n95_<pid>`: under `%TEMP%` on Windows
(when `TEMP` is 50 characters or shorter, else `<system drive>\n95tmp`), under `$TMPDIR`
on Linux when that is set and at most 50 characters, else `/tmp` (`mds/hoswin.f`,
`mds/hosunx.f`; `HALO.md`). A monarch flutter child uses 1-4 GB of it. On a machine whose
`/tmp` is a tmpfs (RAM), runs side by side fill it and stop with GINO I/O errors; set
`TMPDIR` to a folder on disk (skill `nastran95-performance` §5).

The directory is removed on every end the run lives through: a normal end and a fatal
(`HCLEAN`, called from `PEXIT` and from the `atexit` handler), the watchdog's `_exit`, and
on Linux SIGTERM, SIGINT, SIGHUP, SIGSEGV, SIGBUS, SIGFPE and SIGILL (`msc/mscwatch.c`,
`msc_remove_scratch`, async-signal-safe `getdents64`/`unlinkat`/`rmdir`). The SOL 145
driver removes each child's directory after waiting for it, by the exact path the child
wrote into `s<id>/n95_scratch.txt` (the file `N95_SCRATCH_NOTE` names), which covers a
child that was SIGKILLed or, on Windows, ended with files open; on Windows the driver's
console handler does the same on Ctrl-Break or a closed window. What still leaves a
directory: a SIGKILLed or crashed top-level run, and a Windows top-level run that ends
with files open. Only the run's own directory is ever removed, never a pattern and never a
`DIRCTY` from the environment. `N95_KEEP_SCRATCH=1` keeps everything (`HALO.md`, "The
scratch directory on the ways out the exit handler misses").

## 3. Environment variables

All are optional. Each default applies only to a blank, so a caller that sets a variable
is unaffected (`bin/nastrn.f.in`). The complete set of `N95_` names in the tree is listed
here; nothing else is read.

### 3.1 Run control

| variable | meaning | source |
|---|---|---|
| `N95_TIMEOUT` | the wall-clock watchdog limit in minutes; 0 disables. Default: for `nastran95ase` 30 minutes; for `nastran95` the `TIME` card, or NASA's 5 minutes when the card is missing. When it fires the process ends with `_exit(2)` and the message `nastran: stopped after N minutes of wall clock, the limit for this run` naming which limit applied; the print file stops where the solver was. NASTRAN's own `TIME` card is checked only between modules, which is why the watchdog exists | `msc/mscwatch.c`, `bin/nastrn.f.in` |
| `N95_JOBS` | how many SOL 145 children run at once; default the processor count (`sched_getaffinity` on Linux, the affinity mask on Windows) | `msc/mscflut.c` |
| `N95_KEEP_SCRATCH` | `1` keeps every scratch directory | `msc/mscwatch.c` |
| `N95_KEEP_OP4` | `1` keeps NASTRAN-95's raw OUTPUT4 file (`op4_unit<n>.tmp`) next to the converted one | `msc/mscop4.c` |
| `N95_AERO_CACHE` | a directory in which AMG keeps each (Mach, k) pair's AJJ and AMP its LU factors, content-addressed; off when unset (section 7.8) | `msc/mscaec.c` |
| `N95_DLM_QUARTIC` | `1` the quartic doublet-lattice kernel, `0` NASA's parabolic. The front end sets it from the deck's `NASTRAN SYSTEM(270)=1` (or `QUARTICDLM=1`) and leaves a value the user set alone (UIM 9470); the SOL 145 children inherit it | `msc/mscread.c`, `msc/mscisa.c` |
| `OCMEM`, `DBMEM` | the split of open core between the modules and the in-memory database, in words. `nastran95` defaults to 2,000,000 / 12,000,000 (NASA's); `nastran95ase` and a SOL 145 child (`N95_CHILD` set) give the modules three quarters; a flutter run (rigid format AERO 10, or any child of the driver) runs with no in-memory database and the whole of open core for the modules, and `DBMEM` turns it back on. An `OCMEM` above the build's open core says so on standard error and takes the default split (it used to be `MESAGE -61`, `END OF JOB` and exit 0 with nothing solved). The build's open core is 256,000,000 words, 1 GB, set by `-DNASTRAN_OPEN_CORE_WORDS` (the CMake default in the file is NASA's 14,000,000; the build scripts pass 256,000,000) | `bin/nastrn.f.in`, skill `build-nastran95` |
| `DIRCTY` | use this scratch directory and leave it in place | `bin/nastrn.f.in` |
| `RFDIR` | a rigid format directory, overriding the compiled-in library; must fit a `CHARACTER*72` | `bin/nastrn.f.in`, `mds/rfopen.f` |
| `TMPDIR` (Linux) / `TEMP` (Windows) | where `n95_<pid>` is made | `mds/hosunx.f`, `mds/hoswin.f` |
| `LOGNM`, `PUNCHNM`, `PLTNM`, `DICTNM`, `NPTPNM`, `OPTPNM`, `FTN11`..`FTN21`, `SOF1`..`SOF10` | NASA's file-name variables, all defaulted; `FTN11`..`FTN21` are the OUTPUT4 units the front end names itself | `bin/nastrn.f.in` (`HUSAGE`) |
| `GFORTRAN_UNBUFFERED_ALL=y` | the Fortran runtime's buffering off, so the module log's `BEGN`/`END` lines can be timed as they appear | skill `nastran-flutter` §7 |

### 3.2 Threads and the speed-ups

Every speed-up has a switch that turns it off; with all of them off the executable prints
what Jon Chaconas's `halo-ase-sol145` build prints, line for line (skill
`nastran95-performance` §3, §4).

| variable | meaning | source |
|---|---|---|
| `OMP_NUM_THREADS` | the OpenMP thread count. The SOL 145 driver sets it to every processor for each child unless the caller set it, with `OMP_WAIT_POLICY=PASSIVE` | `msc/mscflut.c` |
| `OMP_WAIT_POLICY` | `PASSIVE` is what the driver sets; spinning waits took 400 s against 87 s on the five-Mach deck. On Windows (MinGW libgomp) the barrier never spins and the variable does nothing | `msc/mscflut.c`, skill `build-nastran95` |
| `N95_PK_THREADS` | `1` keeps the serial PK solve (one flutter loop at a time); unset or 0 takes the OpenMP default. DIAG 39 and a basis of fewer than four modes also keep it serial | `mis/fa1pke.f`, `mis/fa1pkp.f` |
| `N95_DLM_THREADS` | `1` keeps GEND's serial loop over the doublet-lattice rows (and turns the batched AMG off) | `mis/gendp.f` |
| `N95_AMG_BATCH` | the number of (Mach, k) pairs of one Mach the doublet lattice does together; default 8, at most 16; `0` keeps AMG's own loop | `mis/amgk.f` |
| `N95_AMP_PIPE` | `0` keeps AMP's serial pair loop | `mis/ampk.f` |
| `N95_AMP_SLOTS`, `N95_AMP_MB` | pairs held in core by AMP's pipeline (default threads + 1, at most 16, and what `N95_AMP_MB` holds at about 26 NJ² bytes a slot; `N95_AMP_MB` default 4096). The driver sets `N95_AMP_MB` to half the available memory shared among the children, between 512 MB and 2 GB, unless set | `mis/ampk.f`, `msc/mscflut.c` |
| `N95_AJJ_SOLVE` | `BUILTIN` (or `builtin`) solves the in-core AJJ with the fork's own LU instead of LAPACK's ZGETRF/ZGETRS. The LAPACK solve is the one change that is not bit-identical: a blocked LU rounds differently and marginal PK roots move (108 of 170,034 print lines on the monarch; no crossing) | `mis/ampczs/`, skill `nastran95-performance` §4 |
| `N95_BLAS_THREADS` | OpenBLAS's thread count, set explicitly because a static OpenBLAS otherwise runs on one thread; the driver sets it to processors / children | `mis/ampczs/ampczt_openblas.f`, `msc/mscflut.c` |
| `N95_INCORE_AJJ` | `0` turns off the in-core AJJ solve (AMPC's and AMPF's) and runs NASA's out-of-core `TRANP1`/`CFACTR`/`CFBSOR` path, hours per Mach on the monarch; the way to A/B the two paths | `mis/ampcz.f` |
| `N95_ISA` | `0` keeps the baseline x86-64 build of the PK QR, EGNVCT and the kernel; otherwise their `-march=x86-64-v3` copies run when the processor has AVX2 (same bits, more speed) | `msc/mscisa.c` |
| `N95_PK_SECANT` | `1` replaces NASA's fixed-point step in the PK iteration with a secant step; off by default because it moves bits (section 7.5) | `mis/fa1pkp.f`, `mis/fa1pke.f` |
| `N95_PK_STATS` | `1` prints, per child, a histogram of QR solves per root to standard error at the end of the PK loop; moves no bits | `mis/fa1pkp.f` |
| `N95_THROTTLE` (Windows) | `1` keeps Windows' execution-speed power throttling; by default the executable opts out (`SetProcessInformation`) | `msc/mscwatch.c`, skill `build-nastran95` |
| `N95_CHILD`, `N95_SCRATCH_NOTE` | set by the SOL 145 driver for its children (the ASE open-core split; the scratch-path note); not for users | `msc/mscflut.c`, `bin/nastrn.f.in` |

`N95_NO_ISA_V3` is a compile-time define, not an environment variable (`msc/mscisa.c`).

## 4. Solution sequences

The SOL map is `sols[]` in `msc/mscexec.c`:

| MSC `SOL` | COSMIC `APP` and rigid format | analysis |
|---|---|---|
| 101 (`SESTATIC`) | DISPLACEMENT 1 | linear statics |
| 103 (`SEMODES`) | DISPLACEMENT 3 | normal modes |
| 105 (`SEBUCKL`) | DISPLACEMENT 5 | buckling |
| 107 .. 112 | DISPLACEMENT 7 .. 12 | direct/modal complex eigenvalues, frequency and transient response |
| 145 (`SEFLUTTR`) | AERO 10 | modal flutter (section 7) |
| 146 (`SEAERO`) | AERO 11 | modal aeroelastic (gust) response (section 8) |
| 200 | not translated: a design cycle run by `msc/mscopt.c` around child analyses of this executable | design optimisation (section 4.1) |
| 1, 3 | DISPLACEMENT 1, 3 | the bare rigid format numbers |

`SOL 106`, `129`, `144`, `153`, `159`, `400`, `401`, `402` are named in `no_map[]` so the
fatal (UFM 9110) can say what was asked for: "NASTRAN-95 has no equivalent". There is no
COSMIC counterpart to SOL 144, static aeroelastic trim; the NASTRAN trim decks need the
licensed solver (skill `nastran-flutter` §7). A deck naming no solution the front end
recognises is UFM 9111.

### 4.1 SOL 200

Implemented in `msc/mscopt.c` and `msc/mscopt2.c`: `DESVAR`, `DVPREL1`, `DVMREL1`,
`DLINK`, `DRESP1` (WEIGHT, VOLUME, FREQ, EIGN, DISP, STRESS), `DCONSTR`, `DCONADD`,
`DSCREEN`, `DOPTPRM`, `DESOBJ`/`DESSUB`/`DESGLB`, `ANALYSIS = STATICS|MODES|BUCK`.
Sensitivities are forward differences from child `--cosmic` runs, one per free design
variable per cycle; the approximation is convex linearisation (CONLIN) solved through its
dual; weight and volume are closed-form from the model. Anything else in the design model
is a numbered fatal naming the card (no `DRESP2`, `DEQATN`, `DVGRID`). The report is in
`<stem>.out`, the final design in `<stem>_final.dat`. On MSC's three-bar truss example it
follows MSC's design-cycle history to within half a percent at every cycle (`HALO.md`;
skill `nastran-analysis` §6).

## 5. What the translator does to a deck

The front end (`msc/mscread.c` reads, `msc/mscexec.c` translates executive and case
control, `msc/mscxlat.c` the bulk data, `msc/mscwrite.c` writes the 8-column deck) records
every action in `<stem>_xlat.txt` and on the terminal, and ends with UIM 9001:
`SOL n became rigid format r (APP ...). c cards written, d dropped, r ids renumbered, k
degrees of freedom constrained for want of stiffness.`

### 5.1 Reading

Small field (8 columns, continuation marker in 73-80), large field (name ending in `*`,
16-column fields, four per line) and free field (commas) are read, and a deck may mix
them. A line contributes a fixed number of field slots - eight in small and free field,
four in large field - whether or not they were written. Nested `INCLUDE`s (UFM 9011 past
the nesting limit) and the `INCLUDE 'path` / `rest'` form split over two lines are read.
A card's fields grow as needed (the cap was 256 fields until fork d5e0e5f, which silently
truncated the 501-point `TABLED1` of the turbulence decks; it is now 1,048,576).

### 5.2 Executive control

`SOL` becomes `APP` + `SOL r,0` + `TIME`. `NASTRAN SYSTEM(270)=1` or `QUARTICDLM=1` sets
`N95_DLM_QUARTIC` (UIM 9470 says which kernel runs). `ASSIGN OUTPUT4` is read (section
9); other `ASSIGN` statements are ignored with UIM 9121. `NASTRAN BANDIT=-1` is written
above 300 grids (UIM 9113), because the bandwidth resequencer's scratch array stops a run
with SFM 3037 on a few thousand grids with rigid elements. `MAXLINES = 999999` is added.

### 5.3 Case control

Commands NASTRAN-95 reads with the same meaning are kept (`case_keep[]` in
`msc/mscexec.c`): `TITLE`, `SUBTITLE`, `LABEL`, `ECHO`, `MAXLINES`, `LINES`, `SPC`, `MPC`,
`LOAD`, `DEFORM`, `TEMPERATURE`, `METHOD`, `CMETHOD`, `FMETHOD`, `SDAMPING`, `FREQUENCY`,
`OFREQUENCY`, `TSTEP`, `DLOAD`, `IC`, `NONLINEAR`, `GUST`, `RANDOM`, the `XY*` plot
commands, `DISPLACEMENT`, `VELOCITY`, `ACCELERATION`, `SPCFORCES`, `OLOAD`, `AEROF`,
`STRESS`, `ELFORCE`, `FORCE`, `SET`, `SUBCASE`, `SUBCOM`, `SUBSEQ`, `SYMMETRY`, `REPCASE`,
`OUTPUT`, `AXISYMMETRIC`, `MODES`, `SVECTOR`, `THERMAL`, `FLUX`, `TRIM`, and since
2026-09-26 `K2PP`, `B2PP`, `M2PP`, `TFL` (direct matrix input; before that they were
dropped and a `DMIG` never reached the solver). Names are prefix-matched. Output options
in parentheses are MSC's and are dropped while the request is kept (UIM 9103).

Commands that are MSC's are dropped, each with what its absence costs named (UIM 9101):
`RESVEC` (MSC adds residual vectors by default, NASTRAN-95 never did), `ANALYSIS`,
`WEIGHTCHECK`, `GROUNDCHECK`, `AUTOSPC` (the front end applies the equivalent
constraints), `MEFFMASS`, `MODALSE`, `SDISPLACEMENT` (UFM 614 in the solver), `ESE`,
`ECHOON`, `ECHOOFF`, `DESOBJ`, `DESSUB`, `DESGLB`, `DSAPRT`, `SEALL`, `SUPER`, `K2GG`,
`M2GG`, `B2GG`. A command the front end does not know is left out with UWM 9102. `APRES`
becomes `AEROF` (NASTRAN-95 prints pressures and forces together), or is dropped when
`AEROF` is already asked for (UIM 9104). `SET n = ...` is written as a command with its
continuation lines. Ids in `SET` lists and `XY` requests follow the bulk data's
renumbering (section 5.5); a `THRU` range bounded by a renumbered id cannot be followed
(UWM 9105).

A SOL 103 deck whose case control asks for no output gets `DISPLACEMENT = ALL` (UIM 9114).

### 5.4 Bulk data, card by card

Dispatch is the loop at `msc/mscxlat.c` line 1025.

| MSC card | becomes | notes |
|---|---|---|
| `RBAR`, `RBE2` | `CRIGD1` | the independent end is chosen so that a grid on a `SUPORT` or `SPC` is never made dependent; the element id is kept (so UFM 2192 names the RBAR's id) |
| `CBUSH` + `PBUSH` | six `CELAS2` when the two grids are coincident; `CONROD` carrying K1 alone when they are separated | a separated `CBUSH` with K2..K6 set has no equivalent and is UFM 9206, not a guess |
| `EIGRL` | `EIGR FEER` for ND roots about V1 Hz, or 0.5 Hz when V1 is blank (UIM 9211) | a zero shift on a free-free model loses every mode; never write `V1 = 0.0` |
| `EIGC` CLAN (or any MSC method) | `EIGC HESS` (UIM 9116) | the continuation's ND says how many roots to keep |
| `PBARL` | `PBAR` with A, I1, I2, J worked out from the dimensions (UIM 9209) | |
| `CBEAM`, `PBEAM`, `PBEAML` | **not handled** | the header comment of `msc/mscxlat.c` and the catalogue's entry 307 say they become `CBAR`/`PBAR` at the end-A properties (and `msc/mscmsg.c`'s example message is a UFM 9203 for a non-prismatic `CBEAM`), but the dispatch loop has no handler for them and they are not in `copy_cards[]`, so they fall to the "anything else" row below; the `nastran-analysis` skill reports `CBEAM` as UFM 9299. Write `CBAR`/`PBAR` |
| `CQUAD4`, `CQUAD`, `CQUADR`; `CTRIA3`, `CTRIAR`; `PSHELL` | `CQUAD2`, `CTRIA2`; `PQUAD2`/`PTRIA2` | panels thinner than 1e-6 are dropped as massless drawing aids ("dropped: no stiffness") |
| `SUPORT1` | `SUPORT` (UIM 9214) | NASTRAN-95 has no set-selected form; the reference freedoms are always in force |
| `SPC1` | `SPC1` with `THRU` expanded and the grid list made contiguous | |
| `FLUTTER` | `PKNL` becomes `PK` + `PARAM PKMATCH 1` (UIM 9123); `IMETH` other than `L` becomes `L` (UIM 9117) | `TCUB` is UFM 316 in Simcenter too; the repo's decks write `L` |
| `SPLINE1` | `SPLINE1` without METH/USAGE | |
| `TABDMP1` | `TABDMP1` without TYPE (G only) | |
| `PARAM` | `WTMASS`, `COUPMASS`, `GRDPNT`, `G`, `W3`, `W4`, `LMODES`, `LFREQ`, `HFREQ`, `MAXRATIO`, `Q`, `MACH`, `KDAMP`, `IFTM` pass through; `PKMATCH`, `PKVECT` pass through (this fork's); `GUSTAERO`'s sign is flipped (UIM 9119: +1 computes the gust aerodynamics in NASTRAN-95, -1 in MSC); every other `PARAM` (`POST`, `AUTOSPC`, `K6ROT`, `RESVEC`, ...) is "dropped: MSC only" | `KDAMP` passes unchanged but means the opposite in the two codes (section 8) |
| `CONM2` | unchanged; the ones with no rotary inertia (fields 9-14 blank or zero) are counted and reported on a modal solution (UWM 9133) | |
| passed through unchanged (`copy_cards[]`: "cards NASTRAN-95 reads with the same name, fields and meaning") | `GRID`, `GRDSET`, `SPOINT`, `EPOINT`, `SEQGP`, `CORD1R/C/S`, `CORD2R/C/S`, `MAT1`-`MAT5`, `PBAR`, `PROD`, `PTUBE`, `PSHEAR`, `PELAS`, `PDAMP`, `PMASS`, `PVISC`, `CBAR`, `CROD`, `CTUBE`, `CONROD`, `CSHEAR`, `CVISC`, `CELAS1-4`, `CDAMP1-4`, `CMASS1-4`, `CONM1`, `CONM2`, `MPC`, `MPCADD`, `SPC`, `SPCADD`, `SPCD`, `SUPORT`, `OMIT`, `OMIT1`, `ASET`, `ASET1`, the static loads (`FORCE*`, `MOMENT*`, `GRAV`, `LOAD`, `PLOAD*`), temperatures, `EIGR`, `EIGB`, `EIGP`, `FREQ*`, `TSTEP`, `TLOAD1/2`, `RLOAD1/2`, `DLOAD`, `DAREA`, `DELAY`, `DPHASE`, `TABLED1-4`, `TABLEM1-4`, `TABLES1`, `TABLEST`, `TF`, `TIC`, `NOLIN1-4`, `AERO`, `AEROS`, `CAERO1`, `PAERO1`, `SPLINE2`, `SET1`, `FLFACT`, `MKAERO1`, `MKAERO2`, `TRIM`, `AESTAT`, `AESURF`, `GUST`, `RANDPS`, `TABRNDG`, `TABRND1`, `DMI`, `DMIG` | |
| dropped as drawing or post-processing aids | `PLOTEL`, `PBUSHT`, `RESVEC`, `MDLPRM`, `CBARAO`, `MONPNT1` (`drop_cards[]`) | |
| anything else | UFM 9299 naming the card, file and line; the deck is not translated, nothing is solved, no print file | `CBEAM` lands here (above); `CAERO4` is not in the front end (skill `solver-benchmarks`) |

A SOL 145 or 146 deck with several subcases in a single run (not through the driver) gets
UWM 9115: NASTRAN-95's rigid format solves the first and the rest are left out.

### 5.5 Renumbering and the automatic constraints

NASTRAN-95 packs an id and a component into one 32-bit word, so every id above
2^24 - 1 = 16,777,215 is renumbered, counting down, everywhere it is referenced
(`renumber_ids` in `msc/mscxlat.c`; UIM 9302 gives the count). `PARAM,GRDPNT` moves with
it (missing that put the two codes' centres of gravity 0.15 m apart on a 1,400-grid model
with mass and inertia agreeing), as do `DAREA`/`DELAY`/`DPHASE`/`TIC` grid ids, `SET`
lists and `XY` points. The print file puts the deck's numbers back (`msc/mscf06.c`). The
aero reference grid 99999999 of VehicleDesign's decks becomes 16777215 in `<stem>_n95.dat`.

Every degree of freedom nothing is attached to is constrained in an `SPC1` set (9998 in the
repo's runs) joined to the user's `SPC` selection through an `SPCADD`, which is what MSC's
`AUTOSPC` does (UIM 9301). NASTRAN-95's own `PARAM AUTOSPC 1` also exists and prints UIM
2435 with the cards it generated; what NASTRAN-95 lacks is MSC's `PARAM,AUTOSPC,YES`
spelling (`HALO.md`, the catalogue audit).

### 5.6 Writing

Small field, eight columns, continuations tagged `+0000001` on (a restart run's begin with
a letter, `+R000001`). A real is written in the eight-column spelling that reads back
closest to the value, NASTRAN's exponent form with the E dropped (`1.23457-5`) included; a
number that does not fit is re-spelled, never cut (`-6.89e+04` cut to eight characters
reads as -6.89: this happened on a PBAR). Large-field cards are used when even that loses
more than 1e-5 (`msc_r8` in `msc/mscwrite.c`).

## 6. Normal modes (SOL 103)

The eigensolver is FEER, with a shift from the `EIGRL`. Points to know when running the
repo's modal decks (skill `nastran-analysis` §2-§5):

- Budget: mfp / monarch_gvt decks 20 s at 120 modes; the CAD-mass configs
  (4,500-5,400 grids) 85-95 s at 120 modes, 15-25 s at 20.
- FEER returns more roots than asked (a reduced problem of twice the size); the print
  rewrite and the `.phg` writer cut to the number requested (UIM 9131).
- On a semi-definite mass matrix - lumped `CONM2`s with no rotary inertia, every model in
  the repo - FEER's reduction can lose the mass norm of a trial vector; the fork reseeds
  (UWM 2394 once) and may return fewer modes than asked (UWM 2390). The modes it does
  return are accurate. The exit handler repeats both on the terminal; UWM 9133 from the
  translator warns up front with the count of such `CONM2`s and the cure: rotary inertia on
  the lumped masses, or fewer modes. The deck that showed all this (830 grids) gets 89 of
  120 modes in 7 s; raising the orthogonalisation cap from 14 to 60 passes bought four rows
  and nothing else (`HALO.md`, "Never hang"). `DIAG 16` prints the reduction row by row.
- A rigid format that needs a mass matrix and has none stops in its own DMAP check with no
  numbered message, `END OF JOB` and exit 0; the exit handler says so on the terminal.
- `ECHO = SORT` is what the grid reader needs; `DISP = ALL` the eigenvector reader;
  `PARAM,GRDPNT,<grid>` the mass reader.
- `ASSIGN OUTPUT4` with the `SEMODES` alter writes `.phg` (PHIG) and `.mgg` (MGG) in MSC's
  formatted layout (section 9); rigid format 3 only.

## 7. Flutter (SOL 145)

### 7.1 What the deck must say

NASA's AERO 10 rigid format has three methods, K, KE and PK, and its PK loops over every
combination of the density, Mach and velocity `FLFACT` lists with a hard stop at 100 loops
(Flutter Error 3). VehicleDesign's decks are matched-point analyses: at a fixed Mach, one
point per altitude, density and true airspeed belonging together, which MSC and Simcenter
spell `FLUTTER ... PKNL` (`HALO.md`, "PKNL, and PK on matched points").

- The IFP accepts `PKNL` as a fifth method name (`mis/ifs5p.f`), and the front end writes
  MSC's `PKNL` as `PK` with `PARAM PKMATCH 1` emitted once (`msc/mscxlat.c`); a deck that
  already says `PK` + `PKMATCH` passes through. `rf/AERO10` hands `PKMATCH` to FA1 as its
  fourth parameter and `PKVECT` as its fifth (`FA1 .../V,Y,PKMATCH=0/V,Y,PKVECT=0`).
- With it, FA1 builds one flutter loop per entry of the density and velocity lists taken
  together, the Mach list walked alongside (its last entry repeated when it is shorter)
  (`mis/fa1.f`, label 98). The PK iteration itself is NASA's.
- The `FLUTTER` card, in its own order: SID, METHOD, DENS, MACH, RFREQ (the velocity
  `FLFACT` for PK), IMETH, NVALUE (the roots found per loop, FA1's `NEIW`), EPS (the
  convergence tolerance on k; 0.001 when blank or zero, `mis/fa1pke.f`). The driver reads
  the Mach and velocity `FLFACT` ids off it (`subcase_mach`, `child_loads_velocity` in
  `msc/mscflut.c`).
- `PARAM LMODES` chooses the modes that carry the flutter solution; the `EIGRL` count only
  sets how many modes READ finds (the References distillation of NASA's User's Manual, §3).
- A negative velocity in the velocity `FLFACT` marks that loop for eigenvector output.
- `FMETHOD` selects the `FLUTTER` card; `METHOD` the `EIGR`; `SDAMPING` the `TABDMP1`;
  `CMETHOD` the `EIGC` (K method only). `OFREQUENCY` must precede `OUTPUT(XYPLOT)`.

### 7.2 Several subcases: the driver

NASTRAN-95's flutter rigid format solves one subcase: FA1 reads the first `CASECC` record
for its `FMETHOD` and never comes back for another. The repo's decks carry one subcase per
Mach. `msc/mscflut.c` therefore:

- writes one translated deck per subcase and runs them as child processes of this
  executable (`--cosmic`, with `N95_CHILD` set for the open-core split), as many at a
  time as there are processors (`N95_JOBS` overrides), each in `s<subcase>/` under the
  output directory (UIM 9450). Two children in one directory would write over each other's
  fixed-name files and die in GP4, hence the directories;
- gives each child its subcase's Mach, read off the `FMETHOD`'s `FLUTTER` card and its
  Mach `FLFACT` (one value on the matched-point decks): `PARAM MACH` for the ADR load
  recovery, and the `MKAERO1`/`MKAERO2` lists cut to that Mach (UIM 9455), so a five-Mach
  deck's children compute one Mach's aerodynamics each instead of five;
- puts the marked loop's velocity on each child's `AERO` card (UIM 9456) so the AEROF
  loads come out at that loop's reduced frequencies (section 7.4);
- sets the threads: `OMP_NUM_THREADS` = every processor for each child (the coarse work -
  doublet-lattice rows and PK loops - shared by the scheduler), `OMP_WAIT_POLICY=PASSIVE`,
  `N95_BLAS_THREADS` = processors / children for the LU, `N95_AMP_MB` as in section 3.2;
  a caller's values stand. Measured on the five-Mach monarch deck on 32 processors: 102 s
  with an even split, 87 s this way, 400 s with spinning waits;
- waits for whichever child finishes (`waitpid` on POSIX, `WaitForMultipleObjects` on
  Windows), prints `nastran: subcase N -> sN/<stem>_sN.out (exit code n)` for each, removes
  each child's scratch, and joins the children's print files into `<stem>.out` in subcase
  order (`<stem>_s<subcase>.out` each), then rewrites the joined file into MSC's layout
  (`msc_f06`), as a single run's print is. The final line is
  `nastran: <stem> -> <stem>.out (N subcases joined)`;
- converts every child's OUTPUT4 files into the output directory per subcase (section 9);
- exits with the worst child's code; a child that could not be started or translated
  (UFM 9451, 9452), ended by a signal, or crashed on Windows (a negative NTSTATUS) counts as
  3. A child that left no print file is UWM 9453.

On Linux a child asks for SIGTERM when the driver dies (`PR_SET_PDEATHSIG`); on Windows
every child is in a job object with kill-on-close, so a driver stopped by its watchdog or
the user takes its children with it (skill `build-nastran95`). A per-point split (one
child per matched point) is deliberately not offered: every child recomputes the modes
and the whole aerodynamic matrix set, and the PK iteration itself takes seconds.

Before starting a run, look for a running `nastran95ase` (`pgrep -a nastran95ase`;
`Get-CimInstance Win32_Process` on Windows): two runs of one deck in one directory write
the same child files (skill `nastran-flutter` §7).

### 7.3 Marked loops, eigenvectors and PKVECT

A negative velocity in the `FLFACT` list is the request both MSC and NASTRAN-95
understand. For every accepted root of that loop FA1 prints the modal vector -
`EIGENVECTOR FROM THE PK METHOD`, then `EIGENVALUE = re im   LOOP = n   VELOCITY = v`
(the header repeated at the top of every page a vector runs over), then the complex
coefficient of each mode of the basis (`mis/fa1pkv.f`, formats 21 and 26) - and the rigid
format recovers the physical vectors at the `DISP` set, when the case control asks for
them (`DISP` and `OFREQUENCY`; `OFREQUENCY` is here a list of velocities, matched by
MODACC on the imaginary part), through MODACC, DDR1, SDR2 and OFP as `COMPLEX
EIGENVECTOR` tables in the layout MSC prints. Every root of the loop is printed, not the
one that crossed: the monarch deck with twenty loops marked prints 1,201 physical vectors
at 251 points, 120 MB (`HALO.md`, "Eigenvectors at marked loops").

A `DISP` set that names the `CAERO1` box ids gets one row per box in the `COMPLEX
EIGENVECTOR` tables - the plunge in T3 and the pitch in R2, in the box's frame - with no
parameter (MSC needs `PARAM OPPHIPA` for the same rows).

`PARAM PKVECT 1` has FA1PKV print the modal vector of every root at every loop, while the
recovery scratch (301) is written for the marked loops alone. The print grows by one
complex number per mode per root per loop (the monarch at 110 modes, 40 loops and five
Machs: some 22,000 vectors, about 150 MB, against 813 MB for the physical recovery of five
marked loops). The flutter summaries are unchanged by it to every printed digit, and the
physical vector of any point is Phi q with the modes run's Phi (VehicleDesign's
`animate_nastran_flutter_mode`; the reader `read_nastran_pk_eigenvectors` joins a vector
over its page breaks).

### 7.4 Aerodynamic loads (AEROF)

The case control request is NASA's `AEROF` (MSC's `APRES` is written as `AEROF`). ADR
builds the box loads of every recovered root and ADRPRT prints them as `AERODYNAMIC LOADS
(UNIT DYNAMIC PRESSURE)`, one line per box with the real and imaginary parts of T1..T3 and
a second with R1..R3. ADR takes each root's reduced frequency from `BOV` = b/V, which APD
forms from the `AERO` card's velocity field; the matched-point decks leave it blank, so
`BOV = 0`, UIM 2272, and nothing printed - which is why the driver writes the marked
loop's velocity onto each child's `AERO` card. With one marked loop per subcase every
recovered root's loads come out at its own k; with several, the first loop's velocity
serves and UWM 9456 says the other loops' loads are at the wrong k. Until fork a262158
these tables printed about 1e-19 on every box (NASA's `UNPACK` limit, section 13); they
now print the loads (`HALO.md`, "Tall columns").

### 7.5 Roots: NVALUE, tracking, the secant

- FA1 solves `NVALUE` roots per loop, numbered by acceptance in ascending frequency, not
  sorted by mode. On `monarch_demo_asm1083` 83 of the 110 modes give the same crossings as
  all 110 with fewer PK warnings and a fifth less time; the rule used there is NVALUE =
  the highest root that reaches 30 Hz anywhere in the baseline all-roots run (skill
  `nastran95-performance` §4, "The roots found").
- Root tracking (`mis/fa1pkt.f`, FA1PKT/FA1PKU): the roots of a loop are re-ordered so
  that POINT n of the summary follows POINT n of the loop before, by the modal assurance
  criterion between the PK eigenvectors plus a quarter of the relative eigenvalue
  distance, assigned greedily; the print says `ROOT TRACKING: FLUTTER LOOP n - m OF k
  ROOTS TOOK ANOTHER PLACE TO FOLLOW THE LOOP BEFORE` for every loop that reordered. The
  tracking restarts when the Mach changes and is skipped with a note when the work space
  is too small. VehicleDesign's plotter re-sorts after the fact as a check; the two agree.
- `N95_PK_SECANT=1`: a secant step on the residual h(k) = k_from_root(k) - k once two
  iterates exist, falling back to NASA's step when it would be non-positive or more than
  twice NASA's. It cuts the QR solves per root (2.24 to 1.68 on the monarch at Mach 0.10,
  EPS 1e-3), moves the lowest crossing per Mach by at most 0.001 m/s, and lands about a
  tenth of the heavily damped roots on another branch, as a change of EPS does. An opt-in
  for the lowest crossing, not a default (`HALO.md`, "A secant step").
- A failure to converge in 10 iterations prints `PK METHOD FIALED TO CONVERGE` [spelled so
  in `mis/fa1pke.f`] with `LEAST SQUARES FIT APPROXIMATION IMPLEMENTED` and takes the fixed
  point of a line fitted through the trials.

### 7.6 The modes once: checkpoint and restart

Every child of the driver solves the eigenproblem again (a child is a complete run of the
AERO rigid format). The alternative is NASTRAN's own checkpoint/restart (`HALO.md`, "The
modes once"; skill `nastran-flutter` §7):

```
nastran95ase <config>_modes.dat scr=no                       (the SOL 103 deck gen_nastran_cards writes beside the flutter deck)
nastran95ase <config>_flutter.dat restart=<config>_modes.dat
nastran95ase <config>_modes.dat scr=no ci_out
nastran95ase <config>_flutter.dat restart=<config>_modes.dat optp=ci_out ci_out
```

`scr=no` checkpoints: `CHKPNT YES,DISK`, the dictionary punched to `<stem>.dic`, the
problem tape `<stem>.nptp` (about 100 MB on the monarch; `mds/gnfiat.f` had to be changed
to honour `NPTPNM`, whose `IPERM` bit was never set). `restart=` reads the dictionary into
the executive control after the `TIME` card (UIM 9462), translates the modes deck a second
time and drops from the restart deck every card whose translated text that run had (a
hash set), so only the cards the modes run did not have are written (UIM 9463: on the
monarch, the 64 aero and flutter cards); NASTRAN merges them with the old bulk data off
the tape. The tape is hard-linked (copied when the volume refuses) into the output
directory as `optp.nptp`, because `DSNAMES` are `CHARACTER*80` and the children run one
directory down. The rigid-format-switch restart (RF3 -> AERO10, UIM 4145) skips GP1
through READ and runs GKAM / APD / GI / AMG / AMP / FA1 / FA2. The joined print gets the
modes run's `REAL EIGENVALUES` pages spliced in after each sorted echo (`msc/mscf06.c`), so
`read_khh` still reads them; UWM 9466 when the modes print is missing.

What makes a restart solve the modes anyway: any card of the restart deck that the modes
run did not have and that is not an aero or flutter card - NASTRAN's restart tables take
it as a change to the structure and re-execute from GP1 (UWM 9467 names such cards). The
first monarch try restarted off the vibe deck and re-solved everything (72 `CELAS2` from
the CBUSH springs, numbered above the highest element id, which the `CAERO1` boxes move,
and the 6 aero `CORD2R`), which is why the modes deck carries `aero_model.bdf` and no
flutter cards. A card that replaces one of the modes run's under the same id is UFM 311
to the merge. Regenerate the modes deck whenever the structure or the aero model changes.

Flutter runs - AERO 10 out of the translation, or any child - run with no in-memory
database, because NASA's DBM loses the block count of the eigenvalue file FA1 appends once
the database is full (`BLOCK NUMBERS INCONSISTANT ON OPEN IN DBMMGR` at loop 23 of every
restarted 120-mode monarch child); `DBMEM` turns it back on.

The restarted summaries agree with a direct run's to six significant figures on the
monarch (FEER's basis off the tape against the one READ recomputes), digit for digit on
the two-subcase test deck. Those last digits move which matched points' PK solutions
glitch - a single point reading g +0.010 between two at -0.016 and -0.017 - so the
plotters require two points above the threshold (`drop_isolated`). On Linux the restart
saves little (73.5 s direct against 72 s), since the direct run's children solve their
modes side by side.

### 7.7 Timings

| deck, machine | time | source |
|---|---|---|
| five-Mach `monarch_demo_asm1083_flutter.dat` (120 modes, 60-61 matched points, 2,282 boxes, 24-48 k per Mach), 16-core/32-thread Linux, unmodified source | 19 min 50 s | `HALO.md`, "Linux" |
| the same, every speed-up, parabolic kernel | 40.5 s (quartic 66-68 s) | skill `nastran95-performance` §1 |
| the same, `N95_AJJ_SOLVE=builtin` | 179-200 s, Jon's print line for line | same |
| the same deck on Jon's Windows build (`fcd681b`, 16 cores) | 25.6 min | `HALO.md` |
| the committed 110-mode PKVECT deck, Core Ultra 7 165U laptop (14 threads): Jon's exe / parity build parabolic / quartic | 1768.6 s / 309.9 s / 387.7 s | skill `nastran95-performance` §1 |
| the reduced one-Mach deck on MSC Nastran (before the switch to Simcenter) | 6.4 min | skill `nastran-flutter` §7 |
| Simcenter 2606, the five-Mach deck | hours against 6.5 min for nastran95ase | VehicleDesign repo guide |

Linux timings from 2026-09-26 were taken with other jobs running and are provisional. Time
on a quiet machine: three runs, the median.

### 7.8 The aerodynamic cache

With `N95_AERO_CACHE=<directory>`, AMG keeps each (Mach, k) pair's AJJ under a 64-bit
hash of every input it depends on (the group's ACPT record, its counts, NJ, the symmetry
flags, the reference chord, Mach, k, and a kernel version word `IAECV`: 1 parabolic, 3
quartic) and AMP keeps each pair's LU factors and pivots under a hash of the AJJ (the
LAPACK solve only; with `N95_AJJ_SOLVE=builtin` nothing is kept). Entries are files
`<key>.1` and `<key>.2`, written as a temporary and renamed into place so runs side by
side can share the directory; nothing is ever deleted (empty the directory when the
aerodynamic model is gone). The print is not told; the counts go to standard error at exit
(`N95_AERO_CACHE: AMG (AJJ) 48 of 48 pairs from the cache`). A cached run prints the
uncached print to the bit, by construction (`msc/mscaec.c`; `HALO.md`, "The aerodynamic
cache"). Size: 42 MB of AJJ plus 83 MB of LU per pair at 2,282 boxes; `HALO.md` gives
21 GB for the five Machs' 178 pairs, the `mscaec.c` header "about 19 GB". On the monarch
study deck: no cache 28.3 s wall / 578 CPU s; every pair from the cache 21.5 s / 353 s.

## 8. Gust response (SOL 146)

AERO 11 goes through the front end since forks c5a910b-03af0af, and every repo gust deck
(von Karman PSD, 1-cos, seeded turbulence) runs to the end and matches Simcenter 2606
since fork f28b083 (skill `nastran-flutter` §7). What the translator does for it: `GUST`,
`RANDPS`, `TABRNDG`, `TABRND1` copied; `RANDOM`, `OUTPUT(XYPLOT)` and the `XY` requests
written as they are, their ids renumbered with the bulk data; `PARAM Q` and `PARAM MACH`
passed (one dynamic pressure and one Mach per execution); `GUSTAERO`'s sign flipped (+1
computes the gust aerodynamics in NASTRAN-95, -1 in MSC); `SDISPLACEMENT` dropped; the
`DAREA`/`DELAY`/`DPHASE`/`TIC` grid ids renumbered with the grids (the gust `DAREA` sits
on the aero reference grid). `PARAM KDAMP` passes unchanged but its sign means the
opposite in the two codes: +1 is viscous modal damping in MSC and Simcenter and complex
stiffness in NASTRAN-95; both default to viscous and no repo deck sets it. Random response
is available only through the `XY` tabular print (`XYPRINT ... PSDF`), per NASA's manual.

Four faults were fixed on the way (`HALO.md`, "Tall columns" and "SOL 146 random
response"): NASA's `UNPACK` stopping at row 65,536 (the gust loads came out zero; doubling
`WG` changed nothing - that is the test for it); SDR2B reading an absent SIL's stale
trailer (SFM 3001 with `ELFORCE`); case control keeping renumbered ids (RAND2 SFM 3002);
and the spline in k solved in single precision (`LSPLND` now solves it in double; the PSD
response was jagged above 1.5 Hz before). The monarch gust decks run in about a minute on
Linux.

## 9. OUTPUT4

`msc/mscop4.c` honours `ASSIGN OUTPUT4='<file>' ... UNIT=n FORM=FORMATTED` plus an
`OUTPUT4` alter, for rigid formats 3, 10 and 11 only (UWM 9125 otherwise):

- MSC alters by label; NASTRAN-95 by DMAP statement number. In rigid format 3 the alter is
  written after statement 77 (SDR1, where PHIG and MGG both exist; the number from a DIAG
  14 listing of the 1995 executable). In AERO 10 and AERO 11 it is written after statement
  90 (AMP), where `QHHL`, `QHJL`, `QKHL`, `MHH`, `KHH`, `BHH` and `PHIDH` all exist. The
  alter text in the deck is otherwise ignored: a Simcenter deck's
  `ALTER 'AJJ0,WSKJF' / OUTPUT4 QHH,,,,//0/101/2` maps `QHH`, `QHJ`, `QKH` to the whole
  lists `QHHL`, `QHJL`, `QKHL`, so one block serves both solvers.
- Data blocks known (`dbmap[]`): `PHG`/`PHIG`, `MGG`, `KGG`, `PHA`/`PHIA`, `MAA`, `KAA`,
  `QHHL`, `QHJL`, `QKHL`, `QHH`, `QHJ`, `QKH`, `MHH`, `KHH`, `BHH`, `PHDH`/`PHIDH`. Any
  other is UFM 9122.
- MSC's units (101, 102, ...) are mapped to Fortran units 11, 12, ... in order of
  appearance; up to 8 requests (`OP4_MAX`). The main program opens units 11, 12 and 13-21
  (`FTN11`..`FTN21`); `mscop4.c`'s header speaks of units 11 to 24 (`FTN11`..`FTN24`),
  which the main program does not open. NASTRAN-95 writes `op4_unit<n>.tmp`, rewritten on
  the way out into MSC's formatted layout: header `NCOL NROW FORM TYPE` (4I8), name (A8),
  `1P,5E16.9`; real matrices out as type 2 and complex as type 4 with real, imaginary
  pairs; per column `IC IR NW` (3I8) then the values five to a line; null columns left out
  (as MSC's own OUTPUT4 does); a trailer column `NCOL+1 1 1` and one `1.0`; a negative
  (symmetric) NASTRAN-95 form made 6. `PHIG`/`PHIA` are cut to the mode count the print
  file was (UIM 9131). `N95_KEEP_OP4=1` keeps the raw file.
- The SOL 145 driver converts each child's files into the output directory as
  `<name>_s<subcase>.<ext>` (UIM 9133), so each Mach's `QHHL` is its own file, n_h rows by
  n_h x n_k columns, one block per k; `MHH`/`KHH`/`BHH` come out per subcase too though
  they are the same in every child. UWM 9134 when no child wrote the file.
- **The order of the blocks is the order of the (Mach, k) list**, and NASA's APD wrote that
  list card by card in sorted bulk data order, so a k list split over several `MKAERO1`
  cards came out shuffled (a two-card deck put k = 1.6, 3.2 ahead of 0.01 .. 0.8; Simcenter
  does the same and does not sort). The flutter and gust interpolations never cared; a
  reader of the exported blocks that assumed ascending k built a plant whose roots were
  nothing like PK. APD now sorts the pairs by Mach then k (`APDMKS` in `mis/apd.f`) and
  prints the list it used as UIM 9457, per child; VehicleDesign's `read_nastran_qhhl`
  takes the order off that print.
- **Known (2026-10-01): the export needs a cold flutter run.** A flutter deck restarted off
  the modes checkpoint is a modified restart with a rigid-format switch, and NASTRAN
  executes only the DMAP instructions its restart tables flag; an ALTERed OUTPUT4 has no
  table entry, so the children leave empty `op4_unit*.tmp` files and the driver reports
  "no child wrote" (UWM 9134). Each child then has to solve the modes again (85-95 s on the
  monarch, in parallel), which is what the export costs until the restart flags the alter.

The repo's `OUTPUT4_rd.m` reads the files; `test_ase_plant` replays FA1's arithmetic on the
export and gets the printed PK roots back to 1e-4 (skill `nastran-flutter` §8).

## 10. Console verdicts and exit codes

The exit handler (`mds/hexit.f`, registered with `atexit`) reads the print file back and
sets the code; the fatal explainer (`msc/mscdiag.c`) runs in both executables.

| exit | console line | meaning |
|---|---|---|
| 0 | `nastran: <stem> -> <dir><stem>.out` | `END OF JOB` reached, no fatal |
| 0 | `nastran: fewer modes than requested were found (UWM 2390 ...)` | FEER reseeded and stopped early; the modes printed are accurate; the count is in the print |
| 0 | `nastran: the mass matrix is singular or indefinite ... (UWM 2394)` | lumped `CONM2`s with no rotary inertia (UWM 9133 said so up front) |
| 0 | `nastran: this solution needs a mass matrix and the model has none ...` | the rigid format stopped in its own check: no eigenvalue table, no results |
| 1 | usage, `deck not found`, `deck name too long`, `cannot write <print file>` | bad arguments or a missing file |
| 2 | `nastran: <stem> did not reach END OF JOB - see <print>` | a crash; the print stops where the solver was |
| 2 | `nastran: stopped after N minutes of wall clock, the limit for this run` | the watchdog; the scratch directory is removed |
| 3 | `nastran: the solver stopped on this message:` + the first fatal line + `WHAT` / `FIX`, then `nastran: <stem> finished with a FATAL message - see <print>` | a `USER` or `SYSTEM FATAL MESSAGE` (either spelling: runs of blanks are collapsed before matching, so `*** USER FATAL    MESSAGE  3056` counts). A number with no entry prints `(no explanation on file for message n; the NASTRAN User's Manual, section 6, has the list)` |
| 3 | `the deck was not translated, so nothing was solved` (UFM 9002) | a translation fatal (UFM 9xxx names the card, file and line); no print file; read `<stem>_xlat.txt` |
| 3 | `finished with a FATAL message` with only `I/O SUBSYSTEM ERROR NUMBER nnn`, `I/O ERROR # n ON FILE ... NAME=xxx` (a buffer dump and the database directory follow) or a bare `ERRTRC CALLED` in the print | GINO could not read or write a block, or a module stopped without saying why; such runs used to exit 0 with `END OF JOB` and no results. Keep the deck and `<stem>.log` |
| 3 | `nastran: this deck is written in MSC Nastran's dialect (SOL 1xx, INCLUDE, free-field cards)` | you ran `nastran95` on an MSC deck |
| 3 | `nastran: the solver stopped without a numbered message, which is how it runs out of open core` + the `-8` entry | an out-of-core stop (`ERRTRC` after a data block table) |

`*** USER POTENTIALLY FATAL MESSAGE` is a note eight of NASA's demonstration decks print
and run past; it is not a fatal to either scanner. Exit 0 is not "120 modes": read the
`fewer modes` line (skill `nastran-analysis` §5, §8).

## 11. The print file

`msc/mscf06.c` rewrites the print file in place on the way out, changing layout and never
a number: the eigenvector banner carries `CYCLES =` and the mode number where MSC puts
them; exact zeros are `0.000000E+00`; the eigenvalue table has MSC's sub-banner (`(BEFORE
AUGMENTATION OF RESIDUAL VECTORS)`) and no blank between header and rows; the weight
generator's rows sit at MSC's columns; the sorted-echo banner is spelled as MSC spells it
(the 1995 print has one more space between the words); renumbered ids are restored; modes
past the number requested are cut; no line is zero-length. A line it does not recognise is
copied through. Hence VehicleDesign's readers (`read_nastran_eig`, `read_nastran_grids`,
`read_nastran_mass`, `read_khh`, `read_nastran_flutter`, `read_nastran_pk_eigenvectors`,
`read_nastran_complex_eigenvectors`, `read_nastran_aero_loads`, `read_nastran_qhhl`) read
either solver's print.

A flutter print holds, per subcase: the sorted bulk data echo (`ECHO = SORT`); UIM 9457
with the sorted (Mach, k) list; the `REAL EIGENVALUES` table (a restart's spliced in from
the modes run); the PK modal vectors of marked loops (and of every loop with `PKVECT`);
`ROOT TRACKING` lines; the `PK METHOD FIALED TO CONVERGE` warnings; the `FLUTTER SUMMARY`
blocks - with `(MATCHED POINTS)` one block per root (`POINT = n METHOD = PK`), a row per
matched point with columns `KFREQ`, `1./KFREQ`, `DENSITY`, `MACH NO.`, `VELOCITY`,
`DAMPING`, `FREQUENCY`, `COMPLEX EIGENVALUE` (format `F9.4` then eight `E13.6`,
`mis/fa2.f` formats 1043 and 1070); COSMIC's own PK layout is one block per (Mach,
density) group with the Mach and density ratio in the header - then the `COMPLEX
EIGENVECTOR` tables of the marked loops and the `AERODYNAMIC LOADS (UNIT DYNAMIC
PRESSURE)` tables when `AEROF` is asked for. `read_nastran_flutter` reads both summary
layouts.

## 12. The message catalogue (`msc/mscdiag.c`)

The first fatal in the print is repeated on the terminal with `WHAT` and `FIX` from a
catalogue of 41 entries, each audited against `um/MSSG.TXT`, the emitting routine and,
where a deck could provoke it, a run (`HALO.md`, "The fatal messages"). The numbers are
NASA's, not MSC's. Condensed:

| number | what | fix |
|---|---|---|
| 300 | a number or word on a card could not be read (field underlined in the print; `ERROR IN XRCARD ROUTINE` for control cards) | fix the field: two decimal points, a dangling exponent, an embedded blank; delete blank lines before `CEND` |
| 307 | a bulk card name the solver does not know (printed after the message) | NASTRAN-95 has `CQUAD4`/`CTRIA3` but no `CBEAM`, `CBUSH`, `RBAR`, `RBE2`, `EIGRL`, `PBARL`; `nastran95ase` translates `CBUSH`, `RBAR`, `RBE2`, `EIGRL`, `PBARL` (the entry's own text claims `CBEAM` too; see §5.4) |
| 311 | two cards of one type share an id | grid, element, property and material ids unique per type; load and constraint sets may share |
| 313 | a card's word count is not one its layout allows | usually an MSC trailing field (`OFFT` in field 9 of a `CBAR`) or a missing continuation |
| 315 | wrong kind of value in a field (integer for a real, MSC's `SEID` on a `GRID`) | decimal point on every real, none on integers, clear fields the 1970s layout leaves blank |
| 316 | illegal data (negative id, component digit above 6); a consequence when 300 came first | compare with the User's Manual layout |
| 340 | DISP rigid formats 10-12 need `PARAM LMODES` or `LFREQ`+`HFREQ` | add one of them |
| 505 | unknown executive control card, or a missing `CEND` | `NASTRAN BANDIT=-1` must be line 1; MSC's `ASSIGN`s are not executive control here |
| 507 | an executive card in the wrong form (`IMHERE` 530: `ID` needs two names; 110: `TIME` an integer) | fix the echoed card |
| 615 | `SET` named by a non-integer | `SET 10 = 1,2,3` |
| 617 | a case control card with no value or a non-integer one; a `PARAM` in case control | move `PARAM`s below `BEGIN BULK` |
| 2007 | an element names an undefined grid | usually an `INCLUDE` left out of a sub-configuration deck |
| 2010 | an element names a missing property | add it, or include the common file |
| 2050 | a `SUPORT` names a grid that does not exist | fix the `SUPORT1` in the MSC deck |
| 2053 | `SPC = n` selects a set no card carries | match the id; a free-free `SUPORT` run needs no `SPC` |
| 2101A | a degree of freedom in two dependent sets | make the grid carrying the SPC or SUPORT the independent end of the rigid element |
| 2140B | a grid id above 2,147,483 with `SEQGP`/`SEQEP` levels | renumber, or drop the sequence cards |
| 2192 | a rigid element names an undefined grid (the id is the RBAR's) | include the component file the joint attaches to |
| 2200 | the rigid-body mass matrix on the `SUPORT` freedoms is singular | rotary inertia on the `CONM2`s, or the missing lumped-mass include |
| 2215 | a property names a missing `MAT1` | add it; sub-configurations must include the common file |
| 2423 | a degree of freedom made dependent twice | one rigid element per dependent grid; `DIAG 21` maps SIL to grid |
| 3005 | a zero pivot in the named routine (`KLL`/`MAA` in FACTOR; `SCRATCH2` in MCE1B: dependent MPC equations; `(NONE)` in SMA3A: a singular GENEL) | constrain or connect the columns UFM 3097 lists; give them mass; rewrite the MPCs |
| 3008 | a module ran out of open core (`ADDITIONAL CORE REQUIRED` when known) | raise `OCMEM`; rebuild with a larger `NASTRAN_OPEN_CORE_WORDS` |
| 3019 | `MAXLINES` exceeded | `MAXLINES = 999999` (nastran95ase does), or print less |
| 3031 | a selected set is not in the bulk data (`SLT` the static LOAD, `DLT` the DLOAD, `FRL`, `TRL`, `EED`, `NLFT`) | a set holding only `SPCD` is never found: add a zero `FORCE` |
| 3032 | `METHOD = n` found no `EIGR` of that id (`EED`), or a `DEFORM` set | match the ids; nastran95ase writes `METHOD` from the `EIGRL` |
| 3037 | the generic `JOB TERMINATED IN SUBROUTINE xxxx`; from SCHEME it is BANDIT | read the message above it first; `NASTRAN BANDIT=-1` or `BANDTDIM=n` |
| 3056 | a `GRAV` load with no mass matrix | `RHO` on the `MAT1`, `CONM2`s or `NSM`; or take the `GRAV` out |
| 3097 | the named block is singular in the listed columns (`KLL`: no stiffness or a mechanism; `MAA`: GIV/MGIV on massless rotations) | `SPC1` or connect; `PARAM AUTOSPC 1`; on `MAA` use FEER, `OMIT1 456`, or add I11/I22/I33 |
| 6206 | the SOF's `PASSWORD` is not the one it was created with | use the earlier password; one SOF per phase set |
| 3118 | a `CROD`/`CONROD` of zero length | a spring between coincident points is a `CELAS2` |
| 3145 | a constraint card with a blank or zero component | put the component digits in |
| 3147 | two `SPC` cards on one component, one with an enforced displacement | one `SPC` per component; plain fixities on `SPC1` |
| 3176 | a `CBAR` cannot form its axes (coincident ends, or the orientation vector along the bar) | a vertical spar gets `0. 1. 0.` |
| 3178 | a `LOAD` combination names a set no load card carries | fold `LOAD`s into one; select `SPCD` sets directly |
| 3179 | a `LOAD` lists one set twice | fold the factors |
| 2386 | FEER's shifted stiffness still singular after two shifts of 100x: freedoms with neither stiffness nor mass | constrain, give mass, or `ASET`/`OMIT` them |
| 2391 | FEER's reduction produced no usable rows; follows a start vector with no positive mass norm | look for UWM 2394; rotary inertia; no negative `CONM2` mass |
| 2395 | FEER's QR got a NaN or ran 200 sweeps per eigenvalue plus 1,000 (NASA's loop had no bound) | as 2391; without a 2394, report the deck (`DIAG 16`) |
| GINO | `I/O SUBSYSTEM ERROR NUMBER nnn`: a file read as written wrong, disk or file slots exhausted | a solver logic defect unless the scratch disk is full; report with the deck and `.log` |
| -8 | out of open core with no number printed | as 3008 |

### 12.1 The front end's own messages (9000 series, `msc/mscmsg.c`)

Three-part shape (what, where, fix); ranges: 9000-9099 reading the deck, 9100-9199
executive and case control, 9200-9299 bulk data, 9300-9399 the model as a whole, 9400-9499
SOL 200 and the SOL 145 driver, 9500-9599 results. The ones a run meets:

| number | message |
|---|---|
| UIM 9000 | reading `<deck>` in the MSC dialect |
| UIM 9001 | the translation summary (`SOL n became rigid format r ... c cards written ...`); also a continuation with no card before it (UWM) |
| UFM 9002 | the deck was not translated, so nothing was solved |
| UFM 9010-9013 | an `INCLUDE` whose name cannot be read; nested too deep; a file that cannot be opened; no bulk data (`BEGIN BULK` missing) |
| UIM 9101 / UWM 9102 / UIM 9103-9104 / UWM 9105 | case control dropped (MSC's, with its cost) / unknown / options dropped, `APRES` to `AEROF` / a `THRU` range bounded by a renumbered id |
| UFM 9110 / 9111 / 9112 | a SOL with no 1970s equivalent / no recognised SOL / cannot write the translated deck |
| UIM 9113 / 9114 / UWM 9115 / UIM 9116 / 9117 / 9119 | `BANDIT=-1` written / `DISPLACEMENT = ALL` added / several subcases, the first run / `EIGC` to HESS / `IMETH` to L / `GUSTAERO` sign flipped |
| UWM 9120, UIM 9121, UFM 9122, UWM 9123, UIM 9124, UWM 9125-9129, UIM 9130-9131, UWM 9132 | OUTPUT4: an unreadable `ASSIGN`; another `ASSIGN` ignored; an unknown data block; a unit never written to; the mapping; wiring, missing or malformed files; the converted matrix; the mode cut; a short column |
| UIM 9123 | `FLUTTER`: `PKNL` became `PK` with `PARAM PKMATCH 1` (the number is shared with the OUTPUT4 warning above) |
| UWM 9133 / UIM 9133, UWM 9134 | `CONM2`s without rotary inertia (`msc/mscxlat.c`) / OUTPUT4 per-subcase files written (`msc/mscop4.c`) - the same number serves both / no child wrote the OUTPUT4 file |
| UFM 9203, UIM 9209, UIM 9211, UIM 9214, UFM 9206, UFM 9299 | a non-prismatic `CBEAM`; `PBARL` to `PBAR`; `EIGRL` to `EIGR FEER`; `SUPORT1` to `SUPORT`; a separated `CBUSH` with K2..K6; an unknown bulk card |
| UIM 9301, 9302 | freedoms constrained for want of stiffness; ids renumbered |
| UIM 9400 | SOL 200 |
| UIM 9449, 9450, UFM 9451, 9452, UWM 9453, 9454, UIM 9455, UIM/UWM 9456 | the SOL 145 driver: one child per subcase; the job count; a subcase not translated; a child that could not start; a child without a print; the joined print not rewritten; the child's Mach; the loads velocity |
| UIM 9457 | from APD in the solver's print: the sorted (Mach, k) list |
| UFM 9460, UIM 9461-9464, UFM 9465, UWM 9466, 9467 | restart: deck, dictionary or tape missing; the restart described; the dictionary lines; the cards dropped and kept; `scr=no` checkpointing; the tape could not be placed; the modes print missing; non-aero cards that will re-solve the modes |
| UIM 9470 | `NASTRAN SYSTEM(270)`: which doublet-lattice kernel runs |

Messages wrap at 72 columns on a space, a word wider than the margin (a path) left whole.

## 13. Troubleshooting

1. **Exit code and the console first.** The first fatal is repeated with WHAT/FIX. `UFM
   307` is a card name the 1970s solver does not know; a fatal on `nastran95` followed by
   "this deck is written in MSC Nastran's dialect" means you wanted `nastran95ase`; `UFM
   9299` a card the front end does not know (`CBEAM` is one: use `CBAR`); `UFM 9206` a
   separated `CBUSH` with K2..K6; `SFM 3008` out of open core,
   raise `OCMEM`; `UFM 2007`/`2192`/`2010`/`2215`/`2053` an `INCLUDE` left out of a
   sub-configuration deck.
2. **`<stem>_xlat.txt`.** "dropped: MSC only" is fine; "dropped: no stiffness" should be
   only the vis panels; a large count of "degrees of freedom constrained for want of
   stiffness" means a component is floating (a missing joint include, a wrong SPC id);
   "ids renumbered" must include `PARAM,GRDPNT` when the deck has it.
3. **`<stem>_n95.dat`** is the deck actually solved. When a number disagrees with
   Simcenter, diff it against VehicleDesign's reference translator
   `msc_deck_to_nastran95(deck)` card by card; a value-level diff found the eight-column
   truncation bug in minutes.
4. **`ERRTRC CALLED` with no numbered message**: out of open core; set `OCMEM` higher.
5. **Fewer modes than asked (UWM 2390)**: the mass matrix is semi-definite (UWM 2394,
   9133). The modes found are valid. Ask for fewer, or give the lumped masses rotary
   inertia - and say why rather than doing it silently. `DIAG 16` prints the FEER
   reduction row by row.
6. **A flutter run with no summaries and exit 0** is a thing of the past: GINO's `I/O ERROR
   #` and `ERRTRC` now count as fatal. If a child dies at a late loop with `BLOCK NUMBERS
   INCONSISTANT ON OPEN IN DBMMGR`, `DBMEM` is set in the environment: unset it.
7. **AEROF tables full of 1e-19, or gust loads that do not change with `WG`**: an
   executable older than fork a262158 (NASA's `UNPACK` limit at row 65,536). Rerun with the
   current build; any model past about 10,900 grids is suspect on an older one.
8. **Jagged PSD response above 1.5 Hz**: an executable older than fork 09c8681 (the k
   spline in single precision).
9. **A turbulence deck stopping with UFM 316 on its `TABLED1`**: older than fork d5e0e5f
   (cards truncated at 256 fields).
10. **The export files are empty after a restart run**: the OUTPUT4 alter is not executed
    on a modified restart; run the flutter deck cold (section 9).
11. **A crossing that moves between two builds or flows**: look for a one-point spike on
    the V-g plot first (section 7.6), then compare the (Mach, k) list (UIM 9457) and the
    kernel (UIM 9470): the quartic and parabolic kernels differ by 0.06-0.35 % in the
    monarch's crossings and the OpenBLAS LU moves marginal roots. `N95_AJJ_SOLVE=builtin`
    with `N95_DLM_QUARTIC=0` reproduces Jon's `halo-ase-sol145` print line for line.
12. **GINO `I/O SUBSYSTEM ERROR NUMBER 102 ... AJJL` on runs side by side**: the scratch
    disk (or tmpfs) is full. Budget runs x 5 children x 2-4 GB plus the cache; set
    `TMPDIR`; delete `n95_<pid>` folders of dead processes (check the process is gone).
13. **A watchdog stop on a big deck**: `N95_TIMEOUT=<minutes>`; the tests set 3 minutes so a
    regression fails instead of hanging. On Linux limits past 71.6 minutes wrapped until
    fork 9ba11cc (`usleep`'s 32-bit argument); `nanosleep` now.
14. **`nastran` resolves to the wrong solver.** VehicleDesign's `nastran.bat` shim runs
    `nastran95ase.exe` when its folder is first on the PATH, which shadows Simcenter's
    `nastran` for that shell; undo it when done. Never create a `nastran95.bat` next to the
    exe (cmd resolves a bare name to the `.exe` first).
15. **Profiling**: not from `<stem>.log`'s CPU column. Use `GFORTRAN_UNBUFFERED_ALL=y` and
    time the `BEGN`/`END` lines, or skill `nastran95-performance`'s `scripts/`.

## 14. Worked examples

From VehicleDesign's repo root on Windows (`cmd`); on Linux replace `.exe` with the bare
name and `\` with `/`:

```
cd ase\vibe
..\..\utilities\open_source_software\NASTRAN\nastran95ase.exe monarch_ff_asm8083_vibe.dat
        outputs beside the deck (what ZAERO wants: <config>_vibe.out, .phg, .mgg)
..\..\utilities\open_source_software\NASTRAN\nastran95ase.exe monarch_gvt_gvt.dat C:\runs
        outputs in C:\runs; the repo stays clean
..\..\utilities\open_source_software\NASTRAN\nastran95ase.exe mfp3p2_vibe.dat out=C:\runs scr=yes
        MSC launcher spellings
```

A flutter study with the cache and the Mach children one at a time, and Jon's print
reproduced (skill `nastran95-performance` §3):

```
cd ase\flutter\nastran_monarch_demo_asm1083
set EXE=..\..\..\utilities\open_source_software\NASTRAN\nastran95ase.exe
%EXE% --version
set N95_AERO_CACHE=D:\n95cache
set N95_TIMEOUT=240
%EXE% monarch_demo_asm1083_flutter.dat C:\temp\run1
rem the same deck as Jon's halo-ase-sol145 build prints it
set N95_AJJ_SOLVE=builtin
set N95_DLM_QUARTIC=0
%EXE% monarch_demo_asm1083_flutter.dat C:\temp\as_jon
rem every speed-up off, one Mach at a time
set N95_JOBS=1
set OMP_NUM_THREADS=1
set N95_PK_THREADS=1
set N95_DLM_THREADS=1
set N95_AMG_BATCH=0
set N95_AMP_PIPE=0
set N95_BLAS_THREADS=1
set N95_ISA=0
%EXE% monarch_demo_asm1083_flutter.dat C:\temp\serial
```

The modes once, then the flutter deck off them (the `run_nastran95ase.bat` /
`run_nastran95ase.sh` in every flutter run folder is these two lines):

```
nastran95ase monarch_demo_asm1083_modes.dat scr=no
nastran95ase monarch_demo_asm1083_flutter.dat restart=monarch_demo_asm1083_modes.dat
```

From MATLAB (the shape the tests use; `call` keeps `cmd.exe` from eating the quotes):

```matlab
set_up_path
exe  = fullfile(VehicleDesign_root, 'utilities', 'open_source_software', 'NASTRAN', 'nastran95ase.exe');
deck = fullfile(VehicleDesign_root, 'ase', 'vibe', 'monarch_gvt_gvt.dat');
outd = tempname; mkdir(outd);
[rc, cmdout] = system(sprintf('call "%s" "%s" "%s"', exe, deck, outd));
assert(rc == 0, cmdout);                 % 1 bad args, 2 no END OF JOB, 3 FATAL
prt = fullfile(outd, 'monarch_gvt_gvt.out');
[f, phi, nodes] = read_nastran_eig(prt);
```

A NASA demonstration deck through the 1970s executable:

```
nastran95 inp\d10021a.inp C:\runs\demos
```

The fast check of the whole SOL 145 path is VehicleDesign's
`test_nastran95ase_flutter_subcases.m` (a cantilever with one `CAERO1` panel, two Machs,
three matched points each, the third marked, through the driver in a second, and the same
deck restarted off its modes checkpoint); the monarch deck's lowest crossing per Mach is
held by `test_nastran95ase_aeroelastic.m` (skill `build-nastran95`).
