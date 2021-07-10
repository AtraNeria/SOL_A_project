#include "commProtocol.h"
#include <string.h>

int bufferCheck(void * buffer){
    char b [MAX_BUF_SIZE];
    strcpy(b,buffer);

    if (strstr(b,EOBUFF)==NULL) return 0;
    else return 1;
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
