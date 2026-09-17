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
