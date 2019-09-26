#!/bin/bash

#for LENGTH in {1000..1700000..1000}
for LENGTH in {1000..1700000..1000}
do
    echo Running test on length = ${LENGTH}

    mkdir "./data/${LENGTH}"

    ./GraphRouter &

    sleep 1

    ./Graph -a 2000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 2000.log > /dev/null &
    ./Graph -a 3000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 3000.log > /dev/null &
    ./Graph -a 4000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 4000.log > /dev/null &
    ./Graph -a 5000 -o | ../../modified-pv/pv -lrafb -t -i 1 2> 5000.log > /dev/null &
    ./ArgTest -p 4 -i 0 -s 128  -c ${LENGTH} | ../../modified-pv/pv -lrafb -t -i 1 2> 1000.log | ./Graph -a 1000 -i && killall GraphRouter &

    sleep 1

    ./GraphRouter -s 1000 -t 2000 -c &
    ./GraphRouter -s 1000 -t 3000 -c &
    ./GraphRouter -s 1000 -t 4000 -c &
    ./GraphRouter -s 1000 -t 5000 -c &
    ./GraphRouter -t 1000 --policy roundrobin &

    wait

    killall GraphRouter
    killall Graph

    sleep 1

    cat 1000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/1000.log"
    cat 2000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/2000.log"
    cat 3000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/3000.log"
    cat 4000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/3000.log"
    cat 5000.log | tr '\r' '\n' | sed 's/ /,/g' > "./data/${LENGTH}/3000.log"

    rm 1000.log
    rm 2000.log
    rm 3000.log
    rm 4000.log
    rm 5000.log

    sleep 1
done

echo "Done!"