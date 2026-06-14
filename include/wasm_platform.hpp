#ifndef EMBWASM_WASM_PLATFORM_HPP_
#define EMBWASM_WASM_PLATFORM_HPP_

#include <cstdint>

// MSVC固有のイントリンシック宣言のためのヘッダーインクルード
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace embwasm {

// =============================================================================
// 1. OS依存の処理（プラットフォーム別 cpp に実装されます）
// =============================================================================

/**
 * @brief 割り込みまたはスレッド排他制御を開始（禁止）します。
 * @return 状態復元のためのフラグ値
 */
uint32_t DisableInterrupts() noexcept;

/**
 * @brief 割り込みまたはスレッド排他制御を終了（復元）します。
 * @param primask_val 復元するフラグ値
 */
void RestoreInterrupts(uint32_t primask_val) noexcept;

// =============================================================================
// 2. OS非依存のコンパイラ・CPUアーキテクチャ最適化処理（ヘッダー共通定義）
// =============================================================================

/**
 * @brief リーディングゼロの数をカウントします（最上位ビットから1が立つまでの0の数）。
 *        OSに関わらず、コンパイラおよびCPUの最適化機能（アセンブラ）を使用します。
 */
#if defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
// ARM Cortex-M アセンブリ CLZ 命令最適化
inline uint32_t CountLeadingZeros(uint32_t value) noexcept {
    if (value == 0) return 32;
    uint32_t result;
    __asm__ volatile (
        "clz %0, %1\n"
        : "=r" (result)
        : "r" (value)
    );
    return result;
}
#elif defined(__GNUC__) || defined(__clang__)
// GCC/Clang の最適化ビルトイン関数
inline uint32_t CountLeadingZeros(uint32_t value) noexcept {
    if (value == 0) return 32;
    return static_cast<uint32_t>(__builtin_clz(value));
}
#elif defined(_MSC_VER)
// MSVC の最適化ビルトイン関数
inline uint32_t CountLeadingZeros(uint32_t value) noexcept {
    if (value == 0) return 32;
    unsigned long index;
    if (_BitScanReverse(&index, value)) {
        return 31 - index;
    }
    return 32;
}
#else
// 最適化命令が利用できない場合のソフトウェアフォールバック
inline uint32_t CountLeadingZeros(uint32_t value) noexcept {
    if (value == 0) return 32;
    uint32_t count = 0;
    if ((value & 0xFFFF0000) == 0) { count += 16; value <<= 16; }
    if ((value & 0xFF000000) == 0) { count += 8;  value <<= 8;  }
    if ((value & 0xF0000000) == 0) { count += 4;  value <<= 4;  }
    if ((value & 0xC0000000) == 0) { count += 2;  value <<= 2;  }
    if ((value & 0x80000000) == 0) { count += 1; }
    return count;
}
#endif

} // namespace embwasm

#endif // EMBWASM_WASM_PLATFORM_HPP_
