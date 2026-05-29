#define _XOPEN_SOURCE 700

#include "uthread.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

// Maximum number of threads that can be created
#define MAX_THREADS 128

// Main thread ID is 0
#define MAIN_THREAD_ID 0

// Default stack size for newly created uthreads
#define DEFAULT_STACK_SIZE (64 * 1024) // 64 KB

#define MIN_PRIORITY 0
#define MAX_PRIORITY 9

// Internal states for threads
typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_FINISHED
} thread_state_t;

// TCB - Thread Control Block: all information to manage a thread
typedef struct {
    uthread_t id;               // Unique thread ID
    ucontext_t context;         // Thread context (registers, stack pointer, etc.)

    void *stack;                // Pointer to the thread's stack
    size_t stack_size;          // Size of the thread's stack

    void *(*func)(void *);      // Function to execute in the thread
    void *args;                 // Arguments to the thread function

    void *retval;               // Return value from the thread function

    int priority;               // Thread priority
    int detached;                // Flag indicating if the thread is detached
    int joiner;                  // ID of thread waiting for this thread to finish

    thread_state_t state;       // Current state of the thread
} tcb;

static tcb thread_table[MAX_THREADS]; // Table to hold all thread control blocks

// ID of current running thread
static uthread_t current_thread_id = MAIN_THREAD_ID;
// Stack size for new uthreads
static size_t thread_stack_size = DEFAULT_STACK_SIZE;

// Flag for when library is initialized
static int initialized = 0;

/*
 * Priority ready queue.
 *
 * Instead of one queue, we have one queue per priority.
 *
 * Priority 0 = lowest
 * Priority 9 = highest
 *
 * Each priority level is still FIFO internally.
 */
static int ready_queue[MAX_PRIORITY + 1][MAX_THREADS];
static int ready_queue_head[MAX_PRIORITY + 1];
static int ready_queue_tail[MAX_PRIORITY + 1];
static int ready_queue_count[MAX_PRIORITY + 1];

/*
 * Total number of ready threads across all priority levels.
 */
static int total_ready_count = 0;

static int ready_queue_empty(void) {
    return total_ready_count == 0;
}

static void enqueue(int id) {
    int priority = thread_table[id].priority;

    if (priority < MIN_PRIORITY || priority > MAX_PRIORITY) {
        abort();
    }

    if (ready_queue_count[priority] >= MAX_THREADS) {
        abort();
    }

    // Add this thread to the back of its priority queue.
    ready_queue[priority][ready_queue_tail[priority]] = id;

    ready_queue_tail[priority] =
        (ready_queue_tail[priority] + 1) % MAX_THREADS;

    ready_queue_count[priority]++;
    total_ready_count++;
}

static int dequeue(void) {
    if (ready_queue_empty()) {
        return -1;
    }

    // Search from highest priority to lowest priority.
    for (int priority = MAX_PRIORITY; priority >= MIN_PRIORITY; priority--) {
        if (ready_queue_count[priority] > 0) {
            int id = ready_queue[priority][ready_queue_head[priority]];

            ready_queue_head[priority] =
                (ready_queue_head[priority] + 1) % MAX_THREADS;

            ready_queue_count[priority]--;
            total_ready_count--;

            return id;
        }
    }

    return -1;
}

static int find_free_thread_slot(void) {
    for (int i = 1; i < MAX_THREADS; i++) {
        if (thread_table[i].state == THREAD_UNUSED) {
            return i;
        }
    }
    return -1; // No free slot  
}

// Starting function for every created uthread
// makecontext starts thread here, we call the actual thread function and then exit when function returns
static void thread_trampoline(int id) {
    void *result = thread_table[id].func(thread_table[id].args);
    uthread_exit(result);
}

static void free_thread(int id) {
    if (id == MAIN_THREAD_ID) {
        return; // Don't free main thread
    }

    free(thread_table[id].stack);

    // reset table slot so it can be reused
    memset(&thread_table[id], 0, sizeof(thread_table[id]));
    thread_table[id].id = id;
    thread_table[id].state = THREAD_UNUSED;
    thread_table[id].joiner = -1;
}

static void reap_detached_finished_threads(void) {
    // Clean up detached threads already finished from another thread
    for (int i = 1; i < MAX_THREADS; i++) {
        if (thread_table[i].state == THREAD_FINISHED && thread_table[i].detached) {
            free_thread(i);
        }
    }
}

int uthread_init(size_t stack_sz) {
    if (initialized) {
        return 0; // Already initialized
    }
    if (stack_sz > 0) {
        thread_stack_size = stack_sz;
    }

    // Clear thread table
    memset(ready_queue, 0, sizeof(ready_queue));
    memset(ready_queue_head, 0, sizeof(ready_queue_head));
    memset(ready_queue_tail, 0, sizeof(ready_queue_tail));
    memset(ready_queue_count, 0, sizeof(ready_queue_count));
    total_ready_count = 0;

    // Initialize every thread table slot
    for (int i = 0; i < MAX_THREADS; i++) {
        thread_table[i].id = i;
        thread_table[i].state = THREAD_UNUSED;
    }

    // Save current context of main thread
    if (getcontext(&thread_table[MAIN_THREAD_ID].context) == -1) {
        return -1; // Failed to get context
    }

    thread_table[MAIN_THREAD_ID].state = THREAD_RUNNING;
    thread_table[MAIN_THREAD_ID].priority = MIN_PRIORITY;
    current_thread_id = MAIN_THREAD_ID;
    initialized = 1;

    return 0;
}

