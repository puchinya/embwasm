#ifndef EMBWASM_WASM_CONFIG_HPP_
#define EMBWASM_WASM_CONFIG_HPP_

#include <cstddef>

namespace embwasm {

// ==========================================
// WASM 実行環境の設定ファイル
// ==========================================

// メモリプールのサイズ（バイト単位）
// この値を変更することで、WASMエンジンが使用するメモリプールのサイズを変更できます。
constexpr std::size_t kMemoryPoolSize = 128 * 1024 * 1024; // 128 MB (スペックテスト対応)

// メモリプールのアライメント（バイト単位）
// Allocate() が返すポインタはこの境界に揃えられる。2の冪乗であること。
constexpr std::size_t kMemoryPoolAlignment = 8;

// 線形メモリの最大サイズ (WASMページ 1ページ = 64KB)
constexpr std::size_t kMaxLinearMemorySize = 64 * 1024 * 1024; // 64 MB (スペックテスト対応)

// サポートする最大WASM関数定義数
constexpr std::size_t kMaxWasmFunctions = 2048;

// サポートする最大WASM型シグネチャ数
constexpr std::size_t kMaxWasmTypes = 512;

// WASM実行スタックの最大深度
constexpr std::size_t kWasmStackSize = 1024;

// WASM関数呼び出しの最大深度（コールスタックサイズ）
constexpr std::size_t kWasmCallStackSize = 256;

// 1つの関数で利用可能な最大ローカル変数（引数＋ローカル変数）の数（実行フレームサイズに影響）
constexpr std::size_t kMaxLocals = 256;

// ロード時にパースできる最大ローカル変数宣言数（kMaxLocalsより大きくできる）
constexpr std::size_t kMaxLocalDecls = 2048;

// 1つの関数内の制御ブロック（block, loop, if）の最大ネスト数
constexpr std::size_t kMaxLabels = 256;

// サポートする最大グローバル変数数
constexpr std::size_t kMaxGlobals = 256;

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
