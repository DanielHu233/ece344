#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

/* This is the wait queue structure */
typedef struct wait_queue {
    //-1 if nothing in queue
    int head;
    int tail;
    //this is the array that stores the next thread id, the order is maintain as FIFO
    //if no elements in queue, then nothing in this array(elements initialized to -1)
    int wait_queue_id[THREAD_MAX_THREADS];
}wait_queue;

/* This is the thread control block */
enum{
    running = 1,
    ready = 0,
    exited = 2,
    uninitialized = 3,
};

//the thread struct
typedef struct thread {
    //thread id
    Tid t_id;
    //status
    int state;
    //the thread context
    ucontext_t t_context;
    //points to the bottom of the thread's stack(used to free up the stack)(free should be called on the 
    //start of the malloced memory)
    void* stackPtr;
    //need a status to record whether the thread yield is in loop
    int yield_time;
    //the id of the next ready thread(used to maintain ready queue)
    int next_ready;
    //the wait queue of this thread
    wait_queue* wq;
    
}thread;

//the structure of the nodes of threads in queues
typedef struct threadNode {
    struct thread *threadPtr;
    struct threadNode *next;
}t_node;

//the structure of the queues
typedef struct threadQueue {
    struct threadNode *head;
}t_queue;


//*********** GLOBALS **************************************************
//statically allocated thread structs, ready queue resides on it
thread Tid_array[THREAD_MAX_THREADS];
//exited queue, stores all the flags indicating whether the thread with id is exited
int exited_queue[THREAD_MAX_THREADS] = {0};
//the running thread's id
int running_t;
//need the id of the head and tail thread of the ready and exited queue
int ready_head = -1;
int ready_tail = -1;

//The helper functions that manipulate the queues******************************
//push a node at the end of the ready queue
void push_ready_back(Tid target_n){
    //if the target queue is empty
    if(ready_head == -1){
        ready_head = target_n;
    }else{
        //not empty
        int cur = ready_head;
        while(Tid_array[cur].next_ready != -1){
            cur = Tid_array[cur].next_ready;
        }
        Tid_array[cur].next_ready = target_n;
    }
}

//pop a thread node at the front of the queue, return null if queue empty
//only the thread node is popped, and nothing is destroyed
int pop_ready_front(){
    //if empty
    if(ready_head == -1){
        return -1;
    }else{
        int popped = ready_head;
        ready_head = Tid_array[ready_head].next_ready;
        Tid_array[popped].next_ready = -1;
        return popped;
    }
}

//find whether the thread with given id is in the queue, 
//if yes, return pointer to it, else return null
int find_ready_node(Tid target_id){
    int cur = ready_head;
    while(cur != -1){
        if(cur == target_id){
            return cur;
        }
        cur = Tid_array[cur].next_ready;
    }
    return -1;
}

//pop the thread node with the given thread id, if correctly popped,
//return the pointer to it, note that nothing is destroyed, if not found that thread, return NULL
int pop_ready_id(Tid target_id){
    int prev = -1;
    int cur = ready_head;
    //when the queue is empty
    if(cur == -1){
        return -1;
    }
    while(cur != -1){
        if(cur == target_id){
            //when cur is the first node in queue, just update head
            if(prev == -1){
                ready_head = Tid_array[ready_head].next_ready;
                Tid_array[cur].next_ready = -1;
                return cur;
            }else{
                //when cur is not the first node, now prev is usable******               
                Tid_array[prev].next_ready = Tid_array[cur].next_ready;
                Tid_array[cur].next_ready = -1;
                return cur;
            }
        }else{
            prev = cur;
            cur = Tid_array[cur].next_ready;
        }
    }
    return -1;
}

//the helper functions that delete the structs********************************
//delete the whole thread structure through its pointer
void delete_thread(thread* target_t){
    //free out the stack it owns
    free(target_t->stackPtr);
    free(target_t);
}

//delete the thread node
void delete_t_node(t_node* target_n){
    target_n->next = NULL;
    delete_thread(target_n->threadPtr);
    free(target_n);
}

