#include <stdlib.h>
#include "wasm_host_api.h"

// エクスポートされる関数
__attribute__((export_name("main")))
int main(void) {
    wasm_host_api_string_t s;
    s.ptr = (uint8_t*)"Hello";
    s.len = 5;
    wasm_host_api_puts(&s);
    return 0;
}


