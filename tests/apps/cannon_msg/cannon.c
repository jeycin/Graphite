/********************************************************
 * This message passing only implementation of Cannon's *
 * algorithm for matrix multiplication. The two input   *
 * matrices are hard-coded into the program.            *
 *            - (edited from Harshad Kasture's version) *
 *                          - Charles Gruenwald         *
 *                          12/12/2008                  *
 *******************************************************/

#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <stdarg.h>
#include <assert.h>
#include <unistd.h>

#include "carbon_user.h"

//#define DEBUG 1
//#define SEQUENTIAL 1

#ifdef DEBUG
pthread_mutex_t lock;
#endif

#define NUM_THREADS 4

unsigned int num_threads;

// Function executed by each thread
void spawner_wait_ack(int id);
void worker_send_ack(int tid);

void worker_wait_go(int tid);
void spawner_send_go(int tid);

void* cannon(void * threadid);

void debug_printf(const char * fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
#ifdef DEBUG
   pthread_mutex_lock(&lock);
   fprintf(stderr, fmt, ap);
   pthread_mutex_unlock(&lock);
#endif
}

int main(int argc, char* argv[])  // main begins
{
   CarbonStartSim(argc, argv);

   float **a, **b, **c;

   unsigned int matSize;

   // Read in the command line arguments
   if (argc != 5)
   {
      printf("Invalid command line options. The correct format is:\n");
      printf("cannon -m num_of_threads -s size_of_square_matrix\n");
      exit(EXIT_FAILURE);
   }
   else
   {
      if ((strcmp(argv[1], "-m\0") == 0) && (strcmp(argv[3], "-s\0") == 0))
      {
         num_threads = atoi(argv[2]);
         matSize = atoi(argv[4]);
      }
      else if ((strcmp(argv[1], "-s\0") == 0) && (strcmp(argv[3], "-m\0") == 0))
      {
         num_threads = atoi(argv[4]);
         matSize = atoi(argv[2]);
      }
      else
      {
         printf("Invalid command line options. The correct format is:\n");
         printf("cannon -m num_of_threads -s size_of_square_matrix\n");
         exit(EXIT_FAILURE);
      }
   }

   // Declare threads and related variables
   carbon_thread_t threads[num_threads];

   // Initialize global variables

   // Initialize a
   a = (float**)malloc(matSize*sizeof(float*));
   for (unsigned int i = 0; i < matSize; i++)
   {
      a[i] = (float*)malloc(matSize*sizeof(float));
      for (unsigned int j = 0; j < matSize; j++) a[i][j] = 2;
   }

   // Initialize b
   b = (float**)malloc(matSize*sizeof(float*));
   for (unsigned int i = 0; i < matSize; i++)
   {
      b[i] = (float*)malloc(matSize*sizeof(float));
      for (unsigned int j = 0; j < matSize; j++) b[i][j] = 3;
   }

   // Initialize c
   c = (float**)malloc(matSize*sizeof(float*));
   for (unsigned int i = 0; i < matSize; i++)
   {
      c[i] = (float*)malloc(matSize*sizeof(float));
      for (unsigned int j = 0; j < matSize; j++) c[i][j] = 0;
   }

   // FIXME: we get a compiler warning here because the output of
   // sqrt is being converted to an int.  We really should be doing
   // some sanity checking of num_threads and matSize to be sure
   // these calculations go alright.
   int blockSize, sqrtNumProcs;
   double tmp = num_threads;
   sqrtNumProcs = (float) sqrt(tmp);
   blockSize = matSize / sqrtNumProcs;

   CAPI_return_t rtnVal;
   rtnVal = CAPI_Initialize((unsigned int)num_threads);

#ifdef DEBUG
   printf("Initializing thread structures\n");
   pthread_mutex_init(&lock, NULL);
#endif

   // Spawn the worker threads
   for (unsigned int i = 0; i < num_threads; i++)
       threads[i] = CarbonSpawnThread(cannon, (void *) i);

   sleep(5);

   for (unsigned int i = 0; i < num_threads; i++)
   {
      int tid = i;

      bool started;
      CAPI_message_receive_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)num_threads, (char *)&started, sizeof(started));
      assert(started == 1);

      CAPI_message_send_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&blockSize, sizeof(blockSize));
      spawner_wait_ack(tid);

      CAPI_message_send_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&sqrtNumProcs, sizeof(sqrtNumProcs));
      spawner_wait_ack(tid);

      // Convert 1-D rank to 2-D rank
      int ax, ay, bx, by;
      int x = tid / sqrtNumProcs;
      int y = tid % sqrtNumProcs;

      ax = x * blockSize;
      ay = ((x + y) < sqrtNumProcs)?(x + y):(x + y - sqrtNumProcs);
      ay = ay * blockSize;

      bx = ((x + y) < sqrtNumProcs)?(x + y):(x + y - sqrtNumProcs);
      bx = bx * blockSize;
      by = y * blockSize;
      for (int row = 0; row < blockSize; row++)
      {
         CAPI_message_send_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&(a[ax + row][ay]), blockSize * sizeof(float));
         spawner_wait_ack(tid);
      }

      for (int row = 0; row < blockSize; row++)
      {
         CAPI_message_send_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&(b[bx + row][by]), blockSize * sizeof(float));
         spawner_wait_ack(tid);
      }
   }

   printf("  Done sending... exiting.\n");

   // Wait for all threads to complete
   for (unsigned int i = 0; i < num_threads; i++)
   {
      int tid = i;
      int ax, ay, bx, by;
      int x = tid / sqrtNumProcs;
      int y = tid % sqrtNumProcs;

      ax = x * blockSize;
      ay = ((x + y) < sqrtNumProcs)?(x + y):(x + y - sqrtNumProcs);
      ay = ay * blockSize;

      bx = ((x + y) < sqrtNumProcs)?(x + y):(x + y - sqrtNumProcs);
      bx = bx * blockSize;
      by = y * blockSize;

      spawner_send_go(tid);
      float *cRow = (float*)malloc(blockSize*sizeof(float));
      for (int row = 0; row < blockSize; row++)
      {
         CAPI_message_receive_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)num_threads, (char *)cRow, blockSize * sizeof(float));
         spawner_send_go(tid);

         for (int y = 0; y < blockSize; y++) c[ax + row][by + y] = cRow[y];
      }
      free(cRow);

      CarbonJoinThread(threads[i]);
   }


   // Print out the result matrix
   printf("c = \n");
   for (unsigned int i = 0; i < matSize; i++)
   {
      for (unsigned int j = 0; j < matSize; j++)
         printf("%f ", c[i][j]);
      printf("\n");
   }

   for (unsigned int i = 0; i < matSize; i++)
   {
      free(a[i]);
      free(b[i]);
      free(c[i]);
   }

   free(a);
   free(b);
   free(c);

   CarbonStopSim();
} // main ends

