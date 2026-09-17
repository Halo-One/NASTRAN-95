C HALO: The exit handler. What a run leaves behind, and what it says
C HALO:   about itself, decided in one place on every way out.
C HALO:
C HALO:   NASTRAN ends in several places. PEXIT is the intended one, but
C HALO:   half a dozen routines STOP or CALL EXIT on their own (ENDSYS,
C HALO:   DSMG1, FFREAD, NSINFO, DBMIO ...), a runtime error ends the
C HALO:   process from inside the Fortran library, and none of those
C HALO:   paths knows about a scratch directory or an exit code. So the
C HALO:   tidy-up is registered with the C library's atexit(), which
C HALO:   runs it on every one of them: STOP, CALL EXIT, END, and a
C HALO:   runtime error alike. (Not a crash; a segmentation fault ends
C HALO:   the process without running anything, and leaves the scratch
C HALO:   directory for the next run to ignore.)
C HALO:
C HALO:   When it runs, the Fortran runtime is still up -- its own
C HALO:   clean-up is registered earlier, at program start, and atexit
C HALO:   handlers run last-registered first -- so ordinary Fortran I/O
C HALO:   works here. Everything is closed first, which is what gets
C HALO:   buffered output onto the disk and makes files deletable on
C HALO:   Windows.
C HALO:
C HALO:   The exit code. NASTRAN exits 0 after a USER FATAL MESSAGE, and
C HALO:   nobody driving it from a script wants that. When the print
C HALO:   file is ours (a deck was named on the command line) it is read
C HALO:   back and the process ends 2 without an END OF JOB banner and 3
C HALO:   with a fatal message, through _exit() because the process is
C HALO:   already inside exit() and cannot call it again. That is a
C HALO:   scan of the text, not a flag set by the code that printed the
C HALO:   message, so it catches every fatal path uniformly -- the same
C HALO:   thing a wrapper script would do, without the wrapper.
      SUBROUTINE HATREG
C HALO: register HATEXT to run when the process exits.
      USE ISO_C_BINDING
      INTERFACE
         INTEGER(C_INT) FUNCTION CATEXT ( F ) BIND(C, NAME='atexit')
         IMPORT :: C_INT, C_FUNPTR
         TYPE(C_FUNPTR), VALUE :: F
         END FUNCTION
         SUBROUTINE HATEXT () BIND(C, NAME='hatext')
         END SUBROUTINE
      END INTERFACE
      INTEGER(C_INT) IRC
      IRC = CATEXT ( C_FUNLOC ( HATEXT ) )
      RETURN
      END
      SUBROUTINE HATEXT () BIND(C, NAME='hatext')
C HALO: the handler itself. See the header of this file. Messages go out
C HALO:   through HMSG (the C runtime), not Fortran unit 0: see hoswin.f.
      INCLUDE 'HSTATE.COM'
      CHARACTER*160   LINE
      CHARACTER*640   MSG
      CHARACTER*5     EXT(4)
      LOGICAL         ENDED, FATAL, THERE, HFATAL
      INTEGER         I, ICODE, ISZ, LS, LO, LP
      DATA            EXT / '.pch ', '.plt ', '.dic ', '.nptp' /
C
C     close every unit: the print file must be complete before it is
C     read, and Windows will not delete an open file. In stdin/stdout
C     mode 5 and 6 are the terminal's and are left alone
C
      DO 10 I = 1, 99
         IF ( HMODE .EQ. 0 .AND. ( I .EQ. 5 .OR. I .EQ. 6 ) ) GO TO 10
         CLOSE ( I, ERR = 10 )
10    CONTINUE
      CALL HCLEAN
      IF ( HMODE .NE. 1 ) RETURN
      LS = LEN_TRIM ( HSTEM )
      LP = LEN_TRIM ( HPRTF )
      LO = LEN_TRIM ( HOUTD )
