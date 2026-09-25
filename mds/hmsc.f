C HALO: the Fortran side of the MSC dialect front end.
C HALO:
C HALO:   The translation itself is C (msc/), because it is string
C HALO:   handling and tables and nothing else, and because a CHARACTER
C HALO:   in this dialect of Fortran is a fixed-length blank-padded
C HALO:   thing that fights every step of reading a free-field deck.
C HALO:   This file is the interface: NUL-terminate, call, return a
C HALO:   status.
      SUBROUTINE HMSCXL ( DECK, XLAT, MSGF, IRF, NMODES, IERR )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCRN ( A, B, C, RF, ND )
     &                           BIND(C, NAME='msc_run')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: A, B, C
         INTEGER(C_INT), INTENT(OUT) :: RF, ND
         END FUNCTION
      END INTERFACE
      CHARACTER*(*)   DECK, XLAT, MSGF
      INTEGER         IRF, NMODES, IERR
      INTEGER(C_INT)  IRC, IRF4, IND4
      IRC = CMSCRN ( DECK(1:LEN_TRIM(DECK)) // C_NULL_CHAR,
     &               XLAT(1:LEN_TRIM(XLAT)) // C_NULL_CHAR,
     &               MSGF(1:LEN_TRIM(MSGF)) // C_NULL_CHAR,
     &               IRF4, IND4 )
      IERR   = IRC
      IRF    = IRF4
      NMODES = IND4
      RETURN
      END
C HALO: SOL 200: the design cycle, which runs the solver as child
C HALO:   processes of this executable (each with --cosmic) and never
C HALO:   returns to the solver in this process.
      SUBROUTINE HMSCOP ( DECK, OUTD, STEM, IERR )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCOP ( A, B, C )
     &                           BIND(C, NAME='msc_sol200')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: A, B, C
         END FUNCTION
      END INTERFACE
      CHARACTER*(*)   DECK, OUTD, STEM
      INTEGER         IERR
      IERR = CMSCOP ( DECK(1:LEN_TRIM(DECK)) // C_NULL_CHAR,
     &                OUTD(1:LEN_TRIM(OUTD)) // C_NULL_CHAR,
     &                STEM(1:LEN_TRIM(STEM)) // C_NULL_CHAR )
      RETURN
      END
C HALO: SOL 145 with several subcases: the flutter driver, which runs
C HALO:   one child process per subcase (each with --cosmic) side by
C HALO:   side and joins their print files; it never returns to the
C HALO:   solver in this process.
      SUBROUTINE HMSCFL ( DECK, OUTD, STEM, IERR )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCFL ( A, B, C )
     &                           BIND(C, NAME='msc_sol145')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: A, B, C
         END FUNCTION
      END INTERFACE
      CHARACTER*(*)   DECK, OUTD, STEM
      INTEGER         IERR
      IERR = CMSCFL ( DECK(1:LEN_TRIM(DECK)) // C_NULL_CHAR,
     &                OUTD(1:LEN_TRIM(OUTD)) // C_NULL_CHAR,
     &                STEM(1:LEN_TRIM(STEM)) // C_NULL_CHAR )
      RETURN
      END
C HALO: on a fatal, repeat the message on the terminal with what it
C HALO:   means and what to do, from msc/mscdiag.c. Both executables.
      SUBROUTINE HMSCDG ( PRTF, IFND )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCDG ( A ) BIND(C, NAME='msc_diag')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: A
         END FUNCTION
      END INTERFACE
      CHARACTER*(*)   PRTF
      INTEGER         IFND
      IFND = CMSCDG ( PRTF(1:LEN_TRIM(PRTF)) // C_NULL_CHAR )
      RETURN
      END
C HALO: checkpoint and restart through the front end (msc/mscxlat.c):
C HALO:   HMSCCK turns the CHKPNT card on for the translated deck, HMSCRS
C HALO:   names the modes run to restart from and gives back the old
C HALO:   problem tape's path
      SUBROUTINE HMSCCK ( ION )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         SUBROUTINE CMSCCK ( ON ) BIND(C, NAME='msc_chkpnt_set')
         IMPORT :: C_INT
         INTEGER(C_INT), VALUE :: ON
         END SUBROUTINE
      END INTERFACE
      INTEGER         ION
      CALL CMSCCK ( INT ( ION, C_INT ) )
      RETURN
      END
      SUBROUTINE HMSCLK ( OUTD, IERR )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCLK ( A )
     &                           BIND(C, NAME='msc_restart_link')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: A
         END FUNCTION
      END INTERFACE
      CHARACTER*(*)   OUTD
      INTEGER         IERR
      IERR = CMSCLK ( OUTD(1:LEN_TRIM(OUTD)) // C_NULL_CHAR )
      RETURN
      END
      SUBROUTINE HMSCRS ( MDECK, OPTP, IERR )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCRS ( A, B )
     &                           BIND(C, NAME='msc_restart_set')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: A, B
         END FUNCTION
      END INTERFACE
      CHARACTER*(*)   MDECK, OPTP
      INTEGER         IERR
      IERR = CMSCRS ( MDECK(1:LEN_TRIM(MDECK)) // C_NULL_CHAR,
     &                OPTP(1:LEN_TRIM(OPTP)) // C_NULL_CHAR )
      RETURN
      END
C HALO: the results side: rewrite the print file into the layout MSC
C HALO:   prints, so that a reader written against MSC output reads
C HALO:   this one. See msc/mscf06.c for what is rewritten and why.
      SUBROUTINE HMSCF6 ( PRTF, IERR )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCF6 ( A ) BIND(C, NAME='msc_f06')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: A
         END FUNCTION
      END INTERFACE
      CHARACTER*(*)   PRTF
      INTEGER         IERR
      IERR = CMSCF6 ( PRTF(1:LEN_TRIM(PRTF)) // C_NULL_CHAR )
      RETURN
      END
C HALO: the wall-clock watchdog: a second thread that ends the process
C HALO:   after WDMIN minutes with NOTE in its message. Both executables;
C HALO:   see msc/mscwatch.c for why a thread and why _exit.
      SUBROUTINE HMSCWD ( WDMIN, NOTE )
      USE ISO_C_BINDING
      IMPLICIT NONE
      INTERFACE
         INTEGER(C_INT) FUNCTION CMSCWD ( M, N )
     &                           BIND(C, NAME='msc_watchdog')
         IMPORT :: C_INT, C_CHAR, C_DOUBLE
         REAL(C_DOUBLE), VALUE :: M
         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: N
         END FUNCTION
      END INTERFACE
      REAL            WDMIN
      CHARACTER*(*)   NOTE
      INTEGER(C_INT)  IRC
      IRC = CMSCWD ( REAL ( WDMIN, C_DOUBLE ),
     &               NOTE(1:LEN_TRIM(NOTE)) // C_NULL_CHAR )
      RETURN
      END
