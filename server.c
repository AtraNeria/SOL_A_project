#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include "server.h"
#include "commProtocol.h"
#include "list.h"

#define CONF_PAR 10
#define MAX_CONN 30
#define TRUE 1



void errEx ();
void init (int index, char * string);
void cleanup(pthread_t workers[], int index, node * socket_list);

//inizializza lo storage all'arrivo del primo file; ne ritorna il puntatore
fileNode * initStorage(FILE * f, char * fname, int fOwner);

/* Invia un file f al client fd per l'operazione di codice op.
    Restituisce 0 se l'invio ha successo, -1 altrimenti
*/
int sendFile (int file, fileNode * f, int op);

/*  Invia al client fd un header riguardante un file contenente il suo
    nome pathname e la sua taglia size.
    Restituisce 0 se l'invio ha successo, -1 altrimenti
*/
int sendHeader (int fd, char * pathname, ssize_t size);

/* Aspetta che il client legato alla socket fd invii una risposta di feedback.
    Restituisce 0 se lo riceve con successo. -1 altrimenti
*/
int waitForAck (int fd);

/* Espelle un file per permettere l'operazione op su file fName richiesta dal client fd.
    Il file vittima viene inviato al client.
    Restituisce 0 in caso successo, -1 altrimenti
*/
int expelFile (int fd, int op, char * fName);

/* Segnala sull'eventfd fdListen fd
*/
void writeListenFd (int fd);

// Setta la flag sigiq_flag in caso di ricezione SIGINT o SIGQUIT
void setIqFl ();
// Setta la flag sighup_flag in caso di ricezione SIGHUP
void setShFl ();

// Flags settate dalla gestione dei segnali
int sigiq;
int sighup;

int threadQuantity;
int storageDim;
size_t capacity;
int queueLenght;
int maxClients;
FILE * logging;

node * requestsQueue=NULL;
node * lastRequest;
pthread_mutex_t mutex;
pthread_cond_t newReq = PTHREAD_COND_INITIALIZER;
pthread_cond_t fileUnlocked = PTHREAD_COND_INITIALIZER;

int fileCount = 0;
ssize_t usedBytes = 0;
fileNode * storage=NULL;
fileNode * lastAddedFile=NULL;

int evClose;
int fdListen;

// Macro per le operazioni standard di fine richiesta; la prima include l'invio di una risposta al client
#define END_REQ(OP) logOperation(OP, currentRequest, res, reqArg);writeListenFd(currentRequest);sendAnswer(currentRequest, res);
#define END_NOANSW(OP) logOperation(OP, currentRequest, res, reqArg);writeListenFd(currentRequest);


void errEx () {
    perror("Error:");
    exit(EXIT_FAILURE);
}

void init (int index, char * string) {

    switch (index)
    {
    case 0:                                     //prima riga di config.txt
        threadQuantity=atoi(string);
        break;
    
    case 1:                                    //seconda riga di config.txt ....
        storageDim=atoi(string);
        break;
    
    case 2:
        capacity=atol(string);
        break;

    case 3:
        queueLenght=atoi(string);
        break;
    
    case 4:
        maxClients=atoi(string);
        if (maxClients>MAX_CONN) {
            fprintf(stderr,"Numero client richiesti troppo alto, impossibile configurare il server");
            exit(EXIT_FAILURE);
        }
        break;

    case 5:
        logging = fopen(string,"w+");
        if (logging==NULL) fprintf(stderr,"Impossibile aprire file di logging");
        break;

    default:
        break;
    }
}

void readConfig () {
    FILE * conf;
    if( (conf=fopen("config.txt","r")) == NULL) //apro file di config
        errEx();
    char confStr [MAX_NAME_LEN];
    char * currTok;
    char * ptr;
    int i;

    for (i=0;i<CONF_PAR;i++){
        fgets(confStr, MAX_NAME_LEN, conf);
        currTok=strtok_r(confStr, ";", &ptr);
        init(i, currTok);
    }
    
    if (fclose(conf)!=0) errEx();

}

