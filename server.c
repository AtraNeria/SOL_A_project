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

/* Invia un file f al client fd.
    Restituisce 0 se l'invio ha successo, -1 altrimenti
*/
int sendFile (int file, fileNode * f);

/* Aspetta che il client legato alla socket fd invii una risposta di feedback.
    Restituisce 0 se lo riceve con successo. -1 altrimenti
*/
int waitForAck (int fd);

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
int capacity;
int queueLenght;
int maxClients;
FILE * logging;

node * requestsQueue=NULL;
node * lastRequest;
pthread_mutex_t mutex;
pthread_cond_t newReq = PTHREAD_COND_INITIALIZER;
pthread_cond_t fileUnlocked = PTHREAD_COND_INITIALIZER;

int fileCount = 0;
fileNode * storage=NULL;
fileNode * lastAddedFile=NULL;

int evClose;
int fdListen;



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
        capacity=atoi(string);
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
            printf("Listen: %d\n",toListen);
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
        while (requestsQueue==NULL ) pthread_cond_wait(&newReq,&mutex);
        currentRequest = requestsQueue->descriptor;
        printf("Serving: %d\n",currentRequest);
        requestsQueue = popNode(requestsQueue);
        if (requestsQueue==NULL) lastRequest=NULL;

        ssize_t reqRes = 1;
        size_t tr = MAX_BUF_SIZE;
        void * buffer = malloc(tr);
        memset (buffer, 0, tr);
        void * toFree = buffer;
        size_t totBytesR=0;

        while (tr>0 && reqRes!=0) {
            if (totBytesR!=0 && bufferCheck(buffer)==1)
                    break;
            if ((reqRes=read(currentRequest, buffer+totBytesR, tr))==-1)
                errEx();
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
        char * opCode = strtok_r(request, ",",&ptr);
        int code = atoi(opCode);

        printf("%d %s\n",code,request); //TEST
        char * reqArg;

        switch (code) {

            // Richiesta di lettura
            case RD: {
                reqArg = strtok_r(NULL, EOBUFF,&ptr);
                if (reqArg == NULL) {
                    pthread_mutex_lock(&mutex);
                    logOperation(RD, currentRequest, FAILURE, reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer (currentRequest, FAILURE);
                    pthread_mutex_unlock(&mutex);
                }

                else {
                    pthread_mutex_lock(&mutex);
                    fileNode * fToRd;
                    int res = 0;

                    // Controllo la presenza
                    if ((res=searchFile(reqArg, storage, &fToRd))==-1) goto log;
                    printf ("FILE: %s\n",fToRd->fileName);
                    printf("SIZE: %li\n",fToRd->fileSize);
                    // Invio la taglia
                    if ((res=sendAnswer(currentRequest,fToRd->fileSize))==-1) goto log;
                    // Invio il contenuto
                    if ((res=sendFile(currentRequest,fToRd))==-1) goto log;

                    // Se l'invio fallisce lo riporto
                    log:
                    logOperation(RD, currentRequest, SUCCESS, reqArg);
                    writeListenFd (currentRequest);
                    pthread_mutex_unlock(&mutex);
                }

                break;
            }
                
            // Richiesta di scrittura
            case WR: {
                reqArg = strtok_r(NULL, "\n", &ptr);
                if (reqArg == NULL) {
                    logOperation(WR, currentRequest, FAILURE, reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer (currentRequest, FAILURE);}

                else {
                    pthread_mutex_lock(&mutex);
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
                    int res = 0;
                    if (searchFile(fileName, storage, &fToWr) == -1) {
                        res = -1;
                        goto logging;
                    }

                    else {
                        // Controllo se si dispone dei permessi per scrivere sul file
                        if(fToWr->owner!=0 && fToWr->owner!=currentRequest) {
                            res = -1;
                            goto logging;
                        }
                        else {
                            // Scrivo il contenuto eventualmente letto nel buffer precedente
                            void * buff2 = calloc(MAX_BUF_SIZE,1);
                            void * toFree = buff2;
                            memcpy (buff2,buffer,MAX_BUF_SIZE);

                            buff2 += reqLen;
                            size_t partialDataLen = MAX_BUF_SIZE-sizeof(char)*reqLen;
                            void * content = calloc(fSize, 1);
                            void * conToFree = content;
                            if (fSize<partialDataLen)
                                memcpy (content, buff2, fSize);
                            else memcpy (content, buff2, partialDataLen); 
                            printf("Buffer: %s \n",content);        //TEST

                            // Leggo il restante contenuto
                            ssize_t dataToRd = fSize - partialDataLen;
                            printf("Remaining data: %li\n" 
                                    "PartialData Lenght: %li\n" 
                                    "Total size: %d\n" 
                                    "Request lenght: %d\n",dataToRd,partialDataLen,fSize,reqLen);

                            size_t readRes = 1;
                            size_t totDataBytesR = 0;
                            while (dataToRd>0 && readRes!=0) {
                                if (totDataBytesR!=0 && bufferCheck(content)==1) break;
                                if ((readRes = read(currentRequest, content+totBytesR, dataToRd))== -1) break;
                                totDataBytesR += readRes;
                                dataToRd -= readRes;
                            }
                            
                            printf("Res: %d\nBuffer: %s \n",readRes,content);   //TEST

                            // Se ho riscontrato problemi nella lettura indico la client di aver fallito
                            if (readRes == -1) {
                                free(conToFree);
                                free(toFree);
                                res = -1;
                                goto logging;

                            }

                            // Altrimenti salvo il file e segnalo l'azione avvenuta con successo
                            else {
                                if (fToWr->fPointer==NULL) {
                                    FILE * newF = fmemopen(NULL,fSize+1,"w+");
                                    size_t r = fwrite(content,1,fSize, newF);
                                    if (r!=fSize) {
                                        res = -1;
                                        free(conToFree);
                                        free(toFree);
                                        goto logging;
                                    }
                                    fToWr->fPointer=newF;
                                    fToWr->fileSize=fSize;
                                }
                                free(conToFree);
                                free(toFree);
                                logging:
                                logOperation(WR, currentRequest, res, reqArg);
                                writeListenFd (currentRequest);
                                sendAnswer(currentRequest, res);
                            }
                        }
                    }
                    pthread_mutex_unlock(&mutex);
                }
                break;
            }
                
            // Apertura di un file
            case OP: {
                reqArg = strtok_r(NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    logOperation(OP, currentRequest, FAILURE, reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer(currentRequest, FAILURE);
                    }
                    
                else {
                    fileNode * fToOp;
                    pthread_mutex_lock(&mutex);
                    if (searchFile(reqArg, storage, &fToOp)==-1) {
                        logOperation(OP, currentRequest, FAILURE, reqArg);
                        writeListenFd (currentRequest);
                        sendAnswer(currentRequest, FAILURE);
                    }
                    else {
                        logOperation(OP, currentRequest, SUCCESS, reqArg);
                        writeListenFd (currentRequest);
                        sendAnswer(currentRequest, SUCCESS);
                    }
                    pthread_mutex_unlock(&mutex);
                }
                break;
            }

            // Rimozione di un file
            case RM: {
                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    logOperation(RM, currentRequest, FAILURE, reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer (currentRequest, FAILURE);
                }

                else {
                    pthread_mutex_lock(&mutex);
                    fileNode * fToRm;
                    // Se il file non esiste l'operazione ha automaticamente successo
                    if (searchFile(reqArg, storage, &fToRm)==-1) {
                        logOperation(RM, currentRequest, SUCCESS, reqArg);
                        writeListenFd (currentRequest);
                        sendAnswer(currentRequest, SUCCESS);
                    }
                    else {
                        deleteFile (fToRm, &storage, &lastAddedFile);
                        fileCount--;
                        logOperation(RM, currentRequest, SUCCESS, reqArg);
                        writeListenFd (currentRequest);
                        sendAnswer(currentRequest, SUCCESS);
                    }
                    pthread_mutex_unlock(&mutex);
                }
                break;
            }

            // Chiusura di una socket client          
            case CLS: {
                void * toClose = &currentRequest;
                sendAnswer(currentRequest, SUCCESS);
                write (evClose, toClose, 8);
                break;
            }

            // Creazione di un nuovo file vuoto
            case PUC: {
                pthread_mutex_lock(&mutex);
                if (fileCount >= capacity) {
                    while (fileCount>=capacity) {
                        storage = popFile(storage);
                        fileCount --;
                        // TO DO: send expelled files and log
                    }
                }

                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    logOperation(PUC, currentRequest, FAILURE, reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer (currentRequest, FAILURE);
                }

                else {
                    if (storage == NULL) {
                        storage = initStorage (NULL, reqArg, 0);
                        fileCount++;
                        logOperation(PUC, currentRequest, SUCCESS, reqArg);
                        writeListenFd (currentRequest);
                        sendAnswer(currentRequest,SUCCESS);
                    }
                    else {
                        fileNode * toCr;
                        if (searchFile(reqArg,storage,&toCr) == -1) {
                            lastAddedFile->next = newFile (NULL, 0, reqArg, 0, &lastAddedFile);
                            lastAddedFile = lastAddedFile->next;
                            fileCount++;
                            printf("Last file: %s\n",lastAddedFile->fileName);
                            logOperation(PUC, currentRequest, SUCCESS, reqArg);
                            writeListenFd (currentRequest);
                            sendAnswer (currentRequest, SUCCESS);
                        }
                        else {
                            logOperation(PUC, currentRequest, FAILURE, reqArg);
                            writeListenFd (currentRequest);
                            sendAnswer (currentRequest, FAILURE);
                        }
                    }
                    pthread_mutex_unlock(&mutex);
                }
                break;
            }

            // Lettura dei primi n file nello storage
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
                    
                    sprintf (msg, "%s,%li£", currentFile->fileName, currentFile->fileSize);
                    printf("Header: %s %d\n",msg,sizeof(char)*strlen(msg));

                    nToWrite = sizeof(char) * strlen (msg);
                    nWritten = 1;
                    totBytesWritten = 0;
                    while (nToWrite>0 && nWritten!=0){
                        if ((nWritten=write (currentRequest, msg+totBytesWritten, nToWrite))==-1) {
                            res = -1;
                            break;
                        }
                        totBytesWritten+=nWritten;
                        nToWrite-=nWritten;
                    }
                    printf("Written: %d\n",totBytesWritten);
                    
                    // Aspetto l'ok del client e invio il file
                    if (waitForAck(currentRequest)==-1) {
                        res=-1;
                        break;}
                    res = sendFile(currentRequest,currentFile);
                    if (res==-1) goto logs;
                    currentFile=currentFile->next;

                    // Aspetto feedback dal client prima di procedere con il file successivo
                    if (waitForAck(currentRequest)==-1) {
                        res=-1;
                        break;}
                }
                
                logs:
                logOperation (RDM, currentRequest,res,NULL);
                writeListenFd (currentRequest);
                pthread_mutex_unlock(&mutex);
                break;
            }

            // Creazione di un file locked
            case PRC: {
                pthread_mutex_lock(&mutex);
                if (fileCount >= capacity) {
                    while (fileCount>=capacity) {
                        storage = popFile(storage);
                        fileCount --;
                        // TO DO: send expelled file and log
                    }
                }

                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    logOperation(PRC,currentRequest,FAILURE,reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer (currentRequest, FAILURE);
                }

                else {
                    if (storage == NULL) {
                        storage = initStorage (NULL, reqArg, currentRequest);
                        fileCount++;
                        logOperation(PRC,currentRequest,SUCCESS,reqArg);
                        writeListenFd (currentRequest);
                        sendAnswer(currentRequest,SUCCESS);
                    }
                    else {
                        fileNode * toCr;
                        if (searchFile(reqArg,storage,&toCr) == -1) {
                            lastAddedFile->next = newFile (NULL, 0, reqArg, currentRequest, &lastAddedFile);
                            lastAddedFile = lastAddedFile->next;
                            fileCount++;
                            printf("Last file: %s\n",lastAddedFile->fileName);
                            logOperation(PRC,currentRequest,SUCCESS,reqArg);
                            writeListenFd (currentRequest);
                            sendAnswer (currentRequest, SUCCESS);
                        }
                        else {
                            logOperation (PRC,currentRequest,FAILURE,reqArg);
                            writeListenFd (currentRequest);
                            sendAnswer (currentRequest, FAILURE);
                        }
                    }
                    pthread_mutex_unlock(&mutex);
                }
                break;
            }

            // Unlock di un file
            case ULC: {
                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    logOperation (ULC, currentRequest, FAILURE, reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer (currentRequest, FAILURE);
                }

                else {
                    pthread_mutex_lock(&mutex);
                    fileNode * fToUnl;
                    int res = searchFile (reqArg, storage, &fToUnl);

                    if (res==-1) {
                        logOperation (ULC, currentRequest, FAILURE, reqArg);
                        writeListenFd (currentRequest);
                        sendAnswer(currentRequest, FAILURE);
                    }
                    else {
                        if (fToUnl->owner==currentRequest) { 
                            fToUnl->owner=0;
                            logOperation (ULC, currentRequest, SUCCESS, reqArg);
                            writeListenFd (currentRequest);
                            sendAnswer(currentRequest,SUCCESS);
                        }
                        else {
                            logOperation (ULC, currentRequest, FAILURE, reqArg);
                            writeListenFd (currentRequest);
                            sendAnswer(currentRequest, FAILURE);
                        }
                    }
                    pthread_mutex_unlock(&mutex);
                }
                break;   
            }

            // Lock di un file
            case LCK: {
                reqArg = strtok_r (NULL, EOBUFF, &ptr);
                if (reqArg == NULL) {
                    logOperation (LCK, currentRequest, FAILURE, reqArg);
                    writeListenFd (currentRequest);
                    sendAnswer (currentRequest, FAILURE);
                }

                else {
                    pthread_mutex_lock(&mutex);
                    fileNode * fToLc;
                    int res = searchFile (reqArg, storage, &fToLc);

                    if (res==-1) {
                        logOperation (LCK, currentRequest, FAILURE, reqArg);
                        writeListenFd (currentRequest);       
                        sendAnswer(currentRequest, FAILURE);
                    }
                    else {
                        if (fToLc->owner == currentRequest || fToLc->owner==0) { 
                            fToLc->owner = currentRequest;
                            logOperation (LCK, currentRequest, SUCCESS, reqArg);
                            writeListenFd (currentRequest);
                            sendAnswer(currentRequest,SUCCESS);
                        }
                        else {
                            // TO DO: se c'è lho locked come fa a essere rilasciato? Also, control timeout
                            int released = 0;
                            while (!released) {
                                pthread_cond_wait(&fileUnlocked,&mutex);
                                if (fToLc->owner==0) {
                                    fToLc->owner=currentRequest;
                                    released=1;
                                }
                            }

                        }
                    }
                    pthread_mutex_unlock(&mutex);
                }
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

int sendFile (int fd, fileNode * f) {
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
    write(fd, EOBUFF, EOB_SIZE);    // SU RDM A VOLTE FINISCE NELL'HEADER SUCCESSIVO TEST
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
