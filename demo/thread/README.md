# embwasm - Multithreading Demo

このデモは、ベアメタルマイコン向け極小WebAssembly実行エンジン（`embwasm`）を用いて、WASM内での協調型マルチスレッド（グリーンスレッド）およびイベント同期機能の動作を示すデモです。

---

## 1. デモの構成要素

* **[main.cpp](main.cpp)**:
  C++側のメインロジック。`WasmScheduler` を初期化し、WASMバイナリ内の `main` 関数をメインスレッドとして開始します。
* **[hostapi.wit](hostapi.wit)**:
  WASMがインポートするスレッド制御API（`thread_spawn`, `thread_yield`, `event_wait`, `event_signal`）やハローAPIと、ホスト側の関数のマッピングを定義します。
* **[wasm/main.c](wasm/main.c)**:
  WebAssembly側（C言語）のソースコード。`thread_spawn` でサブスレッドを生成し、`thread_yield` によるコンテキストスイッチや `event_wait`/`event_signal` による同期処理を行います。

---

## 2. 実行手順

プロジェクトのルートディレクトリで以下のコマンドを実行します：

```bash
# ビルド
cmake --build cmake-build-debug --target embwasm_demo_thread

# 実行
./cmake-build-debug/demo/thread/embwasm_demo_thread
```

---

## 3. 期待される実行出力

```text
=== Embedded WASM Engine Demo (Multithreading) ===
Memory Pool Size Configured: 65536 bytes

Loading WASM Binary...
WASM Loaded successfully.
Memory Used: 742 / 65536 bytes

Starting Scheduler with Multithreaded WASM...
Main func idx: 5, Thread2 func idx: 6
Main
T2:0
M1:0
T2:1
M1:1
T2:2
M1:2
Wait
Done

Execution finished successfully.
Max function nesting depth reached: 1
Max VM stack depth reached: 5
```
