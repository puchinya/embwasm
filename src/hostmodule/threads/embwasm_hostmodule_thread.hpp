#ifndef EMBWASM_HOSTMODULE_THREAD_HPP_
#define EMBWASM_HOSTMODULE_THREAD_HPP_

#include "wasm_types.hpp"

#include "wasm_config.hpp"

namespace embwasm {
class WasmEngine;

namespace hostmodules {
namespace thread {

#if EMBWASM_ENABLE_MULTITHREADING
WasmResult ThreadSpawn(WasmEngine& engine, const char* name, uint32_t name_len, int32_t& out_result) noexcept;
WasmResult ThreadYield(WasmEngine& engine) noexcept;
WasmResult EventWait(WasmEngine& engine, int32_t event_id) noexcept;
WasmResult EventSignal(WasmEngine& engine, int32_t event_id) noexcept;
#else
inline WasmResult ThreadSpawn(WasmEngine&, const char*, uint32_t, int32_t&) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult ThreadYield(WasmEngine&) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult EventWait(WasmEngine&, int32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult EventSignal(WasmEngine&, int32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
#endif

} // namespace thread
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_THREAD_HPP_
