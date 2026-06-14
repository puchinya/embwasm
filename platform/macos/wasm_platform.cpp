// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This macOS platform adapter has been designed and implemented entirely from
// scratch to mock embedded behavior on macOS test systems.
// =============================================================================

#include "wasm_platform.h"

namespace embwasm {

uint32_t DisableInterrupts() noexcept {
    // macOSユーザー空間シミュレータ用モック：割り込み禁止は行いません
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
}

} // namespace embwasm
