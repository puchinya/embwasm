// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This uITRON platform adapter has been designed and implemented entirely from
// scratch using uITRON standard kernel API primitives.
// =============================================================================

#include "wasm_platform.hpp"
#include <kernel.h>

namespace embwasm {

uint32_t DisableInterrupts() noexcept {
    // uITRON 標準のディスパッチ禁止状態へ移行します。
    // 割り込み自体は発生しますが、タスクの切り替え（ディスパッチ）は防止されます。
    dis_dsp();
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
    // uITRON 標準のディスパッチ許可状態へ復元します。
    ena_dsp();
}

} // namespace embwasm
