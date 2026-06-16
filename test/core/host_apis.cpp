#include "host_apis.hpp"

namespace embwasm {

// テスト用グローバル状態の定義
int32_t g_last_printed_value = 0;
bool g_print_val_called = false;

char g_last_printed_char = 0;
bool g_print_char_called = false;

WasmResult PrintVal(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept 
{
    (void)engine;
    (void)results;
    (void)result_count;

    if (arg_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    g_last_printed_value = args[0].value.i32;
    g_print_val_called = true;
    return WasmResult::kOk;
}

WasmResult PrintChar(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept 
{
    (void)engine;
    (void)results;
    (void)result_count;

    if (arg_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    g_last_printed_char = static_cast<char>(args[0].value.i32);
    g_print_char_called = true;
    return WasmResult::kOk;
}

WasmResult DummyHostFunc(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept 
{
    (void)engine;
    (void)args;
    (void)arg_count;
    (void)results;
    (void)result_count;
    return WasmResult::kOk;
}

int g_test_env_init_called = 0;
int g_test_env_deinit_called = 0;

void TestEnvInit(WasmEngine& engine) noexcept {
    (void)engine;
    g_test_env_init_called++;
}

void TestEnvDeinit(WasmEngine& engine) noexcept {
    (void)engine;
    g_test_env_deinit_called++;
}

} // namespace embwasm
