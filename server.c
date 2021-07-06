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

/* Invia esito res di un'operazione al client fd.
    Restituisce 0 se l'invio ha successo, -1 altrimenti
*/
int sendAnswer (int fd, int res);

/* Invia un file f al client fd.
    Restituisce 0 se l'invio ha successo, -1 altrimenti
*/
int sendFile (int file, fileNode * f);

int threadQuantity;
int storageDim;
int capacity;
int queueLenght;
int maxClients;

node * requestsQueue=NULL;
node * lastRequest;
pthread_mutex_t mutex;

int fileCount=0;
fileNode * storage=NULL;
fileNode * lastAddedFile=NULL;

int evClose;



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
    int i;

    for (i=0;i<CONF_PAR;i++){
        fgets(confStr, MAX_NAME_LEN, conf);
        currTok=strtok(confStr, ";");
        init(i, currTok);
    }
    
    if (fclose(conf)!=0) errEx();

}

void startServer () {

    int serverSFD;
    if ((serverSFD = socket(AF_UNIX, SOCK_STREAM,0))==-1)
        errEx();

    node * socketsList = addNode(serverSFD);
    node * currSock = socketsList;

    struct sockaddr_un address;
    address.sun_family = AF_UNIX;
    
    strcpy(address.sun_path, "./server");
    
    if( bind(serverSFD, (struct sockaddr *)&address, sizeof(address))==-1)   //Permission denied, need sudo
        errEx();
    

    pthread_t * workers = malloc(sizeof(pthread_t)*threadQuantity);

    int wsFailed;
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
    // Client + server + eventFD
    int maxFd = maxClients + 2;     
    struct pollfd * connectionFDS = malloc(sizeof(struct pollfd)*maxFd);
    connectionFDS[0].fd = serverSFD;
    connectionFDS[0].events = POLLIN;

    connectionFDS[1].fd = evClose;
    connectionFDS[1].events = POLLIN;

    for (int i=2; i < maxFd; i++) { 
        connectionFDS[i].fd = -1;
        connectionFDS[i].events = POLLIN; 
    }
    int pollRes;
    int timeout = 60*1000;      //1 min

    while(TRUE) {

        //Controllo se devo chiudere una socket prima della poll
        if (connectionFDS[1].revents == POLLIN) {
            void * eventB = malloc(8);
            read (evClose, eventB, 8);
            int toClose = eventB;
            //printf("%d\n",toClose);
            for (int i=2; i<maxFd; i++)
                if (connectionFDS[i].fd==toClose) connectionFDS[i].fd=-1;
            deleteNode(toClose,&socketsList);
            free(eventB);
        }

        //Controllo se ci sono altre richieste
        if ((pollRes = poll(connectionFDS,maxFd,timeout))==-1)
            errEx();
        if (pollRes==0) {
            printf("Sessione scaduta\n");
            //no clients connected atm
        }

        //if client richiede connect
        if (connectionFDS[0].revents == POLLIN) {
            int j = 2;
            while(connectionFDS[j].fd!=-1 && j<maxFd) j++; 
            //controllo se ho spazio per gestire più client
            if (j<=maxFd) { 
                connectionFDS[j].fd = accept (serverSFD, NULL, 0);  //se posso accetto connessione
                currSock->next=addNode(connectionFDS[j].fd);        // per eventuale cleanup
                currSock = currSock->next;
            }
            else printf ("Tentata connessione\n");      //se non posso stampo un avvertimento
            
        }
        
        //Se un client già connesso ha un'altra richiesta
        for (int i=2;i<maxFd;i++){
            if (connectionFDS[i].revents==POLLIN && connectionFDS[i].fd!=-1) {
                if (requestsQueue == NULL) { 
                    requestsQueue = addNode(connectionFDS[i].fd);
                    lastRequest = requestsQueue;
                }
                else { lastRequest->next = addNode(connectionFDS[i].fd);
                    lastRequest=lastRequest->next;
                }
            }
        }
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
       deleteFile(storage, &storage, &lastAddedFile, &fileCount);
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
    list->owner=fOwner;
    list->next=NULL;
    list->prev=NULL;
    list->fPointer=f;
    strcpy(list->fileName,fname);
    lastAddedFile=list;
    return list;

}

void * manageRequest() {

    int currentRequest;     //richiesta che il thread sta servendo

    while(TRUE) {               //finchè accetto nuove richieste

        if (requestsQueue!=NULL) {
            pthread_mutex_lock(&mutex);
            currentRequest = requestsQueue->descriptor;
            popNode(requestsQueue);

            ssize_t reqRes = 1;
            size_t tr = MAX_BUF_SIZE+MAX_FILE_SIZE;
            void * buffer = malloc(tr);
            size_t totBytesR=0;

            while (tr>0 && reqRes!=0) {
                if (totBytesR!=0 && bufferCheck(buffer,totBytesR)==1)
                    break;
                if ((reqRes=read(currentRequest, buffer, tr))==-1)
                    errEx();
                totBytesR+=reqRes;
                tr-=reqRes;
            }
            pthread_mutex_unlock(&mutex);

            //formato richiesta: codice operazione, nomeFile, eventuale file
            char * request;
            char * toTok=malloc(sizeof(char)*MAX_BUF_SIZE);
            memcpy(toTok,(char *)buffer,MAX_BUF_SIZE);
            request = strtok(toTok, ",");

            int code = atoi(request);
            char * reqArg;

            switch (code) {

                // Richiesta di lettura
                case RD:
                    reqArg = strtok(NULL, ",");
                    if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);

                    else {
                        pthread_mutex_lock(&mutex);
                        fileNode * fToRd;
                        // Controllo la presenza del file, lo invio se trovato altrimento riporto il fallimento
                        if ((fToRd=searchFile(reqArg, storage))==NULL) sendAnswer(currentRequest, FAILURE);
                        else if (sendFile(currentRequest, fToRd)==-1) errEx();
                        pthread_mutex_unlock(&mutex);
                    }
                    break;
                
                // Richiesta di scrittura
                case WR:
                    reqArg = strtok(NULL, ",");
                    if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);

                    else {
                        pthread_mutex_lock(&mutex);
                        char fullRequest [MAX_BUF_SIZE];
                        strcpy(fullRequest, reqArg);
                        strcat(fullRequest, ",");

                        // Estraggo il nome
                        char fileName [MAX_NAME_LEN];
                        strcpy(fileName, reqArg);
                        strcat(fullRequest, reqArg);
                        strcat (fullRequest, ",");

                        // Estraggo la taglia del file
                        reqArg = strtok(NULL, "-0-");
                        if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);
                        else {
                            int fSize = atol (reqArg);
                            strcat(fullRequest,reqArg);
                            strcat(fullRequest,"-0-");       // Salvo in fullRequest tutta la stringa di richiesta

                            // Controllo che il file sia stato creato
                            fileNode * fToWr;
                            if ((fToWr=searchFile(fileName, storage))==NULL) sendAnswer(currentRequest, FAILURE);

                            else {
                                // scrivo il contenuto
                                int notContentLen = strlen(fullRequest);
                                void * content = buffer;
                                content += notContentLen;

                                if (fToWr->fPointer==NULL) {
                                    FILE * newF = fmemopen(NULL,fSize,"w+");
                                    fwrite(content,sizeof(char),fSize, newF);
                                    fToWr->fPointer=newF;
                                    fToWr->fileSize=fSize;
                                }
                            }
                        }
                        pthread_mutex_unlock(&mutex);
                    }
                    break;
                
                // Apertura di un file
                case OP:
                    reqArg = strtok(NULL, ",");
                    if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);
                    
                    else {
                        fileNode * fToOp;
                        pthread_mutex_lock(&mutex);
                        if ((fToOp=searchFile(reqArg, storage))==NULL) 
                            sendAnswer(currentRequest, FAILURE);
                        
                        else sendAnswer(currentRequest, SUCCESS);
                        pthread_mutex_unlock(&mutex);
                    }
                    break;

                // Rimozione di un file
                case RM:
                    reqArg = strtok (NULL, ",");
                    if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);

                    else {
                        pthread_mutex_lock(&mutex);
                        fileNode * fToRm;
                        if ((fToRm=searchFile(reqArg, storage))==NULL) sendAnswer(currentRequest, FAILURE);
                        else {
                            deleteFile (fToRm, &storage, &lastAddedFile, &fileCount);
                            sendAnswer(currentRequest, SUCCESS);
                        }
                        pthread_mutex_unlock(&mutex);
                    }
                    break;
                
                case CLS: {
                    void * toClose =malloc(8);
                    toClose = currentRequest;
                    //printf("%d\n",toClose);
                    write (evClose, toClose, 8);
                    close(currentRequest);
                    free(toClose);
                    break;
                }
                
                case PUC:
                    // Creazione di un nuovo file vuoto
                    reqArg = strtok (NULL, ",");
                    if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);
                    else {
                        pthread_mutex_lock(&mutex);
                        if (storage == NULL) initStorage (NULL, reqArg, 0);
                        else {
                            if (searchFile(reqArg,storage) == NULL) {
                                addFile (NULL, 0, reqArg, 0, &lastAddedFile, &fileCount);
                                sendAnswer (currentRequest, SUCCESS);
                            }
                            else sendAnswer (currentRequest, FAILURE);
                        }
                        pthread_mutex_unlock(&mutex);
                    }
                    break;

                case RDM:
                    reqArg = strtok(NULL, ",");
                    int N = atoi(reqArg);
                    pthread_mutex_lock(&mutex);

                    fileNode * currentFile = storage;
                    char * msg = NULL;
                    char * fileSize = NULL;

                    // Leggo tutti i file disponibili
                    if (N==0) {
                        while (currentFile!=NULL) {
                            strcpy(msg, currentFile->fileName);
                            strcat(msg,",");
                            sprintf(fileSize,"%li",currentFile->fileSize);
                            strcat(msg,fileSize);
                            strcat(msg,"\n");
                            // Messaggio al client: nome_file,taglia_file \n contenuto
                            if (write (currentRequest, msg, strlen(msg))==-1) break;
                            sendFile(currentRequest,currentFile);
                            currentFile=currentFile->next;
                        }
                    }
                    // Leggo N file
                    else {
                        int j=1;
                        while (currentFile!=NULL && j<=N && j<=fileCount) {
                            strcpy(msg, currentFile->fileName);
                            strcat(msg,",");
                            sprintf(fileSize,"%li",currentFile->fileSize);
                            strcat(msg,fileSize);
                            strcat(msg,"\n");
                            // Messaggio al client: nome_file,taglia_file \n contenuto
                            if (write (currentRequest, msg, strlen(msg))==-1) break;
                            sendFile(currentRequest,currentFile);
                            currentFile=currentFile->next;
                            j++;                    
                        }

                    }
                    pthread_mutex_unlock(&mutex);
                    break;

                default:
                    TEST
                    break;
            }

        free (buffer);
        }
    }
}

int sendAnswer (int fd, int res) {
    size_t writeSize = sizeof(res)+sizeof(char)*strlen(EOBUFF);
    void * buff = malloc (writeSize);

    if(res==SUCCESS) strcpy(buff,"0 ");
    else strcpy(buff,"-1");
    strcat(buff, EOBUFF);

    ssize_t nWritten;
    while ( writeSize > 0 && nWritten!=0) {
        if ((nWritten = write(fd, buff, writeSize))==-1) {
            free (buff);
            break;
        }
        writeSize -= nWritten;
    }

    if (nWritten==-1) return -1;
    return 0;
}

int sendFile (int fd, fileNode * f) {
    //Copio il contenuto del file
    size_t writeSize = MAX_FILE_SIZE;
    void * buff = malloc(writeSize);
    fread (buff, sizeof(char), MAX_FILE_SIZE, f->fPointer);
    if (!feof(f->fPointer)) {
        free (buff);
        return -1;
    }

    // Invio al client
    ssize_t nWritten;
    while (writeSize > 0) {
        if((nWritten = write(fd, buff, writeSize))==-1) {
            free (buff);
            return -1;
        }
        writeSize -= nWritten;
    }
    free (buff);
    return 0;
    
}
