#ifndef _COMMUNICATION_PROTOCOL_H
#define _COMMUNICATION_PROTOCOL_H

// Codici richieste
#define RD 16
#define WR 32
#define OP 64
#define RM 128
#define CLS 256
#define PUC 512
#define RDM 1024

// Codici richieste come stringhe
#define READ "16,"          // Lettura
#define WRITE "32,"         // Scrittura
#define OPEN "64,"          // Apertura
#define RMV "128,"          // Rimozione
#define CLOSE_S "256,"      // Chiusura di una socket client
#define PUB_CREATE "512,"   // Creazione unlocked
#define RD_MUL "1024,"      // Lettura di molteplici file

#define TEST printf("OK\n");
#define EOBUFF "£"
#define EOB_SIZE sizeof(char)*strlen(EOBUFF)

// Grandezze supportate
#define MAX_BUF_SIZE 4096
#define MAX_NAME_LEN 2048
#define MAX_FILE_SIZE 262144 // 262'144 bytes ==  1/4 MB

// Codici esiti operazioni
#define SUCCESS 0
#define FAILURE -1


/* Controlla se i byte size di buffer sono terminati dai caratteri terminatori EOBUFF.
    Restituisce 1 se lo sono, 0 altrimenti
*/
int bufferCheck(void * buffer);

/* Estrae il nome di un file dal suo percordo in fullpath, salvandolo in name.
    Restituisce 0 in caso di successo, -1 altrimenti
*/
int nameFromPath (char * fullpath, char ** name);

#endif