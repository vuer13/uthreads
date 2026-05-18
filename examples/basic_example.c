#include "uthread.h"

#include <stdio.h>

void *worker(void *arg) {
    char *name = arg;

    printf("worker %s started\n", name);

    printf("worker %s yielding once\n", name);
    uthread_yield();

    printf("worker %s finished\n", name);

    return NULL;
}

int main(void) {
    uthread_init(64 * 1024);

    uthread_t t1;
    uthread_t t2;

    uthread_create(&t1, worker, "A");
    uthread_create(&t2, worker, "B");

    printf("main yield 1\n");
    uthread_yield();

    printf("main yield 2\n");
    uthread_yield();

    printf("main yield 3\n");
    uthread_yield();

    printf("main yield 4\n");
    uthread_yield();

    printf("main yield 5\n");
    uthread_yield();

    printf("main done\n");

    return 0;
}
