---
name: nastran95-performance
description: Find where nastran95ase spends its time on a SOL 145 flutter run and make it faster without moving a number. The measurement scripts (CPU per DMAP module per child on Linux and Windows, wall-clock module sampling, a /proc main-thread sampler, gprof under OpenMP), the bit-for-bit protocol (reference builds of the unmodified commit and of Jon's halo-ase-sol145, cut decks, compare_prints.sh, NASA's 132 demos through two builds), every speed-up with what it bought and the environment switch that turns it off (N95_PK_THREADS, N95_DLM_THREADS, N95_AMG_BATCH, N95_AMP_PIPE, N95_AJJ_SOLVE, N95_BLAS_THREADS, N95_ISA, N95_AERO_CACHE, N95_DLM_QUARTIC, N95_PK_SECANT and the rest), the traps, what was tried and dropped, and what is still serial. Use when a nastran95ase run is slow, before changing solver code or a CMakeLists.txt optimisation fence for speed, when a timing or thread count looks wrong, or when asked whether an optimised build still gives the same answers.
---

# nastran95ase: where the time goes, and making it go faster safely

The solver is 1970s FORTRAN built at `-O0 -fno-automatic` (skill `build-nastran95` says
why, and section 6 below says what happened when the whole tree was tried at -O2 anyway).
Every speed-up here is a small, fenced island of new code or of NASA's code transcribed
with the same arithmetic in the same order, the rest of the tree untouched, checked
**bit for bit**. Read section 3 before writing any: that protocol caught every mistake
listed in section 5.

