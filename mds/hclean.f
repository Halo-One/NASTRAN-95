      SUBROUTINE HCLEAN
C HALO: Delete the files a finished run has no further use for.
C HALO:
C HALO:   NASTRAN itself never tidied up. NASA's bin/nastran csh wrapper made
C HALO:   a scratch directory, ran the solver, and then removed the directory
C HALO:   and a file called `none`. That wrapper cannot be run -- it has one
C HALO:   user's home directory hard-coded in it -- and a solver that can be
C HALO:   handed to someone as a single executable has to tidy up after
C HALO:   itself, or it leaves up to ninety scr* files and a mystery file
C HALO:   called `none` in whatever directory it was run in.
C HALO:
C HALO:   `none` is the string that means "this run does not want that file".
C HALO:   NASTRAN tests for it before some OPENs and not others, so a run
C HALO:   with the default settings really does create a file of that name.
C HALO:
C HALO:   Called from PEXIT, which is where the program actually ends: PEXIT
C HALO:   finishes with CALL EXIT(0), so anything after CALL XSEM00 in the
C HALO:   main program is unreachable. It also closes units 1-4 and 7-22
C HALO:   first, which matters -- Windows will not delete a file that is
C HALO:   still open, and the failure is silent.
C HALO:
C HALO:   The scratch names are rebuilt here rather than read out of DSNAMES.
C HALO:   DSNAMES starts out holding all ninety scratch names and then has
C HALO:   the real output names written over eleven of its entries, so a loop
C HALO:   over DSNAMES(1..90) would delete the results along with the
C HALO:   scratch. Two jobs sharing one DIRCTY would also delete each
C HALO:   other's scratch; give them separate directories, which is what the
C HALO:   Python runner does.
      CHARACTER*5     TMP
      CHARACTER*80    SCRNAM
      LOGICAL         THERE
      INTEGER         K, LD
      INCLUDE 'NASNAMES.COM'
      LD = INDEX ( DIRTRY, ' ' ) - 1
      IF ( LD .LE. 0 ) LD = 1
      DO 20 K = 1, 90
         IF ( K .LE. 9 ) WRITE ( TMP, 11 ) K
         IF ( K .GT. 9 ) WRITE ( TMP, 12 ) K
11       FORMAT ('scr',I1)
12       FORMAT ('scr',I2)
         SCRNAM = DIRTRY(1:LD)//'/'//TMP
         OPEN ( 98, FILE = SCRNAM, STATUS = 'OLD', ERR = 20 )
         CLOSE ( 98, STATUS = 'DELETE', ERR = 20 )
20    CONTINUE
      INQUIRE ( FILE = 'none', EXIST = THERE )
      IF ( .NOT. THERE ) GO TO 30
      OPEN ( 98, FILE = 'none', STATUS = 'OLD', ERR = 30 )
      CLOSE ( 98, STATUS = 'DELETE', ERR = 30 )
30    CONTINUE
      RETURN
      END
