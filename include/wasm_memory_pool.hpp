#ifndef EMBWASM_WASM_MEMORY_POOL_HPP_
#define EMBWASM_WASM_MEMORY_POOL_HPP_

#include <cstddef>
#include <cstdint>
#include "wasm_config.hpp"

namespace embwasm {

/// @brief ベアメタル環境向けフリーリスト方式静的メモリプール。
///
/// 外部から渡した静的バッファを管理し、動的ヒープ（`malloc`/`new`）を一切使用しません。
/// 各アロケーションにはブロックヘッダが付与され、`Free()` による個別解放と
/// 隣接ブロックの即時合体（コアレッシング）によりフラグメンテーションを抑制します。
class WasmMemoryPool {
public:
    WasmMemoryPool() noexcept;
    ~WasmMemoryPool() noexcept;

    /// @brief メモリプールを初期化します。使用前に必ず呼んでください。
    /// @param buffer  プールとして使用する静的バッファの先頭ポインタ。
    /// @param size    バッファのバイト数。
    void Init(uint8_t* buffer, std::size_t size) noexcept;

    /// @brief メモリプールを終了し、内部状態をリセットします。
    void Deinit() noexcept;

    WasmMemoryPool(const WasmMemoryPool&) = delete;
    WasmMemoryPool& operator=(const WasmMemoryPool&) = delete;

    /// @brief 指定サイズのメモリブロックをプールから確保します。
    ///
    /// アライメントは `kMemoryPoolAlignment` に従います。
    /// @param size  確保するバイト数。
    /// @return 確保した領域の先頭ポインタ。プール枯渇時は `nullptr` を返します。
    void* Allocate(std::size_t size) noexcept;

    /// @brief 確保済みメモリブロックを個別解放します。隣接フリーブロックと自動合体します。
    /// @param ptr  `Allocate()` が返したポインタ。`nullptr` を渡した場合は何もしません。
    void Free(void* ptr) noexcept;

    /// @brief プール内の全アロケーションを一括解放します（バッファ自体は解放しません）。
    void Reset() noexcept;

    /// @brief 現在の使用済みバイト数を返します。
    std::size_t GetUsedBytes() const noexcept { return used_bytes_; }

    /// @brief プールの総容量（バイト数）を返します。
    std::size_t GetTotalBytes() const noexcept { return capacity_; }

    /// @brief 空きバイト数を返します。
    std::size_t GetFreeBytes() const noexcept { return capacity_ - used_bytes_; }

private:
    uint8_t*    buffer_;
    std::size_t capacity_;
    std::size_t used_bytes_;
    void*       free_list_head_;  // 実際の型は BlockHeader*（wasm_memory_pool.cpp 内部で定義）
};

} // namespace embwasm

#endif // EMBWASM_WASM_MEMORY_POOL_HPP_
