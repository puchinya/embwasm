#ifndef EMBWASM_HOSTMODULE_THREAD_HPP_
#define EMBWASM_HOSTMODULE_THREAD_HPP_

#include "wasm_types.hpp"

#include "wasm_config.hpp"

namespace embwasm {
class WasmEngine;

namespace hostmodules {
namespace embwasm {
namespace threads {
namespace threads {

// [embwasm-proto:decl-begin]
#if EMBWASM_ENABLE_MULTITHREADING
WasmResult thread_spawn(WasmEngine& engine, const char* name, uint32_t name_len, int32_t& out_result) noexcept;
WasmResult thread_yield(WasmEngine& engine) noexcept;
WasmResult event_wait(WasmEngine& engine, int32_t event_id) noexcept;
WasmResult event_signal(WasmEngine& engine, int32_t event_id) noexcept;
#else
inline WasmResult thread_spawn(WasmEngine&, const char*, uint32_t, int32_t&) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult thread_yield(WasmEngine&) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult event_wait(WasmEngine&, int32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
inline WasmResult event_signal(WasmEngine&, int32_t) noexcept { return WasmResult::kErrorExecuteRuntimeError; }
#endif
// [embwasm-proto:decl-end]

} // namespace threads
} // namespace threads
} // namespace embwasm
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_THREAD_HPP_
