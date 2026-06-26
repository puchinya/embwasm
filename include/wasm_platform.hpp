#ifndef EMBWASM_WASM_PLATFORM_HPP_
#define EMBWASM_WASM_PLATFORM_HPP_

#include <cstdint>

// MSVC固有のイントリンシック宣言のためのヘッダーインクルード
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace embwasm {

class WasmEngine;
enum class WasmResult : int32_t;

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

/**
 * @brief 単調増加の現在時刻をミリ秒で返します。
 * @return 現在時刻（ms）。オーバーフローは約 49 日で発生します。
 */
uint32_t PlatformGetTimeMs() noexcept;

/**
 * @brief WasmEngine インスタンス単位のプラットフォームリソースを確保・初期化します。
 *        WasmEngine::Init() から呼ばれます。
 * @param engine  初期化対象のエンジン。
 * @return kOk（成功）またはエラーコード。失敗時は engine.SetPlatformData(nullptr) 済み。
 */
WasmResult PlatformEngineInit(WasmEngine& engine) noexcept;

/**
 * @brief WasmEngine インスタンス単位のプラットフォームリソースを解放します。
 *        WasmEngine::Deinit() から呼ばれます。
 */
void PlatformEngineDeinit(WasmEngine& engine) noexcept;

/**
 * @brief 実行スレッドの開始をプラットフォームに通知します。
 *        WasmEngine::ExecuteInternal() の RunLoop 直前から呼ばれます。
 *        FreeRTOS/uITRON ではここで実行タスクのハンドル／ID を取得します。
 * @return kOk（成功）またはエラーコード。
 */
WasmResult PlatformEngineExecuteBegin(WasmEngine& engine) noexcept;

/**
 * @brief 実行スレッドの終了をプラットフォームに通知します。
 *        WasmEngine::ExecuteInternal() の RunLoop 直後から呼ばれます。
 */
void PlatformEngineExecuteEnd(WasmEngine& engine) noexcept;

/**
 * @brief ネイティブスレッドを最大 timeout_ms ミリ秒スリープさせます。
 *        PlatformNotifyActivity() が呼ばれると即時復帰します。
 * @param engine      対象エンジン。
 * @param timeout_ms  タイムアウト（ms）。UINT32_MAX の場合は無期限待機。
 */
void PlatformWaitForActivity(WasmEngine& engine, uint32_t timeout_ms) noexcept;

/**
 * @brief PlatformWaitForActivity() で待機中のスレッドを即時起床させます。
 *        ISR やバックグラウンドスレッドから呼び出し可能です。
 * @param engine  対象エンジン。
 */
void PlatformNotifyActivity(WasmEngine& engine) noexcept;

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

// =============================================================================
// CountTrailingZeros — CTZ 命令 / BSF 命令 / RBIT+CLZ によるトレーリングゼロカウント
// =============================================================================

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
// ARMv7-M/7-EM: RBIT で ビット逆順化後に CLZ
inline uint32_t CountTrailingZeros(uint32_t v) noexcept {
    if (v == 0) return 32;
    uint32_t result;
    __asm__ volatile (
        "rbit %0, %1\n"
        "clz  %0, %0\n"
        : "=r" (result) : "r" (v)
    );
    return result;
}
#elif defined(__GNUC__) || defined(__clang__)
inline uint32_t CountTrailingZeros(uint32_t v) noexcept {
    if (v == 0) return 32;
    return static_cast<uint32_t>(__builtin_ctz(v));
}
#elif defined(_MSC_VER)
inline uint32_t CountTrailingZeros(uint32_t v) noexcept {
    if (v == 0) return 32;
    unsigned long index;
    _BitScanForward(&index, v);
    return static_cast<uint32_t>(index);
}
#else
inline uint32_t CountTrailingZeros(uint32_t v) noexcept {
    if (v == 0) return 32;
    uint32_t c = 0;
    if ((v & 0x0000FFFFU) == 0) { v >>= 16; c += 16; }
    if ((v & 0x000000FFU) == 0) { v >>= 8;  c += 8;  }
    if ((v & 0x0000000FU) == 0) { v >>= 4;  c += 4;  }
    if ((v & 0x00000003U) == 0) { v >>= 2;  c += 2;  }
    if ((v & 0x00000001U) == 0) { c += 1; }
    return c;
}
#endif

// =============================================================================
// PopCount — POPCNT 命令によるポップカウント
// =============================================================================

#if defined(__GNUC__) || defined(__clang__)
inline uint32_t PopCount32(uint32_t v) noexcept {
    return static_cast<uint32_t>(__builtin_popcount(v));
}
inline uint32_t PopCount64(uint64_t v) noexcept {
    return static_cast<uint32_t>(__builtin_popcountll(v));
}
#elif defined(_MSC_VER)
inline uint32_t PopCount32(uint32_t v) noexcept {
    return static_cast<uint32_t>(__popcnt(v));
}
inline uint32_t PopCount64(uint64_t v) noexcept {
    return static_cast<uint32_t>(__popcnt64(v));
}
#else
inline uint32_t PopCount32(uint32_t v) noexcept {
    v = v - ((v >> 1) & 0x55555555U);
    v = (v & 0x33333333U) + ((v >> 2) & 0x33333333U);
    return (((v + (v >> 4)) & 0x0F0F0F0FU) * 0x01010101U) >> 24;
}
inline uint32_t PopCount64(uint64_t v) noexcept {
    return PopCount32(static_cast<uint32_t>(v)) + PopCount32(static_cast<uint32_t>(v >> 32));
}
#endif

} // namespace embwasm

#endif // EMBWASM_WASM_PLATFORM_HPP_