void startServer () {

    sigiq = 0;
    sighup = 0;

    /*// Definisco la gestione segnali
    //Per SIGINT e SIGQUIT
    struct sigaction * immediate;
    memset(immediate, 0, sizeof(immediate));
    immediate->sa_handler = setIqFl;
    //Per SIGHUP
    struct sigaction * finishFirst;
    memset(finishFirst, 0, sizeof(finishFirst));
    finishFirst->sa_handler = setShFl;
    // Ignoro SIGPIPE
    struct sigaction * ignore;
    memset(ignore, 0, sizeof(ignore));
    ignore->sa_handler = SIG_IGN;
    // Registro le azioni
    sigaction(SIGINT,immediate,NULL);
    sigaction(SIGQUIT,immediate,NULL);
    sigaction(SIGHUP,finishFirst,NULL);
    sigaction(SIGPIPE,ignore,NULL); */

    int serverSFD;
    if ((serverSFD = socket(AF_UNIX, SOCK_STREAM,0))==-1)
        errEx();

    node * socketsList = addNode(serverSFD);
    node * currSock = socketsList;

    struct sockaddr_un address;
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, "./server");
    
    if (bind(serverSFD, (struct sockaddr *)&address, sizeof(address))==-1)   //Permission denied, need sudo
        errEx();
    
    pthread_t * workers = calloc(threadQuantity,sizeof(pthread_t));

    int wsFailed = 0;
    if ((wsFailed=createWorkers(workers)) !=-1) {
        printf ("Problema riscontrato nella creazione dei thread\n");
        perror("Error:");
        cleanup(workers, wsFailed, socketsList);
        exit(EXIT_FAILURE);
    }

    if ((listen(serverSFD, queueLenght))==-1){
        cleanup(workers, threadQuantity, socketsList);
        errEx();
    }

    // Creo un fd evento per monitorare eventuali comunicazioni da chiudere
    evClose = eventfd(0,0);
    socketsList = addNode(evClose);
    currSock = socketsList;
    // Fd evento per reiniziare a monitorare client dopo la gestione di una loro richiesta
    fdListen = eventfd(0,0);
    currSock->next = addNode(fdListen);
    currSock=currSock->next;
    // Client + server + eventFD
    int maxFd = maxClients + 4;     
    struct pollfd * connectionFDS = calloc(maxFd, sizeof(struct pollfd));
    connectionFDS[0].fd = serverSFD;
    connectionFDS[0].events = POLLIN;

    connectionFDS[1].fd = evClose;
    connectionFDS[1].events = POLLIN;

    connectionFDS[2].fd = fdListen;
    connectionFDS[2].events = POLLIN;

    for (int i=3; i < maxFd; i++) { 
        connectionFDS[i].fd = -1;
        connectionFDS[i].events = POLLIN; 
    }
    int pollRes = 0;
    int timeout = 60*1000;      //1 min



    while(!sigiq && !sighup) {

        //Controllo se devo rimettermi in ascolto di fd
        if (connectionFDS[2].revents == POLLIN) {
            void * eventB = malloc(8);
            memset (eventB, 0, 8);
            read (fdListen, eventB, 8);
            int toListen = atoi(eventB);
            printf("Listen: %d\n\n",toListen);  //TEST
            for (int i=3; i<maxFd; i++) {
                if (connectionFDS[i].fd == toListen) connectionFDS[i].events=POLLIN;
            }
            free(eventB);
        }

        //Controllo se devo chiudere una socket prima della poll
        if (connectionFDS[1].revents == POLLIN) {
            //pthread_mutex_lock(&mutex);   // Maybe gotta lock ???
            void * eventB = malloc(8);
            memset (eventB, 0, 8);
            read (evClose, eventB, 8);
            int * toClose = eventB;
            for (int i=3; i<maxFd; i++) {       // Voglio chiudere 5, trova solo 3 ???
                if (connectionFDS[i].fd == *toClose) connectionFDS[i].fd = -1;
            }
            deleteNode(*toClose,&socketsList);
            close(*toClose);
            free(eventB);

            pthread_mutex_lock(&mutex);
            logOperation(CC, *toClose, 0, NULL);
            pthread_mutex_unlock(&mutex);

        }

        //Controllo se ci sono altre richieste
        if ((pollRes = poll(connectionFDS,maxFd,timeout))==-1)
            errEx();
        if (pollRes == 0) {
            printf("Sessione scaduta\n");
            //no clients connected atm
        }

        //if client richiede connect
        if (connectionFDS[0].revents == POLLIN) {
            int j = 3;
            while(connectionFDS[j].fd!=-1 && j<maxFd) j++; 
            //controllo se ho spazio per gestire più client
            if (j<=maxFd) {
                connectionFDS[j].fd = accept (serverSFD, NULL, 0);  //se posso accetto connessione
                currSock->next = addNode(connectionFDS[j].fd);        // per eventuale cleanup
                currSock = currSock->next;
                // Accept nel file di logging
                pthread_mutex_lock(&mutex);
                logOperation(OC, connectionFDS[j].fd, 0, NULL);
                pthread_mutex_unlock(&mutex);
            }
            //se non posso stampo un avvertimento
            else printf ("Tentata connessione\n");
        }

        //Se un client già connesso ha una richiesta
        for (int i=3;i<maxFd;i++){
            if (connectionFDS[i].revents==POLLIN && connectionFDS[i].fd!=-1) {
                pthread_mutex_lock(&mutex);
                if (requestsQueue == NULL) {
                    requestsQueue = addNode(connectionFDS[i].fd);
                    lastRequest = requestsQueue;
                }
                else {
                    lastRequest->next = addNode(connectionFDS[i].fd);
                    lastRequest=lastRequest->next;
                }
                printf("Add to req list with count %d: %d\n",listCount(requestsQueue),connectionFDS[i].fd);    //TEST
                connectionFDS[i].events = 0;
                pthread_cond_signal(&newReq);
                pthread_mutex_unlock(&mutex);
            }
        }

    }
    if (sigiq) {
        //for (int i=0;i<threadQuantity;i++) {
        //    pthread_kill (workers[i],SIGKILL);
        //}
        cleanup(workers,threadQuantity, socketsList);
        _exit(EXIT_SUCCESS);
    }
    if (sighup) {
        connectionFDS[0].fd = -1;
        //while (requestsQueue!=NULL) wait();
        //for (int i=0;i<threadQuantity;i++) {
        //    pthread_kill (workers[i],SIGKILL);
        //}
        cleanup(workers,threadQuantity, socketsList);
        _exit(EXIT_SUCCESS);
    }
    free (connectionFDS);
}

