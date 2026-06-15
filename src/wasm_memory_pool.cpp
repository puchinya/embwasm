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
#include <cstring>

namespace embwasm {

// =============================================================================
// ブロックヘッダ（実装詳細：ヘッダへの露出なし）
//
// 各アロケーション（空き・使用中問わず）の先頭に配置される管理構造体。
// size_flags の最下位ビット (bit0) が is_free フラグを兼ねており、
// 残りのビットがブロック全体のサイズ（ヘッダ込み）を表す。
//
// sizeof(BlockHeader) は alignof(std::max_align_t) の倍数でなければならない。
// これにより、ヘッダ直後のユーザーデータが常に最大アライメント境界に来る。
// =============================================================================
struct BlockHeader {
    std::size_t  size_flags;  // bit0 = is_free; 残ビット = ブロック全体サイズ
    BlockHeader* prev;        // 物理的に前のブロック (nullptr = プール先頭)
    BlockHeader* next_free;   // フリーリスト上の次のブロック
    BlockHeader* prev_free;   // フリーリスト上の前のブロック
};

static_assert(sizeof(BlockHeader) % alignof(std::max_align_t) == 0,
              "BlockHeader size must be a multiple of alignof(std::max_align_t)");

static constexpr std::size_t kBlockHeaderSize = sizeof(BlockHeader);

// =============================================================================
// ブロックヘッダ操作ユーティリティ
// =============================================================================

static inline std::size_t BlockGetSize(const BlockHeader* h) noexcept {
    return h->size_flags & ~std::size_t(1);
}

static inline bool BlockIsFree(const BlockHeader* h) noexcept {
    return (h->size_flags & std::size_t(1)) != 0;
}

static inline void BlockSetSizeFlags(BlockHeader* h, std::size_t size, bool is_free) noexcept {
    h->size_flags = size | (is_free ? std::size_t(1) : std::size_t(0));
}

// 物理的に次のブロックを返す（プール末尾を超える場合は nullptr）
static inline BlockHeader* PoolGetNextBlock(const BlockHeader* h,
                                            const uint8_t* pool_end) noexcept {
    auto* next = reinterpret_cast<BlockHeader*>(
        reinterpret_cast<uint8_t*>(const_cast<BlockHeader*>(h)) + BlockGetSize(h));
    return (reinterpret_cast<uint8_t*>(next) < pool_end) ? next : nullptr;
}

// =============================================================================
// フリーリスト操作（割り込み排他は呼び出し元で実施済み）
// =============================================================================

static inline void InsertFreeList(void*& head_vp, BlockHeader* h) noexcept {
    BlockHeader* head = static_cast<BlockHeader*>(head_vp);
    h->next_free = head;
    h->prev_free = nullptr;
    if (head) head->prev_free = h;
    head_vp = h;
}

static inline void RemoveFreeList(void*& head_vp, BlockHeader* h) noexcept {
    if (h->prev_free) h->prev_free->next_free = h->next_free;
    else              head_vp = h->next_free;
    if (h->next_free) h->next_free->prev_free = h->prev_free;
    h->next_free = nullptr;
    h->prev_free = nullptr;
}

// =============================================================================
// 公開 API
// =============================================================================

WasmMemoryPool::WasmMemoryPool() noexcept
    : buffer_(nullptr), capacity_(0), used_bytes_(0), free_list_head_(nullptr) {}

WasmMemoryPool::~WasmMemoryPool() noexcept {
    Deinit();
}

void WasmMemoryPool::Init(uint8_t* buffer, std::size_t size) noexcept {
    buffer_         = buffer;
    capacity_       = size;
    used_bytes_     = 0;
    free_list_head_ = nullptr;

    if (!buffer || size < kBlockHeaderSize) return;

    // バッファ全体を 1 つのフリーブロックとして初期化
    BlockHeader* initial = reinterpret_cast<BlockHeader*>(buffer);
    BlockSetSizeFlags(initial, size, /*is_free=*/true);
    initial->prev      = nullptr;
    initial->next_free = nullptr;
    initial->prev_free = nullptr;
    free_list_head_    = initial;
}

void WasmMemoryPool::Deinit() noexcept {
    buffer_         = nullptr;
    capacity_       = 0;
    used_bytes_     = 0;
    free_list_head_ = nullptr;
}

void* WasmMemoryPool::Allocate(std::size_t size) noexcept {
    if (!buffer_ || size == 0) return nullptr;

    static_assert(kMemoryPoolAlignment >= alignof(BlockHeader),
                  "kMemoryPoolAlignment must be >= alignof(BlockHeader)");
    static_assert((kMemoryPoolAlignment & (kMemoryPoolAlignment - 1)) == 0,
                  "kMemoryPoolAlignment must be a power of 2");

    // 要求サイズを kMemoryPoolAlignment 境界に切り上げ
    constexpr std::size_t kAlign = kMemoryPoolAlignment;
    std::size_t aligned_size = (size + kAlign - 1) & ~(kAlign - 1);
    // ヘッダ込みの必要バイト数
    std::size_t total_needed = kBlockHeaderSize + aligned_size;
    if (total_needed < kBlockHeaderSize) return nullptr;  // オーバーフローガード

    uint32_t primask = DisableInterrupts();

    const uint8_t* pool_end = buffer_ + capacity_;

    // ファーストフィット検索
    for (BlockHeader* h = static_cast<BlockHeader*>(free_list_head_);
         h != nullptr; h = h->next_free) {
        std::size_t block_size = BlockGetSize(h);
        if (block_size < total_needed) continue;

        // フリーリストから h を取り出す
        RemoveFreeList(free_list_head_, h);

        std::size_t remainder = block_size - total_needed;
        if (remainder >= kBlockHeaderSize) {
            // 残余が最小ブロックサイズ以上 → ブロックを分割
            BlockHeader* new_free = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<uint8_t*>(h) + total_needed);
            BlockSetSizeFlags(new_free, remainder, /*is_free=*/true);
            new_free->prev      = h;
            new_free->next_free = nullptr;
            new_free->prev_free = nullptr;

            // new_free の次の物理ブロックの prev を更新
            BlockHeader* after_new = PoolGetNextBlock(new_free, pool_end);
            if (after_new) after_new->prev = new_free;

            BlockSetSizeFlags(h, total_needed, /*is_free=*/false);
            InsertFreeList(free_list_head_, new_free);
        } else {
            // 残余が小さすぎる → ブロック全体を使用（内部フラグメント許容）
            BlockSetSizeFlags(h, block_size, /*is_free=*/false);
        }

        used_bytes_ += BlockGetSize(h);
        h->next_free = nullptr;
        h->prev_free = nullptr;

        RestoreInterrupts(primask);
        return reinterpret_cast<uint8_t*>(h) + kBlockHeaderSize;
    }

