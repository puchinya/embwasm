// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// Licensed under the MIT License.
//
// platform/lwip/socket_platform.cpp
//   lwIP 2.x 向けソケットプラットフォーム実装。
//
//   lwIP の BSD ソケット API レイヤー (lwip/sockets.h) を使用する。
//   lwIP 本体はリポジトリに含まれていないため、ビルド時に外部ライブラリとして
//   リンクすること。ビルドシステムで以下のインクルードパスを追加してください:
//     -I<lwip-src>/src/include
//     -I<lwip-src>/src/include/ipv4
//
//   lwIP の設定ファイル (lwipopts.h) で以下を有効にする必要があります:
//     #define LWIP_SOCKET 1
//     #define LWIP_COMPAT_SOCKETS 0   // lwip/sockets.h の関数を直接使用
//
//   ノンブロッキングモードは lwip_fcntl() / O_NONBLOCK で設定する。
//   poll 相当は lwip_select() で実現する（lwIP には poll が無い）。
// =============================================================================

#include "wasi_socket_platform.hpp"

// lwIP BSD ソケット API ヘッダー
// lwIP 本体のインクルードパスが通っている必要がある。
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/init.h>
#include <lwip/errno.h>

#include <cstdint>
#include <cstring>

namespace embwasm {
namespace hostmodules {
namespace socket {

// ---------------------------------------------------------------------------
// 初期化 / 終了
// ---------------------------------------------------------------------------

bool PlatformSocketInit() noexcept {
    // lwip_init() はシステム起動時に一度だけ呼ばれることを想定しているが、
    // 複数回呼んでも安全な実装が多い。
    // ネットワークインタフェースの設定・起動はアプリケーション側で行うこと。
    ::lwip_init();
    return true;
}

void PlatformSocketDeinit() noexcept {
    // lwIP には対応する終了 API が存在しない。
    // ネットワークインタフェースの停止はアプリケーション側で行うこと。
}

// ---------------------------------------------------------------------------
// ソケット操作
// ---------------------------------------------------------------------------

PlatformSocket PlatformSocketCreate(int domain, int type, int protocol) noexcept {
    const int fd = ::lwip_socket(domain, type, protocol);
    if (fd < 0) {
        return kInvalidPlatformSocket;
    }
    return static_cast<PlatformSocket>(fd);
}

int PlatformSocketClose(PlatformSocket sock) noexcept {
    return ::lwip_close(static_cast<int>(sock));
}

int PlatformSocketBind(PlatformSocket sock,
                       const uint8_t* addr_buf,
                       uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    return ::lwip_bind(static_cast<int>(sock), sa,
                       static_cast<socklen_t>(addr_len));
}

int PlatformSocketListen(PlatformSocket sock, int backlog) noexcept {
    return ::lwip_listen(static_cast<int>(sock), backlog);
}

PlatformSocket PlatformSocketAccept(PlatformSocket sock,
                                    uint8_t* peer_addr_buf,
                                    uint32_t* peer_addr_len) noexcept {
    if (peer_addr_buf && peer_addr_len) {
        struct sockaddr* sa = reinterpret_cast<struct sockaddr*>(peer_addr_buf);
        socklen_t sa_len = static_cast<socklen_t>(*peer_addr_len);
        const int fd = ::lwip_accept(static_cast<int>(sock), sa, &sa_len);
        if (fd < 0) return kInvalidPlatformSocket;
        *peer_addr_len = static_cast<uint32_t>(sa_len);
        return static_cast<PlatformSocket>(fd);
    } else {
        const int fd = ::lwip_accept(static_cast<int>(sock), nullptr, nullptr);
        if (fd < 0) return kInvalidPlatformSocket;
        return static_cast<PlatformSocket>(fd);
    }
}

int PlatformSocketConnect(PlatformSocket sock,
                          const uint8_t* addr_buf,
                          uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    return ::lwip_connect(static_cast<int>(sock), sa,
                          static_cast<socklen_t>(addr_len));
}

int PlatformSocketSend(PlatformSocket sock,
                       const uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept {
    return ::lwip_send(static_cast<int>(sock),
                       reinterpret_cast<const void*>(buf),
                       static_cast<size_t>(len),
                       flags);
}

int PlatformSocketRecv(PlatformSocket sock,
                       uint8_t* buf,
                       uint32_t len,
                       int flags) noexcept {
    return ::lwip_recv(static_cast<int>(sock),
                       reinterpret_cast<void*>(buf),
                       static_cast<size_t>(len),
                       flags);
}

int PlatformSocketSendTo(PlatformSocket sock,
                         const uint8_t* buf,
                         uint32_t len,
                         int flags,
                         const uint8_t* addr_buf,
                         uint32_t addr_len) noexcept {
    const struct sockaddr* sa =
        reinterpret_cast<const struct sockaddr*>(addr_buf);
    return ::lwip_sendto(static_cast<int>(sock),
                         reinterpret_cast<const void*>(buf),
                         static_cast<size_t>(len),
                         flags,
                         sa,
                         static_cast<socklen_t>(addr_len));
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
        const int recvd = ::lwip_recvfrom(static_cast<int>(sock),
                                          reinterpret_cast<void*>(buf),
                                          static_cast<size_t>(len),
                                          flags,
                                          sa,
                                          &sa_len);
        if (recvd >= 0) {
            *src_addr_len = static_cast<uint32_t>(sa_len);
        }
        return recvd;
    } else {
        return ::lwip_recvfrom(static_cast<int>(sock),
                               reinterpret_cast<void*>(buf),
                               static_cast<size_t>(len),
                               flags,
                               nullptr,
                               nullptr);
    }
}

int PlatformSocketSetOpt(PlatformSocket sock,
                         int level,
                         int optname,
                         const uint8_t* optval,
                         uint32_t optlen) noexcept {
    return ::lwip_setsockopt(static_cast<int>(sock),
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
    const int ret = ::lwip_getsockopt(static_cast<int>(sock),
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
    int flags = ::lwip_fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return ::lwip_fcntl(fd, F_SETFL, flags);
}

int PlatformSocketPoll(PlatformSocket sock,
                       int events,
                       int timeout_ms) noexcept {
    // lwIP には poll() が存在しないため、select() で代替する。
    // WASM イベントビット: bit0=readable, bit1=writable, bit2=error
    const int fd = static_cast<int>(sock);

    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    fd_set* p_rfds    = nullptr;
    fd_set* p_wfds    = nullptr;
    fd_set* p_efds    = nullptr;

    if (events & 0x01) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        p_rfds = &readfds;
    }
    if (events & 0x02) {
        FD_ZERO(&writefds);
        FD_SET(fd, &writefds);
        p_wfds = &writefds;
    }
    if (events & 0x04) {
        FD_ZERO(&exceptfds);
        FD_SET(fd, &exceptfds);
        p_efds = &exceptfds;
    }

    struct timeval tv;
    struct timeval* p_tv = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec  = static_cast<long>(timeout_ms / 1000);
        tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
        p_tv = &tv;
    }

    const int nfds = ::lwip_select(fd + 1, p_rfds, p_wfds, p_efds, p_tv);
    if (nfds < 0) return -1;
    if (nfds == 0) return 0;

    int ready = 0;
    if (p_rfds && FD_ISSET(fd, p_rfds)) ready |= 0x01;
    if (p_wfds && FD_ISSET(fd, p_wfds)) ready |= 0x02;
    if (p_efds && FD_ISSET(fd, p_efds)) ready |= 0x04;
    return ready;
}

// ---------------------------------------------------------------------------
// アドレスユーティリティ
// ---------------------------------------------------------------------------

uint32_t PlatformInetAddr(const char* addr_str) noexcept {
    // lwIP の inet_aton 相当
    ip4_addr_t addr;
    if (::ip4addr_aton(addr_str, &addr) == 0) {
        return 0U;
    }
    return static_cast<uint32_t>(addr.addr);
}

uint16_t PlatformHostToNetShort(uint16_t host_short) noexcept {
    return lwip_htons(host_short);
}

uint16_t PlatformNetToHostShort(uint16_t net_short) noexcept {
    return lwip_ntohs(net_short);
}

int PlatformSocketGetLastError() noexcept {
    return errno;
}

}  // namespace socket
}  // namespace hostmodules
}  // namespace embwasm