//delete things in a whole thread_queue
void delete_t_queue(t_queue* target_q){
    t_node* cur = target_q->head;
    while(cur != NULL){
        t_node* temp = cur->next;
        delete_t_node(cur);
        cur = temp;
    }
    target_q->head = NULL;
    //free(target_q);
}

wait_queue* wait_queue_create();

//helper function used to free up the exited array
void clean_up(){
    //clean up the exited queue, since all things that needs destroy are here 
    //delete_t_queue(exited_queue);
    for(int i = 0;i < THREAD_MAX_THREADS;i++){
        if(exited_queue[i] == 1){
            free(Tid_array[i].stackPtr);
            Tid_array[i].stackPtr = NULL;
            Tid_array[i].next_ready = -1;
            Tid_array[i].state = uninitialized;
            Tid_array[i].yield_time = 0;
        }
    }
    
}

void thread_stub(void(*thread_main)(void*), void* arg){
    //need to turn on this damn interrupt otherwise try move potato fails directly
    interrupts_on();
    
    thread_main(arg);
    thread_exit();
}

void
thread_init(void)
{
    //interrupts_off();
    int enable = interrupts_off();
    
    //initialize the Tid array(at this time no thread exists, hence all zeroes)
    for(int i = 0;i < THREAD_MAX_THREADS;i++){
        Tid_array[i].t_id = i;
        Tid_array[i].state = uninitialized;
        Tid_array[i].next_ready = -1;
        Tid_array[i].yield_time = 0;
        Tid_array[i].stackPtr = NULL;
        Tid_array[i].t_context.uc_mcontext.gregs[REG_RSP] = (long long int)NULL;
        Tid_array[i].wq = wait_queue_create();
    }
    
    //setup the first(kernel) thread
    Tid_array[0].state = running;
    running_t = 0;
    
    //restore interrupts state, always do this before return
    interrupts_set(enable);
}

Tid
thread_id()
{
    //return running_t->t_id;
    //int enable = interrupts_off();

    return Tid_array[running_t].t_id;
	//return THREAD_INVALID;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
    //first turn off interrupt
    int enable = interrupts_off();
    //then, find weather there's space for new thread
    int ret_id = -1;
    //0 is for the kernel thread 
    for(int i = 1;i < THREAD_MAX_THREADS;i++){
        if(Tid_array[i].state == uninitialized || Tid_array[i].state == exited){
            ret_id = i;
            break;
        }
    }
    //no space for new thread
    if(ret_id == -1){
        interrupts_set(enable);
        return THREAD_NOMORE;
    }else{
        //if the location stands for an exited thread, 
        //need to clear up the exited thread and create new one on it
        if(Tid_array[ret_id].state == exited){
            free(Tid_array[ret_id].stackPtr);
            Tid_array[ret_id].stackPtr = NULL;
            Tid_array[ret_id].next_ready = -1;
            Tid_array[ret_id].state = uninitialized;
            Tid_array[ret_id].yield_time = 0;
        }
        //there's unused thread strcut
        Tid_array[ret_id].state = ready;
    }
    
    //now, try to allocate stack for this new thread
    void* stack_ptr = malloc(THREAD_MIN_STACK);
    if(stack_ptr == NULL){
        interrupts_set(enable);
        return THREAD_NOMEMORY;
    }
    Tid_array[ret_id].stackPtr = stack_ptr;//here, there are memory for the stack so set the stackPtr
    //copy the context
    getcontext(&Tid_array[ret_id].t_context);
    
    //pc is RIP, sp is RSP, parameters in RDI, RSI**********************************************************
    //pc
    Tid_array[ret_id].t_context.uc_mcontext.gregs[REG_RIP] = (long long int)(&thread_stub);
    //arguments (2)
    Tid_array[ret_id].t_context.uc_mcontext.gregs[REG_RDI] = (long long int)fn;
    Tid_array[ret_id].t_context.uc_mcontext.gregs[REG_RSI] = (long long int)parg;
    //need to point to the top of the malloced space
    Tid_array[ret_id].t_context.uc_mcontext.gregs[REG_RSP] = (long long int)stack_ptr + THREAD_MIN_STACK - 8;
    
    //everything finished, add it to ready queue
    //if ready queue has no element yet
    push_ready_back(ret_id);
    
    interrupts_set(enable);
    return ret_id;
}

