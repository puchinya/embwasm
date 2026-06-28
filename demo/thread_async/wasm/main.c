#include <stdlib.h>
#include "wasm_host_api.h"

// ホストから PushThreadArg で渡された n 回ループして出力する非同期タスク
__attribute__((export_name("async_task")))
void async_task(int32_t n) {
    wasm_host_api_string_t msg;
    msg.ptr = (uint8_t*)"AsyncTask: start";
    msg.len = 16;
    sys_rt_stdio_puts(&msg);

    for (int32_t i = 0; i < n; ++i) {
        wasm_host_api_string_t fmt;
        fmt.ptr = (uint8_t*)"AsyncTask: step %d\n";
        fmt.len = 19;
        int32_t arg_data[1] = { i };
        wasm_host_api_list_s32_t args;
        args.ptr = arg_data;
        args.len = 1;
        sys_rt_stdio_printf(&fmt, &args);
    }

    msg.ptr = (uint8_t*)"AsyncTask: done";
    msg.len = 15;
    sys_rt_stdio_puts(&msg);
}
