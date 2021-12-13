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

#define FREE_RET \
    {free(toFreeWrite); free(toFree); return -1;}
#define PROP(op,file,res,byte) if (pOpt_met) {printOpRes(op,file,res,byte);}


extern int pOpt_met;
extern int msec;
extern char * expelledFiles;
extern char * dirReadFiles;
int clientSFD;


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

/* Legge da server un file preceduto da header per l'operazione op.
    Restituisce 0 se ha successo, -1 altrimenti.
    In caso di successo il puntatore content punterà al contenuto del file, 
    pathname al suo nome e size alla sua taglia
*/
int getHeader_File (void ** content, void ** pathname, size_t * size, int op);

/* Legge i file vittima espulsi dal server. 
    Se è stata specificata una cartella con l'opzione -D vengono salvati.
    In caso di successo restituisce 0, -1 altrimenti
*/
int getExpelledFile ();

/* Salva nella cartella dirName il file pathname letto il cui contenuto è in buffer, di grandezza size.
    Restituisce -1 in caso di fallimento settando errno, 0 in caso di successo
*/
int saveFile (void * buffer, const char * dirName, const char * pathname, size_t size);

/* Controlla se il file pathname è aperto e disponibile per ulteriori operazioni.
    In caso non sia aperto tenta di aprirlo, in stato locked se la flag O_LOCK viene specificata,
    in stato unlocked altrimenti.
    Restituisce 0 se il file è aperto correttamente, -1 se l'operazione fallisce o
    il file era stato chiuso in precedenza.
*/
int checkOpen (const char * pathname,int flag);

/* Legge size bytes di buffer e restituisce la risposta contenutavi alla richiesta query.
*/
ssize_t getAnswer(void ** buffer, size_t size, int query);


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
        sleep(msec * 0.001);
        errno=0;
        timespec_get(&end, TIME_UTC);
        total_t = (start.tv_nsec - end.tv_nsec);
    }
    return result;
}

int closeConnection (const char* sockname) {

    /*int res;
    // Richiesta al server
    char * closeReq = calloc(MAX_BUF_SIZE,sizeof(char));
    strcpy(closeReq, CLOSE_S);

    size_t writeSize = sizeof(char)*strlen(closeReq)+1;
    void * buffer = malloc (writeSize);
    memset (buffer, 0, writeSize);
    void * toFreeWrite = buffer;
    strcpy (buffer,closeReq);
    void * buffRead = malloc(MAX_BUF_SIZE);
    memset (buffRead, 0, MAX_BUF_SIZE);
    void * toFree = buffRead;

    // Invio richiesta e chiusura locale
    int sr = writeAndRead(buffer,&buffRead,writeSize-1,MAX_BUF_SIZE);
    res = (sr && close(clientSFD));
    free (toFreeWrite);
    free (closeReq);
    free (toFree);
    return res;*/
    return close(clientSFD);
}

