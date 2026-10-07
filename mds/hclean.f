      SUBROUTINE HCLEAN
C HALO: Delete what a finished run has no further use for.
C HALO:
C HALO:   NASTRAN itself never tidied up. NASA's bin/nastran csh wrapper
C HALO:   made a scratch directory, ran the solver, and then removed the
C HALO:   directory and a file called `none`. That wrapper cannot be run
C HALO:   -- it has one user's home directory hard-coded in it -- and a
C HALO:   solver that can be handed to someone as a single executable
C HALO:   has to tidy up after itself, or it leaves up to ninety scr*
C HALO:   files and a mystery file called `none` behind.
C HALO:
C HALO:   `none` is the string that means "this run does not want that
C HALO:   file". NASTRAN tests for it before some OPENs and not others,
C HALO:   so a run with the default settings really does create a file
C HALO:   of that name, in the current directory.
C HALO:
C HALO:   Called twice on a normal run: from PEXIT, and again from the
C HALO:   exit handler HATEXT (mds/hexit.f) after every unit has been
C HALO:   closed, which is what makes a still-open scratch file
C HALO:   deletable on Windows. It is safe to repeat: a file that is
C HALO:   already gone is skipped, and the directory is removed once.
C HALO:
C HALO:   The scratch names are rebuilt here rather than read out of
C HALO:   DSNAMES. DSNAMES starts out holding all ninety scratch names
C HALO:   and then has the real output names written over eleven of its
C HALO:   entries, so a loop over DSNAMES(1..90) would delete the results
C HALO:   along with the scratch. The rigid format library, when it was
C HALO:   written into the same directory from the copy built into the
C HALO:   executable, goes too; and the directory itself, when this run
C HALO:   created it. A DIRCTY handed in from the environment is left in
C HALO:   place, scratch files deleted, for whoever made it to remove.
      CHARACTER*5     TMP
      INTEGER         K, LD, IERR
      INCLUDE 'NASNAMES.COM'
      INCLUDE 'HSTATE.COM'
      LD = LEN_TRIM ( DIRTRY )
      IF ( LD .LE. 0 ) GO TO 30
      DO 20 K = 1, 90
         IF ( K .LE. 9 ) WRITE ( TMP, 11 ) K
         IF ( K .GT. 9 ) WRITE ( TMP, 12 ) K
11       FORMAT ('scr',I1)
12       FORMAT ('scr',I2)
         CALL HDELF ( DIRTRY(1:LD) // '/' // TMP )
20    CONTINUE
      IF ( HRFMK .NE. 0 ) CALL HRFDEL ( DIRTRY(1:LD) )
      IF ( HDIRMK .EQ. 0 ) GO TO 30
      CALL HRMDIR ( DIRTRY(1:LD), IERR )
      IF ( IERR .EQ. 0 ) HDIRMK = 0
30    CALL HDELF ( 'none' )
      RETURN
      END
      SUBROUTINE HDELF ( NAME )
C HALO: delete file NAME if it exists and is not open; silent otherwise.
      CHARACTER*(*) NAME
      OPEN ( 98, FILE = NAME, STATUS = 'OLD', ERR = 10 )
      CLOSE ( 98, STATUS = 'DELETE', ERR = 10 )
10    RETURN
      END
