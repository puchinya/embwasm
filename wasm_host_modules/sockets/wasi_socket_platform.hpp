// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// Licensed under the MIT License.
//
// wasi_socket_platform.hpp
//   ソケット操作のプラットフォーム抽象化インタフェース。
//   各プラットフォーム実装 (macos / windows / lwip) は、この宣言群を実装する。
//
// ベアメタル制約:
//   - STL 禁止 / 例外禁止 / RTTI 禁止
//   - 動的メモリ割り当て禁止
//   - 再帰呼び出し禁止
//   - 関数ポインタ禁止
// =============================================================================

#ifndef EMBWASM_WASI_SOCKET_PLATFORM_HPP_
#define EMBWASM_WASI_SOCKET_PLATFORM_HPP_

#include <cstdint>

namespace embwasm {
namespace hostmodules {
namespace wasi {
namespace sockets {
namespace sockets {

// ---------------------------------------------------------------------------
// SocketHandle
//   プラットフォームが返す生のソケット記述子を格納する型。
//   WASM 側には int32_t に変換した「仮想ハンドル」を公開し、
//   内部ではこの型で OS の記述子を保持する。
// ---------------------------------------------------------------------------
#if defined(_WIN32)
// Winsock2: SOCKET = UINT_PTR (64bit 環境でも使用)
using PlatformSocket = uintptr_t;
static constexpr PlatformSocket kInvalidPlatformSocket =
    static_cast<PlatformSocket>(~0ULL);  // INVALID_SOCKET 相当
#else
// POSIX / lwIP: int
using PlatformSocket = int;
static constexpr PlatformSocket kInvalidPlatformSocket = -1;
#endif

// ---------------------------------------------------------------------------
// WASM 側へ公開する sockaddr_in 互換レイアウト（プラットフォーム非依存）
//   WASM 線形メモリ上で WASM モジュールが構築する構造体のレイアウト。
//   ネイティブの sockaddr_in と同じバイナリ互換レイアウトを持つ。
//
//   struct WasiSockAddrIn {
//     uint16_t sin_family;   // AF_INET = 2
//     uint16_t sin_port;     // ネットワークバイトオーダー
//     uint32_t sin_addr;     // IPv4 アドレス (ネットワークバイトオーダー)
//     uint8_t  sin_zero[8];  // パディング
//   };  // sizeof = 16
// ---------------------------------------------------------------------------
static constexpr uint32_t kWasiSockAddrInSize = 16U;

// ---------------------------------------------------------------------------
// プラットフォーム初期化 / 終了
// ---------------------------------------------------------------------------

/// プラットフォーム固有のソケットサブシステムを初期化する。
/// Winsock2 では WSAStartup を呼び出す。POSIX では何もしない。
/// @return true: 成功、false: 失敗
bool PlatformSocketInit() noexcept;

/// プラットフォーム固有のソケットサブシステムを終了する。
/// Winsock2 では WSACleanup を呼び出す。
void PlatformSocketDeinit() noexcept;

// ---------------------------------------------------------------------------
// ソケット操作
// ---------------------------------------------------------------------------

/// ソケットを作成する。
PlatformSocket PlatformSocketCreate(int domain, int type, int protocol) noexcept;

/// ソケットを閉じる。
int PlatformSocketClose(PlatformSocket sock) noexcept;

/// ソケットにアドレスをバインドする。
int PlatformSocketBind(PlatformSocket sock,
                       const uint8_t* addr_buf,
                       uint32_t addr_len) noexcept;

/// リッスン状態に移行する。
int PlatformSocketListen(PlatformSocket sock, int backlog) noexcept;

/// 接続を受け付ける。
PlatformSocket PlatformSocketAccept(PlatformSocket sock,
                                    uint8_t* peer_addr_buf,
                                    uint32_t* peer_addr_len) noexcept;

/// リモートアドレスへ接続する。
int PlatformSocketConnect(PlatformSocket sock,
                          const uint8_t* addr_buf,
                          uint32_t addr_len) noexcept;

/// データを送信する。
int PlatformSocketSend(PlatformSocket sock,
                       const uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept;

/// データを受信する。
int PlatformSocketRecv(PlatformSocket sock,
                       uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept;

/// 指定アドレスへデータを送信する（UDP 用）。
int PlatformSocketSendTo(PlatformSocket sock,
                         const uint8_t* buf,
                         uint32_t len,
                         int flags,
                         const uint8_t* addr_buf,
                         uint32_t addr_len) noexcept;

/// 送信元アドレスつきでデータを受信する（UDP 用）。
int PlatformSocketRecvFrom(PlatformSocket sock,
                           uint8_t* buf,
                           uint32_t len,
                           int flags,
                           uint8_t* src_addr_buf,
                           uint32_t* src_addr_len) noexcept;

/// ソケットオプションを設定する（setsockopt 相当）。
int PlatformSocketSetOpt(PlatformSocket sock,
                         int level,
                         int optname,
                         const uint8_t* optval,
                         uint32_t optlen) noexcept;

/// ソケットオプションを取得する（getsockopt 相当）。
int PlatformSocketGetOpt(PlatformSocket sock,
                         int level,
                         int optname,
                         uint8_t* optval,
                         uint32_t* optlen) noexcept;

/// ノンブロッキングモードを設定する。
int PlatformSocketSetNonBlocking(PlatformSocket sock, int nonblocking) noexcept;

/// 単一ソケットのイベントを待つ（poll 相当）。
int PlatformSocketPoll(PlatformSocket sock,
                       int events,
                       int timeout_ms) noexcept;

/// IPv4 アドレス文字列を 32bit ネットワークバイトオーダーに変換する。
uint32_t PlatformInetAddr(const char* addr_str) noexcept;

/// ホストバイトオーダーの 16bit 値をネットワークバイトオーダーに変換する。
uint16_t PlatformHostToNetShort(uint16_t host_short) noexcept;

/// ネットワークバイトオーダーの 16bit 値をホストバイトオーダーに変換する。
uint16_t PlatformNetToHostShort(uint16_t net_short) noexcept;

/// 最後に発生したエラーコードを返す（errno / WSAGetLastError 相当）。
int PlatformSocketGetLastError() noexcept;

}  // namespace sockets
}  // namespace sockets
}  // namespace wasi
}  // namespace hostmodules
}  // namespace embwasm

#endif  // EMBWASM_WASI_SOCKET_PLATFORM_HPP_