void spawner_send_go(int tid)
{
#ifdef SEQUENTIAL
   bool ack = 1;
   CAPI_message_send_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&ack, sizeof(ack));
#endif
}

void worker_wait_go(int tid)
{
#ifdef SEQUENTIAL
   bool ack;
   CAPI_message_receive_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&ack, sizeof(ack));
   assert(ack == true);
#endif
}

void spawner_wait_ack(int tid)
{
#ifdef SEQUENTIAL
   bool ack;
   CAPI_message_receive_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)num_threads, (char *)&ack, sizeof(ack));
   assert(ack == true);
#endif
}

void worker_send_ack(int tid)
{
#ifdef SEQUENTIAL
   bool ack = 1;
   CAPI_message_send_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)num_threads, (char *)&ack, sizeof(ack));
#endif
}

void* cannon(void *threadid)
{
   num_threads = NUM_THREADS;

   // Declare local variables
   int tid;
   int upProc, downProc, rightProc, leftProc;
   float **aBlock, **bBlock, **cBlock;
   int i, j;
   int ai, aj, bi, bj;
   CAPI_return_t rtnVal;

#ifdef DEBUG
   printf("Starting thread %d\n", (unsigned int)threadid);
#endif

   rtnVal = CAPI_Initialize((unsigned int)threadid);
   tid = (int) threadid;
   //CAPI_rank(&tid);

   sleep(2);

   bool started = true;
   CAPI_message_send_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)num_threads, (char *)&started, sizeof(started));

   fprintf(stderr, "Thread %d retrieving initial data...\n", tid);

   // Initialize local variables
   int blockSize, sqrtNumProcs;
   CAPI_message_receive_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&blockSize, sizeof(blockSize));
   worker_send_ack(tid);

   CAPI_message_receive_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)&sqrtNumProcs, sizeof(sqrtNumProcs));
   worker_send_ack(tid);

   // Convert 1-D rank to 2-D rank
   i = tid / sqrtNumProcs;
   j = tid % sqrtNumProcs;

   upProc = (i == 0)?(sqrtNumProcs - 1):(i - 1);
   upProc = (upProc * sqrtNumProcs) + j;

   downProc = (i == (sqrtNumProcs - 1))?0:(i + 1);
   downProc = (downProc * sqrtNumProcs) + j;

   rightProc = (j == (sqrtNumProcs - 1))?0:(j + 1);
   rightProc = (i * sqrtNumProcs) + rightProc;

   leftProc = (j == 0)?(sqrtNumProcs - 1):(j - 1);
   leftProc = (i * sqrtNumProcs) + leftProc;

   ai = i * blockSize;
   aj = ((i + j) < sqrtNumProcs)?(i + j):(i + j - sqrtNumProcs);
   aj = aj * blockSize;

   bi = ((i + j) < sqrtNumProcs)?(i + j):(i + j - sqrtNumProcs);
   bi = bi * blockSize;
   bj = j * blockSize;

   // populate aBlock
   aBlock = (float**)malloc(blockSize*sizeof(float*));
   for (int x = 0; x < blockSize; x++)
   {
      aBlock[x] = (float*)malloc(blockSize*sizeof(float));
      CAPI_message_receive_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)aBlock[x], blockSize * sizeof(float));
      worker_send_ack(tid);
   }

   assert(aBlock[0][0] == 2.0);

   // populate bBlock
   bBlock = (float**)malloc(blockSize*sizeof(float*));
   for (int x = 0; x < blockSize; x++)
   {
      bBlock[x] = (float*)malloc(blockSize*sizeof(float));
      CAPI_message_receive_w((CAPI_endpoint_t)num_threads, (CAPI_endpoint_t)tid, (char *)bBlock[x], blockSize * sizeof(float));
      worker_send_ack(tid);
   }

   assert(bBlock[0][0] == 3.0);

   // Allocate cBlock
   cBlock = (float**)malloc(blockSize*sizeof(float*));
   for (int x = 0; x < blockSize; x++)
      cBlock[x] = (float*)malloc(blockSize*sizeof(float));


   // Initialize cBlock
   cBlock = (float**)malloc(blockSize*sizeof(float*));
   for (int x = 0; x < blockSize; x++)
   {
      cBlock[x] = (float*)malloc(blockSize*sizeof(float));
      for (int y = 0; y < blockSize; y++) cBlock[x][y] = 0;
   }

   fprintf(stderr, "Thread %d processing...\n", tid);

   for (int iter = 0; iter < sqrtNumProcs; iter++) // for loop begins
   {
      for (int x = 0; x < blockSize; x++)
         for (int y = 0; y < blockSize; y++)
            for (int z = 0; z < blockSize; z++) cBlock[x][y] += aBlock[x][z] * bBlock[z][y];

      if (iter < sqrtNumProcs - 1) // if block begins
      {
         // Send aBlock left

         for (int x = 0; x < blockSize; x++)
            for (int y = 0; y < blockSize; y++)
            {
               debug_printf("tid # %d sending to tid # %d\n", tid, leftProc);
               CAPI_message_send_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)leftProc, (char*)&aBlock[x][y], sizeof(float));
            }

         // Send bBlock up
         for (int x= 0; x < blockSize; x++)
            for (int y = 0; y < blockSize; y++)
            {
               debug_printf("tid # %d sending to tid # %d\n", tid, upProc);
               CAPI_message_send_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)upProc, (char*)&bBlock[x][y], sizeof(float));
            }


         // Receive aBlock from right
         for (int x = 0; x < blockSize; x++)
            for (int y = 0; y < blockSize; y++)
            {
               debug_printf("tid # %d receiving from tid # %d\n", tid, rightProc);
               CAPI_message_receive_w((CAPI_endpoint_t)rightProc, (CAPI_endpoint_t)tid, (char*)&aBlock[x][y], sizeof(float));
            }

         // Receive bBlock from below
         for (int x = 0; x < blockSize; x++)
            for (int y = 0; y < blockSize; y++)
            {
               debug_printf("tid # %d receiving from tid # %d\n", tid, downProc);
               CAPI_message_receive_w((CAPI_endpoint_t)downProc, (CAPI_endpoint_t)tid, (char*)&bBlock[x][y], sizeof(float));
            }

      } // if block ends
   } // for loop ends

   debug_printf("tid # %d waiting to send...\n", tid);

   // Update c
   worker_wait_go(tid);
   for (int x = 0; x < blockSize; x++)
   {
      CAPI_message_send_w((CAPI_endpoint_t)tid, (CAPI_endpoint_t)num_threads, (char *)&(cBlock[0][0]), blockSize * sizeof(float));
      worker_wait_go(tid);
   }

   //Free Scratch Memory

   for (unsigned int i = 0; i < blockSize; i++)
   {
      free(aBlock[i]);
      free(bBlock[i]);
      free(cBlock[i]);
   }

   free(aBlock);
   free(bBlock);
   free(cBlock);

   debug_printf("tid # %d done!\n", tid);

   return NULL;
}
