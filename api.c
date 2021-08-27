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
#define FREE_RET {free(toFreeWrite); free(toFree); return -1;}
#define PROP(op,file,res,byte) if (pOpt_met) {printOpRes(op,file,res,byte);}


extern int pOpt_met;
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
    return res;
}

int openFile (const char* pathname, int flags){

    int fNameLen= strlen(pathname);
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

    if ( flags != 0) {
        switch (flags){

            // creo un file
            case O_CREATE:

                nToWrite = sizeof(char)*(strlen(PUB_CREATE)+fNameLen) + 1;  //buffer da inviare a server
                buffer = malloc(nToWrite);
                memset (buffer, 0, nToWrite);
                toFreeWrite = buffer;
                strcpy (buffer, PUB_CREATE);
                strcat (buffer,pathname);

                if (writeAndRead(buffer, &buffRead, nToWrite-1, MAX_BUF_SIZE)==-1) FREE_RET;    // STUCK

                // Se il server mi risponde con l'espulsione di un file
                if (getAnswer(&buffRead,MAX_BUF_SIZE,PUC)==EXPEL) {
                    if (getExpelledFile() == -1) FREE_RET
                }
                break;


            /* apro un file locked
            case O_LOCK:
                buffer = calloc((strlen(PUB_CREATE)+strlen(pathname)),sizeof(char));
                strcpy (buffer, "op,fl,");
                strcat (buffer,pathname);
                if (write(clientSFD, &buffer, sizeof(buffer))==-1) {
                    FREE_RET;
                }
                break; */

            // creo un file locked
            case O_CREATE | O_LOCK :
                nToWrite = sizeof(char)*(strlen(PRIV_CREATE)+strlen(pathname));
                buffer = malloc(nToWrite+1);
                strcpy (buffer, PRIV_CREATE);
                strcat (buffer,pathname);
                if (writeAndRead(buffer,&buffRead, nToWrite, MAX_BUF_SIZE)==-1) FREE_RET;

                // Se il server mi risponde con l'espulsione di un file
                if (getAnswer(&buffRead,MAX_BUF_SIZE,PUC)==EXPEL) {
                    if (getExpelledFile() == -1) FREE_RET
                }
                break;

            default:
                fprintf(stderr,"Flags non valide\n");
                break;
        }
    }

    // apro un file
    else {
        nToWrite = sizeof(char) * (strlen(OPEN)+strlen(pathname)) + 1;
        buffer = malloc(nToWrite);
        memset (buffRead, 0, nToWrite);
        toFreeWrite = buffer;
        strcpy (buffer, OPEN);
        strcat (buffer,pathname);
        if (writeAndRead(buffer,&buffRead, nToWrite-1, MAX_BUF_SIZE)==-1) FREE_RET;
    }

    int answer = getAnswer(&buffRead, MAX_BUF_SIZE, OP);
    free (toFreeWrite);
    free (toFree);

    if(answer == FAILURE && flags==O_CREATE) {
        errno = EEXIST;
        PROP(OP,pathname,-1,0)
        return -1;
    }

    // si è provato ad aprire file non esistente
    if(answer == FAILURE && flags==0){
        errno = ENOENT;
        PROP(OP,pathname,-1,0)
        return -1;
    }
    
    if (answer==SUCCESS) lastOperation = 1;
    openFiles=addString(pathname, openFiles);

    //Se le stampe sono abilitate stampo l'esito
    PROP(OP,pathname,answer,0)
    return answer;
}

