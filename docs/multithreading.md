# WASM 協調型マルチスレッド機能

`embwasm` では、ベアメタル環境向けの軽量な協調型マルチスレッド（Green Threads / Fiber）機能を提供しています。

## 1. 概要

本機能は、標準の WebAssembly Threads 拡張（Shared Memory + Atomics）とは異なり、ホスト側のスケジューラが WASM 実行スタックとコールスタックをスレッドごとに退避・復元することで実現されます。

### 特徴
- **動的メモリ割り当てゼロ**: スレッドおよびイベントはコンパイル時に指定された上限数（`kMaxThreads`, `kMaxEvents`）まで静的に確保されます。
- **協調型スケジューリング**: 明示的な `yield` またはイベント待ち（`wait`）が発生したタイミングでコンテキストスイッチが行われます。
- **軽量**: 各スレッドは WASM 実行に必要な最小限のコンテキスト（スタック領域等）のみを保持します。

---

## 2. ホストAPI インターフェース

WASM側からは以下のホストAPIを通じてスレッド操作を行います。

| 関数名 | シグネチャ | 説明 |
|---|---|---|
| `thread_spawn` | `(func (param i32) (result i32))` | 新しいスレッドを起動します。引数はエクスポート関数のインデックスです。 |
| `thread_yield` | `(func)` | 実行権を他のスレッドに譲ります。 |
| `event_wait` | `(func (param i32))` | 指定した ID のイベントが発生するまで待機（ブロック）します。 |
| `event_signal` | `(func (param i32))` | 指定した ID のイベントを通知し、待機中のスレッドを再開させます。 |

---

## 3. 実装例 (WASM/C)

```c
// インポート関数の宣言
__attribute__((import_module("env"), import_name("thread_spawn")))
int thread_spawn(int func_idx);

__attribute__((import_module("env"), import_name("thread_yield")))
void thread_yield(void);

__attribute__((import_module("env"), import_name("event_wait")))
void event_wait(int event_id);

__attribute__((import_module("env"), import_name("event_signal")))
void event_signal(int event_id);

// サブスレッドの処理
__attribute__((export_name("worker")))
void worker(void) {
    // 処理...
    thread_yield(); // 他のスレッドへ
    // 完了を通知
    event_signal(1);
}

// メインスレッド
__attribute__((export_name("main")))
void main(void) {
    // インデックスを指定してスレッド起動
    thread_spawn(6); // worker関数のインデックス
    
    // workerの終了を待つ
    event_wait(1);
}
```

---

## 4. ホスト側 (C++) の設定

ホスト側では `WasmScheduler` を初期化して実行ループを回します。

```cpp
embwasm::WasmMemoryPool pool;
embwasm::WasmEngine engine;
engine.Init(pool);

engine.Load(wasm_binary, was_size);

// スケジューラの初期化
embwasm::WasmScheduler scheduler(engine);
scheduler.SetAsInstance(); // ホストAPIから参照可能にする

// メインスレッドの作成
int32_t main_idx = engine.GetExportFunctionIndex("main");
scheduler.CreateThread(main_idx);

// すべてのスレッドが終了するまで実行
scheduler.Run();
```

---

## 5. 設定定数 (`include/wasm_config.hpp`)

マルチスレッドに関する制限値は `wasm_config.hpp` で変更可能です。

- `kMaxThreads`: 同時に存在できる最大スレッド数（デフォルト: 4）。
- `kMaxEvents`: 利用可能なイベントオブジェクトの最大数（デフォルト: 8）。
- `kWasmStackSize`: 各スレッドごとのデータスタックサイズ。
- `kWasmCallStackSize`: 各スレッドごとのコールスタック深度。
