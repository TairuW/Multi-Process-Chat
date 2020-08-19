#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include "util.h"
#include <stdbool.h>

#define MAX_THREADS 100
#define MAX_queue_len 100
#define MAX_CE 100
#define INVALID -1
#define BUFF_SIZE 1024
/******************************-by olivia*/

#define MAX_CACHE_SIZE 100
#define MAX_REQUEST_LENGTH 64

static pthread_mutex_t buffer_access = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t buffer_full;
static pthread_cond_t buffer_empty;
int port_num;
int num_dispatchers;
int num_workers;
int size_cache = 0;
int queue_length;

FILE * logfile;  //fd for web_server_log

/***************************************/

// structs:

/************************************by olivia*/
typedef struct request {
   int fd;
   char request_size[MAX_REQUEST_LENGTH];
} request_t;

typedef struct cache_entry {
    int len;
    char request[MAX_REQUEST_LENGTH];
    char data [BUFF_SIZE];
    char *content;
    int numbytes;
} cache_entry_t;

request_t* req_queue[MAX_queue_len];
cache_entry_t* cache_entry[MAX_CACHE_SIZE];
int queue_head = 0;
int queue_tail = 0;
int num_req = 0;

/* ************************ Dynamic Pool Code ***********************************/
// Extra Credit: This function implements the policy to change the worker thread pool dynamically
// depending on the number of requests
void * dynamic_pool_size_update(void *arg) {
  while(1) {
    // Run at regular intervals
    // Increase / decrease dynamically based on your policy
  }
}
/**********************************************************************************/

/* ************************************ Cache Code ********************************/

// Function to check whether the given request is present in cache
int getCacheIndex(char *request){
  for (int i = 0; i < size_cache; i ++){
    if (strcmp(request,cache_entry[i]->request)==0){
      return i;
    }
  }
  return -1;  /// return the index if the request is present in the cache
}

// Function to add the request and its file content into the cache
void addIntoCache(char *mybuf, char *memory , int memory_size){
  // It should add the request at an index according to the cache replacement policy
  // Make sure to allocate/free memeory when adding or replacing cache entries
}

// clear the memory allocated to the cache
void deleteCache(){
  for (int i = 0; i < size_cache; i ++){
    pthread_mutex_lock(&buffer_access);
    free(cache_entry[i]);
    pthread_mutex_unlock(&buffer_access);
  }
  // De-allocate/free the cache memory
}

// Function to initialize the cache
void initCache(){
  // Allocating memory and initializing the cache array
 cache_entry_t* cache_array = (cache_entry_t*)malloc(sizeof(cache_entry_t));
 for (i = 0; i < size_cache; i++)
   cache_entry[i] = NULL;
}

// Function to open and read the file from the disk into the memory
// Add necessary arguments as needed
// need the file name to read from disk
int readFromDisk(char *request) {
  for (int i = 0; i < size_cache; i ++){
    if (strcmp(request,cache_entry[i]->request)==0){
      return i;
    }
  }
  return -1;
  // Open and read the contents of file given the request
}

/**********************************************************************************/

/* ************************************ Utilities ********************************/
// Function to get the content type from the request
char* getContentType(char * mybuf) {
  // Should return the content type based on the file type in the request
  // (See Section 5 in Project description for more details)
  char* content_type;
  char* dot;
  // find the '.' in the string then find the content type
  dot = strrchr(mybuf,'.');
  switch(dot){
    case NULL:
      content_type = "text/plain";
      break;
    case ".html":
    content_type = "text/html";
      break;
    case ".htm":
    content_type = "text/html";
      break;
    case ".jpg":
      content_type = "image/gif";
      break;
    case ".gif":
      content_type = "image/gif";
      break;
    default:
      content_type = "text/plain";
  }
  return content_type;
}

// This function returns the current time in milliseconds
int getCurrentTimeInMills() {
  struct timeval curr_time;
  gettimeofday(&curr_time, NULL);
  return curr_time.tv_usec;
}

/**********************************************************************************/

// Function to receive the request from the client and add to the queue
void * dispatch(void *arg) {
  request_t* req;
  int fd;
  char filename[MAX_REQUEST_LENGTH];

  while (1) {
    // Accept client connection
    if((fd=accept_connection())<0){
      perror("accept_connection fails");
      continue;
    }
    pthread_mutex_lock(&buffer_access);
    // Get request from the client
    if(get_request(fd,filename)!=0){
      pthread_mutex_unlock(&buffer_access);
    }
    // Add the request into the queue
    req = (request_t*) malloc(sizeof(request_t));
    req->fd=fd;
    strncpy(req->request, filename, MAX_REQUEST_LENGTH);
    req_queue[index]=req;
    index=(index+1)%queue_length;
  }
  return NULL;
}

/**********************************************************************************/

// Function to retrieve the request from the queue, process it and then return a result to the client
void * worker(void *arg) {
  while (1) {

    // Start recording time
    int start_time = getCurrentTimeInMills();
    // Get the request from the queue

    // Get the data from the disk or the cache

    // Stop recording the time
    int end_time = getCurrentTimeInMills();
    int request_time = end_time - start_time;
    // Log the request into the file and terminal

    // return the result
  }
  return NULL;
}

/**********************************************************************************/

int main(int argc, char **argv) {

  // Error check on number of arguments
  if(argc != 8){
    printf("usage: %s port path num_dispatcher num_workers dynamic_flag queue_length cache_size\n", argv[0]);
    return -1;
  }
  port_num = atoi(argv[1]);
  num_dispatchers = atoi(argv[3]);
  if(num_dispatchers <= 0 || num_dispatchers > MAX_THREADS){
    printf("Please enter a valid num_dispatcher (1-%d)\n", MAX_THREADS);
    return 1;
  }
  num_workers = atoi(argv[4]);
  if (num_workers <= 0 || num_workers > MAX_THREADS) {
    printf("Please enter a valid num_workers (1-%d)\n", MAX_THREADS);
    return 1;
  }
  queue_length = fmin(atoi(argv[6]), MAX_QUEUE_SIZE);
  if (queue_length > MAX_QUEUE_SIZE) {
    printf("Please enter a valid queue_length (1-%d)\n", MAX_QUEUE_SIZE);
    return 1;
  }
  size_cache = atoi(argv[7]);
  if (size_cache > MAX_CACHE_SIZE) {
    printf("Please enter a valid cache size (1-%d)\n", MAX_CACHE_SIZE);
    return 1;
  }
  else {
    size_cache = 0;
  }
  queue = (request_t *) malloc(queue_length * sizeof(request_t));
  // Get the input args

  // Perform error checks on the input arguments

  // Change the current working directory to server root directory

  // Start the server and initialize cache

  // Create dispatcher and worker threads

  // Clean up
  return 0;
}
