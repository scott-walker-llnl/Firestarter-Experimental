EXP="turbo1"
NITER=100000
TURBO="t"
PSTATE="0x2500"
DUTY=10000
PART=4
MAXFREQ="3.5"
NUMFS=3
NUMSLEEP=1

START=1
END=1
LPOW=150
LSEC=2
SEC=25
POW=150
CTR=1

#for ((POW=60; POW <= 105; POW += 5));
#do
	for ((CTR=START; CTR<=END; CTR++));
	do
		echo -e "$NITER\n$LSEC\n$LPOW.0\n$SEC\n$POW.0\n$PSTATE\n$TURBO\n$DUTY\n$PART\n$MAXFREQ\n$NUMFS\n$NUMSLEEP\n" > fsconfig
		#DPATH="data/$EXP/$LPOW/$LSEC/$POW/$SEC/$PART/$DUTY"
		DPATH="data/$EXP/"
		./FIRESTARTER --function 4 -q -b 0-5 1> pow
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

