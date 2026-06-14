#include <iostream>
#include "host_apis.hpp"

namespace embwasm {

// ホストAPI：WASM内から数値を表示するための関数
WasmResult PrintVal(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results; (void)result_count; (void)user_data;

    if (arg_count < 1 || args[0].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    // デモ用表示
    std::cout << "[Host API env.print_val] Output: " << args[0].value.i32 << std::endl;
    return WasmResult::kOk;
}

// ホストAPI：WASM内から文字コードを出力するための関数（putcharに相当）
WasmResult PrintChar(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results; (void)result_count; (void)user_data;

    if (arg_count < 1 || args[0].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    // 文字コードを1文字出力
    std::cout << static_cast<char>(args[0].value.i32);
    return WasmResult::kOk;
}

// ホストAPI：WASM内から数値を表示するための関数（デモ用 print）
WasmResult Print(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results; (void)result_count; (void)user_data;

    if (arg_count < 1 || args[0].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    // デモ用表示
    std::cout << "[Host API env.print] Output: " << args[0].value.i32 << std::endl;
    return WasmResult::kOk;
}

} // namespace embwasm
