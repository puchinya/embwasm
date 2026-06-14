// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// Licensed under the MIT License.
//
// platform/macos/socket_platform.cpp
//   macOS / POSIX 向けソケットプラットフォーム実装。
//   BSD Sockets API (sys/socket.h, netinet/in.h, arpa/inet.h, fcntl.h, poll.h)
//   を使用する。
//
//   このファイルは macOS 以外の POSIX 準拠環境 (Linux 等) でも利用できる。
// =============================================================================

#include "wasi_socket_platform.hpp"

// POSIX / macOS ヘッダー
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace embwasm {
namespace hostmodules {
namespace socket {

// ---------------------------------------------------------------------------
// 初期化 / 終了（POSIX では不要）
// ---------------------------------------------------------------------------

bool PlatformSocketInit() noexcept {
    // POSIX ソケットは初期化不要
    return true;
}

void PlatformSocketDeinit() noexcept {
    // POSIX ソケットは終了処理不要
}

// ---------------------------------------------------------------------------
// ソケット操作
// ---------------------------------------------------------------------------

PlatformSocket PlatformSocketCreate(int domain, int type, int protocol) noexcept {
    const int fd = ::socket(domain, type, protocol);
    if (fd < 0) {
        return kInvalidPlatformSocket;
    }
    return static_cast<PlatformSocket>(fd);
}

int PlatformSocketClose(PlatformSocket sock) noexcept {
    return ::close(static_cast<int>(sock));
}

int PlatformSocketBind(PlatformSocket sock,
                       const uint8_t* addr_buf,
                       uint32_t addr_len) noexcept {
    // WASM 側の WasiSockAddrIn は sockaddr_in と同じレイアウトなので
    // そのままキャストして使用できる。
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    return ::bind(static_cast<int>(sock), sa,
                  static_cast<socklen_t>(addr_len));
}

int PlatformSocketListen(PlatformSocket sock, int backlog) noexcept {
    return ::listen(static_cast<int>(sock), backlog);
}

PlatformSocket PlatformSocketAccept(PlatformSocket sock,
                                    uint8_t* peer_addr_buf,
                                    uint32_t* peer_addr_len) noexcept {
    if (peer_addr_buf && peer_addr_len) {
        struct sockaddr* sa = reinterpret_cast<struct sockaddr*>(peer_addr_buf);
        socklen_t sa_len = static_cast<socklen_t>(*peer_addr_len);
        const int fd = ::accept(static_cast<int>(sock), sa, &sa_len);
        if (fd < 0) return kInvalidPlatformSocket;
        *peer_addr_len = static_cast<uint32_t>(sa_len);
        return static_cast<PlatformSocket>(fd);
    } else {
        const int fd = ::accept(static_cast<int>(sock), nullptr, nullptr);
        if (fd < 0) return kInvalidPlatformSocket;
        return static_cast<PlatformSocket>(fd);
    }
}

int PlatformSocketConnect(PlatformSocket sock,
                          const uint8_t* addr_buf,
                          uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    return ::connect(static_cast<int>(sock), sa,
                     static_cast<socklen_t>(addr_len));
}

int PlatformSocketSend(PlatformSocket sock,
                       const uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept {
    const ssize_t sent = ::send(static_cast<int>(sock),
                                reinterpret_cast<const void*>(buf),
                                static_cast<std::size_t>(len),
                                flags);
    return static_cast<int>(sent);
}

int PlatformSocketRecv(PlatformSocket sock,
                       uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept {
    const ssize_t recvd = ::recv(static_cast<int>(sock),
                                 reinterpret_cast<void*>(buf),
                                 static_cast<std::size_t>(len),
                                 flags);
    return static_cast<int>(recvd);
}

int PlatformSocketSendTo(PlatformSocket sock,
                         const uint8_t* buf,
                         uint32_t len,
                         int flags,
                         const uint8_t* addr_buf,
                         uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    const ssize_t sent = ::sendto(static_cast<int>(sock),
                                  reinterpret_cast<const void*>(buf),
                                  static_cast<std::size_t>(len),
                                  flags,
                                  sa,
                                  static_cast<socklen_t>(addr_len));
    return static_cast<int>(sent);
}

int PlatformSocketRecvFrom(PlatformSocket sock,
                           uint8_t* buf,
                           uint32_t len,
                           int flags,
                           uint8_t* src_addr_buf,
                           uint32_t* src_addr_len) noexcept {
    if (src_addr_buf && src_addr_len) {
        struct sockaddr* sa = reinterpret_cast<struct sockaddr*>(src_addr_buf);
        socklen_t sa_len = static_cast<socklen_t>(*src_addr_len);
        const ssize_t recvd = ::recvfrom(static_cast<int>(sock),
                                         reinterpret_cast<void*>(buf),
                                         static_cast<std::size_t>(len),
                                         flags,
                                         sa,
                                         &sa_len);
        if (recvd >= 0) {
            *src_addr_len = static_cast<uint32_t>(sa_len);
        }
        return static_cast<int>(recvd);
    } else {
        const ssize_t recvd = ::recvfrom(static_cast<int>(sock),
                                         reinterpret_cast<void*>(buf),
                                         static_cast<std::size_t>(len),
                                         flags,
                                         nullptr,
                                         nullptr);
        return static_cast<int>(recvd);
    }
}

int PlatformSocketSetOpt(PlatformSocket sock,
                         int level,
                         int optname,
                         const uint8_t* optval,
                         uint32_t optlen) noexcept {
    return ::setsockopt(static_cast<int>(sock),
                        level,
                        optname,
                        reinterpret_cast<const void*>(optval),
                        static_cast<socklen_t>(optlen));
}

int PlatformSocketGetOpt(PlatformSocket sock,
                         int level,
                         int optname,
                         uint8_t* optval,
                         uint32_t* optlen) noexcept {
    socklen_t sl = static_cast<socklen_t>(*optlen);
    const int ret = ::getsockopt(static_cast<int>(sock),
                                 level,
                                 optname,
                                 reinterpret_cast<void*>(optval),
                                 &sl);
    if (ret == 0) {
        *optlen = static_cast<uint32_t>(sl);
    }
    return ret;
}

int PlatformSocketSetNonBlocking(PlatformSocket sock, int nonblocking) noexcept {
    const int fd = static_cast<int>(sock);
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return ::fcntl(fd, F_SETFL, flags);
}

int PlatformSocketPoll(PlatformSocket sock,
                       int events,
                       int timeout_ms) noexcept {
    // WASM イベントビット: bit0=readable, bit1=writable, bit2=error
    struct pollfd pfd;
    pfd.fd = static_cast<int>(sock);
    pfd.events = 0;
    pfd.revents = 0;

    if (events & 0x01) pfd.events |= POLLIN;
    if (events & 0x02) pfd.events |= POLLOUT;
    if (events & 0x04) pfd.events |= POLLERR;

    const int nfds = ::poll(&pfd, 1, timeout_ms);
    if (nfds < 0) return -1;
    if (nfds == 0) return 0;

    int ready = 0;
    if (pfd.revents & POLLIN)  ready |= 0x01;
    if (pfd.revents & POLLOUT) ready |= 0x02;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) ready |= 0x04;
    return ready;
}

// ---------------------------------------------------------------------------
// アドレスユーティリティ
// ---------------------------------------------------------------------------

uint32_t PlatformInetAddr(const char* addr_str) noexcept {
    struct in_addr ia;
    if (::inet_aton(addr_str, &ia) == 0) {
        return 0U;
    }
    return static_cast<uint32_t>(ia.s_addr);
}

uint16_t PlatformHostToNetShort(uint16_t host_short) noexcept {
    return htons(host_short);
}

uint16_t PlatformNetToHostShort(uint16_t net_short) noexcept {
    return ntohs(net_short);
}

int PlatformSocketGetLastError() noexcept {
    return errno;
}

}  // namespace socket
}  // namespace hostmodules
}  // namespace embwasm
