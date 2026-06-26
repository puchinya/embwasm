#include "embwasm_hostmodule_system.hpp"

namespace embwasm {
namespace hostmodules {
namespace sys {
namespace rt {
namespace system {

// [embwasm-proto:func:exit]
WasmResult exit(WasmEngine& engine, int32_t code) noexcept
{
    engine.SetExitCode(code);
    return WasmResult::kExit;
}

// [embwasm-proto:funcs-end]
} // namespace system
} // namespace rt
} // namespace sys
} // namespace hostmodules
} // namespace embwasm