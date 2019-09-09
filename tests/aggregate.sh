#!/bin/bash

for f in *.csv; do
	INDEX="${f%.*}"
	DATA=$(tail -q -n2 ${f})
	echo ${INDEX},${DATA}
done
