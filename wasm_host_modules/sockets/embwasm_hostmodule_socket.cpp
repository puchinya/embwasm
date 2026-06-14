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
//   これにより動的メモリ割り当てを完全に排除する。
//
//   プラットフォーム依存の操作はすべて wasi_socket_platform.hpp で宣言した
//   Platform* 関数群に委譲する（switch 文を使った直接呼び出し）。
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

namespace embwasm {
namespace hostmodules {
namespace socket {

// ---------------------------------------------------------------------------
// 設定定数（wasm_config.hpp の constexpr との一貫性を保つ）
// ---------------------------------------------------------------------------

/// 同時に開くことができるソケットの最大数。
/// 組み込み用途を想定して小さめに設定する。
#ifndef EMBWASM_MAX_SOCKETS
static constexpr int32_t kMaxSockets = 8;
#else
static constexpr int32_t kMaxSockets = EMBWASM_MAX_SOCKETS;
#endif

/// sockaddr_in 互換の WASM 側構造体サイズ（16 バイト固定）。
static constexpr uint32_t kAddrBufSize = kWasiSockAddrInSize;

// ---------------------------------------------------------------------------
// 静的ソケットハンドルテーブル
// ---------------------------------------------------------------------------

struct SocketEntry {
    bool in_use;
    PlatformSocket platform_sock;
};

// グローバル静的テーブル（動的メモリ不使用）
static SocketEntry s_socket_table[kMaxSockets];
static bool s_initialized = false;

// ---------------------------------------------------------------------------
// ハンドルテーブルのヘルパー関数
// ---------------------------------------------------------------------------

/// テーブルを初期化する。
static void InitTable() noexcept {
    for (int32_t i = 0; i < kMaxSockets; ++i) {
        s_socket_table[i].in_use = false;
        s_socket_table[i].platform_sock = kInvalidPlatformSocket;
    }
}

/// 空きスロットに PlatformSocket を登録し、仮想ハンドルを返す。
/// テーブルが満杯なら -1 を返す。
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

/// 仮想ハンドルを PlatformSocket に変換する。
/// 無効なハンドルなら kInvalidPlatformSocket を返す。
static PlatformSocket GetPlatformSocket(int32_t handle) noexcept {
    if (handle < 0 || handle >= kMaxSockets) {
        return kInvalidPlatformSocket;
    }
    if (!s_socket_table[handle].in_use) {
        return kInvalidPlatformSocket;
    }
    return s_socket_table[handle].platform_sock;
}

/// 仮想ハンドルをテーブルから解放する。
static void FreeHandle(int32_t handle) noexcept {
    if (handle >= 0 && handle < kMaxSockets) {
        s_socket_table[handle].in_use = false;
        s_socket_table[handle].platform_sock = kInvalidPlatformSocket;
    }
}

// ---------------------------------------------------------------------------
// WASM 線形メモリからバイト列を安全に読み出すヘルパー
// ---------------------------------------------------------------------------

/// WASM 線形メモリ上の offset から len バイトを dst へコピーする。
/// 範囲外アクセスの場合 false を返す。
static bool ReadMemory(WasmEngine& engine,
                       int32_t offset,
                       uint8_t* dst,
                       uint32_t len) noexcept {
    if (offset < 0) return false;
    uint8_t* mem = engine.GetLinearMemory();
    if (!mem) return false;
    const std::size_t mem_size = engine.GetLinearMemorySize();
    const uint32_t u_offset = static_cast<uint32_t>(offset);
    if (static_cast<std::size_t>(u_offset) + len > mem_size) return false;
    std::memcpy(dst, mem + u_offset, len);
    return true;
}

/// WASM 線形メモリ上の offset へ src の len バイトを書き込む。
/// 範囲外アクセスの場合 false を返す。
static bool WriteMemory(WasmEngine& engine,
                        int32_t offset,
                        const uint8_t* src,
                        uint32_t len) noexcept {
    if (offset < 0) return false;
    uint8_t* mem = engine.GetLinearMemory();
    if (!mem) return false;
    const std::size_t mem_size = engine.GetLinearMemorySize();
    const uint32_t u_offset = static_cast<uint32_t>(offset);
    if (static_cast<std::size_t>(u_offset) + len > mem_size) return false;
    std::memcpy(mem + u_offset, src, len);
    return true;
}

/// WASM 線形メモリ上の offset から sizeof(int32_t) を読み出す。
static bool ReadInt32(WasmEngine& engine,
                      int32_t offset,
                      int32_t* out) noexcept {
    uint8_t buf[4];
    if (!ReadMemory(engine, offset, buf, 4)) return false;
    int32_t val;
    std::memcpy(&val, buf, 4);
    *out = val;
    return true;
}

/// WASM 線形メモリ上の offset へ int32_t を書き込む。
static bool WriteInt32(WasmEngine& engine,
                       int32_t offset,
                       int32_t value) noexcept {
    uint8_t buf[4];
    std::memcpy(buf, &value, 4);
    return WriteMemory(engine, offset, buf, 4);
}

// ---------------------------------------------------------------------------
// モジュールライフサイクル
// ---------------------------------------------------------------------------

void Initialize(WasmEngine& engine) noexcept {
    (void)engine;
    if (s_initialized) return;
    InitTable();
    PlatformSocketInit();
    s_initialized = true;
}

void Deinitialize(WasmEngine& engine) noexcept {
    (void)engine;
    if (!s_initialized) return;
    // 開いたままのソケットをすべて閉じる
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
// ホストAPI実装
// ---------------------------------------------------------------------------

// ソケット作成
// WASM sig: (func (param i32 i32 i32) (result i32))
//   args[0] = domain, args[1] = type, args[2] = protocol
//   result: 仮想ソケットハンドル (>=0) またはエラー (-1)
WasmResult SocketCreate(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept {
    (void)engine;
    if (arg_count < 3 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int domain   = args[0].value.i32;
    const int type     = args[1].value.i32;
    const int protocol = args[2].value.i32;

    PlatformSocket ps = PlatformSocketCreate(domain, type, protocol);
    int32_t handle = -1;
    if (ps != kInvalidPlatformSocket) {
        handle = AllocHandle(ps);
        if (handle < 0) {
            // テーブルが満杯: プラットフォームソケットを即座に閉じてリソースリークを防ぐ
            PlatformSocketClose(ps);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = handle;
    return WasmResult::kOk;
}

// ソケットクローズ
// WASM sig: (func (param i32) (result i32))
//   args[0] = 仮想ソケットハンドル
WasmResult SocketClose(WasmEngine& engine,
                       const WasmValue* args, uint32_t arg_count,
                       WasmValue* results, uint32_t result_count) noexcept {
    (void)engine;
    if (arg_count < 1 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle = args[0].value.i32;
    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket) {
        ret = PlatformSocketClose(ps);
        FreeHandle(handle);
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// バインド
// WASM sig: (func (param i32 i32 i32) (result i32))
//   args[0] = 仮想ソケットハンドル
//   args[1] = WASM 線形メモリ上の sockaddr_in オフセット
//   args[2] = アドレス長
WasmResult SocketBind(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 3 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle   = args[0].value.i32;
    const int32_t addr_ptr = args[1].value.i32;
    const int32_t addr_len = args[2].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && addr_len > 0) {
        uint8_t addr_buf[kAddrBufSize];
        const uint32_t copy_len =
            (static_cast<uint32_t>(addr_len) < kAddrBufSize)
                ? static_cast<uint32_t>(addr_len)
                : kAddrBufSize;
        if (ReadMemory(engine, addr_ptr, addr_buf, copy_len)) {
            ret = PlatformSocketBind(ps, addr_buf, copy_len);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// リッスン
// WASM sig: (func (param i32 i32) (result i32))
//   args[0] = 仮想ソケットハンドル, args[1] = backlog
WasmResult SocketListen(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept {
    (void)engine;
    if (arg_count < 2 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle  = args[0].value.i32;
    const int     backlog = args[1].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket) {
        ret = PlatformSocketListen(ps, backlog);
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// アクセプト
// WASM sig: (func (param i32 i32 i32) (result i32))
//   args[0] = 仮想ソケットハンドル (サーバー)
//   args[1] = WASM 線形メモリ上のピアアドレス格納先オフセット (0 なら省略)
//   args[2] = WASM 線形メモリ上のアドレス長格納先オフセット (0 なら省略)
WasmResult SocketAccept(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 3 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle          = args[0].value.i32;
    const int32_t peer_addr_ptr   = args[1].value.i32;
    const int32_t peer_alen_ptr   = args[2].value.i32;

    PlatformSocket server_ps = GetPlatformSocket(handle);
    int32_t new_handle = -1;
    if (server_ps != kInvalidPlatformSocket) {
        uint8_t addr_buf[kAddrBufSize];
        uint32_t addr_len = kAddrBufSize;

        uint8_t* p_addr = nullptr;
        uint32_t* p_len = nullptr;
        if (peer_addr_ptr != 0) {
            p_addr = addr_buf;
            p_len  = &addr_len;
        }

        PlatformSocket client_ps = PlatformSocketAccept(server_ps, p_addr, p_len);
        if (client_ps != kInvalidPlatformSocket) {
            new_handle = AllocHandle(client_ps);
            if (new_handle < 0) {
                PlatformSocketClose(client_ps);
            } else if (p_addr && p_len) {
                // ピアアドレスを WASM 線形メモリへ書き戻す
                WriteMemory(engine, peer_addr_ptr, addr_buf, *p_len);
                WriteInt32(engine, peer_alen_ptr, static_cast<int32_t>(*p_len));
            }
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = new_handle;
    return WasmResult::kOk;
}

// 接続
// WASM sig: (func (param i32 i32 i32) (result i32))
//   args[0] = 仮想ソケットハンドル
//   args[1] = WASM 線形メモリ上の sockaddr_in オフセット
//   args[2] = アドレス長
WasmResult SocketConnect(WasmEngine& engine,
                         const WasmValue* args, uint32_t arg_count,
                         WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 3 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle   = args[0].value.i32;
    const int32_t addr_ptr = args[1].value.i32;
    const int32_t addr_len = args[2].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && addr_len > 0) {
        uint8_t addr_buf[kAddrBufSize];
        const uint32_t copy_len =
            (static_cast<uint32_t>(addr_len) < kAddrBufSize)
                ? static_cast<uint32_t>(addr_len)
                : kAddrBufSize;
        if (ReadMemory(engine, addr_ptr, addr_buf, copy_len)) {
            ret = PlatformSocketConnect(ps, addr_buf, copy_len);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// 送信
// WASM sig: (func (param i32 i32 i32 i32) (result i32))
//   args[0] = 仮想ソケットハンドル
//   args[1] = バッファオフセット
//   args[2] = バッファ長
//   args[3] = フラグ
WasmResult SocketSend(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 4 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle  = args[0].value.i32;
    const int32_t buf_ptr = args[1].value.i32;
    const int32_t buf_len = args[2].value.i32;
    const int     flags   = args[3].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && buf_len > 0 && buf_ptr >= 0) {
        uint8_t* mem = engine.GetLinearMemory();
        const std::size_t mem_size = engine.GetLinearMemorySize();
        const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
        const uint32_t u_len = static_cast<uint32_t>(buf_len);
        if (mem && static_cast<std::size_t>(u_ptr) + u_len <= mem_size) {
            ret = PlatformSocketSend(ps, mem + u_ptr, u_len, flags);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// 受信
// WASM sig: (func (param i32 i32 i32 i32) (result i32))
//   args[0] = 仮想ソケットハンドル
//   args[1] = バッファオフセット
//   args[2] = バッファ長
//   args[3] = フラグ
WasmResult SocketRecv(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 4 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle  = args[0].value.i32;
    const int32_t buf_ptr = args[1].value.i32;
    const int32_t buf_len = args[2].value.i32;
    const int     flags   = args[3].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && buf_len > 0 && buf_ptr >= 0) {
        uint8_t* mem = engine.GetLinearMemory();
        const std::size_t mem_size = engine.GetLinearMemorySize();
        const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
        const uint32_t u_len = static_cast<uint32_t>(buf_len);
        if (mem && static_cast<std::size_t>(u_ptr) + u_len <= mem_size) {
            ret = PlatformSocketRecv(ps, mem + u_ptr, u_len, flags);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// UDP 送信
// WASM sig: (func (param i32 i32 i32 i32 i32 i32) (result i32))
WasmResult SocketSendTo(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 6 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle   = args[0].value.i32;
    const int32_t buf_ptr  = args[1].value.i32;
    const int32_t buf_len  = args[2].value.i32;
    const int     flags    = args[3].value.i32;
    const int32_t addr_ptr = args[4].value.i32;
    const int32_t addr_len = args[5].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && buf_len > 0 && buf_ptr >= 0 && addr_len > 0) {
        uint8_t* mem = engine.GetLinearMemory();
        const std::size_t mem_size = engine.GetLinearMemorySize();
        const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
        const uint32_t u_len = static_cast<uint32_t>(buf_len);

        uint8_t addr_buf[kAddrBufSize];
        const uint32_t copy_len =
            (static_cast<uint32_t>(addr_len) < kAddrBufSize)
                ? static_cast<uint32_t>(addr_len)
                : kAddrBufSize;
        if (mem && static_cast<std::size_t>(u_ptr) + u_len <= mem_size &&
            ReadMemory(engine, addr_ptr, addr_buf, copy_len)) {
            ret = PlatformSocketSendTo(ps, mem + u_ptr, u_len, flags,
                                       addr_buf, copy_len);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// UDP 受信
// WASM sig: (func (param i32 i32 i32 i32 i32 i32) (result i32))
WasmResult SocketRecvFrom(WasmEngine& engine,
                          const WasmValue* args, uint32_t arg_count,
                          WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 6 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle         = args[0].value.i32;
    const int32_t buf_ptr        = args[1].value.i32;
    const int32_t buf_len        = args[2].value.i32;
    const int     flags          = args[3].value.i32;
    const int32_t src_addr_ptr   = args[4].value.i32;
    const int32_t src_alen_ptr   = args[5].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && buf_len > 0 && buf_ptr >= 0) {
        uint8_t* mem = engine.GetLinearMemory();
        const std::size_t mem_size = engine.GetLinearMemorySize();
        const uint32_t u_ptr = static_cast<uint32_t>(buf_ptr);
        const uint32_t u_len = static_cast<uint32_t>(buf_len);
        if (mem && static_cast<std::size_t>(u_ptr) + u_len <= mem_size) {
            uint8_t addr_buf[kAddrBufSize];
            uint32_t addr_len = kAddrBufSize;

            uint8_t* p_addr = (src_addr_ptr != 0) ? addr_buf : nullptr;
            uint32_t* p_len = (src_alen_ptr  != 0) ? &addr_len : nullptr;

            ret = PlatformSocketRecvFrom(ps, mem + u_ptr, u_len, flags,
                                         p_addr, p_len);
            if (ret >= 0 && p_addr && p_len) {
                WriteMemory(engine, src_addr_ptr, addr_buf, *p_len);
                WriteInt32(engine, src_alen_ptr, static_cast<int32_t>(*p_len));
            }
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// setsockopt
// WASM sig: (func (param i32 i32 i32 i32 i32) (result i32))
WasmResult SocketSetOpt(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 5 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle    = args[0].value.i32;
    const int     level     = args[1].value.i32;
    const int     optname   = args[2].value.i32;
    const int32_t optval_ptr = args[3].value.i32;
    const int32_t optlen    = args[4].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && optlen > 0 && optval_ptr >= 0) {
        // オプション値は最大 16 バイトとして扱う
        static constexpr uint32_t kOptBufMax = 16U;
        uint8_t optval_buf[kOptBufMax];
        const uint32_t copy_len =
            (static_cast<uint32_t>(optlen) < kOptBufMax)
                ? static_cast<uint32_t>(optlen)
                : kOptBufMax;
        if (ReadMemory(engine, optval_ptr, optval_buf, copy_len)) {
            ret = PlatformSocketSetOpt(ps, level, optname, optval_buf, copy_len);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// getsockopt
// WASM sig: (func (param i32 i32 i32 i32 i32) (result i32))
WasmResult SocketGetOpt(WasmEngine& engine,
                        const WasmValue* args, uint32_t arg_count,
                        WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 5 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle      = args[0].value.i32;
    const int     level       = args[1].value.i32;
    const int     optname     = args[2].value.i32;
    const int32_t optval_ptr  = args[3].value.i32;
    const int32_t optlen_ptr  = args[4].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket && optval_ptr >= 0 && optlen_ptr >= 0) {
        int32_t optlen_val = 0;
        if (ReadInt32(engine, optlen_ptr, &optlen_val) && optlen_val > 0) {
            static constexpr uint32_t kOptBufMax = 16U;
            uint8_t optval_buf[kOptBufMax];
            uint32_t optlen_u =
                (static_cast<uint32_t>(optlen_val) < kOptBufMax)
                    ? static_cast<uint32_t>(optlen_val)
                    : kOptBufMax;
            ret = PlatformSocketGetOpt(ps, level, optname, optval_buf, &optlen_u);
            if (ret == 0) {
                WriteMemory(engine, optval_ptr, optval_buf, optlen_u);
                WriteInt32(engine, optlen_ptr, static_cast<int32_t>(optlen_u));
            }
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// ノンブロッキングモード設定
// WASM sig: (func (param i32 i32) (result i32))
WasmResult SocketSetNonBlocking(WasmEngine& engine,
                                const WasmValue* args, uint32_t arg_count,
                                WasmValue* results, uint32_t result_count) noexcept {
    (void)engine;
    if (arg_count < 2 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle      = args[0].value.i32;
    const int     nonblocking = args[1].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket) {
        ret = PlatformSocketSetNonBlocking(ps, nonblocking);
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// ソケットポーリング
// WASM sig: (func (param i32 i32 i32) (result i32))
WasmResult SocketPoll(WasmEngine& engine,
                      const WasmValue* args, uint32_t arg_count,
                      WasmValue* results, uint32_t result_count) noexcept {
    (void)engine;
    if (arg_count < 3 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t handle     = args[0].value.i32;
    const int     events     = args[1].value.i32;
    const int     timeout_ms = args[2].value.i32;

    PlatformSocket ps = GetPlatformSocket(handle);
    int ret = -1;
    if (ps != kInvalidPlatformSocket) {
        ret = PlatformSocketPoll(ps, events, timeout_ms);
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = ret;
    return WasmResult::kOk;
}

// inet_addr
// WASM sig: (func (param i32) (result i32))
//   args[0] = WASM 線形メモリ上のアドレス文字列オフセット
WasmResult InetAddr(WasmEngine& engine,
                    const WasmValue* args, uint32_t arg_count,
                    WasmValue* results, uint32_t result_count) noexcept {
    if (arg_count < 1 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const int32_t str_ptr = args[0].value.i32;
    uint32_t addr = 0U;

    if (str_ptr >= 0) {
        uint8_t* mem = engine.GetLinearMemory();
        const std::size_t mem_size = engine.GetLinearMemorySize();
        const uint32_t u_ptr = static_cast<uint32_t>(str_ptr);
        if (mem && u_ptr < mem_size) {
            // 文字列の終端を確認（最大 16 文字 = "255.255.255.255\0"）
            static constexpr uint32_t kAddrStrMax = 16U;
            char addr_str[kAddrStrMax];
            uint32_t len = 0U;
            while (len < kAddrStrMax - 1U && u_ptr + len < mem_size) {
                const char c = static_cast<char>(mem[u_ptr + len]);
                addr_str[len] = c;
                ++len;
                if (c == '\0') break;
            }
            addr_str[len] = '\0';
            addr = PlatformInetAddr(addr_str);
        }
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = static_cast<int32_t>(addr);
    return WasmResult::kOk;
}

// htons
// WASM sig: (func (param i32) (result i32))
WasmResult HostToNetShort(WasmEngine& engine,
                          const WasmValue* args, uint32_t arg_count,
                          WasmValue* results, uint32_t result_count) noexcept {
    (void)engine;
    if (arg_count < 1 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const uint16_t host_short =
        static_cast<uint16_t>(args[0].value.i32 & 0xFFFF);
    results[0].type = WasmType::kI32;
    results[0].value.i32 =
        static_cast<int32_t>(PlatformHostToNetShort(host_short));
    return WasmResult::kOk;
}

// ntohs
// WASM sig: (func (param i32) (result i32))
WasmResult NetToHostShort(WasmEngine& engine,
                          const WasmValue* args, uint32_t arg_count,
                          WasmValue* results, uint32_t result_count) noexcept {
    (void)engine;
    if (arg_count < 1 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    const uint16_t net_short =
        static_cast<uint16_t>(args[0].value.i32 & 0xFFFF);
    results[0].type = WasmType::kI32;
    results[0].value.i32 =
        static_cast<int32_t>(PlatformNetToHostShort(net_short));
    return WasmResult::kOk;
}

// 最後のエラーコード取得
// WASM sig: (func (result i32))
WasmResult SocketGetLastError(WasmEngine& engine,
                              const WasmValue* args, uint32_t arg_count,
                              WasmValue* results, uint32_t result_count) noexcept {
    (void)engine; (void)args; (void)arg_count;
    if (result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    results[0].type = WasmType::kI32;
    results[0].value.i32 = PlatformSocketGetLastError();
    return WasmResult::kOk;
}

}  // namespace socket
}  // namespace hostmodules
}  // namespace embwasm
