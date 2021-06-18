#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include "server.h"

#define LEN 2048
#define CONF_PAR 10

typedef struct node {
    int descriptor;
    node * next;
} node;


void errEx ();
void init (int index, char * string);
void cleanup(pthread_t * workers[], int index, node * socket_list);
node * addNode(int desc);

int threadQuantity;
int storageDim;
int capacity;
int queueLenght;


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


    int newSocket;
    int stop=0;
    pthread_t workers [threadQuantity];

    int wsFailed;
    if ((wsFailed=createWorkers(&workers)) !=-1) {
        printf ("Problema riscontrato nella creazione dei thread\n");
        perror("Error:");
        cleanup(&workers, wsFailed, socketsList);
        exit(EXIT_FAILURE);
    }

    while(!stop) {
        if((listen(serverSFD, queueLenght))==-1)
            errEx();
        if((newSocket=accept(serverSFD, (struct sockaddr *)&address, (socklen_t*)&address))==-1)
            errEx();
        currSock->next=addNode(newSocket);
        currSock = currSock->next;
        /*extern void * buffer;
        read(newSocket, buffer, LEN);   //nel buffer salvo la richiesta fatta dal client
        manageRequest(buffer);*/
    }

}

void cleanup(pthread_t * workers[], int index, node * socket_list) {
    for (int i=0; i<index; i++) {
        pthread_join(workers[i], NULL);
    }
    node * toFree;
    while (socket_list != NULL) {
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
    node * sockList = malloc(sizeof(node));
    sockList->descriptor = desc;
    sockList->next =NULL;
    return sockList;
}


/*void manageRequest(void * buffer) {
    
    if (strcmp(buffer,"print")) {
        printf ("OK\n" );
    }
    

} */