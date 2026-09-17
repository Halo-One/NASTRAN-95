C HALO: The operating-system calls the standalone start-up needs, for
C HALO:   Windows. mds/hosunx.f is the same five routines for everything
C HALO:   else; CMakeLists.txt compiles one of the two.
C HALO:
C HALO:   All five go through the C runtime the executable is already
C HALO:   linked against (msvcrt.dll here), by way of BIND(C), so no
C HALO:   compiler-specific intrinsic and no extra library is involved.
C HALO:   A path is passed with a NUL appended, which is all C wants.
      SUBROUTINE HMKDIR ( PATH, IERR )
C HALO: create directory PATH. IERR is 0 on success and non-zero when
C HALO:   it exists already or cannot be made.
      USE ISO_C_BINDING
      CHARACTER*(*) PATH
      INTEGER       IERR
      INTERFACE
         INTEGER(C_INT) FUNCTION CMKDIR ( P ) BIND(C, NAME='_mkdir')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR) :: P(*)
         END FUNCTION
      END INTERFACE
      IERR = CMKDIR ( PATH(1:LEN_TRIM(PATH)) // C_NULL_CHAR )
      RETURN
      END
      SUBROUTINE HRMDIR ( PATH, IERR )
C HALO: remove directory PATH, which must be empty. IERR as above.
      USE ISO_C_BINDING
      CHARACTER*(*) PATH
      INTEGER       IERR
      INTERFACE
         INTEGER(C_INT) FUNCTION CRMDIR ( P ) BIND(C, NAME='_rmdir')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR) :: P(*)
         END FUNCTION
      END INTERFACE
      IERR = CRMDIR ( PATH(1:LEN_TRIM(PATH)) // C_NULL_CHAR )
      RETURN
      END
      SUBROUTINE HCHDIR ( PATH, IERR )
C HALO: make PATH the current directory. IERR as above.
      USE ISO_C_BINDING
      CHARACTER*(*) PATH
      INTEGER       IERR
      INTERFACE
         INTEGER(C_INT) FUNCTION CCHDIR ( P ) BIND(C, NAME='_chdir')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR) :: P(*)
         END FUNCTION
      END INTERFACE
      IERR = CCHDIR ( PATH(1:LEN_TRIM(PATH)) // C_NULL_CHAR )
      RETURN
      END
      SUBROUTINE HMSG ( TEXT )
C HALO: write TEXT and a newline to standard error through the C runtime
C HALO:   rather than Fortran unit 0. On some fatal paths the solver has
C HALO:   CLOSEd unit 0 by the time the exit handler runs (a CLOSE on a
C HALO:   unit variable that happens to be zero), and a Fortran WRITE to
C HALO:   a closed unit 0 invents a file called fort.0 in the output
C HALO:   directory and loses the message in it.
      USE ISO_C_BINDING
      CHARACTER*(*) TEXT
      INTERFACE
         INTEGER(C_INT) FUNCTION CWRITE ( FD, BUF, N )
     &      BIND(C, NAME='_write')
         IMPORT :: C_INT, C_CHAR
         INTEGER(C_INT), VALUE  :: FD
         CHARACTER(KIND=C_CHAR) :: BUF(*)
         INTEGER(C_INT), VALUE  :: N
         END FUNCTION
      END INTERFACE
      INTEGER(C_INT) IRC
      INTEGER        LT
      LT  = LEN_TRIM ( TEXT )
      IRC = CWRITE ( INT ( 2, C_INT ),
     &               TEXT(1:LT) // CHAR(13) // CHAR(10),
     &               INT ( LT + 2, C_INT ) )
      RETURN
      END
      SUBROUTINE HTMPDR ( BASE )
C HALO: where scratch directories go. TEMP, then TMP, when it is short
C HALO:   enough to leave room for '/n95_<pid>_99/scr90' inside the
C HALO:   CHARACTER*72 that holds the scratch directory; otherwise a
C HALO:   folder off the root of the system drive, which the caller
C HALO:   creates. Fortran truncates an over-long assignment silently,
C HALO:   so the length is decided here rather than discovered later
C HALO:   as a scratch file that cannot be opened.
      CHARACTER*(*) BASE
      CHARACTER*256 V
      INTEGER       LV
      V = ' '
      CALL GETENV ( 'TEMP', V )
      IF ( V .EQ. ' ' ) CALL GETENV ( 'TMP', V )
      LV = LEN_TRIM ( V )
      IF ( LV .GT. 1 .AND. ( V(LV:LV) .EQ. CHAR(92) .OR.
     &                       V(LV:LV) .EQ. '/' ) ) LV = LV - 1
      IF ( LV .GT. 0 .AND. LV .LE. 50 ) THEN
         BASE = V(1:LV)
         RETURN
      ENDIF
      V = ' '
      CALL GETENV ( 'SystemDrive', V )
      IF ( V .EQ. ' ' ) V = 'C:'
      BASE = V(1:LEN_TRIM(V)) // CHAR(92) // 'n95tmp'
      RETURN
      END
