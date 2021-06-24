#ifndef _COMMUNICATION_PROTOCOL_H
#define _COMMUNICATION_PROTOCOL_H

//Codici richieste
#define READ "16,"
#define WRITE "32,"
#define OPEN "64,"
#define RMV "128,"
#define PRIV_CREATE "256,"
#define PUB_CREATE "512,"

#define MAX_BUF_SIZE 4096
#define MAX_NAME_LEN 2048
#define MAX_FILE_SIZE 215.144 // 1/4 Mb

#define SUCCESS 0
#define FAILURE -1


#endif