      SUBROUTINE XDCODE
C
C     (MACHINE INDEPENDENT FORTRAN 77 ROUTINE)
C
C     XDCODE DECODES A 20A4 ARRAY IN RECORD INTO A 80A1 ARRAY IN ICHAR
C
C     XDCODE IS CALLED ONLY BY XRGDCF, XRGDTB, XRGSST, AND XRGSUB
C
      CHARACTER*80    TEMP
      CHARACTER*8     TEMP8
C HALO: scratch for the hand-rolled unpack below
      CHARACTER*4     ONE
      INTEGER         IZERO
      COMMON /SYSTEM/ IBUF,    NOUT,  DM37(37),NBPW
      COMMON /XRGDXX/ SKIP1(3),ICOL,  SKIP2(8),RECORD(20),ICHAR(80),
     1                SKIP3(2),ICOUNT,SKIP4(2),NAME(2)
      DATA    IBLANK/ 4H      /
      DATA    IZERO / 0 /
C
C HALO: The READ that used to be here was
C HALO:     READ (TEMP,20) ICHAR      with 20 FORMAT (80A1)
C HALO:   and gfortran 16 loses every comma in it. In a formatted read from
C HALO:   an INTERNAL unit, gfortran terminates the field at a comma even
C HALO:   under an A edit descriptor, so a card reading
C HALO:       ****SBST   1,  3
C HALO:   decodes as      ****SBST   1   3      -- the comma becomes a blank
C HALO:   and everything after it shifts left. XRGDTP then classifies the
C HALO:   comma as a blank, the state machine in XRGDEV rejects the digit
C HALO:   that follows, and every rigid format in rf/ fails to load with
C HALO:   UFM 8020, SYNTAX ERROR. Measured on gfortran 16.2.0; the identical
C HALO:   read from an EXTERNAL unit keeps the comma, which is why the card
C HALO:   image printed in that error message still has one.
C HALO:
C HALO:   Unpacking the characters by hand avoids formatted input entirely.
C HALO:   The packing has to match what a Hollerith constant looks like in
C HALO:   an INTEGER, because XRGDTP compares ICHAR against 1H, 1H- and
C HALO:   1H0..1H9: the character in the first byte, blanks after it. A
C HALO:   CHARACTER*4 assignment pads on the right, so ONE = TEMP(K:K) is
C HALO:   exactly that, and TRANSFER reinterprets it without converting.
      WRITE (TEMP,10) RECORD
      DO 15 K = 1,80
      ONE      = TEMP(K:K)
      ICHAR(K) = TRANSFER(ONE,IZERO)
 15   CONTINUE
 10   FORMAT (20A4)
 20   FORMAT (80A1)
      RETURN
C
      ENTRY XECODE
C     ============
C
C     XECODE ENCODES A 8A1 BCD ARRAY IN ICHAR INTO A 2A4 BCD ARRAY
C     IN NAME
C     (THIS ENTRY REPLACES THE OLD MACHINE DEPENDENT ROUTINE OF THE
C     SAME NAME)
C
C     THE INCOMING WORD IN CDC MACHINE WOULD BE ZERO FILLED, SUCH AS
C     THE CARD TABLE AND THE MED TABLE IN XGPI RESTART PROCESSING.
C     MAKE SURE THAT THE INCOMING WORD FROM A 60- OR 64- BIT MACHINE
C     IS BLANK FILLED IF IT IS LESS THAN 8 BYTE LONG
C
C     XECODE IS CALL ONLY BY XRGDTB
C
      IF (NBPW.LT.60 .OR. ICOUNT.EQ.8) GO TO 25
      DO 22 K = ICOUNT,7
 22   ICHAR(ICOL+K) = IBLANK
 25   CALL NA12A8 (*50,ICHAR(ICOL),8,NAME,NOTUSE)
      IF (NBPW .NE. 60) RETURN
C
C     BLANK OUT 2ND WORD (CDC ONLY)
C
      WRITE (TEMP8,30) NAME(1)
      NAME(1) = IBLANK
      NAME(2) = IBLANK
      READ (TEMP8,40) NAME
 30   FORMAT (A8)
 40   FORMAT (2A4)
      RETURN
C
 50   WRITE  (NOUT,60)
 60   FORMAT ('0BAD DATA/XECODE')
      RETURN
      END
