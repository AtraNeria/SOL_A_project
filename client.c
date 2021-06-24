#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include "client_api.h"

#define MAX_NAME_LEN 2048

#define DFLT_TIME 0

#define CONN_CLOSED 3
#define T_REPEAT 4
#define F_REPEAT 5
#define NO_ARG 6
#define INVALID_OPT 7

// Stampa a schermo le opzioni supportate
void printOpt();

/* Controlla se è stato passato msec con l'opione -t;
 ritorna il valore del tempo se trovato, 0 altrimenti */
void timeCheck (int argc, char ** argv);

/* Prende come argomento il codice warno,
 stampa a schermo il warning corrispondente */
void printWarning(int warno);

/* Chiama perror e chiude il processo */
void errEx ();

/* Dealloca eventuali buffer prima di chiudere */
void cleanup();

/* Naviga la cartella data fino a n file in modo ricorsivo, 
tenendo il conto dei file già visitati in j*/
void navigateDir(char * dirName, int n, int j);


int msec = 0;
int tOpt_met = 0;
int pOpt_met = 0;
char * sockName = NULL;
char * dirReadFiles=NULL;
char * expelledFiles=NULL;


int main (int argc, char ** argv){

    const char * optstring = "hf:w:W:D:r:R::d:t:l:u:c:p";   //Opzioni supportate
    int curr_opt;
    int fOpt_met = 0;
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
                    sockName=malloc(sizeof(char)*strlen(optarg));
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
                else printWarning(F_REPEAT);
                break;
            }


            case 'w': {
                if(connOpen) {

                    if(optarg != NULL) {
    
                        char * wArg = malloc( sizeof(char)*strlen(optarg));
                        strcpy(wArg,optarg);
                        char * dirName = strtok(wArg, ",");
                        char * numFiles = strtok(NULL, ",");
                        int n=0;
                        if (numFiles!=NULL) n=atoi(numFiles);
                        navigateDir(dirName,n,1);                        
                        optarg=NULL;
                        free(wArg);
                    }

                    else printWarning(NO_ARG);
                }

                else printWarning (CONN_CLOSED);
                break;
            }

            case 'W': {
                if (connOpen){
                    if (optarg!=NULL) {

                        char * args = malloc(sizeof(char)*strlen(optarg));
                        strcpy(args, optarg);
                        char * currArg = strtok(args, ",");
                        if (writeFile(currArg,expelledFiles)==-1) errEx();

                        while ((currArg=strtok(NULL,","))!=NULL){
                            if (writeFile(currArg,expelledFiles)==-1) errEx();
                        }
                        free(args);
                    }
                    else printWarning(NO_ARG);
                }
                else printWarning(CONN_CLOSED);
            }

            case 'r': {
                if (connOpen){

                    if (optarg!=NULL) {
                        char * rArgs = malloc(sizeof(char)*strlen(optarg));
                        strcpy(rArgs, optarg);
                        char * fToRead = strtok(rArgs,",");
                        void ** buffer=NULL;
                        size_t * bufferSize=NULL;
                        if (readFile(fToRead, buffer, bufferSize)==-1) errEx();

                        while ((fToRead = strtok(NULL, ","))!=NULL) {
                            if (readFile(fToRead, buffer, bufferSize)==-1) errEx();
                        }
                        
                        optarg=NULL;
                        free(rArgs);
                    }

                    else printWarning(NO_ARG);
                }
                else printWarning(CONN_CLOSED);
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


            case 'p': {
                if (!pOpt_met) pOpt_met=1;
                break;
            }

            case 'd': {
                if(optarg!=NULL){
                    if (dirReadFiles==NULL){
                        dirReadFiles=malloc(sizeof(char)*MAX_NAME_LEN);
                    }
                    strcpy(dirReadFiles,optarg);
                }
                else printWarning(NO_ARG);
            }

            case 'D': {
                if (optarg!=NULL) {
                    if (expelledFiles==NULL) {
                        expelledFiles=malloc(sizeof(char)*MAX_NAME_LEN);
                    }
                    strcpy(expelledFiles,optarg);
                }
                else printWarning(NO_ARG);
            }

            default: 
                printWarning(INVALID_OPT);
        }
    }

    cleanup();

    if (closeConnection(sockName)==-1) { 
        perror("Error:");
        exit(EXIT_FAILURE);
    }

    return 0;

}





void printOpt(){
    printf( "Opzioni accettate:\n-h\n-f filename:\n-w dirname[,n=0]\n-W file1[,file2]\n\
            -D dirname\n-r file1[,file2]\n-R [n=0]\n-d dirname\n-t time\n-l file1[,file2]\n\
            -u file1[,file2]\n-c file1[,file2]\n-p" );
}


void timeCheck (int argc, char ** argv){
    int t_opt;
    while ((t_opt = getopt(argc,argv,"t"))!=-1) {
        if (t_opt == 't'){
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

        case NO_ARG:
            printf("Argomenti non specificati per un'opzione che li richiede\n");

        case INVALID_OPT:
            printf("Opzione non supportata\n");

    }
}

void errEx () {

    perror("Error:");
    exit(EXIT_FAILURE);
}

void navigateDir(char * dirName, int n, int j) {
    DIR * dirToWrite;
    if ((dirToWrite = opendir(dirName))==NULL) errEx();
    struct dirent * currFile;
    struct stat dirStreamInfo;

    if (n==0) { 
        errno=0;
        if ((currFile=readdir(dirToWrite))==NULL)
            if (errno!=0) errEx();

        while (currFile!=NULL) {
           if (stat(currFile->d_name,&dirStreamInfo)==-1) errEx();
           char * fileName = malloc(sizeof(char)*2048);
           strcpy(fileName, dirName);
           strcat(fileName, "/");
           strcat(fileName,currFile->d_name);
           if(fileName[0]!='.'){
                if (S_ISDIR(dirStreamInfo.st_mode)) navigateDir(fileName, 0, j);
                else if((writeFile(fileName,expelledFiles)) == -1) errEx();
                currFile=readdir(dirToWrite);
           }
           free(fileName);
        }
    }

    else { 
        errno=0;
        if ((currFile=readdir(dirToWrite))==NULL)
            if (errno!=0) errEx();
        while (j<=n && currFile!=NULL) {
           if (stat(currFile->d_name,&dirStreamInfo)==-1) errEx();
           char * fileName = malloc(sizeof(char)*2048);
           strcpy(fileName, dirName);
           strcat(fileName, "/");
           strcat(fileName,currFile->d_name);
           if(fileName[0]!='.'){
                if (S_ISDIR(dirStreamInfo.st_mode)) navigateDir(fileName, n, j);
                else if((writeFile(fileName,expelledFiles)) == -1) errEx();
                currFile=readdir(dirToWrite);
                j++;
           }
            free(fileName);
        }
    }
}

void cleanup(){
    if (expelledFiles!= NULL) free(expelledFiles);
    if (dirReadFiles!= NULL) free(dirReadFiles);
    if (sockName!=NULL) free(sockName);
}
