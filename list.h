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
    char * str;
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
node * deleteNode(node * List);

/* Aggiunge un nodo puntante a file f di nome fname in coda ad una lista di elementi fileNode con fileCount elementi
*/
void addFile (FILE * f, char * fname, int fOwner, fileNode **lastAddedFile, int * fileCount);

/* Cerca il nodo con nome fname nella lista storage
    restituisce il puntatore al file cercato se presente, NULL altimenti
*/
fileNode * searchFile (FILE * f, char * fname, fileNode * storage);

/* Prende una lista di fileNode storage, la sua coda lastAddedFile, il suo numero di elementi fileCount
    e elimina da essa il nodo f. 
*/
void deleteFile (fileNode * f, fileNode ** storage, fileNode **lastAddedFile, int * fileCount);

/* Aggiunge in testa alla lista list un elemento contenente la stringa string. 
    Restituisce un puntatore al nuovo nodo creato in caso di successo, NULL altrimenti e setta errno
*/
strNode * addString (char * string, strNode * list);

/* Elimina il nodo contenente string dalla lista puntata da list.
    Restituisce 0 in caso di successo, -1 in caso di fallimento e setta errno
*/
int deleteString (char * string, strNode ** list);

/* Cerca il nodo nella lista list che abbia come campo str string.
    Restituisce -1 in caso di fallimento e state del nodo in caso di successo
*/
int searchString (char * string, strNode * list);

/* Se è esistente e aperto setta lo state dell'elemento con str string a.
    Restituisce in caso di successo, -1 altrimenti e setta errno
*/
int closeString (char * string, strNode ** list);


#endif