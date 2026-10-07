      SUBROUTINE TKERV (X0,Y0,Z0,NK,KR,BR,SGR,CGR,SGS,CGS,T1,T2,M,
     1                  K1RT1,K1IT1,K2RT2P,K2IT2P,K10T1,K20T2P)
C
C     HALO: TKER (IND = 1, THE INCREMENTAL OSCILLATORY KERNELS) FOR NK
C     REDUCED FREQUENCIES KR(1..NK) AT ONCE - THE SAME GEOMETRY, THE
C     SAME MACH. TKER'S BRANCHES (ICHUZ) AND ALL BUT K1 = KR R1/BR
C     AND WHAT FOLLOWS FROM IT DEPEND ON THE GEOMETRY ALONE, SO THEY ARE
C     WORKED OUT ONCE; EACH K-DEPENDENT STATEMENT OF TKER IS A LOOP OVER
C     THE NK FREQUENCIES HERE, THE SAME EXPRESSION IN THE SAME ORDER, SO
C     EACH FREQUENCY'S RESULT IS TKER'S TO THE BIT. THE LOOPS WITHOUT A
C     LIBRARY CALL VECTORISE (THE ELEVEN DIVISIONS BY C1..C11 AND THE
C     ELEVEN BY THOSE AGAIN ARE MOST OF TKER); SIN AND COS STAY THE
C     SCALAR SINCOSF TKER CALLS, ONE FREQUENCY AT A TIME.
C
C     OUT: K1RT1 .. K2IT2P PER FREQUENCY, K10T1 AND K20T2P (THE SAME FOR
C     ALL) - WHAT TKER LEAVES IN /DLM/ FOR INCRO; T1 AND T2 AS TKER
C     SETS THEM.
C
      INTEGER      NK,NKM,K,ICHUZ
      PARAMETER   (NKM = 16)
      REAL         X0,Y0,Z0,KR(NK),BR,SGR,CGR,SGS,CGS,T1,T2,M
      REAL         K1RT1(NK),K1IT1(NK),K2RT2P(NK),K2IT2P(NK),K10T1,
     1             K20T2P
      REAL         K10,K20,R1,R1S,EPS,C1,C2,C3,C4,C5,T2P,BETA2,BIGR,
     1             MU1,MU,EXARG,E,C3G,C4G,C5G,C3B,C4B,C8
      REAL         K1(NKM),K2(NKM),CC(NKM,11),RR(NKM,11),QQ(NKM,11),
     1             I00R(NKM),I00I(NKM),J00R(NKM),I20R3(NKM),J00I(NKM),
     2             I20I3(NKM),I10I(NKM),I10R(NKM),J0UR(NKM),J0UI(NKM),
     3             I0UR(NKM),I0UI(NKM),I1UR(NKM),I1UI(NKM),I2UR3(NKM),
     4             I2UI3(NKM),DK1R(NKM),DK1I(NKM),DK2R(NKM),DK2I(NKM),
     5             C6(NKM),S6(NKM),CO6(NKM),A3(NKM),CA3(NKM),SA3(NKM),
     6             A5(NKM),CA5(NKM),SA5(NKM),CK1R,CK1I,CK2R,CK2I,C9,CAR
C     HALO-ISA-BEGIN
C     THIS ROUTINE BUILT A SECOND TIME FOR X86-64-V3 (AVX2), TAKEN WHEN
C     THE PROCESSOR HAS IT (MSC/MSCISA.C; CMAKELISTS.TXT MAKES THE COPY)
      INTEGER      N95ISA
      IF (N95ISA() .LT. 3) GO TO 3
      CALL TKERVV3 (X0,Y0,Z0,NK,KR,BR,SGR,CGR,SGS,CGS,T1,T2,M,
     1              K1RT1,K1IT1,K2RT2P,K2IT2P,K10T1,K20T2P)
      RETURN
    3 CONTINUE
