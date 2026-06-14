#ifndef EMBWASM_TEST_HOST_APIS_HPP_
#define EMBWASM_TEST_HOST_APIS_HPP_

#include "wasm_types.hpp"

namespace embwasm {
class WasmEngine;

// 指定された数値をコンソールに出力するAPI
WasmResult PrintVal(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept;

// 指定された文字コードをコンソールに1文字出力するAPI
WasmResult PrintChar(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept;

// テスト用ダミーAPI
WasmResult DummyHostFunc(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept;

extern int g_test_env_init_called;
extern int g_test_env_deinit_called;

void TestEnvInit(WasmEngine& engine) noexcept;
void TestEnvDeinit(WasmEngine& engine) noexcept;

} // namespace embwasm

#endif // EMBWASM_TEST_HOST_APIS_HPP_