int openFile (const char* pathname, int flags){

    int fNameLen = strlen(pathname);
    if(fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;       //nome file troppo lungo
        return -1;
    }

    void * buffer;
    void * toFreeWrite;
    void * buffRead = malloc(MAX_BUF_SIZE); 
    memset (buffRead, 0, MAX_BUF_SIZE);
    void * toFree = buffRead;
    size_t nToWrite;
    ssize_t res;

    if ( flags != 0) {
        switch (flags){

            // creo un file
            case O_CREATE:{

                nToWrite = sizeof(char)*(strlen(PUB_CREATE)+fNameLen) + 1;  //buffer da inviare a server
                buffer = malloc(nToWrite);
                memset (buffer, 0, nToWrite);
                toFreeWrite = buffer;
                strcpy (buffer, PUB_CREATE);
                strcat (buffer,pathname);

                if (writeAndRead(buffer, &buffRead, nToWrite-1, MAX_BUF_SIZE)==-1) FREE_RET;
                res = getAnswer(&buffRead,MAX_BUF_SIZE,PUC);

                // Se il server ha necessità di espellere file prima di crearne uno
                while (res==EXPEL || res==WAIT) {

                    //Se devo ricevere file espulso
                    if (res==EXPEL && getExpelledFile()==-1) FREE_RET
                    //Se non ho i permessi per riceverlo
                    if (res==WAIT && sendAnswer(clientSFD,SUCCESS)==-1) FREE_RET

                    // Leggo risposta successiva
                    memset(toFree,0,MAX_BUF_SIZE);
                    buffRead=toFree;
                    size_t nToRead = MAX_BUF_SIZE;
                    ssize_t nRead = 1;
                    ssize_t totBytesRead = 0;
                    while (nToRead > 0 && nRead!=0) {
                        if (totBytesRead!=0 && bufferCheck(buffRead)==1) break;
                        if ((nRead = read(clientSFD,buffRead+totBytesRead,nToRead)==-1)) {
                            free(toFreeWrite);
                            free(toFree);
                            PROP(PUC,pathname,-1,0)
                            return -1;
                        }
                        totBytesRead += nRead;
                        nToRead -= nRead;
                    }

                    // Rimango nel loop finchè il server deve espellere file
                    char tok [48];
                    strcpy(tok,buffRead);
                    res = atoi(strtok(tok,EOBUFF));
                }
                break;
            }

            // apro file in stato locked
            case O_LOCK:{

                nToWrite = sizeof(char) * (strlen(OP_LOCK)+strlen(pathname)) + 1;
                buffer = malloc(nToWrite);
                memset (buffRead, 0, nToWrite);
                toFreeWrite = buffer;
                strcpy (buffer, OP_LOCK);
                strcat (buffer,pathname);
                if (writeAndRead(buffer,&buffRead, nToWrite-1, MAX_BUF_SIZE)==-1) FREE_RET;
                break;
            }

            // creo un file locked
            case (O_CREATE|O_LOCK):{

                //buffer da inviare a server
                nToWrite = sizeof(char)*(strlen(PRIV_CREATE)+fNameLen);
                buffer = malloc(nToWrite+1);
                memset (buffer, 0, nToWrite+1);
                toFreeWrite = buffer;
                strcpy (buffer, PRIV_CREATE);
                strcat (buffer,pathname);
                if (writeAndRead(buffer,&buffRead, nToWrite, MAX_BUF_SIZE)==-1) FREE_RET;
                res = getAnswer(&buffRead,MAX_BUF_SIZE,PRC);

                while (res==EXPEL || res==WAIT) {

                    //Se devo ricevere file espulso
                    if (res==EXPEL && getExpelledFile()==-1) FREE_RET
                    //Se non ho i permessi per riceverlo
                    if (res==WAIT && sendAnswer(clientSFD,SUCCESS)==-1) FREE_RET

                    // Leggo risposta successiva
                    void * nextRead = calloc(MAX_BUF_SIZE,1);
                    void * nrFree = nextRead;
                    size_t nToRead = MAX_BUF_SIZE;
                    ssize_t nRead = 1;
                    ssize_t totBytesRead = 0;
                    while (nToRead > 0 && nRead!=0) {
                        if (totBytesRead!=0 && bufferCheck(nextRead)==1) break;
                        if ((nRead = read(clientSFD,nextRead+totBytesRead,nToRead)==-1)) {
                            free(toFreeWrite);
                            free(toFree);
                            free(nrFree);
                            PROP(PRC,pathname,-1,0)
                            return -1;
                        }
                        totBytesRead += nRead;
                        nToRead -= nRead;
                    }

                    // Rimango nel loop finchè il server deve espellere file
                    res = atoi((char*)nrFree);
                    free(nrFree);
                }
                break;
            }

            default:
                fprintf(stderr,"Flags non valide\n");
                break;
        }
    }

    // apro un file unlocked
    else {
        nToWrite = sizeof(char) * (strlen(OPEN)+strlen(pathname)) + 1;
        buffer = malloc(nToWrite);
        memset (buffRead, 0, nToWrite);
        toFreeWrite = buffer;
        strcpy (buffer, OPEN);
        strcat (buffer,pathname);
        if (writeAndRead(buffer,&buffRead, nToWrite-1, MAX_BUF_SIZE)==-1) FREE_RET;
    }

    int answer;
    if (flags==0 || flags==O_LOCK) answer = getAnswer(&buffRead, MAX_BUF_SIZE, OP);
    else answer = res;
    free (toFreeWrite);
    free (toFree);

    // si è provato a creare un file già esistente
    if(answer == FAILURE && flags==O_CREATE) errno = EEXIST;

    // si è provato ad aprire file non esistente
    if(answer == FAILURE && (flags==0 || flags==O_LOCK)) errno = ENOENT;
    // si è aperto un file con successo
    if (answer==SUCCESS && searchString(pathname,openFiles)==-1) {
        openFiles=addString(pathname, openFiles);
    }

    //Se le stampe sono abilitate stampo l'esito
    if (flags==O_CREATE) PROP(PUC,pathname,answer,0)
    if (flags==O_LOCK) PROP(OPL,pathname, answer,0)
    if (flags==(O_CREATE|O_LOCK)) PROP(PRC,pathname,answer,0)
    if (flags==0) PROP(OP,pathname,answer,0)
    return answer;
}

