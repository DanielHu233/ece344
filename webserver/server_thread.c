#include "request.h"
#include "server_thread.h"
#include "common.h"

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
        //the request buffer
        int* buffer;
        //the pool of worker threads
        pthread_t* t_pool;
       
};

pthread_mutex_t lock;
pthread_cond_t full;
pthread_cond_t empty;
int in;
int out;

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* read file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	ret = request_readfile(rq);
	if (ret == 0) { /* couldn't read file */
		goto out;
	}
	/* send file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	file_data_free(data);
}

/* entry point functions */
//the worker thread start routine that consumes request by reading network descriptor
void consumer_thread(struct server* sv){
    while(!sv->exiting){
        pthread_mutex_lock(&lock);
        
        //the empty check for consumer
        while(in == out){
            //wait for wakeup on empty
            pthread_cond_wait(&empty, &lock);
            
            //if need to exit, need to have a check here!!!!
            if(sv->exiting){ 
                pthread_mutex_unlock(&lock);
                return;
                //pthread_exit(0);
            }          
        }
        //now can read
        int element = sv->buffer[out];
        
        //now wake up threads sleeping on full
        //this condition check is used to see whether a signal is required. to speed up
        if((in - out + sv->max_requests)%sv->max_requests == sv->max_requests -1){
            pthread_cond_signal(&full);
        }
        
        out = (out+1)%sv->max_requests;
        
        pthread_mutex_unlock(&lock);
        do_server_request(sv, element);
    }
}

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;
        //critical section below
        pthread_mutex_lock(&lock);

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
        
        sv->buffer = NULL;
        sv->t_pool = NULL;
        in = 0;
        out = 0;
        //initialize locks and cvs
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&full, NULL);
        pthread_cond_init(&empty, NULL);
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
            /* Lab 4: create queue of max_request size when max_requests > 0 */
            if(max_requests > 0){
                sv->buffer = (int*)malloc(sizeof(int) * max_requests);
            }            
            /* Lab 4: create worker threads when nr_threads > 0 */
            if(nr_threads > 0){
                sv->t_pool = (pthread_t*)malloc(sizeof(pthread_t) * nr_threads);
            }
            for(int i = 0;i < nr_threads;i++){
                //create pthreads with start routine consumer_thread
                pthread_create(&(sv->t_pool[i]), NULL, (void*)&consumer_thread, sv);
            }
	}
        
        pthread_mutex_unlock(&lock);
        
	/* Lab 5: init server cache and limit its size to max_cache_size */

	return sv;
}

//produces request by saving into shared buffer
void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(&lock);
                
                //the full check for producer
                while(((in - out + sv->max_requests)%sv->max_requests) == (sv->max_requests -1)){
                    //wait for the full cv
                    pthread_cond_wait(&full, &lock);
                }
                //now able to write
                sv->buffer[in] = connfd;
                
                //now wake up sleeping threads on empty
                //to see whether broadcast is needed
                if(in == out){
                    pthread_cond_broadcast(&empty);
                }
                
                in = (in + 1)%sv->max_requests;
                
                pthread_mutex_unlock(&lock);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
        
        pthread_cond_broadcast(&empty);
        pthread_cond_broadcast(&full);
        
        //main server thread wait for all workers
        for(int i = 0;i < sv->nr_threads;i++){
            pthread_join((pthread_t)sv->t_pool[i], NULL);
        }

	/* make sure to free any allocated resources */
        free(sv->t_pool);
        free(sv->buffer);
	free(sv);
}
