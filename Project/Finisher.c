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

int shm_fd;
int buffer_size;
int total_size;
char *name, *number;
const char *empty = "/EMPTY";
const char *full = "/FULL";
const char *mutex = "/MUTEX";

sem_t *empty_sem, *full_sem, *mutex_sem;

int main(int argc, char **argv)
{
    name = argv[1];
    /* Open the shared memory and assign the address to the integer */
    shm_fd = shm_open(name, O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
    if (shm_fd == -1)
    {
        printf("prod: Shared memory failed: %s\n", strerror(errno));
        exit(1);
    }

    number = argv[2];
    buffer_size = atoi(number);

    struct control
    {
        char buffer[buffer_size][256];
        int producers;      // Number of producers at any given moment
        int consumers;     // Number of consumers at any given moment
        int producer_index;  // Indicates where the message should be inserted
        int consumer_index; // Indicates where the message was taken from

        bool flag; // Indicates if the terminator sent the termination signal
        // Statistics
        int messages_left_buffer;      // Indicates the number of messages left in the buffer after deleting consumers
        int total_produced_messages;     // Indicates the total number of messages produced
        int total_producers;          // Total number of producers
        int total_consumers;         // Total number of consumers
        int key_consumers_deleted; // Number of consumers deleted by the random
        double waiting_time;             // Accumulated waiting time for resources
        double blocked_time;          // Time spent blocked
        float user_time;        // User time
        float kernel_time;        // Kernel time
    };
    /* Map the shared memory segment to the pointer variable */
    struct control *pointer = mmap(NULL, sizeof(struct control), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (pointer == MAP_FAILED)
    {
        printf("prod: Map failed: %s\n", strerror(errno));
        close(shm_fd);
        shm_unlink(name);
        exit(1);
    }
    // Initialize semaphores
    if ((mutex_sem = sem_open(mutex, O_CREAT)) == SEM_FAILED)
    {
        printf("Error opening %s: %s\n", mutex, strerror(errno));
    }

    if ((empty_sem = sem_open(empty, O_CREAT)) == SEM_FAILED)
    {
        printf("Error opening %s: %s\n", empty, strerror(errno));
    }

    if ((full_sem = sem_open(full, O_CREAT)) == SEM_FAILED)
    {
        printf("Error opening %s: %s\n", full, strerror(errno));
    }

    sem_wait(mutex_sem); // enter critical region
    pointer->flag = true;
    sem_post(mutex_sem); // exit critical region

    while(pointer->producers > 0 || pointer->consumers > 0){// Wait until there are no active producers or consumers
    }
    // Print statistics
    printf("Messages entered into the buffer: %d\n", pointer->total_produced_messages);
    printf("Total producers: %d\n", pointer->total_producers);
    printf("Total consumers: %d\n", pointer->total_consumers);
    printf("Messages in the buffer: %d\n", pointer->messages_left_buffer);
    printf("Consumers deleted by key: %d\n", pointer->key_consumers_deleted);
    printf("Total waiting time: %lf s\n", pointer->waiting_time);
    printf("Total blocked time: %lf s\n", pointer->blocked_time);
    printf("Total user time: %f s\n", pointer->user_time);
    printf("Total kernel time: %f s\n", pointer->kernel_time);
    // Close semaphores
    sem_close(full_sem);
    sem_close(empty_sem);
    sem_close(mutex_sem);
    // Unlink semaphore names
    sem_unlink(full);
    sem_unlink(empty);
    sem_unlink(mutex);
    // Unmap shared memory
    if (munmap(pointer, sizeof(struct control)) == -1)
    {
        printf("Unmapping failed: %s\n", strerror(errno));
        exit(1);
    }
    // Close shared memory
    close(shm_fd);
    // Unlink shared memory name
    shm_unlink(name);
}