void cleanup(pthread_t workers[], int index, node * socket_list) {
    for (int i=0; i<index; i++) {       //join dei thread aperti
        pthread_join(workers[i], NULL);
    }
    free(workers);

    node * toFree;
    while (socket_list != NULL) {       //chiusura sockets
        if (close(socket_list->descriptor)!=0)
            errEx();
        toFree = socket_list;
        socket_list = socket_list->next;
        free(toFree);
    }

    while (storage!=NULL) {         //chiusura e free file rimasti
       deleteFile(storage, &storage, &lastAddedFile);
    }
    
}

int createWorkers (pthread_t  workers[]) {

    int error_num;
    for (int i=0; i<threadQuantity; i++) {
        if( (error_num = pthread_create(&workers[i], NULL, manageRequest, NULL))!=0) {   //will be revised
            errno = error_num;
            return i;
        }
    }
    return -1;
}

fileNode * initStorage(FILE * f, char * fname, int fOwner){
    fileNode * list = malloc(sizeof(fileNode));
    memset (list, 0, sizeof(fileNode));
    list->owner=fOwner;
    list->next=NULL;
    list->prev=NULL;
    list->fPointer=f;
    list->fileName = calloc((strlen(fname)+1),sizeof(char));
    strcpy(list->fileName,fname);
    lastAddedFile=list;
    return list;

}

