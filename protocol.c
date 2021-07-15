#include "commProtocol.h"
#include <string.h>
#include <stdlib.h>

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
