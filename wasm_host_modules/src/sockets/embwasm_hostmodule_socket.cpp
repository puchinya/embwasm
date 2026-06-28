// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// Licensed under the MIT License.
//
// embwasm_hostmodule_socket.cpp
//   WASI:sockets ホストモジュールの実装。
//
//   WASM 側には 0-based の「仮想ソケットハンドル」(int32_t) を公開し、
//   内部では静的テーブル (kMaxSockets エントリ) でプラットフォーム固有の
//   ソケット記述子にマッピングする。
//
//   POSIX 環境 (macOS/Linux) では、socket_accept と socket_recv を
//   非同期化する：バックグラウンド I/O マネージャスレッドが select() で
//   複数 fd を監視し、完了時にスタックへ結果を積んで ThreadNotify を呼ぶ。
//
// ベアメタル制約:
//   - STL 禁止 / 例外禁止 / RTTI 禁止
//   - 動的メモリ割り当て禁止
//   - 再帰呼び出し禁止
//   - 関数ポインタ禁止
// =============================================================================

#include "embwasm_hostmodule_socket.hpp"
#include "wasi_socket_platform.hpp"
#include "wasm_engine.hpp"

#include <cstdint>
#include <cstring>

#if !defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace embwasm {
namespace hostmodules {
namespace wasi {
namespace sockets {
namespace sockets {

// ---------------------------------------------------------------------------
// 設定定数
// ---------------------------------------------------------------------------

#ifndef EMBWASM_MAX_SOCKETS
static constexpr int32_t kMaxSockets = 8;
#else
static constexpr int32_t kMaxSockets = EMBWASM_MAX_SOCKETS;
#endif

static constexpr uint32_t kAddrBufSize = kWasiSockAddrInSize;

// ---------------------------------------------------------------------------
// 静的ソケットハンドルテーブル
// ---------------------------------------------------------------------------

struct SocketEntry {
    bool in_use;
    PlatformSocket platform_sock;
};

static SocketEntry s_socket_table[kMaxSockets];
static bool s_initialized = false;

// ---------------------------------------------------------------------------
// ハンドルテーブルヘルパー
// ---------------------------------------------------------------------------

static void InitTable() noexcept {
    for (int32_t i = 0; i < kMaxSockets; ++i) {
        s_socket_table[i].in_use = false;
        s_socket_table[i].platform_sock = kInvalidPlatformSocket;
    }
}

static int32_t AllocHandle(PlatformSocket ps) noexcept {
    for (int32_t i = 0; i < kMaxSockets; ++i) {
        if (!s_socket_table[i].in_use) {
            s_socket_table[i].in_use = true;
            s_socket_table[i].platform_sock = ps;
            return i;
        }
    }
    return -1;
}

static PlatformSocket GetPlatformSocket(int32_t handle) noexcept {
    if (handle < 0 || handle >= kMaxSockets) return kInvalidPlatformSocket;
    if (!s_socket_table[handle].in_use) return kInvalidPlatformSocket;
    return s_socket_table[handle].platform_sock;
}

static void FreeHandle(int32_t handle) noexcept {
    if (handle >= 0 && handle < kMaxSockets) {
        s_socket_table[handle].in_use = false;
        s_socket_table[handle].platform_sock = kInvalidPlatformSocket;
    }
}

// ---------------------------------------------------------------------------
// WASM 線形メモリヘルパー
// ---------------------------------------------------------------------------

static bool ReadMemory(WasmEngine& engine, int32_t offset, uint8_t* dst, uint32_t len) noexcept {
    if (offset < 0) return false;
    uint8_t* mem;
    std::size_t mem_size;
    GetLinearMemoryForHostApi(engine, mem, mem_size);
    if (!mem) return false;
    if (static_cast<std::size_t>(static_cast<uint32_t>(offset)) + len > mem_size) return false;
    std::memcpy(dst, mem + static_cast<uint32_t>(offset), len);
    return true;
}

static bool WriteMemory(WasmEngine& engine, int32_t offset, const uint8_t* src, uint32_t len) noexcept {
    if (offset < 0) return false;
    uint8_t* mem;
    std::size_t mem_size;
    GetLinearMemoryForHostApi(engine, mem, mem_size);
    if (!mem) return false;
    if (static_cast<std::size_t>(static_cast<uint32_t>(offset)) + len > mem_size) return false;
    std::memcpy(mem + static_cast<uint32_t>(offset), src, len);
    return true;
}

static bool WriteInt32(WasmEngine& engine, int32_t offset, int32_t value) noexcept {
    uint8_t buf[4];
    std::memcpy(buf, &value, 4);
    return WriteMemory(engine, offset, buf, 4);
}

// ---------------------------------------------------------------------------
// POSIX 非同期 I/O マネージャ
// ---------------------------------------------------------------------------

#if !defined(_WIN32)

static constexpr uint8_t kAsyncModeAccept = 0;
static constexpr uint8_t kAsyncModeRecv   = 1;

struct AsyncSocketOp {
    int fd;            ///< プラットフォームソケット fd
    uint8_t mode;      ///< kAsyncModeAccept or kAsyncModeRecv
    uint8_t* buf;      ///< recv 時: WASM 線形メモリへのポインタ
    uint32_t buf_len;
    int flags;
    uint32_t thread_id;
    WasmEngine* engine;
    WasmThreadContext* ctx;
    bool active;
};

static AsyncSocketOp g_async_ops[kMaxSockets];
static pthread_mutex_t g_async_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_wake_pipe[2] = {-1, -1};
static volatile bool g_io_running = false;
static pthread_t g_io_thread;

static void WakeIoManager() noexcept {
    if (g_wake_pipe[1] >= 0) {
        char c = 1;
        (void)::write(g_wake_pipe[1], &c, 1);
    }
}

static void* IoManagerThread(void*) {
    while (g_io_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int max_fd = g_wake_pipe[0];
        if (g_wake_pipe[0] >= 0) {
            FD_SET(g_wake_pipe[0], &rfds);
        }

        pthread_mutex_lock(&g_async_mutex);
        for (int i = 0; i < kMaxSockets; ++i) {
            if (g_async_ops[i].active) {
                FD_SET(g_async_ops[i].fd, &rfds);
                if (g_async_ops[i].fd > max_fd) max_fd = g_async_ops[i].fd;
            }
        }
        pthread_mutex_unlock(&g_async_mutex);

        struct timeval tv;
        tv.tv_sec  = 5;
        tv.tv_usec = 0;

        int r = ::select(max_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        // wakeup pipe をドレイン
        if (g_wake_pipe[0] >= 0 && FD_ISSET(g_wake_pipe[0], &rfds)) {
            char tmp[16];
            (void)::read(g_wake_pipe[0], tmp, sizeof(tmp));
        }

        // 準備できた op を処理
        pthread_mutex_lock(&g_async_mutex);
        for (int i = 0; i < kMaxSockets; ++i) {
            if (!g_async_ops[i].active) continue;
            if (!FD_ISSET(g_async_ops[i].fd, &rfds)) continue;

            // op をコピーしてスロットを解放（ロック保持中）
            AsyncSocketOp op = g_async_ops[i];
            g_async_ops[i].active = false;
            pthread_mutex_unlock(&g_async_mutex);

            // I/O 実行（ロックなし）
            int32_t result;
            if (op.mode == kAsyncModeAccept) {
                int new_fd = ::accept(op.fd, nullptr, nullptr);
                if (new_fd < 0) {
                    result = -1;
                } else {
                    result = AllocHandle(static_cast<PlatformSocket>(new_fd));
                    if (result < 0) {
                        ::close(new_fd);
                    }
                }
            } else {
                // kAsyncModeRecv
                ssize_t n = ::recv(op.fd, op.buf, static_cast<size_t>(op.buf_len), op.flags);
                result = static_cast<int32_t>(n);
            }

            // 結果をスタックへ積んでから ThreadNotify（IP 巻き戻し不要）
            op.ctx->stack[op.ctx->stack_top++] = WasmValue::FromI32(result);
            op.engine->ThreadNotify(op.thread_id);

            pthread_mutex_lock(&g_async_mutex);
        }
        pthread_mutex_unlock(&g_async_mutex);
    }
    return nullptr;
}

static void StartIoManager() noexcept {
    for (int i = 0; i < kMaxSockets; ++i) {
        g_async_ops[i].active = false;
    }
    if (::pipe(g_wake_pipe) != 0) {
        g_wake_pipe[0] = g_wake_pipe[1] = -1;
    }
    g_io_running = true;
    pthread_create(&g_io_thread, nullptr, IoManagerThread, nullptr);
}

static void StopIoManager() noexcept {
    g_io_running = false;
    WakeIoManager();
    pthread_join(g_io_thread, nullptr);
    if (g_wake_pipe[0] >= 0) { ::close(g_wake_pipe[0]); g_wake_pipe[0] = -1; }
    if (g_wake_pipe[1] >= 0) { ::close(g_wake_pipe[1]); g_wake_pipe[1] = -1; }
}

static bool RegisterAsyncAccept(WasmEngine& engine, int32_t handle) noexcept {
    PlatformSocket ps = GetPlatformSocket(handle);
    if (ps == kInvalidPlatformSocket) return false;

    WasmThreadContext* ctx = engine.GetCurrentThreadContext();
    if (!ctx) return false;

    pthread_mutex_lock(&g_async_mutex);
    for (int i = 0; i < kMaxSockets; ++i) {
        if (!g_async_ops[i].active) {
            g_async_ops[i].fd        = static_cast<int>(ps);
            g_async_ops[i].mode      = kAsyncModeAccept;
            g_async_ops[i].buf       = nullptr;
            g_async_ops[i].buf_len   = 0;
            g_async_ops[i].flags     = 0;
            g_async_ops[i].thread_id = ctx->id;
            g_async_ops[i].engine    = &engine;
            g_async_ops[i].ctx       = ctx;
            g_async_ops[i].active    = true;
            pthread_mutex_unlock(&g_async_mutex);
            WakeIoManager();
            return true;
        }
    }
    pthread_mutex_unlock(&g_async_mutex);
    return false;
}

static bool RegisterAsyncRecv(WasmEngine& engine, int32_t handle,
                              uint8_t* buf, uint32_t buf_len, int flags) noexcept {
    PlatformSocket ps = GetPlatformSocket(handle);
    if (ps == kInvalidPlatformSocket) return false;

    WasmThreadContext* ctx = engine.GetCurrentThreadContext();
    if (!ctx) return false;

    pthread_mutex_lock(&g_async_mutex);
    for (int i = 0; i < kMaxSockets; ++i) {
        if (!g_async_ops[i].active) {
            g_async_ops[i].fd        = static_cast<int>(ps);
            g_async_ops[i].mode      = kAsyncModeRecv;
            g_async_ops[i].buf       = buf;
            g_async_ops[i].buf_len   = buf_len;
            g_async_ops[i].flags     = flags;
            g_async_ops[i].thread_id = ctx->id;
            g_async_ops[i].engine    = &engine;
            g_async_ops[i].ctx       = ctx;
            g_async_ops[i].active    = true;
            pthread_mutex_unlock(&g_async_mutex);
            WakeIoManager();
            return true;
        }
    }
    pthread_mutex_unlock(&g_async_mutex);
    return false;
}

#endif // !defined(_WIN32)

// ---------------------------------------------------------------------------
// モジュールライフサイクル
// ---------------------------------------------------------------------------

// [embwasm-proto:func:initialize]
void initialize(WasmEngine& engine) noexcept {
    (void)engine;
    if (s_initialized) return;
    InitTable();
    PlatformSocketInit();
#if !defined(_WIN32)
    StartIoManager();
#endif
    s_initialized = true;
}

// [embwasm-proto:func:deinitialize]
void deinitialize(WasmEngine& engine) noexcept {
    (void)engine;
    if (!s_initialized) return;
#if !defined(_WIN32)
    StopIoManager();
#endif
    for (int32_t i = 0; i < kMaxSockets; ++i) {
        if (s_socket_table[i].in_use) {
            PlatformSocketClose(s_socket_table[i].platform_sock);
            s_socket_table[i].in_use = false;
            s_socket_table[i].platform_sock = kInvalidPlatformSocket;
        }
    }
    PlatformSocketDeinit();
    s_initialized = false;
}

// ---------------------------------------------------------------------------
// ホスト API 実装（型付きシグネチャ）
// ---------------------------------------------------------------------------

// [embwasm-proto:func:socket_create]
WasmResult socket_create(WasmEngine& engine,
                         int32_t domain, int32_t type, int32_t protocol,
                         int32_t& out_result) noexcept {
    (void)engine;
    PlatformSocket ps = PlatformSocketCreate(domain, type, protocol);
    if (ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    int32_t handle = AllocHandle(ps);
    if (handle < 0) {
        PlatformSocketClose(ps);
    }
    out_result = handle;
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_close]
WasmResult socket_close(WasmEngine& engine,
                        int32_t sock,
                        int32_t& out_result) noexcept {
    (void)engine;
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    int ret = PlatformSocketClose(ps);
    FreeHandle(sock);
    out_result = ret;
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_bind]
WasmResult socket_bind(WasmEngine& engine,
                       int32_t sock, int32_t addr_ptr, int32_t addr_len,
                       int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket || addr_len <= 0) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t addr_buf[kAddrBufSize];
    uint32_t copy_len = (static_cast<uint32_t>(addr_len) < kAddrBufSize)
                        ? static_cast<uint32_t>(addr_len) : kAddrBufSize;
    if (!ReadMemory(engine, addr_ptr, addr_buf, copy_len)) {
        out_result = -1;
        return WasmResult::kOk;
    }
    out_result = PlatformSocketBind(ps, addr_buf, copy_len);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_listen]
WasmResult socket_listen(WasmEngine& engine,
                         int32_t sock, int32_t backlog,
                         int32_t& out_result) noexcept {
    (void)engine;
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    out_result = PlatformSocketListen(ps, backlog);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_accept]
WasmResult socket_accept(WasmEngine& engine,
                         int32_t sock, int32_t peer_addr_ptr, int32_t peer_addr_len_ptr,
                         int32_t& out_result) noexcept {
    (void)peer_addr_ptr;
    (void)peer_addr_len_ptr;

#if !defined(_WIN32)
    // 非同期モード: I/O マネージャに委譲して kYield を返す
    if (RegisterAsyncAccept(engine, sock)) {
        WasmThreadContext* ctx = engine.GetCurrentThreadContext();
        if (ctx) {
            engine.ThreadWait(ctx->id);
            return WasmResult::kYield;
        }
    }
    // フォールバック: 同期 accept
#endif
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    PlatformSocket new_ps = PlatformSocketAccept(ps, nullptr, nullptr);
    if (new_ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    int32_t handle = AllocHandle(new_ps);
    if (handle < 0) {
        PlatformSocketClose(new_ps);
    }
    out_result = handle;
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_connect]
WasmResult socket_connect(WasmEngine& engine,
                          int32_t sock, int32_t addr_ptr, int32_t addr_len,
                          int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket || addr_len <= 0) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t addr_buf[kAddrBufSize];
    uint32_t copy_len = (static_cast<uint32_t>(addr_len) < kAddrBufSize)
                        ? static_cast<uint32_t>(addr_len) : kAddrBufSize;
    if (!ReadMemory(engine, addr_ptr, addr_buf, copy_len)) {
        out_result = -1;
        return WasmResult::kOk;
    }
    out_result = PlatformSocketConnect(ps, addr_buf, copy_len);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_send]
WasmResult socket_send(WasmEngine& engine,
                       int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                       int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket || buf_len <= 0) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t* mem;
    std::size_t mem_size;
    GetLinearMemoryForHostApi(engine, mem, mem_size);
    if (!mem) { out_result = -1; return WasmResult::kOk; }
    const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
    const uint32_t u_len = static_cast<uint32_t>(buf_len);
    if (static_cast<std::size_t>(u_ptr) + u_len > mem_size) {
        out_result = -1;
        return WasmResult::kOk;
    }
    out_result = PlatformSocketSend(ps, mem + u_ptr, u_len, flags);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_recv]
WasmResult socket_recv(WasmEngine& engine,
                       int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                       int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket || buf_len <= 0) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t* mem;
    std::size_t mem_size;
    GetLinearMemoryForHostApi(engine, mem, mem_size);
    if (!mem) { out_result = -1; return WasmResult::kOk; }
    const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
    const uint32_t u_len = static_cast<uint32_t>(buf_len);
    if (static_cast<std::size_t>(u_ptr) + u_len > mem_size) {
        out_result = -1;
        return WasmResult::kOk;
    }

#if !defined(_WIN32)
    // 非同期モード: I/O マネージャに委譲して kYield を返す
    if (RegisterAsyncRecv(engine, sock, mem + u_ptr, u_len, flags)) {
        WasmThreadContext* ctx = engine.GetCurrentThreadContext();
        if (ctx) {
            engine.ThreadWait(ctx->id);
            return WasmResult::kYield;
        }
    }
    // フォールバック: 同期 recv
#endif
    out_result = PlatformSocketRecv(ps, mem + u_ptr, u_len, flags);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_send_to]
WasmResult socket_send_to(WasmEngine& engine,
                          int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                          int32_t addr_ptr, int32_t addr_len,
                          int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket || buf_len <= 0) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t* mem;
    std::size_t mem_size;
    GetLinearMemoryForHostApi(engine, mem, mem_size);
    if (!mem) { out_result = -1; return WasmResult::kOk; }
    const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
    const uint32_t u_len = static_cast<uint32_t>(buf_len);
    if (static_cast<std::size_t>(u_ptr) + u_len > mem_size) {
        out_result = -1;
        return WasmResult::kOk;
    }