int readFile (const char * pathname, void ** buf, size_t* size) {

    int answer = 0;
    // Controllo lunghezza nome
    int fNameLen = strlen (pathname);
    if (fNameLen>MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        answer = -1;
        goto retRead;
    }

    //Controllo se il file è aperto
    if (checkOpen(pathname,0)==-1) {
        answer = -1;
        goto retRead;
    }

    size_t nToWrite = sizeof(char)*(strlen(READ)+fNameLen) + 1;
    void * buffer = malloc(nToWrite);
    memset (buffer, 0, nToWrite);
    void * toFreeWrite = buffer;
    strcpy (buffer, READ);
    strcat(buffer, pathname);
    ssize_t result;

    // Alloco spazio per la risposta
    void * buff = malloc(sizeof(char)*32);
    memset (buff,0,sizeof(char)*32);
    void * freeRes = buff;

    //Invio richiesta
    if (writeAndRead(buffer, buff, nToWrite-1, sizeof(char)*32+EOB_SIZE)==-1) {
        free (toFreeWrite);
        free (freeRes);
        return -1;
    }
    char string[MAX_BUF_SIZE];
    strcpy (string, (char*)freeRes);  
    char * tok = strtok(string,EOBUFF);
    result = atol(tok);


    if(result==-1) {
        free(toFreeWrite);
        free(freeRes);
        return -1;
    }

    // Alloco spazio per il file
    void * content = malloc(result);
    memset (content,0,result);
    void * ptr = content;

    sendAnswer(clientSFD,SUCCESS);

    // Leggo il contenuto restante
    ssize_t nToRd = result+EOB_SIZE;
    ssize_t nRead = 1;
    size_t totBytes = 0;
    while (nToRd>0 && nRead!=0) {
        if (totBytes!=0 && bufferCheck(content)==1) break;
        if ((nRead = read(clientSFD,content+totBytes,nToRd)==-1)) {
            free (toFreeWrite);
            free (freeRes);
            free (ptr);
            return -1;
        }
        totBytes += nRead;
        nToRd -= nRead;
    }

    // Eventualmente salvo il file su disco
    if (dirReadFiles!=NULL) {
        if ((saveFile(ptr,dirReadFiles,pathname,result))==-1) {
            free (toFreeWrite);
            free (freeRes);
            free(ptr);
            answer = -1;
            goto retRead;
        }
    }
    
    // Assegno i puntatori richiesti dal client
    * buf = ptr;
    * size = result;
    free (toFreeWrite);
    free (freeRes);

    //Se le stampe sono abilitate stampo l'esito
    retRead:
    PROP(RD,pathname,answer,*size)
    return answer;
}

