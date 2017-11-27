#include "coursework.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

/*
    SJF Bounded & MC (Shortest-Job-First with Bounding Buffer and Multiple Consumers) Implementation of predefined process.
    Predefined constraints are preprocessor macros in 'coursework.h'
*/

// Using this as a helper function
void swap(struct process** left, struct process** right)
{
    struct process* temp = *left;
    *left = *right;
    *right = temp;
}

// Quick and dirty way to get size of linked list. size_t as opposed to unsigned int as values inputted into things like operator[] are not unsigned ints, and do have different limits.
// size_t declared in stdlib.h and stddef.h iirc.
size_t list_size(struct process* head)
{
    size_t size = 0;
    while(head != (void*)0)
    {
        size++;
        head = head->oNext;
    }
    return size;
}

/* pthread functionality requires that all functions ran on a separate thread must return void* and take a single void* parameter.
however, multiple parameters will be requires, such as a pointer to the head of the process list, a mutex lock etc.
to solve this, the following structs are used to contain all required data:
*/

struct creator_pack
{
    pthread_mutex_t* mutex_handle;
    // This is the shared data. Although this is just a copy of a pointer, the data being pointed to is the shared data.
    // Therefore whenever consumer runs or edits any process in the list in anyway, mutex lock must be invoked during such execution.
    struct process** head;
    unsigned int* creating_finished;
};

struct consumer_pack
{
    pthread_mutex_t* mutex_handle;
    unsigned int consumer_id;
    // still is the shared data.
    // head is a double ptr because the head position will change alot.
    struct process** head;
    struct process* tail;
    unsigned int* creating_finished;
    // Want to access the totals values to edit them with any consumption of processes performed.
    unsigned int* total_response_time;
    unsigned int* total_turnaround_time;
};

// SJF. edits the list so MUST be mutex locked.
void add_process(pthread_mutex_t* lock, struct process** head, struct process* a_process)
{
    pthread_mutex_lock(lock);
    struct process* process_head = *head;
    if(a_process->iBurstTime < process_head->iBurstTime)
    {
        swap(head, &a_process);
        (*head)->oNext = a_process;
        pthread_mutex_unlock(lock);
        return;
    }
    struct process* iter = process_head;
    while(iter->iBurstTime < a_process->iBurstTime)
    {
        // (void*)0 is basically exactly what the macro NULL does. Using it like this as NULL confuses me when it's just referring to memloc 0, not necessarily a nullptr
        if(iter->oNext == (void*)0)
        {
            // at the end of the linked list, so this is the longest burst time so far, so make it the tail
            iter->oNext = a_process;
            break;
        }
        if(iter->oNext->iBurstTime > a_process->iBurstTime)
        {
            // needs to be inserted between iter and iter next
            struct process* tmp = iter->oNext;
            iter->oNext = a_process;
            a_process->oNext = tmp;
        }
        iter = iter->oNext;
    }
    pthread_mutex_unlock(lock);
}
/*
void add_process(pthread_mutex_t* lock, struct process** head, struct process* a_process)
{
    pthread_mutex_lock(lock);
    struct process* head_cpy = *head;
    if(head_cpy == (void*)0)
        *head = a_process;
    while(head_cpy->oNext != (void*)0)
    {
        head_cpy = head_cpy->oNext;
    }
    head_cpy->oNext = a_process;
    pthread_mutex_unlock(lock);
}
*/

// Must be ran on a creator package where the process head is just one element.
void* create_processes(void* creator_package)
{
    struct creator_pack* creator = (struct creator_pack*) creator_package;
    printf("Asserting list_size == 1...\n");
    assert(list_size(*creator->head) == 1);
    size_t processes_created = 1;
    while(processes_created < NUMBER_OF_PROCESSES)
    {
        // this thread keeps trying to create new processes until the number of processes made in total is what we need.
        if(list_size(*creator->head) <= BUFFER_SIZE)
        {
            // we have space to generate a new process, so do so.
            struct process* new_process = generateProcess();
            add_process(creator->mutex_handle, creator->head, new_process);
            processes_created++;
            printf("Added process (size now %d). Created %d/%d in total.\n", list_size(*creator->head), processes_created, NUMBER_OF_PROCESSES);
        }
        else
        {
            // we arent allowed to generate a new process. the mutex is not locked so we wait, i.e do nothing and sleep.
            printf("Buffer Size exceeded, list size = %d, waiting...\n", list_size(*creator->head));
            sleep(1);
        }
    }
    pthread_mutex_lock(creator->mutex_handle);
    *(creator->creating_finished) = 1;
    pthread_mutex_unlock(creator->mutex_handle);
    // Make the bool true so the other thread can safely read. Shouldn't need to mutex this.
    pthread_exit(NULL);
    // Kill the thread. We're done creating processes.
}

// Take double pointer to head remains true. edits the list so MUST be locked!
void remove_process(pthread_mutex_t* lock, struct process** head, struct process* to_remove)
{
    //pthread_mutex_lock(lock);
    struct process* process_head = *head;
    if(process_head == (void*)0)
    {
        //pthread_mutex_unlock(lock);
        printf("the head is NULL, cant remove process.\n");
        return;
    }
    if(process_head == to_remove)
    {
        struct process* tmp = process_head;
        *head = process_head->oNext;
        printf("freed.\n");
        free(process_head);
    }
    while(process_head->oNext != (void*)0)
    {
        if(process_head->oNext == to_remove)
        {
            if(to_remove->oNext != (void*)0)
                process_head->oNext = to_remove->oNext;
            printf("freed.\n");
            free(to_remove);
        }
        process_head = process_head->oNext;
    }
    //pthread_mutex_unlock(lock);
}

