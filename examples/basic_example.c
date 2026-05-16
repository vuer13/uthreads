#include "uthread.h"

#include <stdio.h>

void *worker(void *arg) {
    (void)arg;
    return NULL;
}

int main(void) {
    uthread_init(64 * 1024);

    uthread_t t1;
    uthread_t t2;

    if (uthread_create(&t1, worker, NULL) == -1) {
        perror("uthread_create t1");
        return 1;
    }

    if (uthread_create(&t2, worker, NULL) == -1) {
        perror("uthread_create t2");
        return 1;
    }

    printf("created threads: %d, %d\n", t1, t2);

    return 0;
}