#ifndef EMBWASM_HOSTMODULE_STDIO_HPP_
#define EMBWASM_HOSTMODULE_STDIO_HPP_

#include "wasm_types.hpp"
#include "wasm_engine.hpp"

namespace embwasm {
namespace hostmodules {
namespace stdio {

WasmResult Printf(
    WasmEngine& engine,
    const char* fmt,
    uint32_t fmt_len,
    const int32_t* args,
    uint32_t args_len) noexcept;

WasmResult Puts(
    WasmEngine& engine,
    const char* s,
    uint32_t s_len,
    int32_t& out_result) noexcept;

} // namespace stdio
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_STDIO_HPP_
