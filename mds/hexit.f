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
      CHARACTER*160   LINE, SLINE
      CHARACTER*640   MSG
      CHARACTER*5     EXT(5)
      LOGICAL         ENDED, FATAL, THERE, HFATAL, FEWER, NOMAS, NOMASM
      INTEGER         I, ICODE, ISZ, LS, LO, LP, LQ
      DATA            EXT / '.pch ', '.plt ', '.dic ', '.nptp',
     &                        '.sof ' /
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
      DO 20 I = 1, 5
         THERE = .FALSE.
         ISZ   = -1
         INQUIRE ( FILE = HSTEM(1:LS) // EXT(I), EXIST = THERE,
     &             SIZE = ISZ )
         IF ( THERE .AND. ISZ .EQ. 0 )
     &      CALL HDELF ( HSTEM(1:LS) // EXT(I) )
20    CONTINUE
C
C     in MSC dialect mode the print file is rewritten into the layout
C     MSC prints, so that a reader written against MSC output reads
C     this one. Before the verdict scan: the rewrite keeps every
C     message line, and the verdict is read from the file that is left.
C
      IF ( HASE .EQ. 1 ) CALL HMSCF6 ( HPRTF(1:LP), ICODE )
C
C     read the print file back for the verdict
C
      ENDED = .FALSE.
      FATAL = .FALSE.
      FEWER  = .FALSE.
      NOMAS  = .FALSE.
      NOMASM = .FALSE.
      OPEN ( 98, FILE = HPRTF(1:LP), STATUS = 'OLD', ERR = 40 )
30    READ ( 98, '(A)', END = 35, ERR = 35 ) LINE
C HALO: the solver spells its messages two ways. Most modules write
C HALO:   '*** USER FATAL MESSAGE nnnn, text' through WRTMSG; the ones
C HALO:   that go through mis/msgwrt.f pad each word into a field of its
C HALO:   own and write '*** USER FATAL    MESSAGE  nnnn' with the text
C HALO:   on the next line. Every test below was written against the
C HALO:   first spelling, so a run whose only fatal was of the second
C HALO:   kind (a GRAV load with no mass anywhere is one: UFM 3056)
C HALO:   printed the success line and exited 0. Squeeze the runs of
C HALO:   blanks out of a copy and match on that, so both spellings and
C HALO:   any future one are read alike.
      CALL HSQZ ( LINE, SLINE, LQ )
      IF ( INDEX ( SLINE(1:LQ), 'END OF JOB' ) .GT. 0 ) ENDED = .TRUE.
      IF ( HFATAL ( SLINE(1:LQ) ) ) FATAL = .TRUE.
C HALO: two warnings worth repeating on the terminal: the eigensolver
C HALO:   found fewer modes than the deck asked for (2390), and why it
C HALO:   usually did (2394, a mass matrix that is singular along some
C HALO:   directions). A run that ends 0 with 87 of 120 modes is not
C HALO:   the run the user meant.
      IF ( INDEX ( SLINE(1:LQ), 'WARNING MESSAGE 2390' ) .GT. 0 )
     &   FEWER = .TRUE.
      IF ( INDEX ( SLINE(1:LQ), 'WARNING MESSAGE 2394' ) .GT. 0 )
     &   NOMAS = .TRUE.
C HALO: a rigid format that needs a mass matrix and has none stops in
C HALO:   its own DMAP check, which prints no numbered message at all
C HALO:   and lets the job end normally: END OF JOB, exit 0, and no
C HALO:   eigenvalue table. Nothing else would tell the user that the
C HALO:   run they are about to read produced nothing.
      IF ( INDEX ( SLINE(1:LQ), 'MASS MATRIX REQUIRED' ) .GT. 0 )
     &   NOMASM = .TRUE.
      GO TO 30
35    CLOSE ( 98 )
      IF ( FEWER ) CALL HMSG ( 'nastran: fewer modes than requested '
     &   // 'were found (UWM 2390 in the print file has the count).' )
      IF ( NOMAS ) CALL HMSG ( 'nastran: the mass matrix is singular '
     &   // 'or indefinite along some directions (UWM 2394): lumped' )
      IF ( NOMAS ) CALL HMSG ( '         masses without rotary '
     &   // 'inertia do this. The modes found are still valid.' )
      IF ( NOMASM ) CALL HMSG ( 'nastran: this solution needs a mass '
     &   // 'matrix and the model has none, so it stopped with' )
      IF ( NOMASM ) CALL HMSG ( '         no results (MASS MATRIX '
     &   // 'REQUIRED in the print file). Give the MAT1 cards a' )
      IF ( NOMASM ) CALL HMSG ( '         density in field 6, or add '
     &   // 'CONM2 cards, or NSM on the property cards.' )
C HALO: the message itself, and what it means, on the terminal --
C HALO:   nobody should have to open the print file to learn that a
C HALO:   grid was missing
      IF ( FATAL ) CALL HMSCDG ( HPRTF(1:LP), ICODE )
C HALO: the 1970s executable given an MSC deck fails on the first card
C HALO:   of the executive control, with a message about field widths
C HALO:   that is true and useless. Say what actually happened.
      IF ( FATAL .AND. HASE .EQ. 0 .AND. HMSCDK .EQ. 1 ) THEN
         CALL HMSG ( ' ' )
         CALL HMSG ( 'nastran: this deck is written in MSC Nastran''s '
     &      // 'dialect (SOL 1xx, INCLUDE, free-field cards).' )
         CALL HMSG ( '         nastran95 reads only the 1970s '
     &      // 'input NASA wrote it for; run the same deck with' )
         CALL HMSG ( '         nastran95ase, which translates the '
     &      // 'MSC dialect and writes MSC-layout output.' )
      ENDIF
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
      SUBROUTINE HSQZ ( LINE, OUT, LOUT )
C HALO: copy LINE to OUT with every run of blanks collapsed to one, so
C HALO:   that a test written for one spelling of a message reads every
C HALO:   spelling of it (a trailing run becomes one blank). LOUT is the
C HALO:   length used, at least 1 (a blank line gives one blank).
      CHARACTER*(*) LINE, OUT
      INTEGER       LOUT, I, L, M
      LOGICAL       WASBLK
      L      = LEN ( LINE )
      M      = LEN ( OUT )
      LOUT   = 0
      WASBLK = .FALSE.
      DO 10 I = 1, L
      IF ( LINE(I:I) .EQ. ' ' ) THEN
         IF ( WASBLK ) GO TO 10
         WASBLK = .TRUE.
      ELSE
         WASBLK = .FALSE.
      ENDIF
      IF ( LOUT .GE. M ) GO TO 20
      LOUT = LOUT + 1
      OUT(LOUT:LOUT) = LINE(I:I)
10    CONTINUE
20    IF ( LOUT .EQ. 0 ) THEN
         LOUT   = 1
         OUT(1:1) = ' '
      ENDIF
      RETURN
      END
      LOGICAL FUNCTION HFATAL ( LINE )
C HALO: does this print line announce a fatal message: '***', blanks,
C HALO:   then 'USER FATAL MESSAGE' or 'SYSTEM FATAL MESSAGE'. Every
C HALO:   run of asterisks on the line is tried. The caller passes a line
C HALO:   whose runs of blanks HSQZ has already collapsed to one, so the
C HALO:   spaced spelling mis/msgwrt.f writes is matched by these same
C HALO:   two literals.
      CHARACTER*(*) LINE
      INTEGER       K, M, L, J
      HFATAL = .FALSE.
C HALO: the solver's file system (GINO) stops the run without a
C HALO:   numbered message: "I/O SUBSYSTEM ERROR NUMBER nnn". It is a
C HALO:   fatal in every sense that matters - nothing after it is
C HALO:   solved, END OF JOB still prints - so count it as one.
      IF ( INDEX ( LINE, 'I/O SUBSYSTEM ERROR' ) .GT. 0 ) THEN
         HFATAL = .TRUE.
         RETURN
      ENDIF
C HALO:   so are GINO's 'I/O ERROR # n ON FILE ...' (a block it could
C HALO:   not read or write: the print then dumps the buffer and the
C HALO:   database directory and END OF JOB follows) and the bare
C HALO:   'ERRTRC CALLED' a MESAGE fatal or a module's own stop leaves
C HALO:   when no numbered message came with it. A flutter child of the
C HALO:   restart work ended that way with exit code 0 and no summary.
      IF ( INDEX ( LINE, 'I/O ERROR #' ) .GT. 0 .OR.
     &     INDEX ( LINE, 'ERRTRC CALLED' ) .GT. 0 ) THEN
         HFATAL = .TRUE.
         RETURN
      ENDIF
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
