C
C
C
	FUNCTION FTOTRIG(X)
	COMMON/HIPARNT/HIPR1(100),IHPR2(50),HINT1(100),IHNT2(50)
	SAVE  /HIPARNT/
	OMG=OMG0(X)*HINT1(60)/HIPR1(31)/2.0
	FTOTRIG=1.0-EXP(-2.0*OMG)
	RETURN
	END
