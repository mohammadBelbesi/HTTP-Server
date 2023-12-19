/**Name:Mohammad Belbesi**/

#include <stdio.h>
#include <stdlib.h>
#include "threadpool.h"

/**
 * create_threadpool creates a fixed-sized thread
 * pool.  If the function succeeds, it returns a (non-NULL)
 * "threadpool", else it returns NULL.
 * this function should:
 * 1. input sanity check
 * 2. initialize the threadpool structure
 * 3. initialized mutex and conditional variables
 * 4. create the threads, the thread init function is do_work and its argument is the initialized threadpool.
 */
threadpool *create_threadpool(int num_threads_in_pool) {

    if(num_threads_in_pool>MAXT_IN_POOL||num_threads_in_pool<1){//check the legacy of the parameter
        fprintf(stderr,"the number of threads is bigger than the maximum number of threads allowed in the pool");
        return NULL;
    }

    threadpool *pool=(threadpool*)malloc(1*sizeof (threadpool));//creat the threadPool
    if(pool==NULL){//check the malloc
        fprintf(stderr,"the pool allocated memory is failed!\n");
        return NULL;
    }

    /** initialize the structure **/
    pool->num_threads=num_threads_in_pool;//number of active threads
    pool->qsize=0;//threads number in the queue

    pool->threads=(pthread_t*) malloc(num_threads_in_pool*sizeof (pthread_t));//pointer to threads
    if(pool->threads==NULL){
        fprintf(stderr,"the threads allocated memory is failed!\n");
        free(pool);
        return NULL;
    }

    pool->qhead=pool->qtail=NULL;//initialize queue head and tail pointer to NULL

    int returns_value;
    returns_value=pthread_mutex_init(&(pool->qlock),NULL);//initialize the mutex qLock
    if(returns_value!=0){
        free(pool->threads);
        free(pool);
        perror("pthread initialize is failed!");
        return NULL;
    }

    returns_value=pthread_cond_init(&(pool->q_not_empty),NULL);//initialize the not empty condition
    if(returns_value!=0){
        pthread_mutex_destroy(&(pool->qlock));
        free(pool->threads);
        free(pool);
        perror("pthread condition not empty initialize is failed!");
        return NULL;
    }

    returns_value= pthread_cond_init(&(pool->q_empty),NULL);//initialize the empty condition
    if(returns_value!=0){
        pthread_mutex_destroy(&(pool->qlock));
        pthread_cond_destroy(&(pool->q_not_empty));
        free(pool->threads);
        free(pool);
        perror("pthread condition empty initialize is failed!");
        return NULL;
    }

    pool->shutdown=pool->dont_accept=0;

    for (int i = 0 ; i < num_threads_in_pool; i++) {//create the threads
        returns_value= pthread_create(&(pool->threads[i]),NULL, do_work,pool);
        if(returns_value!=0){
            pthread_mutex_destroy(&(pool->qlock));
            pthread_cond_destroy(&(pool->q_not_empty));
            pthread_cond_destroy(&(pool->q_empty));
            free(pool->threads);
            free(pool);
            perror("thread initialize is failed!");
            return NULL;
        }
    }

    return pool;
}

/**
 * dispatch enter a "job" of type work_t into the queue.
 * when an available thread takes a job from the queue, it will
 * call the function "dispatch_to_here" with argument "arg".
 * this function should:
 * 1. create and init work_t element
 * 2. lock the mutex
 * 3. add the work_t element to the queue
 * 4. unlock mutex
 *
 */
void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg) {
    if(from_me!=NULL || from_me->dont_accept!=1){
        work_t* new_work=(work_t*) malloc(1*sizeof (work_t));//create the work_t structure
        if(new_work==NULL){
            fprintf(stderr,"the new work allocated memory is failed!\n");
            destroy_threadpool(from_me);
            return;
        }
        /** initialize the structure **/
        new_work->routine=dispatch_to_here;
        new_work->arg=arg;
        new_work->next=NULL;
        pthread_mutex_lock(&(from_me->qlock));
        if(from_me->qsize==0){//if the queue is empty
            from_me->qhead=from_me->qtail=new_work;
        }
        else{//if the queue is not empty
            from_me->qtail->next=new_work;
            from_me->qtail=new_work;
        }
        from_me->qsize++;
        pthread_mutex_unlock(&(from_me->qlock));
        pthread_cond_signal(&(from_me->q_not_empty));
        return;
    }
    else//if destroy func has begun , don't accept new item to the queue
        return;
}