int readNFiles (int N, const char* dirname) {

    // Inserisco N nella stringa di richiesta
    // RDM,N£

    char nF [32];
    sprintf (nF,"%d",N);
    size_t writeSize = sizeof(char)*(strlen(RD_MUL)+strlen(nF))+1;
    void * buffer = malloc (writeSize);
    memset (buffer, 0, writeSize);
    void * toFreeWrite = buffer;
    strcpy (buffer, RD_MUL);
    strcat (buffer, nF);

    // Invio la richiesta al server il quale risponde con il numero di file disponibili
    void * buff = malloc(MAX_BUF_SIZE);
    void * tmp = buff;
    memset (buff, 0, MAX_BUF_SIZE);
    if (writeAndRead(buffer,&buff,writeSize-1,MAX_BUF_SIZE)==-1) {
        free(toFreeWrite);
        free(buff);
        return -1;
    }
    N = getAnswer(&buff,MAX_BUF_SIZE,RDM);
    //printf("Available files: %d\n",N);  //TEST
    free (tmp);
    free(toFreeWrite);

    // Riporto al client un errore se il server non ha file da inviare
    if (N==0) {
        errno=ENOENT;
        return -1;
    }

    // Altrimenti segnalo al server di procedere con l'invio dei file
    sendAnswer(clientSFD,SUCCESS);

    // Ciclo per leggere un file alla volta e copiarlo, finchè non ne leggo N
    int j = 1;
    while (j<=N) {
        void * content;
        void * pathname;
        size_t size;

        // Leggo il file preceduto da un header
        if (getHeader_File(&content,&pathname,&size,RDM)==-1) return -1;

        // Ricavo il nome per salvare eventualmente il file
        char * saveName = calloc (MAX_NAME_LEN, sizeof(char));
        if (nameFromPath(pathname,saveName)==-1) {
            free (saveName);
            free (content);
            return -1;
        }

        // Se specificata una cartella salvo il file
        if (dirname!=NULL) saveFile(content, dirname, saveName, size);

        //Se le stampe sono abilitate stampo l'esito
        PROP(RD,pathname,0,size)
        free (content);        
        free (saveName);
        j++;
        //Segnalo al server di poter proseguire col prossimo file
        sendAnswer(clientSFD,SUCCESS);
    } 
    return j;    
}

int writeFile (const char* pathname, const char* dirname){

    // Controllo lunghezza nome
    size_t fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        PROP(WR,pathname,-1,0)
        return -1;
    }

    // Apro file in locale 
    FILE * fToWrite; 
    if ((fToWrite = fopen(pathname, "r+") )== NULL) {
        PROP(WR,pathname,-1,0)
        return -1;
    }

    // Richiesta apertura file al server
    int res = checkOpen(pathname, O_LOCK);
    if (res == -1) {
        if (errno==ENOENT) {
            //Se il file non è già presente, lo creo
            if ((res = openFile(pathname, O_CREATE | O_LOCK))==-1) {    // BUG
                fclose(fToWrite);
                PROP(WR,pathname,-1,0)
                return -1;
            }
        }
        else {
            // Se il file è presente non lo sovrascrivo
            fclose(fToWrite);
            PROP(WR,pathname,-1,0)
            return -1;
        }
    }

    // Trovo la grandezza del file
    struct stat * fSt = calloc(1,sizeof(struct stat));
    if (stat(pathname, fSt)==-1) {
        fclose(fToWrite); 
        PROP(WR,pathname,-1,0)
        return -1;
    }

    size_t fileSize = fSt->st_size;
    free(fSt);
    // Controllo che sia nei limiti
    if(fileSize > MAX_FILE_SIZE) {
        fclose(fToWrite); 
        errno = EFBIG;
        PROP(WR,pathname,-1,0)
        return -1;
    }

    // Copio contenuto in un buffer
    void * buffer = malloc (fileSize);
    memset (buffer, 0, fileSize);
    if (fread (buffer, 1, fileSize, fToWrite)==0 && ferror(fToWrite)!=0) {
        fclose(fToWrite);
        free(buffer);
        PROP(WR,pathname,-1,0)
        return -1;
    }

    // Chiamo appendToFile per completare la scrittura
    if (appendToFile(pathname, buffer, fileSize, expelledFiles)==-1) {
        free(buffer);
        fclose(fToWrite);
        PROP(WR,pathname,-1,0)
        return -1;
    }

    free(buffer);
    PROP(WR,pathname,0,fileSize)
    if (fclose(fToWrite) == EOF) return -1;
    return 0;
}

