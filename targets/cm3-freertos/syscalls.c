#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

extern void uart_putchar(char c);

ssize_t write(int fd, const void *buf, size_t count) {
    if (fd == 1 || fd == 2) {
        for (size_t i = 0; i < count; i++) uart_putchar(((const char *)buf)[i]);
        return (ssize_t)count;
    }
    return -1;
}

/* No hardware clock on bare metal — chrono will show 0 */
int gettimeofday(struct timeval *tp, void *tzp) {
    (void)tzp;
    if (tp) { tp->tv_sec = 0; tp->tv_usec = 0; }
    return 0;
}
