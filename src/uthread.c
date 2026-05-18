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

// FIFO ready queue for scheduling threads
static int ready_queue[MAX_THREADS];
static int ready_queue_head = 0;
static int ready_queue_tail = 0;
static int ready_queue_count = 0;

static int ready_queue_empty(void) {
    return ready_queue_count == 0;
}

static void enqueue(int id) {
    if (ready_queue_count >= MAX_THREADS) {
        abort(); // Should never happen
    }

    ready_queue[ready_queue_tail] = id;
    ready_queue_tail = (ready_queue_tail + 1) % MAX_THREADS;
    ready_queue_count++;
}

static int dequeue(void) {
    if (ready_queue_empty()) {
        return -1; // No threads ready
    }

    int id = ready_queue[ready_queue_head];
    ready_queue_head = (ready_queue_head + 1) % MAX_THREADS;
    ready_queue_count--;

    return id;
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

int uthread_init(size_t stack_sz) {
    if (initialized) {
        return 0; // Already initialized
    }
    if (stack_sz > 0) {
        thread_stack_size = stack_sz;
    }

    // Clear thread table
    memset(thread_table, 0, sizeof(thread_table));
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
    current_thread_id = MAIN_THREAD_ID;
    initialized = 1;

    return 0;
}

int uthread_create(uthread_t *thread, void *(*func)(void *), void *args) {
    if (!initialized) {
        if (uthread_init(DEFAULT_STACK_SIZE) == -1) {
            return -1; // Failed to initialize
        }
    }
    
    if (thread == NULL || func == NULL) {
        errno = EINVAL;
        return -1; // Invalid arguments
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
    // TODO
    return 0;
}

void uthread_exit(void *retval) {
    if (!initialized) {
        exit(0); // If library not initialized, just exit process
    }

    int id = current_thread_id;
    thread_table[id].retval = retval;
    thread_table[id].state = THREAD_FINISHED;
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
    // TODO
}

void uthread_yield(void) {
    // Nothing to do if library has not been initialized
    if (!initialized) {
        return;
    } 

    // Run current thread if no other threads are ready
    if (ready_queue_empty()) {
        return;
    }

    int prev = current_thread_id;
    int next = dequeue();

    if (thread_table[prev].state == THREAD_RUNNING) {
        thread_table[prev].state = THREAD_READY;
        enqueue(prev); // Put current thread back in ready queue
    }

    current_thread_id = next;
    thread_table[next].state = THREAD_RUNNING;

    // Save current thread context and switch to next thread context
    // When prev gets scheduled again, execution resumes after this call
    swapcontext(&thread_table[prev].context, &thread_table[next].context);
}