int appendToFile (const char* pathname, void* buf, size_t size, const char* dirname){

    // Controllo validità nome
    size_t fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Creo stringa di richiesta: operazione, size, nomefile
    char opString [MAX_BUF_SIZE];
    memset (opString,0,sizeof(char)*MAX_BUF_SIZE);
    sprintf(opString, "%d,%li,%s",WR,size,pathname);
    size_t nToWrite = strlen(opString)*sizeof(char);

    //Alloco buffer per la risposta del server
    void * buffRead = malloc(MAX_BUF_SIZE);
    memset (buffRead, 0, MAX_BUF_SIZE);
    void * toFree = buffRead;

    // Invio la richiesta e leggo la risposta del server
    if (writeAndRead(opString, &buffRead, nToWrite, MAX_BUF_SIZE) ==-1) {
        free(buffRead);
        return -1;
    }
    int res = getAnswer(&buffRead,MAX_BUF_SIZE, WR);

    // Resetto buffer
    free (toFree);
    buffRead = malloc(MAX_BUF_SIZE);
    toFree = buffRead;

    // Se il server deve espellere file li ricevo finchè non mi segnala di avere abbastanza spazio
    while (res==EXPEL || res==WAIT) {
        // Leggo file espulso
        if (res==EXPEL){
            if (getExpelledFile() == -1) {
                free(toFree);
                return -1;
            }
        }
        // Se il server mi chiede di aspettare perchè non ho i diritti per ricevere il file rispondo con un ok
        else {
            if (sendAnswer(clientSFD,SUCCESS)==-1) {
                free(toFree);
                return -1;
            }
        }

        // Leggo risposta successiva
        size_t nToRead = MAX_BUF_SIZE;
        ssize_t nRead = 1;
        ssize_t totBytesRead = 0;
        while (nToRead > 0 && nRead!=0) {
            if (totBytesRead!=0 && bufferCheck(buffRead)==1) break;
            if ((nRead = read(clientSFD,buffRead+totBytesRead,nToRead)==-1)) {
                free(toFree);
                return -1;
            }
            totBytesRead += nRead;
            nToRead -= nRead;
        }
        // Rimango nel loop finchè il server deve espellere file
        res = getAnswer(&buffRead,MAX_BUF_SIZE,WR);
        free (toFree);
        buffRead = malloc(MAX_BUF_SIZE);
        toFree = buffRead;
    }

    // Invio file
    if (res==SUCCESS) {    
        if (writeAndRead(buf, &buffRead, size, MAX_BUF_SIZE) ==-1) {
            free(toFree);
            return -1;
        }
        // leggo codice di risposta: Success o Failure
        res = getAnswer(&buffRead,MAX_BUF_SIZE, WR);
    }    
    
    free (toFree);
    return res;
}

