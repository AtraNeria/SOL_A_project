#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include "server.h"

#define LEN 2048
#define CONF_PAR 10

void errEx();
void init(int index, char * string);
void readConfig();
void startServer();
void manageRequest(void * buffer);

void errEx(){
    perror("Error:");
    exit(EXIT_FAILURE);
}

void init(int index, char * string){
    switch (index)
    {
    case 0:                                     //prima riga di config.txt
        extern int ThreadQuantity=atoi(string);
        break;
    
    case 1:                                    //seconda riga di config.txt ....
        extern int StorageDim=atoi(string);
        break;
    
    case 2:
        extern int capacity=atoi(string);
        break;

    case 3:
        extern int queueLenght=atoi(string);

    //case 4:
    //    extern const struct timespec abstime;
    //    abstime.tv_sec=(time_t)atol(string);      ?????????

    //case 5:
    //    abstime.tv_nsec=atol(string);

    default:
        break;
    }
}

void readConfig(){
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

void startServer(){

    int serverSFD;
    if(( serverSFD = socket(AF_UNIX, SOCK_STREAM,0))==-1)
        errEx();


    struct sockaddr_in address;
    if( bind(serverSFD, (struct sockaddr *)&address, sizeof(address))==-1)
        errEx();


    //int queueLenght;    //specificata in config.txt
    int newSocket;
    

    while(true){
        if((listen(serverSFD, queueLenght))==-1)
            errEx();
        if((newSocket=accept(serverSFD, (struct sockaddr *)&address, (socklen_t*)&address))==-1)
            errEx();

        extern void * buffer;
        read(newSocket, buffer, LEN);   //nel buffer salvo la richiesta fatta dal client
        manageRequest(buffer);
    }

}

/*void manageRequest(void * buffer) {
    
    if (strcmp(buffer,"print")) {
        printf ("OK\n" );
    }
    

} */