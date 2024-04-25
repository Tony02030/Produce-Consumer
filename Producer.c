#define _GNU_SOURCE
#define _POSIX_SOURCE
#include <sys/times.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "math.h"
#include <sys/types.h>
#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <time.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <math.h>

#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define MAG "\x1B[35m"
#define CYN "\x1B[36m"
#define WHT "\x1B[37m"
#define RESET "\x1B[0m"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int tics_per_second;
int producedMessages;
double totalBlockedTime, totalExpectedTime;
int bufferSize;
int totalSize;
int producerMode, randm;
float userTime, kernelTime;
const char *empty = "/EMPTY";
const char *full = "/FULL";
const char *mutex = "/MUTEX";
pid_t pid;
time_t rawtime;
struct tm *timeinfo;
//The following code was taken from https://stackoverflow.com/questions/7034930/how-to-generate-gaussian-pseudo-random-numbers-in-c-for-a-given-mean-and-varianc
double drand()
{
  return (rand() + 1.0) / (RAND_MAX + 1.0);
}

double random_normal()

{
  return sqrt(-2 * log(drand())) * cos(2 * M_PI * drand());
}
//--------------------------------------------------------
int main(int argc, char **argv)
{
  struct rusage start1, start2, end1, end2;
  getrusage(RUSAGE_SELF, &start1);
  getrusage(RUSAGE_SELF, &start2);
  char *name, *number, *mode, *random;
  name = argv[1];
  number = argv[2];
  random = argv[3];
  mode = argv[4];

  int fd_description;

  sem_t *empty_sem, *full_sem, *mutex_sem;

  /* Open the shared memory and assign the address to the integer */
  fd_description = shm_open(name, O_EXCL | O_RDWR, 0666);
  if (fd_description == -1)
  {
    printf("Producer: Shared memory failed: %s\n", strerror(errno));
    exit(1);
  }
  bufferSize = atoi(number);
  producerMode = atoi(mode);
  randm = atoi(random);

  struct control
  {
    char buffer[bufferSize][256];
    int producers;         // Number of producers at any given moment
    int consumers;         // Number of consumers at any given moment
    int producerIndex;     // Indicates where the message should be inserted
    int consumerIndex;     // Indicates where the message was taken from
    bool flag;             // Indicates if the terminator sent the termination signal
    //statistics
    int messagesLeftBuffer;         // Indicates the number of messages left in the buffer after deleting consumers
    int totalProducedMessages;      // Indicates the total number of messages produced
    int totalProducers;             // Total number of producers
    int totalConsumers;             // Total number of consumers
    int keyConsumersDeleted;        // Number of consumers deleted by the random
    double waitingTime;             // Accumulated waiting time for resources
    double blockedTime;             // Time spent blocked
    float userTime;                 // User time
    float kernelTime;               // Kernel time
  };

  /* Map the shared memory segment to the pointer variable */
  struct control *pointer = mmap(NULL, sizeof(struct control), PROT_READ | PROT_WRITE, MAP_SHARED, fd_description, 0);
  if (pointer == MAP_FAILED)
  {
    printf("Producer: Mapping failed: %s\n", strerror(errno));
    close(fd_description);
    shm_unlink(name);
    exit(1);
  }

  // Initialize semaphores
  if ((empty_sem = sem_open(empty, O_CREAT)) == SEM_FAILED)
    printf("Error opening %s: %s\n", empty, strerror(errno));
  if ((full_sem = sem_open(full, O_CREAT)) == SEM_FAILED)
    printf("Error opening %s: %s\n", full, strerror(errno));
  if ((mutex_sem = sem_open(mutex, O_CREAT)) == SEM_FAILED)
    printf("Error opening %s: %s\n", mutex, strerror(errno));

  sem_wait(mutex_sem); // enter critical region
  pointer->producers = pointer->producers + 1;
  pointer->totalProducers = pointer->totalProducers + 1;
  sem_post(mutex_sem); // exit critical region

  if (producerMode == 1)
  {
    int count = 0;
    while (pointer->flag == false)
    {
      struct timespec ts;
      if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
      {
        return -1;
      }
      ts.tv_sec += 60;

      time(&rawtime);
      timeinfo = localtime(&rawtime);
      char *dateTime = asctime(timeinfo);
      int producerId = getpid();
      char randomKey = (rand() % 7) + '0';
      char message[100];
      sprintf(message, "%sProducer ID:%s%d\n%sDate:%s%s%sKey: %s %c %s\n ", GRN, CYN, producerId, GRN, CYN, dateTime, GRN, CYN, randomKey, RESET);
      clock_t startTimeBlocked = clock();
      sem_timedwait(empty_sem, &ts); // decrease the count of empty slots
      clock_t startTimeWaiting = clock();
      if (pointer->flag == false || pointer->consumers > 0)
      {
        sem_wait(mutex_sem);                               // enter critical region
        if (pointer->producerIndex == bufferSize)          // Check if the producer index is the same size as the buffer, if so, return to position 0
        {
          pointer->producerIndex = pointer->producerIndex % bufferSize;
        }
        sprintf(pointer->buffer[pointer->producerIndex], "%s", message); // place the new item in the buffer
        pointer->totalProducedMessages = pointer->totalProducedMessages + 1;
        producedMessages = producedMessages + 1;
        printf(MAG "A message was put in the buffer\n");
        printf(MAG "Producer ID: %s%d\n", YEL, producerId);
        printf(MAG "Date: %s%s\n", YEL, dateTime);
        printf(MAG "Key: %s%c\n", YEL, randomKey);
        printf(MAG "Buffer position where the message was inserted: %s %d\n", YEL, pointer->producerIndex);
        printf(MAG "Active Producers: %s %d\n", YEL, pointer->producers);
        printf(MAG "Active Consumers:%s %d\n", YEL, pointer->consumers);
        printf(BLU "---------------------------------------------------------------\n");
        pointer->producerIndex = pointer->producerIndex + 1;
        pointer->messagesLeftBuffer = pointer->messagesLeftBuffer + 1;
        sem_post(mutex_sem); // exit critical region
        clock_t endTimeBlocked = clock();
        clock_t endTimeWaiting = clock();
        sem_post(full_sem); // increase the count of full slots
        count = count + 1;
        double total1 = (double)(endTimeBlocked - startTimeBlocked) / CLOCKS_PER_SEC;
        double total2 = (double)(endTimeWaiting - startTimeWaiting) / CLOCKS_PER_SEC;
        totalBlockedTime = totalBlockedTime + total1;
        totalExpectedTime = totalExpectedTime + total2;
        sleep((int)fabs(randm + 10 * random_normal()));
      }
    };
  }

  if (producerMode == 2)
  {
    int count = 0;
    char ch;
    printf(MAG "\n***Press Enter to add message: \n***");
    ch = fgetc(stdin);
    while (pointer->flag == false && ch == 0x0A)
    {
      struct timespec ts;
      if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
      {
        return -1;
      }
      ts.tv_sec += 60;
      time(&rawtime);
      timeinfo = localtime(&rawtime);
      char *dateTime = asctime(timeinfo);
      int producerId = getpid();
      char randomKey = (rand() % 7) + '0';
      char message[100];
      sprintf(message, "%sProducer ID:%s%d\n%sDate:%s%s%sKey: %s %c %s ", GRN, CYN, producerId, GRN, CYN, dateTime, GRN, CYN, randomKey, RESET);
      clock_t startTimeBlocked = clock();
      sem_timedwait(empty_sem, &ts); // decrease the count of empty slots
      if (pointer->flag == false || pointer->consumers > 0)
      {
        clock_t startTimeWaiting = clock();
        sem_wait(mutex_sem);                               // enter critical region
        if (pointer->producerIndex == bufferSize)          // Check if the producer index is the same size as the buffer, if so, return to position 0
        {
          pointer->producerIndex = pointer->producerIndex % bufferSize;
        }
        sprintf(pointer->buffer[pointer->producerIndex], "%s", message); // place the new item in the buffer
        pointer->totalProducedMessages = pointer->totalProducedMessages + 1;
        producedMessages = producedMessages + 1;
        printf(MAG "A message was put in the buffer\n");
        printf(MAG "Producer ID: %s%d\n", YEL, producerId);
        printf(MAG "Date: %s%s\n", YEL, dateTime);
        printf(MAG "Key: %s%c\n", YEL, randomKey);
        printf(MAG "Buffer position where the message was inserted: %s %d\n", YEL, pointer->producerIndex);
        printf(MAG "Active Producers: %s %d\n", YEL, pointer->producers);
        printf(MAG "Active Consumers:%s %d\n", YEL, pointer->consumers);
        printf(BLU "---------------------------------------------------------------\n");
        pointer->producerIndex = pointer->producerIndex + 1;
        pointer->messagesLeftBuffer = pointer->messagesLeftBuffer + 1;
        sem_post(mutex_sem); // exit critical region
        clock_t endTimeBlocked = clock();
        clock_t endTimeWaiting = clock();
        sem_post(full_sem); // increase the count of full slots
        count = count + 1;
        double total1 = (double)(endTimeBlocked - startTimeBlocked) / CLOCKS_PER_SEC;
        double total2 = (double)(endTimeWaiting - startTimeWaiting) / CLOCKS_PER_SEC;
        totalBlockedTime = totalBlockedTime + total1;
        totalExpectedTime = totalExpectedTime + total2;
        count = count + 1;
        sleep((int)fabs(randm + 10 * random_normal()));
      }
      printf(MAG "\n***Press Enter to add message: \n***");
      ch = fgetc(stdin);
    };
  }
  sem_wait(mutex_sem);                                // enter critical region
  pointer->producers = pointer->producers - 1;        // decrease the number of active producers
  pointer->blockedTime = pointer->blockedTime + totalBlockedTime;
  pointer->waitingTime = pointer->waitingTime + totalExpectedTime;
  sem_post(mutex_sem);                                // exit critical region

  getrusage(RUSAGE_SELF, &end1);
  getrusage(RUSAGE_SELF, &end2);
  userTime = end1.ru_utime.tv_sec - start1.ru_utime.tv_sec + 1e-6 * (end1.ru_utime.tv_usec - start1.ru_utime.tv_usec);
  kernelTime = end2.ru_stime.tv_sec - start2.ru_stime.tv_sec + 1e-6 * (end2.ru_stime.tv_usec - start2.ru_stime.tv_usec);

  sem_wait(mutex_sem);                      // enter critical region
  pointer->userTime = pointer->userTime + userTime; // increase the total user time
  pointer->kernelTime = pointer->kernelTime + kernelTime; // increase the total kernel time
  sem_post(mutex_sem);                      // exit critical region
  /* Remove the mapping of the shared memory segment from the process */
  if (munmap(pointer, sizeof(struct control)) == -1)
  {
    printf("Producer: Unmapping failed: %s\n", strerror(errno));
    exit(1);
  }
  printf(BLU "----------------Statistics---Producer---------------------------\n");
  printf(MAG "Producer PID: %s%d\n", YEL, getpid());
  printf(MAG "Messages added by producer: %s%d\n", YEL, producedMessages);
  printf(MAG "User Time: %s%f s\n", YEL, userTime);
  printf(MAG "Kernel Time: %s%f s\n", YEL, kernelTime);
  printf(MAG "Total expected time: %s%f s\n", YEL, totalExpectedTime);
  printf(MAG "Total blocked time: %s%f s\n", YEL, totalBlockedTime);

  return 0;
}
