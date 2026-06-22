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

uint32_t PlatformGetTimeMs() noexcept {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// スケジューラタスクハンドル（アプリ層で定義する）
extern TaskHandle_t g_scheduler_task;

void PlatformWaitForActivity(uint32_t timeout_ms) noexcept {
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                   : pdMS_TO_TICKS(timeout_ms);
    ulTaskNotifyTake(pdTRUE, ticks);
}

void PlatformNotifyActivity() noexcept {
    BaseType_t higher_prio_woken = pdFALSE;
    vTaskNotifyGiveFromISR(g_scheduler_task, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

} // namespace embwasm
