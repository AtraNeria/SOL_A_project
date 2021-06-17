#define DFLT_TIME 0

void printOpt();
void nameSocket();

int main(int argc, char ** argv){

    const char * optstring = "hf:w:W:D:r:R::d:t:l:u:c:p";
    int curr_opt;
    int fopt_met=0;
    int popt_met=0;
    char * sockname;
    while ((curr_opt=getopt(argc, argv, optstring))!=-1) {
      switch (curr_opt)
          case 'h' {
                printOpt();
                return 1;
            }
            break;
            case 'f' {
                if(!fopt_met){
                    fopt_met=1;
                    nameSocket();
                }
            }
            case 'w' {
                char * dirname;
                strcpy(dirname,optarg);
                optind++;
                if (optind<argc && *argv[optind] != '-'){
                    int n=atoi(argv[optind]);
                    extern raedNFiles(dirname, n);
                }
                else extern raedNFiles(dirname, 0);
                
            }
            case 't' {
                extern int msec=atoi(argv[optind++]);
                if (fopt_met) extern openConnection(sockname, msec, abstime);
                else //error handling
            }
            break;
        //default: error handling
        //un case per ogni opzione possibile
    }
}

void printOpt(){
    printf("Opzioni accettate:\n-h\n-f filename:\n-w dirname[,n=0]\n-W file1[,file2]\n
            -D dirname\n-r file1[,file2]\n-R [n=0]\n-d dirname\n-t time\n-l file1[,file2]\n
            -u file1[,file2]\n-c file1[,file2]\n-p");
}

void nameSocket(){
    extern char * sockName;
    strcpy(sockName,optarg);
}