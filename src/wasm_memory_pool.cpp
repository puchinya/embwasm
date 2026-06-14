// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This static memory pool has been designed and implemented entirely from scratch
// to guarantee deterministic allocation on bare-metal systems without relying 
// on standard dynamic heap allocators or external libraries.
// =============================================================================

#include "wasm_memory_pool.hpp"
#include "wasm_platform.hpp"

namespace embwasm {

WasmMemoryPool::WasmMemoryPool() noexcept : buffer_(nullptr), capacity_(0), offset_(0) {}

WasmMemoryPool::~WasmMemoryPool() noexcept {
    Deinit();
}

void WasmMemoryPool::Init(uint8_t* buffer, std::size_t size) noexcept {
    buffer_ = buffer;
    capacity_ = size;
    offset_ = 0;
}

void WasmMemoryPool::Deinit() noexcept {
    buffer_ = nullptr;
    capacity_ = 0;
    offset_ = 0;
}

void* WasmMemoryPool::Allocate(std::size_t size, std::size_t alignment) noexcept {
    if (buffer_ == nullptr || capacity_ == 0) {
        return nullptr;
    }

    // 【排他制御 / 割り込み排他】
    // マルチタスク環境や割り込みハンドラ（ISR）からの同時呼び出しによる競合状態を防ぐため、
    // プロセッサレベルでグローバル割り込みを一時的に禁止（PRIMASK等の制御）し、クリティカルセクションを保護。
    uint32_t primask = DisableInterrupts();

    // 現在の未割り当て領域の先頭アドレスを算出
    std::size_t current_ptr = reinterpret_cast<std::size_t>(&buffer_[offset_]);
    
    // 【ビット演算によるアライメント調整】
    // 要求された alignment（2の冪乗であることを想定）の境界にアドレスを切り上げる。
    // 例: alignment = 8 の場合, (current_ptr + 7) & ~7 となり、下位3ビットをマスクして8の倍数に丸める。
    // これにより、マイコンにおける未整列メモリアクセス（Unaligned Access Fault）の発生を防ぐ。
    std::size_t aligned_ptr = (current_ptr + alignment - 1) & ~(alignment - 1);
    
    // アライメント調整後のポインタを基準として、今回の要求サイズを加えた新たなオフセット位置を計算
    std::size_t new_offset = (aligned_ptr - reinterpret_cast<std::size_t>(buffer_)) + size;

    // 【バッファ限界チェック】
    // 割り当て後のサイズがプールサイズの上限を超える場合はOOM（メモリ不足）とし、
    // 直ちに割り込み状態を元に戻した上で nullptr を返却（ヒープ例外は `-fno-exceptions` により使用不可）。
    if (new_offset > capacity_) {
        RestoreInterrupts(primask);
        return nullptr;
    }

    // プール状態を更新し、アライメント済みの割り当てアドレスを決定
    offset_ = new_offset;
    void* result = reinterpret_cast<void*>(aligned_ptr);

    // 【割り込み制御の復元】
    // クリティカルセクションを抜け、割り込み無効化前のレジスタ状態（PRIMASK等）に安全に復元。
    RestoreInterrupts(primask);
    
    return result;
}

void WasmMemoryPool::Reset() noexcept {
    // クラスインスタンス再利用時の状態リセット。
    // offset_ の書き換え処理を割り込み排他制御によって安全に実行。
    uint32_t primask = DisableInterrupts();
    offset_ = 0;
    RestoreInterrupts(primask);
}

} // namespace embwasm
