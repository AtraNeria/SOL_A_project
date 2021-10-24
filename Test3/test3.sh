#!/bin/bash

# Avvio 10 client
./client.out -f server -D ./Expelled -w ./Write,4 &
./client.out -f server -w ./MyDir,2 &
./client.out -f server -W ./Write/w8,./Write/w1 &
./client.out -f server -d ./Read -W ./MyDir/myFile4,./MyDir/myFile5 -r ./Write/w8 -w ./Write,2 &
./client.out -f server -r ./Write/w4,./Write/w5,./Write/w3 -c ./Write/w4,./Write/w5 &
./client.out -f server -l ./MyDir/myFile2 -u ./MyDir/myFile5 -w ./MyDir,3 &
./client.out -f server -l ./MyDir/myFile7 -c ./MyDir/myFile7 -W ./MyDir/myFile7 -l ./MyDir/myFile3 -u ./MyDir/myFile3 &
./client.out -f server -d ./Read -r ./MyDir/myFile1,./MyDir/myFile2,./MyDir/myFile3 -c ./MyDir/MyFile2,./MyDir/myFile1 &
./client.out -f server -D ./Expelled -w ./Write,5 &
./client.out -f server -d ./Read -w ./MyDir,6 &

# Setto una variabile per timer di 30 secondi
end=$((SECONDS+30))
until [ $SECONDS -eq $end ]; do
    # Conto i client attivi 
    clients="$(pgrep client | wc -l)"
    tocall=$((10-$clients))
    # Se sono meno di 10 ne avvio di nuovi
    if [ $tocall -ge 0 ]; then
        for ((i=0;i<$tocall;i++)); do
            ./client.out -f server -d ./Read -w ./MyDir,2 -w ./Write,2 -R1 &
        done
    fi
done

# Invio SIGINT al server e fermo i client rimanenti 
pkill -2 -f main.out
kill -9 $(pgrep client)
