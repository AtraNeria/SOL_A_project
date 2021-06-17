#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "client_api.h"

#define DFLT_TIME 0

void printOpt();
void nameSocket();

int msec;

int main(int argc, char ** argv){

    const char * optstring = "hf:w:W:D:r:R::d:t:l:u:c:p";
    int curr_opt;
    int fopt_met=0;
    int popt_met=0;
    char * sockname;

    struct timespec absolute_time;
    absolute_time.tv_nsec = 1000000000;     //1 sec

    while ((curr_opt=getopt(argc, argv, optstring))!=-1) {
      switch (curr_opt) {

          case 'h': {
                printOpt();
                return 1;
                break;
            }


            case 'f': {
                if(!fopt_met){
                    fopt_met=1;
                    nameSocket();
                }
                break;
            }


            case 'w': {
                char * dirname;
                strcpy(dirname,optarg);
                optind++;
                if (optind<argc && *argv[optind] != '-'){
                    int n=atoi(argv[optind]);
                    readNFiles(dirname, n);
                }
                else readNFiles(dirname, 0);
                break;
            }
            


            case 't': {
                msec=atoi(argv[optind++]);
                if (fopt_met) openConnection(sockname, msec, absolute_time);
                else //error handling            
                break;
            }
        //default: error handling
        //un case per ogni opzione possibile
        }
    }
}

void printOpt(){
    printf( "Opzioni accettate:\n-h\n-f filename:\n-w dirname[,n=0]\n-W file1[,file2]\n\
            -D dirname\n-r file1[,file2]\n-R [n=0]\n-d dirname\n-t time\n-l file1[,file2]\n\
            -u file1[,file2]\n-c file1[,file2]\n-p" );
}

void nameSocket(){
    char * sockName;
    strcpy(sockName,optarg);
}