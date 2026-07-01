#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "FreeRTOS.h"
#include "task.h"

extern void uart_putchar(char c);

ssize_t write(int fd, const void *buf, size_t count) {
    if (fd == 1 || fd == 2) {
        for (size_t i = 0; i < count; i++) uart_putchar(((const char *)buf)[i]);
        return (ssize_t)count;
    }
    return -1;
}

int gettimeofday(struct timeval *tp, void *tzp) {
    (void)tzp;
    if (tp) {
        TickType_t ticks = xTaskGetTickCount();
        tp->tv_sec  = ticks / configTICK_RATE_HZ;
        tp->tv_usec = (ticks % configTICK_RATE_HZ) * (1000000UL / configTICK_RATE_HZ);
    }
    return 0;
}
