#include <stdlib.h>
#include <string.h>
#include "wasm_host_api.h"

// スレッド2の本体
__attribute__((export_name("thread2")))
void thread2(void) {
    for (int i = 0; i < 3; ++i) {
        wasm_host_api_string_t fmt;
        fmt.ptr = (uint8_t*)"T2:%d\n";
        fmt.len = 6;

        int32_t arg_data[1] = { i };
        wasm_host_api_list_s32_t args;
        args.ptr = arg_data;
        args.len = 1;

        embwasm_stdio_stdio_printf(&fmt, &args);
        embwasm_threads_threads_thread_yield();
    }
    // イベント1をシグナルして終了
    embwasm_threads_threads_event_signal(1);
}

// エクスポートされるメイン関数
__attribute__((export_name("main")))
int main(void) {
    wasm_host_api_string_t msg;
    msg.ptr = (uint8_t*)"Main";
    msg.len = 4;
    embwasm_stdio_stdio_puts(&msg);

    // スレッド2を起動 (名前で指定)
    wasm_host_api_string_t thread_name;
    thread_name.ptr = (uint8_t*)"thread2";
    thread_name.len = 7;
    embwasm_threads_threads_thread_spawn(&thread_name);

    for (int i = 0; i < 3; ++i) {
        wasm_host_api_string_t fmt;
        fmt.ptr = (uint8_t*)"M1:%d\n";
        fmt.len = 6;

        int32_t arg_data[1] = { i };
        wasm_host_api_list_s32_t args;
        args.ptr = arg_data;
        args.len = 1;

        embwasm_stdio_stdio_printf(&fmt, &args);
        embwasm_threads_threads_thread_yield();
    }

    // スレッド2の終了を待つ
    msg.ptr = (uint8_t*)"Wait";
    msg.len = 4;
    embwasm_stdio_stdio_puts(&msg);
    embwasm_threads_threads_event_wait(1);

    msg.ptr = (uint8_t*)"Done";
    msg.len = 4;
    embwasm_stdio_stdio_puts(&msg);
    return 0;
}


