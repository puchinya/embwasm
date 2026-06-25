# 導入ガイド：WASMエンジン使用の流れ (docs/getting_started.md)

このドキュメントでは、本プロジェクトのマイコン向け極小WASM実行ライブラリをプロジェクトに導入し、WASMバイナリをロード・実行するまでの全体的な開発手順について解説します。

---

## 1. 全体フロー

WASMを実行するまでの基本的な手順は以下の通りです。

```mermaid
graph TD
    A[1. WIT ファイルにインポート関数とメタデータを定義] --> B[2. C++ヘッダーにホスト関数の前方宣言を追加]
    B --> C[3. tools/codegen/gen_api.py を実行して静的検索コードを生成]
    C --> D[4. ホストAPIの実装を追加]
    D --> E[5. メモリプールとエンジンの初期化・WASMバイナリのロード]
    E --> F[6. InstantiateModules() でインポートを解決]
    F --> G[7. WASMのエクスポート関数を実行]
```

---

## 2. 各ステップの詳細

### ステップ 1: API設定ファイル (WIT) の記述
WASMモジュール内で呼び出すホストAPIを設定します。
* **WIT ファイル**（例: `hostapi.wit`）に、`import` 関数と、それに対応するC++関数名（`/// @cpp-func:`）およびインクルードヘッダー（`/// @cpp-header:`）を記述します。
* 指定したヘッダー（例: `host_apis.hpp`）に、ホスト関数のC++プロトタイプ宣言を記述します。

### ステップ 2: 静的検索コードの生成
WITファイルを元に、二分探索用のC++ソースコードを自動生成します。
以下のコマンドを実行します。

```bash
python3 tools/codegen/gen_api.py hostapi.wit src/wasm_api_static.cpp include/wasm_api_static.hpp
```
これにより、指定したヘッダー（例: `host_apis.hpp`）を `#include` した、静的ルックアップ処理用の `src/wasm_api_static.cpp` が生成されます。

### ステップ 3: プラットフォーム（OS）の選択
プロジェクトの対象ターゲット（OS/ベアメタル）に応じたプラットフォームファイルをビルド対象に選択します。
`CMakeLists.txt` の以下のフラグを環境に合わせて設定します。

* **Windows**: 自動的に `windows/wasm_platform.cpp` を選択
* **macOS**: 自動的に `macos/wasm_platform.cpp` を選択
* **FreeRTOS**: CMakeオプション `USE_FREERTOS=ON` を指定し `freertos/wasm_platform.cpp` を選択
* **μITRON**: CMakeオプション `USE_UITRON=ON` を指定し `uitron/wasm_platform.cpp` を選択

### ステップ 4: ホストAPIの実装
C++側で、WASMから呼び出されるホストAPIを実装します。
ホスト関数のシグネチャは `HostFunction` 型定義（`include/wasm_types.hpp`）に従います。

```cpp
// ホスト関数のシグネチャ
// typedef WasmResult (*HostFunction)(
//     const WasmValue* args, uint32_t arg_count,
//     WasmValue* results,    uint32_t result_count,
//     void* user_data);

embwasm::WasmResult MyHostFunc(
    const embwasm::WasmValue* args,
    uint32_t arg_count,
    embwasm::WasmValue* results,
    uint32_t result_count,
    void* user_data) noexcept
{
    // 実装...
    return embwasm::WasmResult::kOk;
}
```

詳細は [docs/api_impl_for_wasm.md](api_impl_for_wasm.md) を参照してください。

### ステップ 5: メモリプールとエンジンの初期化
動的ヒープを使用しないため、外部の静的バッファをメモリプールに渡してエンジンに渡します。
メモリプールの容量は `include/wasm_config.hpp` の `kMemoryPoolSize` で定義されます。
実行時の設定（スタックサイズ等）は `WasmEngineConfig` で変更できます。

```cpp
#include "embwasm.hpp"

// 1. 静的バッファとメモリプールの作成
static uint8_t g_pool_buf[embwasm::kMemoryPoolSize];
embwasm::WasmMemoryPool pool;
pool.Init(g_pool_buf, sizeof(g_pool_buf));

// 2. WASMエンジンの初期化（デフォルト設定）
embwasm::WasmEngine engine;
engine.Init(pool);

// 2b. カスタム設定で初期化する場合
embwasm::WasmEngineConfig config;
config.stack_size      = embwasm::kUnifiedStackSize;  // データスタック + ローカル変数の合計
config.call_stack_size = embwasm::kWasmCallStackSize;
config.labels_pool_size = embwasm::kLabelsPoolSize;
engine.Init(pool, config);
```

### ステップ 6: WASMバイナリのロードとインスタンス化
バイト配列としてのWASMバイナリをロードし、インポートを解決します。

```cpp
// WASMバイナリデータのロード (モジュール名 "default" として登録)
int32_t instance_id = engine.LoadModule("default", 7, kWasmBinary, sizeof(kWasmBinary));
if (instance_id < 0) {
    // ロードエラー処理（戻り値は負の WasmResult 値）
}

// ロード済みの全モジュールのインポートを解決してインスタンス化
embwasm::WasmResult inst_res = engine.InstantiateModules();
if (embwasm::IsError(inst_res)) {
    // インスタンス化エラー処理
}
```

### ステップ 7: WASMエクスポート関数の実行

#### 通常の実行（名前解決あり）

```cpp
embwasm::WasmValue result;

// Execute(module_name, module_name_len, func_name, func_name_len, args, arg_count, results, result_count)
embwasm::WasmResult exec_res = engine.Execute("default", 7, "run", 3, nullptr, 0, &result, 1);
if (exec_res == embwasm::WasmResult::kOk) {
    // 実行成功。result.value.i32 等で結果を参照できます。
}
```

#### ホットパス実行（インデックス直接指定）

同一関数を繰り返し呼ぶ場合は、モジュール名・関数名のルックアップを省略できる `ExecuteByIndex()` を使用してください。

```cpp
// 事前にインデックスを解決しておく
int32_t func_idx = engine.GetExportFunctionIndex("default", 7, "run", 3);

// 以降はインデックスで直接呼び出す
embwasm::WasmValue result;
embwasm::WasmResult exec_res = engine.ExecuteByIndex(instance_id, func_idx, nullptr, 0, &result, 1);
```

---

## 3. ビルドと実行

プロジェクト全体は CMake で構成されています。ビルドとデモプログラムの実行は以下で行います。

```bash
# ビルド（ビルドディレクトリは cmake-build-debug）
cmake -B cmake-build-debug -S .
cmake --build cmake-build-debug

# デモの実行
./cmake-build-debug/demo/hello/embwasm_demo_hello

# ベンチマークデモの実行
./cmake-build-debug/demo/benchmark/embwasm_demo_benchmark
```
