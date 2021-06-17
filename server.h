#ifndef _SERVER_H
#define _SERVER_H

    /*  Legge il file config.txt dove è specificata la
        configurazione di avvio del server.
    */
void readConfig();

    /*  Avvia il server e lo mette in ascolto per eventuali richieste
        da lato client 
    */
void startServer();

    /*  Gestisce la richiesta specificata in buffer effettuata dal 
        client
    */
void manageRequest(void * buffer);

#endif