C     HALO-ISA-END
C
      EPS    = 0.00001
      K10    = 0.0
      K20    = 0.0
      DO 1 K = 1,NK
      K1RT1(K)  = 0.0
      K1IT1(K)  = 0.0
      K2RT2P(K) = 0.0
      K2IT2P(K) = 0.0
    1 CONTINUE
      K10T1  = 0.0
      K20T2P = 0.0
      R1     = SQRT(Y0*Y0 + Z0*Z0)
      R1S    = R1
      IF (ABS(R1) .GT. EPS) GO TO 200
      IF (X0) 905,120,120
  120 T1     = CGR*CGS + SGR*SGS
      K10    = 2.0
!GCC$ NOVECTOR
      DO 125 K = 1,NK
      C1     = KR(K)*X0/BR
      K1RT1(K) = 2.0*T1*COS(C1)
      K1IT1(K) =-2.0*T1*SIN(C1)
  125 CONTINUE
      K10T1  = 2.0*T1
      GO TO  905
  200 C1     = CGR
      C2     = SGR
      C3     = CGS
      C4     = SGS
      T2P    = (Z0*Z0*C1*C3 + Y0*Y0*C2*C4 - Z0*Y0*(C2*C3+C1*C4))
      T2     = (100.*T2P)/(BR*BR)
      IF (ABS(T2)-EPS) 210,220,220
  210 ICHUZ  = 1
      T1     = CGR*CGS + SGR*SGS
      T2     = 0.0
      GO TO 300
  220 T1     = CGR*CGS + SGR*SGS
      IF (ABS(T1)-EPS) 230,240,240
  230 ICHUZ  = 2
      T1     = 0.
      GO TO 300
  240 ICHUZ  = 3
  300 BETA2  = (1.-M*M)
      BIGR   = SQRT(X0*X0 + BETA2*R1*R1)
      DO 305 K = 1,NK
      K1(K)  = KR(K)*R1/BR
  305 CONTINUE
      MU1    = (M*BIGR-X0)/(BETA2*R1)
      MU     = ABS(MU1)
      DO 306 K = 1,NK
      K2(K)  = K1(K)*K1(K)
  306 CONTINUE
      IF (MU1) 310,320,330
  310 ICHUZ  = ICHUZ + 3
      GO TO 330
  320 ICHUZ  = ICHUZ + 6
  330 CONTINUE
      EXARG = -0.372*MU
      IF (EXARG .GE. -180.0) GO TO 335
      E   = 0.0
      GO TO 337
  335 E   = EXP(EXARG)
  337 CONTINUE
      DO 338 K = 1,NK
      CC(K, 1) =  0.138384 + K2(K)
      CC(K, 2) =  0.553536 + K2(K)
      CC(K, 3) =  1.245456 + K2(K)
      CC(K, 4) =  2.214144 + K2(K)
      CC(K, 5) =  3.4596   + K2(K)
      CC(K, 6) =  4.981824 + K2(K)
      CC(K, 7) =  6.780816 + K2(K)
      CC(K, 8) =  8.856576 + K2(K)
      CC(K, 9) = 11.209104 + K2(K)
      CC(K,10) = 13.8384   + K2(K)
      CC(K,11) = 16.744464 + K2(K)
      RR(K, 1) = .24186198 / CC(K, 1)
      RR(K, 2) =-2.7918027 / CC(K, 2)
      RR(K, 3) = 24.991079 / CC(K, 3)
      RR(K, 4) =-111.59196 / CC(K, 4)
      RR(K, 5) = 271.43549 / CC(K, 5)
      RR(K, 6) =-305.75288 / CC(K, 6)
      RR(K, 7) =-41.18363  / CC(K, 7)
      RR(K, 8) = 545.98537 / CC(K, 8)
      RR(K, 9) =-644.78155 / CC(K, 9)
      RR(K,10) = 328.72755 / CC(K,10)
      RR(K,11) =-64.279511 / CC(K,11)
  338 CONTINUE
      IF (ICHUZ .LT. 4) GO TO 340
      DO 339 K = 1,NK
      I00R(K) = .372*(RR(K,1) + 2.*RR(K,2) + 3.*RR(K,3) + 4.*RR(K,4)
     1        + 5.*RR(K,5) + 6.*RR(K,6) + 7.*RR(K,7) + 8.*RR(K,8)
     2        + 9.*RR(K,9) + 10.*RR(K,10) + 11.*RR(K,11))
      I00I(K) =-K1(K)*(RR(K,1) + RR(K,2) + RR(K,3) + RR(K,4)
     1        + RR(K,5) + RR(K,6) + RR(K,7) + RR(K,8) + RR(K,9)
     2        + RR(K,10) + RR(K,11))
  339 CONTINUE
  340 GO TO (420,350,350,390,350,350,380,350,350), ICHUZ
  350 DO 351 K = 1,NK
      QQ(K, 1) = RR(K, 1)/CC(K, 1)
      QQ(K, 2) = RR(K, 2)/CC(K, 2)
      QQ(K, 3) = RR(K, 3)/CC(K, 3)
      QQ(K, 4) = RR(K, 4)/CC(K, 4)
      QQ(K, 5) = RR(K, 5)/CC(K, 5)
      QQ(K, 6) = RR(K, 6)/CC(K, 6)
      QQ(K, 7) = RR(K, 7)/CC(K, 7)
      QQ(K, 8) = RR(K, 8)/CC(K, 8)
      QQ(K, 9) = RR(K, 9)/CC(K, 9)
      QQ(K,10) = RR(K,10)/CC(K,10)
      QQ(K,11) = RR(K,11)/CC(K,11)
  351 CONTINUE
      GO TO (420,410,410,390,360,360,380,360,360), ICHUZ
  360 DO 361 K = 1,NK
      J00R(K) = QQ(K,1)*(.138384-K2(K))+QQ(K,2)*(.553536-K2(K))
     1        + QQ(K,3)*(1.245456-K2(K))+QQ(K,4)*(2.214144-K2(K))
     2        + QQ(K,5)*(3.4596-K2(K))+QQ(K,6)*(4.981824-K2(K))
     3        + QQ(K,7)*(6.780816-K2(K))+QQ(K,8)*(8.856576-K2(K))
     4        + QQ(K,9)*(11.209104-K2(K))+QQ(K,10)*(13.8384-K2(K))
     5        + QQ(K,11)*(16.744464-K2(K))
      I20R3(K) = 2.+K1(K)*I00I(K)+K2(K)*J00R(K)
  361 CONTINUE
      GO TO  (420,410,410,390,410,390,380,370,370),ICHUZ
  370 DO 371 K = 1,NK
      J00I(K) = -K1(K)*(.744*QQ(K,1)+1.488*QQ(K,2)+2.232*QQ(K,3)
     1        + 2.976*QQ(K,4)+3.72*QQ(K,5)+4.464*QQ(K,6)
     2        + 5.208*QQ(K,7)+5.952*QQ(K,8)+6.696*QQ(K,9)
     3        + 7.44*QQ(K,10)+8.184*QQ(K,11))
      I20I3(K) = -K1(K)*I00R(K)+K2(K)*J00I(K)
  371 CONTINUE
      IF (ICHUZ .EQ. 8) GO TO 500
  380 DO 381 K = 1,NK
      I10I(K) = -K1(K)*I00R(K)
  381 CONTINUE
  390 DO 391 K = 1,NK
      I10R(K) = 1.+ K1(K)*I00I(K)
  391 CONTINUE
      GO TO (420,410,410,420,410,410,500,500,500), ICHUZ
  410 DO 411 K = 1,NK
      J0UR(K) = E*(QQ(K,1)*(0.138384 - K2(K) + 0.372*MU*CC(K,1)) +
     1       E*(QQ(K,2)*(0.553536 - K2(K) + 0.744*MU*CC(K,2)) +
     2       E*(QQ(K,3)*(1.245456 - K2(K) + 1.116*MU*CC(K,3)) +
     3       E*(QQ(K,4)*(2.214144 - K2(K) + 1.488*MU*CC(K,4)) +
     4       E*(QQ(K,5)*(3.4596   - K2(K) + 1.860*MU*CC(K,5)) +
     5       E*(QQ(K,6)*(4.981824 - K2(K) + 2.232*MU*CC(K,6)) +
     6       E*(QQ(K,7)*(6.780816 - K2(K) + 2.604*MU*CC(K,7)) +
     7       E*(QQ(K,8)*(8.856576 - K2(K) + 2.976*MU*CC(K,8)) +
     8       E*(QQ(K,9)*(11.209104- K2(K) + 3.348*MU*CC(K,9)) +
     9       E*(QQ(K,10)*(13.8384 - K2(K) + 3.72*MU*CC(K,10)) +
     O       E*(QQ(K,11)*(16.744464-K2(K) + 4.092*MU*CC(K,11)))
     A       ))))))))))
      J0UI(K) = -K1(K)*(E*(QQ(K,1)*(0.744 + MU*CC(K,1)) +
     1          E*(QQ(K,2)*(1.488 + MU*CC(K,2)) +
     2          E*(QQ(K,3)*(2.232 + MU*CC(K,3)) +
     3          E*(QQ(K,4)*(2.976 + MU*CC(K,4)) +
     4          E*(QQ(K,5)*(3.720 + MU*CC(K,5)) +
     5          E*(QQ(K,6)*(4.464 + MU*CC(K,6)) +
     6          E*(QQ(K,7)*(5.208 + MU*CC(K,7)) +
     7          E*(QQ(K,8)*(5.952 + MU*CC(K,8)) +
     8          E*(QQ(K,9)*(6.696 + MU*CC(K,9)) +
     9          E*(QQ(K,10)*(7.44 + MU*CC(K,10))+
     O          E*(QQ(K,11)*(8.184+ MU*CC(K,11)))
     A          )))))))))))
  411 CONTINUE
  420 DO 421 K = 1,NK
      I0UR(K) = .372*E*(RR(K,1)+E*(2.*RR(K,2)+E*(3.*RR(K,3)+
     1          E*(4.*RR(K,4)+E*(5.*RR(K,5)+E*(6.*RR(K,6)+
     2          E*(7.*RR(K,7)+E*(8.*RR(K,8)+E*(9.*RR(K,9)+
     3          E*(10.*RR(K,10)+E*11.*RR(K,11)))))))))))
      I0UI(K) = -K1(K)*(E*(RR(K,1)+E*(RR(K,2)+E*(RR(K,3)+
     1          E*(RR(K,4)+E*(RR(K,5)+E*(RR(K,6)+E*(RR(K,7)+
     2          E*(RR(K,8)+E*(RR(K,9)+E*(RR(K,10)+E*RR(K,11)
     3          )))))))))))
      C6(K) = K1(K)*MU
  421 CONTINUE
      R1   = R1S
