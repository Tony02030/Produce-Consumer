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
#include <sys/types.h>
#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <time.h>
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
int consumedMessages;
int keyIndicator;
double totalBlockedTime, totalExpectedTime;
float kernelTime, userTime;
int bufferSize;
int totalSize;
int producerMode, randomValue;
const char *empty = "/EMPTY";
const char *full = "/FULL";
const char *mutex = "/MUTEX";
pid_t pid;

char findKey(char array[])
{
  char g;
  for (int i = 0; i < strlen(array) - 1; i++)
  {
    if (array[i] == 'L' && array[i + 1] == 'l')
    {
      g = array[i + 13];
    }
  }
  return g;
}

char findpid(char array[])
{
  char g;
  for (int i = 0; i < strlen(array) - 1; i++)
  {
    if (array[i] == 'o' && array[i + 1] == 'r' && array[i + 2] == ':')
    {
      for (int x = 0; array[x] != '\n'; x++)
      {
        g = array[x];
      }
    }
  }
  return g;
}

// The following code was taken from https://stackoverflow.com/questions/7034930/how-to-generate-gaussian-pseudo-random-numbers-in-c-for-a-given-mean-and-varianc
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

  struct tms t;
  clock_t dub;
  dub = times(&t);

  tics_per_second = sysconf(_SC_CLK_TCK);
  int fd_description;

  sem_t *empty_sem, *full_sem, *mutex_sem;

  /* Open shared memory and assign the address to the integer */
  fd_description = shm_open(name, O_EXCL | O_RDWR, 0666);
  if (fd_description == -1)
  {
    printf("Consumer: Shared memory failed: %s\n", strerror(errno));
    exit(1);
  }
  bufferSize = atoi(number);
  producerMode = atoi(mode);
  randomValue = atoi(random);

  struct control
  {
    char buffer[bufferSize][256];
    int producers;            // number of producers at any given time
    int consumers;            // number of consumers at any given time
    int producerIndex;        // indicates where the message should be inserted
    int consumerIndex;        // indicates where the message was taken from
    bool flag;                // indicates if the terminator sent the terminate signal
    // statistics
    int messagesLeftBuffer;   // indicates the number of messages left in the buffer after removing consumers
    int totalProducedMessages;// indicates the total number of messages produced
    int totalProducers;       // total number of producers
    int totalConsumers;       // total number of consumers
    int keyConsumersDeleted;  // number of consumers deleted by random
    double waitingTime;       // accumulated time waiting for resources
    double blockedTime;       // time spent blocked
    float userTime;           // user time
    float kernelTime;         // kernel time
  };

  /* Map the shared memory segment to the pointer variable */
  struct control *pointer = mmap(NULL, sizeof(struct control), PROT_READ | PROT_WRITE, MAP_SHARED, fd_description, 0);
  if (pointer == MAP_FAILED)
  {
    printf("Consumer: Mapping failed: %s\n", strerror(errno));
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

  if (producerMode == 1)
  {
    sem_wait(mutex_sem);                                                   // enter critical region
    pointer->consumers = pointer->consumers + 1;                           // increase the number of active consumers
    pointer->totalConsumers = pointer->totalConsumers + 1;                 // increase the total number of consumers
    sem_post(mutex_sem);                                                   // exit critical region
    char oldMessage[100];
    char newMessage[100] = "NULL";
    while (pointer->flag == false && keyIndicator != 1)
    {
      struct timespec ts;
      if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
      {
        return -1;
      }

      ts.tv_sec += 30;

      clock_t startTimeBlocked = clock();
      if (pointer->flag == false)
      {
        sem_timedwait(full_sem, &ts); // decrease the count of full slots
        clock_t startTimeWaiting = clock();
        if (pointer->flag == false && pointer->producers > 0)
        {
          sem_wait(mutex_sem); // enter critical region and consume the message
          if (pointer->consumerIndex == bufferSize) // Check if the consumer index is the same size as the buffer, if so, return to position 0
          {
            pointer->consumerIndex = pointer->consumerIndex % bufferSize;
          }
          strcpy(oldMessage, pointer->buffer[pointer->consumerIndex]);
          sprintf(pointer->buffer[pointer->consumerIndex], "%s", newMessage);
          printf(GRN "A message was consumed from the buffer\n");
          printf(GRN "Message:\n%s\n", oldMessage);
          printf(GRN "Buffer position where the message was consumed: %s %d\n", CYN, pointer->consumerIndex);
          printf(GRN "Active Producers: %s %d\n", CYN, pointer->producers);
          printf(GRN "Active Consumers:%s %d\n", CYN, pointer->consumers);
          printf(RED "---------------------------------------------------------------\n");
          pointer->consumerIndex = pointer->consumerIndex + 1;
          if (findKey(oldMessage) == findpid(oldMessage)) // Compare the last digit of the producer's PID with the key
          {
            keyIndicator = keyIndicator + 1;
          }
          pointer->messagesLeftBuffer = pointer->messagesLeftBuffer - 1;
          consumedMessages = consumedMessages + 1;
          sem_post(mutex_sem); // exit critical region
          clock_t endTimeBlocked = clock();
          clock_t endTimeWaiting = clock();
          sem_post(empty_sem); // increase the count of empty slots
          double total1 = (double)(endTimeBlocked - startTimeBlocked) / CLOCKS_PER_SEC;
          double total2 = (double)(endTimeWaiting - startTimeWaiting) / CLOCKS_PER_SEC;
          totalBlockedTime = totalBlockedTime + total1;
          totalExpectedTime = totalExpectedTime + total2;

          sleep((int)fabs(randomValue + 10 * random_normal()));
        }
      }
    };
  }
  if (producerMode == 2)
  {
    sem_wait(mutex_sem);                                                   // enter critical region
    pointer->consumers = pointer->consumers + 1;                           // increase the number of active consumers
    pointer->totalConsumers = pointer->totalConsumers + 1;                 // increase the total number of consumers
    sem_post(mutex_sem);                                                   // exit critical region
    char ch;
    printf(GRN "\n***Press Enter to consume message: \n***");
    ch = fgetc(stdin);
    char oldMessage[100];
    char newMessage[100] = "NULL";
    while (pointer->flag == false && ch == 0x0A && keyIndicator != 1)
    {
      struct timespec ts;
      if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
      {
        return -1;
      }

      ts.tv_sec += 40;
      clock_t startTimeBlocked = clock();
      if (pointer->flag == false)
      {
        sem_timedwait(full_sem, &ts); // decrease the count of full slots
        clock_t startTimeWaiting = clock();
        if (pointer->flag == false && pointer->producers > 0)
        {
          sem_wait(mutex_sem); // enter critical region and consume the message
          if (pointer->consumerIndex == bufferSize) // Check if the consumer index is the same size as the buffer, if so, return to position 0
          {
            pointer->consumerIndex = pointer->consumerIndex % bufferSize;
          }
          strcpy(oldMessage, pointer->buffer[pointer->consumerIndex]);
          sprintf(pointer->buffer[pointer->consumerIndex], "%s", newMessage);
          printf(GRN "A message was consumed from the buffer\n");
          printf(GRN "Message:\n%s\n", oldMessage);
          printf(GRN "Buffer position where the message was consumed: %s %d\n", CYN, pointer->consumerIndex);
          printf(GRN "Active Producers: %s %d\n", CYN, pointer->producers);
          printf(GRN "Active Consumers:%s %d\n", CYN, pointer->consumers);
          printf(RED "---------------------------------------------------------------\n");
          pointer->consumerIndex = pointer->consumerIndex + 1;
          if (findKey(oldMessage) == findpid(oldMessage)) // Compare the last digit of the producer's PID with the key
          {
            keyIndicator = keyIndicator + 1;
          }
          pointer->messagesLeftBuffer = pointer->messagesLeftBuffer - 1;
          consumedMessages = consumedMessages + 1;
          sem_post(mutex_sem); // exit critical region
          clock_t endTimeBlocked = clock();
          clock_t endTimeWaiting = clock();
          sem_post(empty_sem); // increase the count of empty slots
          double total1 = (double)(endTimeBlocked - startTimeBlocked) / CLOCKS_PER_SEC;
          double total2 = (double)(endTimeWaiting - startTimeWaiting) / CLOCKS_PER_SEC;
          totalBlockedTime = totalBlockedTime + total1;
          totalExpectedTime = totalExpectedTime + total2;
          sleep((int)fabs(randomValue + 10 * random_normal()));
        }
      }
      printf(GRN "\n***Press Enter to consume message: \n***");
      ch = fgetc(stdin);
    };
  }
  sem_wait(mutex_sem);                                         // enter critical region
  pointer->consumers = pointer->consumers - 1;                 // decrease the number of active producers
  pointer->blockedTime = pointer->blockedTime + totalBlockedTime;// add total blocked time to memory
  pointer->waitingTime = pointer->waitingTime + totalExpectedTime;// add total expected time to memory
  sem_post(mutex_sem);                                         // exit critical region
  getrusage(RUSAGE_SELF, &end1);
  getrusage(RUSAGE_SELF, &end2);
  userTime = end1.ru_utime.tv_sec - start1.ru_utime.tv_sec + 1e-6 * (end1.ru_utime.tv_usec - start1.ru_utime.tv_usec);// calculate user time
  kernelTime = end2.ru_stime.tv_sec - start2.ru_stime.tv_sec + 1e-6 * (end2.ru_stime.tv_usec - start2.ru_stime.tv_usec);// calculate kernel time

  sem_wait(mutex_sem);                                        // enter critical region
  pointer->userTime = pointer->userTime + userTime;           // increase total user time
  pointer->kernelTime = pointer->kernelTime + kernelTime;     // increase total kernel time
  sem_post(mutex_sem);                                        // exit critical region

  printf(RED "----------------Statistics---Consumer---------------------------\n");
  printf(GRN "Consumer PID: %s%d\n", CYN, getpid());
  printf(GRN "Suspension Reason: ");
  if (keyIndicator == 1)
  {
    sem_wait(mutex_sem); // enter critical region
    pointer->keyConsumersDeleted = pointer->keyConsumersDeleted + 1;
    sem_post(mutex_sem);
    printf(CYN "Suspended by key\n");
  }
  else
  {
    printf(CYN "Suspended by terminator\n");
  }
  printf(GRN "Total messages consumed: %s%d\n", CYN, consumedMessages);
  printf(GRN "User Time: %s%f s\n", CYN, userTime);
  printf(GRN "Kernel Time: %s%f s\n", CYN, kernelTime);
  printf(GRN "Total Expected Time: %s%f s\n", CYN, totalExpectedTime);
  printf(GRN "Total Blocked Time: %s%f s\n", CYN, totalBlockedTime);

  /* Remove the mapping of the shared memory segment from the process */
  if (munmap(pointer, sizeof(struct control)) == -1)
  {
    printf("Consumer: Unmapping failed: %s\n", strerror(errno));
    exit(1);
  }

  return 0;
}
