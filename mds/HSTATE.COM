C HALO: State shared between the standalone start-up in bin/nastrn.f.in,
C HALO:   the exit handler in mds/hexit.f and the tidy-up in mds/hclean.f.
C HALO:
C HALO:   HMODE   0  deck on standard input, print file on standard
C HALO:              output: NASA's original contract
C HALO:           1  deck named on the command line; the print file is
C HALO:              HPRTF and the other outputs are HSTEM.log and so
C HALO:              on, all in the current directory, which is HOUTD
C HALO:   HDIRMK  1  the scratch directory DIRTRY was created by this
C HALO:              run and is removed on the way out
C HALO:   HRFMK   1  the rigid format library was written into DIRTRY
C HALO:              from the copy built into the executable, and is
C HALO:              removed on the way out
C HALO:
C HALO:   Two blocks because a COMMON that mixes CHARACTER with numeric
C HALO:   storage is a standard violation some compilers refuse.
      COMMON / HSTATI / HMODE, HDIRMK, HRFMK
      COMMON / HSTATC / HPRTF, HSTEM, HOUTD
      INTEGER           HMODE, HDIRMK, HRFMK
      CHARACTER*256     HPRTF, HSTEM, HOUTD
