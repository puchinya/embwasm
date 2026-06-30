#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

extern void uart_putchar(char c);

int _write(int fd, const char *buf, int len) {
    if (fd == 1 || fd == 2) {
        for (int i = 0; i < len; i++) uart_putchar(buf[i]);
        return len;
    }
    return -1;
}

static uint8_t s_heap[32768];
static uint8_t *s_heap_ptr = s_heap;

void *_sbrk(intptr_t incr) {
    uint8_t *prev = s_heap_ptr;
    if (s_heap_ptr + incr > s_heap + sizeof(s_heap)) {
        errno = ENOMEM;
        return (void*)-1;
    }
    s_heap_ptr += incr;
    return prev;
}

int _read(int fd, char *buf, int len)  { (void)fd; (void)buf; (void)len; return -1; }
int _close(int fd)                     { (void)fd; return -1; }
int _lseek(int fd, int off, int w)    { (void)fd; (void)off; (void)w; return -1; }
int _fstat(int fd, struct stat *st)   { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                   { (void)fd; return 1; }
void _exit(int status)                { (void)status; while (1) {} }
int _kill(int pid, int sig)           { (void)pid; (void)sig; return -1; }
int _getpid(void)                     { return 1; }