void * manageRequest() {

    /*// Setto le sigmask per far gestire i segnali solo da thread master
    sigset_t * sigmask;
    if (sigfillset(sigmask)==-1) errEx();
    if (pthread_sigmask (SIG_SETMASK, sigmask, NULL) != 0) errEx(); */

    //fd associato alla richiesta attuale
    int currentRequest;

    //finchè accetto nuove richieste
    while(TRUE) {

        pthread_mutex_lock(&mutex);
        // Prelevo fd da servive dalla queue
        while (requestsQueue==NULL ) pthread_cond_wait(&newReq,&mutex);
        currentRequest = requestsQueue->descriptor;
        requestsQueue = popNode(requestsQueue);
        if (requestsQueue==NULL) lastRequest=NULL;

        // Leggo la richiesta
        ssize_t reqRes = 1;
        size_t tr = MAX_BUF_SIZE;
        void * buffer = calloc(tr,1);
        void * toFree = buffer;
        size_t totBytesR = 0;
        while (tr>0 && reqRes!=0) {
            if (totBytesR!=0 && bufferCheck(buffer)==1) break;
            if ((reqRes=read(currentRequest, buffer+totBytesR, tr))==-1) errEx();
            totBytesR+=reqRes;
            tr-=reqRes;
        }
        pthread_mutex_unlock(&mutex);

        // Creo una copia del buffer per tokenizzare
        char * tk = calloc(MAX_BUF_SIZE, sizeof(char));
        memcpy (tk,(char *)buffer,MAX_BUF_SIZE);

        // Tolgo il terminatore dalla richiesta, formato: codiceOp,nomeFile,Contenuto
        char * request;
        char * ptr;
        request = strtok_r(tk,EOBUFF,&ptr);

        // Tokenizzo il codice
        char * tmp = strtok_r(request, ",",&ptr);
        int code;
        if (tmp!=NULL) code = atoi(tmp);

        printf("%d %s\n",code,request); //TEST
        char * reqArg;

        switch (code) {

            // Richiesta di lettura //
            case RD: {
                int res = 0;
                reqArg = strtok_r(NULL, EOBUFF,&ptr);
                if (reqArg == NULL) {
                    pthread_mutex_lock(&mutex);
                    res = -1;
                    END_NOANSW(RD)
                    pthread_mutex_unlock(&mutex);
                }

                else {
                    pthread_mutex_lock(&mutex);
                    fileNode * fToRd;

                    // Controllo la presenza
                    if ((res=searchFile(reqArg, storage, &fToRd))==-1) {END_NOANSW(RD)}
                    printf ("FILE: %s\n",fToRd->fileName);
                    printf("SIZE: %li\n",fToRd->fileSize);
                    // Invio la taglia
                    if ((res=sendAnswer(currentRequest,fToRd->fileSize))==-1) {END_NOANSW(RD)}
                    // Invio il contenuto
                    if ((res=sendFile(currentRequest,fToRd,RD))==-1) {END_NOANSW(RD)}

                    // Se l'invio fallisce lo riporto
                    pthread_mutex_unlock(&mutex);
                }

                break;
            }
                
            // Richiesta di scrittura //
            case WR: {
                int res = 0;
                pthread_mutex_lock(&mutex);
                reqArg = strtok_r(NULL, EOBUFF, &ptr);
                if (reqArg == NULL) res = -1;    

                else {
                    // Salvo richiesta completa
                    char fullRequest [MAX_NAME_LEN];
                    memset (fullRequest,0, sizeof(char)* MAX_NAME_LEN);
                    strcpy(fullRequest,WRITE);
                    strcat(fullRequest,reqArg);
                    int reqLen = strlen(fullRequest)+1;

                    // Ne faccio una copia da tokenizzate
                    char toTok [reqLen];
                    memset (toTok,0,sizeof(char)*reqLen);
                    strcpy(toTok,fullRequest);

                    // Estraggo la taglia del file
                    reqArg = strtok_r(toTok, ",", &ptr);
                    reqArg = strtok_r(NULL, ",", &ptr);
                    int fSize = atol (reqArg);

                    // Estraggo il path del file
                    reqArg = strtok_r(NULL, ",", &ptr);
                    char fileName [MAX_NAME_LEN];
                    strcpy(fileName, reqArg);

                    // Controllo che il file sia stato creato
                    fileNode * fToWr;
                    if (searchFile(fileName, storage, &fToWr) == -1) res = -1;

                    else {
                        // Controllo se si dispone dei permessi per scrivere sul file
                        if(fToWr->owner!=0 && fToWr->owner!=currentRequest) res = -1;
                           
                        else {
                            // Se posso scrivere controllo di avere spazio disponibile; in caso procedo all'espulsione di file vittima
                            printf("Used bytes: %li/%li and fSize %li\n",usedBytes,capacity,fSize);
                            while (usedBytes+fSize > capacity) {
                                expelFile(currentRequest,WR,fileName);
                            }

                            // Segnalo al client di poter inviare il contenuto del file
                            sendAnswer(currentRequest,SUCCESS);
                            void * content = calloc(fSize+EOB_SIZE, 1);
                            void * conToFree = content;

                            ssize_t dataToRd = fSize+EOB_SIZE;
                            size_t readRes = 1;
                            size_t totDataBytesR = 0;
                            while (dataToRd>0 && readRes!=0) {
                                if (totDataBytesR!=0 && bufferCheck(content)==1) break;
                                if ((readRes = read(currentRequest, content, dataToRd))== -1) break;
                                content += readRes;
                                totDataBytesR += readRes;
                                dataToRd -= readRes;
                            }

                            // Se ho riscontrato problemi nella lettura dico al client di aver fallito
                            if (readRes == -1) {
                                free(conToFree);
                                res = -1;
                            }

                            // Altrimenti salvo il file e segnalo l'azione avvenuta con successo
                            else {
                                if (fToWr->fPointer==NULL) {
                                    FILE * newF = fmemopen(NULL,fSize+1,"w+");
                                    size_t r = fwrite(conToFree,1,fSize, newF);
                                    if (r!=fSize) {
                                        res = -1;
                                        free(conToFree);
                                        END_REQ(WR)
                                        pthread_mutex_unlock(&mutex);
                                        break;
                                    }
                                    fToWr->fPointer=newF;
                                    fToWr->fileSize=fSize;
                                    usedBytes += fSize;
                                }
                                free(conToFree);
                            }
                        }
                    }
                    // Se la scrittura ha fallito ma esiste il file vuoto lo elimino
                    if (res==-1) {
                        usedBytes-= fToWr->fileSize;
                        deleteFile(fToWr,&storage,&lastAddedFile);
                        fileCount--;
                    }
                }
                END_REQ(WR)
                pthread_mutex_unlock(&mutex);
                break;
            }
                
            // Apertura di un file //
            case OP: {
                int res = 0;
                reqArg = strtok_r(NULL, EOBUFF, &ptr);
                pthread_mutex_lock(&mutex);
                if (reqArg == NULL) {
                    res = -1;
                    END_REQ(OP)
                    }
                    
                else {
                    fileNode * fToOp;
                    if (searchFile(reqArg, storage, &fToOp)==-1) {
                        res = -1;
                    }   
                    END_REQ(OP) 
                }
                pthread_mutex_unlock(&mutex);
                break;
            }

            // Rimozione di un file //
            case RM: {
                int res = 0;
                pthread_mutex_lock(&mutex);
                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    res = -1;
                    END_REQ(RM)
                }

                else {
                    fileNode * fToRm;
                    int res;
                    if ((res=searchFile(reqArg, storage, &fToRm))<=0) {
                        // Se il file non è in stato locked o locked dal processo richiedente lo posso eliminare
                        if(res==0 || res==currentRequest) {
                            usedBytes-= fToRm->fileSize;
                            deleteFile (fToRm, &storage, &lastAddedFile);
                            fileCount--;
                        }
                        // Se il file non esiste l'operazione ha automaticamente successo
                        END_REQ(RM)
                    }
                    // Se il file è locked da un altro processo l'operazione fallisce
                    else {
                        res = -1;
                        END_REQ(RM)
                    }
                }
                pthread_mutex_lock(&mutex);
                break;
            }

            // Chiusura di una socket client          
            case CLS: {
                void * toClose = &currentRequest;
                sendAnswer(currentRequest, SUCCESS);
                write (evClose, toClose, 8);
                break;
            }

            // Creazione di un nuovo file vuoto //
            case PUC: {
                pthread_mutex_lock(&mutex);
                printf ("Capacity: %d/%d\n",fileCount,storageDim);
                int res = 0;

                // Se sono a capacità massima avverto il client dell'espulsione di un file e aspetto feedback
                if (fileCount == storageDim) {
                    if (expelFile(currentRequest,PUC,reqArg)==-1) {
                        res = -1;
                        END_REQ(PUC)
                    }
                }
                // Ricavo nome file
                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    res = -1;
                    END_REQ(PUC)
                }

                // Se è il primo file nello storage lo inizializzo
                else {
                    if (storage == NULL) {
                        storage = initStorage (NULL, reqArg, 0);
                        fileCount++;
                        END_REQ(PUC)
                    }
                    else {
                        // Se non è il primo file lo aggiungo
                        fileNode * toCr;
                        if (searchFile(reqArg,storage,&toCr) == -1) {
                            lastAddedFile->next = newFile (NULL, 0, reqArg, 0, &lastAddedFile);
                            lastAddedFile = lastAddedFile->next;
                            fileCount++;
                            printf("Last file: %s\n",lastAddedFile->fileName);
                            END_REQ(PUC)
                        }
                        else {
                            res = -1;
                            END_REQ(PUC);
                        }
                    }
                    pthread_mutex_unlock(&mutex);
                }
                break;
            }

            // Lettura dei primi n file nello storage //
            case RDM: {
                reqArg = strtok_r(NULL, EOBUFF, &ptr);
                int N = atoi(reqArg);

                pthread_mutex_lock(&mutex);
                fileNode * currentFile = storage;
                char msg [MAX_NAME_LEN];
                int res = 0;

                // Invio al client il numero di file disponibili che invierò
                char availableF [32];
                int max;
                if (N==0 || N>fileCount) {
                    max = fileCount;
                    sprintf(availableF, "%d£",fileCount);}
                else {
                    max = N;
                    sprintf(availableF, "%d£",N);}
            
                size_t nToWrite = sizeof(char) * strlen(availableF);
                ssize_t nWritten = 1;
                size_t totBytesWritten = 0;
                while ( nToWrite > 0 && nWritten!=0) {
                    if ((nWritten = write(currentRequest, availableF+totBytesWritten, nToWrite))==-1) {
                        res = -1;
                        goto logs;
                    }
                    nToWrite -= nWritten;
                    totBytesWritten += nWritten;
                }

                // Se non ho file da inviare loggo il fallimento dell'operazione
                if (max==0) {
                    res = -1;
                    goto logs;
                }

                // Aspetto che il client mi dica di procedere com l'invio dei file
                if (waitForAck(currentRequest)==-1) {
                    res = -1;
                    goto logs;
                }                

                // Invio max file ognuno preceduto da un header con nome e taglia
                for (int i=0;i<max;i++) {
                    
                    // Invio header
                    if (sendHeader(currentRequest,currentFile->fileName,currentFile->fileSize)==-1) {
                        res = -1;
                        goto logs;
                    }
                    
                    // Aspetto l'ok del client e invio il file
                    if (waitForAck(currentRequest)==-1) {
                        res=-1;
                        break;}
                    res = sendFile(currentRequest,currentFile,RDM);
                    if (res==-1) goto logs;
                    currentFile=currentFile->next;

                    // Aspetto feedback dal client prima di procedere con il file successivo
                    if (waitForAck(currentRequest)==-1) {
                        res=-1;
                        break;}
                }
                
                logs:
                END_NOANSW(RDM)
                pthread_mutex_unlock(&mutex);
                break;
            }

            // Creazione di un file locked //
            case PRC: {
                pthread_mutex_lock(&mutex);
                int res = 0;
                if (fileCount >= capacity) {
                    while (fileCount>=capacity) {
                        storage = popFile(storage);
                        fileCount --;
                        // TO DO: send expelled file and log
                    }
                }

                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) res = -1;

                else {
                    if (storage == NULL) {
                        storage = initStorage (NULL, reqArg, currentRequest);
                        fileCount++;
                    }
                    else {
                        fileNode * toCr;
                        if (searchFile(reqArg,storage,&toCr) == -1) {
                            lastAddedFile->next = newFile (NULL, 0, reqArg, currentRequest, &lastAddedFile);
                            lastAddedFile = lastAddedFile->next;
                            fileCount++;
                            printf("Last file: %s\n",lastAddedFile->fileName);
                        }
                        else res = -1;
                        
                    }
                }
                END_REQ(PRC)
                pthread_mutex_unlock(&mutex);
                break;
            }

            // Unlock di un file //
            case ULC: {
                pthread_mutex_lock(&mutex);
                int res = 0;
                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) res =-1;

                else {
                    fileNode * fToUnl;
                    // Fallisco se il file non è presente
                    res = searchFile (reqArg, storage, &fToUnl);

                    if (res!=-1) {
                        // Successo se il file è acquisito dal processo
                        if (fToUnl->owner==currentRequest) { 
                            fToUnl->owner=0;
                        }
                        // Fallimento altrimenti
                        else res = -1;
                    }
                }
                END_REQ(ULC)
                pthread_mutex_unlock(&mutex);
                break;
            }

            // Lock di un file //
            case LCK: {
                pthread_mutex_lock(&mutex);
                int res = 0;
                reqArg = strtok_r (NULL, ",", &ptr);
                if (reqArg == NULL) res = -1;

                else {
                    fileNode * fToLc;
                    char pathname [MAX_NAME_LEN];
                    strcpy(pathname,reqArg);

                    // Se il file non esiste l'operazione fallisce
                    int res = searchFile (pathname, storage, &fToLc);
                    if (res!=-1){
                        
                        // Se non è locked o è stato già acquisito dal processo l'operazione ha successo
                        if (fToLc->owner == currentRequest || fToLc->owner==0) { 
                            fToLc->owner = currentRequest;
                        }
                        else {
                            // Leggo il tempo massimo di attesa per acquisire la lock
                            int msec = atoi(strtok_r(NULL,EOBUFF,&ptr));
                            struct timespec timeout;
                            // Se non specificato default di 1 sec
                            if (msec==0) msec = 1000;
                            timeout.tv_nsec = msec * 1000000;
                            timeout.tv_sec = msec * 0.001;
                            int released = 0;
                            time_t et = 0;
                            time_t start = clock();

                            // Ogni volta che viene rilasciato un file sono notificato e controllo se è quello richiesto
                            while (!released) {
                                if (pthread_cond_timedwait(&fileUnlocked,&mutex,&timeout)==-1) break;
                                if ((res = searchFile (pathname, storage, &fToLc))==-1) break;
                                // Se è stato rilasciato acquisisco lock
                                if (fToLc->owner==0) {
                                    fToLc->owner=currentRequest;
                                    released = 1;
                                }
                                // Altrimenti riprovo fino a che non lo acquisisco o allo scadere del timeout
                                et+=clock()-start;
                                if (et>=timeout.tv_nsec) break;
                            }

                            // Se il tempo scade senza che il file venga rilasciato l'operazione fallisce
                            if (released!=1) res = -1;
                        }
                    }
                }
                END_REQ(LCK)
                pthread_mutex_unlock(&mutex);
                break;   
            }

            default:
                TEST
                break;
        }
    
    free (toFree);
    free (tk);
    }
}

