#include "uthread.h"

#include <stdio.h>

int main(void) {
    if (uthread_init(64 * 1024) == 0) {
        printf("uthread library initialized\n");
    }

    return 0;
}