#ifndef EMBWASM_HOSTMODULE_STDIO_HPP_
#define EMBWASM_HOSTMODULE_STDIO_HPP_

#include "embwasm.hpp"

namespace embwasm {
namespace hostmodules {
namespace sys {
namespace rt {
namespace stdio {

// [embwasm-proto:decl-begin]
WasmResult printf(
    WasmEngine& engine,
    const char* fmt,
    uint32_t fmt_len,
    const int32_t* args,
    uint32_t args_len) noexcept;

WasmResult puts(
    WasmEngine& engine,
    const char* s,
    uint32_t s_len,
    int32_t& out_result) noexcept;
// [embwasm-proto:decl-end]

} // namespace stdio
} // namespace rt
} // namespace sys
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_STDIO_HPP_
