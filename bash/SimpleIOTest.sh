#!/bin/bash

for LENGTH in {1000..1700000..1000}
do
    echo Running test on length = ${LENGTH}
    ./ArgTest -s ${LENGTH} -i 0 -c 5000 | ../../modified-pv/pv -lrafb -t -i 1 2> speed.log > /dev/null && cat speed.log | tr '\r' '\n' | sed 's/ /,/g' > data/${LENGTH}.csv
    rm speed.log
done