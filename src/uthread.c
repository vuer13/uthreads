#include "uthread.h"

int uthread_init(size_t stack_sz) {
    // TODO
    return 0;
}

int uthread_create(uthread_t *thread, void (*func)(void *), void *args) {
    // TODO
    return 0;
}

int uthread_join(uthread_t thread, void **retval) {
    // TODO
    return 0;
}

void uthread_exit(void *retval) {
    // TODO
}

void uthread_detach(uthread_t thread) {
    // TODO
}

void uthread_yield(void) {
    // TODO
}