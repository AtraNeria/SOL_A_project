#ifndef _COMMUNICATION_PROTOCOL_H
#define _COMMUNICATION_PROTOCOL_H

// Codici richieste
#define RD 16
#define WR 32
#define OP 64
#define RM 128
#define PRC 256
#define PUC 512
#define RDM 1024

// Codici richieste come stringhe
#define READ "16,"          // Lettura
#define WRITE "32,"         // Scrittura
#define OPEN "64,"          // Apertura
#define RMV "128,"          // Rimozione
#define PRIV_CREATE "256,"  // Creazione locked
#define PUB_CREATE "512,"   // Creazione unlocked
#define RD_MUL "1024,"      // Lettura di molteplici file



// Grandezze supportate
#define MAX_BUF_SIZE 4096
#define MAX_NAME_LEN 2048
#define MAX_FILE_SIZE 215.144 // 1/4 Mb

// Codici esiti operazioni
#define SUCCESS 0
#define FAILURE -1


#endif