    uint8_t addr_buf[kAddrBufSize];
    uint32_t copy_len = 0;
    const uint8_t* addr_p = nullptr;
    if (addr_ptr > 0 && addr_len > 0) {
        copy_len = (static_cast<uint32_t>(addr_len) < kAddrBufSize)
                   ? static_cast<uint32_t>(addr_len) : kAddrBufSize;
        if (!ReadMemory(engine, addr_ptr, addr_buf, copy_len)) {
            out_result = -1;
            return WasmResult::kOk;
        }
        addr_p = addr_buf;
    }
    out_result = PlatformSocketSendTo(ps, mem + u_ptr, u_len, flags, addr_p, copy_len);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_recv_from]
WasmResult socket_recv_from(WasmEngine& engine,
                            int32_t sock, int32_t buf_ptr, int32_t buf_len, int32_t flags,
                            int32_t src_addr_ptr, int32_t src_addr_len_ptr,
                            int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket || buf_len <= 0) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t* mem;
    std::size_t mem_size;
    GetLinearMemoryForHostApi(engine, mem, mem_size);
    if (!mem) { out_result = -1; return WasmResult::kOk; }
    const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
    const uint32_t u_len = static_cast<uint32_t>(buf_len);
    if (static_cast<std::size_t>(u_ptr) + u_len > mem_size) {
        out_result = -1;
        return WasmResult::kOk;
    }

