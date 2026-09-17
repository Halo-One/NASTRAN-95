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