int lockFile(const char* pathname){

    // Controllo se posso operare sul file
    if (checkOpen(pathname,0)==-1) return -1;

    // Controllo lunghezza nome
    int fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        PROP(LCK,pathname,-1,0)
        return -1;
    }

    // Stringa di richiesta: 4096,pathname,timeout
    char * buffer = calloc(MAX_BUF_SIZE,sizeof(char));
    sprintf (buffer,"%s,%s,%d",LOCK_F,pathname,msec);
    int bufferSize = strlen(buffer)+1;
    // Buffer in cui salvo la risposta
    void * buffRead = malloc(MAX_BUF_SIZE);
    memset (buffRead, 0, MAX_BUF_SIZE);
    // Puntatori iniziali per free
    void * toFreeWrite = buffer;
    void * toFree = buffRead;
    
    int result;
    if ((result = writeAndRead(buffer,&buffRead,bufferSize-1,MAX_BUF_SIZE))==-1) {
        PROP(LCK,pathname,result,0)
        FREE_RET
    }
    else {
        int answer = getAnswer(&buffRead,MAX_BUF_SIZE,LCK);
        // Se il file che sto provando a lockare non esiste lo creo con flag O_LOCK
        if (answer == ENOENT) answer = openFile(pathname,O_CREATE | O_LOCK);
        free (toFreeWrite);
        free (toFree);
        PROP(LCK,pathname,answer,0)
        return answer;
    }

}

int unlockFile(const char* pathname){

    // Controllo se posso operare sul file
    if (checkOpen(pathname,0)==-1) {
        PROP(ULC,pathname,-1,0)
        return -1;
    }

    // Controllo lunghezza nome
    int fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        PROP(ULC,pathname,-1,0)
        return -1;
    }

    // Stringa di richiesta
    size_t bufferSize = sizeof(char) * (strlen(UNLOCK)+fNameLen) +1;
    void * buffer = malloc(bufferSize);
    memset (buffer, 0, bufferSize);
    void * buffRead = malloc(MAX_BUF_SIZE);
    memset (buffRead, 0, MAX_BUF_SIZE);
    void * toFreeWrite = buffer;
    void * toFree = buffRead;
    
    strcpy (buffer, UNLOCK);
    strcat (buffer, pathname);
    int result;

    if ((result = writeAndRead(buffer,&buffRead,bufferSize-1,MAX_BUF_SIZE))==-1) {
        PROP(ULC,pathname,-1,0)
        FREE_RET
    }
    else {
        int answer = getAnswer(&buffRead,MAX_BUF_SIZE,ULC);
        free (toFreeWrite);
        free (toFree);
        PROP(ULC,pathname,answer,0)
        return answer;
    }
}

int removeFile (const char* pathname){

    // Controllo se posso operare sul file
    if (checkOpen(pathname,0)==-1) return -1;

    // Controllo lunghezza nome
    int fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        PROP(RM,pathname,-1,0)
        return -1;
    }

    // Preparo richiesta
    size_t bufferSize = sizeof(char) * (strlen(RMV)+fNameLen) +1;
    void * buffer = malloc(bufferSize);
    memset (buffer, 0, bufferSize);
    void * toFreeWrite = buffer;
    void * buffRead = malloc(MAX_BUF_SIZE);
    memset (buffRead, 0, MAX_BUF_SIZE);
    void * toFree = buffRead;

    strcpy (buffer, RMV);
    strcat (buffer, pathname);
    int result;
    // Se la comunicazione col server è fallita riporto errore dopo aver deallocato
    if ((result = writeAndRead(buffer, &buffRead, bufferSize-1, MAX_BUF_SIZE))==-1) {
        PROP(RM,pathname,-1,0)
        FREE_RET
    }

    // Se ha avuto successo riporto l'esito della rimozione
    else {
        int answer = getAnswer(&buffRead,MAX_BUF_SIZE,RM);
        free (toFreeWrite);
        free (toFree);
        // Se è stato rimosso da server chiudo il file da lato client
        if (answer == SUCCESS) {
            closeFile (pathname);}
        PROP(RM,pathname,answer,0)
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
    PROP(CF,pathname,close,0)
    return close;
}

