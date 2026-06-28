// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This Windows platform adapter has been designed and implemented entirely from
// scratch to mock embedded behavior on Windows test systems.
// =============================================================================

#include "wasm_engine.hpp"
#include "wasm_platform.hpp"
#include "wasm_types.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace embwasm {

struct WasmEnginePlatformData {
    HANDLE idle_event;
    CRITICAL_SECTION cs;
};

uint32_t DisableInterrupts() noexcept {
    // Windowsユーザー空間シミュレータ用モック：割り込み禁止は行いません
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
}

uint32_t PlatformGetTimeMs() noexcept {
    return static_cast<uint32_t>(GetTickCount64());
}

WasmResult PlatformEngineInit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(
        engine.GetMemoryPool()->Allocate(sizeof(WasmEnginePlatformData)));
    if (!d) return WasmResult::kErrorOutOfMemory;
    d->idle_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!d->idle_event) {
        engine.GetMemoryPool()->Free(d);
        return WasmResult::kErrorPlatformInit;
    }
    InitializeCriticalSection(&d->cs);
    engine.SetPlatformData(d);
    return WasmResult::kOk;
}

void PlatformEngineDeinit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    DeleteCriticalSection(&d->cs);
    if (d->idle_event) CloseHandle(d->idle_event);
    engine.GetMemoryPool()->Free(d);
    engine.SetPlatformData(nullptr);
}

WasmResult PlatformEngineRunBegin(WasmEngine& engine) noexcept {
    (void)engine;
    return WasmResult::kOk;
}

void PlatformEngineRunEnd(WasmEngine& engine) noexcept {
    (void)engine;
}

void PlatformWaitForActivity(WasmEngine& engine, uint32_t timeout_ms) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d || !d->idle_event) return;
    DWORD ms = (timeout_ms == UINT32_MAX) ? INFINITE : static_cast<DWORD>(timeout_ms);
    WaitForSingleObject(d->idle_event, ms);
}

void PlatformLock(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) EnterCriticalSection(&d->cs);
}

void PlatformUnlock(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) LeaveCriticalSection(&d->cs);
}

void PlatformNotifyActivity(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d && d->idle_event) SetEvent(d->idle_event);
}

} // namespace embwasm
