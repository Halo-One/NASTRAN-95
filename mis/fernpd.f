      SUBROUTINE FERNPD (IROW, DVAL)
C HALO: FEER met a trial vector whose mass norm is not positive.
C HALO:
C HALO:   Called from FERXTD / FERXTS when the quantity about to go under
C HALO:   a square root (the mass norm of a trial vector) is zero,
C HALO:   negative or NaN. That happens when the mass matrix is only
C HALO:   semi-definite along the vector - lumped masses with no rotary
C HALO:   inertia leave rotational degrees of freedom massless - and
C HALO:   roundoff lands on the wrong side of zero. The caller discards
C HALO:   the vector and reseeds, or reduces the problem; this routine
C HALO:   just says so, once per run, because the reseed loop may come
C HALO:   through here several times and one warning is enough.
      INTEGER          IROW
      DOUBLE PRECISION DVAL
      INTEGER          KSYSTM(65), IO, NSAID
      CHARACTER        UFM*23, UWM*25
      COMMON /XMSSG /  UFM, UWM
      COMMON /SYSTEM/  KSYSTM
      EQUIVALENCE      (KSYSTM(2), IO)
      DATA             NSAID / 0 /
      IF (NSAID .NE. 0) RETURN
      NSAID = 1
      CALL PAGE2 (5)
      WRITE (IO,10) UWM, IROW, DVAL
   10 FORMAT (A25,' 2394', /5X,'FEER TRIAL VECTOR',I5,' HAS NO ',
     1       'POSITIVE MASS NORM (',1P,D12.4,').', /5X,'THE MASS ',
     2       'MATRIX IS',
     2       ' SINGULAR OR INDEFINITE ALONG IT, WHICH LUMPED MASSES',
     3       ' WITHOUT ROTARY INERTIA', /5X,'PRODUCE. THE VECTOR IS',
     4       ' DISCARDED AND THE REDUCTION RESEEDED; IF FEWER MODES',
     5       ' THAN REQUESTED', /5X,'FOLLOW, SEE MESSAGE 2390.')
      RETURN
      END
