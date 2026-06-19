#ifndef EMBWASM_WASM_CONFIG_HPP_
#define EMBWASM_WASM_CONFIG_HPP_

#include <cstddef>

#ifdef EMBWASM_USE_SPEC_CONFIG
#include "wasm_config_spec.hpp"
#else

namespace embwasm {

// ==========================================
// WASM 実行環境の設定ファイル (通常・省メモリ環境用)
// ==========================================

/// @brief メモリプールの総サイズ（バイト単位）。
constexpr std::size_t kMemoryPoolSize = 512 * 1024; // 512 KB

/// @brief メモリプールのアライメント（バイト単位）。
constexpr std::size_t kMemoryPoolAlignment = 8;

/// @brief 線形メモリの最大サイズ（WASM 1 ページ = 64 KB）。
constexpr std::size_t kMaxLinearMemorySize = 256 * 1024; // 256 KB

/// @brief 同時にロードできる最大 WASM モジュール数。
constexpr std::size_t kMaxModules = 4;

/// @brief 1 モジュールあたりの最大 WASM 関数定義数。
constexpr std::size_t kMaxWasmFunctions = 32;

/// @brief 1 モジュールあたりの最大 WASM テーブル数。
constexpr std::size_t kMaxTables = 4;

/// @brief 1 モジュールあたりの最大 WASM 型シグネチャ数。
constexpr std::size_t kMaxWasmTypes = 16;

/// @brief WASM 演算スタック（データスタック）の最大深度。スレッドごとに確保されます。
constexpr std::size_t kWasmStackSize = 64;

/// @brief WASM 関数呼び出しの最大深度（コールスタックサイズ）。スレッドごとに確保されます。
constexpr std::size_t kWasmCallStackSize = 16;

/// @brief 1 関数で利用可能な最大ローカル変数数（引数＋ローカル変数の合計）。実行フレームサイズに影響します。
constexpr std::size_t kMaxLocals = 32;

/// @brief ロード時にパースできる最大ローカル変数宣言数。`kMaxLocals` より大きく設定できます。
constexpr std::size_t kMaxLocalDecls = 64;

/// @brief ローカル変数プールの総サイズ（全コールフレーム共有・`WasmThreadContext` 内に静的確保）。
constexpr std::size_t kLocalsPoolSize = kWasmCallStackSize * kMaxLocals; // = 512

/// @brief 1 関数内の制御ブロック（`block` / `loop` / `if`）の最大ネスト数。
constexpr std::size_t kMaxLabels = 32;

/// @brief 1 モジュールあたりの最大グローバル変数数。
constexpr std::size_t kMaxGlobals = 16;

/// @brief マルチスレッド機能の有効化フラグ。0 にすると協調スケジューラが無効になります。
#define EMBWASM_ENABLE_MULTITHREADING 1

#if EMBWASM_ENABLE_MULTITHREADING
/// @brief 同時に存在できる最大スレッド数（slot 0 がメインスレッド専用）。
constexpr std::size_t kMaxThreads = 4;
#endif

#if EMBWASM_ENABLE_MULTITHREADING
/// @brief 利用可能なイベントオブジェクトの最大数。
constexpr std::size_t kMaxEvents = 8;
#endif

} // namespace embwasm

#endif // EMBWASM_USE_SPEC_CONFIG

#endif // EMBWASM_WASM_CONFIG_HPP_
