#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include "server.h"

#define LEN 2048
#define CONF_PAR 10
#define MAX_CONN 30

typedef struct node {
    int descriptor;
    node * next;
} node;


void errEx ();
void init (int index, char * string);
void cleanup(pthread_t * workers[], int index, node * socket_list);
node * addNode(int desc);
node * deleteNode(node * List);

int threadQuantity;
int storageDim;
int capacity;
int queueLenght;
int maxClients;
node * requestsQueue=NULL;
node * lastRequest;
pthread_mutex_t mutex;


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
    char confStr [LEN];
    char * currTok;
    int i;

    for (i=0;i<CONF_PAR;i++){
        fgets(confStr, LEN, conf);
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


    int stop=0;
    pthread_t workers [threadQuantity];

    int wsFailed;
    if ((wsFailed=createWorkers(&workers)) !=-1) {
        printf ("Problema riscontrato nella creazione dei thread\n");
        perror("Error:");
        cleanup(&workers, wsFailed, socketsList);
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


    while(!stop) {

        if ((pollRes=poll(connectionFDS,maxClients,timeout))==-1) 
            errEx();
        if (pollRes==0) {
            printf("Timed out without connections\n");
            //no clients connected, close server
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
            if (connectionFDS[i].revents==POLLIN) 
                if (requestsQueue == NULL) { 
                    requestsQueue == addNode(connectionFDS[i].fd);
                    lastRequest = requestsQueue;
                }
                else { lastRequest->next = addNode(connectionFDS[i].fd);
                    lastRequest=lastRequest->next;
                }
        }
    }

}

void cleanup(pthread_t * workers[], int index, node * socket_list) {
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
}

int createWorkers (pthread_t * workers[]) {

    int error_num;
    for (int i=0; i<threadQuantity; i++) {
        if( (error_num = pthread_create(&workers[i], NULL, manageRequest, NULL))!=0){   //will be revised
            errno = error_num;
            return -1;
        }
    }
    return 0;
}

node * addNode (int desc) {
    node * List = malloc(sizeof(node));
    List->descriptor = desc;
    List->next =NULL;
    return List;
}

node * deleteNode(node * List){
    node * toDelete =  List;
    List=List->next;
    free(toDelete);
    return List;
}


void manageRequest() {

    int currentRequest;     //richiesta che il thread sta servendo
    while() {               //finchè accetto nuove richieste
        pthread_mutex_lock(&mutex);
        currentRequest = requestsQueue->descriptor;
        deleteNode(requestsQueue);
        pthread_mutex_unlock(&mutex);

        ssize_t reqRes;
        void * buffer;
        if ((reqRes=read(currentRequest, buffer, 2048))==-1)
            errEx();
    }

}