    RestoreInterrupts(primask);
    return nullptr;  // Out of memory
}

void WasmMemoryPool::Free(void* ptr) noexcept {
    if (!ptr || !buffer_) return;

    uint32_t primask = DisableInterrupts();

    BlockHeader* h = reinterpret_cast<BlockHeader*>(
        reinterpret_cast<uint8_t*>(ptr) - kBlockHeaderSize);

    const uint8_t* pool_end = buffer_ + capacity_;

    // 範囲外チェック
    if (reinterpret_cast<uint8_t*>(h) < buffer_ ||
        reinterpret_cast<uint8_t*>(h) + kBlockHeaderSize > pool_end) {
        RestoreInterrupts(primask);
        return;
    }
    // 二重解放チェック
    if (BlockIsFree(h)) {
        RestoreInterrupts(primask);
        return;
    }

    std::size_t block_size = BlockGetSize(h);
    used_bytes_ -= block_size;

    // 解放マーク
    BlockSetSizeFlags(h, block_size, /*is_free=*/true);
    h->next_free = nullptr;
    h->prev_free = nullptr;

    // 次の物理ブロックが空きなら合体
    BlockHeader* next_block = PoolGetNextBlock(h, pool_end);
    if (next_block && BlockIsFree(next_block)) {
        RemoveFreeList(free_list_head_, next_block);
        std::size_t merged = BlockGetSize(h) + BlockGetSize(next_block);
        BlockSetSizeFlags(h, merged, /*is_free=*/true);
        BlockHeader* after_next = PoolGetNextBlock(h, pool_end);
        if (after_next) after_next->prev = h;
    }

    // 前の物理ブロックが空きなら合体
    if (h->prev && BlockIsFree(h->prev)) {
        BlockHeader* prev_block = h->prev;
        RemoveFreeList(free_list_head_, prev_block);
        std::size_t merged = BlockGetSize(prev_block) + BlockGetSize(h);
        BlockSetSizeFlags(prev_block, merged, /*is_free=*/true);
        BlockHeader* after_h = PoolGetNextBlock(prev_block, pool_end);
        if (after_h) after_h->prev = prev_block;
        h = prev_block;
    }

    InsertFreeList(free_list_head_, h);

    RestoreInterrupts(primask);
}

void WasmMemoryPool::Reset() noexcept {
    if (!buffer_ || capacity_ < kBlockHeaderSize) {
        used_bytes_     = 0;
        free_list_head_ = nullptr;
        return;
    }

    uint32_t primask = DisableInterrupts();

    // バッファ全体を 1 つのフリーブロックに再初期化
    BlockHeader* initial = reinterpret_cast<BlockHeader*>(buffer_);
    BlockSetSizeFlags(initial, capacity_, /*is_free=*/true);
    initial->prev      = nullptr;
    initial->next_free = nullptr;
    initial->prev_free = nullptr;
    free_list_head_    = initial;
    used_bytes_        = 0;

    RestoreInterrupts(primask);
}

} // namespace embwasm