int uthread_create(uthread_t *thread, void *(*func)(void *), void *args, int priority) {
    if (!initialized) {
        if (uthread_init(DEFAULT_STACK_SIZE) == -1) {
            return -1; // Failed to initialize
        }
    }

    reap_detached_finished_threads();
    
    if (thread == NULL || func == NULL) {
        errno = EINVAL;
        return -1; // Invalid arguments
    }

    // Check validity of priority
    if (priority < MIN_PRIORITY || priority > MAX_PRIORITY) {
        errno = EINVAL;
        return -1;
    }

    int id = find_free_thread_slot();

    if (id == -1) {
        errno = EAGAIN;
        return -1; // No more threads can be created
    }

    // Allocate stack for the new thread
    void *stack = malloc(thread_stack_size);
    if (stack == NULL) {
        return -1; // Failed to allocate stack
    }

    // Intialize thread context
    if (getcontext(&thread_table[id].context) == -1) {
        free(stack);
        return -1; // Failed to get context
    }

    thread_table[id].stack = stack;
    thread_table[id].stack_size = thread_stack_size;

    thread_table[id].func = func;
    thread_table[id].args = args;
    thread_table[id].retval = NULL;
    thread_table[id].priority = priority;

    thread_table[id].detached = 0;
    thread_table[id].joiner = -1;
    thread_table[id].state = THREAD_READY;

    // Tell context to use stack we allocated
    thread_table[id].context.uc_stack.ss_sp = stack;
    thread_table[id].context.uc_stack.ss_size = thread_stack_size;
    thread_table[id].context.uc_stack.ss_flags = 0;

    // Where to go after after thread finishes (we will call thread_trampoline to call the actual thread function and then exit)
    thread_table[id].context.uc_link = NULL; // No context to switch to when thread finishes

    makecontext(&thread_table[id].context, (void (*)(void))thread_trampoline, 1, id);

    enqueue(id); // Add new thread to ready queue
    *thread = id; // Return thread ID to caller

    return 0;
}

int uthread_join(uthread_t thread, void **retval) {
    if (!initialized || thread <= 0 || thread >= MAX_THREADS) {
        errno = EINVAL;
        return -1;
    }

    // Thread cannot join itself
    if(thread == current_thread_id) {
        errno = EINVAL;
        return -1;
    }

    // Must join a thread that exists
    if (thread_table[thread].state == THREAD_UNUSED) {
        errno = EINVAL;
        return -1;
    }

    // Thread cannot join a thread that is already detached
    if (thread_table[thread].detached) {
        errno = EINVAL;
        return -1;
    }

    // Only one thread can join target thread
    if (thread_table[thread].joiner != -1 && thread_table[thread].joiner != current_thread_id) {
        errno = EINVAL;
        return -1;
    }

    while (thread_table[thread].state != THREAD_FINISHED) {
        int prev = current_thread_id;

        // Prev is now waiting for thread to finish, block it
        thread_table[thread].joiner = prev;
        int next = dequeue();

        if (next == -1) {
            errno = EDEADLK;
            return -1;
        }

        thread_table[prev].state = THREAD_BLOCKED;

        // Run next thread
        current_thread_id = next;
        thread_table[next].state = THREAD_RUNNING;
        swapcontext(&thread_table[prev].context, &thread_table[next].context);
    }

    if (retval != NULL) {
        *retval = thread_table[thread].retval; // Return thread's return value to caller
    }

    free_thread(thread); // Clean up thread resources
    return 0;
}

void uthread_exit(void *retval) {
    if (!initialized) {
        exit(0); // If library not initialized, just exit process
    }

    int id = current_thread_id;
    thread_table[id].retval = retval;
    thread_table[id].state = THREAD_FINISHED;

    // If another thread is blocked waiting to join this one, wake that thread up by
    // putting it in the ready queue
    int joiner = thread_table[id].joiner;
    if (joiner != -1 && thread_table[joiner].state == THREAD_BLOCKED) {
        thread_table[joiner].state = THREAD_READY;
        enqueue(joiner);
    }

    int next = dequeue();

    if (next == -1) {
        exit(0); // No other threads ready, just exit process
    }

    // Set next thread up to run
    current_thread_id = next;
    thread_table[next].state = THREAD_RUNNING;

    // Switch to next thread context
    setcontext(&thread_table[next].context);

    // Don't return setcontext
    abort();
}

void uthread_detach(uthread_t thread) {
    if (!initialized || thread <= 0 || thread >= MAX_THREADS) {
        return;
    }
    // Cannot detach thread that doesn't exist
    if (thread_table[thread].state == THREAD_UNUSED) {
        return;
    }

    thread_table[thread].detached = 1;
    reap_detached_finished_threads();
}

void uthread_yield(void) {
    // Nothing to do if library has not been initialized
    if (!initialized) {
        return;
    } 

    // Clean up
    reap_detached_finished_threads();

    // Run current thread if no other threads are ready
    if (ready_queue_empty()) {
        return;
    }

    int prev = current_thread_id;

    // Put current thread back in ready queue first
    // Scheduler can choose b/t current thread and all other ready threads
    //  If current has highest priority, it may be selected again
    if (thread_table[prev].state == THREAD_RUNNING) {
        thread_table[prev].state = THREAD_READY;
        enqueue(prev);
    }

    int next = dequeue();

    current_thread_id = next;
    thread_table[next].state = THREAD_RUNNING;

    if (next == prev) {
        return;
    }

    // Save current thread context and switch to next thread context
    // When prev gets scheduled again, execution resumes after this call
    swapcontext(&thread_table[prev].context, &thread_table[next].context);
}