C
C     the optional outputs were named so a deck that wants them gets
C     them; one this deck did not want is an empty file, so remove it
C
      DO 20 I = 1, 4
         THERE = .FALSE.
         ISZ   = -1
         INQUIRE ( FILE = HSTEM(1:LS) // EXT(I), EXIST = THERE,
     &             SIZE = ISZ )
         IF ( THERE .AND. ISZ .EQ. 0 )
     &      CALL HDELF ( HSTEM(1:LS) // EXT(I) )
20    CONTINUE
C
C     read the print file back for the verdict
C
      ENDED = .FALSE.
      FATAL = .FALSE.
      OPEN ( 98, FILE = HPRTF(1:LP), STATUS = 'OLD', ERR = 40 )
30    READ ( 98, '(A)', END = 35, ERR = 35 ) LINE
      IF ( INDEX ( LINE, 'END OF JOB' ) .GT. 0 ) ENDED = .TRUE.
      IF ( HFATAL ( LINE ) ) FATAL = .TRUE.
      GO TO 30
35    CLOSE ( 98 )
40    CONTINUE
      ICODE = 0
      IF ( .NOT. ENDED ) ICODE = 2
      IF ( FATAL ) ICODE = 3
      MSG = ' '
      IF ( ICODE .EQ. 0 ) THEN
         IF ( LO .GT. 0 ) THEN
            MSG = 'nastran: ' // HSTEM(1:LS) // ' -> ' // HOUTD(1:LO)
     &            // HPRTF(1:LP)
         ELSE
            MSG = 'nastran: ' // HSTEM(1:LS) // ' -> ' // HPRTF(1:LP)
         ENDIF
         CALL HMSG ( MSG )
         RETURN
      ENDIF
      IF ( ICODE .EQ. 2 ) MSG = 'nastran: ' // HSTEM(1:LS)
     &   // ' did not reach END OF JOB - see ' // HPRTF(1:LP)
      IF ( ICODE .EQ. 3 ) MSG = 'nastran: ' // HSTEM(1:LS)
     &   // ' finished with a FATAL message - see ' // HPRTF(1:LP)
      CALL HMSG ( MSG )
      CALL HEXIT ( ICODE )
      RETURN
      END
      LOGICAL FUNCTION HFATAL ( LINE )
C HALO: does this print line announce a fatal message: '***', blanks,
C HALO:   then 'USER FATAL MESSAGE' or 'SYSTEM FATAL MESSAGE'. Every
C HALO:   run of asterisks on the line is tried.
      CHARACTER*(*) LINE
      INTEGER       K, M, L, J
      HFATAL = .FALSE.
      L = LEN ( LINE )
      K = 1
10    J = INDEX ( LINE(K:L), '***' )
      IF ( J .EQ. 0 ) RETURN
      M = K + J - 1 + 3
20    IF ( M .GT. L ) RETURN
      IF ( LINE(M:M) .NE. ' ' ) GO TO 30
      M = M + 1
      GO TO 20
30    IF ( M + 17 .LE. L ) THEN
         IF ( LINE(M:M+17) .EQ. 'USER FATAL MESSAGE' ) HFATAL = .TRUE.
      ENDIF
      IF ( M + 19 .LE. L ) THEN
         IF ( LINE(M:M+19) .EQ. 'SYSTEM FATAL MESSAGE' )
     &      HFATAL = .TRUE.
      ENDIF
      IF ( HFATAL ) RETURN
      K = K + J + 2
      IF ( K .GT. L ) RETURN
      GO TO 10
      END
      SUBROUTINE HEXIT ( ICODE )
C HALO: end the process now with exit code ICODE. _exit rather than
C HALO:   exit because this is called from inside exit's own handlers.
      USE ISO_C_BINDING
      INTEGER ICODE
      INTERFACE
         SUBROUTINE CEXIT ( I ) BIND(C, NAME='_exit')
         IMPORT :: C_INT
         INTEGER(C_INT), VALUE :: I
         END SUBROUTINE
      END INTERFACE
      CALL CEXIT ( INT ( ICODE, C_INT ) )
      RETURN
      END
