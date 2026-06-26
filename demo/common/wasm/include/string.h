#ifndef EMBWASM_COMMON_WASM_STRING_H_
#define EMBWASM_COMMON_WASM_STRING_H_

#include <stddef.h>

size_t strlen(const char *s);
static void *memcpy(void *dest, const void *src, size_t n) {
    return __builtin_memcpy(dest, src, n);
}

#endif // EMBWASM_COMMON_WASM_STRING_H_