    uint8_t src_addr_buf[kAddrBufSize];
    uint32_t src_addr_len = (src_addr_ptr > 0) ? kAddrBufSize : 0;
    int ret = PlatformSocketRecvFrom(
        ps, mem + u_ptr, u_len, flags,
        (src_addr_ptr > 0) ? src_addr_buf : nullptr,
        (src_addr_ptr > 0) ? &src_addr_len : nullptr);

    if (ret >= 0 && src_addr_ptr > 0) {
        WriteMemory(engine, src_addr_ptr, src_addr_buf, src_addr_len);
        if (src_addr_len_ptr > 0) {
            WriteInt32(engine, src_addr_len_ptr, static_cast<int32_t>(src_addr_len));
        }
    }
    out_result = ret;
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_set_opt]
WasmResult socket_set_opt(WasmEngine& engine,
                          int32_t sock, int32_t level, int32_t optname,
                          int32_t optval_ptr, int32_t optlen,
                          int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket || optlen <= 0) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t opt_buf[64];
    uint32_t copy_len = (static_cast<uint32_t>(optlen) < 64u)
                        ? static_cast<uint32_t>(optlen) : 64u;
    if (!ReadMemory(engine, optval_ptr, opt_buf, copy_len)) {
        out_result = -1;
        return WasmResult::kOk;
    }
    out_result = PlatformSocketSetOpt(ps, level, optname, opt_buf, copy_len);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_get_opt]
