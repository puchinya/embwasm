// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This macOS platform adapter has been designed and implemented entirely from
// scratch to mock embedded behavior on macOS test systems.
// =============================================================================

#include "wasm_platform.hpp"

#include <pthread.h>
#include <time.h>
#include <cstdint>

namespace embwasm {

uint32_t DisableInterrupts() noexcept {
    // macOSユーザー空間シミュレータ用モック：割り込み禁止は行いません
    return 0;
}

void RestoreInterrupts(uint32_t primask_val) noexcept {
    (void)primask_val;
}

// ---------------------------------------------------------------------------
// アクティビティ通知用 condvar（全スレッド kWaiting 時のネイティブスリープに使用）
// ---------------------------------------------------------------------------

static pthread_mutex_t g_idle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_idle_cond  = PTHREAD_COND_INITIALIZER;
static bool            g_notified   = false;

uint32_t PlatformGetTimeMs() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(
        static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u);
}

void PlatformWaitForActivity(uint32_t timeout_ms) noexcept {
    pthread_mutex_lock(&g_idle_mutex);
    if (!g_notified) {
        if (timeout_ms == UINT32_MAX) {
            pthread_cond_wait(&g_idle_cond, &g_idle_mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += static_cast<time_t>(timeout_ms / 1000u);
            ts.tv_nsec += static_cast<long>((timeout_ms % 1000u) * 1000000L);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&g_idle_cond, &g_idle_mutex, &ts);
        }
    }
    g_notified = false;
    pthread_mutex_unlock(&g_idle_mutex);
}

void PlatformNotifyActivity() noexcept {
    pthread_mutex_lock(&g_idle_mutex);
    g_notified = true;
    pthread_cond_signal(&g_idle_cond);
    pthread_mutex_unlock(&g_idle_mutex);
}

} // namespace embwasm
