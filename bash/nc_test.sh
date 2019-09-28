#!/bin/bash


for LENGTH in {1000..1700000..1000}
do
	echo Running test on length = ${LENGTH}
	mkdir "./data/${LENGTH}"

	nc -l -p 10000 | ../../modified-pv/pv -lrafb -t -i 1 2> nc-out.log > /dev/null &
	./ArgTest -p 1 -i 0 -s 128 -c ${LENGTH} | ../../modified-pv/pv -lrafb -t -i 1 2> nc-in.log | nc -c localhost 10000

	cat nc-in.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/nc-in.log"
	cat nc-out.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/nc-out.log"

	echo "DONE"
done
