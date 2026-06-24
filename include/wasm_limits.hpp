#ifndef EMBWASM_WASM_LIMITS_HPP_
#define EMBWASM_WASM_LIMITS_HPP_

#include <cstdint>

namespace embwasm {

// WebAssembly Core Spec 2.0 の検証条件: |t*| ≤ 1000
static constexpr uint16_t kWasmMaxParamCount  = 1000;
static constexpr uint16_t kWasmMaxResultCount = 1000;

// ValidateFunctionBody で使用する上限値
static constexpr uint32_t kWasmValidationMaxLabelDepth = 256;
static constexpr uint32_t kWasmValidationMaxStack      = 1024;
static constexpr uint32_t kWasmValidationMaxLocals     = 1024;

} // namespace embwasm

#endif // EMBWASM_WASM_LIMITS_HPP_
