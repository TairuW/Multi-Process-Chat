#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "util.h"

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 100
#define MAX_REQUEST_LENGTH 64
#define MAX_PATH_LEN 2048
#define MAX_RTN_LEN 128
#define MAX_BUF_SIZE 524288
#define MAX_CACHE_SIZE 100

char HTML_STR[MAX_RTN_LEN] = "text/html";
char TXT_STR[MAX_RTN_LEN] = "text/plain";
char GIF_STR[MAX_RTN_LEN] = "image/gif";
char JPG_STR[MAX_RTN_LEN] = "image/jpeg";

char ERROR_FILE[MAX_RTN_LEN] = "File not found.";
char ERROR_FBD[MAX_RTN_LEN] = "Permission Denied.";
char ERROR_UNK[MAX_RTN_LEN] = "Unknown error.";
char ERROR_LEN[MAX_RTN_LEN] = "Request too long.";
char NO_ERROR[MAX_RTN_LEN] = "";

static pthread_mutex_t buffer_access = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t buffer_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t buffer_empty = PTHREAD_COND_INITIALIZER;

int dispatch_completed;

int port_num;
char path_prefix[1024];
int prefix_len;
int num_dispatcher;
int num_workers;
int queue_length;
int size_cache = 0;

FILE * log_file;

//Structure for queue.
typedef struct request_queue
{
  int m_socket;
  char m_szRequest[MAX_REQUEST_LENGTH];
  struct timeval time_s;
} request_queue_t;

typedef struct buffer_cache
{
  int m_usage;
  char m_request[MAX_REQUEST_LENGTH];
  char m_data[MAX_BUF_SIZE];
  int m_data_size;
  char* m_return_type;
  char* m_error_type;
} buffer_cache_t;

request_queue_t* queue_req[MAX_QUEUE_SIZE];
buffer_cache_t* buffer_pool[MAX_CACHE_SIZE];
int queue_head = 0;
int queue_tail = 0;
int num_req = 0;


double comp_time(struct timeval time_s, struct timeval time_e) {
  double elap = 0.0;
  if (time_e.tv_sec > time_s.tv_sec) {
    elap += (time_e.tv_sec - time_s.tv_sec - 1) * 1000000.0;
    elap += time_e.tv_usec + (1000000 - time_s.tv_usec);
  }
  else {
    elap = time_e.tv_usec - time_s.tv_usec;
  }
  return elap;
}

void log_request(int thread_id, int num_req, request_queue_t* req_packet, int bytes_returned, char * err, int hit_or_miss) {
  struct timeval time_e;
  gettimeofday(&time_e, NULL);

  if (bytes_returned >= 0) {
    fprintf(log_file, "[%d][%d][%d][%s][%d][%.0fus][%s]\n",
            thread_id,
            num_req,
            req_packet->m_socket,
            req_packet->m_szRequest,
            bytes_returned,
            comp_time(req_packet->time_s, time_e),
            (size_cache ? (hit_or_miss ? "HIT" : "MISS") : "N/A")
           );
  } else {
    fprintf(log_file, "[%d][%d][%d][%s][%s][%.0fus][%s]\n",
            thread_id,
            num_req,
            req_packet->m_socket,
            req_packet->m_szRequest,
            err,
            comp_time(req_packet->time_s, time_e),
            (size_cache ? (hit_or_miss ? "HIT" : "MISS") : "N/A")
           );
  }
  fflush(log_file);
}

void * dispatch(void * arg)
{
  struct timeval time_s;
  int fd;
  char filename[1024];
  request_queue_t* req_packet;
  while (1) {
    if ((fd = accept_connection()) < 0) {
      pthread_exit(NULL);
    }
    pthread_mutex_lock(&buffer_access);
    gettimeofday (&time_s, NULL);
    if (get_request(fd, filename) != 0) {
      pthread_mutex_unlock(&buffer_access);
      continue;
    }
    while (queue_tail - queue_head >= queue_length - 1) {
      // pthread_yield();
      pthread_cond_wait(&buffer_empty, &buffer_access);
    }

    req_packet = (request_queue_t*) malloc(sizeof(request_queue_t));
    req_packet->m_socket = fd;
    strncpy(req_packet->m_szRequest, filename, MAX_REQUEST_LENGTH);
    if (strlen(filename) >= MAX_REQUEST_LENGTH) {
      req_packet->m_szRequest[MAX_REQUEST_LENGTH - 1] = 30;
    }
    req_packet->time_s = time_s;
    queue_req[queue_tail % queue_length] = req_packet;
    // printf("Tail: %d\n", queue_tail);
    queue_tail++;
    pthread_cond_signal(&buffer_full);
    pthread_mutex_unlock(&buffer_access);
  }
  return NULL;
}

