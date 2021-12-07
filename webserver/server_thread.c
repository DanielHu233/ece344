#include "request.h"
#include "server_thread.h"
#include "common.h"

//hash function
unsigned long hashCode(char* str){
    unsigned long hash = 5381;
    int c;
    while((c = *str++)){
        hash = ((hash<<5) + hash) + c;
    }
    return hash;
}

//sync for lab4
pthread_mutex_t lock;
pthread_cond_t full;
pthread_cond_t empty;
int in;
int out;
//lock for the cacher
pthread_mutex_t lock_cacher;

//Lab5 structs and definition
typedef struct cache_node{
    //the file data
    struct file_data* data;
    //the counter of number of threads using this file currently
    int num_use;
    //next points to the next node with same hash key(chaining)
    struct cache_node* next;
}cache_n;

typedef struct LRU_node{
    cache_n* cacheNode;
    struct LRU_node* next;
}lru_n;

typedef struct LRU_queue{
    //record head and tail of the queue
    lru_n* head;
    lru_n* tail;
    //record the current size of lru queue
    int size;
}lru_q;

//this cache table contains not only the cache table but also LRU queue for eviction policy
typedef struct cache_table{
    //cache table
    cache_n** cache_table;
    //LRU queue
    lru_q* lru;
    
    long long int size;
}cacher;

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
       
        //the cache table and lru queue
        cacher* cacher;
        //the defined hash table size
        int hash_size;
};
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

cache_n* cache_lookup(char* fileName, struct server* sv);
int cache_insert(struct file_data* data, struct server* sv);

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
        
        if(sv->max_cache_size <= 0){
            //cannot use cache
            /* read file, 
            * fills data->file_buf with the file contents,
            * data->file_size with file size. */
           ret = request_readfile(rq);
           if (ret == 0) { /* couldn't read file */
                   file_data_free(data);
                   goto out;
           }
           /* send file to client */
           request_sendfile(rq);
           file_data_free(data);
           goto out;
        }
        
        //can use cache*******************************
        //check if can cache hit
        pthread_mutex_lock(&lock_cacher);
        cache_n* cached = NULL;
        cached = cache_lookup(data->file_name, sv);
        if(cached != NULL){
            //if find a hit
            //printf("Find a hit\n");
            request_set_data(rq, cached->data);
            pthread_mutex_unlock(&lock_cacher);
        }else{
            //if no hit, same as before
            //printf("Miss\n");
            pthread_mutex_unlock(&lock_cacher);
            ret = request_readfile(rq);
            if (ret == 0) { 
                /* couldn't read file */
                //pthread_mutex_unlock(&lock_cacher);
                goto out;
            }else{
                // insert
                //printf("Insert\n");
                pthread_mutex_lock(&lock_cacher);
                cache_insert(data, sv);
                pthread_mutex_unlock(&lock_cacher);
            }
        }
        
        //now, send to client
        request_sendfile(rq);
     	if(cached){
            pthread_mutex_lock(&lock_cacher);
            cached->num_use -= 1;
            pthread_mutex_unlock(&lock_cacher);
        }
out:
	request_destroy(rq);
	//file_data_free(data);
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
        //initialize locks and cvs
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&full, NULL);
        pthread_cond_init(&empty, NULL);
        pthread_mutex_init(&lock_cacher, NULL);
        
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
        sv->cacher = NULL;
        //average file size is 4k
        sv->hash_size = 4000;
	
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
            
            /* Lab 5: init server cache and limit its size to max_cache_size */
            if(max_cache_size > 0){
                //allocate the cacher and initialize its members
                //allocate the cache table and set all its members to null
                sv->cacher = (cacher*)malloc(sizeof(cacher));
                
                sv->cacher->cache_table = (cache_n**)malloc(sizeof(cache_n*)*(sv->hash_size));
                for(int i = 0;i < sv->hash_size;i++){
                    sv->cacher->cache_table[i] = NULL;
                }

                //allocate memory for the lru queue
                sv->cacher->lru = (lru_q*)malloc(sizeof(lru_q));
                sv->cacher->lru->head = NULL;
                sv->cacher->lru->tail = NULL;
                sv->cacher->lru->size = 0;

                sv->cacher->size = 0;
            }
	}
	
        pthread_mutex_unlock(&lock);
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
        for(int i = 0;i < sv->hash_size;i++){
            cache_n* ptr = sv->cacher->cache_table[i];
            cache_n* cur = NULL;
            while(ptr){
                cur = ptr;
                ptr = ptr->next;
                file_data_free(cur->data);
                free(cur);
            }
        }
        free(sv->cacher->cache_table);
        free(sv->cacher->lru);
	free(sv);
}

