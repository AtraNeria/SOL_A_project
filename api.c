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
#define FREE_RET {free(buffer); free(buffRead); return -1;}



extern int msec;
extern char * expelledFiles;
extern char * dirReadFiles;
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

/* Salva nella cartella dirName il file pathname letto il cui contenuto è in buffer, di grandezza size.
    Restituisce -1 in caso di fallimento settando errno, 0 in caso di successo
*/
int saveFile (void * buffer, const char * dirName, const char * pathname, size_t size);

/* Legge size bytes di buffer e restituisce la risposta contenutavi.
    
*/
int getAnswer(void ** buffer, size_t size);

int openConnection (const char* sockname, int msec, const struct timespec abstime) {     //see timespec_get (da settare nel client prima della chiamata)
    long int total_t=0;                     //tempo totale passato da confrontare con il tempo limite

    int result;                             //risultato della connessione

    struct timespec start;
    struct timespec end;

    clientSFD = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;

    int name_len = strlen(sockname);                        //controllo che il nome della socket non sfori la lunghezza massima
    if (name_len > MAX_LEN) {
        fprintf(stderr, "Nome socket troppo lungo\n" );
        return -1;
    } 

    // setto l'indirizzo a cui connettersi
    strcpy(sa.sun_path, "./");              
    strcat(sa.sun_path, sockname);
    sa.sun_family=AF_UNIX;              

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

int closeConnection (const char* sockname) {
    int res;

    // Richiesta al server
    char * closeReq ="256,-0-";
    size_t writeSize = sizeof(char)*strlen(closeReq);
    void * buffer = malloc (writeSize);
    strcpy (buffer,closeReq);
    void * buffRead = malloc(MAX_BUF_SIZE);

    // Invio richiesta e chiusura locale
    int sr = writeAndRead(buffer,&buffRead,writeSize,MAX_BUF_SIZE);
    res = (sr && close(clientSFD));
    free(buffer);
    //free(buffRead);                     // SEG FAULT
    return res;
}

int openFile (const char* pathname, int flags){

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
                nToWrite = sizeof(char)*(strlen(PUB_CREATE)+fNameLen+strlen(EOBUFF));  //buffer da inviare a server
                buffer = malloc(nToWrite);
                strcpy (buffer, PUB_CREATE);
                strcat (buffer,pathname);
                strcat (buffer, EOBUFF);
                if (writeAndRead(buffer, &buffRead, nToWrite, MAX_BUF_SIZE)==-1) FREE_RET;
                break;


            /* apro un file locked
            case O_LOCK:
                buffer = malloc(sizeof(char)*(strlen(PUB_CREATE)+strlen(pathname)));
                strcpy (buffer, "op,fl,");
                strcat (buffer,pathname);
                if (write(clientSFD, &buffer, sizeof(buffer))==-1) {
                    FREE_RET;
                }
                break;

            // creo un file locked
            case O_CREATE | O_LOCK :
                buffer = malloc(sizeof(char)*(strlen(PRIV_CREATE)+strlen(pathname)));
                strcpy (buffer, PRIV_CREATE);
                strcat (buffer,pathname);
                nToWrite = sizeof(char)*(strlen(PRIV_CREATE)+strlen(pathname));

                if (writeAndRead(buffer,&buffRead, nToWrite, MAX_BUF_SIZE)==-1) FREE_RET;
                break;*/

            default:
                fprintf(stderr,"Flags non valide\n");
                break;
        }
    }

    // apro un file
    else {
        nToWrite = sizeof(char)*(strlen(OPEN)+strlen(pathname)+strlen(EOBUFF));
        buffer = malloc(nToWrite);
        strcpy (buffer, OPEN);
        strcat (buffer,pathname);
        strcat (buffer,EOBUFF);
        if (writeAndRead(buffer,&buffRead, nToWrite, MAX_BUF_SIZE)==-1) FREE_RET;
    }

    int answer = getAnswer(&buffRead, MAX_BUF_SIZE);
    //free (buffRead);     SEG FAULT

    if(answer == FAILURE && flags==O_CREATE) {
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

int readFile (const char * pathname, void ** buf, size_t* size) {

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
    
    //Se ho avuto successo salvo il file nella cartella specificata per i file letti se specificata
    if (result!=-1 && dirReadFiles!=NULL) {
        if (saveFile(buffer, NULL, pathname, MAX_FILE_SIZE)==-1) {
            free (buffer);
            return -1;
        }        
    }
    free (buffer);
    return result;
}

int readNFiles (int N, const char* dirname) {

    // Inserisco N nella stringa di richiesta
    char nF [32];
    sprintf (nF,"%d",N);
    size_t writeSize = sizeof(char)*(strlen(RD_MUL)+strlen(nF));
    void * buffer = malloc (writeSize);
    strcpy (buffer, RD_MUL);
    strcat (buffer, nF);

    size_t readS = MAX_BUF_SIZE+MAX_FILE_SIZE;
    void * buffRead = malloc (readS);

    // Invio richiesta al server
    size_t nToWrite = writeSize;
    ssize_t nWritten;
    
    while (nToWrite>0) {
        if ((nWritten = write(clientSFD, buffer, nToWrite))==-1) FREE_RET
            nToWrite -= nWritten;
        }
    

    // Ciclo per leggere un file alla volta e copiarlo, finchè non ne leggo N o arrivo all'ultimo inviato
    int j = 1;
    while (j<=N && buffRead!=NULL) {

        char * tk; // token tmp
        size_t fileSize; // taglia del file
        char infoIndex[MAX_BUF_SIZE]; // header completo
        char pathName[MAX_NAME_LEN];  // nome file

        // Leggo il primo file e lo carico in un buffer void*
        size_t nToRd=readS;
        ssize_t nRead;
        while (nToRd>0) {
            if ((nRead=read(clientSFD,buffRead,readS))==-1) FREE_RET
            if (nRead == 0) break;
            nToRd-=nRead;
        }

        // Estraggo il nome da buffer
        tk = strtok(buffRead,",");
        strcpy(pathName, tk);

        strcpy(infoIndex,tk);
        strcat(infoIndex,",");

        // Estraggo la taglia
        tk = strtok(NULL,"\n");
        fileSize = atol(tk);

        strcat(infoIndex,tk);
        strcat(infoIndex,"\n");


        // Sposto il puntatore a inizio contenuto file
        buffRead+=strlen(infoIndex);
        // Salvo il file
        saveFile(buffRead, dirname, pathName, fileSize);

        j++;

    } 

    free (buffer);
    free (buffRead);

    // Errore se il server è vuoto e non ha passato nulla
    if (j==1 && j<=N && buffRead==NULL) {
        errno = ENOENT;
        return -1;
    }

    return j;
    
}

int writeFile (const char* pathname, const char* dirname){

    printf("%s\n",pathname);
    // Controllo lunghezza nome
    size_t fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
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
    if ((fToWrite = fopen(pathname, "r+") )== NULL) {
        return -1;
    }

    // Richiesta apertura file al server
    int res = openFile(pathname, 0);

    if (res == -1) {
        if (errno==ENOENT) { 
            //Se il file non è già presente, lo creo
            res = openFile(pathname, O_CREATE);
        }
        else {
            // Se il file è presente non lo sovrascrivo
            fclose(fToWrite);                   
            return -1;
        }
    }

    // Trovo la grandezza del file
    struct stat * fSt = malloc(sizeof(struct stat));
    if (stat(pathname, fSt)==-1) {
        fclose(fToWrite); 
        return -1;
    }

    size_t fileSize = fSt->st_size;
    free(fSt);
    // Controllo che sia nei limiti
    if(fileSize > MAX_FILE_SIZE) {
        fclose(fToWrite); 
        errno = EFBIG;
        return -1;
    }

    // Chiamo appendToFile per completare la scrittura
    if (appendToFile(pathname, fToWrite, fileSize, expelledFiles)==-1) {            //HERE
        fclose(fToWrite);
        return -1;
    }

    if (fclose(fToWrite) == EOF) return -1;
    return 0;
}

int appendToFile (const char* pathname, void* buf, size_t size, const char* dirname){
    //controllo validità nome
    size_t fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Creo stringa di richiesta: operazione, size, nomefile \n
    char opString [MAX_BUF_SIZE];
    sprintf(opString, "%d,%li,%s,",WR,size,pathname);

    //Aggiungo alla richiesta il contenuto da scrivere
    size_t nToSend = (sizeof(char) * (strlen(opString))) + size;
    void * buffer = malloc (nToSend);

    strcpy (buffer, opString);
    strcat(buffer, buf);
    strcat(buffer, EOBUFF);

    // Invio
    void * buffRead = malloc(MAX_BUF_SIZE);
    if (writeAndRead(buffer, &buffRead, nToSend, MAX_BUF_SIZE) ==-1) FREE_RET

    // leggo codice di risposta: Success o Failure
    int res = getAnswer(&buffRead,MAX_BUF_SIZE);

    free (buffer);
    //free (buffRead);      //SEG FAULT
    return res;
}

int removeFile (const char* pathname){

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
    if ((result = writeAndRead(buffer, &buffRead, bufferSize, MAX_BUF_SIZE))==-1) {
        FREE_RET
    }
    // Se ha avuto successo riporto l'esito della rimozione
    else {
        int answer = getAnswer(buffRead,MAX_BUF_SIZE);
        free (buffer);
        free (buffRead);
        // Se è stato rimosso da server chiuso il file da lato client
        if (answer == SUCCESS) closeFile (pathname);
        return answer;
    }
    
    
}

int closeFile (const char* pathname){
    // Controllo lunghezza nome
    int fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Chiudo in locale il file
    int close = closeString (pathname, &openFiles);
    return close;
}

ssize_t writeAndRead (void * bufferToWrite, void ** bufferToRead, size_t bufferSize, size_t answer){
    
    //write finchè non ho scritto tutti i byte del buffer
    size_t nToWrite = bufferSize;
    ssize_t nWritten=1;

    while ( nToWrite > 0 && nWritten!=0) {
        if ((nWritten = write(clientSFD, bufferToWrite, nToWrite))==-1) return -1;
            nToWrite -= nWritten;
        }

    size_t nToRead = answer;
    ssize_t nRead = 1;
    ssize_t totBytesRead = 0;

    //aspetto il tempo di risposta
    sleep(msec * 0.001);

    //leggo la risposta del server
    while (nToRead > 0 && nRead!=0) {                           
        if (totBytesRead!=0 && bufferCheck(&bufferToRead)==1) break;
        if ((nRead = read(clientSFD,bufferToRead,nToRead)==-1)) return -1;
        totBytesRead+=nRead;
        nToRead -= nRead;
    }
    return totBytesRead;
}

int saveFile (void * buffer, const char * dirName, const char * pathname, size_t size) {

    //Apro file e se non esiste lo creo
    char * fpath=NULL;
    // Se non è specificata una cartella uso quella per i file letti
    if(dirName == NULL) {
        strcpy(fpath, dirReadFiles);
        strcat(fpath,"/");
        strcat(fpath,pathname);
    }
    else {
        strcpy(fpath, dirName);
        strcat(fpath,"/");
        strcat(fpath,pathname);
    }
    FILE * f;
    if ((f = fopen(fpath, "w+"))==NULL) return -1;

    // Copio il contenuto del buffer
    fwrite(buffer, sizeof(char), size, f);
    if (fclose(f) == EOF) return -1;
    return 0;

}

int getAnswer(void ** buffer, size_t size) {
    char * string = malloc(sizeof(char)*size);
    memcpy(string, (char*)buffer, sizeof(char)*size);

    char * toTok;
    toTok = strtok(string,EOBUFF);

    int res = atoi(toTok);
    free (string);
    if (res==1) return -1;
    return res;
}
