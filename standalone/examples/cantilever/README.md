# A worked example for nastran95

Two small decks in the 1970s COSMIC dialect, together with the print files a build of
this fork produced (Linux, 2026-10-01; the Windows `nastran95.exe` printed the same
numbers). Use them to check that an executable works on your machine before trying
anything of your own.

| file | what |
|---|---|
| `cantilever_static.inp` | a ten-element cantilever bar, tip force and torque, statics (`SOL 1,0`) |
| `cantilever_static.out` | the print file from running it |
| `cantilever_modes.inp` | the same bar, first 60 natural modes (`SOL 3,0`) |
| `cantilever_modes.out` | the print file from running it |

## Run it

Build the executables (`../../build/`), or copy `nastran95` (`nastran95.exe` on Windows)
and this folder side by side anywhere, open a terminal in this folder, and:

```
..\..\nastran95 cantilever_static.inp        (Windows, inside the repository)
../../nastran95 cantilever_static.inp        (Linux)
```

You should see exactly one line and get the prompt back in under a second:

```
nastran: cantilever_static -> cantilever_static.out
```

That overwrites `cantilever_static.out` with your run and writes a `cantilever_static.log`
beside it. To keep the committed print file, run into another directory instead:

```
..\..\nastran95 cantilever_static.inp C:\runs      (Windows)
../../nastran95 cantilever_static.inp /tmp/runs    (Linux)
```

Then the same for `cantilever_modes.inp`.

## What to look for

Open the `.out` file in any text editor. It is long because NASTRAN-95 echoes the deck
and prints everything asked for. The results are near the end.

**cantilever_static.out.** Search for `D I S P L A C E M E N T   V E C T O R`. The last
row of that table is grid 11, the tip:

```
POINT ID.  TYPE   T1            T2             T3             R1             R2             R3
   11      G      0.0     1.190476E-03  -4.761905E-03   5.066667E-03   7.142857E-03   1.785714E-03
```

Those three are the closed-form answers: T2 = F_y L³ / 3 E I₂ = 1.190476E-03 m,
T3 = F_z L³ / 3 E I₁ = -4.761905E-03 m, R1 = T L / G J = 5.066667E-03 rad — with
F_y = 5 N and F_z = -10 N, the `FORCE` card's magnitude of 10.0 times its direction
(0, 0.5, -1), which NASTRAN does not normalise, and T = 2 N·m from the `MOMENT` card.
Simcenter Nastran 2606 prints the same digits for the same bulk data run as `SOL 101`.
Also search for `EPSILON`: a healthy static solution has a value of 1e-12 or smaller
there.

**cantilever_modes.out.** Search for `R E A L   E I G E N V A L U E S`. The first two
rows are the first bending modes in the two planes:

```
MODE  EXTRACTION  EIGENVALUE     RADIAN        CYCLIC       GENERALIZED  GENERALIZED
 NO.    ORDER                    FREQUENCY     FREQUENCY    MASS         STIFFNESS
  1       30      6.701677E+03   8.186377E+01  1.302902E+01 1.631553E-01 1.093414E+03
  2       50      1.340407E+04   1.157759E+02  1.842631E+01 1.631498E-01 2.186872E+03
```

13.03 Hz and 18.43 Hz. Each mode shape follows as a `R E A L   E I G E N V E C T O R`
table. Simcenter Nastran 2606 (the same bulk data as `SOL 103`) prints the same
eigenvalues within 6.5e-7 of each, one unit of the last printed digit (mode 3:
2.647985E+05 against 2.647986E+05 here).

**If it went wrong.** The console line says so and the exit code is non-zero:

```
nastran: cantilever_static finished with a FATAL message - see cantilever_static.out
```

Search the `.out` file for `FATAL`. The message number and text are on that line, and
the card that caused it is usually echoed just above. `nastran95 --help` lists the exit
codes; `nastran95 --version` says which build you have.

## Writing your own deck

Start from one of these two and read the deck-format notes in `../../README.md` under
*Running a deck*. The one-sentence version: this is 1970s NASTRAN (`ID`, `APP`,
`SOL 1,0`, `TIME`, fixed 8-column fields, no `INCLUDE`), not the MSC dialect that
Simcenter Nastran reads and that `nastran95ase` translates. NASA's 132 demonstration
decks in the repository's `inp/` folder, with the print each produced in 1995 in
`demoout/`, cover every analysis type the solver has; `../flat_plate/` is the same wing
three ways in the MSC dialect for `nastran95ase`.
