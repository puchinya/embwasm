// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This macOS platform adapter has been designed and implemented entirely from
// scratch to mock embedded behavior on macOS test systems.
// =============================================================================

#include "wasm_engine.hpp"
#include "wasm_platform.hpp"
#include "wasm_types.hpp"

#include <pthread.h>
#include <time.h>
#include <cstdint>

namespace embwasm {

struct WasmEnginePlatformData {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            notified;
};

uint32_t DisableInterrupts() noexcept {
    // macOSユーザー空間シミュレータ用モック：割り込み禁止は行いません
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
}

uint32_t PlatformGetTimeMs() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(
        static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u);
}

WasmResult PlatformEngineInit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(
        engine.GetMemoryPool()->Allocate(sizeof(WasmEnginePlatformData)));
    if (!d) return WasmResult::kErrorOutOfMemory;
    if (pthread_mutex_init(&d->mutex, nullptr) != 0) {
        engine.GetMemoryPool()->Free(d);
        return WasmResult::kErrorPlatformInit;
    }
    if (pthread_cond_init(&d->cond, nullptr) != 0) {
        pthread_mutex_destroy(&d->mutex);
        engine.GetMemoryPool()->Free(d);
        return WasmResult::kErrorPlatformInit;
    }
    d->notified = false;
    engine.SetPlatformData(d);
    return WasmResult::kOk;
}

void PlatformEngineDeinit(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    pthread_cond_destroy(&d->cond);
    pthread_mutex_destroy(&d->mutex);
    engine.GetMemoryPool()->Free(d);
    engine.SetPlatformData(nullptr);
}

WasmResult PlatformEngineExecuteBegin(WasmEngine& engine) noexcept {
    (void)engine;
    return WasmResult::kOk;
}

void PlatformEngineExecuteEnd(WasmEngine& engine) noexcept {
    (void)engine;
}

void PlatformWaitForActivity(WasmEngine& engine, uint32_t timeout_ms) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    pthread_mutex_lock(&d->mutex);
    if (!d->notified) {
        if (timeout_ms == UINT32_MAX) {
            pthread_cond_wait(&d->cond, &d->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += static_cast<time_t>(timeout_ms / 1000u);
            ts.tv_nsec += static_cast<long>((timeout_ms % 1000u) * 1000000L);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&d->cond, &d->mutex, &ts);
        }
    }
    d->notified = false;
    pthread_mutex_unlock(&d->mutex);
}

void PlatformLock(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) pthread_mutex_lock(&d->mutex);
}

void PlatformUnlock(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (d) pthread_mutex_unlock(&d->mutex);
}

void PlatformNotifyActivity(WasmEngine& engine) noexcept {
    auto* d = static_cast<WasmEnginePlatformData*>(engine.GetPlatformData());
    if (!d) return;
    pthread_mutex_lock(&d->mutex);
    d->notified = true;
    pthread_cond_signal(&d->cond);
    pthread_mutex_unlock(&d->mutex);
}

} // namespace embwasm