!GCC$ NOVECTOR
      DO 422 K = 1,NK
      S6(K)  = SIN(C6(K))
      CO6(K) = COS(C6(K))
  422 CONTINUE
      C3G  = SQRT(1.+MU*MU)
      C4G  = MU/C3G
      C5G  = C4G/(1.+MU*MU)
      GO TO (430,440,430,430,440,430,500,500,500), ICHUZ
  430 DO 431 K = 1,NK
      I1UR(K) = CO6(K)*(1.-C4G+K1(K)*I0UI(K)) - S6(K)*K1(K)*I0UR(K)
      I1UI(K) =-CO6(K)*K1(K)*I0UR(K) - S6(K)*(1.-C4G+K1(K)*I0UI(K))
  431 CONTINUE
      GO TO (500,440,440,460,440,440,500,500,500), ICHUZ
  440 DO 441 K = 1,NK
      I2UR3(K) = CO6(K)*(2.*(1.-C4G)-C5G+K1(K)*I0UI(K)+K2(K)*J0UR(K))
     1         + S6(K)*(C6(K)*(1.-C4G)-K1(K)*I0UR(K) + K2(K)*J0UI(K))
      I2UI3(K) = CO6(K)*(C6(K)*(1.-C4G)-K1(K)*I0UR(K)+K2(K)*J0UI(K))
     1         - S6(K)*(2.*(1.-C4G)-C5G+K1(K)*I0UI(K) + K2(K)*J0UR(K))
  441 CONTINUE
      GO TO (500,500,500,460,450,450,500,500,500), ICHUZ
  450 DO 451 K = 1,NK
      I2UR3(K) = 2.0*I20R3(K) - I2UR3(K)
  451 CONTINUE
      IF (ICHUZ-6) 500,460,500
  460 DO 461 K = 1,NK
      CAR  = 2.*I10R(K) - I1UR(K)
      I1UR(K) = CAR
  461 CONTINUE
  500 R1   = R1S
      DO 501 K = 1,NK
      DK1R(K) = 0.
      DK1I(K) = 0.
      DK2R(K) = 0.
      DK2I(K) = 0.
      A3(K)   = K1(K)*MU1
      A5(K)   = KR(K)*X0/BR
  501 CONTINUE
