// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This uITRON platform adapter has been designed and implemented entirely from
// scratch using uITRON standard kernel API primitives.
// =============================================================================

#include "wasm_engine.hpp"
#include "wasm_platform.hpp"
#include "wasm_types.hpp"
#include <kernel.h>

namespace embwasm {

struct WasmEnginePlatformData {
    ID scheduler_task_id;
};

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

WasmResult PlatformEngineInit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(
        engine.GetMemoryPool()->Allocate(sizeof(WasmEnginePlatformData)));
    if (!d) return WasmResult::kErrorOutOfMemory;
    d->scheduler_task_id = 0;
    engine.SetPlatformData(d);
    return WasmResult::kOk;
}

void PlatformEngineDeinit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    engine.GetMemoryPool()->Free(d);
    engine.SetPlatformData(nullptr);
}

WasmResult PlatformEngineRunBegin(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return WasmResult::kErrorPlatformInit;
    ER err = get_tid(&d->scheduler_task_id);
    if (err != E_OK) return WasmResult::kErrorPlatformInit;
    return WasmResult::kOk;
}

void PlatformEngineRunEnd(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) d->scheduler_task_id = 0;
}

void PlatformWaitForActivity(WasmEngine& engine, uint32_t timeout_ms) noexcept {
    (void)engine;
    if (timeout_ms == UINT32_MAX) {
        slp_tsk();
    } else {
        tslp_tsk(static_cast<TMO>(timeout_ms));
    }
}

void PlatformLock(WasmEngine& engine) noexcept {
    (void)engine;
    dis_dsp();
}

void PlatformUnlock(WasmEngine& engine) noexcept {
    (void)engine;
    ena_dsp();
}

void PlatformNotifyActivity(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d || d->scheduler_task_id == 0) return;
    iwup_tsk(d->scheduler_task_id);
}

} // namespace embwasm