Tid
thread_yield(Tid want_tid)
{
    //Note: thread_ANY = -1, thread_Self = -2 defined in thread.h. thread self, invalid and none also defined
    //first turn off interrupts since we do not want to be bothered in a thread switch
    int enable = interrupts_off();
    
    //first do the input checking 
    //not a valid tid
    if(want_tid < -2 || want_tid >= THREAD_MAX_THREADS){
        interrupts_set(enable);
        return THREAD_INVALID;
    }else{
        //input tid is in right bound, however two cases of failure
        //first, if input tid is any but no other thread is ready, return none
        if(want_tid == THREAD_ANY && ready_head == -1){
            interrupts_set(enable);
            return THREAD_NONE;
        }
        //second, if input is tid but tid not found in ready queue, return invalid
        if(want_tid > -1 && (find_ready_node(want_tid) == -1) && want_tid != running_t){
            interrupts_set(enable);
            return THREAD_INVALID;
        }      
    }
    
    int current_t;
    //if the current running thread already exited*******************************************************
    if(Tid_array[running_t].state == exited){
        //if(want_tid == THREAD_ANY){
            //denote it in the exited queue, so that will be cleaned later
        exited_queue[running_t] = 1;
        Tid_array[running_t].next_ready = -1;
        //}
    }else{
        //now the input tid is valid and the running thread is not exited, start thread switch
        //(1) put the caller into the back of the ready queue
        current_t = running_t;
        Tid_array[running_t].state = ready;
        push_ready_back(running_t);
    }
    //the return tid
    Tid ret_id;  
    //********************(2) context switching ***********************************************
    getcontext(&Tid_array[running_t].t_context);
    //need to check how many times yield tries to happen, notice after the first time, 
    //the code will jump back to run here and skip the loop
    if(Tid_array[running_t].yield_time == 0){
        Tid_array[running_t].yield_time = 1;
        //now consider different input tid cases and determine the new running thread
        //if tid is a normal id in ready queue
        if(want_tid >= 0){
            int popped = pop_ready_id(want_tid);
            running_t = popped;
            Tid_array[running_t].state = running;
            ret_id = running_t;
        //if is any thread, just use the head of ready queue
        }else if(want_tid == THREAD_ANY){
            int popped = pop_ready_front();
            running_t = popped;
            Tid_array[running_t].state = running;
            ret_id = running_t;
        //if is thread self
        }else{
            //resume the last running thread, which is pointed to by new_node
            int popped = pop_ready_id(current_t);
            running_t = popped;
            Tid_array[running_t].state = running;
            ret_id = running_t;
        }
        
        
        //now restore the thread context
        setcontext(&Tid_array[running_t].t_context);
        //****************************************************************************************
    }
    
    //reset the yield time back to 0
    Tid_array[running_t].yield_time = 0;
    
    //clean the exited threads
    clean_up();
    
    //finally, enable interrupt and return
    interrupts_set(enable);
    return ret_id;
}

int
thread_wakeup(struct wait_queue *queue, int all);

void
thread_exit()
{
    int enable = interrupts_off();
    //first thing to do now is wake up the threads that wait on this thread
    thread_wakeup(Tid_array[running_t].wq, 1);
    //check if there are other threads to run
    //if there are other threads in ready, switch to a new running thread
    if(ready_head != -1){
        //push the current thread into exit queue and change its state
        Tid_array[running_t].state = exited;
        //now, yield to other ready thread
        //notice, in thread yield, I'll check if the current running thread is in exited state, if yes
        //put it into exited array instead of ready, so it'll be cleaned up at the end of yield.
        thread_yield(THREAD_ANY);
    }else{
        interrupts_set(enable);
        exit(0);
    }
}

