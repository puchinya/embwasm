#ifndef EMBWASM_WASM_CONFIG_HPP_
#define EMBWASM_WASM_CONFIG_HPP_

#include <cstddef>

namespace embwasm {

// ==========================================
// WASM 実行環境の設定ファイル
// ==========================================

// メモリプールのサイズ（バイト単位）
// この値を変更することで、WASMエンジンが使用するメモリプールのサイズを変更できます。
constexpr std::size_t kMemoryPoolSize = 65536; // 64 KB

// 線形メモリの最大サイズ (WASMページ 1ページ = 64KB だが、組み込み向けに制限する)
constexpr std::size_t kMaxLinearMemorySize = 2048; // 2 KB

// サポートする最大WASM関数定義数
constexpr std::size_t kMaxWasmFunctions = 64;

// サポートする最大WASM型シグネチャ数
constexpr std::size_t kMaxWasmTypes = 16;

// WASM実行スタックの最大深度
constexpr std::size_t kWasmStackSize = 64;

// WASM関数呼び出しの最大深度（コールスタックサイズ）
constexpr std::size_t kWasmCallStackSize = 16;

// 1つの関数で利用可能な最大ローカル変数（引数＋ローカル変数）の数
constexpr std::size_t kMaxLocals = 32;

// 1つの関数内の制御ブロック（block, loop, if）の最大ネスト数
constexpr std::size_t kMaxLabels = 8;

// サポートする最大グローバル変数数
constexpr std::size_t kMaxGlobals = 16;

// マルチスレッド機能の有効化
// true に設定すると、WasmScheduler および関連するホストAPIが有効になります。
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

#endif // EMBWASM_WASM_CONFIG_HPP_
