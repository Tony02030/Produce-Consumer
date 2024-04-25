#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h> 
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
const char * empty="/EMPTY";
const char * full="/FULL";
const char * mutex="/MUTEX";
sem_t * empty_sem, *full_sem, *mutex_sem;
int main (int argc, char** argv){

   if ( (mutex_sem = sem_open(mutex,O_CREAT)) == SEM_FAILED){
        printf("Error creating %s: %s\n", mutex, strerror(errno));
    }

   if ( (empty_sem = sem_open(empty, O_CREAT)) == SEM_FAILED){
        printf("Error creating %s: %s\n", empty, strerror(errno));
    }
        
   if ( (full_sem = sem_open(full, O_CREAT)) == SEM_FAILED){
        printf("Error creating %s: %s\n", full, strerror(errno));
    }
    sem_close(full_sem);
    sem_close(empty_sem);
    sem_close(mutex_sem);

    sem_unlink(full);
    sem_unlink(empty);
    sem_unlink(mutex);
    




}
