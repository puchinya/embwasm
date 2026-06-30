#include <stdint.h>
#include <stddef.h>

extern void* malloc(size_t size);
extern void  free(void* ptr);
extern void* realloc(void* ptr, size_t size);

__attribute__((export_name("cabi_realloc")))
void* cabi_realloc(void* old_ptr, size_t old_size, size_t align, size_t new_size) {
    (void)old_size; (void)align;
    if (new_size == 0) { free(old_ptr); return (void*)1; }
    if (old_ptr == NULL) return malloc(new_size);
    return realloc(old_ptr, new_size);
}

static int32_t fib(int32_t n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

__attribute__((export_name("thread_worker")))
int32_t thread_worker(int32_t n) {
    volatile char guard[64];
    int32_t i;
    for (i = 0; i < 64; ++i) guard[i] = (char)(n + i);
    int32_t result = fib(n);
    for (i = 0; i < 64; ++i)
        if (guard[i] != (char)(n + i)) return -1;
    return result;
}