WasmResult socket_get_opt(WasmEngine& engine,
                          int32_t sock, int32_t level, int32_t optname,
                          int32_t optval_ptr, int32_t optlen_ptr,
                          int32_t& out_result) noexcept {
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    uint8_t opt_buf[64];
    uint32_t opt_len = 64u;
    int ret = PlatformSocketGetOpt(ps, level, optname, opt_buf, &opt_len);
    if (ret == 0) {
        WriteMemory(engine, optval_ptr, opt_buf, opt_len);
        if (optlen_ptr > 0) {
            WriteInt32(engine, optlen_ptr, static_cast<int32_t>(opt_len));
        }
    }
    out_result = ret;
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_set_non_blocking]
WasmResult socket_set_non_blocking(WasmEngine& engine,
                                   int32_t sock, int32_t nonblocking,
                                   int32_t& out_result) noexcept {
    (void)engine;
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    out_result = PlatformSocketSetNonBlocking(ps, nonblocking);
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_poll]
WasmResult socket_poll(WasmEngine& engine,
                       int32_t sock, int32_t events, int32_t timeout_ms,
                       int32_t& out_result) noexcept {
    (void)engine;
    PlatformSocket ps = GetPlatformSocket(sock);
    if (ps == kInvalidPlatformSocket) {
        out_result = -1;
        return WasmResult::kOk;
    }
    out_result = PlatformSocketPoll(ps, events, timeout_ms);
    return WasmResult::kOk;
}

