#ifndef UTHREAD_H
#define UTHREAD_H

#include <stddef.h>

// Public thread ID type - user recieves an integer ID representing user-level thread
typedef int uthread_t;

// Initialize the threading library 
// Stack size for each thread; if 0 will use a default size
int uthread_init(size_t stack_sz);

// Creates a new user-level thread
// thread: output parameter for thread ID to be stored
// func: function to run
// args: arguments for function
int uthread_create(uthread_t *thread, void (*func)(void *), void *args);

// Wait for another thread to finish
// retval: if not NULL, will store the return value of the thread
int uthread_join(uthread_t thread, void **retval);

// Exit current thread
void uthread_exit(void *retval);

// Detach thread, cannot be joined
void uthread_detach(uthread_t thread);

// Give up control for another ready thread
void uthread_yield(void);

#endif