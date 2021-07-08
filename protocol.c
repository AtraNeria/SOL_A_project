#include "commProtocol.h"
#include <string.h>

int bufferCheck(void * buffer){
    char b [MAX_BUF_SIZE];
    strcpy(b,buffer);

    if (strstr(b,EOBUFF)==NULL) return 0;
    else return 1;
}
