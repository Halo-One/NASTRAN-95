      SUBROUTINE CPUTIM (ICPUSC,RCPUSC,IFLAG)        
C        
C     THIS ROUTINE IS MACHINE DEPENDENT        
C        
C     THIS ROUTINE OBTAINS THE CURRENT CPU TIME IN SECONDS.        
C     IF IFLAG .EQ. 0, CPU TIME IS RETURNED AS AN INTEGER VALUE.        
C     IF IFLAG .NE. 0, CPU TIME IS RETURNED AS A REAL VALUE.        
C        
C     DESIGN REQUIREMENT:        
C     RCPUSC MUST OVER THE RANGE OF 10**-3 TO 10**5 CPU SECONDS        
C        
C     DEC VAX/VMS VERSION        
C     ===================        
C        
C     INCLUDE '($JPIDEF)'        
C        
C     INTEGER   BUF_ADDR, ZERO, CPU_TIME        
C     INTEGER*2 BUF_LNGTH, ITEM_CODE        
C        
C     COMMON /CPU_LIST/ BUF_LNGTH, ITEM_CODE, BUF_ADDR, LNGTH_ADDR,     
C    1                  ZERO        
C        
C     DATA BUF_LNGTH, LNGTH_ADDR, ZERO /4, 2*0/        
C     DATA ITEM_CODE /JPI$_CPUTIM/        
C        
C     BUF_ADDR = %LOC (CPU_TIME)        
C     CALL SYS$GETJPI (,,,BUF_LNGTH,,,)        
C     IF (IFLAG.EQ.0) ICPUSC = CPU_TIME/100        
C     IF (IFLAG.NE.0) RCPUSC = CPU_TIME/100.0        
C     RETURN        
C        
C        
C     SUBROUTINE CPUTIM (ICPUSC,RCPUSC,IFLAG)        
C        
C     UNIX VERSION        
C     ============        
C        
C     THIS ROUTINE OBTAINS THE CURRENT CPU TIME IN SECONDS        
C     IF IFLAG.EQ.0, CPU TIME IS RETURNED AS AN INTEGER VALUE IN ICPUSC 
C     IF IFLAG.NE.0, CPU TIME IS RETURNED AS A REAL VALUE IN RCPUSC,    
C        
C     DESIGN REQUIREMENT -        
C     RCPUSC MUST COVER THE RANGE OF 1.0**-3 TO 1.0**+5 CPU SECONDS     
C        
C     NOTE - THE CURRENT CALL TO CPUTIM MUST GIVE A TIME VALUE BIGGER   
C     THAN PREVIOUS CPUTIME CALL. OTHERWISE, CALLING ROUTINE MAY GET    
C     INTO TROUBLE, SUCH AS DIVIDED BY ZERO.        
C        
C     REAL ARRAY(2)        
C        
C     CALL ETIME (ARRAY)        
C     IF (IFLAG .NE. 0) GO TO 10        
C     ICPUSC = ARRAY(2) + .49        
C     GO TO 20        
C  10 SAVE   = RCPUSC        
C     RCPUSC = ARRAY(2)        
C     IF (RCPUSC .LE. SAVE) RCPUSC = RCPUSC + 0.0001        
C  20 RETURN        
C        
C        
C        
C     SUBROUTINE CPUTIM (ICPUSC,RCPUSC,IFLAG)        
C        
C     UNIVERSAL VERSION        
C     =================        
C        
C     THIS ROUTINE OBTAINS THE CURRENT CPU TIME IN SECONDS        
C     IF IFLAG.EQ.0, CPU TIME IS RETURNED AS AN INTEGER VALUE IN ICPUSC 
C     IF IFLAG.NE.0, CPU TIME IS RETURNED AS A REAL VALUE IN RCPUSC,    
C        
C     DESIGN REQUIREMENT -        
C     RCPUSC MUST COVER THE RANGE OF 1.0**-3 TO 1.0**+5 CPU SECONDS     
C     (SECNDS MAY BE ACCURATE ONLY TO 1/60, OR 0.001 SECOND)        
C        
C     NOTE - THE CURRENT CALL TO CPUTIM MUST GIVE A TIME VALUE BIGGER   
C     THAN PREVIOUS CPUTIME CALL. OTHERWISE, CALLING ROUTINE MAY GET    
C     INTO TROUBLE, SUCH AS DIVIDED BY ZERO.        
C        
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
      T=ARRAY(2)
      IF (IFLAG .NE. 0) GO TO 30        
      ICPUSC = T + .49        
      GO TO 40        
   30 SAVE   = RCPUSC        
      RCPUSC = T        
      IF (RCPUSC .LE. SAVE) RCPUSC = RCPUSC + 0.0001        
   40 RETURN        
C        
      END        
