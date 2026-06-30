#include <unistd.h>
#include <sys/stat.h>

extern void uart_putchar(char c);

ssize_t write(int fd, const void *buf, size_t count) {
    if (fd == 1 || fd == 2) {
        for (size_t i = 0; i < count; i++) uart_putchar(((const char *)buf)[i]);
        return (ssize_t)count;
    }
    return -1;
}

void _exit(int status) { (void)status; while (1) {} }
