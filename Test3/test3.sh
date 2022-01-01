#!/bin/bash

# Avvio 10 client

# Write
./client.out -f server -D ./Expelled -w ./Write,6 &
# MyDir
./client.out -f server -w ./MyDir,6 -u ./MyDir/myFile7 &
# Write_2nd
./client.out -f server -W ./Write_2nd/myFile8,./Write_2nd/myFile1,./Write_2nd/myFile2,./Write_2nd/myFile7,./Write_2nd/myFile5 &
# MyDir_2nd, Write_3
./client.out -f server -d ./Read -W ./MyDir_2nd/myFile4,./MyDir_2nd/myFile5 -r ./Write_3/w8 -w ./Write_3,2 &
# ___
./client.out -f server -r ./Write/w4,./Write/w5,./Write/w3 -c ./Write/w4,./Write/w5 &
# MyDir_3
./client.out -f server -l ./MyDir/myFile2 -u ./MyDir/myFile5 -w ./MyDir_3,3 &
# MyDir_4
./client.out -f server -l ./MyDir/myFile7 -c ./MyDir/myFile7 -W ./MyDir_4/myFile7 -l ./MyDir/myFile3 -u ./MyDir/myFile3 &
#___
./client.out -f server -d ./Read -r ./MyDir/myFile1,./MyDir/myFile2,./MyDir/myFile3 -c ./MyDir/MyFile2,./MyDir/myFile1 &
# Write_4
./client.out -f server -D ./Expelled -w ./Write_4,5 &
# Write_5
./client.out -f server -d ./Read -w ./Write_5,6 &


# Setto una variabile per timer di 30 secondi
end=$((SECONDS+30))
until [ $SECONDS -eq $end ]; do
    # Conto i client attivi 
    clients="$(pgrep client | wc -l)"
    tocall=$((10-$clients))
    # Se sono meno di 10 ne avvio di nuovi
    if [ $tocall -ge 1 ]; then
        for ((i=0;i<$tocall;i++)); do
            ./client.out -f server -d ./Read -R5 &
        done
    fi
done

# Invio SIGINT al server e fermo i client rimanenti 
pkill -2 -f main.out
kill -9 $(pgrep client)