// [embwasm-proto:func:inet_addr]
WasmResult inet_addr(WasmEngine& engine,
                     int32_t addr_str_ptr,
                     int32_t& out_result) noexcept {
    if (addr_str_ptr < 0) {
        out_result = 0;
        return WasmResult::kOk;
    }
    uint8_t* mem;
    std::size_t mem_size_unused;
    GetLinearMemoryForHostApi(engine, mem, mem_size_unused);
    if (!mem) { out_result = 0; return WasmResult::kOk; }
    const char* addr_str = reinterpret_cast<const char*>(mem + static_cast<uint32_t>(addr_str_ptr));
    out_result = static_cast<int32_t>(PlatformInetAddr(addr_str));
    return WasmResult::kOk;
}

// [embwasm-proto:func:host_to_net_short]
WasmResult host_to_net_short(WasmEngine& engine,
                             int32_t host_short,
                             int32_t& out_result) noexcept {
    (void)engine;
    out_result = static_cast<int32_t>(
        PlatformHostToNetShort(static_cast<uint16_t>(host_short)));
    return WasmResult::kOk;
}

// [embwasm-proto:func:net_to_host_short]
WasmResult net_to_host_short(WasmEngine& engine,
                             int32_t net_short,
                             int32_t& out_result) noexcept {
    (void)engine;
    out_result = static_cast<int32_t>(
        PlatformNetToHostShort(static_cast<uint16_t>(net_short)));
    return WasmResult::kOk;
}

// [embwasm-proto:func:socket_get_last_error]
WasmResult socket_get_last_error(WasmEngine& engine,
                                 int32_t& out_result) noexcept {
    (void)engine;
    out_result = PlatformSocketGetLastError();
    return WasmResult::kOk;
}

// [embwasm-proto:funcs-end]
}  // namespace sockets
}  // namespace sockets
}  // namespace wasi
}  // namespace hostmodules
}  // namespace embwasm
