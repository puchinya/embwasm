#include "wasm_api.h"

// エクスポートされる関数
__attribute__((export_name("main")))
void main(void) {
    print_char('H');
    print_char('e');
    print_char('l');
    print_char('l');
    print_char('o');
    print_char('\n');
    print_val(100);
}