!GCC$ NOVECTOR
      DO 502 K = 1,NK
      CA3(K)  = COS(A3(K))
      SA3(K)  = SIN(A3(K))
  502 CONTINUE
      C3B  = M*R1/BIGR
      C4B  = SQRT(1.+MU1*MU1)
!GCC$ NOVECTOR
      DO 503 K = 1,NK
      CA5(K)  = COS(A5(K))
      SA5(K)  = SIN(A5(K))
  503 CONTINUE
      GO TO (530,540,530,530,540,530,510,520,510), ICHUZ
  510 DO 511 K = 1,NK
      I1UR(K) = I10R(K)
      I1UI(K) = I10I(K)
  511 CONTINUE
      IF (ICHUZ-7) 520,530,520
  520 DO 521 K = 1,NK
      I2UR3(K) = I20R3(K)
      I2UI3(K) = I20I3(K)
  521 CONTINUE
      IF (ICHUZ-8) 530,540,530
  530 K10  = 1.0 + X0/BIGR
      DO 531 K = 1,NK
      CK1R = I1UR(K) + C3B*CA3(K)/C4B
      CK1I = I1UI(K) - C3B*SA3(K)/C4B
      DK1R(K) = CK1R*CA5(K) + CK1I*SA5(K)
      DK1I(K) = CK1I*CA5(K) - CK1R*SA5(K)
  531 CONTINUE
      GO TO (900,540,540,900,540,540,900,540,540), ICHUZ
  540 C8   = (BETA2*(R1/BIGR)**2 + (2.+MU1*C3B)/(C4B*C4B))*(-C3B/C4B)
      K20  = -2.0 - X0*(2.0+BETA2*(R1/BIGR)**2)/BIGR
      DO 541 K = 1,NK
      C9   = (K1(K)*C3B)*( C3B/C4B)
      CK2R = -I2UR3(K) + C8*CA3(K) - C9*SA3(K)
      CK2I = -I2UI3(K) - C9*CA3(K) - C8*SA3(K)
      DK2R(K) = CK2R*CA5(K) + CK2I*SA5(K)
      DK2I(K) = CK2I*CA5(K) - CK2R*SA5(K)
  541 CONTINUE
  900 CONTINUE
      DO 901 K = 1,NK
      K1RT1(K)  = T1 *DK1R(K)
      K1IT1(K)  = T1 *DK1I(K)
      K2RT2P(K) = T2P*DK2R(K)
      K2IT2P(K) = T2P*DK2I(K)
  901 CONTINUE
      K10T1  = K10*T1
      K20T2P = K20*T2P
  905 CONTINUE
      RETURN
      END
