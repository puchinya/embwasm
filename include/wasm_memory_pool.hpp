#ifndef EMBWASM_WASM_MEMORY_POOL_HPP_
#define EMBWASM_WASM_MEMORY_POOL_HPP_

#include <cstddef>
#include <cstdint>
#include "wasm_config.hpp"

namespace embwasm {

// ベアメタル環境向けの静的メモリプールクラス
// 動的メモリ割り当て（ヒープ）を排除し、設定されたサイズのバッファからアロケートします。
class WasmMemoryPool {
public:
    WasmMemoryPool() noexcept;
    ~WasmMemoryPool() noexcept;

    // 明示的な初期化と終了
    void Init(uint8_t* buffer, std::size_t size) noexcept;
    void Deinit() noexcept;

    // コピー・代入は禁止
    WasmMemoryPool(const WasmMemoryPool&) = delete;
    WasmMemoryPool& operator=(const WasmMemoryPool&) = delete;

    // アライメントを考慮したメモリ割り当て
    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept;

    // プールのリセット（一括解放）
    void Reset() noexcept;

    // メモリ使用状況の確認
    std::size_t GetUsedBytes() const noexcept { return offset_; }
    std::size_t GetTotalBytes() const noexcept { return capacity_; }
    std::size_t GetFreeBytes() const noexcept { return capacity_ - offset_; }

private:
    uint8_t* buffer_;
    std::size_t capacity_;
    std::size_t offset_;
};

} // namespace embwasm

#endif // EMBWASM_WASM_MEMORY_POOL_HPP_
