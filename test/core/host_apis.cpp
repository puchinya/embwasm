#include "host_apis.hpp"

namespace embwasm {

int32_t g_last_printed_value = 0;
bool g_print_val_called = false;

char g_last_printed_char = 0;
bool g_print_char_called = false;

int g_test_env_init_called = 0;
int g_test_env_deinit_called = 0;

void TestEnvInit(WasmEngine& engine) noexcept {
    (void)engine;
    g_test_env_init_called++;
}

void TestEnvDeinit(WasmEngine& engine) noexcept {
    (void)engine;
    g_test_env_deinit_called++;
}

namespace hostmodules {
namespace embwasm {
namespace test {
namespace test_env {

WasmResult print_val(WasmEngine& engine, int32_t val) noexcept {
    (void)engine;
    ::embwasm::g_last_printed_value = val;
    ::embwasm::g_print_val_called = true;
    return WasmResult::kOk;
}

WasmResult print_char(WasmEngine& engine, int32_t character) noexcept {
    (void)engine;
    ::embwasm::g_last_printed_char = static_cast<char>(character);
    ::embwasm::g_print_char_called = true;
    return WasmResult::kOk;
}

WasmResult dummy(WasmEngine& engine) noexcept {
    (void)engine;
    return WasmResult::kOk;
}

} // namespace test_env
} // namespace test
} // namespace embwasm
} // namespace hostmodules

} // namespace embwasm
