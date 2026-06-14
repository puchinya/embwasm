// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This static memory pool has been designed and implemented entirely from scratch
// to guarantee deterministic allocation on bare-metal systems without relying 
// on standard dynamic heap allocators or external libraries.
// =============================================================================

#include "wasm_memory_pool.h"
#include "wasm_platform.h"

namespace embwasm {

// アライメントを保証した静的バッファの実体定義
alignas(alignof(std::max_align_t)) uint8_t WasmMemoryPool::buffer_[kMemoryPoolSize];

WasmMemoryPool::WasmMemoryPool() noexcept : offset_(0) {}

void* WasmMemoryPool::Allocate(std::size_t size, std::size_t alignment) noexcept {
    // 割り込み禁止（Cortex-M3/M4アセンブリコードが有効な場合はMRS/CPSIDを呼び出し）
    uint32_t primask = DisableInterrupts();

    // 現在のオフセットからアライメント位置を算出
    std::size_t current_ptr = reinterpret_cast<std::size_t>(&buffer_[offset_]);
    std::size_t aligned_ptr = (current_ptr + alignment - 1) & ~(alignment - 1);
    std::size_t new_offset = (aligned_ptr - reinterpret_cast<std::size_t>(buffer_)) + size;

    if (new_offset > kMemoryPoolSize) {
        // メモリプール不足時は割り込み状態を復元してnullptrを返却
        RestoreInterrupts(primask);
        return nullptr;
    }

    offset_ = new_offset;
    void* result = reinterpret_cast<void*>(aligned_ptr);

    // 割り込み状態を復元
    RestoreInterrupts(primask);
    
    return result;
}

void WasmMemoryPool::Reset() noexcept {
    uint32_t primask = DisableInterrupts();
    offset_ = 0;
    RestoreInterrupts(primask);
}

} // namespace embwasm
