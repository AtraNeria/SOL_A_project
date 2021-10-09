#!/bin/bash

#Salvo PID del server
serverPID=$(pgrep server)

#Primo client testa -W, -d, -r
./client.out -p -t 200 -f server -W ./WriteFile/w1,./WriteFile/w2 -d ./ReadFile -r ./WriteFile/w1,./WriteFile/w2 &
client1PID=$!
#Secondo client testa -w, -c, -R
./client.out -p -t 200 -f server -w ./MyDir,3 -d ./ReadFile -c ./WriteFile/w1 -R &
client2PID=$!
#Terzo client testa -l, -u
./client.out -p -t 200 -f server -W ./WriteFile/w4,./WriteFile/w5 -l ./WriteFile/w4,./WriteFile/w5 -u ./WriteFile/w5,./WriteFile/w6 &
client3PID=$!

#Aspetto terminino i client
wait $client1PID
wait $client2PID
wait $client3PID

#Termino server con SIGHUP
kill -1 $serverPID