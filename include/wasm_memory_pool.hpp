#ifndef EMBWASM_WASM_MEMORY_POOL_HPP_
#define EMBWASM_WASM_MEMORY_POOL_HPP_

#include <cstddef>
#include <cstdint>
#include "wasm_config.hpp"

namespace embwasm {

// ベアメタル環境向けフリーリスト方式メモリプール
// バッファは外部から渡す静的配列を使用し、動的ヒープは一切使用しない。
// 各アロケーションにはブロックヘッダが付与され、Free() による個別解放と
// 隣接ブロックの即時合体（コアレッシング）によりフラグメンテーションを抑制する。
class WasmMemoryPool {
public:
    WasmMemoryPool() noexcept;
    ~WasmMemoryPool() noexcept;

    void Init(uint8_t* buffer, std::size_t size) noexcept;
    void Deinit() noexcept;

    WasmMemoryPool(const WasmMemoryPool&) = delete;
    WasmMemoryPool& operator=(const WasmMemoryPool&) = delete;

    // メモリ割り当て（アライメントは kMemoryPoolAlignment に従う）
    void* Allocate(std::size_t size) noexcept;

    // 個別メモリ解放（隣接フリーブロックと自動合体）
    void Free(void* ptr) noexcept;

    // プールのリセット（全アロケーションを一括解放）
    void Reset() noexcept;

    // メモリ使用状況の確認
    std::size_t GetUsedBytes() const noexcept { return used_bytes_; }
    std::size_t GetTotalBytes() const noexcept { return capacity_; }
    std::size_t GetFreeBytes() const noexcept { return capacity_ - used_bytes_; }

private:
    uint8_t*    buffer_;
    std::size_t capacity_;
    std::size_t used_bytes_;
    void*       free_list_head_;  // 実際の型は BlockHeader*（wasm_memory_pool.cpp 内部で定義）
};

} // namespace embwasm

#endif // EMBWASM_WASM_MEMORY_POOL_HPP_