void * worker(void * arg)
{
  int i, found_cache;
  struct stat stat_file_att;
  int filed;
  int size_file_in_byte, nread;
  int len_full_path;
  char full_path[MAX_PATH_LEN];
  char buf[MAX_BUF_SIZE];
  char* return_type_file;
  char* return_error_num;
  buffer_cache_t* cache_packet;
  request_queue_t* req_packet;

  int t_n_id = *((int *) arg);
  queue_head = queue_tail = 0;
  while (1) {
    size_file_in_byte = 0;
    return_error_num = NO_ERROR;
    pthread_mutex_lock(&buffer_access);
    while (queue_head == queue_tail) {
      if (dispatch_completed == 1) {
        pthread_mutex_unlock(&buffer_access);
        pthread_exit(NULL);
      }
      // pthread_yield();
      pthread_cond_wait(&buffer_full, &buffer_access);
    }

    num_req++;
    req_packet = queue_req[queue_head % queue_length];
    // printf("Head: %d\n", queue_head);
    queue_head++;
    if (req_packet->m_szRequest[MAX_REQUEST_LENGTH - 1] != 30) {
      strncpy(full_path, path_prefix, prefix_len);
      strncpy(full_path + prefix_len,
              req_packet->m_szRequest,
              MAX_REQUEST_LENGTH);

      found_cache = 0;
      for (i = 0; i < size_cache && buffer_pool[i] != NULL; i++) {
        if (strcmp(req_packet->m_szRequest, buffer_pool[i]->m_request) == 0) {
          found_cache = 1;
          if (buffer_pool[i]->m_data_size >= 0) {
            return_result(req_packet->m_socket,
                          buffer_pool[i]->m_return_type,
                          buffer_pool[i]->m_data,
                          buffer_pool[i]->m_data_size
                          );
          } else {
            return_error(req_packet->m_socket, buffer_pool[i]->m_error_type);
          }
          log_request(t_n_id,
                      num_req,
                      req_packet,
                      buffer_pool[i]->m_data_size,
                      buffer_pool[i]->m_error_type,
                      1
                      );
          buffer_pool[i]->m_usage++;
          break;
        }
      }
      if (found_cache) {
        free(req_packet);
        pthread_cond_signal(&buffer_empty);
        pthread_mutex_unlock(&buffer_access);
        continue;
      }

      if ((filed = open(full_path, O_RDONLY)) != -1) {
        len_full_path = strlen(full_path);

        if (len_full_path > 5 &&
            strcmp(full_path + len_full_path - 5, ".html") == 0) {
          return_type_file = HTML_STR;
        } else if (len_full_path > 4 &&
                   strcmp(full_path + len_full_path - 4, ".gif") == 0) {
          return_type_file = GIF_STR;
        } else if (len_full_path > 4 &&
                   strcmp(full_path + len_full_path - 4, ".jpg") == 0) {
          return_type_file = JPG_STR;
        } else {
          return_type_file = TXT_STR;
        }
        // start to read files
        stat(full_path, &stat_file_att);
        size_file_in_byte = stat_file_att.st_size;
        nread = read(filed, buf, size_file_in_byte);
        return_result(req_packet->m_socket,
                      return_type_file,
                      buf,
                      size_file_in_byte);
        log_request(t_n_id, num_req, req_packet, size_file_in_byte, "", 0);
        close(filed);
      } else {
        // have difficulty to reach the file
        if (errno == ENOENT) {
          return_error(req_packet->m_socket, ERROR_FILE);
          return_error_num = ERROR_FILE;
          log_request(t_n_id, num_req, req_packet, -1, ERROR_FILE, 0);
        } else if (errno == EACCES) {
          return_error(req_packet->m_socket, ERROR_FBD);
          return_error_num = ERROR_FBD;
          log_request(t_n_id, num_req, req_packet, -1, ERROR_FBD, 0);
        } else {
          return_error(req_packet->m_socket, ERROR_UNK);
          return_error_num = ERROR_UNK;
          log_request(t_n_id, num_req, req_packet, -1, ERROR_UNK, 0);
        }
      }

      if (size_cache != 0) {
        cache_packet = (buffer_cache_t *) malloc(sizeof(buffer_cache_t));
        cache_packet->m_usage = 1;
        strncpy(cache_packet->m_request,
                req_packet->m_szRequest,
                MAX_REQUEST_LENGTH);
        memcpy(cache_packet->m_data, buf, size_file_in_byte);
        if (size_file_in_byte == 0)
          cache_packet->m_data_size = -1;
        else
          cache_packet->m_data_size = size_file_in_byte;
        cache_packet->m_return_type = return_type_file;
        cache_packet->m_error_type = return_error_num;
        if (i < size_cache && buffer_pool[i] == NULL) {
          buffer_pool[i] = cache_packet;
        } else {
          i = 0;
          while (1) {
            i = (i >= size_cache ? i - size_cache : i);
            buffer_pool[i]->m_usage--;
            if (buffer_pool[i]->m_usage <= 0) {
              free(buffer_pool[i]);
              buffer_pool[i] = cache_packet;
              break;
            }
            i++;
          }
        }
      }

    } else {
      // the request is too long
      return_error(req_packet->m_socket, ERROR_LEN);
      return_error_num = ERROR_LEN;
      req_packet->m_szRequest[MAX_REQUEST_LENGTH - 1] = 0;
      req_packet->m_szRequest[MAX_REQUEST_LENGTH - 2] = '.';
      req_packet->m_szRequest[MAX_REQUEST_LENGTH - 3] = '.';
      req_packet->m_szRequest[MAX_REQUEST_LENGTH - 4] = '.';
      log_request(t_n_id, num_req, req_packet, -1, ERROR_LEN, 0);
    }
    free(req_packet);
    pthread_cond_signal(&buffer_empty);
    pthread_mutex_unlock(&buffer_access);
  }
  return NULL;
}

