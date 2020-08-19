#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "util.h"

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 100
#define MAX_REQUEST_LENGTH 1024

static pthread_t *dispatcher_threads;
static pthread_t *worker_threads;
static pthread_mutex_t queue_access = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_access = PTHREAD_MUTEX_INITIALIZER;

static int queue_length;
static int in = 0;
static int out = 0;

static sem_t worker_slots;
static sem_t dispatcher_slots;

FILE *server_log;

//Structure for a single request.
typedef struct request {
        int     m_socket;
        char    m_szRequest[MAX_REQUEST_LENGTH];
} request_t;

static request_t *queue; // need to be lock every time modify

void * dispatch(void * arg) {
    request_t request;
    while(1){
        sem_wait(&dispatcher_slots); //Initialize Semaphore in main(). and Check if there are any request in the Q
        //Initialize accept_connection and get_request and check or errors
        if ((request.m_socket = accept_connection()) < 0){
            fprintf(stderr, "Error\n");
            exit(-1);
        }

        if (!get_request(request.m_socket, request.m_szRequest)) {
        	// Grab the lock and put request in the Q for the worker
            pthread_mutex_lock(&queue_access);
            queue[in] = request;
            in = (in + 1) % queue_length;
            pthread_mutex_unlock(&queue_access); // release the lock after
            sem_post(&worker_slots);
        }
        else {
            fprintf(stderr, "get_request error. Try again.\n");
        }
    }
    return NULL;
}

void * worker(void * arg) {
    int pid = (long) arg;
    int req_num = 0;
    int req_fd;
    char* content_type;
    char* ext;
    char* req_string;
    int num_bytes;
    int fd;
    struct stat st;
    char* buf;
    request_t req;

    while (1) {
        sem_wait(&worker_slots);
        pthread_mutex_lock(&queue_access);
        req = queue[out];
        out = (out + 1) % queue_length;
        pthread_mutex_unlock(&queue_access);
        sem_post(&dispatcher_slots);

        req_string = req.m_szRequest + 1;
        req_fd = req.m_socket;
        fd = open(req_string, O_RDONLY);

        if (fd == -1) {
            if (return_error(req_fd, "File not found") != 0) {
                fprintf(stderr, "Error returning error: File not found\n");
                exit(-1);
            }
            pthread_mutex_lock(&log_access);
            fprintf(server_log, "[%d][%d][%d][%s][File not found.]\n", pid, req_num, req_fd, req.m_szRequest);
            fflush(server_log);
            pthread_mutex_unlock(&log_access);

            // Go to the next iteration. Don't process this request any further.
            continue;
        }

        if (fstat(fd, &st) < 0) {
            perror("Error getting file metadata");
            exit(-1);
        }
        num_bytes = st.st_size;
        buf = (char *) malloc(num_bytes);
        if (!buf) {
            perror("Error allocating buffer");
            exit(-1);
        }

        ext = strrchr(req_string, '.');
        if (ext == NULL) {
            content_type = "text/plain";
        } else if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
            content_type = "text/html";
        } else if (strcmp(ext,".jpg") == 0) {
            content_type = "image/jpeg";
        } else if (strcmp(ext, ".gif") == 0) {
            content_type = "image/gif";
        } else {
            content_type = "text/plain";
        }

        if (read(fd, buf, num_bytes) < 0) {
            perror("Error reading file");
            exit(-1);
        }

        ++req_num;
        if (return_result(req_fd, content_type, buf, num_bytes) != 0) {
            fprintf(stderr,"Error returning result\n");
            exit(-1);
        }
        close(fd);
        free(buf);
        pthread_mutex_lock(&log_access);
        fprintf(server_log, "[%d][%d][%d][%s][%d]\n", pid, req_num, req_fd, req.m_szRequest, num_bytes);
        fflush(server_log);
        pthread_mutex_unlock(&log_access);
    }
    return NULL;
}


int main(int argc, char **argv) {
    //Error check first.
    if(argc != 6 && argc != 7) {
        printf("usage: %s port path num_dispatchers num_workers queue_length [cache_size]\n", argv[0]);
        return -1;
    }

    // Log file
    server_log = fopen("./web_server_log", "w+");
    if (server_log == NULL) {
        perror("Error opening log file");
        exit(-1);
    }

    // Parse command line arguments

    int port = atoi(argv[1]);

    if (chdir(argv[2])) {
        perror("Couldn't change directory");
        exit(-1);
    }

    int num_dispatchers = fmin(atoi(argv[3]), MAX_THREADS);
    int num_workers     = fmin(atoi(argv[4]), MAX_THREADS);
    dispatcher_threads  = (pthread_t *) malloc(num_dispatchers * sizeof(pthread_t));
    if (!dispatcher_threads) {
        perror("Couldn't allocate dispatcher threads");
        exit(-1);
    }
    worker_threads = (pthread_t *) malloc(num_workers * sizeof(pthread_t));
    if (!worker_threads) {
        perror("Couldn't allocate worker threads");
        exit(-1);
    }

    queue_length = fmin(atoi(argv[5]), MAX_QUEUE_SIZE);
    queue = (request_t *) malloc(queue_length * sizeof(request_t));
    if (!queue) {
        perror("Couldn't allocate request queue");
        exit(-1);
    }

    sem_init (&worker_slots, 0, 0);
    sem_init (&dispatcher_slots, 0, queue_length);


    printf("Starting server on port %d: %d disp, %d work\n", port, num_dispatchers, num_workers);

    init(port);

    // Create and launch threads
    long i;

    for (i = 0; i < num_dispatchers; i++) {
        if (pthread_create(&dispatcher_threads[i], NULL, dispatch, NULL)) {
            fprintf(stderr, "Failed creating dispatcher thread %ld\n", i);
            exit(1);
        }
    }

    for (i = 0; i < num_workers; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker, (void *) i)) {
            fprintf(stderr, "Failed creating worker thread %ld\n", i);
            exit(1);
        }
    }

    // Wait for all threads
    for (i = 0; i < num_dispatchers; i++) {
        if (pthread_join(dispatcher_threads[i], NULL)) {
            fprintf(stderr, "Error waiting for dispatcher thread %ld\n", i);
        }
    }

    for (i = 0; i < num_workers; i++) {
        if (pthread_join(worker_threads[i], NULL)) {
            fprintf(stderr, "Error waiting for worker thread %ld\n", i);
        }
    }

    // Clean up resources
    fclose(server_log);
    free(dispatcher_threads);
    free(worker_threads);
    free(queue);
    pthread_mutex_destroy(&queue_access);
    pthread_mutex_destroy(&log_access);
    return 0;
}
