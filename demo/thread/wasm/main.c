#include "wasm_host_api.h"

// スレッド2の本体
__attribute__((export_name("thread2")))
void thread2(void) {
    for (int i = 0; i < 3; ++i) {
        wasm_host_api_print_char('T');
        wasm_host_api_print_char('2');
        wasm_host_api_print_char(':');
        wasm_host_api_print_val(i);
        wasm_host_api_print_char('\n');
        wasm_host_api_thread_yield();
    }
    // イベント1をシグナルして終了
    wasm_host_api_event_signal(1);
}

// エクスポートされるメイン関数
__attribute__((export_name("main")))
int main(void) {
    wasm_host_api_print_char('M');
    wasm_host_api_print_char('a');
    wasm_host_api_print_char('i');
    wasm_host_api_print_char('n');
    wasm_host_api_print_char('\n');

    // スレッド2を起動 (名前で指定)
    wasm_host_api_string_t thread_name;
    thread_name.ptr = (uint8_t*)"thread2";
    thread_name.len = 7;
    wasm_host_api_thread_spawn(&thread_name);

    for (int i = 0; i < 3; ++i) {
        wasm_host_api_print_char('M');
        wasm_host_api_print_char('1');
        wasm_host_api_print_char(':');
        wasm_host_api_print_val(i);
        wasm_host_api_print_char('\n');
        wasm_host_api_thread_yield();
    }

    // スレッド2の終了を待つ
    wasm_host_api_print_char('W');
    wasm_host_api_print_char('a');
    wasm_host_api_print_char('i');
    wasm_host_api_print_char('t');
    wasm_host_api_print_char('\n');
    wasm_host_api_event_wait(1);

    wasm_host_api_print_char('D');
    wasm_host_api_print_char('o');
    wasm_host_api_print_char('n');
    wasm_host_api_print_char('e');
    wasm_host_api_print_char('\n');
    return 0;
}

// wit-bindgen の cabi_realloc が依存する標準関数用の極小ダミー実装
void *malloc(size_t size) {
    (void)size;
    return NULL;
}
void *realloc(void *ptr, size_t size) {
    (void)ptr; (void)size;
    return NULL;
}
void free(void *ptr) {
    (void)ptr;
}
void abort(void) {
    while (1) {}
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}
