#include "commProtocol.h"
#include <string.h>
#include <stdlib.h>
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

int sendAnswer (int fd, int res) {
    char ansStr [MAX_BUF_SIZE];
    if (res == SUCCESS) strcpy(ansStr,"0\0");
    else strcpy(ansStr,"-1\0");

    size_t writeSize = sizeof(char) * strlen (ansStr);
    void * buffer = malloc(MAX_BUF_SIZE);
    memset (buffer, 0, MAX_BUF_SIZE);
    strcpy (buffer, ansStr);

    ssize_t nWritten = write(fd, buffer, writeSize);
    write(fd, EOBUFF, EOB_SIZE);

    free (buffer);
    if (nWritten==-1) return -1;
    return 0;
}
