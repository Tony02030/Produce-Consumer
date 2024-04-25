#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <errno.h>
#include <semaphore.h> 
#include <stdbool.h>

//-----------------------
int fd_memory;
int bufferSize;
char *name, *number;
const char *empty="/EMPTY";   // Semaphore name for empty buffer slots
const char *full="/FULL";     // Semaphore name for full buffer slots
const char *mutex="/MUTEX";   // Semaphore name for mutual exclusion

sem_t *empty_sem, *full_sem, *mutex_sem;

int main(int argc, char **argv) {
    
    name = argv[1]; // Shared memory segment name
    
    /* Opens the shared memory and assigns the address to an integer */
    if ((fd_memory = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0660)) == -1)
        perror("shm_open");
    
    //if(fd_memory = shm_open(name, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600) == -1){
    //    fd_memory = shm_open(name, O_RDWR | O_TRUNC, (mode_t)0600);
    //}
   
    number = argv[2]; // Buffer size as string
    bufferSize = atoi(number); // Converts buffer size to integer

    struct control {
        char buffer[bufferSize][256];   // Buffer for messages
        int producers;                  // Number of producers at any given time
        int consumers;                  // Number of consumers at any given time
        int producerIndex;              // Indicates where the message should be inserted
        int consumerIndex;              // Indicates from where the message was taken
        bool flag;                      // Indicates if the terminator sent the termination signal
        //statistics
        int messagesLeftInBuffer;       // Indicates the number of messages left in the buffer after consumers removal
        int totalMessagesProduced;      // Indicates the total number of messages produced
        int totalProducers;             // Total number of producers
        int totalConsumers;             // Total number of consumers
        int consumersRemovedKey;        // Number of consumers removed by random
        double waitTime;                // Accumulated wait time for resource
        double blockedTime;             // Time spent blocked
        float utime;                    // User time
        float stime;                    // Kernel time
    };

    /* Configures the size of the shared memory segment */
    ftruncate(fd_memory, sizeof(struct control));

    /* Maps the shared memory segment to the pointer variable */
    struct control *pointer = mmap(NULL, sizeof(struct control), PROT_READ | PROT_WRITE, MAP_SHARED, fd_memory, 0);
    
    if (pointer == MAP_FAILED) {
        printf("prod: Map failed: %s\n", strerror(errno));
        close(fd_memory);
        shm_unlink(name); 
        exit(1);
    }

    // Initializes variables
    pointer->flag = false;
    pointer->messagesLeftInBuffer = 0;
    pointer->totalMessagesProduced = 0;
    pointer->consumers = 0;
    pointer->consumersRemovedKey = 0;
    pointer->totalConsumers = 0;
    pointer->producerIndex = 0;
    pointer->consumerIndex = 0;
    pointer->producers = 0;
    pointer->totalProducers = 0;
    pointer->blockedTime = 0;
    pointer->waitTime = 0;
    pointer->utime = 0;
    pointer->stime = 0;
    
    // Creates and initializes the semaphores
    if ((mutex_sem = sem_open(mutex, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1)) == SEM_FAILED) {
        printf("Error creating %s: %s\n", mutex, strerror(errno));
    }

    if ((empty_sem = sem_open(empty, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, bufferSize)) == SEM_FAILED) {
        printf("Error creating %s: %s\n", empty, strerror(errno));
    }
        
    if ((full_sem = sem_open(full, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0)) == SEM_FAILED) {
        printf("Error creating %s: %s\n", full, strerror(errno));
    }

    return 0;
}
