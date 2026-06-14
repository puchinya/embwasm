// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This FreeRTOS platform adapter has been designed and implemented entirely from
// scratch using FreeRTOS kernel API primitives.
// =============================================================================

#include "wasm_platform.hpp"
#include "FreeRTOS.hpp"
#include "task.hpp"

namespace embwasm {

uint32_t DisableInterrupts() noexcept {
    // FreeRTOS のタスクスケジューラを一時停止（ディスパッチ禁止）
    // 割り込み自体は許可されたまま、タスクのコンテキストスイッチが防止されます。
    vTaskSuspendAll();
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
    // FreeRTOS のタスクスケジューラを再開
    xTaskResumeAll();
}

} // namespace embwasm