Tid
thread_kill(Tid tid)
{
    int enable = interrupts_off();
    //first check if the tid is valid
    if(tid < 0 || tid >= THREAD_MAX_THREADS || tid == running_t || Tid_array[tid].state == uninitialized){
        interrupts_set(enable);
        return THREAD_INVALID;
    }else{
        //here, we ensure node with tid is in ready, so move it to exited queue and wakeup threads 
        //sleeping on it
        thread_wakeup(Tid_array[tid].wq, 1);
        pop_ready_id(tid);
        Tid_array[tid].state = exited;
    }
    interrupts_set(enable);
    return tid;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
    int enable = interrupts_off();
    struct wait_queue *wq;

    wq = malloc(sizeof(struct wait_queue));
    assert(wq);
    //initialize all thing to -1 indicating nothing in the queue
    wq->head = -1;
    wq->tail = -1;
    for(int i = 0;i < THREAD_MAX_THREADS;i++){
        wq->wait_queue_id[i] = -1;
    }
    
    interrupts_set(enable);
    return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
    int enable = interrupts_off();
    //nothing malloced so just free wq
    free(wq);
    interrupts_set(enable);
}

//wait queue helper functions*********************************************
//push a thread id into the specified wait queue
void enqueue_wait(wait_queue* wq, int Tid){
    int enable = interrupts_off();
    //if the wait queue is empty
    if(wq->head == -1){
        wq->head = Tid;
        wq->tail = Tid;
        wq->wait_queue_id[0] = Tid;
        interrupts_set(enable);
    }else{
        //queue not empty
        wq->tail = Tid;
        int location = 0;
        while(wq->wait_queue_id[location] != -1){
            location++;
        }
        wq->wait_queue_id[location] = Tid;
        interrupts_set(enable);
    }
}

//pop the head of the wait queue, if success return the id of popped, not success(empty queue), return -1
int pop_front_wait(wait_queue* wq){
    int enable = interrupts_off();
    //wait queue already empty
    if(wq->head == -1){
        interrupts_set(enable);
        return -1;
    }else{
        //only one element in the wait queue
        if(wq->head == wq->tail){
            int popped = wq->head;
            wq->head = -1;
            wq->tail = -1;
            wq->wait_queue_id[0] = -1;
            interrupts_set(enable);
            return popped;
        //more than one element in the wq
        }else{
            int popped = wq->head;
            wq->head = wq->wait_queue_id[1];
            int fast = 1;
            int slow = 0;
            //to update the array of Tid in wait queue
            while(wq->wait_queue_id[fast] != -1){
                wq->wait_queue_id[slow] = wq->wait_queue_id[fast];
                slow++;
                fast++;
            }
            wq->wait_queue_id[slow] = -1;
            
            interrupts_set(enable);
            return popped;
        }
    }
}

//moves all the threads in this wait queue to ready queue, returns the number of removed threads
//if the thread in the waiting queue is already killed, do not put it into ready
int move_to_ready_all(wait_queue* wq){
    int enable = interrupts_off();
    //wait queue already empty
    if(wq->head == -1){
        interrupts_set(enable);
        return 0;
    }else{
        int count = 0;
        int num_waked = 0;
        while(wq->wait_queue_id[count] != -1){
            //only when this thread in the wait queue is not killed, will he be moved to ready
            if(Tid_array[wq->wait_queue_id[count]].state != exited){
                push_ready_back(wq->wait_queue_id[count]);
                Tid_array[wq->wait_queue_id[count]].state = ready;
                wq->wait_queue_id[count] = -1;
                num_waked++;
            }else{
                //already killed, so just remove him from waiting kill 
                wq->wait_queue_id[count] = -1;
            }
            count++;
        }
        wq->head = -1;
        wq->tail = -1;
        interrupts_set(enable);
        return count;
    }
}

