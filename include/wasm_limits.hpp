#ifndef EMBWASM_WASM_LIMITS_HPP_
#define EMBWASM_WASM_LIMITS_HPP_

#include <cstdint>

namespace embwasm {

// WebAssembly Core Spec 2.0 の検証条件: |t*| ≤ 1000
static constexpr uint16_t kWasmMaxParamCount  = 1000;
static constexpr uint16_t kWasmMaxResultCount = 1000;

// ValidateFunctionBody で使用する上限値
static constexpr uint32_t kWasmValidationMaxLabelDepth = 0xFFFF;
static constexpr uint32_t kWasmValidationMaxStack      = 0xFFFF;
static constexpr uint32_t kWasmValidationMaxLocals     = 0xFFFF;

// EncodeFuncRef が下位16ビットに func_idx をエンコードするため、
// funcref として使える関数インデックスの上限（0x10000 未満）
static constexpr uint32_t kWasmMaxFuncRefIndex = 0xFFFF;

// メモリ load/store 命令のアクセスサイズ（バイト数）ルックアップテーブル
// index = op - 0x28 (load: 0x28..0x35), op - 0x36 (store: 0x36..0x3E)
static constexpr uint8_t kLoadSize[14]  = {4,8,4,8,1,1,2,2,1,1,2,2,4,4};
static constexpr uint8_t kStoreSize[9]  = {4,8,4,8,1,2,1,2,4};

} // namespace embwasm

#endif // EMBWASM_WASM_LIMITS_HPP_
