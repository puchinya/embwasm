// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This Windows platform adapter has been designed and implemented entirely from
// scratch to mock embedded behavior on Windows test systems.
// =============================================================================

#include "wasm_platform.hpp"

namespace embwasm {

uint32_t DisableInterrupts() noexcept {
    // Windowsユーザー空間シミュレータ用モック：割り込み禁止は行いません
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
}

} // namespace embwasm
