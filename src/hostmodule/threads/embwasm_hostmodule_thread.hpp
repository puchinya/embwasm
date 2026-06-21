#ifndef EMBWASM_HOSTMODULE_THREAD_HPP_
#define EMBWASM_HOSTMODULE_THREAD_HPP_

#include "wasm_types.hpp"

#include "wasm_config.hpp"

namespace embwasm {
class WasmEngine;

namespace hostmodules {
namespace thread {

#if EMBWASM_ENABLE_MULTITHREADING
WasmResult ThreadSpawn(WasmEngine& engine, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;
WasmResult ThreadYield(WasmEngine& engine, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;
WasmResult EventWait(WasmEngine& engine, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;
WasmResult EventSignal(WasmEngine& engine, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;
#else
// マルチスレッド無効時はエラーを返すダミー実装をインラインまたは実装ファイルで提供する
inline WasmResult ThreadSpawn(WasmEngine&, const WasmValue*, uint32_t, WasmValue*, uint32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult ThreadYield(WasmEngine&, const WasmValue*, uint32_t, WasmValue*, uint32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult EventWait(WasmEngine&, const WasmValue*, uint32_t, WasmValue*, uint32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult EventSignal(WasmEngine&, const WasmValue*, uint32_t, WasmValue*, uint32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
#endif

} // namespace thread
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_THREAD_HPP_
