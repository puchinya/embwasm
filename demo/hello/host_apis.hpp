#ifndef EMBWASM_DEMO_HOST_APIS_HPP_
#define EMBWASM_DEMO_HOST_APIS_HPP_

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

// 指定された数値をコンソールに出力するAPI（デモ用 print）
WasmResult Print(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept;

} // namespace embwasm

#endif // EMBWASM_DEMO_HOST_APIS_HPP_
