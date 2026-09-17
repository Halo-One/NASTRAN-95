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
all are. The diff against NASA's tree is ten files. Every source change is
tagged `C HALO:` in place with the reason.

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

## Licence

NASA Open Source Agreement 1.3, unchanged. See `NASA Open Source
Agreement-NASTRAN 95.doc`. It permits modification and internal use; if a
modified solver is ever distributed it must go out under NOSA with the
modifications identified, which is what this file is for.
