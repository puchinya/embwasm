#include <stdlib.h>
#include <string.h>
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

