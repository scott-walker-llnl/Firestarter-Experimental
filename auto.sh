EXP="merlot-test1"
NITER=200000
TURBO="t"
PSTATE="0x1a00"
DUTY=100000
PART=4
MAXFREQ="2.6"

START=1
END=1
LPOW=60
LSEC=8
SEC=25
POW=105
CTR=1

#for ((POW=60; POW <= 105; POW += 5));
#do
	for ((CTR=START; CTR<=END; CTR++));
	do
		echo -e "$NITER\n$LSEC\n$LPOW.0\n$SEC\n$POW.0\n$PSTATE\n$TURBO\n$DUTY\n$PART\n$MAXFREQ" > fsconfig
		#DPATH="data/$EXP/$LPOW/$LSEC/$POW/$SEC/$PART/$DUTY"
		DPATH="data/$EXP/$CTR/"
		./FIRESTARTER --function 10 -q 1> pow
		./check.sh 1> out
		mkdir -p $DPATH
		mv *.msrdat $DPATH
		mv *.pow $DPATH 
		mv out $DPATH
		mv pow $DPATH 
		#sleep 40s
		echo -e "$EXP, $NITER, $LSTART, $LSEC, $SEC, $TURBO, $PSTATE, $PART, $DUTY" >> data/$EXP/README.txt
	done
#done

