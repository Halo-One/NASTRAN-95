      SUBROUTINE TDATE (DATE)        
C        
C     VAX VERSION        
C     ===========        
C     (ALSO SiliconGraphics, DEC/ultrix, and SUN.        
C      CRAY AND HP DO NOT HAVE IDATE)        
C        
C     THIS ROUTINE OBTAINS THE MONTH, DAY AND YEAR, IN INTEGER FORMAT   
C        
      INTEGER DATE(3), DATE1(3)        
C        
      CALL IDATE (DATE1)        
C                 DAY   MONTH     YEAR        
C     THESE DATES HAD TO BE INTERCHANGED FOR THE SUN
      DATE(1)=DATE1(2)
      DATE(2)=DATE1(1)
C HALO: the year is printed I2 (page.f, format 20), and gfortran's
C HALO: IDATE returns the full year, so 1900 subtracted from 2026
C HALO: gives 126 and every page header reads 'SEP 17, **'. The two
C HALO: digits NASA meant are the year modulo 100. The year is also
C HALO: packed into the checkpoint tape id in xcsa.f, in an eight bit
C HALO: field, which 26 fits and 126 only just did.
      DATE(3)=MOD(DATE1(3),100)
      RETURN        
      END        
