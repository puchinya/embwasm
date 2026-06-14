// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// Licensed under the MIT License.
//
// embwasm_hostmodule_socket.hpp
//   WASI:sockets ホストモジュールの公開インタフェース。
//   WIT ファイル (wasm_host_modules/sockets/sockets.wit) に記述された
//   各インポート関数に対応する C++ 関数の宣言。
//
// ベアメタル制約:
//   - STL 禁止 / 例外禁止 / RTTI 禁止
//   - 動的メモリ割り当て禁止（静的ソケットハンドルテーブルを使用）
//   - 再帰呼び出し禁止
//   - 関数ポインタ禁止
// =============================================================================

#ifndef EMBWASM_HOSTMODULE_SOCKET_HPP_
#define EMBWASM_HOSTMODULE_SOCKET_HPP_

#include "wasm_types.hpp"
#include "wasm_config.hpp"

namespace embwasm {
class WasmEngine;

namespace hostmodules {
namespace socket {

// ---------------------------------------------------------------------------
// モジュールライフサイクル
// ---------------------------------------------------------------------------
void Initialize(WasmEngine& engine) noexcept;
void Deinitialize(WasmEngine& engine) noexcept;

// ---------------------------------------------------------------------------
// ソケットライフサイクル
// ---------------------------------------------------------------------------
WasmResult SocketCreate(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketClose(WasmEngine& engine,
                       const WasmValue* args, uint32_t arg_count,
                       WasmValue* results, uint32_t result_count) noexcept;

// ---------------------------------------------------------------------------
// サーバー側操作
// ---------------------------------------------------------------------------
WasmResult SocketBind(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketListen(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketAccept(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept;

// ---------------------------------------------------------------------------
// クライアント側操作
// ---------------------------------------------------------------------------
WasmResult SocketConnect(WasmEngine& engine,
                         const WasmValue* args, uint32_t arg_count,
                         WasmValue* results, uint32_t result_count) noexcept;

// ---------------------------------------------------------------------------
// データ送受信
// ---------------------------------------------------------------------------
WasmResult SocketSend(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketRecv(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketSendTo(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketRecvFrom(WasmEngine& engine,
                          const WasmValue* args, uint32_t arg_count,
                          WasmValue* results, uint32_t result_count) noexcept;

// ---------------------------------------------------------------------------
// ソケットオプション
// ---------------------------------------------------------------------------
WasmResult SocketSetOpt(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketGetOpt(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept;

// ---------------------------------------------------------------------------
// ノンブロッキング / ポーリング
// ---------------------------------------------------------------------------
WasmResult SocketSetNonBlocking(WasmEngine& engine,
                                const WasmValue* args, uint32_t arg_count,
                                WasmValue* results, uint32_t result_count) noexcept;

WasmResult SocketPoll(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept;

// ---------------------------------------------------------------------------
// アドレスユーティリティ
// ---------------------------------------------------------------------------
WasmResult InetAddr(WasmEngine& engine,
                    const WasmValue* args, uint32_t arg_count,
                    WasmValue* results, uint32_t result_count) noexcept;

WasmResult HostToNetShort(WasmEngine& engine,
                          const WasmValue* args, uint32_t arg_count,
                          WasmValue* results, uint32_t result_count) noexcept;

WasmResult NetToHostShort(WasmEngine& engine,
                          const WasmValue* args, uint32_t arg_count,
                          WasmValue* results, uint32_t result_count) noexcept;

// ---------------------------------------------------------------------------
// エラー情報
// ---------------------------------------------------------------------------
WasmResult SocketGetLastError(WasmEngine& engine,
                              const WasmValue* args, uint32_t arg_count,
                              WasmValue* results, uint32_t result_count) noexcept;

}  // namespace socket
}  // namespace hostmodules
}  // namespace embwasm

#endif  // EMBWASM_HOSTMODULE_SOCKET_HPP_