Scripts are in [scripts/](scripts/); from the repository root they are
`.claude/skills/nastran95-performance/scripts/<name>`. The executable under test is
`standalone/nastran95ase` (`.exe` on Windows), which `standalone/build/` builds from this
checkout (skill `build-nastran95`), or a candidate the build script left in its build tree
(`-NoInstall` / `--no-install`). The speed-ups were written on branch
`halo-ase-sol145-linux` (Jon's `halo-ase-sol145` plus the POSIX port) and
`halo-ase-sol145-perf` (from 2026-09-26 the one source both Windows and Linux build), and
are in this branch whole. Each change's reasoning is in its commit message, its `C HALO:`
comments, its fence in `CMakeLists.txt` (which files are optimised, with which flags) and
`HALO.md` ("Linux, and the flutter run in a minute and a quarter", "The second pass", "The
aerodynamic cache", "The quartic doublet-lattice kernel", "A secant step in the PK
iteration", "What is still serial"). The switches are tabulated in
`doc/nastran95ase_users_manual.md` §3.2, the timings in §7.7, the cache in §7.8; the
verification record is `doc/nastran95ase_theoretical_manual.md` §12.3. This skill is how
those numbers were measured and checked, and how to measure and check the next change.

## 1. The numbers

The benchmark is VehicleDesign's five-Mach monarch flutter deck,
`monarch_demo_asm1083_flutter.dat` (a proprietary aircraft model, not in this repo): five
Machs, 120 modes, 60 or 61 matched points each, 2,282 doublet-lattice boxes, 24 to 48
reduced frequencies per Mach. Its numbers below are the record of what each speed-up
bought. The procedures work on any SOL 145 deck; section 3 says which of the small decks
in `standalone/` exercises which path.

Linux, an AMD Ryzen AI MAX+ 395 workstation (16 cores, 32 threads), the SOL 145 driver
running one child per Mach:

| build | wall | CPU | print |
|---|---|---|---|
| unmodified (fcd681b + the POSIX port: 37a6aba) | 19 min 50 s | - | reference |
| first pass (PK loops and rows threaded, OpenBLAS solve, AMP's file walks, driver) | 74 s | 1,720 s | 108 lines (OpenBLAS, see 4) |
| + EGNVCT's pivot search, AMPK | 56 s | 1,440 s | the same 108 |
| + AMGK (the doublet lattice in batches over k) | 49 s | | |
| + FA1PKG's loops swapped | 47 s | | |
| + the x86-64-v3 kernels | 44 s | | |
| + GINO's run copies, GP4's bisection, the setup's inner loops, HSBG 8 columns at a time | **40.5 s** | 956 s | the same 108 |
| the same, `N95_AJJ_SOLVE=builtin` (Jon's LU) | 179 s | | **Jon's 8cd363e, line for line** |

The Mach 0.10 child alone: 40.6 s -> 17.2 s. The modes-once restart flow (users manual
§7.6; section 8) 38 s; the SOL 146 gust deck reached its SDR2 stop in 27 s (it runs to
the end since d0e9419). The 108 lines move no flutter crossing: with the run's thresholds
both prints give the same 35 crossings (29 when a crossing must hold for two points,
VehicleDesign's `drop_isolated`), and the lowest crossing of each Mach is the one Jon's
Windows build gives (33.3 m/s EAS at 3.73 Hz, 29.9/3.82, 22.3/10.74, 20.1/10.78,
19.8/10.73). Jon's Windows run of the same deck (fcd681b): 25.6 min. On the reduced
one-Mach deck Simcenter 2606 took 31 min on the laptop below; MSC Nastran 6.4 min.

**The 110-mode PKVECT deck (2026-09-25)**: the same aircraft, 110 modes, EAS 1..40 (40-41
matched points), `PARAM PKVECT 1` (a PK vector at every loop, 968 MB and 10 M lines of
print). 1d8307a: 42.9 s direct, 45 s the restart flow (modes 5.8 + flutter 39.2); the
Linux build before it (a82d5d9, which drops PKVECT) 39.7 s and 485 MB. With
`N95_AJJ_SOLVE=builtin` 1d8307a prints Jon's 63e15d6 (built for Linux) line for line;
OpenBLAS differs from it in 48 of 22,447 summary rows plus eigenvector last digits, every
crossing the same. The lowest crossings per Mach, parabolic kernel: 33.326/3.721,
29.900/3.812, 22.331/10.738, 20.116/10.782, 19.754/10.735 (VehicleDesign's regression
test holds them to 0.001).

**The parity build, dc5e42d (2026-09-26)**, the first source both systems built: 1d8307a
plus the aerodynamic cache, the quartic kernel and the Windows-only pieces. With
`N95_DLM_QUARTIC=0` it prints 1d8307a's print of the PKVECT deck line for line (0 of
10,070,885) in 47 s. The deck as written (`NASTRAN SYSTEM(270)=1`) runs the quartic kernel
in 66 s; its lowest crossings are 33.306/3.710, 29.844/3.799, 22.410/10.699, 20.152/10.741
and 19.784/10.699, 0.06-0.35 % from parabolic.

**9ba11cc (the same branch, that afternoon)** moves no bit of a default run. It adds:
- the opt-in PK secant, `N95_PK_SECANT=1`;
- `N95_PK_STATS=1`;
- the Linux watchdog's `nanosleep` (the old `usleep` wrapped past 71.6 minutes);
- a killed run removing its own scratch;
- `K2PP`/`B2PP` kept in case control.

What the secant does:
- QR solves per root: 2.24 to 1.68 and 3.25 to 2.10 on the monarch at Mach 0.10 (EPS 1e-3
  / 1e-4); 1.29 to 1.20 and 1.83 to 1.61 on the five-Mach study deck (PKVECT off), 3-4 %
  of wall.
- Serial and threaded stay bit-identical.
- The lowest crossing per Mach moves by at most 0.001 m/s.
- About a tenth of the root values 0.6-20 Hz land on another branch, mostly heavily
  damped ones, as a change of EPS does with NASA's step. Some crossings above the lowest
  move.

It is an opt-in for the lowest crossing, not a default (`HALO.md`, "A secant step in the
PK iteration").

**On Windows** (a Core Ultra 7 165U laptop, 14 threads, 15 W; quiet; the PKVECT deck):
Jon's exe (63e15d6) 1768.6 s; the parity build with `N95_AJJ_SOLVE=builtin
N95_DLM_QUARTIC=0` prints it line for line (0 of 10,035,296) in 673.7 s; OpenBLAS
parabolic 309.9 s (5.7x, 8 lines at 1e-21, the same crossings); quartic as the deck asks
387.7 s (4.6x). `N95_ISA=0` prints the same and is no slower (298.5 s): the x86-64-v3
copies buy nothing measurable on this CPU. The laptop is ~6x the workstation for either
kernel.

A sensitivity study (3 x 3 structural variants of the deck with PKVECT off, run one at a
time, no cache): 12.8 design points per hour; with the aerodynamic cache 23.2. **The
laptop's best layout** (the same study, warm cache, 83 roots, quiet): **three runs side by
side, `OMP_NUM_THREADS=4` each**, no affinity: 872 s, 37.1 points per hour, against 30.2
with all 110 roots. Two runs x 6 threads 35.0; four x 3 34.0 (per point 415 s, the
E-cores' share); six x 2 failed (three points stopped on GINO I/O errors, most likely the
scratch disk full with 30 children at once; their scratch stayed behind in
`%TEMP%\n95_<pid>`); the three runs pinned to the P-cores and two sets of four E-cores
(affinity masks 0xC03, 0x3C, 0x3C0) 32.2 - the P-core run finishes early and waits while
nothing rebalances; the two LP E-cores left out (0xFFF) 36.8, the same as all 14 within
the noise. Jon's exe: about 2 points per hour.

What differs on Windows, and why (skill `build-nastran95`, *Windows*): libgomp's barrier
never spins (`OMP_WAIT_POLICY` does nothing), `THREADPRIVATE` is emulated TLS, and the
NASTRAN logs' clock columns count the main thread's CPU only - so profile with
`scripts/run_cpu_by_module.ps1`, not the logs.

The Linux times from 2026-09-26 (the cache, the quartic, the roots cap) were taken with
other jobs running and are provisional until re-timed. **Time on a quiet machine**:
nothing else of yours running (`kill -STOP` a long job and `kill -CONT` it after), three
runs, the median. A timing taken beside anything else says so.

## 2. Where a SOL 145 run spends its time

One child (one Mach) runs, in order: the setup (GP1-GP4, MCE1/MCE2 with a DECOMP, READ =
FEER for the modes, GKAD/GKAM, APD, GI the spline: about 6 s serial at -O0), **AMG** (AJJ
per reduced frequency k: the doublet-lattice kernel for every box pair), **AMP** (per k:
DJH, the solve QJH = (AJJ^T)^-1 DJH, QKH = SKJ QJH, QHH = GKI^T QKH), **FA1** (the PK
loops: per matched point, per root, per iteration a 240-square Hessenberg QR, HSBG +
ATEIG), then FA2 and, for loops marked with a negative velocity, the physical eigenvector
recovery (OFP formatting a 1.7-million-line print took 23 s). Theoretical manual §5-§7
has what each module computes.

On the monarch now (40.5 s): of 956 CPU s, FA1 about 650 (nearly all NASA's QR), AMP 170
(the LUs), AMG 100. The wall clock is the Mach 0.10 child: setup 6, AMG 4, AMP 11-12 (3.4
s alone: its GINO read chain is serial and shares the machine with the other children's
FA1), FA1 17.

## 3. Measuring, and proving nothing moved

**Tools that tell the truth here** (`S=.claude/skills/nastran95-performance/scripts`; the
`.sh` run under bash, the Linux ones need `/proc`)

- `bash $S/run_cpu_by_module.sh EXE DECK OUTDIR [SECONDS]` (Linux) - the whole run through
  the driver; every 0.5 s each child's whole-process CPU and the DMAP module its log is in;
  prints wall and CPU per child per module and the CPU per module over all children. Start
  here: it says whether the run is bound by one child's path or by total CPU.
  `run_cpu_by_module.ps1 -Exe EXE -Deck DECK -Out DIR [-Env @{...}] [-Period 0.5]` is the
  Windows twin.
- `N95_PK_STATS=1` - one line per flutter loop on stderr: its roots, its QR solves, and how
  many QR solves each root took (0..11+). It moves no bits. FA1PKR writes it, which only
  the parallel PK path calls, so it also says whether a deck took that path.
- `bash $S/run_timed.sh EXE DECK OUTDIR [SECONDS]` and `bash $S/modules_summary.sh OUTDIR
  [MODULE ...]` - wall clock and `/usr/bin/time -v`, the module each log is in every 2 s,
  and the second each module was first seen per child. `run_timed.ps1` is the Windows wall
  and CPU of the whole process tree (a job object).
- `bash $S/run_sampled.sh EXE DECK OUTDIR [SECONDS]` - one child's deck (`--cosmic`,
  `N95_CHILD=1`), its main thread sampled from `/proc`. Main-thread user+system ~ wall
  means serial work on the critical path. A child's deck is
  `<out>/s<subcase>/<stem>_s<subcase>.dat` after one full run.
- gprof: configure a tree as the build script does (skill `build-nastran95`) plus
  `-DCMAKE_Fortran_FLAGS="-pg -g" -DCMAKE_C_FLAGS="-pg -g"`, run in the output directory,
  `gprof -b -p` (routines) or `gprof -b -l` (lines: this is how GP4's quadratic search and
  the setup's inner loops were found). It does sample the OpenMP threads; it credits
  outlined parallel regions to the preceding global symbol (check `nm -n`), and misses
  kernel time. `perf` works once `kernel.perf_event_paranoid` is lowered.
- `strace -c -f EXE ...` works (tracing your own child is allowed; attaching is not): it
  showed GINO's I/O is ~20,000 buffered reads for a gigabyte - not a cost.
- Micro-benchmarks: a 20-line Fortran driver around one routine, the original and the new
  one side by side, comparing output bits (`transfer(x, 1, n)`) and time. The QR, EGNVCT,
  TKERV, FA1PKG and ZGETRF (bits at 1-32 threads) were all settled this way before
  touching a deck.

Not `<stem>.log`'s own columns: its CPU column is the process's system time
(`mds/cputim.f`, as NASA shipped it), its elapsed one is no use, and on Windows it counts
the main thread only.

**The bit-for-bit protocol**

1. Keep two reference builds and never delete them. The build script builds a pinned
   commit into a directory of its own: `--commit <full sha> --branch <name> --install-dir
   <dir>`, or `--source <tree>` for a local tree (`-Commit`, `-Branch`, `-InstallDir`,
   `-Source` on Windows; skill `build-nastran95`). If it no longer configures a tree that
   old, configure by hand: Debug, `-DNASTRAN_OPEN_CORE_WORDS=256000000`.
   - **The unmodified solver**: 37a6aba (`37a6abac64a4a0cc0ae3b9bb075a1ecfa838d009`), which
     is fcd681b, the flutter source the speed-ups started from, plus the POSIX port and
     nothing else.
   - **Jon's `halo-ase-sol145` with only the POSIX port** (8cd363e and 63e15d6 in this
     record). "Jon's Windows branch as the reference" means this: on Linux it printed a
     committed Windows modal example line for line and gave his Windows flutter
     crossings. On Windows his commits build as they are (63e15d6 is
     `63e15d64a44e9ee8a37c598fc47de5ea65368c4e`, branch `halo-ase-sol145`). On Linux: a
     worktree at his commit; in it, the port's files as the Linux branch's merge of that
     commit has them (76d9d1a merged 8cd363e, 00c22f1 merged 63e15d6), `git checkout
     <merge> -- msc/*.c msc/msc.h bin/nastrn.f.in mds/PAKBLK.COM mds/hexit.f mis/smc2c?.f
     mis/smc2r?.f mis/trd1e.f`, then `git diff fcd681b 37a6aba -- CMakeLists.txt | git
     apply`; check `git diff --stat <sha>` lists only those files, and build it with
     `--source`. For a newer commit of his, branch the last reference (`git worktree add
     -b ref-linux-<sha> ../ref-<sha> <last reference branch>`), merge
     `origin/halo-ase-sol145` into it, and check the same.
2. Cut decks from a child's deck: `python $S/cut_deck.py IN OUT --points 3` (a minute on
   the monarch) or `--points 12 --mark 4,9` (PK eigenvectors, scratch 301/302, the
   physical vector recovery: 1.7 M lines there). Run one as a child is run
   (`N95_CHILD=1 nastran95ase --cosmic OUT.dat <outdir>`; `run_sampled.sh` does).
3. `bash $S/compare_prints.sh A.out B.out [N]` - 0 differing lines is the bar. It leaves
   out only what differs between two runs of one executable: the clock lines, the
   page-header date, UIM 3028's B/BBAR/C/CBAR/R, the RESTART line's time of day, a fatal
   dump's addresses and scratch-directory process id; `grep -a`, because a few NASA prints
   hold a stray binary byte. `python $S/numdiff.py A B` for a change that is not
   bit-identical: the largest difference against each line's largest number, the lines
   aligned with their numbers masked.
4. **NASA's 132 demos through two builds**: `bash $S/run_demos.sh EXE OUT [JOBS]
   [PATTERN]` (15 s on 12 jobs on the workstation) and `bash $S/compare_demos.sh OUT_A
   OUT_B`. The decks are this checkout's `inp/<name>.inp` that have a 1995 print in
   `demoout/` (`NASTRAN_FORK=<checkout>` takes another's); the test is the two builds
   against each other, not against NASA's 1995 prints. Expect d01002a to differ (it prints
   the GINO timing constants it measures), d07021a and d07022a sometimes (section 5), and
   against a build of Jon's source without the in-core gust solve d11031a/d11032a (1.4e-4,
   1e-5 of each line's largest). Anything new the aero path prints moves the five aero
   demos (UIM 9457's (Mach, k) list since 8d013f5). `run_demos.sh` wants the COSMIC
   executable, `standalone/nastran95` or a CMake tree's `bin/nastran`: not `nastran95ase`,
   which reads a deck in MSC's dialect (UFM 9111), and not the repository's `bin/nastran`,
   NASA's 1995 csh launcher. Every GINO, setup or kernel change goes through this.
5. Then a real deck against the reference - with `N95_AJJ_SOLVE=builtin` it must be Jon's
   print exactly (170,034 lines on the 120-mode monarch deck, 10,070,894 on the 110-mode
   PKVECT one; `cmp -l` then counts the raw bytes that differ - 29 there, all clocks). For
   a change to what AMG or AMP write, compare the matrices too: a throwaway dump of each
   pair's AJJ as AMPK reads it (debugging code, not kept in the tree) found AMGK
   byte-identical (1 GB) where a print could have hidden a last bit.

**What the small decks exercise.** The parallel PK path needs a basis of four modes or
more (`NROW.LT.4` in FA1PKE keeps the serial solve, as do DIAG 39 and `N95_PK_THREADS=1`),
and the driver needs several subcases.
- `standalone/examples/flat_plate/flat_plate_sol145.dat` - a 45-grid cantilever plate, 32
  boxes, PK on matched points, seconds. One subcase and `LMODES 3`: no driver and the
  serial PK solve, but AMGK, AMPK, the in-core solve, the cache (`N95_AERO_CACHE`) and
  the quartic kernel (`N95_DLM_QUARTIC=1`).
- `standalone/test/decks/two_subcase_flutter.dat` and `plate_flutter.dat` - beams of six
  modes with two subcases: the driver, the parallel PK path, `PARAM PKVECT 1`; the first
  has marked loops, the second 14 points per Mach and a k list split over two MKAERO1
  cards. Seconds each.
- `python standalone/test/run_decks.py --exe <candidate>` runs every standalone deck and
  checks what it prints (no MATLAB, about a minute);
  `standalone/test/test_nastran95ase_flutter_subcases.m` (needs VehicleDesign's readers)
  checks the two-subcase deck's threaded solve against the serial one line for line.

A small deck checks a change's bits, not its speed, and in the record only the monarch
deck tested the parallel PK path at scale. Compare a print against a reference build's
print made on the same machine: the OpenBLAS LU's last bits differ between machines (on
the PKVECT deck it left 48 summary rows against the built-in LU on the workstation, 8
lines on the laptop).

**Check it yourself on Windows** (no install: the exe is static). From the deck's
directory (its INCLUDEs are relative to it), in cmd, one variable per line (`set X=1 & rem
...` keeps the spaces in X):

    set EXE=C:\path\to\NASTRAN-95\standalone\nastran95ase.exe
    set REF=C:\temp\ref-63e15d6\nastran95ase.exe
    %EXE% --version
    %REF% deck.dat C:\temp\ref
    rem your own print from the new exe: Jon's LU and NASA's parabolic kernel
    set N95_AJJ_SOLVE=builtin
    set N95_DLM_QUARTIC=0
    %EXE% deck.dat C:\temp\new_as_ref
    bash C:/path/to/NASTRAN-95/.claude/skills/nastran95-performance/scripts/compare_prints.sh C:/temp/ref/deck.out C:/temp/new_as_ref/deck.out

`0 differing lines (...)` is the answer: on the PKVECT deck the parity build (dc5e42d)
gave 0 of 10,035,296 against Jon's exe. Later commits print more on purpose, and no switch
takes that back: UIM 9457's list of the (Mach, k) pairs (8d013f5), the AEROF load pages
(a262158, 09c8681), on a deck with ids above 2^24-1 the ids the print now restores
(9d96727), and whatever else a deck uses that a later fix changed (`HALO.md` dates each).
Against Jon's exe expect those lines; against the build just before your change, none.

Without the two lines the OpenBLAS LU leaves 8 lines at 1e-21 (aerodynamic loads) and the
same crossings; without only `N95_DLM_QUARTIC=0` a deck that says `NASTRAN
SYSTEM(270)=1` gets the quartic kernel it asks for (on the monarch, crossings 0.06-0.35 %
closer to Simcenter's). The switches, each on its own line: `N95_JOBS=1` (the Mach
children one at a time), `OMP_NUM_THREADS=1`, `N95_PK_THREADS=1`, `N95_DLM_THREADS=1`,
`N95_AMG_BATCH=0`, `N95_AMP_PIPE=0`, `N95_AJJ_SOLVE=builtin`, `N95_BLAS_THREADS=1`,
`N95_ISA=0` - all of them together is the serial path with Jon's own features (his
in-core AJJ path and his LU). Not a speed-up switch: `N95_INCORE_AJJ=0` turns off Jon's
in-core AJJ path too and runs NASA's out-of-core one, slower than Jon's exe (a first
Windows attempt with it was still on its first Mach child after 80 min). Wall and CPU of
the whole process tree (a job object; `timing.txt`), through `-Command` because `-File`
passes a hashtable as a string:

    powershell -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\path\to\NASTRAN-95\.claude\skills\nastran95-performance\scripts\run_timed.ps1' -Exe '%EXE%' -Deck deck.dat -Out C:\temp\t -Env @{N95_AJJ_SOLVE='builtin'}"

The Windows ablation (the laptop, quiet; the PKVECT deck, parabolic forced):

| row | wall (s) | CPU (s) | against Jon's exe | print against Jon's |
|---|---|---|---|---|
| Jon's exe 63e15d6 (his -O0 build) | 1768.6 | not measured | 1.00x | - |
| children side by side, 1 thread each, Jon's LU, everything else off | 1106.6 | 4288.2 | 1.60x | 0 lines |
| + the PK loops threaded | 919.6 | 4848.4 | 1.92x | 0 lines |
| + DLM rows threaded, AMG batched, AMP in core, OpenBLAS LU (`N95_ISA=0`) | 281.9 | 1957.2 | 6.27x | 8 lines, 1e-21 |
| + the x86-64-v3 kernels: every default | 283.9 | 2002.9 | 6.23x | 8 lines |
| aerodynamic cache, filling | 294.7 | 2043.8 | 6.00x | 8 lines |
| aerodynamic cache, hit | 226.8 | 1130.6 | 7.80x | 8 lines |
| the quartic kernel, as the deck asks | 370.2 | 2707.7 | 4.78x | another kernel |

With Jon's LU and every other speed-up on (`N95_AJJ_SOLVE=builtin` alone): 673.7 s, Jon's
print line for line. The 1.60x row is the optimised files and the always-on,
bit-identical changes (section 4: EGNVCT, HSBG, the PK QR twin, GINO, GP4); the 3.3x step
is the four switches together (split on Linux in section 1's table).

gfortran at -O2/-O3 gives the -O0 bits as long as there is no FMA contraction
(`-ffp-contract=off` on every optimised file), no reassociation (never `-ffast-math`), and
no libmvec (section 5). Summation order is part of the arithmetic: a transcription that
sums in another order is a different answer, and a loop interchange that keeps each sum's
order (FA1PKG) is not.

## 4. What was done, and the switch that undoes each

| change | where | switch |
|---|---|---|
| PK loops of one Mach side by side, written back in serial order | `mis/fa1pkp.f`, `mis/fa1pke.f` | `N95_PK_THREADS=1` |
| The PK QR (FA1PKA+HSBG+ATEIG) without aliasing, -O3 | `mis/fa1pkq.f` | DIAG 39 |
| FA1PKG (GMMATS) with J innermost - each sum in its order, contiguous; HSBG's loop 300 eight columns at a time (eight independent sums) | `mis/fa1pkq.f` | - |
| EGNVCT's pivot search: an element whose squared modulus is below `(X1*(1-2**-20))**2` cannot beat X1, so its CABS is skipped; 7.4x | `mis/egnvct.f` | - |
| Doublet-lattice rows side by side | `mis/gendp.f`, `/DLM/`,`/KDS/` THREADPRIVATE | `N95_DLM_THREADS=1` (also turns AMGK off) |
| AMG's pairs of one Mach in batches: SNPDF, the geometry, TKER's branches, sqrt and exp, IDF1/IDF2's log and atan once; every k-dependent statement a lane loop | `mis/amgk.f`, `mis/tkerv.f` | `N95_AMG_BATCH=0` (=n: batch size, default 8, at most 16) |
| AMP's pair loop in core: read / compute / write OpenMP tasks, GINO calls chained on one dependence in pair order, up to 16 pairs in core | `mis/ampk.f` | `N95_AMP_PIPE=0`; `N95_AMP_SLOTS`, `N95_AMP_MB` |
| In-core AJJ solve through LAPACK (OpenBLAS static, DYNAMIC_ARCH) | `mis/ampczs/` | `N95_AJJ_SOLVE=builtin` (Jon's LU); `N95_INCORE_AJJ=0` NASA's out-of-core path |
| OpenBLAS's thread count set explicitly (not inside AMPK's tasks) | `mis/ampczs/ampczt_openblas.f` | `N95_BLAS_THREADS` |
| AJJL and SKJ no longer rewound and walked k-squared | `mis/ampc.f`, `mis/ampd.f` | - |
| The gust path (AMPF) in core | `mis/ampf.f`, `AMPCZN` | `N95_INCORE_AJJ=0` |
| MMA104's inner loops at -O2 | `mis/mma10k.f` | - |
| The PK and kernel files built twice, baseline and `-march=x86-64-v3` under V3 names (CMake makes the copies), chosen by `__builtin_cpu_supports` | `msc/mscisa.c`, `CMakeLists.txt` | `N95_ISA=0` |
| GINO PACK/UNPACK copy a run of unconverted, contiguous elements in one call | `mds/pack.f`, `mds/unpack.f`, `mds/n95fast.f` | - |
| A secant step in the PK iteration (9ba11cc; opt-in because it moves bits: about a tenth of the roots on another branch, the lowest crossing within 0.001 m/s) | `mis/fa1pkp.f`, `mis/fa1pke.f` | off unless `N95_PK_SECANT=1` |
| GP4 looks each MPC term's SIL up by bisection (the list is sorted, repeat-free) | `mis/gp4.f` | - (unsorted keeps NASA's search) |
| FERXTD's reorthogonalisation, DECOMP's and MMA112's inner products as -O2 copies | `mis/n95twin.f` | - |
| The driver: every processor per child for coarse work, passive waits; each child's AMP memory = half the available shared among the children, 512 MB - 2 GB | `msc/mscflut.c` | set `OMP_NUM_THREADS` / `N95_AMP_MB` yourself; `N95_JOBS` children at a time |
| **The aerodynamic cache** (fa6e903). AMG keeps each (Mach, k) pair's AJJ and AMP keeps its LU factors, under a 64-bit content hash of every input word (the kernel, the boxes, Mach, k; the LU keyed by a hash of AJJ itself); written to a temporary file and renamed, so runs side by side share it. A structural study pays the doublet lattice once per aerodynamic model | `msc/mscaec.c`, `mis/amgk.f` (DLAMGK), `mis/ampk.f` (AMPKC), `mis/ampczs/` (AMPCZF/AMPCZQ) | off unless `N95_AERO_CACHE=<dir>` |
| **The quartic doublet-lattice kernel** (a84d2e7). `NASTRAN SYSTEM(270)=1` / `QUARTICDLM=1`, as Simcenter reads it: the quartic numerator fit and Desmarais' 12-term kernel, double precision, batched over k. It matches Simcenter's quartic to 5-6 digits on the HALE and Goland wings | `mis/dlmq.f`, `mis/incro.f`, `mis/amgk.f`, `msc/mscread.c` (UIM 9470) | `N95_DLM_QUARTIC=0` forces NASA's parabolic kernel; the cache key carries the kernel |
| **The roots found** (deck level). FA1 solves NVALUE roots. They are numbered by mode, not sorted by frequency (on the monarch 3 x 3 study root 79 comes down to 24.7 Hz, and root 69, 41 Hz at 1 m/s, to 13.7 Hz), and a capped print numbers them differently, but the roots it finds in 0.6-20 Hz are the same bits: the study with 83 of 110 against all 110, 9 points x 5 Machs x every speed, 1836 of 1836 rows identical (Windows, 2026-09-26). Rule: NVALUE = the highest root that reaches 30 Hz (25 Hz x 1.2 for the design spread) anywhere in the baseline point's all-roots run - 83 on the monarch, where the whole grid needs 79. Crossings the tracker follows through the renumbered roots above 20 Hz can move: 5 of 238 in 0.6-20 Hz, all above 50 m/s EAS. 60 roots would drop roots that enter the band | the FLUTTER card | leave NVALUE = the modes |

The OpenBLAS solve is the one change that is not bit-identical to NASA's: a blocked LU
rounds differently in double precision, and where that flips a single-precision bit of
QJH the PK iteration on a marginal root lands elsewhere - 108 of 170,034 print lines on
the monarch, every one such a root. Judge a change like that by the flutter crossings, not
the diff count, and say so in the PR.

## 5. Traps (each one cost an afternoon, or would have)

- **gfortran vectorises SIN, COS, EXP into glibc's libmvec** (`_ZGVdN8v_sinf`) in any loop
  it vectorises, even without `-ffast-math`: on glibc it pre-includes
  `math-vector-fortran.h`, and neither `-fpre-include=` (it adds, it does not replace) nor
  `-fno-builtin` stops it. libmvec rounds differently: TKERV differed on 194,796 of
  200,000 geometries. Put `!GCC$ NOVECTOR` on every loop in an optimised file that calls
  a transcendental, and check the object: `nm x.o | grep _ZGV` must be empty. (`amgb1b.f`
  and `amgb1c.f`, compressor-blade theory, in the -O2 kernel list since `halo-ase`, do
  call libmvec.)
- **ATEIG reads outside its matrix.** Its search for a small subdiagonal element steps one
  past (2,1) to `A(1-IA)`; whether that double is below EPS decides where the QR sweep
  starts. Lay a thread's arrays out as open core is (`BW` in `fa1pkp.f`). **Any routine
  you move off open core may read outside its arrays.**
- **An argument that lives in open core can be overwritten under you.** AMG passed AMGK
  its box count as `IZ(3)`; DLAMGK's read of the ACPT record lands on IZ, and the second
  batch saw 14 boxes (SFM 3007). Copy such arguments at entry.
- **Dummy-argument aliasing.** NASTRAN passes one open-core array as two or three dummies
  of different types; -O1 and up assume they do not overlap. Do not recompile such a
  routine; write a twin, or call an optimised copy of just its inner loop with a run-time
  check that the sum is none of its own terms (`n95twin.f`).
- **`-fno-automatic` and threads**: a routine called from a thread must be `-fautomatic`,
  and every COMMON it writes THREADPRIVATE in every unit that names it.
- **OpenBLAS in a static executable runs on one thread** until told
  (`openblas_set_num_threads`); inside an OpenMP parallel region it runs on the calling
  thread whatever it was told - which AMPK relies on - and `openblas_set_num_threads`
  itself must not be called from concurrent tasks.
- **Building OpenBLAS: one make target at a time** (`make libs && make netlib`).
- **Oversubscribed OpenMP must wait passively** (`OMP_WAIT_POLICY=PASSIVE`: 87 s against
  400 s).
- **k-squared GINO walks**: `GOPEN(...,0)` + `FWDREC`/`SKPREC` to a column inside a per-k
  loop.
- **Fixed form stops at column 72**, silently for statements, loudly (an "expected right
  parenthesis") when it cuts a nested expression: `awk 'length($0) > 72'`. Count the
  closing parentheses of a reflowed nest by hand.
- **Line endings and padding.** Some fork files are CRLF, some LF (`git ls-files --eol`);
  NASA's GINO files pad every line to column 80 with spaces. Patch line by line with the
  padding ignored and keep each untouched line as it was, or the diff is the whole file (a
  patch helper that matches on `line.rstrip()` and keeps the old line wherever the new
  text repeats it).
- **The unmodified solver is not deterministic everywhere**: UIM 3028's decomposition
  statistics vary run to run, and NASA demo d07022a takes 10 complex decompositions
  instead of 13 in 1-3 runs of 40 - in the speed-up build, in Jon's 8cd363e and in fcd681b
  alike; d07021a does the same (76 lines between two builds of Jon's own 8cd363e and
  63e15d6). Run a suspect deck 40 times on each build before blaming a change.
- **Merging Jon's commits into a speed-up branch**: FA1PKV is split here (FA1PKV
  computes, FA1PKW writes) for the parallel PK solve, so anything he changes in FA1PKV's
  output belongs in FA1PKW, and any new condition on which loops get vectors (his PRTV for
  PKVECT) has to reach FA1PKL/FA1PKR in `mis/fa1pkp.f` too - the serial path alone would
  still pass a small deck. Keep the merge's verification: the new reference build, the
  large deck with the built-in LU, the demos.
- **A chained `&&` that fails halfway leaves the later edits undone**: after a patch step
  fails, check `git status` for every file you meant to touch.
- **pgrep -f matches its caller**; wait on a PID.
- **NASA's parabolic kernel puts one arctangent in the wrong quadrant.** IDF1/IDF2 take
  `ATAN(2e|zeta|/(r^2-e^2))` without Rodden's 1972 fix, so a receiving point off the
  sending box's plane and within r < e of its centre is off by order one. A tail box
  behind and just above or below a wing box is that case. It is left as NASA wrote it, so
  the parabolic path stays NASA's to the bit; the quartic path has the quadrant right. On
  the monarch the two kernels differ by 0.06-0.35 % in the crossings, so the defect is
  small there.
- **Runs side by side need scratch on disk.** Each run's GINO scratch goes to `$TMPDIR`
  (`HTMPDR`), else `/tmp`, at 1-4 GB a run. On the Linux workstation `/tmp` was a 62 GB
  tmpfs, i.e. RAM: when the 3 x 3 study ran 2-8 at a time there it filled up, and the
  runs stopped with exit 3 and `I/O SUBSYSTEM ERROR NUMBER 102 ... INCORRECT BLOCK NUMBER
  ... AJJL`, the lowest crossings NaN (2026-09-26). For a study, set `TMPDIR` to a folder
  on NVMe. **Windows too**: the scratch goes to `%TEMP%\n95_<pid>` on C:. The laptop's
  study six runs at a time (30 children) stopped three points on GINO I/O errors
  (`DSREAD` status 5002 on QKHL) with 85-105 GB free when idle. Budget the scratch before
  going wide: runs x 5 children (one per Mach) x 2-4 GB a child, plus the aerodynamic
  cache (21 GB). **Since bc385f4 killed runs clean up after themselves**: a run stopped by
  its watchdog or a signal removes its own `n95_<pid>`, and the SOL 145 driver removes a
  child's after the child ends, however it ended. Leftovers now come only from a
  SIGKILLed or crashed top-level run; delete those by name, and only after checking that
  the process is gone. `N95_KEEP_SCRATCH=1` keeps scratch for debugging (users manual
  §2.3; skill `build-nastran95`).
- **The aerodynamic cache is big**: 42 MB of AJJ plus 83 MB of LU per pair, 21 GB for the
  five-Mach monarch deck (178 pairs). Put it on local disk, not in the repo. On a 31 GB
  laptop, cache fewer Machs.
- **Quartic kernel cross-checks**: TKERQ was checked against PanelAero's kernel (DLR,
  BSD-3) at 3,000 random geometries (1.6e-7, 9.5e-6), and the closed forms against mpmath
  quadrature. PanelAero's coplanar nonplanar term (`2e/(eta^2-e^2)`) is wrong; the series
  alpha the quadrature bears out is used instead.

## 6. Tried, and why not

- **The whole tree at -O2** with `-fno-aggressive-loop-optimizations -fno-strict-aliasing
  -fwrapv -ffp-contract=off`: 124 of 132 demos print differently, 51 segfault. gfortran
  treats dummy arguments as not overlapping at -O1 and above and there is no flag to stop
  it; open core is overlapping dummies.
- **ATEIG blocked** (a block of QR steps' far row and column operations held back and
  applied column by column): the same bits, and 3.55-3.9 ms against 3.42. The 240-square
  matrix is L2-resident; the QR is bound by its dependences.
- **A bigger GINO buffer**: `NASTRAN BUFFSIZE=` fails (the buffers are fixed at the first
  open); SYSBUF changed at start-up moves FEER's modes (it changes how much open core FEER
  works in) and saved nothing.
- **Thread priorities** (FA1 threads niced, or the shorter Machs niced 1-16): the long
  child reaches FA1 sooner, the run ends no sooner. What is left is CPU, and a niced
  thread still takes its core's other half.
- **More AMP slots** (32): no faster; AMP under load is bound by its serial read chain.
- **x86-64-v4 (AVX-512)**: slower on the QR than v3.
- **The PK QR through LAPACK** (DGEHRD + DHSEQR, eigenvalues only, OpenBLAS): 290 real
  monarch PK matrices (n = 220) were dumped from a run and replayed in a harness. NASA's
  -O3/v3 twin of HSBG + ATEIG takes 3.95 ms a solve, LAPACK 6.55 ms (multishift and
  aggressive early deflation do not pay at this size), and 7 % of the eigenvalues
  differed in the last single-precision bit. Dropped.

## 7. SOL 146

The monarch gust decks run in about a minute on Linux. Until d0e9419 the PSD decks stopped
in data recovery (SDR2's SFM 3001 on the ELFORCE request, RAND2's SFM 3002 without it;
`HALO.md`, "SOL 146 random response"). The in-core gust solve agrees with the out-of-core
one on NASA's d11031a to 1.4e-4 and d11032a to 1e-5 of the largest value on each line.
AMPK runs before AMPF and leaves the files as AMP's loop does (AMPF rewinds); the gust deck
prints the same with and without AMPK.

**The PSD decks were jagged above 1.5 Hz until 09c8681, and not from a speed-up.**
- The fully serial path (every switch off), `N95_AMP_PIPE=0` and the parabolic kernel gave
  the same digits.
- MINTRP's spline in k (LSPLIN, cubic and even in k) is solved in single precision and is
  ill-conditioned for the monarch's k list (0.005, 0.01, 0.02 ...).
  - Debug prints of its weights: order 1-20, oscillating, 0.988 at a tabulated k.
  - Of the interpolated gust matrix at a tabulated k: rows off by up to 70 %.
- `LSPLND` solves it in double precision for ADRI and FRD2I.
- How it was found, for the next one:
  - First, compare the transfer function frequency by frequency against Simcenter (smooth
    against jagged).
  - Next, take the GUST out, so a 1 N DAREA alone isolates QHH, which was clean.
  - Then give MKAERO every analysis k exactly (the jaggedness stays, so the interpolation
    is not reproducing its own nodes).
  - Finally, print one element of QHJ at a node before and after MINTRP.
- NASA's AERO 11 demos move below 1.4e-4 of a line's largest number. The five-Mach flutter
  print moves only in its aero-load pages (ADR interpolates through ADRI too).

**The 1-cos and turbulence decks run to the end. Before a262158 their gust loads were
zero.** NASA's `UNPACK` ended its first-to-last mode (row range left to the column) at row
65,536, and MPYAD method 10 (`MMA1`) reads every column of "A" that way. `ADRI`'s
interpolation of QHJL, one column per k of boxes x modes rows (130,074 on the monarch),
came out zero.
- How it was found, for the next one like it: doubling `WG` moved nothing. Then norms of
  each matrix file down the chain, printed from a debug build, located the step that loses
  the value: AMPF's RJH was fine, MINTRP's MPYAD product was 1e-8 of its input, and MMA's
  in-core "A" column ended at row 2,282 of 130,074.
- Neither speed-up commit on that path (eff7659 MMA104, 526a453 PACK/UNPACK) was the
  cause: the limit is in NASA's 1995 source and in Jon's build.
- The fix (`LARGE` = 2**30) moves no bit of a run without such a column: NASA's demos
  print as before, and so does the five-Mach deck outside its AEROF aero-load pages
  (6,764,231 lines). Those pages, `ADR`'s interpolation of QKHL, had printed about 1e-19
  and now print the loads.
- The 1-cos, PSD and turbulence decks against Simcenter: `HALO.md`, "Tall columns" and
  "SOL 146 random response"; theoretical manual §12.4.

## 8. Checkpoint and restart (Jon's)

The modes-once-then-restart flow (users manual §7.6) runs on Linux; its summaries differ
from the direct run's in the sixth figure, in Jon's build as in the speed-up build - the
restart, not the speed-ups (with `N95_AJJ_SOLVE=builtin` the speed-up build's restart flow
prints Jon's 8cd363e's line for line). Those digits move which matched points' PK
solutions glitch (one point jumps above g_cross, the next is back below), and on Windows
other points glitch again. Counting every sign change, the Linux restart reported 4.6 /
7.9 m/s at Mach 0.30 / 0.40 and a Windows run of 8cd363e's exe on the same decks 23.7 m/s
at 1.38 Hz at Mach 0.10, against 33.3 / 20.2 / 19.7 direct - all one-point spikes, not
roots hovering at the threshold. With a crossing required to hold for two points
(`drop_isolated`) both Linux flows give 33.3 / 29.9 / 22.3 / 20.2 / 19.7. **A crossing
that moves between two builds or flows: look for a one-point spike on the V-g plot
first.** Check a restart-flow change against Jon's reference build running the same flow.

## 9. Still serial, and what to try next

FA1 is seven tenths of the CPU and nearly all of it the QR: 4-5 ms per iteration, bound by
its own dependences, and the order of its operations is the answer. Each child's setup is
about 6 s of serial -O0 at the start with most of the machine idle (moving APD + AMG ahead
of MCE1/READ by DMAP ALTER would overlap it, but the rigid format's conditional jumps and
Jon's restart tables sit on those statement numbers). AMP's GINO read chain is serial and
slow under load. A marked loop's output is OFP formatting, serial.

Next for FA1 is fewer QR solves, not a faster QR. Two ways, and two that are worse:
- **Fewer roots.** Solve only the roots that are wanted (FLUTTER NVALUE; done at deck
  level, section 4).
- **Fewer QRs per root.** NASA already starts each root well. When root i converges, the
  next root's first estimate comes free from the eigenvalues at root i's k, and the
  fixed-point map contracts, so the start is close.
- **Worse: a start from k = 0.** It is further from the answer than NASA's start, and it
  costs a QR.
- **Worse: continuation from the previous matched point.** It serialises the loops, which
  run side by side.

What is left is to converge faster, with a secant step on k = (b/V) Im(lambda_i(k)). The
fork's experiment for it is `N95_PK_SECANT=1`; `N95_PK_STATS=1` prints QR solves per loop
and per root; `HALO.md` ("A secant step in the PK iteration") has the design and what it
moved. Such a change cannot be bit-identical: judge it by every crossing of every Mach,
against the reference build and Simcenter.