Tid
thread_sleep(struct wait_queue *queue)
{
    int enable = interrupts_off();
    
    //first do the validity check
    //invalid queue
    if(queue == NULL){
        interrupts_set(enable);
        return THREAD_INVALID;
    //no threads available
    }else if(ready_head == -1){
        interrupts_set(enable);
        return THREAD_NONE;
    }
    
    //now, everything valid, first insert the current thread into wait queue
    enqueue_wait(queue, running_t);
    //then, pick the next thread to run from the ready queue
    int next_run = pop_ready_front();
    
    //finally switch to the thread with id next_run
    //********************(2) context switching ***********************************************
    getcontext(&Tid_array[running_t].t_context);
    //need to check how many times yield tries to happen, notice after the first time, 
    //the code will jump back to run here and skip the loop
    if(Tid_array[running_t].yield_time == 0){
        Tid_array[running_t].yield_time = 1;
        //tid is a normal id in ready queue
        running_t = next_run;
        Tid_array[running_t].state = running;
        //now restore the thread context
        setcontext(&Tid_array[running_t].t_context);
        //****************************************************************************************
    }    
    //reset the yield time back to 0
    Tid_array[running_t].yield_time = 0;
    
    //clean the exited threads
    clean_up();
    
    //finally, enable interrupt and return
    interrupts_set(enable);
    return next_run;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
    int enable = interrupts_off();
    
    //first do the validity check
    //invalid queue
    if(queue == NULL){
        interrupts_set(enable);
        return 0;
    }
    
    //if the wait queue is empty, return 0
    if(queue->head == -1){
        interrupts_set(enable);
        return 0;
    }
    
    //now if all is 0
    if(all == 0){
        //first remove the head of the wait queue
        int waked = pop_front_wait(queue);
        //then, push this id into ready queue and change its state to ready
        push_ready_back(waked);
        Tid_array[waked].state = ready;
        
        interrupts_set(enable);
        return 1;
    }else{
        int result = move_to_ready_all(queue);
        interrupts_set(enable);
        return result;
    }
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
    int enable = interrupts_off();
    
    //first check validity of tid
    if(tid < 0 || tid >= THREAD_MAX_THREADS || tid == running_t || 
            Tid_array[tid].state == exited || Tid_array[tid].state == uninitialized){
        interrupts_set(enable);
        return THREAD_INVALID;
    }
    
    //now, the tid is valid
    //sleep the current thread on wait queue of tid
    thread_sleep(Tid_array[tid].wq);
    //notice when the thread with tid terminates(call exit), staff needs to be handled in thread_exit
    //and here, we just safely return 
    interrupts_set(enable);
    return tid;
}

struct lock {
    //the wait queue on this lock
    wait_queue*  lock_queue;
    int acquired;
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	lock->acquired = 0;
        lock->lock_queue = wait_queue_create();
        

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	wait_queue_destroy(lock->lock_queue);

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	//the blocking lock needs interrupt disabling
        int enable = interrupts_off();
        while(lock->acquired == 1){
            thread_sleep(lock->lock_queue);
        }
        
        //now lock is free, acquire the lock
        lock->acquired = 1;
        interrupts_set(enable);
}

void
lock_release(struct lock *lock)
{
    int enable = interrupts_off();
    lock->acquired = 0;
    //when the lock is released, blocking lock should wake up all the threads on its queue
    thread_wakeup(lock->lock_queue, 1);
    interrupts_set(enable);
}

struct cv {
    wait_queue* wq;
};

struct cv *
cv_create()
{
        int enable = interrupts_off();
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	cv->wq = wait_queue_create();
        
        interrupts_set(enable);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
    int enable = interrupts_off();	
    assert(cv != NULL);
    
    wait_queue_destroy(cv->wq);
    
    interrupts_set(enable);
    free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    int enable = interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);
    if(lock->acquired == 1){
        //first release
        lock_release(lock);
        //then, sleep
        thread_sleep(cv->wq);
        //now, signaled, so reacquire lock
        lock_acquire(lock);
    }

    interrupts_set(enable);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    int enable = interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);
    
    if(lock->acquired == 1){
        thread_wakeup(cv->wq, 0);
    }

    interrupts_set(enable);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    int enable = interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);

    if(lock->acquired == 1){
        thread_wakeup(cv->wq, 1);
    }
    
    interrupts_set(enable);
}