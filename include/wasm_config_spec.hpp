#ifndef EMBWASM_WASM_CONFIG_SPEC_HPP_
#define EMBWASM_WASM_CONFIG_SPEC_HPP_

#include <cstddef>

namespace embwasm {

// ==========================================
// WASM 実行環境の設定ファイル (公式スペックテスト用)
// ==========================================

// メモリプールのアライメント（バイト単位）
constexpr std::size_t kMemoryPoolAlignment = 8;

// 線形メモリの最大サイズ (WASMページ 1ページ = 64KB)
constexpr std::size_t kMaxLinearMemorySize = 64 * 1024 * 1024; // 64 MB (スペックテスト対応)

// サポートする最大WASM関数定義数
constexpr std::size_t kMaxModules = 512;

// WASM関数呼び出しの最大深度（コールスタックサイズ）
constexpr std::size_t kWasmCallStackSize = 256;

// ロード時にパースできる最大ローカル変数宣言数（kMaxLocalsより大きくできる）
constexpr std::size_t kMaxLocalDecls = 2048;

// 統合スタックの総サイズ（ローカル変数領域 + 演算スタック領域）
constexpr std::size_t kUnifiedStackSize = 33280;

// ラベルプールの総サイズ（全コールフレーム共有・WasmThreadContext 内に静的確保）
constexpr std::size_t kLabelsPoolSize = 512;

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

#endif // EMBWASM_WASM_CONFIG_SPEC_HPP_
