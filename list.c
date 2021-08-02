#include "list.h"
#include "commProtocol.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>

node * addNode (int desc) {
    node * List = calloc(1,sizeof(node));
    List->descriptor = desc;
    List->next =NULL;
    return List;
}

node * popNode(node * List){
    node * toDelete =  List;
    List=List->next;
    free(toDelete);
    return List;
}

void deleteNode (int fd, node ** list){
    if  ((*list)->descriptor==fd) *list=popNode(*list);
    else {
        node * previous = NULL;
        node * current = *list;
        while (current->descriptor!=fd && current!=NULL) {
            previous = current;
            current = current->next;
        }
        if (current!=NULL && current->descriptor==fd) {
            previous->next=current->next;
            free (current);
        }
    }
}

fileNode * newFile (FILE * f, long size, char * fname, int fOwner, fileNode **lastAddedFile) {
    fileNode * newFile = calloc(1,sizeof(fileNode));
    newFile->owner = fOwner;
    newFile->fileSize = size;
    newFile->fPointer = f;
    newFile->next = NULL;
    newFile->prev = *lastAddedFile;
    newFile->fileName = calloc((strlen(fname)+1),sizeof(char));
    strcpy (newFile->fileName, fname);
    return newFile;
}

void addFile (FILE * f, long size, char * fname, int fOwner, fileNode **lastAddedFile) {
    fileNode * newFile = calloc(1,sizeof(fileNode));
    newFile->owner = fOwner;
    newFile->fileSize = size;
    newFile->fPointer = f;
    newFile->next = NULL;
    newFile->prev = *lastAddedFile;
    newFile->fileName = calloc((strlen(fname)+1),sizeof(char));
    strcpy (newFile->fileName, fname);
    *lastAddedFile = newFile;
}

int searchFile (char * fname, fileNode * storage, fileNode ** ptr) {

    if (storage==NULL) {
        return -1;}
    else {
        fileNode * currFP = storage;
        while (currFP != NULL) {
            if (strcmp(currFP->fileName, fname)==0) {
                * ptr = currFP;
                return 0;
            }
            currFP=currFP->next;
        }
    return -1;
    }
}

int checkLock (char * fname, fileNode * storage){
    fileNode * ptr = NULL;
    int res;
    if ((res=searchFile(fname,storage,&ptr))==-1) return -1;
    return ptr->owner;
}

fileNode * popFile (fileNode * list){
    fileNode * toDelete = list;
    list = list->next;
    free(toDelete);
    return list;
}

void deleteFile (fileNode * f, fileNode ** storage, fileNode ** lastAddedFile) {


        if(f == *lastAddedFile) *lastAddedFile=f->prev;
        if(f == *storage) *storage=f->next;

        fileNode * p = f->prev;
        fileNode * n = f->next;

        // Non è in testa
        if (p != NULL) p->next = n;
        // Non è in coda
        if (n != NULL) n->prev = p;
        
        fclose(f->fPointer);
        free (f->fileName);
        free(f);
}

strNode * addString (const char * string, strNode * list) {

    if (searchString(string, list) !=-1 ) {
        errno = EEXIST;
        return NULL;
    }
    strNode * newNode = calloc(1,sizeof(strNode));
    strcpy(newNode->str,string);
    newNode->state=1;
    newNode->next=list;
    return newNode;
}

int deleteString (const char * string, strNode ** list) {

    strNode * currNode = *list;
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
            *list = currNode;
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

int searchString(const char * string, strNode * list) {

    strNode * curr = list;
    while (curr != NULL) {
        if (strcmp(string,curr->str)==0) return curr->state;
        curr=curr->next;
    }
    return -1;
}

int closeString (const char * string, strNode ** list) {
    int searchR = searchString(string, *list);
    switch (searchR) {
        case -1:
            errno = ENOENT;
            return -1;
            break;
        
        case 0:
            return 0;

        case 1: {
            strNode * curr = *list;
            while (strcmp(curr->str,string)!=0) {
                curr=curr->next;
            }
            curr->state=0;
            return 0;
        }
    }

    return 0;
}