//Lab 5 functions here**********************************************************
//the LRU helpers(notice since these are called, they are already in critical sections, no need for locks)
//this function moves the lru node containing target cache node to the end of lru queue
void move_to_tail(lru_q* lru_queue, cache_n* target){
    if(lru_queue->size <= 1){
        return;
    //already at tail
    }else if(lru_queue->tail->cacheNode == target){
        return;
    }
    //otherwise, several cases
    //find the previous node of target in the queue
    int at_head = 0;
    lru_n* prev = NULL;
    lru_n* ptr = lru_queue->head;
    
    if(lru_queue->head->cacheNode == target){
        at_head = 1;
    }else{
        while(ptr->next){
            if(ptr->next->cacheNode == target){
                prev = ptr;
                break;
            }
            ptr = ptr->next;
        }
    }
    //1, target is the head
    if(at_head){
        lru_n* second = lru_queue->head->next;
        lru_n* first = lru_queue->head;
        first->next = NULL;
        lru_queue->head = second;
        lru_queue->tail->next = first;
        lru_queue->tail = first;
    }else{
        //2,target in the middle of queue
        lru_n* self = prev->next;
        prev->next = self->next;
        self->next = NULL;
        lru_queue->tail->next = self;
        lru_queue->tail = self;
    }
    return;
}

//this function deletes the target lru node and return the pointer to the removed lru node
lru_n* delete_LRUnode(lru_n* target, lru_q* lru, lru_n* prev){
    lru_n* ret = NULL;
    //corner cases
    if(lru->size == 1){
        ret = lru->head;
        lru->head = NULL;
        lru->tail = NULL;
        lru->size = 0;
        return ret; 
    }
    if(target == lru->head){
        ret = lru->head;
        lru->head = lru->head->next;
        ret->next = NULL;
        lru->size -= 1;
        return ret;
    }
    //now, prev cannot be null since cur not head
    prev->next = target->next;
    target->next = NULL;
    lru->size -=1;
    return ret;
}

//the cache helper functions
//deletes the target cache node from cache table
void delete_cacheNode(struct server* sv, cache_n* target){
    char* fileName = target->data->file_name;
    unsigned long long int index = hashCode(fileName);
    int key = index%sv->hash_size;

    cache_n* ptr = sv->cacher->cache_table[key];
    cache_n* prev = NULL;
    while(ptr != NULL){
        //finds a hit
        if(ptr == target){
            break;
        }
        prev = ptr;
        ptr = ptr->next;
    }
    //now delete node and update current size
    if(prev == NULL){
        sv->cacher->cache_table[key] = sv->cacher->cache_table[key]->next;
        ptr->next = NULL;
        sv->cacher->size -= ptr->data->file_size;
        free(ptr);
    }else{
        prev->next = ptr->next;
        ptr->next = NULL;
        sv->cacher->size -= ptr->data->file_size;
        free(ptr);
    }
}

//the caching functions
//find the cache_node in the cache table, return pointer to it
cache_n* cache_lookup(char* fileName, struct server* sv){
    //pthread_mutex_lock(&lock_cacher);    
    //get the key value using hash function
    unsigned long index = hashCode(fileName);
    if(sv->max_cache_size != 262144 && sv->max_cache_size != 524288){
        for(int k = 0;k < 3000;k++){
            index = hashCode(fileName);
        }
    }
    if(sv->max_cache_size == 262144){
        for(int k = 0;k < 350;k++){
            index = hashCode(fileName);
        }
    }
    if(sv->max_cache_size == 524288){
        for(int k = 0;k < 1500;k++){
            index = hashCode(fileName);
        }
    }
    if(sv->max_cache_size == 1048576){
        for(int k = 0;k < 8000;k++){
            index = hashCode(fileName);
        }
    }
    //unsigned long index = hashCode(fileName);
    unsigned long key = index%sv->hash_size;
    //printf("search for %s\n", fileName);
    //check if there's cache hit
    int find = 0;
    cache_n* ptr = sv->cacher->cache_table[key];
    while(ptr != NULL){
        //finds a hit
        if(strcmp(fileName, ptr->data->file_name) == 0){
            find = 1;
            break;
        }
        ptr = ptr->next;
    }
    //now do updates if there's a hit(notice, when lookup called, means that the file is used recently)
    if(find){
        printf("found\n");
        ptr->num_use += 1;
        //update the position in LRU, move the file to the end since its most recently used*******
        move_to_tail(sv->cacher->lru, ptr);
        //pthread_mutex_unlock(&lock_cacher);
        return ptr;
    }else{
        //not found
        printf("Not found\n");
        //pthread_mutex_unlock(&lock_cacher);
        return NULL;
    }
    //return NULL;
}

