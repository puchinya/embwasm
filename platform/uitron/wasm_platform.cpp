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

uint32_t PlatformGetTimeMs() noexcept {
    SYSTIM tim;
    get_tim(&tim);
    return static_cast<uint32_t>(tim);
}

void PlatformWaitForActivity(uint32_t timeout_ms) noexcept {
    if (timeout_ms == UINT32_MAX) {
        slp_tsk();
    } else {
        tslp_tsk(static_cast<TMO>(timeout_ms));
    }
}

// スケジューラタスク ID（アプリ層で定義する）
extern ID g_scheduler_task_id;

void PlatformNotifyActivity() noexcept {
    iwup_tsk(g_scheduler_task_id);
}

} // namespace embwasm
