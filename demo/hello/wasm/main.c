#include <stdlib.h>
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

