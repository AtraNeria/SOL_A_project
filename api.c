#include "client_api.h"
#include <time.h>

#define MAX_LEN 108

int clientSFD;

int openConnection(const char* sockname, int msec, const struct timespec abstime) {     //see timespec_get (da settare nel client prima della chiamata)
    long int wait_t= msec * 0.000001;       //tempo di attesa in nanosecondi
    long int total_t=0;                     //tempo totale passato da confrontare con il tempo limite

    int result;                             //risultato della connessione

    struct timespec start;
    struct timespec end;

    clientSFD = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;

    int name_len = strlen(sockname);                        //controllo che il nome della seocket non sfori la lunghezza massima
    if (name_len > MAX_LEN) {
        fprintf(stderr, "Nome socket troppo lungo\n" );
        exit(EXIT_FAILURE);
    } 
    strcpy(sa.sun_path, sockname);    

    while ( (result=connect(clientSFD (struct sockaddr*)&sa, sizeof(sa)))!=0 && total_t < abstime.tv_nsec ){
        timespec_get(start, TIME_UTC);
        if (errno != NULL || errno != ENOENT) {
                perror("Error:");
                exit(EXIT_FAILURE);
        }
        sleep(msec * 0.001);
        errno=0;
        timespec_get(end, TIME_UTC);
        total_t = (start.tv_nsec - end.tv_nsec);
    }

}


int closeConnection(const char* sockname) {
    if ( (int res = close(clientSFD)) == -1 ) {
        perror("Error:");
        exit(EXIT_FAILURE);      
    }
}

