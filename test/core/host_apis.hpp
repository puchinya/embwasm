#ifndef EMBWASM_TEST_HOST_APIS_HPP_
#define EMBWASM_TEST_HOST_APIS_HPP_

#include "wasm_types.hpp"

namespace embwasm {
class WasmEngine;

extern int g_test_env_init_called;
extern int g_test_env_deinit_called;

void TestEnvInit(WasmEngine& engine) noexcept;
void TestEnvDeinit(WasmEngine& engine) noexcept;

namespace hostmodules {
namespace embwasm {
namespace test {
namespace test_env {

WasmResult print_val(WasmEngine& engine, int32_t val) noexcept;
WasmResult print_char(WasmEngine& engine, int32_t character) noexcept;
WasmResult dummy(WasmEngine& engine) noexcept;

} // namespace test_env
} // namespace test
} // namespace embwasm
} // namespace hostmodules

} // namespace embwasm

#endif // EMBWASM_TEST_HOST_APIS_HPP_
