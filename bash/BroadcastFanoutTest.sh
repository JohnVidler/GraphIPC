#!/bin/bash

#for LENGTH in {1000..1700000..1000}
for SINKS in {1..100..1}; do
    echo Running test on sinks = ${SINKS}

    mkdir "./data/${SINKS}" -p

    ./GraphRouter &

    sleep 1

    START=1
    END=$SINKS
    for (( ITER=$START; ITER<$END; ITER++ )); do
        let "ADDRESS = ($ITER*1000) + 1000"

        ./Graph -a ${ADDRESS} -o | ../../modified-pv/pv -lrafb -t -i 1 2> "${ADDRESS}.log" > /dev/null &
        ./GraphRouter -s 1000 -t ${ADDRESS} -c
    done

    sleep 1
    echo "Go!"

    ./GraphRouter -t 1000 --policy broadcast
    ./ArgTest -i 0 -s 12800  -c 20000 | ../../modified-pv/pv -lrafb -t -i 1 2> 1000.log | ./Graph -a 1000 -i && killall GraphRouter &

    wait

    killall GraphRouter
    killall Graph

    for (( ITER=$START; ITER<$END; ITER++ )); do
        let "ADDRESS = ($ITER*1000) + 1000"

        cat "${ADDRESS}.log" | tr '\r' '\n' | tr -s ' ' ',' > "./data/${SINKS}/${ADDRESS}.log"
        rm "${ADDRESS}.log"
    done

    cat 1000.log | tr '\r' '\n' | tr -s ' ' ',' > "./data/${SINKS}/1000.log"
    rm 1000.log

    sleep 1
done

echo "Done!"