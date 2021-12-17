#!/bin/bash

#Entro nella cartella con il file log.txt
cd $1

#Ogni thread salva nel file di logging il proprio TID (sia worker che main)
grep -w 'TID' log.txt | (while read -r line ; do
    #Estraggo TID
    tid=${line#TID }
    #Per ognuno conto le richieste servite
    reqPerTid=$(grep -w -c $tid log.txt)
    echo TID $tid ha servito $reqPerTid richieste
done)

#Conto numero di lock, open-lock, unlock, close e expulsion
lock=$(grep -w -c 'Lock' log.txt)
open_lock=$(grep -w -c 'Open-lock' log.txt)
unlock=$(grep -w -c 'Unlock' log.txt)
close=$(grep -w -c 'Remove' log.txt)
expel=$(grep -w -c 'Expel' log.txt)

#Numero di file letti e scritti con totale in byte
bytesRead=0
noFilesRead=0
bytesWritten=0
noFilesWritten=0
#Numero di file nello storage e variabile per salvare il massimo
noFilesStorage=0
maxStorage=0
#Bytes salvati nello storage e massimo raggiunto
bytesStorage=0
maxBytes=0
#Numero di connessioni accettate contemporaneamente e valore massimo raggiunto
simClients=0
maxSimClients=0

grep -w 'Read\|Write\|Remove\|Expel\|Create-unlocked\|Create-locked\|Accepted\|Closed' log.txt | (while read -r line ; do

    # Lettura
    if [[ $line == Read* ]] ; then 
        #Aggiorno numero file e byte letti
        noFilesRead=$(($noFilesRead+1))
        tmp=${line#*Bytes }
        toadd=${tmp%: Process*}
        bytesRead=$(($bytesRead+$toadd))
    fi

    #Scrittura
    if [[ $line == Write* ]] ; then
        #Aggiorno numero file e byte scritti
        noFilesWritten=$(($noFilesWritten+1))
        tmp=${line#*Bytes }
        toadd=${tmp%: Process*}
        bytesWritten=$(($bytesWritten+$toadd))

        #Aggiorno byte occupati nello storage e, se superato, il massimo raggiunto
        bytesStorage=$(($bytesStorage+$toadd))
        if [[ $bytesStorage > $maxBytes ]]; then
            maxBytes=$bytesWritten
        fi
    fi

    #Rimozione o espulsione
    if [[ $line == Remove* || $line == Expel* ]] ; then
        #Aggiorno numero file e byte letti
        tmp=${line#*Bytes }
        tosub=${tmp%: Process*}
        noFilesStorage=$(($noFilesStorage-1))
        bytesStorage=$(($bytesStorage-$tosub))
    fi

    #Creazione di un file prima della scrittura del contenuto
    if [[ $line == Create-locked* || $line == Create-unlocked* ]] ; then
        #Aggiorno numero di file nello storage e numero massimo, se superato
        noFilesStorage=$(($noFilesStorage+1))
        if [[ $noFilesStorage > $maxStorage ]]; 
            then maxStorage=$noFilesStorage 
        fi
    fi

    #Connessione accettata
    if [[ $line == Accepted* ]] ; then
        simClients=$(($simClients+1))
        if [[ $simClients > $maxSimClients ]] ; then 
            maxSimClients=$simClients 
        fi
    fi

    #Connessione chiusa
    if [[ $line == Closed* ]] ; then 
        simClients=$(($simClients-1)) 
    fi

done

if [[ $noFilesRead > 0 ]] ; then
    mediumBytesRead=$(($bytesRead/$noFilesRead))
    echo Letture: $noFilesRead con una media di $mediumBytesRead bytes
fi

if [[ $noFilesWritten > 0 ]] ; then
    mediumBytesWritten=$(($bytesWritten/$noFilesWritten))
    echo Scritture: $noFilesWritten con una media di $mediumBytesWritten bytes
fi
echo Lock: $lock
echo Open-lock: $open_lock
echo Unlock: $unlock
echo Chiusure file: $close
echo Dimensione massima in bytes dello storage: $maxBytes
echo Massimo numero di file nello storage: $maxStorage
echo Espulsioni effettutate: $expel
echo Numero massimo di connessioni contemporanee: $maxSimClients)