int sendFile (int fd, fileNode * f, int op) {
    //Copio il contenuto del file
    size_t writeSize = f->fileSize;
    void * buff = malloc(writeSize);
    memset (buff, 0, writeSize);
    void * toFree = buff;
    fseek (f->fPointer,0,SEEK_SET);
    size_t r = fread (buff, 1, writeSize, f->fPointer);

    // Se non ho letto tutto il file nel buffer e c'è un errore
    if (r!=writeSize && !feof(f->fPointer)) {
        free (buff);
        return -1;
    }
    printf ("SEND %li of: %s\n",r,(char*)buff);   //TEST
    fseek (f->fPointer,0,SEEK_END);

    // Invio al client
    ssize_t nWritten;
    size_t totBytesWritten = 0;
    while (writeSize > 0) {
        if((nWritten = write(fd, buff+totBytesWritten, writeSize))==-1) {
            free (buff);
            return -1;
        }
        writeSize -= nWritten;
        totBytesWritten += nWritten;
    }
    // Per RDM c'è il rischio che EOBUFF finisca nell'header successivo
    if (op!=RDM) write(fd, EOBUFF, EOB_SIZE);
    free (toFree);
    return 0; 
}

void logOperation (int op, int process, int res, char * file) {

    if (logging!=NULL) {
        switch (op)
            {
            case RD:
                fprintf (logging,"Read, file %s, fd %d, result %d\n",file,process,res);
                break;

            case RDM:
                fprintf (logging, "Multiple read for process %d\n",process);
                break;
            
            case WR:
                fprintf (logging,"Write, file %s, fd %d, result %d\n",file,process,res);
                break;

            case OP:
                fprintf (logging,"Open, file %s, fd %d, result %d\n",file,process,res);
                break;

            case CF:
                fprintf (logging,"Close, file %s, fd %d, result %d\n",file,process,res);
                break;

            case RM:
                fprintf (logging,"Remove, file %s, fd %d, result %d\n",file,process,res);
                break;
            
            case EXF:
                fprintf (logging,"Expel, file %s, fd %d, result %d\n",file,process,res);
                break;

            case PUC:
                fprintf (logging,"Create unlocked, file %s, fd %d, result %d\n",file,process,res);
                break;
            
            case PRC:
                fprintf (logging,"Create locked, file %s, fd %d, result %d\n",file,process,res);
                break;

            case LCK:
                fprintf (logging,"Lock, file %s, fd %d, result %d\n",file,process,res);
                break;

            case ULC:
                fprintf (logging,"Unlock, file %s, fd %d, result %d\n",file,process,res);
                break;

            case CC:
                fprintf (logging,"Closed connection with fd %d\n",process);
                break;

            case OC:
                fprintf (logging,"Accepted connection from fd %d\n",process);
                break;

            default:
                break;
            }
    }
}

