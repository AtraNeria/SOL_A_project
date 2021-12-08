#!/bin/bash

#Entro nella cartella con il file log.txt
echo $1
cd $1

#Contro numero di lock, open-lock, unlock, close e expulsion
lock=$(grep -ow 'Lock,' log.txt | wc -l)
open_lock=$(grep -ow 'Open-lock,' log.txt | wc -l)
unlock=$(grep -o 'Unlock' log.txt | ec -l)
close=$(grep -o 'Remove' log.txt | ec -l)
expel=$(grep -o 'Expel' log.txt | ec -l)
