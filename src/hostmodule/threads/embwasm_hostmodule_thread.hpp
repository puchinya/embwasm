#ifndef EMBWASM_HOSTMODULE_THREAD_HPP_
#define EMBWASM_HOSTMODULE_THREAD_HPP_

#include "wasm_types.hpp"

#include "wasm_config.hpp"

namespace embwasm {
namespace hostmodules {
namespace thread {

#if EMBWASM_ENABLE_MULTITHREADING
WasmResult ThreadSpawn(const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count, void* user_data) noexcept;
WasmResult ThreadYield(const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count, void* user_data) noexcept;
WasmResult EventWait(const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count, void* user_data) noexcept;
WasmResult EventSignal(const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count, void* user_data) noexcept;
#else
// マルチスレッド無効時はエラーを返すダミー実装をインラインまたは実装ファイルで提供する
inline WasmResult ThreadSpawn(const WasmValue*, uint32_t, WasmValue*, uint32_t, void*) noexcept { return WasmResult::kErrorRuntimeError; }
inline WasmResult ThreadYield(const WasmValue*, uint32_t, WasmValue*, uint32_t, void*) noexcept { return WasmResult::kErrorRuntimeError; }
inline WasmResult EventWait(const WasmValue*, uint32_t, WasmValue*, uint32_t, void*) noexcept { return WasmResult::kErrorRuntimeError; }
inline WasmResult EventSignal(const WasmValue*, uint32_t, WasmValue*, uint32_t, void*) noexcept { return WasmResult::kErrorRuntimeError; }
#endif

} // namespace thread
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_THREAD_HPP_
