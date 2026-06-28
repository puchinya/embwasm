// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This platform adapter uses C++ standard library primitives (std::mutex,
// std::condition_variable) to mock embedded behavior on PC environments
// (Windows, macOS, Linux). It replaces the separate Windows/macOS adapters.
// =============================================================================

#include "wasm_engine.hpp"
#include "wasm_platform.hpp"
#include "wasm_types.hpp"

#include <condition_variable>
#include <chrono>
#include <mutex>
#include <new>
#include <cstdint>

namespace embwasm {

struct WasmEnginePlatformData {
    std::mutex              mutex;
    std::condition_variable cond;
    bool                    notified = false;
};

uint32_t DisableInterrupts() noexcept {
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
}

uint32_t PlatformGetTimeMs() noexcept {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

WasmResult PlatformEngineInit(WasmEngine& engine) noexcept {
    void* mem = engine.GetMemoryPool()->Allocate(sizeof(WasmEnginePlatformData));
    if (!mem) return WasmResult::kErrorOutOfMemory;
    auto* d = new(mem) WasmEnginePlatformData{};
    engine.SetPlatformData(d);
    return WasmResult::kOk;
}

void PlatformEngineDeinit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    d->~WasmEnginePlatformData();
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
    if (!d) return;
    std::unique_lock<std::mutex> lk(d->mutex);
    if (timeout_ms == UINT32_MAX) {
        d->cond.wait(lk, [d] { return d->notified; });
    } else {
        d->cond.wait_for(lk, std::chrono::milliseconds(timeout_ms), [d] { return d->notified; });
    }
    d->notified = false;
}

void PlatformLock(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) d->mutex.lock();
}

void PlatformUnlock(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) d->mutex.unlock();
}

void PlatformNotifyActivity(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    {
        std::lock_guard<std::mutex> lk(d->mutex);
        d->notified = true;
    }
    d->cond.notify_one();
}

} // namespace embwasm
