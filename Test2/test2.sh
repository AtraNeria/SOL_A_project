#!/bin/bash

#Primo client scrive 5 file
./client.out -p -f server -D ./Expelled -w ./Write
client1PID=$!

#Secondo client scrive 8 file
./client.out -p -f server -D ./Expelled_2nd -w ./Write_2nd
client2PID=$!

#Aspetto terminino i client
wait $client1PID;
wait $client2PID;

#Termino server con SIGHUP
pkill -1 -f main.out