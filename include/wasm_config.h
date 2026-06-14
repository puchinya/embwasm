#ifndef EMBWASM_WASM_CONFIG_H_
#define EMBWASM_WASM_CONFIG_H_

#include <cstddef>

namespace embwasm {

// ==========================================
// WASM 実行環境の設定ファイル
// ==========================================

// メモリプールのサイズ（バイト単位）
// この値を変更することで、WASMエンジンが使用するメモリプールのサイズを変更できます。
constexpr std::size_t kMemoryPoolSize = 65536; // 64 KB

// 登録可能な最大ホストAPI数
constexpr std::size_t kMaxHostApis = 16;

// サポートする最大WASM関数定義数
constexpr std::size_t kMaxWasmFunctions = 32;

// サポートする最大WASM型シグネチャ数
constexpr std::size_t kMaxWasmTypes = 16;

// WASM実行スタックの最大深度
constexpr std::size_t kWasmStackSize = 64;

// 1つの関数で利用可能な最大ローカル変数（引数＋ローカル変数）の数
constexpr std::size_t kMaxLocals = 32;

} // namespace embwasm

#endif // EMBWASM_WASM_CONFIG_H_
