      SUBROUTINE FA1PKV (AZ,AMK,AMB,N,E1,CZ,BREF,PI,VEL,IBUF,ISAVE)
C HALO: ISAVE - the vector goes to the recovery scratch (a marked loop)
C HALO:   as well as to the print; with PARAM PKVECT 1 FA1PKE calls this
C HALO:   for every loop and the unmarked ones are printed alone. The
C HALO:   header names the loop (the FLFACT entry) and its velocity, so a
C HALO:   reader can put a vector to its matched point.
C
C     HALO: THE VECTOR IS COMPUTED HERE AND WRITTEN BY FA1PKW (BELOW),
C     SO THAT THE PARALLEL PK SOLVE (MIS/FA1PKP.F) CAN COMPUTE IT IN A
C     THREAD AND WRITE IT LATER IN THE ORDER THIS ROUTINE WOULD HAVE.
C     THE FILE, THE PRINT AND THE PASS COUNT ARE FA1PKW'S NOW.
C
      INTEGER         IBUF(1)
      LOGICAL         ISAVE
      REAL            E1(5),E(2),AMK(1),AMB(1),CZ(1)
      COMPLEX         AZ(1),CEIG,EIGEN,EIGZ
      EQUIVALENCE     (EIGEN,E(1))
C
      EIGZ = (0.0,0.0)
      IF (N .LT. 2) RETURN
      E(1) = E1(1)
      E(2) = E1(2)
C
C     BUILD A = IP2 + M-1B P + M-1K
C
      CEIG = EIGEN*EIGEN
      K = 0
      DO 10 I = 1,N
      DO 10 J = 1,N
      K = K + 1
      AZ(K) = -AMB(K)*EIGEN - AMK(K)
      IF (I .EQ. J) AZ(K) = AZ(K) + CEIG
   10 CONTINUE
C
C     CORE FOR EGNVCT
C
      N2 = N*2
      NA = 1  + N2*N
      NB = NA + N2
      NC = NB + N2
      ND = NC + N2
      CALL EGNVCT (AZ,CZ(NA),EIGZ,CZ(NB),CZ(NC),CZ(ND),N)
      CALL FA1PKW (E1,CZ(NB),N,BREF,VEL,IBUF,ISAVE)
      RETURN
      END

      SUBROUTINE FA1PKW (E1,CZ,N,BREF,VEL,IBUF,ISAVE)
C
C     HALO: FA1PKV'S OUTPUT OF ONE PK EIGENVECTOR (CZ, N COMPLEX) OF THE
C     ROOT E1: A RECORD ON SCRATCH 301 FOR THE PHYSICAL VECTORS THE
C     RIGID FORMAT RECOVERS (A MARKED LOOP, ISAVE), AND THE PRINTED
C     'EIGENVECTOR FROM THE PK METHOD' WITH THE LOOP AND ITS VELOCITY
C     (EVERY LOOP UNDER PARAM PKVECT 1). FA1PKV'S OWN STATEMENTS, MOVED;
C     CZ(NB) THERE IS CZ(1).
C
      INTEGER         IBUF(1),IV(6),TRL(7),FLOOP
      LOGICAL         ISAVE
      REAL            E1(5),V(6),E(2),CZ(1)
      COMPLEX         EIGEN
      COMMON /SYSTEM/ SYSBUF,NOUT,SPACE(6),NLPP,X(2),LINES
      COMMON /BLANK / FLOOP
      EQUIVALENCE     (V(1),IV(1)),(EIGEN,E(1))
      DATA    ISCR  / 301/, IPASS /0/
C
      IF (N .LT. 2) RETURN
      E(1) = E1(1)
      E(2) = E1(2)
      IF (.NOT. ISAVE) GO TO 9
      IF (IPASS .NE. 0) GO TO 5
      CALL OPEN (*1000,ISCR,IBUF,1)
      GO TO 8
    5 CALL OPEN (*1000,ISCR,IBUF,3)
    8 IPASS = IPASS + 1
    9 CONTINUE
      N2 = N*2
      NB = 1
C
C     BUILD ON SCR1 DATA FOR VECTOR OUTPUT
C
      IF (.NOT. ISAVE) GO TO 23
      IV(1) = IPASS
      IV(2) = IPASS
      V (3) = E1(1)
      V (4) = E1(2)
      IF (E1(2) .EQ. 0.0) GO TO 20
      V(5)  = E1(3)
      V(6)  = E1(5)
      GO TO 22
   20 V(5)  = 0.0
      V(6)  = (BREF/(.34657*VEL))*E1(1)
   22 CALL WRITE (ISCR,IV,6,1)
      CALL WRITE (ISCR,CZ(NB),N2,1)
   23 CONTINUE
C
C     VECTOR IS IN CZ(NB)
C
      LINES = NLPP
      K = 0
      DO 30 I = 1,N
      IF (LINES .LT. NLPP) GO TO 25
      CALL PAGE1
      WRITE  (NOUT,21) EIGEN,FLOOP,VEL
   21 FORMAT (1H0,47X,30HEIGENVECTOR FROM THE PK METHOD, /3X,
     1        13HEIGENVALUE = ,1P,E15.5,1P,E15.5,9H   LOOP =,I5,
     2        13H   VELOCITY =,1P,E15.5, //3X,11HEIGENVECTOR)
      LINES = LINES + 5
   25 LINES = LINES + 1
      WRITE  (NOUT,26) CZ(NB+K),CZ(NB+K+1)
   26 FORMAT (16X,1P,E15.5,1P,E15.5)
      K = K + 2
   30 CONTINUE
      IF (.NOT. ISAVE) RETURN
      TRL(1) = ISCR
      TRL(2) = 1
      CALL WRTTRL (TRL)
 1000 CALL CLOSE (ISCR,3)
      RETURN
      END