/**
 * The work function of the thread
 * this function should:
 * 1. lock mutex
 * 2. if the queue is empty, wait
 * 3. take the first element from the queue (work_t)
 * 4. unlock mutex
 * 5. call the thread routine
 *
 */
void *do_work(void *p) {
    threadpool* pool=(threadpool*)p;//casting the void* p to work on it
    int returns_value;//to check the access of every functions that we use
    returns_value= pthread_mutex_lock(&pool->qlock);
    if(returns_value!=0){//it's meaning that the lock func failed
        perror("the pthread mutex lock is failed!");
        return NULL;
    }

    while(1){//endless loop
        if(pool->shutdown==1){
            returns_value=pthread_mutex_unlock(&pool->qlock);
            if(returns_value!=0){
                perror("the pthread mutex unlock is failed!");
                return NULL;
            }
            return NULL;
        }
        else if(pool->qsize==0){
            returns_value=pthread_cond_wait(&(pool->q_not_empty),&(pool->qlock));//wait (no job to make) until the pool is not empty
            if(returns_value!=0){
                perror("the pthread condition wait is failed!");
                return NULL;
            }
            /*if(pool->shutdown==1){//// if the thread wake up check again the shutdown ////
                //pthread_exit(NULL);//it's cause a memory leaking , that's why i use return NULL
                return NULL;
            }*/
        }
        else{
            /**
                take the first element from the queue (*work_t)
                ,make the next element is the header and update the qsize
             **/
            work_t* active_thread = pool->qhead;
            pool->qhead=pool->qhead->next;
            pool->qsize--;

            if(pool->qsize==0 && pool->dont_accept==1){
                returns_value=pthread_cond_signal(&(pool->q_empty));
                if(returns_value!=0){
                    perror("pthread condition signal failed!");
                    free(active_thread);
                    return NULL;
                }
                pthread_mutex_unlock(&(pool->qlock));
                active_thread->routine(active_thread->arg);
                free(active_thread);
                return NULL;
            }
            pthread_mutex_unlock(&(pool->qlock));
            active_thread->routine(active_thread->arg);
            free(active_thread);
        }
    }

}

/**
 * destroy_threadpool kills the threadpool, causing
 * all threads in it to commit suicide, and then
 * frees all the memory associated with the threadpool.
 */
void destroy_threadpool(threadpool *destroyme) {
    pthread_mutex_lock(&(destroyme->qlock));
    destroyme->dont_accept=1;
    while (destroyme->qsize!=0){
        pthread_cond_wait(&(destroyme->q_empty),&(destroyme->qlock));
    }
    destroyme->shutdown=1;
    pthread_cond_broadcast(&(destroyme->q_not_empty));//the same of signal but the broadcast wake all the threads "stackoverflow"
    pthread_mutex_unlock(&(destroyme->qlock));

    void *status;
    for (int i = 0; i < destroyme->num_threads; i++) {
        pthread_join(destroyme->threads[i],&status);//join all the threads
    }
    /** free and destroy whatever i have to free and destroy**/
    pthread_mutex_destroy(&(destroyme->qlock));
    pthread_cond_destroy(&(destroyme->q_empty));
    pthread_cond_destroy(&(destroyme->q_not_empty));
    free(destroyme->threads);
    free(destroyme);
}

/*
int printFun(void* a){
    int i = *((int*)a);
    printf("ran%d\n" , i);
    return 0;
}


int printFun0(void* a){
    printf("ran0\n" );
    return 0;
}

int printFun1(void* a){
    printf("ran1\n" );
    return 0;
}

int printFun2(void* a){
    printf("ran2\n" );
    return 0;
}


int main() {
    int num = 200;
    int (*funcPrint)(void*) = &printFun;
    int (*funcPrint0)(void*) = &printFun0;
    int (*funcPrint1)(void*) = &printFun1;
    int (*funcPrint2)(void*) = &printFun2;
    threadpool *th = create_threadpool(200);
    int arr[num];
for(int i = 0 ; i < num ; i++){
        arr[i]=i;
        dispatch(th, funcPrint ,&arr[i] );
    }

    for(int i = 0 ; i < num ; i++){
        arr[i]=i;
        dispatch(th, funcPrint0 ,&arr[i] );
    }
    for(int i = 0 ; i < num ; i++){
        arr[i]=i;
        dispatch(th, funcPrint1 ,&arr[i] );
    }
    for(int i = 0 ; i < num ; i++){
        arr[i]=i;
        dispatch(th, funcPrint2 ,&arr[i] );
    }
    destroy_threadpool(th);
    return 0;

}
*/
