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

    uthread_t low;
    uthread_t high;

    uthread_create(&low, worker, "LOW", 1);
    uthread_create(&high, worker, "HIGH", 9);

    void *ret_low;
    void *ret_high;

    uthread_join(low, &ret_low);
    uthread_join(high, &ret_high);

    printf("low returned: %s\n", (char *)ret_low);
    printf("high returned: %s\n", (char *)ret_high);

    return 0;
}
