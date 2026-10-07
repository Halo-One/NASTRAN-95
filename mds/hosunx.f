C HALO: The operating-system calls the standalone start-up needs, for
C HALO:   everything that is not Windows. mds/hoswin.f is the Windows
C HALO:   version and CMakeLists.txt compiles one of the two. See the
C HALO:   header there; this file is the same four routines against
C HALO:   the POSIX names.
      SUBROUTINE HMKDIR ( PATH, IERR )
      USE ISO_C_BINDING
      CHARACTER*(*) PATH
      INTEGER       IERR
      INTERFACE
         INTEGER(C_INT) FUNCTION CMKDIR ( P, M ) BIND(C, NAME='mkdir')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR) :: P(*)
         INTEGER(C_INT), VALUE  :: M
         END FUNCTION
      END INTERFACE
C HALO: 448 is 0700: the owner's private scratch
      IERR = CMKDIR ( PATH(1:LEN_TRIM(PATH)) // C_NULL_CHAR,
     &                INT ( 448, C_INT ) )
      RETURN
      END
      SUBROUTINE HRMDIR ( PATH, IERR )
      USE ISO_C_BINDING
      CHARACTER*(*) PATH
      INTEGER       IERR
      INTERFACE
         INTEGER(C_INT) FUNCTION CRMDIR ( P ) BIND(C, NAME='rmdir')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR) :: P(*)
         END FUNCTION
      END INTERFACE
      IERR = CRMDIR ( PATH(1:LEN_TRIM(PATH)) // C_NULL_CHAR )
      RETURN
      END
      SUBROUTINE HCHDIR ( PATH, IERR )
      USE ISO_C_BINDING
      CHARACTER*(*) PATH
      INTEGER       IERR
      INTERFACE
         INTEGER(C_INT) FUNCTION CCHDIR ( P ) BIND(C, NAME='chdir')
         IMPORT :: C_INT, C_CHAR
         CHARACTER(KIND=C_CHAR) :: P(*)
         END FUNCTION
      END INTERFACE
      IERR = CCHDIR ( PATH(1:LEN_TRIM(PATH)) // C_NULL_CHAR )
      RETURN
      END
      SUBROUTINE HMSG ( TEXT )
C HALO: TEXT and a newline to standard error, through write(2); see
C HALO:   hoswin.f for why not Fortran unit 0.
      USE ISO_C_BINDING
      CHARACTER*(*) TEXT
      INTERFACE
         INTEGER(C_SIZE_T) FUNCTION CWRITE ( FD, BUF, N )
     &      BIND(C, NAME='write')
         IMPORT :: C_INT, C_CHAR, C_SIZE_T
         INTEGER(C_INT), VALUE    :: FD
         CHARACTER(KIND=C_CHAR)   :: BUF(*)
         INTEGER(C_SIZE_T), VALUE :: N
         END FUNCTION
      END INTERFACE
      INTEGER(C_SIZE_T) IRC
      INTEGER           LT
      LT  = LEN_TRIM ( TEXT )
      IRC = CWRITE ( INT ( 2, C_INT ), TEXT(1:LT) // CHAR(10),
     &               INT ( LT + 1, C_SIZE_T ) )
      RETURN
      END
      SUBROUTINE HTMPDR ( BASE )
C HALO: TMPDIR when it is short enough (see hoswin.f), else /tmp.
      CHARACTER*(*) BASE
      CHARACTER*256 V
      INTEGER       LV
      V = ' '
      CALL GETENV ( 'TMPDIR', V )
      LV = LEN_TRIM ( V )
      IF ( LV .GT. 1 .AND. V(LV:LV) .EQ. '/' ) LV = LV - 1
      IF ( LV .GT. 0 .AND. LV .LE. 50 ) THEN
         BASE = V(1:LV)
         RETURN
      ENDIF
      BASE = '/tmp'
      RETURN
      END
