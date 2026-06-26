// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This FreeRTOS platform adapter has been designed and implemented entirely from
// scratch using FreeRTOS kernel API primitives.
// =============================================================================

#include "wasm_engine.hpp"
#include "wasm_platform.hpp"
#include "wasm_types.hpp"
#include "FreeRTOS.hpp"
#include "task.hpp"

namespace embwasm {

struct WasmEnginePlatformData {
    TaskHandle_t scheduler_task;
};

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

WasmResult PlatformEngineInit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(
        engine.GetMemoryPool()->Allocate(sizeof(WasmEnginePlatformData)));
    if (!d) return WasmResult::kErrorOutOfMemory;
    d->scheduler_task = nullptr;
    engine.SetPlatformData(d);
    return WasmResult::kOk;
}

void PlatformEngineDeinit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    engine.GetMemoryPool()->Free(d);
    engine.SetPlatformData(nullptr);
}

WasmResult PlatformEngineExecuteBegin(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return WasmResult::kErrorPlatformInit;
    d->scheduler_task = xTaskGetCurrentTaskHandle();
    if (!d->scheduler_task) return WasmResult::kErrorPlatformInit;
    return WasmResult::kOk;
}

void PlatformEngineExecuteEnd(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) d->scheduler_task = nullptr;
}

void PlatformWaitForActivity(WasmEngine& engine, uint32_t timeout_ms) noexcept {
    (void)engine;
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                   : pdMS_TO_TICKS(timeout_ms);
    ulTaskNotifyTake(pdTRUE, ticks);
}

void PlatformNotifyActivity(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d || !d->scheduler_task) return;
    BaseType_t higher_prio_woken = pdFALSE;
    vTaskNotifyGiveFromISR(d->scheduler_task, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

} // namespace embwasm
