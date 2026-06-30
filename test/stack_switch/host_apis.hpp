#pragma once
#include "wasm_engine.hpp"

namespace embwasm {
void SSTestEnvInit(WasmEngine& engine) noexcept;
void SSTestEnvDeinit(WasmEngine& engine) noexcept;
} // namespace embwasm
