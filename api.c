#include "client_api.h"
#include "commProtocol.h"
#include "list.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define MAX_LEN 108

//Codici flags
#define O_CREATE 01
#define O_LOCK 10

#define SET_LO if (lastOperation==1) lastOperation=0


extern msec;
extern expelledFiles;
int clientSFD;
/* Viene settata a 1 quando una open viene fatta con successo e
  rimessa a 0 all'operazione successiva; usata per check nella 
  funzione writeFile */
int lastOperation = 0;

// Lista in cui segno i file correntemente aperti dal client
strNode * openFiles = NULL;

/* Funzione per inviare richieste al server e leggere la risposta;
    prende come argomenti il buffer bufferToWrite contenente i byte da inviare, la sua
    taglia bufferSize, il buffer bufferToRead in cui inserire i byte di risposta e il
    numero di byte da allocarvi.
    In caso di fallimento restituisce -1 e setta errno, in caso di successo restituisce
    la taglia del buffer letto.
*/
ssize_t writeAndRead(void * bufferToWrite, void ** bufferToRead, size_t bufferSize, size_t answer);

int openConnection(const char* sockname, int msec, const struct timespec abstime) {     //see timespec_get (da settare nel client prima della chiamata)
    long int total_t=0;                     //tempo totale passato da confrontare con il tempo limite

    int result;                             //risultato della connessione

    struct timespec start;
    struct timespec end;

    clientSFD = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;

    int name_len = strlen(sockname);                        //controllo che il nome della seocket non sfori la lunghezza massima
    if (name_len > MAX_LEN) {
        fprintf(stderr, "Nome socket troppo lungo\n" );
        return -1;
    } 
    strcpy(sa.sun_path, sockname);    

    while ( (result=connect(clientSFD, (struct sockaddr*)&sa, sizeof(sa)))!=0 && total_t < abstime.tv_nsec ){
        timespec_get(&start, TIME_UTC);
        if (errno != 0 || errno != ENOENT) {
                return result;
        }
        sleep(msec * 0.001);
        errno=0;
        timespec_get(&end, TIME_UTC);
        total_t = (start.tv_nsec - end.tv_nsec);
    }
    return result;

}

int closeConnection(const char* sockname) {
    int res;
    res = close(clientSFD);     //setta errno, eventuale perror in client
    return res;
}

int openFile(const char* pathname, int flags){

    int fNameLen= strlen(pathname);
    if(fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;       //nome file troppo lungo
        return -1;
    }

    void * buffer;
    void * buffRead = malloc(MAX_BUF_SIZE); 
    size_t nToWrite;
    if ( flags != 0) {
        switch (flags){

            // creo un file
            case O_CREATE:

                nToWrite = sizeof(char)*(strlen(PUB_CREATE)+fNameLen);  //buffer da inviare a server
                buffer = malloc(nToWrite);
                strcpy (buffer, PUB_CREATE);
                strcat (buffer,pathname);

                if (writeAndRead(buffer, &buffRead, nToWrite, MAX_BUF_SIZE)==-1) return -1;
                break;

            // apro un file locked
            case O_LOCK:
                buffer = malloc(sizeof(char)*(strlen(PUB_CREATE)+strlen(pathname)));
                strcpy (buffer, "op,fl,");
                strcat (buffer,pathname);
                if (write(clientSFD, &buffer, sizeof(buffer))==-1) {
                    return -1;
                }
                break;

            // creo un file locked
            case O_CREATE | O_LOCK :

                buffer = malloc(sizeof(char)*(strlen(PRIV_CREATE)+strlen(pathname)));
                strcpy (buffer, PRIV_CREATE);
                strcat (buffer,pathname);
                nToWrite = sizeof(char)*(strlen(PRIV_CREATE)+strlen(pathname));

                if (writeAndRead(buffer,&buffRead, nToWrite, MAX_BUF_SIZE)==-1) return -1;
                break;

            default:
                fprintf(stderr,"Flags non valide\n");
                break;
        }
    }

    // apro un file
    else {
        nToWrite = sizeof(char)*(strlen(OPEN)+strlen(pathname));
        buffer = malloc(nToWrite);
        strcpy (buffer, OPEN);
        strcat (buffer,pathname);

        if (writeAndRead(buffer,&buffRead, nToWrite, MAX_BUF_SIZE)==-1) return -1;
    }

    int answer = atoi((char *)buffRead);
    free(buffer);
    free(buffRead);

    // si è provato a creare un file già esistente
    if(answer==FAILURE && flags==O_CREATE) {
        errno = EEXIST;
        return -1;
    }

    // si è provato ad aprire file non esistente
    if(answer == FAILURE && flags==0){
        errno = ENOENT;
        return -1;
    }

    if (answer==SUCCESS) lastOperation = 1;
    openFiles=addString(pathname, openFiles);
    return answer;
}

