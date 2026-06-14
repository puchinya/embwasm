#ifndef EMBWASM_HOSTMODULE_STDIO_HPP_
#define EMBWASM_HOSTMODULE_STDIO_HPP_

#include "wasm_types.hpp"
#include "wasm_engine.hpp"

namespace embwasm {
namespace hostmodules {
namespace stdio {

// printf のホストAPI実装
WasmResult Printf(
    WasmEngine& engine,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept;

// puts のホストAPI実装
WasmResult Puts(
    WasmEngine& engine,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept;

} // namespace stdio
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_STDIO_HPP_
