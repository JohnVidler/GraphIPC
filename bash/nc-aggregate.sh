#!/bin/bash

for dir in */
do
	INDEX="${dir%/}"
	#DATA=$(tail -q -n2 ${f})
	#echo ${INDEX},${DATA}

	FINALAVG_A=$(tail -n 2 ${dir}/nc-in.log | cut -f 4 -d, | head -n 1)
	FINALAVG_B=$(tail -n 2 ${dir}/nc-out.log | cut -f 4 -d, | head -n 1)

	echo ${INDEX},${FINALAVG_A},${FINALAVG_B}
done
