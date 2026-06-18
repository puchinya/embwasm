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

// メモリプールのサイズ（バイト単位）
constexpr std::size_t kMemoryPoolSize = 512 * 1024; // 512 KB

// メモリプールのアライメント（バイト単位）
constexpr std::size_t kMemoryPoolAlignment = 8;

// 線形メモリの最大サイズ (WASMページ 1ページ = 64KB)
constexpr std::size_t kMaxLinearMemorySize = 256 * 1024; // 256 KB

// サポートする最大WASM関数定義数
constexpr std::size_t kMaxModules = 4;
constexpr std::size_t kMaxWasmFunctions = 32;

// サポートする最大WASMテーブル数
constexpr std::size_t kMaxTables = 4;

// サポートする最大WASM型シグネチャ数
constexpr std::size_t kMaxWasmTypes = 16;

// WASM実行スタックの最大深度
constexpr std::size_t kWasmStackSize = 64;

// WASM関数呼び出しの最大深度（コールスタックサイズ）
constexpr std::size_t kWasmCallStackSize = 16;

// 1つの関数で利用可能な最大ローカル変数（引数＋ローカル変数）の数（実行フレームサイズに影響）
constexpr std::size_t kMaxLocals = 32;

// ロード時にパースできる最大ローカル変数宣言数（kMaxLocalsより大きくできる）
constexpr std::size_t kMaxLocalDecls = 64;

// ローカル変数プールの総サイズ（全コールフレーム共有・WasmThreadContext 内に静的確保）
constexpr std::size_t kLocalsPoolSize = kWasmCallStackSize * kMaxLocals; // = 512

// 1つの関数内の制御ブロック（block, loop, if）の最大ネスト数
constexpr std::size_t kMaxLabels = 32;

// サポートする最大グローバル変数数
constexpr std::size_t kMaxGlobals = 16;

// マルチスレッド機能の有効化
#define EMBWASM_ENABLE_MULTITHREADING 1

// サポートする最大スレッド数
#if EMBWASM_ENABLE_MULTITHREADING
constexpr std::size_t kMaxThreads = 4;
#endif

// サポートする最大イベント数
#if EMBWASM_ENABLE_MULTITHREADING
constexpr std::size_t kMaxEvents = 8;
#endif

} // namespace embwasm

#endif // EMBWASM_USE_SPEC_CONFIG

#endif // EMBWASM_WASM_CONFIG_HPP_
