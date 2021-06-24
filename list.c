#include "list.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>

node * addNode (int desc) {
    node * List = malloc(sizeof(node));
    List->descriptor = desc;
    List->next =NULL;
    return List;
}

node * deleteNode(node * List){
    node * toDelete =  List;
    List=List->next;
    free(toDelete);
    return List;
}

void addFile (FILE * f, char * fname, int fOwner, fileNode **lastAddedFile, int * fileCount) {
    fileNode * newFile = malloc(sizeof(fileNode));
    newFile->owner = fOwner;
    newFile->fPointer = f;
    newFile->next = NULL;
    newFile->prev = lastAddedFile;
    strcpy (newFile->fileName, fname);
    fileCount++;
    lastAddedFile = newFile;
}

fileNode * searchFile (FILE * f, char * fname, fileNode * storage) {
    fileNode * currFP = storage;
    while (currFP->next!= NULL) {
        if (strcmp(currFP->fileName, fname)==0)
            return currFP;
        currFP=currFP->next;
    }
    
    return NULL;
}

void deleteFile (fileNode * f, fileNode ** storage, fileNode ** lastAddedFile, int * fileCount) {
    if(f == lastAddedFile) lastAddedFile=f->prev;
    if(f == storage) storage=f->next;
    fileNode * p = f->prev;
    fileNode * n = f->next;
    p->next = n;
    n->prev = p;
    fclose(f->fPointer);
    free(f);
    fileCount--;
}

strNode * addString (char * string, strNode * list) {

    if (searchString(string, list) !=-1 ) {
        errno = EEXIST;
        return NULL;
    }

    strNode * newNode = malloc(sizeof(strNode));
    strcpy(newNode->str,string);
    newNode->state=1;
    newNode->next=list;
    return newNode;
}

int deleteString (char * string, strNode ** list) {

    strNode * currNode = list;
    strNode * prevNode = NULL;
    int cmp;
    while (currNode->next != NULL && (cmp=strcmp(string, currNode->str))!=0) {
        prevNode = currNode;
        currNode = currNode->next;
    }

    // Non è stato trovato il nodo da eliminare
    if (cmp!=0) {
        errno = ENOENT;
        return -1;
    }

    else {

        // Il nodo da eliminare è in testa alla lista
        if (prevNode == NULL) {
            prevNode = currNode;
            list = currNode;
            free(prevNode);
            return 0;
        }

        // Il nodo da eliminare è in coda alla lista
        if (currNode->next == NULL) {
            prevNode->next=NULL;
            free(currNode);
            return 0; 
        }

        // Il nodo da eliminare è in mezzo alla lista
        prevNode->next = currNode->next;
        free(currNode);
        return 0;
    }
    
}

int searchString(char * string, strNode * list) {
    strNode * curr = list;
    while (curr != NULL) {
        if (strcmp(string,curr->str)==0) return curr->str;
    }
    return -1;
}

int closeString (char * string, strNode ** list) {
    int searchR = searchString(string, list);
    switch (searchR) {
    case -1:
        errno = ENOENT;
        return -1;
        break;
    
    case 0:
        return 0;

    case 1:
        strNode * curr = list;
        while (strcmp(curr->str,string)!=0) {
            curr=curr->next;
        }
        curr->state=0;
        return 0;
    }
}