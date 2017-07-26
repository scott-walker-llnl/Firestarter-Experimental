EXP="exp1"
NITER=20000
TURBO="t"
PSTATE="0x2d00"
DUTY=10000
PART=4
MAXFREQ="4.2" # should be set to max non-turbo frequency
FSPATH="~/Firestarter-Experimental/FIRESTARTER" # should point to firestarter executable
TDP=205
CPS=8 #cores per socket

START=$TDP * 0.6
END=$TDP #set to tdp
LPOW=60
LSEC=8
SEC=25
POW=$TDP # set to tdp
CTR=1

#for ((POW=60; POW <= 105; POW += 5));
#do
	for ((LPOW=START; LPOW<=END; LPOW += 5));
	do
		echo -e "$NITER\n$LSEC\n$LPOW.0\n$SEC\n$POW.0\n$PSTATE\n$TURBO\n$DUTY\n$PART\n$MAXFREQ\n$CPS" > fsconfig
		DPATH="data/$EXP/$LPOW/$LSEC/$POW/$SEC/$PART/$DUTY"
		$FSPATH --function 2 -q 1> pow
		./check.sh 1> out
		mkdir -p $DPATH
		mv *.msrdat $DPATH
		mv *.pow $DPATH 
		mv out $DPATH
		mv pow $DPATH 
		sleep 40s
		echo -e "$EXP, $NITER, $LSTART, $LSEC, $SEC, $TURBO, $PSTATE, $PART, $DUTY" >> data/$EXP/README.txt
	done
#done

