#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
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

/* Invia esito res di un'operazione al client fd
    Restituisce 0 se l'invio ha successo, -1 altrimenti
*/
int sendAnswer (int fd, int res);

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
            printf("Numero client richiesti troppo alto, impossibile configurare il server");
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

    struct sockaddr_in address;
    if( bind(serverSFD, (struct sockaddr *)&address, sizeof(address))==-1)
        errEx();


    pthread_t workers [threadQuantity];

    int wsFailed;
    if ((wsFailed=createWorkers(workers)) !=-1) {
        printf ("Problema riscontrato nella creazione dei thread\n");
        perror("Error:");
        cleanup(workers, wsFailed, socketsList);
        exit(EXIT_FAILURE);
    }


    if ((listen(serverSFD, queueLenght))==-1)
        errEx();

    struct pollfd connectionFDS [maxClients];
    connectionFDS[0].fd = serverSFD;
    connectionFDS[0].events = POLLIN;
    for (int i=1; i < maxClients; i++) { 
        connectionFDS[i].fd = -1;
        connectionFDS[i].events = POLLIN; 
    }
    int pollRes;
    int timeout = 60*1000;      //1 min


    while(TRUE) {

        if ((pollRes=poll(connectionFDS,maxClients,timeout))==-1) 
            errEx();
        if (pollRes==0) {
            printf("Sessione scaduta\n");
            //no clients connected atm
        }


        if (connectionFDS[0].revents == POLLIN) {   //if client richiede connect

            int j = 1;
            while(connectionFDS[j].fd!=-1 && j<maxClients) j++; //controllo se ho spazio per gestire più client

            if (j<=maxClients) { 
                connectionFDS[j].fd = accept (serverSFD, NULL, 0);  //se posso accetto connessione
                currSock->next=addNode(connectionFDS[j].fd);        // per eventuale cleanup
                currSock = currSock->next;
            }
            else printf ("Tentata connessione\n");      //se non posso stampo un avvertimento
            
        }

        for (int i=1;i<maxClients;i++){
            if (connectionFDS[i].revents==POLLIN) {
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

}

void cleanup(pthread_t workers[], int index, node * socket_list) {
    for (int i=0; i<index; i++) {       //join dei thread aperti
        pthread_join(workers[i], NULL);
    }

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
        if( (error_num = pthread_create(&workers[i], NULL, manageRequest, NULL))!=0){   //will be revised
            errno = error_num;
            return -1;
        }
    }
    return 0;
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
        pthread_mutex_lock(&mutex);
        currentRequest = requestsQueue->descriptor;
        deleteNode(requestsQueue);
        pthread_mutex_unlock(&mutex);

        ssize_t reqRes;
        void * buffer=NULL;
        if ((reqRes=read(currentRequest, buffer, MAX_BUF_SIZE))==-1)
            errEx();

        //formato richiesta: codice operazione, nomeFile
        char * request;
        request = strtok(buffer, ",");
        int code =atoi(request);
        char * reqArg;

        switch (code) {

            case RD:
                break;
            
            case WR:
                break;
            
            case OP:
                break;

            case RM:

                reqArg = strtok (NULL, ",");
                if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);

                else {
                    fileNode * fToRm;
                    if ((fToRm=searchFile(reqArg, storage))==NULL) sendAnswer(currentRequest, FAILURE);
                    else deleteFile (fToRm, &storage, &lastAddedFile, &fileCount);
                    sendAnswer(currentRequest, SUCCESS);
                }
                break;
            
            case PRC:
                break;
            
            case PUC:

                reqArg = strtok (NULL, ",");
                if (reqArg == NULL) sendAnswer (currentRequest, FAILURE);
                else {
                    if (storage == NULL) initStorage (NULL, reqArg, 0);
                    else {
                        if (searchFile(reqArg,storage) == NULL) {
                            addFile (NULL, reqArg, 0, &lastAddedFile, &fileCount);
                            sendAnswer (currentRequest, SUCCESS);
                        }
                        else sendAnswer (currentRequest, FAILURE);
                    }
                }
                break;

            case RDM:
                break;

            default:
                break;
        }

    }

}


int sendAnswer (int fd, int res) {
    size_t writeSize = sizeof(res);
    void * buff = malloc (writeSize);

    ssize_t nWritten;
    while ( writeSize > 0) {
        if ((nWritten = write(fd, buff, writeSize))==-1) {
            free (buff);
            return -1;
        }
            writeSize -= nWritten;
    }

    free (buff);
    return 0;
}