ssize_t writeAndRead (void * bufferToWrite, void ** bufferToRead, size_t bufferSize, size_t answer){
    
    // Write finchè non ho scritto tutti i byte del buffer
    size_t nToWrite = bufferSize;
    ssize_t nWritten = 1;
    size_t totBytesWritten = 0;
    //printf("%s\n",(char*)bufferToWrite);  //TEST
    while ( nToWrite > 0 && nWritten!=0) {
        if ((nWritten = write(clientSFD, bufferToWrite, nToWrite))==-1) return -1;
            bufferToWrite+=nWritten;
            nToWrite -= nWritten;
            totBytesWritten += nWritten;
        }
    write(clientSFD,EOBUFF,EOB_SIZE);

    // Aspetto il tempo di risposta
    sleep(msec * 0.001);

    // Leggo la risposta del server
    size_t nToRead = answer;
    ssize_t nRead = 1;
    ssize_t totBytesRead = 0;
    while (nToRead > 0 && nRead!=0) {
        if (totBytesRead!=0 && bufferCheck(bufferToRead)==1) break;
        if ((nRead = read(clientSFD,bufferToRead,nToRead)==-1)) return -1;
        bufferToRead+=nRead;
        totBytesRead += nRead;
        nToRead -= nRead;
    }
    return totBytesRead;
}

int saveFile (void * buffer, const char * dirName, const char * pathname, size_t size) {

    // Estraggo solo il nome da un eventuale percorso con cui è stato nominato dal server
    char tm [MAX_NAME_LEN];
    memset(tm,0,MAX_NAME_LEN);
    if (nameFromPath (pathname, tm)==-1) return -1;

    // Salvo il path ./directory/filename
    char fpath [MAX_NAME_LEN];
    memset(fpath,0,MAX_NAME_LEN);
    strcpy(fpath, dirName);
    strcat(fpath,"/");
    strcat(fpath,tm);

    // Salvo il file aprendolo in creazione
    FILE * f;
    if ((f = fopen(fpath,"w+"))==NULL) return -1;

    // Copio il contenuto del buffer
    if (fwrite(buffer,1,size,f)<size) {
        fclose(f);
        return -1;
    }
    
    fflush(f);
    if (fclose(f) == 0) return 0;
    return -1;

}

ssize_t getAnswer(void ** buffer, size_t size, int query) {
    char * string = calloc(size, sizeof(char));
    memcpy(string, buffer, sizeof(char)*size);

    char * toTok;
    toTok = strtok(string,EOBUFF);

    ssize_t res = atoi(toTok);
    free (string);
    if (query!=RDM && query!=RD && res==1) return -1;
    return res;
}

void printOpRes (int op, const char * fname, int res, size_t bytes) {
    switch (op)
    {
    case RD:
        printf ("Eseguita lettura di %li bytes del file %s\n con risultato %d\n",bytes,fname,res);
        break;
    
    case WR:
        printf ("Eseguita scrittura di %li bytes del file %s\n con risultato %d\n",bytes,fname,res);
        break;

    case OP:
        printf ("Eseguita apertura del file %s con risultato %d\n",fname,res);
        break;

    case OPL:
        printf ("Eseguita open-lock del file %s con risultato %d\n",fname,res);
        break;

    case PUC:
        printf ("Eseguita creazione del file %s con risultato %d\n",fname,res);
        break;
    
    case PRC:
        printf ("Eseguita creazione locked del file %s con risultato %d\n",fname,res);
        break;

    case CF:
        printf ("Eseguita chiusura del file %s con risultato %d\n",fname,res);
        break;

    case RM:
        printf ("Eseguita rimozione del file %s con risultato %d\n",fname,res);
        break;

    case LCK:
        printf ("Eseguita acquisizione del file %s con risultato %d\n",fname,res);
        break;

    case ULC:
        printf ("Eseguito rilascio del file %s con risultato %d\n",fname,res);
        break;

    case EXF:
        printf("Ricevuta espulsione di %li bytes del file %s con risultato %d\n",bytes,fname,res);
        break;

    default:
        break;
    }
}

