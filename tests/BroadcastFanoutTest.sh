#!/bin/bash

#for LENGTH in {1000..1700000..1000}
for LENGTH in {100000..1700000..5000}
do
    echo Running test on length = ${LENGTH}

    mkdir "./data/${LENGTH}"

    #./GraphRouter &

    sleep 2

    ./Graph -a 2000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 2000.log > /dev/null &
    ./Graph -a 3000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 3000.log > /dev/null &
    ./Graph -a 4000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 4000.log > /dev/null &
    ./Graph -a 5000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 5000.log > /dev/null &
    ./Graph -a 6000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 6000.log > /dev/null &
    ./Graph -a 7000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 7000.log > /dev/null &
    ./Graph -a 8000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 8000.log > /dev/null &
    ./Graph -a 9000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 9000.log > /dev/null &
    ./Graph -a 10000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 10000.log > /dev/null &
    ./Graph -a 11000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 11000.log > /dev/null &
    ./ArgTest -p 4 -i 0 -s 128  -c ${LENGTH} | ../../modified-pv/pv -lrafb -t -i 1 2> 1000.log | ./Graph -a 1000 -i && killall GraphRouter &

    sleep 2

    ./GraphRouter -s 1000 -t 2000 -c
    ./GraphRouter -s 1000 -t 3000 -c
    ./GraphRouter -s 1000 -t 4000 -c
    ./GraphRouter -s 1000 -t 5000 -c
    ./GraphRouter -s 1000 -t 6000 -c
    ./GraphRouter -s 1000 -t 7000 -c
    ./GraphRouter -s 1000 -t 8000 -c
    ./GraphRouter -s 1000 -t 9000 -c
    ./GraphRouter -s 1000 -t 10000 -c
    ./GraphRouter -s 1000 -t 11000 -c
    ./GraphRouter -t 1000 --policy broadcast

    wait

    #killall GraphRouter
    killall Graph

    sleep 2

    cat 1000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/1000.log"
    cat 2000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/2000.log"
    cat 3000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/3000.log"
    cat 4000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/4000.log"
    cat 5000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/5000.log"
    cat 6000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/6000.log"
    cat 7000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/7000.log"
    cat 8000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/8000.log"
    cat 9000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/9000.log"
    cat 10000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/10000.log"
    cat 11000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/11000.log"

    rm 1000.log
    rm 2000.log
    rm 3000.log
    rm 4000.log
    rm 5000.log
    rm 6000.log
    rm 7000.log
    rm 8000.log
    rm 9000.log
    rm 10000.log
    rm 11000.log

    sleep 2
done

echo "Done!"