#!/usr/bin/env bash

./GraphRouter &

sleep 2

./Graph -a 2000 -o | opennlp SentenceDetector ../opennlp-models/en-sent.bin &

sleep 2

#./GraphRouter -s 1000 -t 2000 -c &

wait

killall GraphRouter
killall Graph

sleep 2

cat mediumcorpus.txt

# opennlp POSTagger opennlp-models/en-pos-perceptron.bin
# opennlp ChunkerME opennlp-models/en-chunker.bin