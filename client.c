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
#define MAX_FILE_SIZE 262144

#define DFLT_TIME 0

#define CONN_CLOSED 3
#define T_REPEAT 4
#define F_REPEAT 5
#define NO_ARG 6
#define INVALID_OPT 7
#define NO_DIR 8

#define TEST printf("OK\n");

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
int navigateDir(char * dirName, int n, int j);


int msec = 0;
int tOpt_met = 0;
int pOpt_met = 0;
char * sockName = NULL;
char * dirReadFiles=NULL;
char * expelledFiles=NULL;


int main (int argc, char ** argv){

    const char * optstring = "-hf:w:W:D:r:R::d:t:l:u:c:p";   //Opzioni supportate
    int curr_opt;
    int fOpt_met = 0;
    int connOpen = 0;

    struct timespec absolute_time;
    absolute_time.tv_nsec = 1000000000;     //1 sec

    // Parsing delle opzioni passate a terminale
    while ((curr_opt=getopt(argc, argv, optstring))!=-1) {
        switch (curr_opt) {

          // Stampa opzioni e termina
            case 'h': {
                printOpt();
                return 0;
                break;
            }


            // Apre la connessione alla socket passata come argomento
            case 'f': {

                if (!fOpt_met){
                    if (optarg!=NULL){
                        fOpt_met=1;
                        sockName=calloc(strlen(optarg)+1,sizeof(char));
                        strcpy(sockName,optarg);

                        // Controllo se è stato specificato msec o uso default
                        if (!tOpt_met) {
                            int tmp = optind;
                            timeCheck(argc,argv);
                            optind=tmp;
                        }
                        if (!connOpen) {
                            if (openConnection(sockName, msec, absolute_time)==-1){
                                perror("Error:");
                                exit(EXIT_FAILURE);
                            }
                            connOpen = 1;
                        }
                    }

                    else printWarning (NO_ARG); 
                }
                else printWarning(F_REPEAT);
                break;
            }


            // Scrittura su server di n file nella cartella specificata
            case 'w': {
                if(connOpen) {

                    if(optarg != NULL) {
    
                        char * wArg = calloc(strlen(optarg)+1,sizeof(char));
                        strcpy(wArg,optarg); 
                        char * dirName = strtok(wArg, ",");
                        char * numFiles = strtok(NULL, ",");
                        int n=0;
                        if (numFiles!=NULL) n=atoi(numFiles);
                        if (navigateDir(dirName,n,1)==-1) break;                        
                        free(wArg);
                    }

                    else printWarning(NO_ARG);
                }

                else printWarning (CONN_CLOSED);
                break;
            }


            // Scrittura lista di file
            case 'W': {
                if (connOpen){
                    if (optarg!=NULL) {
                        char * args = calloc(strlen(optarg)+1,sizeof(char));
                        char * saveptr;
                        strcpy(args, optarg);
                        char * currArg = strtok_r(args, ",", &saveptr);

                        if (writeFile(currArg,expelledFiles)==-1) {
                            free(args);
                            break;
                        }
                        while ((currArg = strtok_r(NULL,", ", &saveptr))!=NULL){
                            if (writeFile(currArg,expelledFiles)==-1) {
                                free(args);
                                break;
                            }
                        }
                        free(args);
                    }
                    else printWarning(NO_ARG);
                }
                else printWarning(CONN_CLOSED);
                break;
            }


            // Lettura lista di file
            case 'r': {
                if (connOpen){

                    if (optarg!=NULL) {
                        char * rArgs = calloc(strlen(optarg)+1,sizeof(char));
                        strcpy(rArgs, optarg);
                        char * ptr;
                        char * fToRead = strtok_r(rArgs,",",&ptr);
                        void * buffer=NULL;
                        size_t bufferSize=0;

                        if (readFile(fToRead, &buffer, &bufferSize)==-1) {
                            free(rArgs);
                            break;
                        }
                        free(buffer);
                        buffer = NULL;
                        bufferSize = 0;

                        while ((fToRead = strtok_r(NULL, ",",&ptr))!=NULL) {
                            void * buff=NULL;
                            size_t s=0;
                            if (readFile(fToRead, &buff, &s)==-1) {
                                free (rArgs);
                                break;
                            }
                            free(buff);
                        }
                        free(rArgs);
                    }

                    else printWarning(NO_ARG);
                }
                else printWarning(CONN_CLOSED);
                break;
            }


            // Lettura dei primi n file su server; se n=0 o non specificato leggo tutti i file disponibili
            case 'R' : {
                if (connOpen) {
                    if (dirReadFiles==NULL) printWarning (NO_DIR);
                    else if (optarg != NULL) {
                        int n = atoi(optarg);
                        if (readNFiles(n, dirReadFiles)==-1) break;
                    }
                    else if (readNFiles(0, dirReadFiles)==-1) break;
                }
                else printWarning(CONN_CLOSED);
                break;
            }


            // Specifico msec
            case 't': {
                if(!tOpt_met){ 
                    if (optarg!=NULL){
                        msec=atoi(optarg);
                        tOpt_met=1;
                    }
                    else printWarning(NO_ARG);
                }
                else printWarning (T_REPEAT);            
                break;
            }


            // Abilito stampe
            case 'p': {
                if (!pOpt_met) pOpt_met=1;
                break;
            }


            // Specifico cartella per salvare file letti
            case 'd': {
                if(optarg!=NULL){
                    if (dirReadFiles==NULL){
                        dirReadFiles = calloc(MAX_NAME_LEN,sizeof(char));
                    }
                    strcpy(dirReadFiles,optarg);
                }
                else printWarning(NO_ARG);
                break;
            }


            // Specifico cartella per file espulsi
            case 'D': {
                if (optarg!=NULL) {
                    if (expelledFiles==NULL) {
                        expelledFiles = calloc(MAX_NAME_LEN, sizeof(char));
                    }
                    strcpy(expelledFiles,optarg);
                }
                else printWarning(NO_ARG);
                break;
            }


            // Rimozione file
            case 'c': {
                if (connOpen){
                    if (optarg!=NULL) {
                        char * args = calloc(strlen(optarg)+1,sizeof(char));
                        char * saveptr;
                        strcpy(args, optarg);

                        char * currArg = strtok_r(args, ",", &saveptr);
                        if (removeFile(currArg)==-1) {
                            free (args);
                            break;
                        }
                        while ((currArg = strtok_r(NULL,", ", &saveptr))!=NULL) {
                            if (removeFile(currArg)==-1) {
                                free(args);
                                break;
                            }
                        }
                        free(args);
                    }
                    else printWarning(NO_ARG);
                }
                else printWarning(CONN_CLOSED);
                break;
            }


            // Lock file
            case 'l': {
                if (connOpen) {
                    if (optarg!=NULL) {
                        char * args = calloc(strlen(optarg)+1,sizeof(char));
                        char * saveptr;
                        strcpy(args, optarg);
                        char * currArg = strtok_r(args, ",", &saveptr);
                        while (currArg!=NULL) {
                            if (lockFile(currArg)==-1) break;
                            currArg = strtok_r(NULL, ",", &saveptr);
                        }
                    }
                    else printWarning(NO_ARG);
                }
                else printWarning(CONN_CLOSED);
                break;
            }

            // Unlock file
            case 'u': {
                if (connOpen) {
                    if (optarg!=NULL) {
                        char * args = calloc(strlen(optarg)+1,sizeof(char));
                        char * saveptr;
                        strcpy(args, optarg);
                        char * currArg = strtok_r(args, ",", &saveptr);
                        while (currArg!=NULL) {
                            if (unlockFile(currArg)==-1) break;
                            currArg = strtok_r(NULL, ",", &saveptr);
                        }
                    }
                    else printWarning(NO_ARG);
                }
                else printWarning(CONN_CLOSED);
                break;
            }


            default: 
                printWarning(INVALID_OPT);
                break;
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

    while ((t_opt = getopt(argc,argv,"-:t"))!=-1) {
        if (t_opt == 't'){
            msec = atoi(optarg);
            t_opt=1;
        }
    }
}

void printWarning(int warno) {
    switch (warno) {

        case CONN_CLOSED:
            printf ("Richiesta non eseguibile per connessione al server non ancora aperta\n");
            break;

        case T_REPEAT:
            printf("Verrà considerato valido il valore specificato nella prima occorrenza di -t\n"); 
            break;

        case NO_ARG:
            printf("Argomenti non specificati per un'opzione che li richiede\n");
            break;

        case INVALID_OPT:
            printf("Opzione non supportata\n");
            break;
        
        case NO_DIR:
            printf("Directory non specificata con opzione -d: i file letti non verrano salvati su disco");
            break;

    }
}

void errEx () {

    perror("Error");
    exit(EXIT_FAILURE);
}

int navigateDir(char * dirName, int n, int j) {
    // Apro la cartella ./dirName
    DIR * dirToWrite;
    if ((dirToWrite = opendir(dirName))==NULL) return -1;

    // Numero di file da scrivere non specificato
    if (n==0) { 
        struct dirent * currFile = calloc (sizeof(struct dirent),1);
        struct dirent * ptr = currFile;
        errno = 0;
        if ((currFile=readdir(dirToWrite))==NULL && errno!=0) {
            free(ptr);
            return -1;
        }

        while (currFile!=NULL) {
            // Copio nome
            struct stat * dirStreamInfo = calloc(sizeof(struct stat),1);
            char currentName [strlen(currFile->d_name)+1];
            strcpy(currentName,currFile->d_name);
            
            // Path intero: ./dirName/current_file_name
            char * fileName = calloc(2048, sizeof(char));
            strcpy(fileName, dirName);
            strcat(fileName, "/");
            strcat(fileName,currentName);
            if (stat(fileName,dirStreamInfo)==-1) {
                free(ptr);
                free(dirStreamInfo);
                free(fileName);
                return -1;
            }

            // if is not a dot file  
            if(strcmp(currentName,".")!=0 && strcmp(currentName,"..")!=0){
                // Se è una sottodirectory la esploro ricorsivamente
                if (S_ISDIR(dirStreamInfo->st_mode)) {
                    char * subDir = calloc (strlen(dirName)+strlen(currentName)+strlen("/")+1,sizeof(char));
                    sprintf(subDir,"%s/%s",dirName,currentName);
                    if (navigateDir(subDir, 0, j)==-1) {
                        free(ptr);
                        free(dirStreamInfo);
                        free(fileName);
                        free(subDir);
                        return -1;
                    }
                    free (subDir);
                }
                // Se non è una cartella scrivo il file
                else if((writeFile(fileName,expelledFiles)) == -1) {
                    free(ptr);
                    free(dirStreamInfo);
                    free(fileName);
                    return -1;
                }
            }
            memset (ptr,0,sizeof(struct dirent));
            currFile = readdir(dirToWrite);
            free (dirStreamInfo);
            free(fileName);
        }
        free (ptr);
    }

    // Numero file specifico
    else { 
        struct dirent * currFile = calloc (sizeof(struct dirent),1);
        struct dirent * ptr = currFile;
        errno = 0;
        if ((currFile=readdir(dirToWrite))==NULL && errno!=0) {
            free(ptr);
            return -1;
        }

        while (j<=n && currFile!=NULL) {
            // Copio nome
            struct stat * dirStreamInfo = calloc(sizeof(struct stat),1);
            char currentName [strlen(currFile->d_name)+1];
            strcpy(currentName,currFile->d_name);

            // Path intero
            char * fileName = calloc(2048, sizeof(char));
            strcpy(fileName, dirName);
            strcat(fileName, "/");
            strcat(fileName,currentName);
            printf("%s\n",fileName);
            if (stat(fileName,dirStreamInfo)==-1) {
                free(ptr);
                free(dirStreamInfo);
                free(fileName);
                return -1;
            }

            // Se non è un dotFile
            if(strcmp(currentName,".")!=0 && strcmp(currentName,"..")!=0){
                // Se è una sottodirectory
                if (S_ISDIR(dirStreamInfo->st_mode)) {
                    char * subDir = calloc (strlen(dirName)+strlen(currFile->d_name)+strlen("/")+1,sizeof(char));
                    sprintf(subDir,"%s/%s",dirName,currFile->d_name);
                    if (navigateDir(subDir, 0, j)==-1) {
                        free(ptr);
                        free(dirStreamInfo);
                        free(fileName);
                        free(subDir);
                        return -1;
                    }
                    free (subDir);
                }
                // Se è un file
                else {
                    if((writeFile(fileName,expelledFiles)) == -1) {
                        free(ptr);
                        free(dirStreamInfo);
                        free(fileName);
                        return -1;
                    }
                    else j++;
                }
            }
            memset (ptr,0,sizeof(struct dirent));
            currFile=readdir(dirToWrite);
            free (dirStreamInfo);
            free(fileName);
        }
        free (ptr);
    }
    closedir(dirToWrite);
    return 0;
}

void cleanup(){
    if (expelledFiles!= NULL) free(expelledFiles);
    if (dirReadFiles!= NULL) free(dirReadFiles);
    if (sockName!=NULL) free(sockName);
}
