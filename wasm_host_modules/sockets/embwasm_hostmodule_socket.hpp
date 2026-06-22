// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// Licensed under the MIT License.
//
// embwasm_hostmodule_socket.hpp
//   WASI:sockets ホストモジュールの公開インタフェース。
//   sockets.wit に記述された各インポート関数に対応する型付き C++ 宣言。
//   gen_api.py が生成するディスパッチコードから呼ばれる。
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

// socket-create: func(domain: s32, %type: s32, protocol: s32) -> s32
WasmResult SocketCreate(WasmEngine& engine,
                        int32_t domain, int32_t type, int32_t protocol,
                        int32_t& out_result) noexcept;

// socket-close: func(sock: s32) -> s32
WasmResult SocketClose(WasmEngine& engine,
                       int32_t sock,
                       int32_t& out_result) noexcept;

// ---------------------------------------------------------------------------
// サーバー側操作
// ---------------------------------------------------------------------------

// socket-bind: func(sock: s32, addr-ptr: s32, addr-len: s32) -> s32
WasmResult SocketBind(WasmEngine& engine,
                      int32_t sock, int32_t addr_ptr, int32_t addr_len,
                      int32_t& out_result) noexcept;

// socket-listen: func(sock: s32, backlog: s32) -> s32
WasmResult SocketListen(WasmEngine& engine,
                        int32_t sock, int32_t backlog,
                        int32_t& out_result) noexcept;

// socket-accept: func(sock: s32, peer-addr-ptr: s32, peer-addr-len-ptr: s32) -> s32
// 非同期対応: POSIX 環境では kYield を返してバックグラウンド I/O マネージャに委譲する
WasmResult SocketAccept(WasmEngine& engine,
                        int32_t sock, int32_t peer_addr_ptr, int32_t peer_addr_len_ptr,
                        int32_t& out_result) noexcept;

// ---------------------------------------------------------------------------
// クライアント側操作
// ---------------------------------------------------------------------------

// socket-connect: func(sock: s32, addr-ptr: s32, addr-len: s32) -> s32
WasmResult SocketConnect(WasmEngine& engine,
                         int32_t sock, int32_t addr_ptr, int32_t addr_len,
                         int32_t& out_result) noexcept;

// ---------------------------------------------------------------------------
// データ送受信
// ---------------------------------------------------------------------------

// socket-send: func(sock: s32, buf-ptr: s32, buf-len: s32, %flags: s32) -> s32
WasmResult SocketSend(WasmEngine& engine,
                      int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                      int32_t& out_result) noexcept;

// socket-recv: func(sock: s32, buf-ptr: s32, buf-len: s32, %flags: s32) -> s32
// 非同期対応: POSIX 環境では kYield を返してバックグラウンド I/O マネージャに委譲する
WasmResult SocketRecv(WasmEngine& engine,
                      int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                      int32_t& out_result) noexcept;

// socket-send-to: func(sock: s32, buf-ptr: s32, buf-len: s32, %flags: s32, addr-ptr: s32, addr-len: s32) -> s32
WasmResult SocketSendTo(WasmEngine& engine,
                        int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                        int32_t addr_ptr, int32_t addr_len,
                        int32_t& out_result) noexcept;

// socket-recv-from: func(sock: s32, buf-ptr: s32, buf-len: s32, %flags: s32, src-addr-ptr: s32, src-addr-len-ptr: s32) -> s32
WasmResult SocketRecvFrom(WasmEngine& engine,
                          int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                          int32_t src_addr_ptr, int32_t src_addr_len_ptr,
                          int32_t& out_result) noexcept;

// ---------------------------------------------------------------------------
// ソケットオプション
// ---------------------------------------------------------------------------

// socket-set-opt: func(sock: s32, level: s32, optname: s32, optval-ptr: s32, optlen: s32) -> s32
WasmResult SocketSetOpt(WasmEngine& engine,
                        int32_t sock, int32_t level, int32_t optname,
                        int32_t optval_ptr, int32_t optlen,
                        int32_t& out_result) noexcept;

// socket-get-opt: func(sock: s32, level: s32, optname: s32, optval-ptr: s32, optlen-ptr: s32) -> s32
WasmResult SocketGetOpt(WasmEngine& engine,
                        int32_t sock, int32_t level, int32_t optname,
                        int32_t optval_ptr, int32_t optlen_ptr,
                        int32_t& out_result) noexcept;

// ---------------------------------------------------------------------------
// ノンブロッキング / ポーリング
// ---------------------------------------------------------------------------

// socket-set-non-blocking: func(sock: s32, nonblocking: s32) -> s32
WasmResult SocketSetNonBlocking(WasmEngine& engine,
                                int32_t sock, int32_t nonblocking,
                                int32_t& out_result) noexcept;

// socket-poll: func(sock: s32, events: s32, timeout-ms: s32) -> s32
WasmResult SocketPoll(WasmEngine& engine,
                      int32_t sock, int32_t events, int32_t timeout_ms,
                      int32_t& out_result) noexcept;

// ---------------------------------------------------------------------------
// アドレスユーティリティ
// ---------------------------------------------------------------------------

// inet-addr: func(addr-str-ptr: s32) -> s32
WasmResult InetAddr(WasmEngine& engine,
                    int32_t addr_str_ptr,
                    int32_t& out_result) noexcept;

// host-to-net-short: func(host-short: s32) -> s32
WasmResult HostToNetShort(WasmEngine& engine,
                          int32_t host_short,
                          int32_t& out_result) noexcept;

// net-to-host-short: func(net-short: s32) -> s32
WasmResult NetToHostShort(WasmEngine& engine,
                          int32_t net_short,
                          int32_t& out_result) noexcept;

// ---------------------------------------------------------------------------
// エラー情報
// ---------------------------------------------------------------------------

// socket-get-last-error: func() -> s32
WasmResult SocketGetLastError(WasmEngine& engine,
                              int32_t& out_result) noexcept;

}  // namespace socket
}  // namespace hostmodules
}  // namespace embwasm

#endif  // EMBWASM_HOSTMODULE_SOCKET_HPP_
