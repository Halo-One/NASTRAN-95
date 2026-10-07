---
name: build-nastran95
description: Building nastran95 (NASA's 1995 COSMIC NASTRAN-95 as NASA wrote it) and nastran95ase (the same solver behind the MSC-dialect front end, with SOL 144/145/146/200 and OUTPUT4) from this checkout of Halo One's fork, on Windows (pinned winlibs gfortran 16.2, static msvcrt link, fixed image base) and on Linux (the machine's gfortran 13+, static, -no-pie), with OpenBLAS 0.3.34 built once - the scripts in standalone/build/ and their switches (-Source, -Commit, -Branch, -InstallDir, -WorkDir, -NoInstall, -Clean), and shipping a release build to VehicleDesign; the flags and source changes that are load-bearing and why (-O0 and the fenced optimised files, the kernels built twice, the OpenBLAS thread count, -fno-automatic, the dialect flags, open core, BANDIT, the FEER guards, the libgfortran --wrap, the watchdog, the scratch clean-up, ids up to 99,999,999 and the renumbering check, the OUTPUT4 traps); changing the solver or the front end; adding a DMAP module (MPL, link table, XSEM00 dispatch); verifying a build and checking Windows against Linux; the Windows-only pieces; what a macOS port would still need. Use when an executable needs building, when the solver or the front end changes, when a build fails or a result moves between builds or platforms, or when porting to another OS.
---

# Building nastran95 and nastran95ase

Two executables, built by one command from this checkout, on each of two platforms:
`nastran95.exe` / `nastran95ase.exe` on Windows and `nastran95` / `nastran95ase` on Linux.
Below, a name without an extension means either. The two platforms' builds agree digit for
digit on the models they have been compared on; the one NASA demo whose verdict differs is
in *Porting*.

| executable | what |
|---|---|
| `nastran95` | NASA's NASTRAN-95 (April 1995 COSMIC release) reading the 1970s input it was written for: `ID`/`APP`/`SOL n,0`, fixed 8-column bulk data. NASA's 132 demonstration decks are in `inp/`, their 1995 prints in `demoout/`. |
| `nastran95ase` | The same solver behind an MSC-dialect front end: reads free-field decks with split `INCLUDE`s, `SOL 103`, `RBAR`, `RBE2`, `CBUSH`, `EIGRL`, `PBAR`/`PBARL`, `CQUAD4`, `CTRIA3`, `PSHELL` and more, rewrites them for the solver, runs, and rewrites the print file into the MSC layout (the one Simcenter prints); honours `ASSIGN OUTPUT4` and the `OUTPUT4` alters; runs SOL 144 (static aeroelastic trim), 145 (flutter), 146 (gust) and 200 (design optimisation). |

Read these rather than this skill for what they cover: `standalone/README.md` (running, the
MSC dialect, SOL 144/145/146/200, OUTPUT4, Linux, verification), `HALO.md` (every change
against NASA's tree, its reason and its check), `SOL144.md`,
`doc/nastran95ase_users_manual.md` and `doc/nastran95ase_theoretical_manual.md`. Every flag
is explained where it is set, in the comments of `CMakeLists.txt` and in the header comments
of the two build scripts; read those first. This skill is the procedure around them, and the
reasons that are easy to undo by accident.

## Build

```
powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1                # this checkout -> standalone\; ~3-6 min, ~40 the first time (toolchain, OpenBLAS)
powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1 -NoInstall     # a candidate, left in the build tree
powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1 -Source <dir>  # another local tree
powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1 -Commit <sha> -Branch <name>   # that commit of Halo-One/NASTRAN-95, downloaded: a release build
powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1 -Clean         # throw the work directory away first (OpenBLAS again too)

bash standalone/build/build_nastran95.sh                         # ~15-40 s on 32 cores; ~100 s the first time (OpenBLAS)
bash standalone/build/build_nastran95.sh --commit <sha> --branch <name> --install-dir <dir>
NASTRAN_BUILD_JOBS=4 bash standalone/build/build_nastran95.sh    # on a machine busy with something else (default: every processor)
```

The switches, the same on both (`-Source` is `--source`, `-InstallDir` is `--install-dir`,
and so on):

- **What is built.** By default the checkout the script is in; the build record names its
  HEAD (and `origin`) and says `+ local changes` when tracked files are modified.
  `-Source <dir>` builds another local tree the same way. `-Commit <sha>` (the full 40
  characters, so the record is unambiguous) downloads that commit's archive from GitHub
  into the work directory and builds it: the record then names exactly that commit, which
  anyone can fetch. `-Branch <name>` goes with it, since an archive has no branch to read.
  `-Source` and `-Commit` exclude each other. Anything that leaves this machine is a
  `-Commit` build of a pushed commit.
- **Where it goes.** `-InstallDir <dir>` takes the executables and the build record
  (default `standalone/`, where both are gitignored). Point it at VehicleDesign's
  `utilities/open_source_software/NASTRAN` to update that repository's copies.
  `-NoInstall` builds and checks but leaves the executables (and, on Linux, the record) in
  the build tree, a candidate beside the installed pair.
- **The cache.** `-WorkDir <dir>` holds the toolchain (Windows), OpenBLAS and one CMake
  build tree per source (`cmake-build-local` for this checkout, so a rebuild is
  incremental). Default `standalone/build/work/`, gitignored; `-Clean` deletes it first.

`--version` prints the build id: the branch less its `halo-ase-` prefix, the commit's first
eight characters (`+` when the tree had local changes), the compiler and the date, at most
56 characters (it becomes a Fortran character constant).

The Windows script downloads winlibs GCC 16.2 (260 MB, MSVCRT flavour, pinned by URL **and
SHA-256**) and OpenBLAS 0.3.34 (URL and SHA-256), builds OpenBLAS once, configures the tree
with CMake + Ninja from that toolchain (`CMAKE_BUILD_TYPE=Debug`, which is -O0; open core
256,000,000 words; `NASTRAN_LAPACK_LIBRARIES` set to that OpenBLAS; `NASTRAN_BUILD_ID`),
builds ~1,850 Fortran files and the `msc/` C, strips both executables, checks with `objdump`
that they import only `KERNEL32.dll` and `msvcrt.dll`, runs each `--version`, installs them
(the CMake target `nastran.exe` becomes `nastran95.exe`) and writes `BUILD_INFO.txt`: the
source and commit, the compiler, the flags, the fenced optimised files, OpenBLAS, the link,
open core, the date and the machine's name, and the size and SHA-256 of each executable.
(The machine's name is one more reason the record stays out of this repository.)

The Linux script is the same sequence with these differences, and its header says so:

- the toolchain is the machine's own gfortran (13+; `-fdec`, `-fallow-argument-mismatch`
  and `-fallow-invalid-boz` are not all there before), recorded in `BUILD_INFO_linux.txt`
  rather than pinned by hash;
- OpenBLAS is built with its Makefiles. `make libs` and `make netlib` run one after the
  other: together under `-j` they write one archive at once and corrupt it ("file format
  not recognized");
- the link is `-no-pie` (with `-fno-pie` on the way in, both from `CMakeLists.txt`)
  instead of the three PE flags, and `-static` folds glibc in too, so the executables run on
  any 64-bit Linux. The script checks the result rather than the flags: no `INTERP` segment,
  ELF type `EXEC`, and the top of the image (the highest `LOAD` vaddr + memsz in `readelf
  -lW`) below 2 GB;
- it strips the installed copy, not the build tree's output: ninja tracks that file, and
  rewriting it behind ninja's back makes the next incremental build's Fortran dyndep plan
  assert;
- it installs beside the `.exe` files with its own record, `BUILD_INFO_linux.txt`, so one
  folder holds both platforms' builds.

The Linux link prints two warnings that are expected: the `system_` symbol size (NASA's
BLOCK DATA, on every build) and libgomp's static `dlopen` (offload plugins, never used).

**OpenBLAS**, the same on both, for the in-core doublet-lattice solve and SOL 144's dense
algebra: `DYNAMIC_ARCH TARGET=GENERIC` with `DYNAMIC_LIST="PRESCOTT SANDYBRIDGE HASWELL
SKYLAKEX"` (one kernel set per x86-64 ISA level, SSE3, AVX, AVX2+FMA, AVX-512; every other
core falls back to the nearest one below, a Core Ultra to HASWELL, a Zen 5 to SKYLAKEX, and
the Zen 5 gets the same ZGETRF speed), `USE_OPENMP` (the solver's own libgomp, not a second
thread pool), `NO_AFFINITY` (the SOL 145 driver runs its children side by side), static,
the Fortran interface and LAPACK only. That list keeps each executable at 19.5 MB (33 MB
with every core's kernels, 13 MB without OpenBLAS). Windows builds it differently: *Windows*.

**The source of the shipped executables.** Since 2026-10-06 both platforms' executables
come from one commit, **821d508** (`halo-ase-sol144-plant`): `halo-ase-plant` 67c00ab (the
ASE plant's OUTPUT4 export of QHHL / QHJL / QKHL / MHH / KHH / BHH / PHIDH, the sorted
(Mach, k) list, the control-surface modes, ids up to 99,999,999) merged with
`halo-ase-sol144-corrections` 01d59c9 (SOL 144 static aeroelastic trim, W2GJ / FA2J / WKK,
DIVERG, half models, any SUPORT, CAERO4 strip-theory divergence). It contains every other
`halo-ase-*` branch whole: Jon's `halo-ase-sol145` (SOL 145, the parallel driver, SOL
146), `halo-ase-sol145-linux` (the POSIX port and the flutter speed-ups),
`halo-ase-sol145-perf` (the aerodynamic cache, the quartic doublet-lattice kernel, the
Windows pieces, the scratch clean-up, UNPACK past row 65,536, the SOL 146 fixes) and
`halo-ase-sol144`. Both builds print `sol144-plant 821d5087` (gfortran 15.2 on Linux,
winlibs 16.2 on Windows). This branch, `halo-ase-nastran95ase`, carries on from 821d508 and
adds `standalone/` and these skills. Before 2026-09-26, and again from 09-30 to 10-06, the
two platforms were built from different commits; the dated detail is in the git history
and `HALO.md`. What came of it is one rule: **both platforms build the same commit, and
both build records name it.**

### The checks after a build

Do not skip them.

```
standalone\nastran95ase --version                                              # the build id
standalone\nastran95 standalone\examples\cantilever\cantilever_modes.inp <scratch>   # then diff its .out with the committed one, clock and date lines aside
python standalone/test/run_decks.py                                             # any machine, no MATLAB (python3 on Linux)
```

(On Linux, the same with `/`.) `run_decks.py` runs every deck in `standalone/test/decks/`
and `standalone/examples/flat_plate/` to exit 0, then checks the numbers, about a minute in
all: the flat plate's three solutions against each other (SOL 144's divergence against SOL
145's real root, SOL 146 at its lowest frequency against SOL 144's static deflection, the
gust PSD against |H|^2 times the von Karman spectrum), the typical section's divergence
against its closed form, the guide examples against Simcenter's prints, the small
aircraft's 1 g trims, and the two-subcase flutter deck cold and restarted. Its docstring
has the whole list. For a `-NoInstall` candidate give it `--exe <path>`; the build script
prints the line.

Then the MATLAB tests. They need VehicleDesign's readers (`VEHICLEDESIGN_ROOT`, or a
`VehicleDesign` checkout beside this one) and without them stop Incomplete rather than
failed (`disp(table(r))` tells the two apart):

```
matlab -batch "r = runtests('standalone/test/test_nastran95ase_flutter_subcases.m'); assertSuccess(r)"   % seconds: the SOL 145 driver, the joined print, the eigenvectors, the threaded solve against the serial one line for line, the restart off the modes checkpoint
matlab -batch "r = runtests('standalone/test/test_nastran95ase_trim.m'); assertSuccess(r)"               % seconds: SOL 144 on a small free-free aircraft, the readers and the physics
matlab -batch "r = runtests('standalone/test/test_nastran95ase_trim_corrections.m'); assertSuccess(r)"   % ~20 s: W2GJ / WKK / FA2J against exact equivalents, a typical section's divergence, the guide's HA144A, HA144B (trim, divergence) and HA145C against Simcenter's prints in standalone/test/data/
```

`standalone/test/n95_platform.m` is the only place the tests decide which platform they
are on (the executables' names, the shell's quoting, the hasher, which `BUILD_INFO` to
read, where the machine's own facts live). A new Windows-ism in a test belongs in there,
not in an `ispc` at the call site.

NASA's aero demos (`d10021a`-`d10023a` flutter, `d11031a`/`d11032a` gust) run from `inp/`
with `nastran95 inp/<deck>.inp <outdir>`; all 132 against the previous build is in
*Verifying a build*. Before a build goes to VehicleDesign, its acceptance tests run there
too (*Shipping a build to VehicleDesign*).

**Line endings and the executable bit.** `.gitattributes` is `* -text`: git converts
nothing, so a file is committed with the endings it was written with. NASA's files and the
fork's Fortran are CRLF; `msc/`, `CMakeLists.txt` and the Markdown are LF. The scripts
with a `#!` line under `standalone/` and `.claude/skills/` (shell and Python) and the
extensionless `nastran` stand-in must be LF (a CRLF script fails on Linux) and committed
`100755` (a Linux checkout otherwise gets `Permission denied`); a move of these files once
lost both. `git ls-files --eol` and
`git ls-files -s` show them; Windows has no executable bit, so set it there with
`git update-index --chmod=+x <file>`.

## Windows and Linux: one source, checked

The 821d508 check (2026-10-06): the same 13 decks through both builds, one Linux
workstation and one Windows laptop, the prints compared as results.

| deck | Linux against Windows |
|---|---|
| monarch_demo_asm1083 vibe, SOL 103, 14,000+ ids over 2^24 | the same print, all 831,257 lines |
| five-Mach flutter, SOL 145, 83 roots x 40 points, the plant's OUTPUT4 export | every lowest crossing the same to 4 decimals; 98.9 % of root-points within 1e-3 Hz and 1e-4 g; the rest are roots the PK fitted after failing to converge (~145 warnings in each print), where the two systems' roundoff picks different fits; MHH / KHH / BHH identical, QHHL within 8.4e-7 of each matrix's largest entry |
| monarch trim, SOL 144, 23 subcases, plain and W2GJ + WKK + DIVERG | derivatives within 4e-5 of a column's largest; trim variables within 1e-6 rad |
| gust, SOL 146: PSD, 1-cos, seeded turbulence | within 1e-4 of each column's peak (1-cos: its worst, 0.6 %, a 3e-6 rad yaw rotation in a symmetric gust - antisymmetric roundoff) |
| the guide's HA144A, HA144B (trim, divergence), HA145C; trim_small_aircraft, divergence_typical_section; plate_flutter | within 2e-5 of each line's largest number |

VehicleDesign's SOL 145 acceptance test gives the same recorded crossings on both systems
(both kernels), and SOL 144 sits within 2e-5 (plain) and 5.5e-5 (W2GJ + WKK) of
Simcenter's derivatives on either.

**The method**, after anything that can move a number on one platform and not the other (a
change outside the C front end, a new compiler or OpenBLAS, a new optimised file): run one
deck list through each build into scratch, then compare the answers - crossings, roots as
matched sets, derivatives over a column's largest, matrices over their largest entry,
displacements over a line's largest number - not the print's text. The flutter print's
layout drifts wherever one system's PK needs a least-squares fit the other doesn't, and an
`OUTPUT4` alter in the deck moves every page break: compare through a reader (VehicleDesign's
flutter reader and root tracker), or with the page headers stripped, never line for line.
Windows' libm SIN/COS differ from glibc's, so bit-for-bit baselines are Windows against
Windows and Linux against Linux. Scale the check to the change: e6c5275 changed only the
translation (`msc/`), and its Windows rebuild needed the renumbering shift check and the
flutter tests, not the five-Mach parity runs.

## Shipping a build to VehicleDesign

VehicleDesign commits all four executables and both build records in
`utilities/open_source_software/NASTRAN/`. The executables there and the commit their
build records name must always agree, and a record that says `+ local changes` never
ships.

1. Push the commit to the fork.
2. On Windows: `powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1
   -Commit <sha> -Branch <branch> -InstallDir
   <VehicleDesign>\utilities\open_source_software\NASTRAN`. On Linux: `bash
   standalone/build/build_nastran95.sh --commit <sha> --branch <branch> --install-dir
   <VehicleDesign>/utilities/open_source_software/NASTRAN`. The same `<sha>` and
   `<branch>` on both, so both print the same label.
3. Check that `--version` names the commit on both, that `objdump -p` lists only
   KERNEL32.dll and msvcrt.dll on Windows, and that both build records name the same
   commit.
4. Run the checks above on both, the parity decks when the change calls for them, and
   VehicleDesign's acceptance tests: the monarch SOL 145 deck's lowest crossing per Mach
   against the recorded ones, SOL 144 against Simcenter's derivative fixture and SOL 146
   against Simcenter (~3 min on Linux, ~25 on Windows), the flutter comparison with
   Simcenter, and the monarch GVT worked example (no licence, half a minute; the Linux build
   reproduces its committed print line for line, 43,513 lines, and its committed outputs
   come from Linux, so re-run it there).
5. Commit the four executables, both build records and the worked example's outputs
   together in VehicleDesign, or the example names a build that is no longer there.

## The things that are load-bearing (do not "improve" them)

- **`-O0`.** `-O2` with gfortran 16 fails NASA demo d01013a (UFM 615 in the case-control
  parser) and segfaults on ten-element cantilevers. The code aliases INTEGER/REAL/DOUBLE
  through one COMMON block (`/ZZZZZZ/`, "open core"); the optimiser is entitled to break
  it. Speed is not what these executables are for; the 1,400-grid monarch GVT model does
  120 modes in 20 s, and the 4,500-5,400-grid vibe models in 85-95 s.
- **The fenced exceptions to `-O0`** (from `halo-ase-sol145-linux` on, and
  `halo-ase-sol145`'s `mis/ampcz.f` and doublet-lattice kernel before it): a short list of
  files in `CMakeLists.txt`, each with its reason beside it, compiled `-O2` or `-O3`
  **with `-ffp-contract=off`** (no fused multiply-add may move a bit), `-fautomatic` where
  threads call them, `-fopenmp` where they are threaded. They are new code or NASA's code
  rewritten without the dummy-argument aliasing -O2 would break, and each was checked
  against the unmodified solver line for line. Never widen the list by recompiling a NASA
  routine that passes one array as two dummies; the whole tree at -O2, even with
  `-fno-aggressive-loop-optimizations -fno-strict-aliasing -fwrapv`, prints 124 of NASA's
  132 demos differently and segfaults on 51. Skill **`nastran95-performance`** has the
  list, the checks and what each one bought.
- **The kernels built twice** (`fa1pkq.f`, `egnvct.f`, `tkerv.f`): CMake writes copies of
  them into `<build>/isa/` at configure time, renamed `...V3` with their HALO-ISA hand-over
  block left out, and compiles them `-march=x86-64-v3`; `msc/mscisa.c` picks them when the
  processor has AVX2 (`N95_ISA=0` keeps the baseline). That is why the executables still
  run on any x86-64 and are 10-17 % faster on the kernels where they can be. Do not add
  `-march` anywhere else: the baseline must stay baseline. And any new optimised file must
  be checked with `nm file.o | grep _ZGV`: gfortran on glibc vectorises SIN/COS/EXP loops
  into libmvec, which rounds differently (`!GCC$ NOVECTOR` on those loops).
- **OpenBLAS has to be told its thread count** (`mis/ampczs/ampczt_openblas.f`): its OpenMP
  build reads its thread ceiling in its own constructor, which in a static executable runs
  before libgomp has read `OMP_NUM_THREADS`, so it records 1 and every ZGETRF is serial
  while `openblas_get_num_threads()` reports 8.
- **`-fno-automatic`.** 1970s locals are assumed static between calls.
- **`-std=legacy -fdec -fallow-argument-mismatch -fallow-invalid-boz`.** The dialect. Never
  add `-fdefault-real-8` or `-fdefault-integer-8`: open core is word-addressed and 4 bytes
  a word, and the build would succeed and the answers would be garbage.
- **Static link, msvcrt flavour.** `-static` folds libgfortran/libgcc/libquadmath/
  libwinpthread (and libgomp) in; the MSVCRT (not UCRT) winlibs build imports only
  `msvcrt.dll`, present on every Windows. That is what "runs on any Windows machine" rests
  on.
- **Fixed image base, ASLR off** (`-Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va
  -Wl,--image-base,0x400000`, from the Windows script). `LOCFX` returns addresses as 32-bit
  word offsets; with high-entropy ASLR a 256 MB open core can straddle a 4 GB boundary and
  the arithmetic wraps, about one boot in seventy. `-no-pie` is the ELF counterpart
  (*Porting*).
- **Open core 256,000,000 words, 1 GB** (`-DNASTRAN_OPEN_CORE_WORDS`, passed by both
  scripts; `CMakeLists.txt`'s default is NASA's 14,000,000, so a hand build without it gets
  56 MB). 64,000,000 until 2026-09-25: callers asked for `OCMEM=256000000` against that
  build, which was `MESAGE -61`, `END OF JOB` and exit 0 with nothing solved; an `OCMEM`
  above the total now warns on stderr and takes the default split. A static array; costs
  nothing until used. `nastran95` splits it 2,000,000 modules / rest database (NASA's
  habit); `nastran95ase` gives the modules three quarters because a 120-mode eigensolve
  wants working space and the database sits a sixth full. `OCMEM` / `DBMEM` in the
  environment override either. A model that does not fit gets a `-8 MESAGE` and the
  explainer says so.
- **`NASTRAN BANDIT=-1`** for models over 300 grids (the front end writes it): the
  bandwidth resequencer's scratch array is 1970s-sized and stops the run (SFM 3037) on a
  few thousand grids with rigid elements. The decomposition does not need it at this size.
- **The FEER guards (`mis/ferxtd.f`, `mis/ferxts.f`, `mis/fqrwv.f`, `mis/fqrw.f`,
  `mis/fernpd.f`).** On a semi-definite mass matrix (lumped masses with no rotary inertia,
  the shape of every model the ASE chain writes) the reduction's mass norm of a trial
  vector can come out negative by roundoff; NASA took `DSQRT` of it, and the NaN spun the
  QR iteration for hours. A non-positive norm is now the null vector it is (reseed,
  `UWM 2394` once); the QR sweep is bounded (`UFM 2395`); and the reseed path writes the
  in-core vectors to scratch file 7 first, because NASA's 1994 in-core mode never did and
  the reseed read them from there (`I/O SUBSYSTEM ERROR 110`, which is now counted as the
  fatal it is). Do not "restore" NASA's bare `DSQRT`, and keep the orthogonalisation cap at
  14 passes: 60 bought four rows on the deck that showed all this and nothing else. That
  deck (830 grids, `HALO.md`, "Never hang") gets 89 of 120 modes, which is the algorithm's
  limit on that metric, and the translator says so up front (`UWM 9133`).
- **`-Wl,--wrap=_gfortrani_fc_strdup_notrim` and `msc/mscgfwrap.c`, on every executable that
  links libgfortran.** libgfortran copies a run-time format with `strndup` and then hashes
  the copy for the format's full declared length; NASTRAN's formats live in zero-filled
  `INTEGER` arrays, so the copy is short and the hash reads off the end of a heap block.
  Four NASA fluid demos segfaulted or not depending on where that block landed. The wrapper
  copies the full length. The object goes on each executable's own source list, not into
  `libnas.a`: the reference to `__wrap_` only appears when the linker reaches
  `libgfortran.a`, after it has finished with the library, and the link fails with an
  undefined `__wrap__gfortrani_fc_strdup_notrim` otherwise. Present in GCC 15 and on trunk;
  re-check whether a newer libgfortran still needs it (`format_hash` in `io/format.c`,
  `fc_strdup_notrim` in `runtime/string.c`).
- **The wall-clock watchdog (`msc/mscwatch.c`, both executables).** `TIME` is only checked
  between modules, so a loop inside one is invisible to it. A thread ends the process with
  `_exit(2)` and a message after `N95_TIMEOUT` minutes (0 disables), else the `TIME` card
  (`nastran95`, NASA's default of 5 when absent), else 30 (`nastran95ase`). The tests set
  it to 3 minutes so a regression fails instead of hanging. It has to be `_exit`: the main
  thread may hold a Fortran unit lock. On Linux it sleeps with `nanosleep` on whole seconds
  (c75f958). The `usleep` before it took a 32-bit `useconds_t`, so any limit over 71.6
  minutes wrapped: 300 minutes fired after 820 s, and 14400 after 712 s.
- **The scratch clean-up (bc385f4; `msc/mscwatch.c`, `msc/mscflut.c`, `mds/hmsc.f` HMSCSD,
  one call in `bin/nastrn.f.in`).** The exit handler (HCLEAN) removes a run's `n95_<pid>` on
  every `exit()`, whether the end is normal or a fatal. The other ends:
  - The main program hands the directory it made to `msc_scratch_dir`.
  - The watchdog's `_exit`, and handlers for SIGTERM, SIGINT, SIGHUP, SIGSEGV, SIGBUS,
    SIGFPE and SIGILL, remove that directory's files and then the directory. On Linux this
    is async-signal-safe (raw `getdents64`/`unlinkat`/`rmdir`). The handler that was there
    before (the Fortran runtime's backtrace) runs afterwards. A signal the process started
    out ignoring stays ignored.
  - The SOL 145 driver removes each child's directory after waiting for it. It reads the
    exact path from `s<id>/n95_scratch.txt`, which the child writes to the file
    `N95_SCRATCH_NOTE` names; the note is deleted once read. On Windows, a console handler
    ends the job, waits for the children and removes their directories.
  - It never uses a pattern and never touches a `DIRCTY` from the environment.
    `N95_KEEP_SCRATCH=1` keeps every scratch directory.
  - What it cannot cover: a SIGKILLed top-level run, and a Windows top-level run that ends
    with its files open (Windows does not delete an open file).
  - `HALO.md` has the Linux checks. **Checked on Windows** (9ba11cc, a five-Mach deck
    through the driver, `%TEMP%\n95_*` listed before and after): the watchdog
    (`N95_TIMEOUT=1`) leaves none of its five; `N95_KEEP_SCRATCH=1` keeps all five;
    `taskkill /F` of one child - the driver exits 1 and all five are gone; CTRL_BREAK on
    the run's console - the children end and all five are gone within about 10 s; a run
    killed beside a normal one leaves the normal run's five in place and the normal run
    finishes, then nothing is left; a normal run leaves nothing. CTRL_C did not reach the
    run: a process started under a process group that ignores Ctrl-C (a service or an
    agent's shell, `CREATE_NEW_PROCESS_GROUP`) passes the ignore on to its children, and
    `GenerateConsoleCtrlEvent(CTRL_C_EVENT)` then goes nowhere; CTRL_BREAK is always
    delivered (closing the console window was not tried).
- **FEER with a shift** (the front end writes `EIGR FEER` from `EIGRL`): a zero shift on a
  free-free model whose rigid-body modes are only nearly zero loses every mode; 0.5 Hz (or
  the EIGRL's V1) finds them all. FEER also returns more roots than asked (a reduced
  problem of twice the size); the print-file rewrite and the `.phg` writer cut to the
  number requested.
- **Every card that names a grid is renumbered**, `PARAM,GRDPNT` included. Miss one and the
  run still succeeds with a plausible number: the missed `PARAM,GRDPNT` put the two codes'
  centres of gravity 0.15 m apart on a 1,400-grid model, with the mass and every inertia
  agreeing. `renumber_ids` in `msc/mscxlat.c` has the table; a new card type that holds a
  grid id has to go in it.
- **Ids up to 99,999,999, as MSC and Simcenter take them** (9d96727, 2026-10-05; `HALO.md`,
  "Ids up to 99,999,999"). NASTRAN-95 holds a grid or element id in 24 bits (GP2:
  `UFM 2138, ELEMENT ID ... TOO LARGE` over 16,777,215). How the front end bridges the two:
  - **Collect, sort, assign.** `renumber_pass` gathers every id over the limit, from the
    restart's modes deck too, sorts them, and puts them on a block ending at 16,777,215, in
    their own order. The order is load-bearing: NASTRAN sequences the degrees of freedom by
    grid id, so a scrambled numbering puts the eigenvector rows, the print's POINT ID order
    and the `.phg` rows out of MSC's order, and breaks case-control THRU ranges.
  - **Fatal checks.** The block has to clear every id the deck keeps under the limit,
    CAERO1 boxes included (UFM 9305 when it cannot). An id over 99,999,999 is UFM 9303.
  - **CBUSH springs go below the block** (`scan` leaves the block out of the next free
    element id; UFM 9304 if they would reach it).
  - **Restart.** The card match of a restart reuses the map, so the two decks still compare
    equal.
  - **Restore.** Both restore tables (`mscf06.c` print, `mscexec.c` case control) are hash
    maps of any size. The print restores every field of a sorted-echo line, the weight
    generator's reference point, the singularity table, UWM 2015 and UIM 3113, the static
    tables (DISPLACEMENT, OLOAD, SPC forces), besides the eigenvectors and the XY curves.
  - **Not renumbered:** `CAERO1` box ids and grids in `DMIG` columns.

  What it took, so the shape of the failure is recognisable: until then the front end
  counted down from 16,777,215 in deck order, numbered the springs from the highest element
  id + 1, and restored 1,024 ids. One id over the limit (the aero reference grid) never
  showed any of that. Decks whose point masses were numbered `6CCGNNNN` (some 9,600 ids
  over) stopped with UFM 2138 on the springs at 16,777,216, while Simcenter ran them.

  **The check for any change to the renumbering** is a deck whose ids are all under the
  limit, against a copy with some shifted over it and the grid order kept. They must print
  the same to the bit outside the sorted echo. `.claude/skills/build-nastran95/scripts/
  shift_ids.py` does both halves (`shift` the copy in place, `compare` the prints with the
  shift undone); its docstring has the usage. Two traps:
  - Shift everything between the ids you move and the next one above them: on the monarch
    decks the point masses 6000000.. *and* the bungees 8000000.., under 99999999.
    Otherwise the order changes and you get round-off, not a verdict.
  - Leave CAERO1 box ids alone (the monarch's 7xxxxxx aero block): the front end does not
    renumber them.

  On `monarch_demo_asm1083` (14,368 ids moved), the vibe deck matched on 836,818 lines, and
  the modes run plus the five-Mach flutter restart on 10,855,484 lines.
- **The OUTPUT4 reader's three record traps** (`msc/mscop4.c`), all from reading
  `mis/outpt4.f` and none of them documented anywhere else. (1) The records are fixed-width
  Fortran output - `1X,3I13` then `1X,10E13.6` single precision, `1X,3I16` then
  `1X,8D16.9` double - and must be sliced at the field width, because a negative number
  fills its field to the edge and two adjacent negatives touch, which makes whitespace
  tokenising drift. (2) `JJ` in a column header counts single-precision *words*, so a
  double-precision column announces twice the values it holds. (3) A column with no terms
  comes back with `II` zero and the previous column's words still in the unpack buffer:
  read past it and write nothing, the way MSC Nastran's own OUTPUT4 leaves null columns
  out. Miss (3) and a mass matrix grows one spurious row-1 entry per empty degree of
  freedom - it still factors, still gives modes, and still looks like a mass matrix.
  `N95_KEEP_OP4=1` in the environment keeps the raw file next to the converted one, which
  is how you tell these apart. Null columns also meant the two codes' `.phg` files differed
  in non-zero count (ours 150,166 against MSC Nastran 2025.1's 150,240 on a monarch vibe
  deck, all of it exact zeros in the rigid-body columns); it was the mass matrix whose
  5,777 non-zeros matched exactly.
- **Messages wrap at 72 columns in `emit()`** (`msc/mscmsg.c`) on a space, keeping a word
  wider than the margin whole so a path is never folded, and treat a newline in the format
  string as a hard break. A message whose own hard lines run past 72 will be re-folded in an
  ugly place, so keep each one short.

## Changing the solver or the front end

The source of truth is this repository, `github.com/Halo-One/NASTRAN-95`. NASA's tree is
untouched except where `HALO.md` says; every change to NASA's Fortran is tagged `C HALO:` in
place with its reason, and the C is all new, in `msc/`. The NASA Open Source Agreement 1.3
§3.B requires modifications to be identified, so keep `HALO.md` current.

The fork's additions:

| path | what |
|---|---|
| `CMakeLists.txt` | the build; `NASTRAN_OPEN_CORE_WORDS`, `NASTRAN_BUILD_ID`, `NASTRAN_ASE_MODE` and `NASTRAN_PATH_SEP` substituted into `bin/nastrn.f.in` (configured twice: two main programs); the fenced optimised files; the x86-64-v3 copies; `NASTRAN_LAPACK_LIBRARIES`; the static, `-no-pie` and `--wrap` link options |
| `bin/nastrn.f.in` | the main program template: command line, output directory, scratch under TEMP, core split, the ASE branch (translate, then open the translated deck; `--cosmic` skips translation; SOL 200 hands over to the driver and never returns) |
| `mds/hexit.f`, `mds/hclean.f`, `mds/hoswin.f` / `mds/hosunx.f`, `mds/HSTATE.COM` | exit handler (atexit; the print-file verdict -> exit code 0/2/3; removes empty optional outputs; calls the print-file rewrite and the fatal explainer), tidy-up, the OS calls (one file per OS), shared state |
| `mds/hmsc.f` | the Fortran side of the C calls (`HMSCXL` translate, `HMSCF6` rewrite, `HMSCOP` SOL 200, `HMSCDG` explain, `HMSCSD` the scratch directory), all `BIND(C)` |
| `msc/mscmain.c` | the executable's own path, `realpath`, directories: the C side's OS calls |
| `msc/mscread.c` | reads MSC-dialect decks: free/small/large field, continuations (a line always contributes 8 slots), nested and split-across-lines `INCLUDE` |
| `msc/mscxlat.c`, `msc/mscexec.c` | the translation: SOL map, case control (prefix-matched names, MSC-only commands dropped with the cost named), card handlers (RBAR/RBE2->CRIGD1, CBUSH->CELAS2/CONROD, EIGRL->EIGR FEER, PBARL->PBAR, CQUAD4->CQUAD2, negligible panels dropped), ids over 2^24-1 (up to 99,999,999) renumbered onto an ordered block ending at 2^24-1 (`renumber_pass`; the case control's SET/XY ids follow, `mscexec.c`), auto-SPC, per-card tally |
| `msc/mscwrite.c` | 8-column output; `msc_r8` picks the eight-column spelling closest to the value; numbers that do not fit are re-spelled, never cut (a 9-character `-6.89e+04` cut to 8 reads as -6.89 - this happened); large field when even that loses more than 1e-5 |
| `msc/mscf06.c` | the print-file rewrite into the MSC layout (eigenvector banners, eigenvalue table, weight generator, sorted-echo banner, renumbered ids restored, modes past the count cut, no zero-length lines) |
| `msc/mscop4.c` | `ASSIGN OUTPUT4` + `OUTPUT4` alter -> `ALTER 77` in rigid format 3 (after SDR1), statement 90 of AERO 10 and AERO 11 (after AMP: QHHL, QHJL, QKHL, MHH, KHH, BHH, PHIDH; statement numbers from a DIAG 14 listing) -> FTN11.. units -> MSC-layout formatted OP4, one file per SOL 145 subcase |
| `msc/mscflut.c` | the SOL 145 driver: a child run per subcase, the processors shared out by kind of work, the prints joined; fork/execv and `PR_SET_PDEATHSIG` on POSIX, `_spawnl` and a kill-on-close job on Windows |
| `msc/mscaec.c` | the aerodynamic cache (`N95_AERO_CACHE`) |
| `msc/mscisa.c` | picks the x86-64-v3 kernel copies at run time |
| `msc/mscwatch.c` | the watchdog, the scratch clean-up on signals, the Windows power-throttling opt-out |
| `msc/mscgfwrap.c` | the full-length format copy the `--wrap` routes libgfortran to |
| `msc/mscopt.c`, `msc/mscopt2.c` | SOL 200: design model, responses, finite-difference sensitivities via child `--cosmic` runs, CONLIN dual, move limits, convergence |
| `msc/mscdiag.c` | the solver's fatal numbers -> what happened / what to do, printed on the terminal in both executables |
| `msc/msctrim.c`, `msc/msctrim.h` | SOL 144: the static aeroelastic cards and `TRIM = n` / `DIVERG = n` taken out of the deck and kept in memory (`aet_g`), the correction matrices by their restricted names (DMI / DMIJ / DMIK: `aet_matrix`), `AERO`/`MKAERO1` (the TRIM and DIVERG Machs)/a dummy `EIGR` written for the aero modules, and the `APP DMAP` program the solution runs as (`aet_write_dmap`; with or without SUPORT) |
| `msc/mscaest.c`, `mis/aetrim.f` | the DMAP module `AETRIM`: the Fortran side reads its data blocks a column / record at a time into the C side, which does the trim's algebra (LAPACK when linked; compiled `-O2 -ffp-contract=off`, `N95_LAPACK` from CMake): the intercept (W2GJ, FA2J), WKK, the divergence roots (DGEEV on the splined set), half models, 0-6 SUPORT dofs, and formats the tables the Fortran side prints |
| `msc/mscmsg.c` | numbered messages (this front end's 9000 series) in the solver's own three-part shape, plus the tally |
| `mis/ampczs/` | the in-core AJJ solve: LAPACK's ZGETRF + ZGETRS or the built-in LU, and the OpenBLAS thread-count call |
| `HALO.md`, `SOL144.md`, `doc/` | the record of every change; SOL 144's equations, design and validation; the user's and theoretical manuals |
| `.claude/skills/` | this skill, and `nastran95-performance` (the speed-ups, measuring, comparing prints) |

The packaging of the two executables, `standalone/`:

| path | what |
|---|---|
| `standalone/README.md` | running, the MSC dialect, SOL 144/145/146/200, OUTPUT4, Linux, building, verification |
| `standalone/nastran.bat`, `standalone/nastran` | stand-ins for the licensed solver's launcher: they run `nastran95ase`, so a script that calls `nastran <deck>` runs this build while the folder is ahead of the licensed solver on the PATH |
| `standalone/run_nastran95.bat`, `standalone/run_nastran95.sh` | run a deck through `nastran95` beside them (the `.bat` takes a deck dropped on it) |
| `standalone/build/build_nastran95.ps1`, `.sh` | the builds (*Build*) |
| `standalone/build/work/` | toolchain, OpenBLAS, CMake build trees (gitignored) |
| `standalone/nastran95(.exe)`, `nastran95ase(.exe)`, `BUILD_INFO.txt`, `BUILD_INFO_linux.txt` | installed by the scripts (gitignored) |
| `standalone/examples/cantilever/` | statics and modes decks for `nastran95`, with their prints |
| `standalone/examples/flat_plate/` | one flat plate through SOL 144, 145 and 146, with their prints in `out/` |
| `standalone/test/decks/` | plate_flutter, two_subcase_flutter / two_subcase_modes, trim_small_aircraft, divergence_typical_section, aeroelastic_guide_examples/ (HA144A, HA144B x2, HA145C) |
| `standalone/test/data/` | Simcenter 2606 prints of the guide examples |
| `standalone/test/run_decks.py` | the check that needs no MATLAB |
| `standalone/test/*.m` | the MATLAB tests and `n95_platform.m` |

The loop for a change:

1. Branch from `halo-ase-nastran95ase` (821d508 and on; `halo-ase` is far behind it).
2. Change the source: `C HALO:` and the reason on every change to NASA's Fortran; new C in
   `msc/`; a new optimised file follows the fencing rules above.
3. Build this checkout: `-NoInstall` while iterating (the CMake tree in the work directory
   is incremental: a C change rebuilds in seconds; touching `bin/nastrn.f.in` or the open
   core rebuilds everything, minutes), then a plain build into `standalone/`.
4. Run the checks (*Build*, then *Verifying a build*). Before trusting a number the front
   end produces, run the same deck through the licensed solver (MSC or Simcenter Nastran)
   where one is available and compare through VehicleDesign's readers. When a number
   disagrees, diff the translated deck (`<stem>_n95.dat`) against the original card by
   card, value by value: that found the eight-column truncation bug in minutes.
5. Update `HALO.md` (and `SOL144.md` or the manuals where they describe what changed),
   commit, push.
6. To ship it: *Shipping a build to VehicleDesign*.

By hand, without the script (a debug session, a configure experiment), with the toolchain
the script unpacked (`<winlibs>` is `standalone/build/work/winlibs/mingw64/bin`, `<work>`
the work directory):

```
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_Fortran_COMPILER=<winlibs>/gfortran.exe -DCMAKE_C_COMPILER=<winlibs>/gcc.exe -DCMAKE_MAKE_PROGRAM=<winlibs>/ninja.exe "-DCMAKE_EXE_LINKER_FLAGS=-static -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va -Wl,--image-base,0x400000" -DNASTRAN_OPEN_CORE_WORDS=256000000 -DNASTRAN_LAPACK_LIBRARIES=<work>/OpenBLAS-.../libopenblas.a
cmake --build build-dev --parallel
```

On Linux the same with the machine's compilers and no linker flags (`CMakeLists.txt` adds
`-static` and `-no-pie` itself). `build-dev*/` is gitignored. Without
`NASTRAN_OPEN_CORE_WORDS` the build gets NASA's 56 MB; without `NASTRAN_LAPACK_LIBRARIES`
the in-core solve is the built-in LU, SOL 144 uses its own LU, and DIVERG is a fatal
(UFM 9668: it needs DGEEV). In bash, put `<winlibs>` on PATH as `/c/...`: a `C:/...` entry
splits at the colon.

**Adding a DMAP module** (what `AETRIM` took, 2026-09-30). A module needs three entries in
NASA's tables and nothing else: its **MPL entry** in `mis/xmpldd.f` (`length, name (2
words), 1 (functional), #inputs, #outputs, #scratch`, then the parameters as `type,
default` pairs - 1 integer, 3 BCD with two words - the length counting itself); its **link
table entry** in `mis/xlnkdd.f` (DMAP name, entry name, link bits; `LLINKX`, the `LINK`
and `KLINK` dimensions grow by 5 a module); and its **dispatch** in `mis/xsem00.f`, a
computed GO TO on the module's **position in the MPL** (`MODX`), counting the blank
padding entries (`M, 20, 19*0`) too. So a new entry must not shift the ones after it:
`AETRIM` replaced the 20-word blank entry after DUMMOD4 with a 20-word entry of its own (5
integer and 1 BCD parameter) and took that blank's dispatch label, `2041 CONTINUE` ->
`2041 CALL AETRIM`. Up to 35 files a module (MAXFIL); `AETRIM` has 19 inputs. Purged
inputs show as `RDTRL` returning a negative name. A whole new solution is easiest as an
`APP DMAP` program the front end writes (no rigid format, so no restart tables to keep
consistent); the 1970s DMAP needs a dynamics pool (an `EIGR`) before DPD will make the
tables APD reads. Two traps met: **GI kept state** (its set flags were DATA-initialised
and only cleared, so a second call in one run inherited the first call's; now reset per
call), and **the Fortran files are CRLF** - a Python edit that reads text and writes it
back turns the whole file to LF, and `* -text` commits it so (open the file binary, or
convert back; `file mis/x.f` or `git ls-files --eol` shows it).

Things the Bash tool does to you while editing the source: heredocs over ~8 KB are
truncated, and `\\` inside a heredoc collapses to `\` (a C `"\n"` in a Python patch string
becomes a real newline in the C source). Write patch scripts with the Write tool and run
them; never heredoc C or Fortran.

## Verifying a build

- `nastran95 --version` and `nastran95ase --version` print the build id (commit, compiler,
  date).
- **The Windows build is byte-reproducible except for four bytes.** Two runs of the script
  from the same commit gave executables differing only at file offsets 137-138 and
  217-218, the PE header's link timestamp. A rebuild you did not intend to change anything
  should differ by exactly that and no more; in VehicleDesign, do not re-commit the binaries for
  it - revert the executables and reconcile the build record's hashes with what is
  committed. gfortran puts each source file's full path into the executable, so the tree's
  path is part of the bytes: a build from a longer path came out 105 KB bigger, and a
  `-Commit` build and a checkout build of the same commit differ in size, not in what they
  print. **The Linux build is byte-reproducible outright**: two runs of the script from one
  commit give the same SHA-256.
- `objdump -p standalone\nastran95ase.exe | findstr "DLL Name"` lists KERNEL32.dll and
  msvcrt.dll only.
- **NASA's 132 demos** through the new build and the one before, compared line for line:
  `.claude/skills/nastran95-performance/scripts/run_demos.sh <nastran95> OUT 12` for each
  build's `nastran95`, then `compare_demos.sh OUT_OLD OUT_NEW` (15 s a build). Expect d01002a (it prints the GINO
  timing constants it measures), occasionally d07021a / d07022a (nondeterministic in the
  unmodified solver too), and d10022a's UIM 3028 decomposition statistics, which flip run
  to run on every build (2,3,2 and 2,3,3 over three runs of each of two builds; which is
  why `compare_prints.sh` leaves them out). Anything new the aero path prints moves the
  five aero demos: the sorted (Mach, k) list (8d013f5) added UIM 9457's five lines to
  d10021a-d10023a, d11031a and d11032a.
- Until 2026-09-23 a four-way test in VehicleDesign (NASA's 1995 prints, MSC Nastran
  2025.1, MYSTRAN and this build; NASA's 132 demo verdicts pinned) verified each build.
  The demo comparison above replaces it for the solver, and the checks in *Build* for the
  ASE paths.
- A deck that differs from the licensed solver by more than 1e-4 in a frequency, or a
  `.phg` that VehicleDesign's OUTPUT4 reader will not load, is a bug in the translation,
  not a tolerance to widen.
- Two checks catch what a frequency comparison does not: the grid point weight generator
  against Simcenter's, and the OUTPUT4 matrices read back with `phi' M phi` against the
  identity (VehicleDesign's readers do both).

## Windows

`build_nastran95.ps1` is the Linux build's sequence with these pieces of its own:

- **OpenBLAS 0.3.34 from source with the Linux options**, once, into
  `<work>\OpenBLAS-0.3.34-static-omp-prescott-sandybridge-haswell-skylakex\libopenblas.a`
  (23.5 MB). **With OpenBLAS's own CMake and the toolchain's Ninja, not its Makefiles**:
  the Makefiles need a POSIX shell, and under Git for Windows' `sh` every recipe line is an
  MSYS fork - `c_check` alone forks once per token of `gcc -v` (five minutes before the
  first compile), and 3,700 of the ~8,000 compiles took 50 minutes; CMake + Ninja did all
  7,948 steps in 33 minutes on a Core Ultra 7 165U. Two spellings differ from make:
  CMake's `DYNAMIC_ARCH` puts `PRESCOTT` in front of `DYNAMIC_LIST` itself (naming it again
  creates `kernel_PRESCOTT` twice and the configure stops), and the Release flags are set
  to `-O2` (CMake's own default is `-O3`; the make build is `-O2`). The same kernel set as
  Linux results. The prebuilt `OpenBLAS-0.3.34-x64.zip` does hold a static
  `libopenblas.a`, but a GCC 9.3, pthreads (a Win32 thread server beside libgomp),
  every-kernel one: not used.
- `-DNASTRAN_LAPACK_LIBRARIES=<that lib>`: the path holds "openblas", which is how
  `CMakeLists.txt` picks `ampczt_openblas.f`, the thread-count call a static OpenBLAS needs.
  A LAPACK at a path without that word gets `ampczt_none.f`.
- **PowerShell variables are case-insensitive**: a parameter spelled `$SourceDir` is the
  script's own `$sourceDir`, and the first version of the local-source switch silently
  overwrote itself. Check every new parameter's name against the script's variables.
- In cmd, `%ERRORLEVEL%` on the same line as the command is expanded before the command
  runs: it reports the previous command's code. Read the build's log, not that.
- Windows lets a running executable be renamed but not overwritten: when a run holds the
  installed one open, the script puts it aside as `<name>_in_use_<yyyyMMdd_HHmm>.exe` and
  says so. Delete it when the run ends.
- The build record lists the fenced optimised files, OpenBLAS and the libgomp link.

Windows-only source changes (all `_WIN32` / `if(WIN32)`):

- `msc/mscflut.c` (05a2145): every SOL 145 child goes into one job object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` - the twin of Linux's `PR_SET_PDEATHSIG`: a driver
  stopped by its watchdog, the user or a test timeout takes its children with it (before,
  they ran on and held the executable open).
- `CMakeLists.txt` (05a2145): the x86-64-v3 copies are assembled with
  `-Wa,-muse-unaligned-vector-move`. Win64 promises only a 16-byte aligned stack and gcc
  does not realign it for 32-byte AVX spills (GCC bug 54412), so an aligned `vmovaps` to
  the stack can fault at random.
- `msc/mscflut.c` (dc5e42d): `flut_processors()` counts the bits of
  `GetProcessAffinityMask` (Linux: `sched_getaffinity`), so `start /affinity FFF
  nastran95ase ...` is honoured; libgomp on MinGW sizes its default team from the same
  mask.
- `msc/mscwatch.c` (dc5e42d): a constructor opts each Windows executable out of
  execution-speed power throttling (`SetProcessInformation`, `ProcessPowerThrottling`,
  looked up with `GetProcAddress` so Windows 7 still starts it; `N95_THROTTLE=1` keeps
  Windows' choice). Windows 11 puts minimised and background processes - a solver started
  by MATLAB's `system()` - on EcoQoS unless the power slider is on Best Performance.

**Verified on a Core Ultra 7 165U laptop (2026-09-26)**, the first Windows build of the
one source (dc5e42d), against the Windows executables before it (`halo-ase-sol145` at
63e15d64), on the five-Mach 110-mode PKVECT monarch deck:

| build / switches | wall | print against 63e15d64's | lowest crossings (m/s EAS, Mach 0.10 .. 0.40) |
|---|---|---|---|
| `halo-ase-sol145` 63e15d64 | 1768.6 s | - | 33.105 / 29.795 / 22.169 / 20.043 / 16.172 |
| dc5e42d, `N95_AJJ_SOLVE=builtin N95_DLM_QUARTIC=0` | 673.7 s | **0 of 10,035,296 lines** | the same |
| dc5e42d, `N95_DLM_QUARTIC=0` (OpenBLAS) | 305.8 s | 8 lines (aero loads at 1e-21) | the same |
| dc5e42d, `N95_ISA=0 N95_DLM_QUARTIC=0` | 298.5 s | 0 lines against the line above | the same |
| dc5e42d, default (quartic: the deck's SYSTEM(270)=1) | 383.2 s | kernel changed | 33.082 / 29.738 / 22.245 / 20.078 / 19.765 |

The quartic kernel moves Mach 0.40's lowest crossing from 16.17 m/s at 38.4 Hz (a PK
artefact) to 19.77 m/s at 10.70 Hz, Simcenter's root. NASA's 132 demos through the new
`nastran95.exe` and the one before: 130 identical; d11031a / d11032a differ by the in-core
gust path only (1.15e-4 and 9.4e-6 of each line's largest, Linux 1.4e-4 / 1e-5). The v3
kernels bought nothing measurable on the 165U (298.5 s without, 305.8 s with - within
run-to-run noise).

Toolchain facts that shape Windows performance (verified in the pinned winlibs GCC 16.2):

- libgomp's barrier on MinGW never spins (POSIX `bar.c`: mutex + semaphore, ~15 us a
  barrier), so `OMP_WAIT_POLICY` and `GOMP_SPINCOUNT` do nothing; `OMP_PROC_BIND` /
  `OMP_PLACES` are ignored (no `affinity.c` for mingw). Keep parallel regions coarse.
- TLS is emulated: every `THREADPRIVATE` access (`/DLM/`, `/KDS/` in the parabolic
  kernel's `tker`, `incro`, `flld`, `subb`) is a call to `__emutls_get_address`. The
  quartic kernel (`dlmq.f`) reads no COMMON.
- `__builtin_cpu_supports` works in the static executable; Windows' libm SIN/COS differ
  from glibc's, so bit-for-bit baselines are Windows against Windows.
- ifx needs MSVC's linker and cannot link MinGW objects; PGO and LTO are not worth it here
  (the few optimised files; NASA's aliasing under LTO).

## Porting: what Linux needed, and what macOS would still need

Linux is done (September 2026). The solver itself was always portable - NASA's tree built
on Sun, VAX and PC, and `CMakeLists.txt` selects `mds/hosunx.f` over `mds/hoswin.f` off
`WIN32` - so the port is twelve files, all of them in the front end, the build or NASA's
own text, plus the SOL 145 driver `msc/mscflut.c` (fork/execv, waitpid, the CPU set by
`sched_getaffinity`, `PR_SET_PDEATHSIG` so a child dies with its driver). It is commits
37a6aba and 8c4c94f on `halo-ase-sol145-linux` (VehicleDesign carried it as a patch
until 2026-09-25); `HALO.md`, "Linux, and the flutter run in a minute and a quarter", is
the account. In short:

- `msc/mscmain.c`: `GetModuleFileNameA` -> `readlink("/proc/self/exe")` (and
  `_NSGetExecutablePath` under `__APPLE__`, written but never run); `_fullpath` ->
  `realpath`; `_mkdir`/`_chdir` -> `mkdir(p,0777)`/`chdir`.
- `msc/mscopt2.c`: `_spawnl(_P_WAIT, ...)` -> `fork`/`execv`/`waitpid`. The arguments are
  **not** quoted on POSIX - nothing re-parses them, so a quote becomes part of the file
  name.
- `msc/mscop4.c`: `_putenv` -> `setenv`. Not cosmetic: POSIX `putenv` keeps the caller's
  string and that one is a local buffer.
- `bin/nastrn.f.in`: `BSLASH = CHAR(92)` -> `PSEP = CHAR(@NASTRAN_PATH_SEP@)`, 92 or 47
  from CMake. A wrong separator here is silent - the output directory becomes part of the
  file name.
- `CMakeLists.txt`: that substitution, `-fno-pie`/`-no-pie` on ELF, and `-static` left off
  on Apple only.
- **Six of NASA's files ended in a `0x1A`**, CP/M's end-of-file mark, which a Windows
  text-mode read stops at: `mis/smc2c[ds].f`, `mis/smc2r[ds].f`, `mis/trd1e.f`,
  `mds/PAKBLK.COM`. gfortran on Linux reads it as a source character and says "Non-numeric
  character in statement label", which is true and mystifying.

Load-bearing on ELF, the counterpart of the PE image-base flags: **`-no-pie`**. `LOCFX`
returns `LOC(I)/4` in a default INTEGER, so the 1 GB open core has to sit below 2 GB (the
fixed image base puts it at 4 MB); Linux loads a PIE at `0x55...`, where that arithmetic is
nonsense. The build script checks the top of the finished image (`readelf -lW`, highest
`LOAD` vaddr + memsz) rather than trusting the flag. The honest fix is still to make open
core `ALLOCATABLE` and rework `LOCFX`/`KORSZ`.

What the port cost, measured rather than assumed: every number of the licence-free test of
the time (NASA's demos and the coverage decks) came out the same on both platforms,
**except NASA demo `d03031a`** (a half-filled rigid sphere, AXIF/CFLUID). There FEER returns
30 of NASA's 35 roots and says so itself (`INFORMATION 3307`: four potentially not found at
the low end, where NASA's run said two); the roots it does return are NASA's to every
printed digit, and the first one missing is the second half of a degenerate pair at
8.973466 - a convergence decision made on the last bit, not an arithmetic difference. It is
deterministic across runs and across open core from 2M to 40M words, so it is not the
heap-layout class that used to afflict that family. It was recorded as the one expected
platform difference, with that evidence; everything else must still match on both. Expect
any future port's demo verdicts to move like that or not at all - a deck that completes on
one run and segfaults on the next is not "unstable", it is the libgfortran format-copy
defect, and a new libgfortran may need the same `--wrap`.

macOS is untried. What is already in place for it: `mds/hosunx.f`, the `__APPLE__` branch
of `msc_self_path`, and `-static` left off (Apple ships no static libSystem, so
`-static-libgfortran -static-libgcc` and a libSystem dependency every Mac has). What is
not: nobody has run it, `-no-pie` is spelled `-Wl,-no_pie` there, Apple silicon needs the
whole address question re-examined, and the build scripts know Windows and Linux only.
Other things to expect on any new POSIX system: `HTMPDR` reads `TMPDIR` and the solver's
CHARACTER*72 limits still apply (deck stem under 60 characters, scratch path under 66);
the `rf/` names and `INCLUDE 'DSIOF.COM'` are upper-case and the CMake glob is case-exact.
