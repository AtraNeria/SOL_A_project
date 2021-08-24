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

int nameFromPath (char * fullpath, char ** name) {
    char * dirless;
    char * ptr;
    char * tmp;
    dirless = strtok_r (fullpath,"/", &ptr);
    while ((tmp=strtok_r(NULL,"/", &ptr))!=NULL) {
        dirless = tmp;
    }
    if (dirless == NULL) return -1;
    else {
        strcpy (*name, dirless);
        return 0;
    } 

}

int sendAnswer (int fd, ssize_t res) {
    char ansStr [MAX_BUF_SIZE];
    sprintf (ansStr,"%li£",res);

    size_t writeSize = sizeof(char) * strlen (ansStr);
    void * buffer = malloc(writeSize);
    memset (buffer, 0, writeSize);
    strcpy (buffer, ansStr);

    size_t nToWrite = writeSize;
    ssize_t nWritten = 1;
    size_t totBytesWritten = 0;

    while (nToWrite>0 && nWritten!=0) {
        if ((nWritten = write(fd, buffer+totBytesWritten, nToWrite))==-1) {
            free(buffer);
            return -1;
        }
        nToWrite -= nWritten;
        totBytesWritten += nWritten;
    }

    free (buffer);
    return 0;
}
