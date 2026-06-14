// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// Licensed under the MIT License.
//
// platform/windows/socket_platform.cpp
//   Windows 向けソケットプラットフォーム実装（Winsock2 使用）。
//
//   Winsock2 では初期化に WSAStartup、終了に WSACleanup が必要。
//   SOCKET 型は uintptr_t と互換 (PlatformSocket = uintptr_t)。
//   エラーコードは WSAGetLastError() で取得する。
//
//   ノンブロッキングモードは ioctlsocket() で設定する。
//   poll 相当は WSAPoll() (Vista 以降) を使用する。
// =============================================================================

#include "wasi_socket_platform.hpp"

// Winsock2 は windows.h より先にインクルードする必要がある
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

// Winsock2 ライブラリのリンク（MSVC でのみ pragma comment が有効）
#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <cstdint>
#include <cstring>

namespace embwasm {
namespace hostmodules {
namespace socket {

// ---------------------------------------------------------------------------
// 初期化 / 終了
// ---------------------------------------------------------------------------

bool PlatformSocketInit() noexcept {
    WSADATA wsa_data;
    // バージョン 2.2 を要求する
    const int result = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
    return (result == 0);
}

void PlatformSocketDeinit() noexcept {
    ::WSACleanup();
}

// ---------------------------------------------------------------------------
// ソケット操作
// ---------------------------------------------------------------------------

PlatformSocket PlatformSocketCreate(int domain, int type, int protocol) noexcept {
    const SOCKET s = ::socket(domain, type, protocol);
    if (s == INVALID_SOCKET) {
        return kInvalidPlatformSocket;
    }
    return static_cast<PlatformSocket>(s);
}

int PlatformSocketClose(PlatformSocket sock) noexcept {
    const int ret = ::closesocket(static_cast<SOCKET>(sock));
    return (ret == SOCKET_ERROR) ? -1 : 0;
}

int PlatformSocketBind(PlatformSocket sock,
                       const uint8_t* addr_buf,
                       uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    const int ret = ::bind(static_cast<SOCKET>(sock), sa,
                           static_cast<int>(addr_len));
    return (ret == SOCKET_ERROR) ? -1 : 0;
}

int PlatformSocketListen(PlatformSocket sock, int backlog) noexcept {
    const int ret = ::listen(static_cast<SOCKET>(sock), backlog);
    return (ret == SOCKET_ERROR) ? -1 : 0;
}

PlatformSocket PlatformSocketAccept(PlatformSocket sock,
                                    uint8_t* peer_addr_buf,
                                    uint32_t* peer_addr_len) noexcept {
    if (peer_addr_buf && peer_addr_len) {
        struct sockaddr* sa = reinterpret_cast<struct sockaddr*>(peer_addr_buf);
        int sa_len = static_cast<int>(*peer_addr_len);
        const SOCKET client = ::accept(static_cast<SOCKET>(sock), sa, &sa_len);
        if (client == INVALID_SOCKET) return kInvalidPlatformSocket;
        *peer_addr_len = static_cast<uint32_t>(sa_len);
        return static_cast<PlatformSocket>(client);
    } else {
        const SOCKET client = ::accept(static_cast<SOCKET>(sock), nullptr, nullptr);
        if (client == INVALID_SOCKET) return kInvalidPlatformSocket;
        return static_cast<PlatformSocket>(client);
    }
}

int PlatformSocketConnect(PlatformSocket sock,
                          const uint8_t* addr_buf,
                          uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    const int ret = ::connect(static_cast<SOCKET>(sock), sa,
                              static_cast<int>(addr_len));
    return (ret == SOCKET_ERROR) ? -1 : 0;
}

int PlatformSocketSend(PlatformSocket sock,
                       const uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept {
    const int sent = ::send(static_cast<SOCKET>(sock),
                            reinterpret_cast<const char*>(buf),
                            static_cast<int>(len),
                            flags);
    return (sent == SOCKET_ERROR) ? -1 : sent;
}

int PlatformSocketRecv(PlatformSocket sock,
                       uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept {
    const int recvd = ::recv(static_cast<SOCKET>(sock),
                             reinterpret_cast<char*>(buf),
                             static_cast<int>(len),
                             flags);
    return (recvd == SOCKET_ERROR) ? -1 : recvd;
}

int PlatformSocketSendTo(PlatformSocket sock,
                         const uint8_t* buf,
                         uint32_t len,
                         int flags,
                         const uint8_t* addr_buf,
                         uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    const int sent = ::sendto(static_cast<SOCKET>(sock),
                              reinterpret_cast<const char*>(buf),
                              static_cast<int>(len),
                              flags,
                              sa,
                              static_cast<int>(addr_len));
    return (sent == SOCKET_ERROR) ? -1 : sent;
}

int PlatformSocketRecvFrom(PlatformSocket sock,
                           uint8_t* buf,
                           uint32_t len,
                           int flags,
                           uint8_t* src_addr_buf,
                           uint32_t* src_addr_len) noexcept {
    if (src_addr_buf && src_addr_len) {
        struct sockaddr* sa = reinterpret_cast<struct sockaddr*>(src_addr_buf);
        int sa_len = static_cast<int>(*src_addr_len);
        const int recvd = ::recvfrom(static_cast<SOCKET>(sock),
                                     reinterpret_cast<char*>(buf),
                                     static_cast<int>(len),
                                     flags,
                                     sa,
                                     &sa_len);
        if (recvd != SOCKET_ERROR) {
            *src_addr_len = static_cast<uint32_t>(sa_len);
        }
        return (recvd == SOCKET_ERROR) ? -1 : recvd;
    } else {
        const int recvd = ::recvfrom(static_cast<SOCKET>(sock),
                                     reinterpret_cast<char*>(buf),
                                     static_cast<int>(len),
                                     flags,
                                     nullptr,
                                     nullptr);
        return (recvd == SOCKET_ERROR) ? -1 : recvd;
    }
}

int PlatformSocketSetOpt(PlatformSocket sock,
                         int level,
                         int optname,
                         const uint8_t* optval,
                         uint32_t optlen) noexcept {
    const int ret = ::setsockopt(static_cast<SOCKET>(sock),
                                 level,
                                 optname,
                                 reinterpret_cast<const char*>(optval),
                                 static_cast<int>(optlen));
    return (ret == SOCKET_ERROR) ? -1 : 0;
}

int PlatformSocketGetOpt(PlatformSocket sock,
                         int level,
                         int optname,
                         uint8_t* optval,
                         uint32_t* optlen) noexcept {
    int sl = static_cast<int>(*optlen);
    const int ret = ::getsockopt(static_cast<SOCKET>(sock),
                                 level,
                                 optname,
                                 reinterpret_cast<char*>(optval),
                                 &sl);
    if (ret == 0) {
        *optlen = static_cast<uint32_t>(sl);
    }
    return (ret == SOCKET_ERROR) ? -1 : 0;
}

int PlatformSocketSetNonBlocking(PlatformSocket sock, int nonblocking) noexcept {
    u_long mode = static_cast<u_long>(nonblocking != 0 ? 1 : 0);
    const int ret = ::ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &mode);
    return (ret == SOCKET_ERROR) ? -1 : 0;
}

int PlatformSocketPoll(PlatformSocket sock,
                       int events,
                       int timeout_ms) noexcept {
    // WASM イベントビット: bit0=readable, bit1=writable, bit2=error
    WSAPOLLFD pfd;
    pfd.fd = static_cast<SOCKET>(sock);
    pfd.events = 0;
    pfd.revents = 0;

    if (events & 0x01) pfd.events |= POLLRDNORM;
    if (events & 0x02) pfd.events |= POLLWRNORM;
    if (events & 0x04) pfd.events |= POLLERR;

    const int nfds = ::WSAPoll(&pfd, 1, timeout_ms);
    if (nfds == SOCKET_ERROR) return -1;
    if (nfds == 0) return 0;

    int ready = 0;
    if (pfd.revents & POLLRDNORM) ready |= 0x01;
    if (pfd.revents & POLLWRNORM) ready |= 0x02;
    if (pfd.revents & (POLLERR | POLLHUP)) ready |= 0x04;
    return ready;
}

// ---------------------------------------------------------------------------
// アドレスユーティリティ
// ---------------------------------------------------------------------------

uint32_t PlatformInetAddr(const char* addr_str) noexcept {
    // inet_addr は INADDR_NONE (0xFFFFFFFF) をエラー値として返す
    const u_long addr = ::inet_addr(addr_str);
    if (addr == INADDR_NONE) return 0U;
    return static_cast<uint32_t>(addr);
}

uint16_t PlatformHostToNetShort(uint16_t host_short) noexcept {
    return ::htons(host_short);
}

uint16_t PlatformNetToHostShort(uint16_t net_short) noexcept {
    return ::ntohs(net_short);
}

int PlatformSocketGetLastError() noexcept {
    return ::WSAGetLastError();
}

}  // namespace socket
}  // namespace hostmodules
}  // namespace embwasm
