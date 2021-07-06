#include "commProtocol.h"
#include <string.h>

int bufferCheck(void * buffer, long int size){
    char b [MAX_BUF_SIZE];
    strcpy(b,buffer);

    char * terminator;
    int ter_ind= size-strlen(EOBUFF);
    terminator = &b[ter_ind];

    if (strcmp(terminator,EOBUFF)==0) return 1;
    else return 0;
}
