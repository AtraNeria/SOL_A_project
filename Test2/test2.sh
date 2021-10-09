#!/bin/bash

#Salvo PID del server
serverPID=$(pgrep server)

#Primo client testa -W, -d, -r
./client.out -f server -D ./Expelled -w ./Write  &
client1PID=$!
#Secondo client testa -w, -c, -R
./client.out -f server -d ./Read -W ./Write/w1,./Write/w2,./Write/w3,./Write/w4,./Write/w5 -r ./Write/w1,./Write/w2,./Write/w3,./Write/w4,./Write/w5 &
client2PID=$!

#Aspetto terminino i client
wait $client1PID
wait $client2PID

#Termino server con SIGHUP
kill -1 $serverPID