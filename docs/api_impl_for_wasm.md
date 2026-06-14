# WASM向け公開API (Host API) の実装方法

このドキュメントでは、本プロジェクトのC++実行環境から、WASMモジュール内に提供する公開API（インポート関数/ホスト関数）の実装および登録方法について解説します。

---

## 1. ホストAPIの基本設計

ベアメタル環境の制約（**STL禁止、例外処理禁止、RTTI禁止**）に従い、ホストAPIは以下のシグネチャを持つ静的C++関数として定義します。

```cpp
WasmResult HostFunction(
    const WasmValue* args,      // WASM側から渡された引数の配列
    uint32_t arg_count,         // 引数の個数
    WasmValue* results,         // WASM側へ返却する戻り値の配列
    uint32_t result_count,      // 戻り値の個数
    void* user_data             // 任意のコンテキストポインタ（オプション）
);
```

### 例外処理とエラーハンドリング
例外が利用できないため、関数自体の実行の成否は `WasmResult` のステータスコードを返します。
正常に値が計算できた場合は `WasmResult::kOk` を返し、値は `results` 配列に格納します。

---

## 2. APIの実装手順

### 手順 1: C++関数の定義
例えば、WASMモジュールに対して「センサーから現在値を取得するAPI（`get_sensor_value`）」と「値をLEDに書き込むAPI（`write_led_value`）」を提供したい場合のコード例です。

```cpp
#include "wasm_types.h"
#include <iostream> // 組み込み環境では代替のログ出力

namespace {

// センサー値を読み込むホストAPI
// WASMシグネチャ: (import "env" "get_sensor_value" (func (result i32)))
embwasm::WasmResult GetSensorValue(
    const embwasm::WasmValue* args, 
    uint32_t arg_count, 
    embwasm::WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)args;      // 引数なし
    (void)arg_count;
    (void)user_data;

    if (result_count < 1) {
        return embwasm::WasmResult::kErrorRuntimeError;
    }

    // 擬似的なセンサー値の読み出し
    int32_t dummy_sensor_val = 42;

    // 戻り値を設定
    results[0].type = embwasm::WasmType::kI32;
    results[0].value.i32 = dummy_sensor_val;

    return embwasm::WasmResult::kOk;
}

// LEDに出力するホストAPI
// WASMシグネチャ: (import "env" "write_led_value" (func (param i32)))
embwasm::WasmResult WriteLedValue(
    const embwasm::WasmValue* args, 
    uint32_t arg_count, 
    embwasm::WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results;      // 戻り値なし
    (void)result_count;
    (void)user_data;

    if (arg_count < 1 || args[0].type != embwasm::WasmType::kI32) {
        return embwasm::WasmResult::kErrorRuntimeError;
    }

    int32_t led_state = args[0].value.i32;
    
    // ベアメタル環境ではここでハードウェアレジスタを操作
    // (例: GPIO_WriteBit(LED_PORT, LED_PIN, led_state);)
    std::cout << "[HOST API] LED State changed to: " << led_state << std::endl;

    return embwasm::WasmResult::kOk;
}

} // namespace
```

### 手順 2: レジストリへの登録
定義したホストAPIを `WasmApiRegistry` に登録して、WASMエンジンから見つけられるようにします。

```cpp
#include "wasm_api.h"

void RegisterApis(embwasm::WasmApiRegistry& registry) {
    // モジュール名 "env", フィールド名 "get_sensor_value" として登録
    registry.Register("env", "get_sensor_value", GetSensorValue);

    // モジュール名 "env", フィールド名 "write_led_value" として登録
    registry.Register("env", "write_led_value", WriteLedValue);
}
```

---

## 3. WASMモジュール側の記述 (WAT)

WASM（WebAssembly Text Format）側で上記で定義した API をインポートして呼び出す記述例です。

```wat
(module
  ;; ホストAPIのインポート定義
  (import "env" "get_sensor_value" (func $get_sensor (result i32)))
  (import "env" "write_led_value" (func $write_led (param i32)))

  ;; メインの処理
  (func (export "run_control_loop")
    ;; センサー値を取得
    call $get_sensor
    
    ;; センサー値が30より大きければ LED を ON(1) に、以下なら OFF(0) にする
    (if (i32.gt_s (i32.const 30))
      (then
        (i32.const 1)
        call $write_led)
      (else
        (i32.const 0)
        call $write_led))
  )
)
```

---

## 4. メモリ管理と設定値の変更

### 専用メモリプールの役割
ホストAPIがパースされる際、モジュール名や関数名などの文字列を安全に保持するため、`WasmMemoryPool` を利用してメモリが確保されます。
メモリが不足すると API の登録に失敗し、`WasmResult::kErrorOutOfMemory` を返します。

### メモリプールの変更方法
メモリ不足が発生した場合は、`include/wasm_config.h` の設定値を書き換えてビルドしてください。

* **[include/wasm_config.h](file:///Users/nabeshimamasataka/CLionProjects/embwasm/include/wasm_config.h)**:
  - `kMemoryPoolSize`: メモリプールの容量（デフォルト: 64KB）。
  - `kMaxHostApis`: 登録できるホスト関数の上限（デフォルト: 16個）。
