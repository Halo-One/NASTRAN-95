# Halo One's fork of NASTRAN-95

This is a fork of [nasa/NASTRAN-95](https://github.com/nasa/NASTRAN-95), the
1995 COSMIC release NASA published under the NASA Open Source Agreement 1.3.
Upstream's last commit was December 2015 and NASA offers no support.

It is consumed as a git submodule by `nastran95/solver/` in Halo One's
`ai_tools` repository, which holds the build scripts, the runner, the
regression suite and the browser-based pre/post processor. Build instructions
live there; the short version is `nastran95/build.ps1`.

## What changed, and why

NOSA 1.3 section 3.B requires modifications to be identified, so here they
all are. The diff against NASA's tree is nineteen files, and everything that
makes the executable stand on its own is in new files or in the main program.
Every source change is tagged `C HALO:` in place with the reason.

### Build system (new files)

| File | Why |
|---|---|
| `CMakeLists.txt` | NASA shipped no build system at all. Descended from the CMakeLists in [Foadsf/NASTRAN-95](https://github.com/Foadsf/NASTRAN-95), rewritten for gfortran on Windows, for `-O0` by default, and for an open core that is a build option rather than a literal in the source. |
| `.gitattributes` | NASA's files are stored with CRLF endings. Windows checkouts default to converting them, which would rewrite every line of the reference output in `demoout/` that the regression suite diffs against. |
| `.gitignore` | Build output. |

### Renamed

`bin/nastrn.f` → `bin/nastrn.f.in`. The main program is now a CMake template.
The only substitution is the open core size, which NASA wrote as the literal
`14000000` in two places that have to agree: the `COMMON /ZZZZZZ/ IZ(...)`
declaration, which is the memory, and `LENOPC`, which is what the rest of the
program believes it has. Making it one setting removes the chance of changing
one and not the other.

`mds/nastrn.f` is a byte-for-byte duplicate of the original and is untouched.
It is excluded from the build; editing it has no effect.

### Source changes, all for gfortran

| File | Change |
|---|---|
| `mds/cputim.f`, `mds/nastim.f` | `CALL ETIME(ARRAY)` → `CALL ETIME(ARRAY,TOTAL)`. gfortran's `ETIME` is a two-argument subroutine. The value used is still `ARRAY(2)`, exactly as NASA had it, so no timing behaviour moves. |
| `mis/endsys.f`, `mis/pexit.f` | `LINK` added to `EXTERNAL`. gfortran provides `LINK` as a GNU intrinsic wrapping `link(2)`, which an undeclared `CALL LINK` binds to instead of NASTRAN's own overlay-switch routine. |
| `mis/sofut.f` | `RENAME` added to `EXTERNAL`, for the same reason: gfortran's `RENAME` intrinsic renames a file, and the one meant here is the five-argument substructure rename in `mis/rename.f`. |
| `mis/xdcode.f` | The card decoder no longer round-trips through a formatted read from an internal unit. gfortran 16 terminates such a field at a comma even under an `A` edit descriptor, so `****SBST   1,  3` decoded with the comma turned into a blank and every rigid format failed to load with UFM 8020. The same read from an *external* unit keeps the comma. |

### A second change that is not a compiler fix

`mds/tdate.f` built the two-digit year the page header prints as
`DATE1(3) - 1900`. gfortran's `IDATE` returns the full year, so from 2000
onwards that overflows the `I2` field in `mis/page.f` and every page of every
print file reads `SEP 17, **` instead of a date. It is now `MOD(DATE1(3),100)`,
the two digits NASA meant. The year is also packed into the checkpoint tape id
in `mis/xcsa.f` in an eight-bit field, which 26 fits and 126 only just did. No
number in any print file changes.

### One change that is not a compiler fix

`mds/rfopen.f` held the rigid format directory and the assembled path in
`CHARACTER*44`. The path is built as `RFDIR // '/' // name`, so a 38-character
`RFDIR` leaves five characters for the name, and Fortran truncates the
assignment without a word:

* `AERO10` became `AERO1`, which does not exist, so those decks stopped.
* `DISP10` became `DISP1`, **which does exist**. A deck asking for complex
  eigenvalue analysis silently ran rigid format 1, statics, and the only
  symptom was a later complaint about a missing LOAD card.

The second of those is a wrong answer rather than a failure, which is why it is
worth changing NASA's code over. `RFDIR` is now `CHARACTER*72`, matching what
`bin/nastrn.f` already stores these names in, and `DSN` is `CHARACTER*80`.
Fixing it took the number of demonstration decks reproducing NASA's own output
from 55 to 70.

Nothing else. In particular, no numerical code, no algorithm and no element
formulation has been touched.

### Deliberately *not* changed

The upstream CMake build excludes `bd/semdbd.f`. That is a mistake and this
fork includes it. It is the only `BLOCK DATA` in the tree that initialises
`/XMSSG/` (the `0*** USER FATAL MESSAGE` prefixes), `/SEM/`, `/SYSTEM/`,
`/XFIAT/` and `/OSCENT/`. Without it the executable still links, and then runs
on zeroed control blocks. There is no second copy of it, whatever the `bd/`
naming suggests.

The `!DEC$ IF DEFINED(CRAY_COMPILER)` guards the Foadsf fork adds around
`CDIR$` lines in `mds/dsupkc.f`, `mis/partn3.f`, `mis/rcard.f` and
`mis/rcard2.f` are not carried over. `CDIR$` has a `C` in column 1, so it is
an ordinary comment to gfortran; those guards are only needed for Intel
Fortran, which recognises `CDIR$` as a directive prefix.

## Making the executable stand alone

NASA's `nastran.exe` cannot be handed to anyone. It reads the deck from stdin,
writes the print file to stdout, and takes the name of every other file from an
environment variable -- about twenty-five of them, none with a default. An
unset `DBMEM` is not a default; it is an end-of-file on an integer `READ`. The
supported way to drive it was `bin/nastran`, a csh script with one user's home
directory hard-coded in it.

These changes make `nastran.exe deck.inp` work on a machine with nothing
installed on it:

| File | Change |
|---|---|
| `CMakeLists.txt` | `NASTRAN_STATIC_RUNTIME`, on by default: link libgfortran and libgcc statically. Without it the executable imports `libgfortran-5.dll` from the conda environment it was built in, and Windows refuses to start it anywhere else. What is left is `KERNEL32` and the `api-ms-win-crt-*` set, which are part of Windows. |
| `bin/nastrn.f.in` | A deck named on the command line is opened on unit 5 and `<deck>.out` on unit 6, beside the deck or in the directory given as a second argument; the solver changes into that directory and names its outputs by stem, because every name it holds is a `CHARACTER*72`. Every environment variable gets a default; each one applies only to a blank, so a caller that sets them is unaffected. A no-argument run on a terminal prints usage instead of waiting silently on stdin; `--version` prints `NASTRAN_BUILD_ID`, a configure-time string. |
| `mds/rfopen.f` | Take `RFDIR` from `COMMON /DOSNAM/` rather than calling `GETENV` into a local of the same name. It was the only routine in the solver reading its own configuration out of the environment, which meant an executable that had worked out where its rigid format library was could load `NASINFO` and then fail to load `DISP1`. |
| `utility/hrfgen.f` (new), `CMakeLists.txt` | The rigid format library is compiled in. At build time `hrfgen` turns every file in `rf/` into a generated `hrflib.f`: one `WRITE` of a character constant per line, split into 40-character pieces so nothing passes column 72, apostrophes doubled, one subroutine per file. `HRFLIB(dir)` writes the library into a directory; `HRFDEL(dir)` removes it. At start-up the main program writes it into the scratch directory and points `RFDIR` there, unless `RFDIR` is set, which still wins. `RFOPEN` is untouched: it reads files from `RFDIR` as it always did. The previous arrangement - search for `rf/` next to the executable - came up empty whenever the program was run by bare name from `PATH`, because argument zero is then a bare name too. |
| `mds/hoswin.f`, `mds/hosunx.f` (new) | Make, remove and enter a directory, where `TEMP` is, and a line to standard error: five routines against the C runtime's `_mkdir`/`_rmdir`/`_chdir`/`_write` (Windows) or `mkdir`/`rmdir`/`chdir`/`write` (POSIX), by way of `BIND(C)`. CMake compiles one of the two. The message routine exists because on some fatal paths the solver has `CLOSE`d Fortran unit 0 by the time the exit handler runs (a `CLOSE` on a unit variable that is zero), and a Fortran `WRITE` to a closed unit 0 invents a file called `fort.0` in the output directory and loses the message in it. The scratch directory is `TEMP\n95_<pid>` (`TEMP` only when it is 50 characters or shorter, because `DIRTRY` is a `CHARACTER*72` that gets `/scr90` appended; else `<system drive>\n95tmp`), made fresh per run and removed at exit, unless `DIRCTY` is set, in which case it is used and left in place. |
| `mds/hexit.f` (new), `mds/HSTATE.COM` (new) | The exit handler. NASTRAN ends in several places - `PEXIT` is the intended one, but `ENDSYS`, `DSMG1`, `FFREAD`, `NSINFO`, `DBMIO` and a few others `STOP` or `CALL EXIT` on their own, and a runtime error ends the process from inside libgfortran - so the tidy-up is registered with the C library's `atexit()` and runs on all of them. It closes every unit, calls `HCLEAN`, removes the optional outputs left empty (`.pch`, `.plt`, `.dic`, `.nptp`), and when the deck was named on the command line reads the print file back and ends the process with `_exit(2)` if there is no `END OF JOB` banner or `_exit(3)` if there is a `USER` or `SYSTEM FATAL MESSAGE`. NASA's solver exits 0 after a fatal message, which nobody driving it from a script wants; a wrapper used to grep for this, and now the exe does. `HSTATE.COM` is the state the handler needs (mode, whether this run made the scratch directory, the print file name). |
| `mds/hclean.f` (new), `mis/pexit.f` | Delete the scratch files, the embedded library's copy, the scratch directory when this run made it, and the `none` placeholder, as NASA's csh wrapper did. Called from `PEXIT` and again from the exit handler after every unit has been closed, which is what makes a still-open scratch file deletable on Windows; safe to repeat. |

Three traps worth recording, all of which cost time:

* **`atexit` from Fortran is fine, as long as the handler runs before the
  runtime's own clean-up.** It does: libgfortran registers its clean-up at
  program start and handlers run last-registered first, so Fortran I/O still
  works inside the handler. Setting a new exit code from inside `exit()` has
  to be `_exit()`; calling `exit()` again is undefined.

* **A backslash is not an escape in Fortran by default**, so `'\'` is a
  two-character string that can never equal one character -- and every path on
  Windows is spelled with backslashes. The directory scan silently found no
  separator at all. `CHAR(92)` is unambiguous.
* **Nothing may pass column 72.** A `WRITE` whose string ran to column 74 was
  truncated mid-literal and the compiler reported an unterminated character
  constant several lines later.

## Licence

NASA Open Source Agreement 1.3, unchanged. See `NASA Open Source
Agreement-NASTRAN 95.doc`. It permits modification and internal use; if a
modified solver is ever distributed it must go out under NOSA with the
modifications identified, which is what this file is for.

## The MSC dialect front end, and the second executable

`nastran95ase` is built from the same library as `nastran`; the two main
programs are one template (`bin/nastrn.f.in`) configured twice, differing in
one logical. Everything below is new in this fork and none of it is NASA's.
NASA's solver reads what it always read; the front end rewrites the input
before the solver sees it and the print file after the solver is done.

| File | What |
|---|---|
| `msc/mscread.c` | Reads an MSC Nastran deck: small, large and free field, continuations (a line contributes eight field slots whether or not it was written -- getting this wrong moves a PBAR's I12 into the K2 column), nested `INCLUDE`s, and the `INCLUDE 'path` / `rest'` form split over two lines that this repository's writers use. |
| `msc/mscxlat.c`, `msc/mscexec.c` | The translation. `SOL 101/103/105/107-112/145/146` to rigid formats 1/3/5/7-12 and AERO 10/11; case control with prefix-matched names and MSC-only commands dropped with what they cost named; `RBAR`/`RBE2` to `CRIGD1` (independent end chosen so that a grid on a SUPORT or SPC is never made dependent), `CBUSH`+`PBUSH` to `CELAS2` (coincident) or `CONROD` (separated, axial only), `EIGRL` to `EIGR FEER` with a shift, `PBARL` to `PBAR`, `CQUAD4`/`CTRIA3`/`PSHELL` to `CQUAD2`/`CTRIA2`/`PQUAD2` (panels thinner than 1e-6 dropped as the massless drawing aids they are), `SUPORT1` to `SUPORT`, ids above 2^24-1 renumbered everywhere they are referenced, SPC1 `THRU` expanded, and every degree of freedom nothing is attached to constrained, which is what MSC's AUTOSPC does. A card it does not know is a fatal that names it. |
| `msc/mscwrite.c` | Eight-column output. `msc_r8` tries every eight-column spelling and keeps the one that reads back closest; a number wider than eight columns is re-spelled, never cut (`-6.89e+04` cut to eight reads as -6.89: this happened, on a PBAR, and cost an eigensolve ten minutes of finding nothing); large-field cards when even that loses more than 1e-5. |
| `msc/mscf06.c` | The print file rewritten into MSC's layout on the way out, so that a reader written against MSC output reads it: the eigenvector banner carries `CYCLES =` and the mode number where MSC puts them, exact zeros are `0.000000E+00`, the eigenvalue table has MSC's sub-banner and no blank between header and rows, the weight generator's rows sit at MSC's columns, the sorted-echo banner is spelled as MSC spells it, renumbered ids are restored, modes past the number requested are cut (FEER returns a reduced problem's worth), and no line is zero-length. |
| `msc/mscop4.c` | `ASSIGN OUTPUT4` and the `OUTPUT4 PHG//-1/101/2` alter of the SEMODES decks become `ALTER 77` in rigid format 3 (after SDR1, where PHIG and MGG both exist; the number is from a DIAG 14 listing), FTN11.. units, and a rewrite of NASTRAN-95's formatted file into MSC's 4I8 / A8 / `1P,5E16.9` layout that `OUTPUT4_rd.m` and ZAERO read. Three traps live in that rewrite, all of them from `mis/outpt4.f` rather than from any document: the records are fixed-width Fortran output (`1X,3I13` then `1X,10E13.6` single precision, `1X,3I16` then `1X,8D16.9` double) and must be sliced at the field width, because a negative number fills its field to the edge and two adjacent negatives touch; `JJ` in a column header counts single-precision *words*, so a double-precision column announces twice the values it holds; and a column with no terms comes back with `II` zero and the previous column's words still in the unpack buffer, so it has to be read past and left out, which is also what MSC's own OUTPUT4 does with it. `N95_KEEP_OP4` in the environment keeps the raw file next to the converted one. |
| `msc/mscopt.c`, `msc/mscopt2.c`, `msc/mscopt.h` | `SOL 200`. The design model (`DESVAR`, `DVPREL1`, `DVMREL1`, `DLINK`, `DRESP1` WEIGHT/VOLUME/FREQ/EIGN/DISP/STRESS, `DCONSTR`, `DCONADD`, `DSCREEN`, `DOPTPRM`, `DESOBJ`/`DESSUB`/`DESGLB`/`ANALYSIS`), MSC's constraint normalisation, forward-difference sensitivities from child runs of this executable (`--cosmic`), convex linearisation (CONLIN) solved through its dual, move limits, hard convergence. Weight and volume are closed-form from the model. On MSC's own three-bar truss example it follows MSC's design-cycle history to within half a percent at every cycle. |
| `msc/mscdiag.c` | The solver's fatal messages, repeated on the terminal with what they mean and what to do, in both executables. |
| `msc/mscmsg.c`, `msc/mscmap.c`, `msc/mscutil.c`, `msc/msc.h` | Numbered messages in the solver's own three-part shape (what, where, fix; this front end's 9000 series), a per-card tally, an integer map, string helpers. |
| `mds/hmsc.f` (new) | The `BIND(C)` bridges the main program and exit handler call. |
| `bin/nastrn.f.in` | The ASE branch (translate, then open the translated deck), `--cosmic`, MSC launcher keywords (`out=` honoured, `scr=`/`bat=`/`old=`/`append=` accepted), the SOL 200 hand-over, and two fixes: the checkpoint tape and SOF 1 both defaulted to a file called `none` and a substructuring deck read the one as the other (UFM 6206); and `nastran95ase` splits open core three quarters for the modules. |
| `CMakeLists.txt` | C added to the project; `NASTRAN_ASE_MODE` configured twice; the `msc/` sources in the library. |
| `mds/hexit.f`, `mds/HSTATE.COM` | `HASE` state; the print-file rewrite and the fatal explainer called on the way out. |

## Never hang: the FEER guards and the watchdog

A 830-grid modal deck (lumped masses on rigid bars, the shape of every model in
the caller's ASE chain) ran in a second on MSC Nastran and spun on this solver
until killed. The mechanism, found with `DIAG 16` and the Fortran runtime's
buffering turned off:

* **`mis/ferxtd.f`, `mis/ferxts.f` (the FEER tridiagonal reduction).** At label
  480 the mass norm of the new trial vector goes under a square root. On a mass
  matrix that is only semi-definite - lumped masses carry no rotary inertia, so
  every rotation is massless - roundoff put that norm at -1e-17 on row 88 of
  250, `DSQRT` returned NaN, and every test after it is a comparison a NaN fails:
  the "null vector, reseed" test let it through, the next row divided by it, and
  rows 88 to 250 came out NaN. G. Chan's 1992 comment two labels earlier asks
  "what happens if D is negative here?" about the off-diagonal term; this is the
  same question for `DB`, and the answer was a hang. Now a norm that is not
  positive (or is NaN) is treated as the null trial vector it is: the reduction
  reseeds through its existing restart loop (`FEER3`, capped at MORD reseeds),
  and `mis/fernpd.f` (new) prints `UWM 2394` once saying so. The same guard is on
  the two start-vector normalisations (labels 40 and 240) and a NaN test joins
  the two existing "problem size reduced" exits (labels 150 and 310). If fewer
  modes come out than were asked for, the solver's own `UWM 2390` reports the
  count, and the exit handler repeats both warnings on the terminal.
* **The reseed itself was broken, in the same two files.** Once the null vector
  was handled, the reseed re-entered `FERXTD`, which reads the previous trial
  vectors back from scratch file 7 (label 65) - and NASA's 1994 in-core
  modification (`NIDORV`) keeps them in memory and only writes them to that
  file when the reduction is complete, so the reseed found nothing there and the
  run stopped with `I/O SUBSYSTEM ERROR NUMBER 110, EXPECTED A SB OR EB CONTROL
  WORD ON FILE SCRATCH7` - after `END OF JOB`, exit code 0, no modes. On a
  modern open core the vectors always fit, so the reseed path had never worked
  in this build. The in-memory vectors are now written to the file (from the
  start, trailer reset) before every return to the reseed loop, and the exit
  handler and the explainer count a GINO `I/O SUBSYSTEM ERROR` as the fatal
  it is.
* **What the model then gets.** The 830-grid deck that hung returns 89 accurate
  modes of the 120 it asked for, in 7 seconds. After a reseed on this
  semi-definite metric the next vector's orthogonalisation converges by a
  factor of 0.6 a pass and each vector after it slower still; raising NASA's
  cap of 14 passes to 60 bought four more rows and nothing else, so the cap is
  kept at 14 and the shortfall is recorded for what it is: FEER works in the
  mass metric and a semi-definite mass matrix is outside what it was written
  for (MSC's Lanczos is not affected). The translator now counts the `CONM2`
  cards with no rotary inertia and, on a modal solution, says so up front
  (`UWM 9133`) with the cure: rotary inertia on the lumped masses, or fewer
  modes.
* **`mis/fqrwv.f`, `mis/fqrw.f` (the QR iteration on the reduced matrix).** The
  sweep at label 70 had no bound. It now stops after 200 sweeps per eigenvalue
  plus 1,000, or on a NaN in its input, with `UFM 2395` and a fatal exit
  (`MESAGE -37`, as FEER itself does for a singular matrix). Defence in depth:
  with the reduction guarded it should never fire.
* **`msc/mscwatch.c` (new), `mds/hmsc.f`, `bin/nastrn.f.in`: the wall-clock
  watchdog, both executables.** NASTRAN's `TIME` card is checked between modules
  (`TMTOGO`), so a loop inside a module is invisible to it. A second thread now
  sleeps for the allowed wall-clock time and ends the process with `_exit(2)` and
  a message saying which limit applied, that the print file stops where the
  solver was, that the scratch directory is left behind, and how to raise the
  limit. `_exit` and not `exit`, because the main thread may be inside a Fortran
  `WRITE` with a unit locked, and running the runtime's clean-up over that from
  another thread is not safe. The limit is `N95_TIMEOUT` from the environment
  (minutes, 0 disables), else the `TIME` card for the 1970s executable (NASA's
  own default of 5 minutes when it is missing, the same number the solver
  prints), else 30 minutes for `nastran95ase`, whose translated decks carry a
  `TIME` that means nothing to the user; the 5,400-grid decks it exists for
  take two minutes.
* **`mds/hexit.f`, `bin/nastrn.f.in`: the MSC-deck notice.** The 1970s
  executable handed an MSC deck fails on the first executive-control line with
  `UFM 300`, which the explainer described, truthfully and uselessly, as a field
  width problem. The start-up pass that reads `CHKPNT` now also notices a
  three-digit `SOL` or an `INCLUDE`, and the exit handler says to use
  `nastran95ase` instead.
* **`msc/mscdiag.c`.** Entries for `2386`, `2391` and `2395`.

## The fatal messages: a spelling the verdict missed, and a catalogue audit

Two things were wrong with the way a run reports that it failed.

**A fatal in the other spelling was not a fatal at all.** The solver writes its
numbered messages through two routines. Most modules use `WRTMSG`, which writes
`*** USER FATAL MESSAGE 3097, SYMMETRIC DECOMPOSITION ...` with the text on the
same line. The rest go through `mis/msgwrt.f`, whose formats 2000 and 2001 pad
each word into a field of its own and write `*** USER FATAL    MESSAGE  3056`
with the text on the next line. `HFATAL` in `mds/hexit.f` compared a fixed
18-character literal and `msc_diag` used `strstr(line, "FATAL MESSAGE")`, so both
saw the first spelling and neither saw the second. **A run whose only fatal was
of the second kind printed the success line and exited 0.**

A statics deck with a `GRAV` load and no mass anywhere is one: its single fatal
is `UFM 3056, NO MASS MATRIX IS PRESENT BUT MASS DATA IS REQUIRED`, spaced. So is
a run that exhausts open core (`SFM 3008`), which is why the `-8` entry never
appeared, and so are `3005` and `3037` when they arrive first, which is why those
two entries could never be reached either. Both places now collapse the runs of
blanks in a copy of the line before matching (`HSQZ` in `mds/hexit.f`,
`squeeze()` in `msc/mscdiag.c`), so every spelling of a message reads alike; the
same copy feeds the `UWM 2390` and `2394` notices and the number lookup. The two
scanners were also made to agree on what a fatal is (asterisks, then `USER` or
`SYSTEM`, then `FATAL MESSAGE`): `msc_diag` used to accept `*** USER POTENTIALLY
FATAL MESSAGE`, a note eight of NASA's demonstration decks print and run past.

All 132 NASA demonstration decks were run before and after. The new rule flags
exactly the 18 decks recorded as `fatal` in `NASTRAN/test/demo_expectations.txt`;
the old one flagged 17, missing `t01231a`, whose only fatal is a spaced `3037`.
No recorded verdict moves, because the MATLAB test matched fatals with a
whitespace-tolerant regular expression all along. Only the executables were wrong.

**A rigid format that needs a mass matrix and has none** stops in its own DMAP
check, which prints no numbered message and lets the job end normally: `END OF
JOB`, exit 0, and no eigenvalue table. The exit handler now says so on the
terminal, next to the `2390` and `2394` notices. The exit code is left alone.

**The catalogue was audited against the manual, the source and the solver.**
Every entry was checked against `um/MSSG.TXT`, against the routine that emits the
message, and, where a deck could provoke it, against a run. What that found:

* `305` was the entry for an unknown card name. The solver prints `UFM 307,
  ILLEGAL NAME FOR BULK DATA CARD CBEAM` for that; `305` is a **system** fatal
  from `mis/ifp.f:172`, `IFP CANNOT OPEN GINO FILE`. So the commonest mistake in
  a hand-written deck was answered with "no explanation on file". Re-keyed to
  `307`, and the card list in its text corrected: NASTRAN-95 does have `CQUAD4`
  and `CTRIA3`; what it lacks is `CBEAM`, `CBUSH`, `RBAR`, `RBE2`, `EIGRL` and
  `PBARL`.
* `2015` could never fire. It is a **warning** (`mis/usrmsg.f:94`, and the manual
  says "a warning only"), and the explainer runs on fatals. The condition it
  described reaches the user as `UFM 3097`, which the catalogue already answers
  with the same advice. Deleted.
* `2140A` is the warning; the fatal that follows it is `2140B`. Re-keyed.
* `3097` said the stiffness matrix. `GIV` and `MGIV` factor the **mass** matrix,
  so the same message arrives on data block `MAA` when the rotations are
  massless, and the cure there is the opposite of constraining them. Its fix also
  said `nastran95` cannot constrain singular freedoms itself. It can:
  `PARAM AUTOSPC 1` prints `UIM 2435` and the `SPC1` cards it generated. What
  NASTRAN-95 lacks is MSC's `PARAM,AUTOSPC,YES` spelling.
* `3037` is not the bandwidth resequencer running out of scratch. It is the
  generic `JOB TERMINATED IN SUBROUTINE ****` that about a hundred routines call
  after printing their own reason, so the text now says to read the message above
  it first, and keeps `BANDIT` as the example it is.
* `300`, `311`, `316`, `505`, `615`, `2050`, `2101A`, `3005`, `3031`, `2386`,
  `2395`, `6206` and the `GINO` and `-8` entries each had a sentence a run
  disproved: the real-needs-a-decimal-point advice on `300` belongs to `315`, the
  unique-ids advice on `311` is not true of load and constraint sets, the
  continuation clause on `316` belongs to `209`, and so on.

Twenty-one entries were added for messages a deck of this kind actually meets,
each one provoked by a deck before it was written: `307`, `313`, `315`, `340`,
`507`, `617` (card and control errors), `2007`, `2010`, `2053`, `2192`, `2200`,
`2215`, `2423` (missing grids, properties, materials and constraint sets, a
`SUPORT` direction with no mass, a freedom made dependent twice), `3008`, `3032`,
`3056` (out of core, a missing `EIGR` set, a `GRAV` with no mass) and `3118`,
`3145`, `3147`, `3176`, `3178`, `3179` (zero-length elements, constraint
components, load-set combinations). The catalogue holds 41 entries.

## The four "unstable" demonstration decks, and the libgfortran defect behind them

NASA's d03021a, d03031a, d07021a and d07022a (gas in a spherical tank, liquid in
a half-filled sphere, a gas-filled thin cylinder: the `AXIF`/`CFLUID` fluid
elements) either completed and matched NASA's 1995 print file or died with
SIGSEGV, and which one happened depended on the process's memory layout: the
same executable, deck and scratch path flipped with the size of the environment
block, between a shell and MATLAB's `system()`, and never under a debugger. It
was recorded as memory corruption in the fluid-element code path. It is not.

With `_NO_DEBUG_HEAP=1` gdb reproduces it, and the fault is inside libgfortran's
`parse_format`, called from `mis/ofp.f` line 1031: the run-time-format `WRITE` of
the SORT-1 fluid harmonic-point line. NASTRAN builds that format, like many
others, in an `INTEGER FMT(300)` array of Hollerith words and hands the array to
`WRITE` as the format. gfortran accepts the extension and passes the runtime the
array's storage as a 1,200-byte string, of which the format text is the first
hundred or two and the rest is zeros (the array is static, in `.bss`, and never
written past the closing parenthesis).

libgfortran (`io/format.c`) copies that string with `fc_strdup_notrim`, which is
`strndup` (`runtime/string.c`): it stops at the first NUL, so the copy is a few
hundred bytes. `dtp->format_len` stays at 1,200. `save_parsed_format` then runs
`format_hash` over the short copy for 1,200 bytes, reading up to a kilobyte past
the end of a small heap block. Whether that read crosses into an unmapped page
depends on where `malloc` put the block, which depends on everything allocated
before it, the environment block included. That is the whole of the symptom.
The defect is in GCC 15 and on trunk; nothing in NASA's code is wrong by the
standard of the extension it uses, and every run-time format in a zero-filled
array in this tree was one heap layout away from the same crash.

The fix is `msc/mscgfwrap.c`, linked into each executable with
`-Wl,--wrap=_gfortrani_fc_strdup_notrim` (`CMakeLists.txt`): a copy of the full
`src_len` bytes, NUL-terminated. The parser still stops at the parenthesis it
always stopped at, the cache comparison (`strncmp`) is unchanged, and the hash
stays in bounds. The wrapper object is on each executable's own source list
rather than in the library, because the reference to `__wrap_` only appears when
the linker reaches `libgfortran.a`, after it has finished with `libnas.a`.
Checked by running the four decks at twelve environment sizes from 0 to 20,000
characters: 48 of 48 complete and match NASA, where the unwrapped executable
failed 4 of 36.

Why only those four decks, when the whole tree builds formats this way: this
build runs as machine type 7 (`mds/btstrp.f`), for which `OFPPNT` never takes
its `WRITE (L,FMT,...)` branch (machine types 2, 5 and 21 only) and prints every
other output table line through `FORWRT` (`mds/forwrt.f`), NASTRAN's own format
interpreter. The SORT-1 fluid harmonic-point line at `OFP` labels 1800-1960 is
the one `WRITE` in the solver that hands the 1,200-byte zero-padded array to
libgfortran, and only `AXIF`/`CFLUID` decks reach it. `WRTMSG` (`mis/wrtmsg.f`,
`WRITE (MO,FOR)` with `INTEGER FOR(100)`, every user message) and `TABLE5`
(`mis/table5.f`, `CHARACTER*10 FMT(30)`) are the same class and were one heap
layout away from the same fault; the wrapper covers them too. A second
libgfortran hazard in the same code, `format_error` writing
`format_len - format_string_len` spaces into a 300-byte stack buffer, also goes
away once the two lengths agree.

An 80-line stand-alone reproducer (OFP's declarations, its format builder and
the two `WRITE`s, run at a sweep of environment sizes) crashes with the pinned
toolchain and does not with the wrapper or with `DATA FMT/300*4H    /`. The
web has nothing on it: no issue or pull request on `nasa/NASTRAN-95`, no fork,
no distribution patch, no GCC bug report. The nearest prior art is GCC's 2013
fix for PR56737, whose `xmalloc`+`memcpy` copy was later replaced by the
`strndup` that does this (present from GCC 5.5). It is worth a GCC bug report
with that reproducer.

### What the front end is checked against

Everything above is verified against MSC Nastran 2025.1 on the same decks, in
this repository's caller rather than here: `NASTRAN/test/test_nastran95_three_way.m`
in Halo One's VehicleDesign, leg 5. Two of its checks were worth the trouble of
writing, because both failure modes returned a plausible number rather than an
error:

* **The weight generator against MSC's, digit for digit.** `PARAM,GRDPNT` names a
  grid, so it has to move with the renumbering of ids above 2^24-1. It did not,
  and the two codes reported the centre of gravity from points 0.15 m apart on a
  1,400-grid model - the height of the aero reference grid - with the mass and
  every inertia agreeing. A c.g. 150 mm out on an aeroelastic model is wrong and
  is not obviously wrong.
* **The OUTPUT4 matrices read back through the caller's own reader, and
  `phi' M phi` against the identity.** A mass matrix with one spurious entry per
  empty degree of freedom still factors, still gives modes, and still looks like a
  mass matrix.

## Flutter: matched points, the aerodynamic solve, threads, and what the g-method would take

Branch `halo-ase-sol145`. NASA's AERO 10 rigid format (modal flutter) has three
methods, K, KE and PK (User's Manual 1.11.4 and 3.20), and its PK loops over
every combination of the density, Mach and velocity FLFACT lists - the inner
loop is velocity - with a hard stop at 100 loops (Flutter Error 3). The decks
Halo One writes for MSC / NX Nastran are matched-point analyses in the ZAERO
FIXMATM sense: at a fixed Mach, one point per altitude of a standard-atmosphere
list, density and true airspeed belonging together, which MSC and NX spell
`FLUTTER ... PKNL`. Forty altitudes as a PK product would be 1,600 loops.

### PKNL, and PK on matched points (`PARAM,PKMATCH,1`)

What the branch changed, file by file, so that a stock PK becomes a matched
point analysis:

* `mis/ifs5p.f` - the IFP accepts a fifth method name, `PKNL`, on the FLUTTER
  card (`MET(5)`), so an MSC deck can also be solved as written.
* `mis/fa1.f` - FA1 builds the flutter loop list that FA2 and the DMAP loop
  walk (records of `FSAVE`). For PK it wrote every (density, Mach, velocity)
  combination; for a matched-point request it now writes one loop per entry
  of the density and velocity lists taken together, the Mach list walked
  alongside (its last entry repeated when it is shorter). Everything after
  that - the PK iteration on each loop, `FA1PKE`, the convergence on k - is
  NASA's PK untouched: a matched point is just a loop with its own density
  and velocity. A ninth word in the `FSAVE` header (`REC0(9) = 1`) tells FA2
  the run was matched.
* `mis/fa2.f` - with that word set FA2 prints the flutter summary as MSC does:
  one block per root, a row per matched point, DENSITY and MACH columns in
  the row (COSMIC's own PK layout is one block per (Mach, density) group);
  `read_nastran_flutter` in VehicleDesign reads both layouts.
* `rf/AERO10` and `mis/fa1.f` again - the request comes in through a
  parameter, not only through the spelling: `PARAM,PKMATCH,1` in the bulk
  data is handed to FA1 by the rigid format (`FA1 .../S,N,NOCEAD/V,Y,PKMATCH=0`,
  the fourth word of its `/BLANK/` common), and `FLUTTER ... PK` with it set
  takes the matched path. So the deck NASTRAN-95 solves is spelled with the
  method every NASTRAN has, plus one parameter that MSC and NX ignore.
* `msc/mscxlat.c` - the front end writes MSC's `PKNL` as `PK` and emits
  `PARAM PKMATCH 1` once; a deck that already says `PK` + `PKMATCH` passes
  through. The translated deck (`<stem>_n95.dat`) is what to read when in
  doubt.

Why not make PK itself walk the lists when they have the same length: a user
who wants the 3 x 3 product of three densities and three velocities would get
three points and no warning. The parameter says what is meant.

### Where the time went, and the in-core aerodynamic solve

On the monarch (2,282 doublet-lattice boxes, 31 reduced frequencies per Mach)
one Mach took 134 min in the -O0 build against 6.4 min in MSC. Profiling it
needs care: the module log's CPU column is the process's *system* time
(`mds/cputim.f` returns `ETIME`'s second element, as NASA shipped it; the
fork kept that because a real clock would arm the `TIME` card's limit) and
its wall-clock column is zero, so the only trustworthy profile is the log
written unbuffered (`GFORTRAN_UNBUFFERED_ALL=y`) with its `BEGN`/`END` lines
timed as they appear. Done that way on the reduced deck (Mach 0.10, 31 k,
30 modes solved, 20 kept), two modules hold everything: AMG, the doublet
lattice, and AMP, whose `AMPC` for each k transposed the AJJ matrix
(`TRANP1`), decomposed the transpose out of core through GINO (`CFACTR`,
the banded unsymmetric `CDCOMP` with scratch files and packed columns) and
solved the NOH right-hand sides (`CFBSOR`). FA1/FA2, the PK iterations
themselves, take under a second for 40 matched points.

`mis/ampcz.f` (new) replaces the AMPC step when there is one theory group
and the matrices fit in open core: AJJ is unpacked transposed into core as
complex double precision (2,282 square is 83 MB, against the ASE
executable's 48 million-word open core), factored in place with partial
pivoting, the right hand sides solved, and QJH packed with the trailer
`CFBSOR` would have left. `AMPC` calls it before the transpose and falls
back to the original path when it declines (no core, a vanishing pivot, or
`N95_INCORE_AJJ=0` in the environment, which is how the two paths are
compared). It is compiled `-O2 -fautomatic -fopenmp` (see the CMake
comment): new code with no static locals and no open-core aliasing of its
own, so the reasons the rest of the tree stays at -O0 do not apply to it.
The trailing update of the LU and the right-hand-side solves are OpenMP
parallel loops; libgomp is linked statically like libgfortran (`-fopenmp`
on the link line, not the imported target, which names `libgomp.dll.a`),
and the SOL 145 driver gives each child `OMP_NUM_THREADS` = processors /
children.

The doublet lattice's own arithmetic - the kernel integrals `incro`, `tker`,
`subp`, `subpb`, `idf1`, `idf2`, `snpdf`, `subi`, `akp2`, `asycon`, `suba`,
`intert` and the D-matrix assembly `amgb1b`, `amgb1c`, `amgb1d` - is compiled
at -O2 as well, with the tree's other flags unchanged. The drivers around
them (`amg.f`, `amgb1.f`, `amgb1a.f`, `dlamg.f`, `dlamby.f`, `subph1.f`),
which pack the matrices through GINO, were tried at -O2 and produced an
unusable AJJ (a division by zero in the transpose that read it); they stay
at -O0.

Measured on the reduced deck, wall clock, on a machine already running
other solver jobs:

| build | AMG (doublet lattice) | AMP (31 solves) | FA1 + FA2 | total |
|---|---|---|---|---|
| -O0, NASA's AMPC | (in the 134 min) | (in the 134 min) | < 1 s | 134 min |
| -O0 kernel, in-core solve, 4 threads | 14 min 49 s | 11 min 44 s | < 1 s | 26.7 min |
| -O2 kernel, in-core solve, 8 threads | 8 min 45 s | 16 min 24 s (five other solver jobs on the machine) | < 1 s | 25.4 min |

The two in-core runs agree with each other to every printed digit (800 root values).
Checked: the NASA doublet-lattice demonstration decks d10021a, d10022a and
d10023a (KE and PK on the 15-degree swept wing) through both paths agree in
every flutter summary number to 1e-5 relative or better (the original path
works in single precision, the new one in double), the optimised kernel bit
for bit with the -O0 one; d11031a (AERO 11 gust) and t09061a run unchanged.
The reduced monarch deck's 30 modes agree with MSC's to every printed digit
and its 40 matched points are MSC's; the roots themselves await an MSC or NX
licence for `test_nastran95ase_flutter_vs_msc` (the first run of that test,
before this work, agreed to three digits on the first flexible root).

### Parallel processing, what pays and what does not

* **Across subcases** (one per Mach): `msc/mscflut.c` runs one child process
  per subcase, as many at a time as there are processors. This is the whole
  gain available from process parallelism: every child recomputes the modes
  and the full aerodynamic matrix set, so splitting finer - one child per
  matched point - would repeat that forty times for seconds of PK iteration.
  The children run `--cosmic` and print NASTRAN-95's own layout; the parent
  joins their prints in subcase order and then rewrites the joined file into
  MSC's layout with `msc_f06`, as a single run does from `mds/hmsc.f` (the
  sorted echo's title, which has one more space between the words in the
  1995 print and which a reader finds by the exact string; the renumbered
  ids put back; blank lines as one space). Until that call was there a
  multi-subcase print had no echo a reader recognised, and no mode shape
  could be drawn from it.
* **Inside a subcase, across k**: the AMP loop over (Mach, k) pairs is
  embarrassingly parallel in principle, but the module is written around one
  open core and one set of GINO scratch files; threading it means threading
  the in-core solve (done) rather than the module.
* **Inside the solve**: the LU's trailing update is where the flops are
  (2/3 n^3 complex, 6e10 flops per k on the monarch); it scales with threads
  until memory bandwidth does not (the unblocked rank-1 update streams the
  trailing matrix once per column). A blocked LU would roughly double the
  rate again; not done.
* **AMG next**: with the kernel optimised the doublet lattice is still the
  largest single module. Its drivers at -O0 (the loops over boxes in
  `amgb1a`/`amgb1`, and GINO packing of a 2.6 GB AJJL) are the remainder;
  making them safe at -O2 means finding what they alias, one file at a time
  (the demos d10021a-d10023a are the check).
* **Not worth it**: -O2 on the whole tree (breaks the parser and the FEER
  guards, see above); a per-point split of the flutter loop.

### Eigenvectors at marked loops, and the root tracker

A negative velocity in the FLFACT list is the request both MSC and
NASTRAN-95 understand: the eigenvectors of that flutter loop are printed.
NASA's FA1 prints, for every accepted root of the loop, the modal vector -
`FA1PKV`, `EIGENVECTOR FROM THE PK METHOD`, the eigenvalue and then the
complex coefficient of each mode of the modal basis - and the rigid format
recovers the physical vectors at the DISP set, when the case control asks
for them (`DISP` and `OFREQUENCY`, which the front end keeps; `OFREQUENCY`
must precede `OUTPUT(XYPLOT)`), through MODACC, DDR1, SDR2 and OFP as
`COMPLEX EIGENVECTOR` tables in the layout MSC prints, so one reader reads
either solver. Every root of the loop is printed, not the one that crossed:
the monarch deck with twenty loops marked prints 1,201 physical vectors at
251 points and the file is 120 MB. That is the price of the 1970s output
path; a per-root request would be a change in FA1's flag handling and VDR.

The order of the roots between loops was the other half of the problem.
FA1 accepts the PK roots of a loop in ascending frequency (`RSORT` on the
imaginary part), so POINT n of the summary is the n-th lowest root at that
speed, and where two roots cross in frequency the columns swap: the V-g
curves kink and the crossing search reports crossings that are not there.
`mis/fa1pkt.f` adds a tracker: `FA1PKT` keeps every accepted root's modal
vector and eigenvalue of the loop on scratch files (302 for the vectors,
the previous loop's vectors and eigenvalues on MXHH, 204), and `FA1PKU`
assigns the roots of the next loop to the previous loop's places greedily,
by the modal assurance criterion between the vectors plus a quarter of the
eigenvalue distance (the first version compared vectors alone and, with a
root compared against itself, never moved anything). The print says
`ROOT TRACKING: FLUTTER LOOP n - m OF k ROOTS TOOK ANOTHER PLACE TO FOLLOW
THE LOOP BEFORE` for every loop that reordered. Measured on the reduced
monarch deck (Mach 0.10, 40 matched points, 20 modes) against the MATLAB
re-sort the repo's plotter does after the fact (nearest predicted frequency
and damping, the prediction extrapolated from the two points before): both
lower the elastic roots' mean |delta g| per step from 0.078 to 0.054 at
equal frequency continuity, and they agree on the order, so the plotter's
re-sort stays on as the check of the solver's. On the full deck a third
to two thirds of the 57 roots change place at most loops - the low-speed
end, where the aerodynamic roots and the rigid body roots wander, is where
the frequency order and the tracked order differ most.

### The eigenvectors at the aero boxes, the aerodynamic loads, and each child at its own Mach

The flutter rigid format recovers the complex eigenvectors at the aero
points as well: `MPYAD GTKA,CPHIA,/CPHIK` puts the structural vectors
through the spline onto the k-set and `UMERGE` joins them to the
structural rows before SDR2, so a DISP set that names the box ids (the
CAERO1 box numbers) gets one row per box in the COMPLEX EIGENVECTOR
tables - the plunge in T3 and the pitch in R2, in the box's frame. No
parameter switches it on; MSC needs PARAM OPPHIPA for the same rows.

The aerodynamic pressures and forces on the boxes are NASA's `AEROF`
case control request: the ADR module builds P_kf = Q_kh(k, Mach) u_h for
every recovered root and ADRPRT prints them as `AERODYNAMIC LOADS (UNIT
DYNAMIC PRESSURE)`, one line per box with the real and imaginary parts of
T1..T3 and a second with R1..R3. The front end dropped `AEROF` as a
command it did not know; it keeps it now and writes MSC's `APRES` as
`AEROF` (or drops it when `AEROF` is asked for already), since NASTRAN-95
prints pressures and forces together. What still stops the recovery:
ADR takes the reduced frequency of each root from `BOV`, b/V, which APD
sets from the AERO card's velocity field - the matched-point decks leave
it blank, so `BOV = 0.0` and ADR says so (UIM 2272) and prints nothing.
For the K method b/V is one number per loop; for PK on matched points
every marked loop has its own velocity, and several loops are usually
marked in one subcase. The next step is FA1 handing ADR the velocity per
root (the loop's V, which FA1 knows and FA1PKV prints beside each
vector) instead of one BOV for the run; until then the animator's
pressure contour has a reader for ADRPRT's layout and nothing to read.

The parallel driver now gives each child its subcase's Mach, read off
the FMETHOD's FLUTTER card and its Mach FLFACT (one value on the
matched-point decks): `PARAM MACH` for the ADR recovery, which takes the
Mach of the MKAERO1 list closest to it (NASA's default 0.0 took the
lowest for every subcase), and the MKAERO1 / MKAERO2 lists cut to that
Mach. A five-Mach deck's children each computed the doublet lattice and
the solves for all five Machs and used one; they compute one now.

### The modes once: checkpoint and restart

Every child of the SOL 145 driver solved the eigenproblem again - the
five-Mach monarch deck five times - because a child is a complete run of
the AERO rigid format. NASTRAN has had the answer since 1970: checkpoint
the modes run, restart the flutter off its problem tape. The launcher
keyword `scr=no` (MSC's, meaning keep the database) checkpoints: the
translated deck gets `CHKPNT YES,DISK` (the tape form, `CHKPNT YES`, is
UFM 508 on this build), the dictionary is punched to `<stem>.dic`, and
the new problem tape is `<stem>.nptp` through `NPTPNM` - which needed
`mds/gnfiat.f` to actually use the name: its `IPERM` bit was never set,
so the tape was allocated as a scratch file and deleted with the run.
`restart=<modes deck>` restarts off that run: the front end reads the
dictionary into the executive control after the TIME card (RESTART card
first, as punched), translates the modes deck a second time and drops
from the restart deck every card whose translated text that run had
(a hash set of the translated cards - the same translation of the same
lines gives the same numbering), so only the cards the modes run did not
have are written: NASTRAN merges them with the old bulk data off the
tape. The tape is hard-linked (copied when the volume refuses) into the
output directory as `optp.nptp`, because `DSNAMES` are CHARACTER*80 and
the children run one directory down. `optp=<dir>` says where the modes
run left its files when it had an output directory.

Three things the first tries taught:

1. **Continuation tags.** The old bulk data comes back off the tape with
   the tags the translation wrote it with (`+0000001` on), and the
   restart deck's own cards were numbered from one again: UFM 208
   (duplicate parent), 209 (a continuation without a parent), 316
   (illegal data on the EIGC that had taken the EIGR's `MASS` line). A
   restart run's tags begin with a letter, `+R000001`.
2. **What re-solves the modes.** NASTRAN's restart tables re-execute
   every DMAP statement a changed card type feeds, and a card of the
   restart deck that the modes run did not have is a change. The monarch
   restarted off the vibe deck ran GP1 through READ again, 25 minutes as
   before: the flutter deck's 72 `CELAS2` and 6 `CORD2R` were "new". The
   CELAS2 are the CBUSH springs, numbered above the highest element id
   the deck has - which the CAERO1 boxes (7101000 on) move; the CORD2R
   are the aero model's. So the modes deck the repository writes
   (`gen_nastran_cards`, `<config>_modes.dat`) carries `aero_model.bdf`
   with the structure, and holds no flutter cards (a FLFACT or MKAERO1
   list a flutter run changes must be that run's own). UWM 9467 names
   the cards when a restart is going to solve the modes anyway - or
   stop: a card that replaces one of the modes run's under the same id
   is a duplicate to the merge (UFM 311). With that, the restart deck's own cards are the 64 aero and flutter ones,
   and the children go straight from GKAM to APD, AMG and AMP; the
   rigid-format-switch restart (RF3 -> AERO10, `UIM 4145`) is a modified
   restart NASTRAN handles itself.
3. **The eigenvalue table.** READ's OFP is skipped with READ, so the
   flutter print has no `REAL EIGENVALUES` pages and `read_khh` (the
   generalized stiffnesses the mode ranking wants) reads nothing. The
   modes run's pages - the consecutive pages from the first that carries
   the banner, already in MSC's layout - are spliced into the print after
   each sorted echo (`msc_f06`), where a run of its own prints them.

5. **The restart tables.** NASTRAN re-executes a DMAP statement on a
   modified restart when a card of one of the statement's `****CARD`
   bits changed (the table under `$*CARD BITS` in `rf/AERO10`: bit 36
   is FLFACT / FLUTTER, 34-40 the flutter cards together). FA1 and FA2
   carry 34-40, so a restart whose own cards are the flutter cards
   solves the flutter again; VDR, its OFP, the XY section and everything
   from MODACC to the OFPs of the recovered vectors carried bit 21
   (AOUT$) alone, which the rigid-format-switch restart does not set -
   VDR was skipped, NOP kept the tape's value, `COND FINIS,PJUMP`
   jumped, and the restarted print had the PK modal vectors but no
   COMPLEX EIGENVECTOR tables. With the recovery re-executed, SDR1
   stopped in MERGE (SFM 3007): `EQUIV GO,GOD/NOUE/GM,GMD/NOUE` (bits
   without 34-40) had been skipped as well, and GMD is not on an RF3
   tape. Every statement from `LABEL VDR` to `LABEL FINIS`, that EQUIV
   and the PFILE PARAM now carry 34-40 too.

On the two-subcase test deck the joined print of the restart is the
direct run's to every printed digit: the summaries, the PK modal vectors
and the physical eigenvectors at the marked points, the merged sorted
echo the grid reader reads (`test_nastran95ase_flutter_subcases`,
`decks/two_subcase_modes.dat` holds it). On the monarch the summaries
agree to six significant figures and not beyond (a 30-mode single-Mach
deck: 258 of 1,830 rows differ in the seventh digit, damping
-7.632827E-03 against -7.632829E-03): the modal basis off the tape and
the one a direct run's READ computes in the same job are FEER's answer
to the last bit, not to the last printed digit, and the flutter
solution follows them there. Nothing a crossing can see. A run with a marked point restarted off
a checkpoint whose modes run had `DISP` output once stopped in MERGE
(SFM 3007) - the repository's modes deck prints the eigenvectors at the
elastic axis nodes and the flutter restarts have run clean since; kept
here as the place to look if it comes back.

4. **The in-memory database.** With the modes off the tape and the
   restart deck's cards right, every monarch child still died at
   flutter loop 23, whatever the Mach and whatever the open core (the
   64M and the 256M builds, a 16M-word database): `BLOCK NUMBERS
   INCONSISTANT ON OPEN IN DBMMGR`, unit 30 = BXHH, the FCB at block 13
   and the in-memory chain at 14, then GINO's `I/O ERROR # 0` and the
   buffer dump. BXHH is CLAMA in the PK path, the eigenvalue file FA1
   appends every loop (`fa1pke.f`: close without rewind, reopen for
   write without rewind), and NASA's 1994 in-memory database loses the
   count of such a file's blocks once the database is full - which the
   aerodynamic matrices make certain, restart or not; the direct run
   fills it at a different moment and happens to get through. The
   same child with `DBMEM=0` ran to its 120 summaries. So a flutter
   run - AERO 10 out of the translation, or a child of the driver -
   now runs with no in-memory database and the whole of open core for
   the modules (`bin/nastrn.f.in`; `DBMEM` in the environment turns it
   back on). The 30-mode single-Mach monarch deck restarted clean
   before that change: the failure needs the file to reach the
   database's edge.

One more verdict came out
of it: a child that ended in GINO's `I/O ERROR # 0 ON FILE ... NAME=BXHH`
(a buffer dump, the database directory, END OF JOB) had exit code 0 and
no summary, so `I/O ERROR #` and a bare `ERRTRC CALLED` now count as
fatal in `mds/hexit.f`.

And open core: the flutter runs, the studies and the CI had been given
`OCMEM=256000000` against a 64,000,000-word build, which was `MESAGE
-61`, `END OF JOB` and exit code 0 - nothing solved, nothing said, and
the run looked done. The build's open core is 256,000,000 words (1 GB,
`NASTRAN_OPEN_CORE_WORDS`; a static array below 2 GB with the fixed
image base, costing nothing until used), the ASE split three quarters to
the modules, and an `OCMEM` above the total now says so on standard
error and takes the default split.

### What the g-method would take

ZAERO's g-method (Theoretical Manual 7.3; Chen, "Damping perturbation method
for flutter solution: the g-method", AIAA J. 38(9), 2000) keeps the P-K
equation's form but replaces the aerodynamic damping term Q_I/k with the
derivative of the aerodynamic matrix along the imaginary axis, Q'(ik) =
dQ/d(ik), which the Cauchy-Riemann conditions make equal to dQ/dg for an
analytic Q(p). The flutter equation becomes a quadratic eigenproblem in the
damping g (7.28): [g^2 A + g B + C]{q} = 0 with A = (V/L)^2 M, B = 2ik(V/L)^2 M
- (rho V^2/2) Q'(ik) + (V/L) Z, C = -k^2 (V/L)^2 M + K - (rho V^2/2) Q(ik) +
ik (V/L) Z, solved as a state-space eigenproblem [D - gI]{X} = 0 and swept in
k from 0 to k_max; a root is a flutter root where Im(g) crosses zero, found
by interpolation in k, with f = kV/(2 pi L) and the damping 2 Re(g)/k. A
predictor-corrector on the eigenvalues (dg/dk from left and right
eigenvectors, the step cut when the prediction misses) keeps the tracking
honest.

In NASTRAN-95 terms, all of it lives in FA1's PK branch:

1. `Q'(ik)` per (Mach, k): central differences of the interpolated QHH over
   the MKAERO1 k list (forward at k = 0), one more matrix per k alongside
   QHHL - the interpolation FA1 already does (linear, `IMETH L`) supplies
   Q(ik) at any k; its derivative is a second interpolation.
2. A quadratic eigenproblem instead of PK's fixed-point iteration: linearise
   to 2h x 2h and call the complex eigensolver the K method already uses
   (`CEAD`, HESS) once per k of the sweep, not once per PK iteration - so
   the DMAP loop structure (FA1 -> CEAD -> FA2 -> loop) fits as it is, with
   the loop counter walking k instead of the FLFACT entries and FA2 given
   the roots with Im(g) crossings marked.
3. FA2: a summary per root of the sweep (k, V, g, f) in the MSC layout,
   which the readers already parse; the ZAERO-style "extra" aerodynamic lag
   roots appear naturally and need labelling.
4. The card: a sixth FLUTTER method name (`G`), the sweep step as EPS or a
   new field, the matched-point lists as for PKNL.

The order of work is 1 then 2: without Q'(ik) the equation is PK's, and the
sweep with a proper eigensolver is where the P-K fixed-point iteration's
occasional wrong root goes away. Not started.

## Linux, and the flutter run in a minute and a quarter

Branch `halo-ase-sol145-linux`, on `halo-ase-sol145`. Two things: the branch builds
and runs on Linux (and, untried, macOS), and a SOL 145 run on it is sixteen times
faster - with the printed answer unchanged to the last line, except where the
OpenBLAS solve (below) rounds a marginal root differently. The Windows build pins
`halo-ase-sol145` until someone rebuilds from here; every change below is either
behind `#ifdef _WIN32` or portable Fortran and CMake, so it can.

The case everything was measured on is VehicleDesign's
`monarch_demo_asm1083_flutter.dat`: five Machs, 120 modes, 60 matched points each,
2,282 doublet lattice boxes, 24 to 48 reduced frequencies per Mach, on a 16-core,
32-thread Linux machine. This branch's source with none of the speed-ups: 19 min
50 s. With them: 74 s. Jon's Windows run of the same deck (`fcd681b`, 16 cores):
25.6 min.

### The POSIX port

What VehicleDesign carried as `NASTRAN/build/linux/posix-port.patch` from
2026-09-20 (against `1dcf4cf`), now in the tree: `msc/mscmain.c`'s own path
(`/proc/self/exe`, `_NSGetExecutablePath`), `realpath`, `mkdir`/`chdir`;
`msc/mscopt2.c`'s child runs by `fork`/`execv`/`waitpid` (arguments not quoted:
nothing re-parses them); `msc/mscop4.c`'s `setenv` for `_putenv`; the path separator
substituted into `bin/nastrn.f.in` by CMake; `-fno-pie`/`-no-pie` on ELF (open core
has to sit below 2 GB for `LOCFX`); the trailing `0x1A` removed from six NASA files.

`msc/mscflut.c`, the SOL 145 driver, was Windows-only (`_spawnl`,
`WaitForMultipleObjects`, `GetSystemInfo`). On POSIX a child is `fork` + `execv`,
the driver waits with `waitpid` for whichever finishes, the processor count is the
process's CPU set (`sched_getaffinity`), and each child asks for `SIGTERM` when the
driver dies (`PR_SET_PDEATHSIG`). A child ended by a signal, and on Windows a
crashed child's negative NTSTATUS, count as fatal.

### Where the time was

`<stem>.log`'s CPU column is system time, so the profile came from the module each
log was in, sampled against the wall clock, a `/proc` sampler of the main thread's
user and system time, and a gprof build (which credits OpenMP's outlined functions to
the preceding global symbol and does not see kernel time). For the Mach 0.10 child:
modes 6 s, AMG (the doublet lattice) and AMP (the aerodynamic solve and
products) 190 s between them, **FA1 16.5 min**. On a
120-mode basis FA1 is the PK iteration's 240-square Hessenberg QR per root per
iteration, 54 ms each at -O0, about 320 of them per matched point.

### What was done

Every change is fenced: new code, or NASA's code transcribed with the same
arithmetic in the same order, compiled `-O2`/`-O3` with `-ffp-contract=off` (no
fused multiply-add can change a bit) and never `-ffast-math`. Every one was checked
by diffing print files line by line against this branch's source without it (only
the clock and date lines excluded) at several thread counts.

* **PK loops side by side** (`mis/fa1pkp.f`, hook in `mis/fa1pke.f`). FA1PKE solves
  each flutter loop from k = 0 with nothing carried over, so on the first loop of a
  Mach group FA1PKP solves them all, an OpenMP thread per loop (FA1PKL: FA1PKE's
  labels 100 to 300 transcribed onto arrays of their own), and keeps what each loop
  would have written; FA1PKE then writes loop by loop, in order (FA1PKR): the PK
  eigenvectors and the non-convergence warnings printed, scratch 301 and 302, the
  roots. The root tracker (FA1PKU) stays serial. `FA1PKV` and `FA1PKT` are split into
  their compute and write halves (`FA1PKW`, `FA1PKX`) for the replay. DIAG 39,
  `N95_PK_THREADS=1` and a basis of fewer than four modes keep the serial solve.
* **The QR without aliasing** (`mis/fa1pkq.f`: FA1PKQ = FA1PKA + HSBG + ATEIG, FA1PKG
  = GMMATS's square product; `-O3 -funroll-loops`, 7.6 ms per QR). The originals pass
  one array as both a REAL input and a DOUBLE PRECISION work area. **ATEIG reads one
  double outside its matrix**: its search for a small subdiagonal element (620-660)
  steps past (2,1) to `A(1-IA)`, and whether that double is below EPS decides whether
  the QR sweep starts at row 1 or 2, which moves the roots in the sixth digit. In
  FA1PKE the address is the top of M^-1 B. The twin keeps the read, and FA1PKP lays
  each thread's arrays out as open core is so that the same bytes are there (a
  separate allocation gave different roots, or a segmentation fault).
* **Doublet lattice rows side by side** (`mis/gendp.f`, hook in `mis/gend.f`). GEND's
  rows are computed a block at a time on threads and packed in its order. The row
  routines (`dpps`, `subp`, `snpdf`, `incro`, `tker`, `idf1`, `idf2`) are `-O2
  -fautomatic` (no DATA, no SAVE, no local read before it is set in a call - the
  maybe-uninitialized warnings on TKER's computed GO TOs were traced path by path),
  and `/DLM/` and `/KDS/` are THREADPRIVATE in all four routines that name them (TKER,
  INCRO, FLLD, SUBB). `N95_DLM_THREADS=1` keeps GEND's loop.
* **AMP without k-squared file walks** (`mis/ampc.f`, `mis/ampd.f`, `/AMPHLO/`). Per
  (Mach, k) pair AMPC rewound AJJL and skipped AJJCOL-1 columns, and AMPD the same on
  SKJ: 2.6 million GINO records each over the 48 reduced frequencies of Mach 0.10, 31
  s of system time. When the last pair left the file at the pair's first column, it is
  reopened where it stands.
* **The in-core solve through LAPACK** (`mis/ampczs/`). AMPCZ's solve is AMPCZS:
  ZGETRF + ZGETRS when CMake is given a LAPACK (`NASTRAN_LAPACK_LIBRARIES`; the Linux
  build links OpenBLAS 0.3.34 statically, DYNAMIC_ARCH, OpenMP), the built-in LU
  (AMPCZB, the loops AMPCZ1 had) otherwise, and with `N95_AJJ_SOLVE=BUILTIN`.
  **OpenBLAS in a static executable runs on one thread** unless told otherwise: its
  OpenMP build records its thread ceiling in its own constructor, which runs before
  libgomp has read `OMP_NUM_THREADS`; `ampczt_openblas.f` sets the count
  (`N95_BLAS_THREADS`, else the OpenMP count). This is the one change that is not bit
  for bit: a blocked LU rounds differently in double precision, and where that flips a
  single-precision bit of QJH a marginal PK root (one that ends in the least-squares
  fit, or two roots the tracker could pair either way) lands elsewhere - 108 of the
  monarch print's 170,034 lines.
* **The gust path in core too** (`mis/ampf.f`, `AMPCZN` in `mis/ampcz.f`). SOL 146's
  AMPF solved RJH = AJJ^-1 S(K) per (Mach, k) through CFACTR + CFBSOR, the path AMPC
  left, after the same k-squared walks; it now takes the in-core solve without the
  transpose and keeps its file positions. The monarch PSD deck reaches data recovery
  in 75 s instead of hours - and stops there, in SDR2 (SFM 3001, its ELFORCE request)
  or in RAND2 (SFM 3002, without it): the SOL 146 front end's open end. NASA's AERO 11
  demos d11031a and d11032a agree with the out-of-core path to 1.4e-4 and 1e-5 of the
  largest value on each line, and d11031a is nearer NASA's 1995 print than before.
* **MMA104's inner loops** (`mis/mma10k.f`) at -O2, called from MMA104, which passes
  one open-core array as three dummies and stays at -O0.
* **The driver shares the processors out by kind of work** (`msc/mscflut.c`). Each
  child gets every processor for the coarse work (the rows, the PK loops) and waits
  passively, and the LU gets the even share: the children finish at different times,
  and an even split left the finished children's processors idle. 102 s with the even
  split, 87 s this way, 400 s with spinning waits. A caller's `OMP_NUM_THREADS` is
  taken as it is.

| step | five-Mach deck | print against the unmodified source |
|---|---|---|
| unmodified | 19 min 50 s | - |
| PK loops threaded, QR at -O2, rows threaded | 3 min 34 s | identical |
| + OpenBLAS | 2 min 13 s | 108 lines |
| + the file walks, OpenBLAS told its threads | 1 min 42 s | 108 lines |
| + the driver's thread policy | 87 s | 108 lines |
| + MMA104's loops, the QR at -O3 | 74 s | 108 lines |
| the same, `N95_AJJ_SOLVE=BUILTIN` | 200 s | identical |

### The second pass: 74 s to 40 s, the same bits

The same deck and machine. At 74 s the profile (a CPU sampler per child per module,
gprof with `-g` for lines) said: of 1,720 CPU seconds, FA1 1,140, AMG 327, AMP 200;
and the wall clock was the Mach 0.10 child's AMP, 41 s nearly serial on its main
thread. Everything below is again new code or NASA's code transcribed with the same
arithmetic in the same order; each was checked against the build before it, and the
whole against `halo-ase-sol145` 8cd363e.

* **EGNVCT's pivot searches** (`mis/egnvct.f`). The complete-pivoting elimination
  took CABS of every element of the active block at every step: four billion
  `hypotf` calls per Mach, a fifth of FA1. The search keeps the first element whose
  CABS exceeds all before it, so an element whose squared modulus (in double
  precision: exact but for one rounding) is below `(X1*(1-2**-20))**2` provably
  cannot be picked and is skipped; every other one is tested as NASA wrote it. 7.97
  -> 1.07 ms per call.
* **AMP's pair loop in core** (`mis/ampk.f`). A pipeline of OpenMP tasks: per pair a
  read (its AJJ and SKJ off the files, kept open), a compute (DJH, the solve, QKH =
  SKJ QJH, QIH = GKI(T) QKH) and a write (QJHL, QHHL); every GINO call is in a read
  or write task, chained on one dependence in pair order. The compute replicates
  SADD's two multiply-adds and MMA214's and MMA104's sums in their order; the serial
  path skips terms with an unstored (zero) factor and this keeps them, and a zero of
  either sign added to a sum that started at +0 changes nothing. Up to 16 pairs in
  core (`N95_AMP_SLOTS`, `N95_AMP_MB`; the SOL 145 driver gives each child half the
  memory available shared among the children, between 512 MB and 2 GB), each solve
  on one thread: OpenBLAS's ZGETRF gives the same bits on 1 thread as on 32.
  `N95_AMP_PIPE=0` keeps AMP's loop.
* **AMG's pairs in batches** (`mis/amgk.f`, `mis/tkerv.f`). Per (Mach, k) pair AMG
  re-read the group's record, re-wrote the same SKJ and recomputed every element; of
  that only the kernels and what is linear in them depend on k. For one
  doublet-lattice group, up to 8 pairs of one Mach (`N95_AMG_BATCH`) are done
  together: the steady part (SNPDF), the geometry, TKER's branches, square roots and
  exponential, IDF1/IDF2's logarithm and arctangent once; every k-dependent
  statement a loop over the batch (TKERV, INCROK, SUBPK, IDF1V, IDF2V, DPPSK); each
  pair's rows packed in turn. AJJ dumped as AMP reads it is byte for byte the serial
  NASA loop's. **gfortran vectorises SIN and COS in such loops into glibc's
  libmvec** (`_ZGVdN8v_sinf`): it pre-includes `math-vector-fortran.h`, even without
  `-ffast-math`, and libmvec rounds differently - 194,796 of 200,000 test geometries
  differed until the loops that call them were marked `!GCC$ NOVECTOR`. (Two of the
  kernel files compiled -O2 since `halo-ase`, `amgb1b.f` and `amgb1c.f`, the
  compressor-blade theory, do call libmvec; the doublet lattice does not.)
* **FA1PKG with its loops swapped** (`mis/fa1pkq.f`): J innermost instead of K, every
  C(I,J) the same sum in the same order, the inner loop contiguous. 449 -> 80 us.
  And HSBG's loop 300 (row L times each column, a long dependent sum per column)
  eight columns at a time: eight independent sums in their own orders, 12 % off the
  QR.
* **The kernels twice, for x86-64-v3** (`msc/mscisa.c`, CMake). `fa1pkq.f`,
  `egnvct.f` and `tkerv.f` are compiled for the baseline and, under V3 names CMake
  makes from the same source, with `-march=x86-64-v3`; each hands over to its V3
  build when `__builtin_cpu_supports("x86-64-v3")` (`N95_ISA=0` keeps the baseline).
  `-ffp-contract=off` and no `-ffast-math`: wider vectors, the same bits. FA1's CPU
  842 -> 707 s. (`x86-64-v4`/AVX-512 was slower on the QR than v3.)
* **GINO's PACK and UNPACK** (`mds/pack.f`, `mds/unpack.f`, `mds/n95fast.f`). A run
  of elements that needs no conversion and lies word after word is copied in one
  call, with the loop's own bookkeeping after it.
* **GP4's MPC look-up by bisection** (`mis/gp4.f`): the linear search of the sorted,
  repeat-free list of dependent SILs for every MPC term was 1.6 s of every child's
  setup.
* **The setup's inner loops** (`mis/n95twin.f`): FERXTD's reorthogonalisation,
  DECOMP's loop 810 and MMA112's inner product as optimised copies of themselves.

| step | five-Mach deck |
|---|---|
| the first pass | 74 s |
| + EGNVCT, AMPK | 56 s |
| + AMGK | 49 s |
| + FA1PKG | 47 s |
| + x86-64-v3 kernels | 44 s |
| + GINO, GP4, the setup loops | 40 s |

The Mach 0.10 child alone: 40.6 -> 17.2 s. **With `N95_AJJ_SOLVE=BUILTIN` the
five-Mach print is `halo-ase-sol145` 8cd363e's, line for line (170,034 lines)**;
with OpenBLAS it differs from it in the same 108 lines as before. NASA's 132
demonstration decks print as before, but for d01002a, which prints the GINO timing
constants it measures, and d07022a, which is nondeterministic in the unmodified
solver too (in 40 runs each of this build, of 8cd363e and of fcd681b, 1 to 3 take
10 complex decompositions instead of 13).

Tried and dropped: the whole tree at `-O2 -fno-aggressive-loop-optimizations
-fno-strict-aliasing -fwrapv` (124 of 132 demos differ, 51 segfault - gfortran
treats dummy arguments as not overlapping at -O1 and above, and open core is
nothing but overlapping dummies); ATEIG's far updates deferred and applied a block
of steps at a time (the same bits, slower: the 240-square QR lives in L2 and is
bound by its dependences, not memory); a larger GINO buffer (SYSBUF) (FEER's modes
move with it, and it saved nothing); lowering the FA1 children's priority, or the
shorter Machs' (the long child reaches FA1 sooner and the run ends no sooner: what
is left is CPU, and a low-priority thread still shares its core's other half).

### halo-ase-sol145's checkpoint and restart, on Linux

Merged from `halo-ase-sol145` 8cd363e. The POSIX side of it: the problem tape goes
into the output directory by `link(2)`, a copy when that fails (`CreateHardLink` /
`CopyFile`); the directory test is `stat`, `_stricmp` is `strcasecmp`, `_fullpath`
is `msc_abs_path`; the restarted children get `optp=../optp.nptp` in their argv.
The direct flutter run is unchanged by the merge line for line (the flutter runs now
go without the in-memory database; 73.5 s), and the restart flow - the modes once
with `scr=no`, the flutter deck off them - takes 72 s. The two agree to six
significant figures, as on Windows, and the speed-ups reproduce each exactly (the
Mach 0.40 child, every threaded path off against on, restarted and not). But the
marginal PK roots at the slowest points land differently between the two, and on
the monarch deck that moves the lowest crossing at Mach 0.30 and 0.40 (4.8 and 7.9
m/s at 9.8 Hz off the restart, 20.1 and 19.8 m/s at 10.8 Hz direct): a root hovering
about g = 0.005, the crossing threshold, at the lowest densities.

### What is still serial

FA1 is now seven tenths of the CPU and nearly all of it NASA's 240-square QR (HSBG +
ATEIG, 4-5 ms per iteration): the order of its operations is the answer, and it is
bound by its own dependences. A faster QR (LAPACK's DHSEQR) would not be NASA's
roots. Each child's setup (the modes, the constraints, the spline) is about 6 s of
serial -O0 before AMG, and with five children in it at once most of the machine
waits. Under load the Mach 0.10 child's AMP takes 11-12 s against 3.4 s alone: its
GINO read chain is serial and shares the machine with the other children's FA1. A
marked loop's output is OFP formatting, serial.
