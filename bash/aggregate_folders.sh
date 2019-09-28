#!/bin/bash

for dir in */
do
	INDEX="${dir%/}"
	#DATA=$(tail -q -n2 ${f})
	#echo ${INDEX},${DATA}

	FINALAVG_A=$(tail -n 2 ${dir}/1000.log | cut -f 4 -d, | head -n 1)
	FINALAVG_B=$(tail -n 2 ${dir}/2000.log | cut -f 4 -d, | head -n 1)
	FINALAVG_C=$(tail -n 2 ${dir}/3000.log | cut -f 4 -d, | head -n 1)
	FINALAVG_D=$(tail -n 2 ${dir}/4000.log | cut -f 4 -d, | head -n 1)

	echo ${INDEX},${FINALAVG_A},${FINALAVG_B},${FINALAVG_C},${FINALAVG_D}
done
