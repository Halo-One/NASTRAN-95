      SUBROUTINE NASTIM (IHR, IMN, ISC, CPUSEC)
C HALO: gfortran spells ETIME as a two-argument SUBROUTINE,
C HALO:   CALL ETIME(VALUES, TIME), where VALUES(1) is user time,
C HALO:   VALUES(2) is system time and TIME is their sum. The one-argument
C HALO:   f77/Cray form that was here does not compile. The extra argument
C HALO:   is the only change -- the value used below still comes from
C HALO:   ARRAY(2), exactly as NASA shipped it, so no timing behaviour
C HALO:   moves. (Taking system time rather than the sum looks wrong for
C HALO:   something called CPU time, and it leaves the TIME-card limits
C HALO:   effectively inert. Correcting it is a behaviour change, not a
C HALO:   port, so it is not done here.)
      REAL ARRAY(2)
      REAL TOTAL
      CALL ETIME(ARRAY,TOTAL)
      SECS   = ARRAY(2)
      IHR    = SECS / 3600.  
      IMN    = ( SECS - 3600.*IHR ) / 60.
      ISC    = SECS - ( 3600.*IHR ) - ( 60.*IMN )
      CPUSEC = SECS
      RETURN
      END
