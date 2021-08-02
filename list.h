#ifndef _LIST_H
#define _LIST_H

#include <stdio.h>

// Nodo per liste con elementi int
typedef struct node {
    int descriptor;
    struct node * next;
} node;

// Nodo per liste doppiamente connesse con elementi che contengono un puntatore ad un file e le sue informazioni
typedef struct fileNode {
    int owner;
    long fileSize;
    char * fileName;
    FILE * fPointer;
    struct fileNode * next;
    struct fileNode * prev;
} fileNode;


/* Nodo contenente una stringa, un puntatore al prossimo elemento della lista
    e un int significante lo stato; sarà settato ad un 1 se il file di nome
    str è correntemente aperto, 0 se è stato chiuso
*/
typedef struct strNode {
    char str [2048];
    int state;
    struct strNode * next;
} strNode;

/* Crea un nodo di tipo node con elemento desc.
    Ritorna un puntatore al nodo creato
*/
node * addNode (int desc);

/* Esegue pop del primo nodo della lista List.
    Restituisce un puntatore alla nuova testa della lista
*/
node * popNode(node * List);

/* Elimina dalla lista il nodo contenente fd.
*/
void deleteNode (int fd, node ** list);

/* Aggiunge un nodo puntante a file f di nome fname in coda ad una lista di elementi fileNode
*/
void addFile (FILE * f, long size, char * fname, int fOwner, fileNode **lastAddedFile);

/* Crea un nodo che punta al file f di grandezza file e nome fname, posseduto da fOwner;
    posizionato in coda alla lisa lastAddedFile.
    Restituisce un puntatore al nodo.
*/
fileNode * newFile (FILE * f, long size, char * fname, int fOwner, fileNode **lastAddedFile);

/* Cerca il nodo con nome fname nella lista storage, salvandone il puntatore in ptr se trovato.
    Restituisce 0 in caso di successo, -1 altrimenti
*/
int searchFile (char * fname, fileNode * storage, fileNode ** ptr);

/* Controlla se il file fname nella lista storage è bloccato per utilizzo esclusivo da un processo.
    Restituisce 0 se non è locked, l'fd del processo proprietario altrimenti.
    Restituisce -1 se il file non è presente in lista.
*/
int checkLock (char * fname, fileNode * storage);

/* Elimina dalla lista list il nodo file in testa.
    Restituisce la nuova testa della lista
*/
fileNode * popFile (fileNode * list);

/* Prende una lista di fileNode storage, la sua coda lastAddedFile, il suo numero di elementi fileCount
    e elimina da essa il nodo f. 
*/
void deleteFile (fileNode * f, fileNode ** storage, fileNode **lastAddedFile);

/* Aggiunge in testa alla lista list un elemento contenente la stringa string, se non esiste già
    un elemento tale.
    Restituisce un puntatore al nuovo nodo creato in caso di successo, NULL altrimenti e setta errno
*/
strNode * addString (const char * string, strNode * list);

/* Elimina il nodo contenente string dalla lista puntata da list.
    Restituisce 0 in caso di successo, -1 in caso di fallimento e setta errno
*/
int deleteString (const char * string, strNode ** list);

/* Cerca il nodo nella lista list che abbia come campo str string.
    Restituisce -1 in caso di fallimento e state del nodo in caso di successo
*/
int searchString (const char * string, strNode * list);

/* Se è esistente e aperto setta lo state dell'elemento con str string a.
    Restituisce 0 in caso di successo, -1 altrimenti e setta errno
*/
int closeString (const char * string, strNode ** list);


#endif