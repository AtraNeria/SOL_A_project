#include "commProtocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int bufferCheck(void * buffer){
    char * b = malloc(sizeof(char)*MAX_FILE_SIZE);
    //memset (b, 0, sizeof(char)*MAX_FILE_SIZE);
    strcpy (b, (char *) buffer);

    if (strstr(b, EOBUFF)==NULL) {
        free(b);
        return 0;}
    else {
        free (b);
        return 1;
    }
}

int nameFromPath (char * fullpath, char * name) {
    char * dirless;
    char * ptr=NULL;
    char * tmp;
    //Copio fullpath per strtok_r
    char toTok [strlen(fullpath)+1];
    memset(toTok,0,strlen(fullpath)+1);
    strcpy(toTok,fullpath);
    //Ricavo il nome del file dal path completo
    dirless = strtok_r (toTok,"/", &ptr);
    while ((tmp=strtok_r(NULL,"/", &ptr))!=NULL) {
        dirless=tmp;
    }
    strcpy (name, dirless);
    return 0;
}

int sendAnswer (int fd, ssize_t res) {
    char ansStr [MAX_BUF_SIZE];
    sprintf (ansStr,"%li£",res);

    size_t writeSize = sizeof(char) * strlen (ansStr) + 1;
    void * buffer = malloc(writeSize);
    void * toFree = buffer;
    memset (buffer, 0, writeSize);
    strcpy (buffer, ansStr);

    size_t nToWrite = writeSize;
    ssize_t nWritten = 1;
    size_t totBytesWritten = 0;

    while (nToWrite>0 && nWritten!=0) {
        if ((nWritten = write(fd, buffer, nToWrite))==-1) {
            free(toFree);
            return -1;
        }
        buffer += nWritten;
        nToWrite -= nWritten;
        totBytesWritten += nWritten;
    }

    free (toFree);
    return 0;
}