void setIqFl () {
    sigiq = 1;
}

void setShFl () {
    sighup = 1;
}

void writeListenFd (int fd) {

    char * toListen = calloc(8,1);
    sprintf (toListen,"%d",fd);
    if (write (fdListen, toListen, 8) ==-1) {
        free (toListen);
        errEx();}
    free (toListen);
}

int waitForAck (int fd) {
    int res = 0;
    void * ack = malloc(MAX_BUF_SIZE);
    void * toFree = ack;
    size_t nToRead = MAX_BUF_SIZE;
    ssize_t nRead = 1;
    ssize_t totBytesRead = 0;
    while (nToRead > 0 && nRead!=0) {
        if (totBytesRead!=0 && bufferCheck(ack)==1) break;
        if ((nRead = read(fd,ack+totBytesRead,nToRead)==-1)) {
            res = -1;
            break;
            }
        totBytesRead += nRead;
        nToRead -= nRead;
    }
    free (toFree);
    return res;
}

int sendHeader (int fd, char * pathname, ssize_t size) {
    char s [32];
    sprintf(s,"%li",size);
    int headerLen = strlen(pathname)+strlen(",£")+strlen(s);
    char msg[headerLen+1];
    sprintf (msg, "%s,%li£", pathname, size);
    printf("Header: %s %li\n",msg,sizeof(char)*strlen(msg));

    int res = 0;
    ssize_t nToWrite = sizeof(char) * strlen (msg);
    ssize_t nWritten = 1;
    ssize_t totBytesWritten = 0;
    while (nToWrite>0 && nWritten!=0){
        if ((nWritten=write (fd, msg+totBytesWritten, nToWrite))==-1) {
            res = -1;
            break;
        }
        totBytesWritten+=nWritten;
        nToWrite-=nWritten;
    }
    printf("Written: %li\n",totBytesWritten);
    return res;
}

int expelFile (int fd, int op, char * fName) {
    
    // Se sono a capacità massima avverto il client dell'espulsione di un file e aspetto feedback
    if (sendAnswer(fd,EXPEL)==-1 || waitForAck(fd)==-1) {
        logOperation(op, fd, FAILURE, fName);
        writeListenFd (fd);
        return -1;
    }

    // Invio nome e taglia del file da espellere e a seguire il contenuto
    if (sendHeader(fd,storage->fileName,storage->fileSize) == -1 || sendFile(fd,storage,op)==-1) {
        logOperation(op, fd, FAILURE, fName);
        writeListenFd (fd);
        return -1;
    }

    // Elimino il file dallo storage
    usedBytes-= storage->fileSize;
    storage=popFile(storage);
    fileCount--;
    return 0;
}
