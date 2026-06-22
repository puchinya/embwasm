// HTTP サーバーデモ (WASM 側実装)
// ポート 50050 で HTTP/1.0 リクエストを受け付け、固定レスポンスを返す。
//
// ソケット操作は wasi:sockets/sockets ホストモジュール経由で行う。
// SocketAccept と SocketRecv は協調スケジューラの非同期対応によって
// 他スレッドをブロックせず、ネイティブスレッドもスリープ状態を保つ。

#include <stdint.h>
#include "wasm_host_api.h"

// ---------------------------------------------------------------------------
// ソケットアドレス構造体 (sockaddr_in 互換、16 バイト固定レイアウト)
// ---------------------------------------------------------------------------
typedef struct {
    uint16_t sin_family;   // AF_INET = 2
    uint16_t sin_port;     // ネットワークバイトオーダーのポート番号
    uint32_t sin_addr;     // IPv4 アドレス (0 = INADDR_ANY)
    uint8_t  sin_zero[8];  // パディング
} SockAddrIn;

// ---------------------------------------------------------------------------
// 定数
// ---------------------------------------------------------------------------
#define AF_INET      2
#define SOCK_STREAM  1
#define SOL_SOCKET   0xffff  // macOS/BSD: 0xffff
#define SO_REUSEADDR 0x0004  // macOS/BSD: 4
#define SERVER_PORT  50050
#define BACKLOG      8
#define RECV_BUF_SIZE 2048

// ---------------------------------------------------------------------------
// 固定 HTTP レスポンス
// ---------------------------------------------------------------------------
static const char k_http_response[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 20\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello from embwasm!\n";

static const int k_http_response_len = sizeof(k_http_response) - 1;

// ---------------------------------------------------------------------------
// 受信バッファ（静的確保）
// ---------------------------------------------------------------------------
static char g_recv_buf[RECV_BUF_SIZE];

// ---------------------------------------------------------------------------
// エクスポートされるメイン関数
// ---------------------------------------------------------------------------
__attribute__((export_name("main")))
int main(void) {
    // 1. ソケット作成
    int32_t server_fd = wasi_sockets_sockets_socket_create(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return 1;
    }

    // 2. SO_REUSEADDR を設定（アドレス再利用で即時再起動を可能にする）
    int32_t opt_val = 1;
    wasi_sockets_sockets_socket_set_opt(
        server_fd,
        SOL_SOCKET, SO_REUSEADDR,
        (int32_t)(unsigned int)&opt_val,
        (int32_t)sizeof(opt_val));

    // 3. バインド (0.0.0.0:50050)
    SockAddrIn addr;
    addr.sin_family  = AF_INET;
    addr.sin_port    = (uint16_t)wasi_sockets_sockets_host_to_net_short(SERVER_PORT);
    addr.sin_addr    = 0;  // INADDR_ANY
    addr.sin_zero[0] = 0; addr.sin_zero[1] = 0; addr.sin_zero[2] = 0; addr.sin_zero[3] = 0;
    addr.sin_zero[4] = 0; addr.sin_zero[5] = 0; addr.sin_zero[6] = 0; addr.sin_zero[7] = 0;

    int32_t bind_ret = wasi_sockets_sockets_socket_bind(
        server_fd,
        (int32_t)(unsigned int)&addr,
        (int32_t)sizeof(addr));
    if (bind_ret < 0) {
        wasi_sockets_sockets_socket_close(server_fd);
        return 2;
    }

    // 4. リッスン
    int32_t listen_ret = wasi_sockets_sockets_socket_listen(server_fd, BACKLOG);
    if (listen_ret < 0) {
        wasi_sockets_sockets_socket_close(server_fd);
        return 3;
    }

    // 5. 接続ループ
    while (1) {
        // accept (非同期: 接続があるまで協調スケジューラが他スレッドを実行)
        int32_t client_fd = wasi_sockets_sockets_socket_accept(server_fd, 0, 0);
        if (client_fd < 0) {
            continue;
        }

        // recv (非同期: データが届くまでスリープ)
        int32_t n = wasi_sockets_sockets_socket_recv(
            client_fd,
            (int32_t)(unsigned int)g_recv_buf,
            RECV_BUF_SIZE - 1,
            0);

        if (n > 0) {
            g_recv_buf[n] = '\0';
            // HTTP レスポンスを送信
            wasi_sockets_sockets_socket_send(
                client_fd,
                (int32_t)(unsigned int)k_http_response,
                k_http_response_len,
                0);
        }

        wasi_sockets_sockets_socket_close(client_fd);
    }

    // unreachable
    wasi_sockets_sockets_socket_close(server_fd);
    return 0;
}