void* consume_processes(void* consumer_package)
{
    struct consumer_pack* consumer = (struct consumer_pack*) consumer_package;
    //struct process* head_cache = consumer->head;
    struct process* begin = *consumer->head;
    unsigned int i = 0;
    // make begin point to cid element in the linked list. but that element may not have been made yet. if its not, we need to wait till it exists.
    while(i < consumer->consumer_id)
    {
        if(begin->oNext == NULL)
        {
            printf("beginning point for cid %d is not ready yet, waiting...\n", consumer->consumer_id);
            sleep(1);
            continue;
        }
        begin = begin->oNext;
        i++;
    }
    struct process* head = begin;
    //printf("cid = %d, list size upon thread init = %d.\n", consumer->consumer_id, list_size(head));
    // when this begins, we will have definitely at least one process in the linked list. apart from that, everything is off the cards.
    // we must not process and finish the last process (head) though until more are made or no more are being made and we're about to finish up.
    // the reason for this is that there will be a dangling head ptr which will segfault when the other thread tries to add another process after it.
    while(*(consumer->creating_finished) == 0 || list_size(head) > 0)
    {
        // tasks are still on their way and we need to be ready for them too.
        // just make sure we dont complete the last task.
        // we cant hack through this though, we do first come first serve. this is why we need the tail i.e never process tail until the loop is ending.
        if(list_size(head) <= 1 && list_size(head) <= consumer->consumer_id && (*consumer->creating_finished) == 0)
        {
            printf("list size is less than cid (%d) or 1 (%d), waiting for more...\n", consumer->consumer_id, list_size(head));
            //head = begin;
            sleep(1);
            continue;
        }
        printf("\nlocking cid %d.\n", consumer->consumer_id);
        // by this point, another consumer might have just killed and invalidated the current head in question. so after locking, make sure the head is still valid or dont bother.
        // pthread mutex trylock is used here to continue if the lock is in use instead of waiting with an invalid head.
        if(pthread_mutex_trylock(consumer->mutex_handle) != 0)
        {
            printf("cant lock cid %d, continuing loop wait...\n", consumer->consumer_id);
            sleep(1);
            continue;
        }
        //pthread_mutex_lock(consumer->mutex_handle);
        printf("\nlocked cid %d.\n", consumer->consumer_id);
        // perform processing
        struct timeval start, end;
        int previous_burst = head->iBurstTime;
        simulateSJFProcess(head, &start, &end);
        unsigned int response_time = getDifferenceInMilliSeconds(head->oTimeCreated, start);
        printf("pid = %d, previous burst = %d, new burst = %d", head->iProcessId, previous_burst, head->iBurstTime);
        printf(", response time = %ld", response_time);
        *(consumer->total_response_time) += response_time;

        unsigned int turnaround_time = getDifferenceInMilliSeconds(head->oTimeCreated, end);
        printf(", turnaround time = %ld", turnaround_time);
        //printf("\nprocess being killed. process list size = %d\n", list_size(*consumer->head));
        *(consumer->total_turnaround_time) += turnaround_time;
        remove_process(consumer->mutex_handle, &begin, head);

        printf("\nunlocking.\n");
        pthread_mutex_unlock(consumer->mutex_handle);
        printf("\nunlocked. this thread sees list size as %d, actual list size is %d\n", list_size(begin), list_size(*consumer->head));

        head = begin;
        printf("\n");
    }
    pthread_exit(NULL);
    // Kill the thread.
}

int is_finished(struct process* a_process)
{
    return a_process->iState == FINISHED;
}

int all_finished(struct process* process_head)
{
    while(process_head != (void*)0)
    {
        if(process_head->iState != FINISHED)
            return 0;
        process_head = process_head->oNext;
    }
    return 1;
}

int main()
{
    unsigned int total_turnaround_time = 0;
    unsigned int total_response_time = 0;
    // Give me a process. Linked List is currently sorted as contains one element.
    struct process* process_head = generateProcess();
    struct process* process_tail = process_head;
    unsigned int i;
    // make number of processes we've allocated equal to the macro
    /*
    for(i = 0; i < NUMBER_OF_PROCESSES; i++)
    {
        struct process* a_process = generateProcess();
        add_process(process_head, a_process);
        process_tail = a_process;
    }
    */
    unsigned int create_done = 0;
    pthread_mutex_t lock;
    pthread_t creator_thread_handle, consumer_thread_handle[NUMBER_OF_CONSUMERS];
    struct creator_pack creator;
    creator.mutex_handle = &lock;
    creator.head = &process_head;
    creator.creating_finished = &create_done;
    pthread_create(&creator_thread_handle, NULL, create_processes, &creator);
    struct consumer_pack consumer[NUMBER_OF_CONSUMERS];
    for(i = 0; i < NUMBER_OF_CONSUMERS; i++)
    {
        consumer[i].mutex_handle = &lock;
        consumer[i].consumer_id = i;
        consumer[i].head = &process_head;
        consumer[i].tail = process_tail;
        consumer[i].creating_finished = &create_done;
        consumer[i].total_response_time = &total_response_time;
        consumer[i].total_turnaround_time = &total_turnaround_time;
        pthread_create(&consumer_thread_handle[i], NULL, consume_processes, &consumer[i]);
    }
    // Creator thread separate. Consumption thread unnecessary as that will be done in the main thread.
    // The reason I do not create another thread for consumption as the main thread will just wait for it anyway so might aswell use it.

    pthread_join(creator_thread_handle, NULL);
    for(i = 0; i < NUMBER_OF_CONSUMERS; i++)
        pthread_join(consumer_thread_handle[i], NULL);
    printf("Done. Average Response Time = %ldms, Average Turnaround Time = %ldms\n", total_response_time / NUMBER_OF_PROCESSES, total_turnaround_time / NUMBER_OF_PROCESSES);
    return 0;
}