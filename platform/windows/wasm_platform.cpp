// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This Windows platform adapter has been designed and implemented entirely from
// scratch to mock embedded behavior on Windows test systems.
// =============================================================================

#include "wasm_platform.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace embwasm {

// 自動リセットイベント（PlatformWaitForActivity / PlatformNotifyActivity で使用）
static HANDLE g_idle_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

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

void PlatformWaitForActivity(uint32_t timeout_ms) noexcept {
    DWORD ms = (timeout_ms == UINT32_MAX) ? INFINITE : static_cast<DWORD>(timeout_ms);
    WaitForSingleObject(g_idle_event, ms);
}

void PlatformNotifyActivity() noexcept {
    SetEvent(g_idle_event);
}

} // namespace embwasm
