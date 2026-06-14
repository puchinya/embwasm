#ifndef EMBWASM_TEST_HOST_APIS_HPP_
#define EMBWASM_TEST_HOST_APIS_HPP_

#include "wasm_types.hpp"

namespace embwasm {

// 指定された数値をコンソールに出力するAPI
WasmResult PrintVal(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept;

// 指定された文字コードをコンソールに1文字出力するAPI
WasmResult PrintChar(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept;

// テスト用ダミーAPI
WasmResult DummyHostFunc(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept;

} // namespace embwasm

#endif // EMBWASM_TEST_HOST_APIS_HPP_
