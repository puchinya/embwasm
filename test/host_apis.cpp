#include "host_apis.h"

namespace embwasm {

// テスト用グローバル状態の定義
int32_t g_last_printed_value = 0;
bool g_print_val_called = false;

char g_last_printed_char = 0;
bool g_print_char_called = false;

WasmResult PrintVal(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results;
    (void)result_count;
    (void)user_data;

    if (arg_count < 1 || args[0].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    g_last_printed_value = args[0].value.i32;
    g_print_val_called = true;
    return WasmResult::kOk;
}

WasmResult PrintChar(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results;
    (void)result_count;
    (void)user_data;

    if (arg_count < 1 || args[0].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    g_last_printed_char = static_cast<char>(args[0].value.i32);
    g_print_char_called = true;
    return WasmResult::kOk;
}

WasmResult DummyHostFunc(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)args;
    (void)arg_count;
    (void)results;
    (void)result_count;
    (void)user_data;
    return WasmResult::kOk;
}

} // namespace embwasm