int getHeader_File (void ** content, void ** pathname, size_t * size, int op){

        void * buffRead = malloc (MAX_BUF_SIZE);
        memset (buffRead, 0, MAX_BUF_SIZE);
        void * toFree = buffRead;

        char * tk; // token tmp
        size_t fileSize; // taglia del file
        char * pathName = calloc(MAX_NAME_LEN,sizeof(char));  // nome file

        // Leggo header sul file e ne faccio una copia
        ssize_t nToRd = MAX_BUF_SIZE;
        ssize_t nRead = 1;
        size_t totBytes = 0;
        while (nToRd>0 && nRead!=0) {
            if (totBytes!=0 && bufferCheck(buffRead)!=0) break;
            if ((nRead=read(clientSFD,buffRead+totBytes,nToRd))==-1) {
                free(toFree);
                return -1;
            }
            nToRd-=nRead;
            totBytes +=nRead;
        }
        char toTok [MAX_BUF_SIZE];
        strcpy (toTok,(char*)buffRead);

        // Estraggo il nome da buffer
        tk = strtok(toTok,",");
        strcpy(pathName, tk);

        // Estraggo la taglia
        tk = strtok(NULL, EOBUFF);
        fileSize = atol(tk);

        // Salvo eventuale contenuto che possa essere finito nel buffer dell'header
        void * file = malloc (fileSize+EOB_SIZE);
        memset (file, 0, fileSize);
        void * freePtr = file;
        if (op!=RD || op!=EXF) nToRd = fileSize+EOB_SIZE; 
        else nToRd=fileSize;    //+1;

        if ((tk=strtok(NULL,EOBUFF))!=NULL) {
            memcpy(file,tk,sizeof(char)*strlen(tk));
            nToRd-=sizeof(char)*strlen(tk)+EOB_SIZE;
        }

        nRead = 1;
        totBytes = 0;
        // Se l'operazione è una Read Multipla invio l'ok per ricever il contenuto
        if (op == RDM) sendAnswer(clientSFD,SUCCESS);
        // Leggo il restante contenuto se presente
        while (nToRd>0 && nRead!=0) {
            if (totBytes!=0 && bufferCheck(file)!=0) break;
            if ((nRead=read(clientSFD,file+totBytes,nToRd))==-1) {
                free (freePtr);
                free (toFree);
                return -1;
            }
            nToRd-=nRead;
            totBytes +=nRead;
        }
        free(toFree);
        * pathname = pathName;
        * content = freePtr;
        * size = fileSize;
        return 0;
}

int getExpelledFile () {
    // Segnalo di poter ricevere il file
    sendAnswer(clientSFD,SUCCESS);
    void * content;
    void * pathname;
    size_t size;

    // Leggo il file preceduto da un header
    if (getHeader_File(&content,&pathname,&size,PUC)==-1) return -1;

    // Se ho specificato una cartella per i file espulsi salvo il file
    int res = 0;
    if (expelledFiles!=NULL) {res = saveFile(content,expelledFiles,pathname,size);}
    PROP(EXF,pathname,res,size)
    free (pathname);
    free(content);
    return res;
}

int checkOpen (const char * pathname, int flag) {

    //Controllo se il file è già aperto
    int result = 0;
    int fState = searchString(pathname,openFiles);
    // Se non è presente tento di aprirlo
    if (fState == -1) {
        if (flag==0 && openFile(pathname, 0)==-1) result=-1;
        if (flag==O_LOCK && openFile(pathname, O_LOCK)==-1) result=-1;
    }   
    // Se è stato chiuso in precedenza non posso più operare sul file
    if (fState == 0) {
        errno = EPERM;
        result = -1;
    }
    return result;
}
