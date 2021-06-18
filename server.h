#ifndef _SERVER_H
#define _SERVER_H

#include <pthread.h>


    /*  Legge il file config.txt dove è specificata la
        configurazione di avvio del server.
    */
void readConfig();

    /*  Avvia il server e lo mette in ascolto per eventuali richieste
        da lato client 
    */
void startServer();

    /*  Crea un numero di thread workers come specificato in config.txt
        Ritorna -1 se ha successo, il numero di indice di workers in cui ha fallito altrimenti
    */
int createWorkers (pthread_t * workers[]);

    /*  Gestisce la richiesta specificata in buffer effettuata dal 
        client
    */
void manageRequest(void * buffer);

#endif