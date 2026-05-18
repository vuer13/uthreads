#include "uthread.h"

#include <stdio.h>

void *worker(void *arg) {
    char *name = arg;

    for (int i = 0; i < 3; i++) {
        printf("worker %s: %d\n", name, i);
        uthread_yield();
    }

    printf("worker %s finished\n", name);

    return "done";
}

int main(void) {
    uthread_init(64 * 1024);

    uthread_t t1;
    uthread_t t2;

    uthread_create(&t1, worker, "A");
    uthread_create(&t2, worker, "B");

    void *ret1;
    void *ret2;

    uthread_join(t1, &ret1);
    uthread_join(t2, &ret2);

    printf("t1 returned: %s\n", (char *)ret1);
    printf("t2 returned: %s\n", (char *)ret2);

    printf("main done\n");

    return 0;
}