void stop_server(int signo) {
  int i;
  fclose(log_file);
  exit(0);
}

int main(int argc, char **argv)
{
  int i;
  struct sigaction act;
  act.sa_handler = stop_server;
  sigfillset(&act.sa_mask);

  //Error check first.
  pthread_t t_dispathers[MAX_THREADS];
  pthread_t t_workers[MAX_THREADS];

  if(argc != 6 && argc != 7)
  {
    printf("usage: %s port path num_dispatcher num_workers queue_length [cache_size]\n", argv[0]);
    return 1;
  }

  port_num = atoi(argv[1]);
  strncpy(path_prefix, argv[2], 1024);
  prefix_len = strlen(path_prefix);
  num_dispatcher = atoi(argv[3]);
  if (num_dispatcher <= 0 || num_dispatcher > MAX_THREADS) {
    printf("Please enter a valid num_dispatcher (1-%d)\n", MAX_THREADS);
    return 1;
  }
  num_workers = atoi(argv[4]);
  if (num_workers <= 0 || num_workers > MAX_THREADS) {
    printf("Please enter a valid num_workers (1-%d)\n", MAX_THREADS);
    return 1;
  }
  queue_length = atoi(argv[5]);
  if (queue_length > MAX_QUEUE_SIZE) {
    printf("Please enter a valid queue_length (1-%d)\n", MAX_QUEUE_SIZE);
    return 1;
  }
  if (argc == 7) {
    size_cache = atoi(argv[6]);
    if (size_cache > MAX_CACHE_SIZE) {
      printf("Please enter a valid cache size (1-%d)\n", MAX_CACHE_SIZE);
      return 1;
    }
  } else {
    size_cache = 0;
  }
  for (i = 0; i < size_cache; i++)
    buffer_pool[i] = NULL;
  dispatch_completed = 0;

  log_file = fopen("web_server_log", "a");
  // make sure to close the file when server is quit
  sigaction(SIGINT, &act, NULL);

  init(port_num);

  for (i = 0; i < num_dispatcher; i++) {
    if (pthread_create(&t_dispathers[i], NULL, dispatch, NULL) != 0) {
      printf("Error creating dispatch thread\n");
      exit(1);
    }
  }
  for (i = 0; i < num_workers; i++) {
    if (pthread_create(&t_workers[i], NULL, worker, (void *) &i) != 0) {
      printf("Error creating worker thread\n");
      exit(1);
    }
  }

  for (i = 0; i < num_dispatcher; i++) {
    if (pthread_join(t_dispathers[i], NULL) != 0)
      printf("Error joining dispatch thread\n");
  }
  dispatch_completed = 1;
  for (i = 0; i < num_workers; i++)
    pthread_cond_signal(&buffer_full);
  fprintf(stderr, "All dispatch exit\n");
  for (i = 0; i < num_workers; i++) {
    if (pthread_join(t_workers[i], NULL) != 0)
      printf("Error joining worker thread\n");
  }
  fclose(log_file);
  return 0;
}
