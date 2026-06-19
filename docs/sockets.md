# WASI 互換ネットワークソケット機能

`embwasm` では、ベアメタル環境向けに WASI (WebAssembly System Interface) の `sockets` 提案に基づいたサブセットのホスト API を提供しています。

## 1. 概要

本機能により、WASM モジュール内から標準的なバークレーソケット API に近い形式で TCP/UDP 通信が可能になります。

### 特徴
- **動的メモリ割り当てゼロ**: ソケットハンドルおよび内部バッファは、コンパイル時に `include/wasm_config.hpp` で指定された上限数（`kMaxSockets`）まで静的に確保されます。
- **プラットフォーム抽象化**: ホスト側の BSD Socket API または LwIP などのスタックを抽象化し、WASM 側からは統一されたインターフェースで見えます。
- **ノンブロッキング対応**: `socket_poll` や `socket_set_non_blocking` を通じて、協調型マルチスレッドと組み合わせた非同期処理が可能です。

---

## 2. ホスト API インターフェース

WASM 側からは以下のホスト API を通じてネットワーク操作を行います。
詳細は `wasm_host_modules/sockets/sockets.wit` を参照してください。

### ライフサイクル
| 関数名 | 説明 |
|---|---|
| `socket_create` | 新しいソケットを作成します。 |
| `socket_close` | ソケットを閉じます。 |

### サーバー側操作
| 関数名 | 説明 |
|---|---|
| `socket_bind` | アドレスとポートを紐付けます。 |
| `socket_listen` | 接続待ち受けを開始します。 |
| `socket_accept` | 接続を受け付け、新しいソケットを返します。 |

### クライアント側操作 / データ送受信
| 関数名 | 説明 |
|---|---|
| `socket_connect` | リモートサーバーに接続します。 |
| `socket_send` / `socket_recv` | TCP データの送受信を行います。 |
| `socket_send_to` / `socket_recv_from` | UDP データの送受信を行います。 |

---

## 3. 実装例 (WASM/C)

```c
#include "wasm_api.h" // gen_api.py で生成されたヘッダー

void run_http_client() {
    // 1. ソケット作成 (AF_INET=2, SOCK_STREAM=1, IPPROTO_TCP=6)
    int sock = socket_create(2, 1, 6);
    
    // 2. 接続先アドレスの設定 (127.0.0.1:80)
    struct sockaddr_in addr;
    addr.sin_family = 2;
    addr.sin_port = host_to_net_short(80);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 3. 接続
    if (socket_connect(sock, &addr, sizeof(addr)) == 0) {
        // 4. データ送信
        const char* msg = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        socket_send(sock, msg, strlen(msg), 0);
        
        // 5. データ受信
        char buf[128];
        int len = socket_recv(sock, buf, sizeof(buf), 0);
        // ... 処理
    }

    // 6. クローズ
    socket_close(sock);
}
```

---

## 4. ホスト側 (C++) の設定

ホスト側では `InitializeAllHostModules` を呼び出すか、個別に `socket::Initialize` を呼び出します。
`InitializeAllHostModules` は `gen_api.py` が自動生成する `wasm_api_static.cpp` に含まれます。

```cpp
static uint8_t g_pool_buf[embwasm::kMemoryPoolSize];
embwasm::WasmMemoryPool pool;
pool.Init(g_pool_buf, sizeof(g_pool_buf));

embwasm::WasmEngine engine;
engine.Init(pool);

// ソケットモジュールの初期化 (内部で静的テーブルが準備される)
embwasm::InitializeAllHostModules(engine);

engine.Load("default", 7, wasm_binary, wasm_size);
engine.Execute("default", 7, "run_http_client", 15, nullptr, 0, nullptr, 0);

engine.Deinit();
pool.Deinit();
```

---

## 5. 設定定数 (`include/wasm_config.hpp`)

- `kMaxSockets`: 同時にオープン可能な最大ソケット数（デフォルト: 4）。
- `kSocketBufferSize`: UDP 等で使用する内部一時バッファのサイズ。
