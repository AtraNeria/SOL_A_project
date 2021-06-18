#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "client_api.h"

#define DFLT_TIME 0

#define CONN_CLOSED 3
#define T_REPEAT 4

void printOpt();        // Processo per stampare a schermo le opzioni supportate
void timeCheck (int argc, char ** argv);    // Funzione per controllare se è stato passato msec con l'opione -t;
                                            //  ritorna il valore del tempo se trovato, 0 altrimenti
void printWarning(int warno);


int msec = 0;
int tOpt_met = 0;
char * sockname;


int main (int argc, char ** argv){

    const char * optstring = "hf:w:W:D:r:R::d:t:l:u:c:p";
    int curr_opt;
    int fOpt_met = 0;
    int pOpt_met = 0;
    int connOpen = 0;

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
                if (!fOpt_met){
                    fOpt_met=1;
                    char * sockName;
                    strcpy(sockName,optarg);
                    if (!tOpt_met) {
                        timeCheck(argc,argv);
                    }
                    if (!connOpen) {
                        if (openConnection(sockName, msec, absolute_time)==-1){
                            perror("Error:");
                            exit(EXIT_FAILURE);
                        }
                        connOpen = 1;
                    }
                }
                else printf("Opzione -f consetita una sola volta\n");
                break;
            }


            case 'w': {
                if(connOpen) {
                    char * dirname;
                    strcpy(dirname,optarg);
                    optind++;
                    if (optind<argc && *argv[optind] != '-'){
                        int n=atoi(argv[optind]);
                        readNFiles(dirname, n);
                    }
                    else readNFiles(dirname, 0);
                }

                else printWarning (CONN_CLOSED);
                break;
            }
            


            case 't': {
                if(!tOpt_met){ 
                    msec=atoi(argv[optind++]);
                    tOpt_met=1;
                }
                else printWarning (T_REPEAT);            
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


void timeCheck (int argc, char ** argv){
    int t_opt;
    while ((t_opt = getopt(argc,argv,"t"))!=-1) {
        if (t_opt == "t"){
            msec = atoi(argv[optind++]);
            t_opt=1;
        }
    }
}

void printWarning(int warno) {
    switch (warno) {
        case CONN_CLOSED:
            printf ("Richiesta non eseguibile per connessione al server non ancora aperta\n");
        case T_REPEAT:
            printf("Verrà considerato valido il valore specificato \
                                nella prima occorrenza di -t\n"); 

    }
}