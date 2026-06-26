#ifndef EMBWASM_HOSTMODULE_SYSTEM_HPP_
#define EMBWASM_HOSTMODULE_SYSTEM_HPP_

#include "embwasm.hpp"

namespace embwasm {
namespace hostmodules {
namespace sys {
namespace rt {
namespace system {

// [embwasm-proto:decl-begin]
WasmResult exit(WasmEngine& engine, int32_t code) noexcept;
// [embwasm-proto:decl-end]

} // namespace system
} // namespace rt
} // namespace sys
} // namespace hostmodules
} // namespace embwasm

#endif // EMBWASM_HOSTMODULE_SYSTEM_HPP_