int readFile (const char * pathname, void ** buf, size_t* size) {

    // Controllo lunghezza nome
    int fNameLen = strlen (pathname);
    if (fNameLen>MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    //Controllo se il file è già aperto
    int fState = searchString(pathname,openFiles);
    // Se non è presente tento di aprirlo
    if (fState == -1) {
        if (openFile(pathname, 0)==-1) return -1;
    }   
    if (fState == 0) {  // Se è stato chiuso in precedenza non posso più operare sul file
        errno = EPERM;
        return -1;
    }

    //Preparo richiesta al server
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
    if (writeAndRead(buffer, buff, nToWrite-1, sizeof(char)*32)==-1) {
        free (toFreeWrite);
        free (freeRes);
        return -1;
    }
    char string[MAX_BUF_SIZE];
    strcpy (string, (char*)buff);
    char * tok = strtok(string,EOBUFF);
    result = atol(tok);
    printf("SIZE: %li from %s\n",result,string);    //TEST

    // Alloco spazio per il file
    void * content = malloc(result);
    memset (content,0,result);
    void * ptr = content;

    // Salvo contenuto finito insieme all'header
    char header [MAX_BUF_SIZE];
    sprintf (header,"%li£",result);
    int headerLen = strlen (header) ;
    buff+=headerLen;
    ssize_t partialSize = MAX_BUF_SIZE - headerLen;
    if (partialSize<result) memcpy (content,buff,partialSize);
    else memcpy (content, buff, result);


    // Leggo il contenuto restante
    ssize_t nToRd = result - partialSize;
    ssize_t nRead = 1;
    size_t totBytes = 0;
    while (nToRd>0 && nRead!=0) {
        if (totBytes!=0 && bufferCheck(content)==1) break;
        if ((nRead = read(clientSFD,content+totBytes,nToRd)==-1)) {
            free (toFreeWrite);
            free (freeRes);
            free(ptr);
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
            return -1;
        }
    }

    // Assegno i puntatori richiesti dal client
    * buf = ptr;
    * size = result;
    free (toFreeWrite);
    free (freeRes);

    //Se le stampe sono abilitate stampo l'esito
    PROP(RD,pathname,0,*size)
    return 0;
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
    printf("Available files: %d\n",N);
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
        char * saveName = calloc  (MAX_NAME_LEN, sizeof(char));
        if (nameFromPath(pathname,&saveName)==-1) {
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

    printf("%s\n",pathname);

    // Controllo lunghezza nome
    size_t fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        PROP(WR,pathname,-1,0)
        return -1;
    }

    // Controllo se posso operare sul file
    int fileS = searchString (pathname, openFiles);
    if (fileS == 0) {
        errno = EPERM;
        PROP(WR,pathname,-1,0)
        return -1;
    }

    // Apro file in locale 
    FILE * fToWrite; 
    if ((fToWrite = fopen(pathname, "r+") )== NULL) {
        printf ("Open failure: %s\n",pathname);     //TEST
        PROP(WR,pathname,-1,0)
        return -1;
    }

    // Richiesta apertura file al server
    int res = openFile(pathname, 0);        // res è -1 ma errno è 104 dopo

    if (res == -1) {
        if (errno==ENOENT) { 
            //Se il file non è già presente, lo creo
            res = openFile(pathname, O_CREATE);
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
        return -1;
    }

    free(buffer);
    PROP(WR,pathname,0,fileSize)
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

    // Creo stringa di richiesta: operazione, size, nomefile
    char opString [MAX_BUF_SIZE];
    memset (opString,0,sizeof(char)*MAX_BUF_SIZE);
    sprintf(opString, "%d,%li,%s\n",WR,size,pathname);
    printf("%s\n",opString);    //TEST

    // Invio header richiesta
    size_t nToWrite = sizeof(char)*strlen(opString);
    ssize_t nWritten = 1;
    size_t totBytesWritten=0;
    while (nToWrite>0 && nWritten!=0){
        if ((nWritten = write(clientSFD, opString+totBytesWritten, nToWrite))==-1) return -1;
        totBytesWritten+=nWritten;
        nToWrite-=nWritten;
        }

    // Invio file
    void * buffRead = malloc(MAX_BUF_SIZE);
    memset (buffRead, 0, MAX_BUF_SIZE);
    void * toFree = buffRead;
    if (writeAndRead(buf, &buffRead, size, MAX_BUF_SIZE) ==-1) {
        free(buffRead);
        return -1;
    }

    // leggo codice di risposta: Success o Failure
    int res = getAnswer(&buffRead,MAX_BUF_SIZE, WR);
    free (toFree);
    return res;
}

int lockFile(const char* pathname){
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
        free (toFreeWrite);
        free (toFree);
        PROP(LCK,pathname,answer,0)
        return answer;
    }

}

int unlockFile(const char* pathname){

    // Controllo lunghezza nome
    int fNameLen = strlen(pathname);
    if (fNameLen > MAX_NAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Stringa di richiesta
    size_t bufferSize = sizeof(char) * (strlen(LOCK_F)+fNameLen) +1;
    void * buffer = malloc(bufferSize);
    memset (buffer, 0, bufferSize);
    void * buffRead = malloc(MAX_BUF_SIZE);
    memset (buffRead, 0, MAX_BUF_SIZE);
    void * toFreeWrite = buffer;
    void * toFree = buffRead;
    
    strcpy (buffer, LOCK_F);
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
    
    //write finchè non ho scritto tutti i byte del buffer
    size_t nToWrite = bufferSize;
    ssize_t nWritten = 1;
    size_t totBytesWritten = 0;

    printf("%s\n",(char*)bufferToWrite);    //TEST
    while ( nToWrite > 0 && nWritten!=0) {
        if ((nWritten = write(clientSFD, bufferToWrite+totBytesWritten, nToWrite))==-1) return -1;
            nToWrite -= nWritten;
            totBytesWritten += nWritten;
        }
    write(clientSFD,EOBUFF,EOB_SIZE);

    size_t nToRead = answer;
    ssize_t nRead = 1;
    ssize_t totBytesRead = 0;

    //aspetto il tempo di risposta
    sleep(msec * 0.001);

    //leggo la risposta del server
    while (nToRead > 0 && nRead!=0) {
        if (totBytesRead!=0 && bufferCheck(bufferToRead)==1) break;
        if ((nRead = read(clientSFD,bufferToRead+totBytesRead,nToRead)==-1)) return -1;
        totBytesRead += nRead;
        nToRead -= nRead;
    }
    return totBytesRead;
}

int saveFile (void * buffer, const char * dirName, const char * pathname, size_t size) {

    // Estraggo solo il nome da un eventuale percorso con cui è stato nominato dal server
    printf ("Salvo file: %s\n",pathname);                   // TEST
    char * tm = calloc (strlen(pathname)+1, sizeof(char));
    strcpy (tm, pathname);
    nameFromPath (tm, &tm);

    // Salvo il path ./directory/filename
    char * fpath = calloc (strlen(dirName)+strlen("/")+strlen(tm)+1, sizeof(char));
    strcpy(fpath, dirName);
    strcat(fpath,"/");
    strcat(fpath,tm);

    // Salvo il file aprendolo in creazione
    FILE * f;
    if ((f = fopen(fpath, "w+"))==NULL) return -1;

    // Copio il contenuto del buffer
    fwrite(buffer, sizeof(char), size, f);
    free (fpath);
    free (tm);
    if (fclose(f) == 0) return 0;
    return -1;

}

ssize_t getAnswer(void ** buffer, size_t size, int query) {
    char * string = calloc(size, sizeof(char));
    memcpy(string, (char*)buffer, sizeof(char)*size);

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
        char pathName[MAX_NAME_LEN];  // nome file

        // Leggo header sul file e ne faccio una copia
        ssize_t nToRd = MAX_BUF_SIZE;
        ssize_t nRead = 1;
        size_t totBytes = 0;
        while (nToRd>0 && nRead!=0) {
            printf("To read :%d\n",nToRd);
            if (totBytes!=0 && bufferCheck(buffRead)!=0) break;
            if ((nRead=read(clientSFD,buffRead+totBytes,nToRd))==-1) {
                free(toFree);
                return -1;
            }
            nToRd-=nRead;
            totBytes +=nRead;
        }
        printf("Header letto: %s\n",(char *)buffRead);
        char toTok [MAX_BUF_SIZE];
        strcpy (toTok,(char*)buffRead);

        // Estraggo il nome da buffer
        tk = strtok(toTok,",");
        strcpy(pathName, tk);

        // Estraggo la taglia
        tk = strtok(NULL, EOBUFF);
        fileSize = atol(tk);

        // Salvo eventuale contenuto che possa essere finito nel buffer dell'header
        void * file = malloc (fileSize);
        memset (file, 0, fileSize);
        void * freePtr = file;
        nToRd = fileSize ; 

        if ((tk=strtok(NULL,EOBUFF))!=NULL) {
            memcpy(file,tk,sizeof(char)*strlen(tk));
            nToRd-=sizeof(char)*strlen(tk);
        }

        // Segnalo al server di procedere con l'invio del contenuto del file
        nRead = 1;
        totBytes = 0;
        if (op == RDM) sendAnswer(clientSFD,SUCCESS);
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
        * content = file;
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

    // Ricavo il nome per salvare eventualmente il file
    char * saveName = calloc  (MAX_NAME_LEN, sizeof(char));
    if (nameFromPath(pathname,&saveName)==-1) {
        free (saveName);
        free (content);
        return -1;
    }
    int res = 0;

    // Se ho specificato una cartella per i file espulsi salvo il file
    if (expelledFiles!=NULL) {res = saveFile(content,expelledFiles,saveName,size);}
    if (res==-1) {
        free (saveName);
        free(content);
        return -1;
    }
    return 0;
}