int readFile(const char * pathname, void ** buf, size_t* size) {

    // Controllo lunghezza nome
    int fNameLen = strlen (pathname);
    if(fNameLen>MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    //Controllo se il file è già aperto
    int fState = searchString(pathname,openFiles);
    if (fState ==-1) openFile(pathname, 0);   // Se non è presente lo apro
    if (fState == 0) {  // Se è stato chiuso in precedenza non posso più operare sul file
        errno = EPERM;
        return -1;
    }

    //Preparo richiesta al server
    size_t nToWrite = sizeof(char)*(strlen(READ)+fNameLen);
    void * buffer = malloc(nToWrite);
    strcpy (buffer, READ);
    strcat(buffer, pathname);
    ssize_t result;

    //Invio richiesta
    result = writeAndRead(buffer, buf, nToWrite, MAX_FILE_SIZE);
    SET_LO;
    free (buffer);
    return result;
}

int writeFile(const char* pathname, const char* dirname){

    // Controllo lunghezza nome
    size_t fNameLen = strlen(pathname);
    if (fNameLen < MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Controllo se posso operare sul file
    int fileS = searchString (pathname, openFiles);
    if (fileS == 0) {
        errno = EPERM;
        return -1;
    }

    // Apro file in locale 
    FILE * fToWrite; 
    if ((fToWrite = fopen(pathname, "r+"))== NULL) return -1;

    // Richiesta apertura file al server
    int res = openFile(pathname, 0);
    if (res ==-1) {
        if (errno==ENOENT) { 
            res = openFile(pathname, O_CREATE);  //Se il file non è già presente, lo creo
        }
        else {
            fclose(fToWrite); 
            return -1;
        }
    }
    
    // Trovo la grandezza del file
    struct stat * fSt;
    if (stat(pathname, fSt)==-1) {
        fclose(fToWrite); 
        return -1;
    }
    size_t fileSize = fSt->st_size;
    // Controllo che sia nei limiti
    if(fileSize > MAX_FILE_SIZE) {
        fclose(fToWrite); 
        errno = EFBIG;
        return -1;
    }

    // Chiamo appendToFile per completare la scrittura
    if (appendToFile(pathname, fToWrite, fileSize, expelledFiles)==-1) {
        fclose(fToWrite);
        return -1;
    }
    return 0;
}

int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname){
    
    //controllo validità nome
    size_t fNameLen = strlen(pathname);
    if (fNameLen < MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }
    
    //controllo presenza del file
    int openRes;
    if((openRes=openFile(pathname, 0)) == -1) return -1;

    //invio richiesta operazione e buffer al server
    size_t nToSend = sizeof(char) * (strlen(WRITE) + size + fNameLen);
    void * bufferToSend = malloc (nToSend);
    strcpy (bufferToSend, WRITE);       //Buffer: operazione, nome file, contenuto di cui fare append
    strcat(bufferToSend, pathname);
    strcat(bufferToSend, buf);
    void * buffRead = malloc(MAX_BUF_SIZE);
    if (writeAndRead(bufferToSend, &buffRead, nToSend, MAX_BUF_SIZE) ==-1) return -1;
    
    // leggo codice di risposta: Success o Failure
    int res = atoi((char *) buffRead);
    if (res == SUCCESS) SET_LO;
    return res;
}

int removeFile(const char* pathname){

    // Controllo lunghezza nome
    int fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Preparo richiesta
    size_t bufferSize = sizeof(char) * (strlen(RMV)+fNameLen);
    void * buffer = malloc(bufferSize);
    void * buffRead = malloc(MAX_BUF_SIZE);

    int result;
    // Se la comunicazione col server è fallita riporto errore dopo aver deallocato
    if ((result = writeAndRead(buffer, buffRead, bufferSize, MAX_BUF_SIZE))==-1) {
        free (buffer);
        free(buffRead);
        return -1;
    }
    // Se ha avuto successo riporto l'esito della rimozione
    else {
        int answer = atoi((char *) buffRead);
        free (buffer);
        free (buffRead);
        return answer;
    }
    
    
}

ssize_t writeAndRead(void * bufferToWrite, void ** bufferToRead, size_t bufferSize, size_t answer){
                
    //write finchè non ho scritto tutti i byte del buffer
    size_t nToWrite = bufferSize;
    ssize_t nWritten;
    while ( nToWrite > 0) {
        if ((nWritten = write(clientSFD, bufferToWrite, nToWrite))==-1) return -1;
            nToWrite -= nWritten;
        }


    size_t nToRead = answer;
    ssize_t nRead;
    ssize_t totByteRead = 0;

    //aspetto il tempo di risposta
    sleep(msec * 0.001);

    //leggo la risposta del server
    while (nToRead > 0) {
        if((nRead=read(clientSFD,bufferToRead,nToRead))==-1) return -1;
        totByteRead+=nRead;
        if (nRead == 0) break;
        nToRead -= nToRead;
    }

    return totByteRead;

}
