#ifndef EMBWASM_TEST_HOST_APIS_HPP_
#define EMBWASM_TEST_HOST_APIS_HPP_

#include "wasm_types.hpp"

namespace embwasm {
class WasmEngine;

WasmResult PrintVal(WasmEngine& engine, int32_t val) noexcept;
WasmResult PrintChar(WasmEngine& engine, int32_t character) noexcept;
WasmResult DummyHostFunc(WasmEngine& engine) noexcept;

extern int g_test_env_init_called;
extern int g_test_env_deinit_called;

void TestEnvInit(WasmEngine& engine) noexcept;
void TestEnvDeinit(WasmEngine& engine) noexcept;

} // namespace embwasm

#endif // EMBWASM_TEST_HOST_APIS_HPP_
