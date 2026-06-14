#include "wasm_host_api.h"

// エクスポートされる関数
__attribute__((export_name("main")))
int main(void) {
    wasm_host_api_print_char('H');
    wasm_host_api_print_char('e');
    wasm_host_api_print_char('l');
    wasm_host_api_print_char('l');
    wasm_host_api_print_char('o');
    wasm_host_api_print_char('\n');
    return 0;
}

// wit-bindgen の cabi_realloc が依存する標準関数用の極小ダミー実装
void *realloc(void *ptr, size_t size) {
    (void)ptr; (void)size;
    return NULL;
}
void abort(void) {
    while (1) {}
}