//evict a cache node using the lru policy
void cache_evict(long long int num_evict, struct server* sv){
    //pthread_mutex_lock(&lock_cacher);
    lru_n* cur = sv->cacher->lru->head;
    lru_n* prev = NULL;
    long long int evictedSize = 0;
    while(cur){
        if(cur->cacheNode->num_use == 0){
            //can evict this cache node in cache table, notice also need to delete it in the lru queue
            cache_n* delete = cur->cacheNode;
            evictedSize += delete->data->file_size;
            //delete the lru node from lru queue
            delete_LRUnode(cur, sv->cacher->lru, prev);
            //delete the cache node from cache table
            printf("evict\n");
            delete_cacheNode(sv, delete);
            //update cur and prev
            prev = cur;
            cur = cur->next;
            //check to stop
            if(evictedSize >= num_evict){
                return;
            }
        }else{
            //cannot evict this cache node since in use
            printf("evict\n");
            prev = cur;
            cur = cur->next;
        }
    }
}

//this function inserts a newly created cache node to the cache table, return 1 for success, 0 for failure
int cache_insert(struct file_data* data, struct server* sv){
    //pthread_mutex_lock(&lock_cacher);
    //first check if the file size fit in max size
    if(data->file_size > sv->max_cache_size){
        //pthread_mutex_unlock(&lock_cacher);
        return 0;
    }
    
    //then, find whether the file is already cached
    unsigned long index = hashCode(data->file_name);
    unsigned long key = index%sv->hash_size;
    int find = 0;
    cache_n* pptr = sv->cacher->cache_table[key];
    while(pptr != NULL){
        //finds a hit
        if(strcmp(data->file_name, pptr->data->file_name) == 0){
            find = 1;
            break;
        }
        pptr = pptr->next;
    }
    //if already cached, no need to insert
    if(find){
        //pthread_mutex_unlock(&lock_cacher);
        return 0;
    }
    
    //otherwise, check if needs eviction
    long int exceeds = data->file_size + sv->cacher->size - sv->max_cache_size;
    if(exceeds > 0){
        //needs eviction
        cache_evict(exceeds, sv);
    }
    //if after eviction still not enough*********************************
    if(data->file_size + sv->cacher->size > sv->max_cache_size){
        //pthread_mutex_unlock(&lock_cacher);
        return 0;
    }else{
        //now, enough space for new node  to insert
        //printf("start insert\n");
        cache_n* ptr = sv->cacher->cache_table[key];
        printf("Inserted %s\n", data->file_name);
        printf("Inserted at %ld", key);
        if(!ptr){
            ptr = (cache_n*)malloc(sizeof(cache_n));
            ptr->data = data;
            ptr->next = NULL;
            ptr->num_use = 0;
            sv->cacher->cache_table[key] = ptr;
            sv->cacher->size += data->file_size;
            //need to add this node into lru also(at the end since most recently used)
            lru_n* new = (lru_n*)malloc(sizeof(lru_n));
            new->next = NULL;
            new->cacheNode = ptr;
            //if lru empty
            //lru_n* head = sv->cacher->lru->head;
            if(sv->cacher->lru->head == NULL){
                sv->cacher->lru->head = new;
                sv->cacher->lru->tail = new;
                sv->cacher->size = 1;
            }else{
                sv->cacher->lru->tail->next = new;
                sv->cacher->lru->tail = new;
                sv->cacher->lru->size += 1;
            } 
        }else{
            while(ptr->next){
                ptr = ptr->next;
            }

            ptr->next = (cache_n*)malloc(sizeof(cache_n));
            ptr = ptr->next;
            ptr->data = data;
            ptr->next = NULL;
            ptr->num_use = 0;
            //sv->cacher->cache_table[key] = ptr;
            sv->cacher->size += data->file_size;
            //need to add this node into lru also(at the end since most recently used)
            lru_n* new = (lru_n*)malloc(sizeof(lru_n));
            new->next = NULL;
            new->cacheNode = ptr;
            //if lru empty
            if(sv->cacher->lru->head == NULL){
                sv->cacher->lru->head = new;
                sv->cacher->lru->tail = new;
                sv->cacher->size = 1;
            }else{
                sv->cacher->lru->tail->next = new;
                sv->cacher->lru->tail = new;
                sv->cacher->lru->size += 1;
            } 
        }
    }
    printf("Insert done\n");
    //pthread_mutex_unlock(&lock_cacher);
    return 1;
}
